// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_viewer.hpp"
#include "o1602/core/profile.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include "fertility_view.hpp"
#include "tile_status.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/cursor_resource.hpp"
#include "o1602/assets/data_fixes.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/assets/high_scores.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/assets/unlock_levels.hpp"
#include "o1602/audio/package_music.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/png_writer.hpp"
#include "o1602/core/text.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/localization.hpp"
#include "o1602/ui/package_icons.hpp"
#ifdef O1602_EXPERIMENTAL_VITA
#include "o1602/platform/vita.hpp"
#endif

namespace o1602::app {

namespace {

/// Why a building cannot go here, in the player's language, for the status line.
std::string refusal_text(sim::BuildRefusal refusal) {
    const char* key = [refusal] {
        switch (refusal) {
            case sim::BuildRefusal::None:
                return "build.refusal.ok";
            case sim::BuildRefusal::UnknownBuilding:
                return "build.refusal.unknown";
            case sim::BuildRefusal::OutsideIsland:
                return "build.refusal.outside_island";
            case sim::BuildRefusal::Terrain:
                return "build.refusal.terrain";
            case sim::BuildRefusal::Territory:
                return "build.refusal.territory";
            case sim::BuildRefusal::ForeignCity:
                return "build.refusal.foreign_city";
            case sim::BuildRefusal::CityLimit:
                return "build.refusal.city_limit";
            case sim::BuildRefusal::SecondKontor:
                return "build.refusal.second_kontor";
            case sim::BuildRefusal::NoCoast:
                return "build.refusal.no_coast";
            case sim::BuildRefusal::NoDeposit:
                return "build.refusal.no_deposit";
            case sim::BuildRefusal::NoRock:
                return "build.refusal.no_rock";
            case sim::BuildRefusal::NoRiver:
                return "build.refusal.no_river";
            case sim::BuildRefusal::UnitInTheWay:
                return "build.refusal.unit_in_the_way";
            case sim::BuildRefusal::Locked:
                return "build.refusal.locked";
            case sim::BuildRefusal::Limit:
                return "build.refusal.limit";
            case sim::BuildRefusal::Infertile:
                return "build.refusal.infertile";
            case sim::BuildRefusal::Overlap:
                return "build.refusal.overlap";
            case sim::BuildRefusal::Money:
                return "build.refusal.money";
            case sim::BuildRefusal::Tools:
                return "build.refusal.tools";
            case sim::BuildRefusal::Wood:
                return "build.refusal.wood";
            case sim::BuildRefusal::Bricks:
                return "build.refusal.bricks";
            case sim::BuildRefusal::Cannons:
                return "build.refusal.cannons";
            case sim::BuildRefusal::ShipTooFar:
                return "build.refusal.ship_too_far";
        }
        return "build.refusal.unknown";
    }();
    return ui::tr(key);
}

/// Whether any tile of `item` lies in a city of `player` (the original's edge rule could take it).
bool touches_own_land(const sim::IslandMap& map, const sim::PlannedBuild& item, sim::PlayerId player) {
    std::uint32_t w = 1;
    std::uint32_t h = 1;
    sim::rotated_size(*item.type, item.rotation, w, h);
    for (std::uint32_t j = 0; j < h; ++j) {
        for (std::uint32_t i = 0; i < w; ++i) {
            const std::int32_t x = item.x + static_cast<std::int32_t>(i);
            const std::int32_t y = item.y + static_cast<std::int32_t>(j);
            if (map.contains(x, y) &&
                map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).owner == player) {
                return true;
            }
        }
    }
    return false;
}

/// A building in the build tool that only lacks gold or goods blinks: shown for this long, then
/// hidden for as long, as in the original.
constexpr std::uint64_t kShortOfMeansBlinkMs = 300;
/// Real time a frame may spend on ticks; the rest of the frame draws.
constexpr int kTickBudgetMs = 25;
/// The status line under a still mouse is worked out again this often (FB-1).
constexpr std::uint64_t kTileStatusMs = 100;
const char* zoom_name(render::Zoom zoom) {
    switch (zoom) {
        case render::Zoom::Large:
            return "GFX";
        case render::Zoom::Medium:
            return "MGFX";
        default:
            return "SGFX";
    }
}

std::string player_label(const sim::Player& p, sim::PlayerId slot, const render::BannerTable& banners,
                         const sim::World& world) {
    std::string label = "player " + std::to_string(slot);
    if (const render::Banner* b = banners.get(p.banner)) {
        const auto colon = b->id.find(':');
        label += " " + (colon == std::string::npos ? b->id : b->id.substr(colon + 1));
    }
    // A neutral slot by its faction ("anno1602:pirates"), the settlers by who steers them.
    return label + " (" + (p.role == sim::PlayerRole::Neutral ? world.faction(p).id : sim::role_name(p.role)) + ")";
}

}  // namespace

namespace {

/// What a new game starts with as the mods change it.
struct ModRules {
    std::vector<sim::Faction> factions = sim::base_factions();
    sim::Balance balance;
    /// The buildings the packages added, with the Ids `buildings` gave them.
    std::vector<assets::OwnBuilding> own;
    /// The original's goods and those the packages added (goods.toml).
    assets::Goods goods;
    /// The classes of inhabitants as the packages change and add them (classes.toml); empty when
    /// none does.
    std::vector<assets::ResidentClass> classes;
    /// The unlock levels as the packages change and add them (levels.toml); empty when none does.
    std::vector<assets::InfraLevel> levels;
    /// The images of the packages with their sprite numbers (graphics.toml).
    std::vector<assets::PackageImage> images;
    /// The figures the packages draw with their images (figure_images.toml).
    std::vector<assets::FigureLook> looks;
    /// The music of the packages (music.toml).
    std::vector<audio::MusicTrack> music;
};

/// The icons of `packages` (icons.toml), numbered in their load order.
std::vector<assets::PackageIcon> load_package_icons(std::span<const assets::ModPackage> packages,
                                                    std::vector<std::string>& problems) {
    std::vector<assets::PackageIcon> icons;
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kIconsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_icons_file(icons, package.id, package.folder, text, problems);
        }
    }
    return icons;
}

/// The base game's factions and numbers as the mods change them (<package folder>/factions.toml and
/// balance.toml), the packages in their load order; the buildings they add go into `buildings`
/// first (own_buildings.toml), so that a package can change another package's building too.
ModRules load_mod_rules(std::span<const assets::ModPackage> packages, assets::BuildingTable& buildings,
                        const assets::FigureTable& figures) {
    ModRules rules;
    std::vector<std::string> problems;
    // Goods first (with the icons they may show), then the classes and buildings that use them,
    // then the values that may name both. The icons' warnings come when the game takes them
    // (MapViewer::use_game_packages).
    std::vector<std::string> ignored;
    const std::vector<assets::PackageIcon> icons = load_package_icons(packages, ignored);
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kGoodsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_goods_file(rules.goods, package.id, text, problems, icons);
        }
    }
    // Then the classes that need them, then the buildings, among them houses of those classes.
    std::vector<assets::ResidentClass> classes = buildings.resident_classes();
    bool classes_changed = false;
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kClassesFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            classes_changed |= assets::apply_classes_file(classes, package.id, text, problems, rules.goods);
        }
    }
    if (classes_changed) {
        rules.classes = classes;
    }
    // Then the levels that count inhabitants of those classes, which the buildings may need.
    std::vector<assets::InfraLevel> levels = buildings.infra_levels();
    bool levels_changed = false;
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kLevelsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            levels_changed |= assets::apply_levels_file(levels, package.id, text, problems, classes);
        }
    }
    if (levels_changed) {
        rules.levels = levels;
    }
    // Then the images the buildings draw.
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kGraphicsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_graphics_file(rules.images, package.id, package.folder, text, problems);
        }
        if (const auto bytes = core::read_file(package.folder / assets::kFigureImagesFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_figure_images_file(rules.looks, package.id, text, rules.images, figures, problems);
        }
        if (const auto bytes = core::read_file(package.folder / audio::kMusicFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            audio::apply_music_file(rules.music, package.id, package.folder, text, problems);
        }
    }
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kOwnBuildingsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_own_buildings_file(buildings, rules.own, package.id, text, problems, rules.goods, classes,
                                             levels, rules.images);
        }
    }
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / sim::kFactionFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_faction_file(rules.factions, package.id, text, problems);
        }
        if (const auto bytes = core::read_file(package.folder / sim::kBalanceFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_balance_file(rules.balance, package.id, text, problems);
        }
        if (const auto bytes = core::read_file(package.folder / sim::kBuildingsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_buildings_file(rules.balance, package.id, text, problems, rules.own);
        }
        if (const auto bytes = core::read_file(package.folder / sim::kFiguresFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_figures_file(rules.balance, package.id, text, problems);
        }
    }
    sim::drop_unknown_buildings(rules.balance, buildings, problems);
    sim::drop_unknown_figures(rules.balance, figures, problems);
    for (const std::string& p : problems) {
        std::fprintf(stderr, "mods: %s\n", p.c_str());
    }
    return rules;
}

}  // namespace

core::Result<std::unique_ptr<MapViewer>> MapViewer::create(const assets::GameDirectory& game,
                                                           const ViewerOptions& options) {
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::phase("BEGIN original building / figure tables");
#endif
    auto buildings = assets::BuildingTable::load(game);
    if (!buildings) {
        return buildings.error();
    }
    auto figures = assets::FigureTable::load(game);
    if (!figures) {
        return figures.error();
    }
    // settings.cfg as it was left: how the window opens (the rest is read with load_options()) and
    // whether the original's data is corrected, before anything draws from the tables.
    const AppSettings remembered = load_user_settings(data_folder(), {},
#ifdef O1602_EXPERIMENTAL_VITA
        true
#else
        false
#endif
    );
    const bool data_fixes = remembered.data_fixes && !options.original_data;
    if (data_fixes) {
        std::vector<std::string_view> fixed = assets::fixes::fix_buildings(*buildings);
        for (const std::string_view id : assets::fixes::fix_figures(*figures)) {
            fixed.push_back(id);
        }
        for (const std::string_view id : fixed) {
            std::fprintf(stderr, "data fix %.*s\n", static_cast<int>(id.size()), id.data());
        }
    }
    std::vector<std::string> warnings;
    std::vector<assets::ModPackage> packages;
    {
        std::vector<std::string> problems;
        packages = assets::load_mod_packages(data_folder() / "mods", problems, remembered.disabled_mods);
        for (const std::string& p : problems) {
            std::fprintf(stderr, "mods: %s\n", p.c_str());
        }
        for (const assets::ModPackage& p : packages) {
            std::fprintf(stderr, "mods: package %s\n", assets::package_label(p.content()).c_str());
        }
    }
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::phase("BEGIN original world and rules");
#endif
    ModRules rules = load_mod_rules(packages, *buildings, *figures);
    core::Result<sim::World> world = core::make_error(core::ErrorCode::NotFound, "no game");
    const bool joining = options.multiplayer && options.multiplayer->role == ViewerOptions::Multiplayer::Role::Join;
    if (options.menu || joining) {
        world = sim::World(500, 350);  // the sea behind the menu until a game is chosen or comes from the host
        world->set_packages(assets::content_of(packages));
        world->set_own_buildings(rules.own);
        world->set_goods(rules.goods);
        world->set_resident_classes(rules.classes);
        world->set_infra_levels(rules.levels);
    } else if (options.new_game) {
        auto texts = assets::TextTable::load(game);
        if (!texts) {
            return texts.error();
        }
        world = sim::start_new_game(game, *buildings, *texts, *options.new_game, &warnings);
        if (world) {
            world->set_factions(rules.factions);
            world->set_balance(rules.balance);
            world->set_packages(assets::content_of(packages));
            world->set_own_buildings(rules.own);
            world->set_goods(rules.goods);
            world->set_resident_classes(rules.classes);
            world->set_infra_levels(rules.levels);
        }
    } else {
        auto bytes = core::read_file(options.scenario);
        if (!bytes) {
            return bytes.error();
        }
        if (sim::is_open1602_save(*bytes)) {
            world = sim::read_game(*bytes, *buildings, &warnings, &*figures);
        } else {
            auto file = assets::ChunkFile::from_bytes(std::move(bytes).value());
            if (!file) {
                return file.error();
            }
            world = sim::load_world(*file, *buildings, game, &warnings, options.world);
            if (world) {
                world->set_factions(rules.factions);  // the original's files have none of their own
                world->set_balance(rules.balance);
                world->set_packages(assets::content_of(packages));
                world->set_own_buildings(rules.own);
                world->set_goods(rules.goods);
                world->set_resident_classes(rules.classes);
                world->set_infra_levels(rules.levels);
            }
        }
    }
    if (!world) {
        return world.error();
    }
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    auto banners = render::BannerTable::original();
    banners.add_more_players();
    for (std::size_t i = 0; i < world->players().size(); ++i) {
        const auto& p = world->players()[i];
        std::fprintf(stderr, "%s, money %d\n", player_label(p, static_cast<sim::PlayerId>(i), banners, *world).c_str(),
                     p.money);
    }
    std::fprintf(stderr, "%u islands, %u ships\n", unsigned(world->islands().size()), unsigned(world->ships().size()));
    const auto palette_path = game.resolve(render::kTerrainPalettePath);
    if (!palette_path) {
        return core::make_error(core::ErrorCode::NotFound, "toolgfx/stadtfld.col not found");
    }
    auto palette = assets::Palette::load(*palette_path);
    if (!palette) {
        return palette.error();
    }

    std::unique_ptr<MapViewer> viewer(new MapViewer(game, std::move(buildings).value(), std::move(world).value(),
                                                    std::move(palette).value(), std::move(figures).value(), options));
    viewer->m_factions = std::move(rules.factions);
    viewer->m_balance = rules.balance;
    viewer->m_own_buildings = std::move(rules.own);
    viewer->m_goods = std::move(rules.goods);
    viewer->m_classes = std::move(rules.classes);
    viewer->m_levels = std::move(rules.levels);
    viewer->m_package_images = std::move(rules.images);
    viewer->m_figure_looks = std::move(rules.looks);
    assets::apply_figure_looks(viewer->m_figures, viewer->m_original_figures, viewer->m_figure_looks);
    viewer->m_packages = std::move(packages);
    {
        std::vector<std::string> problems;
        viewer->m_cached_packages = assets::cached_packages(data_folder() / "mods-cache", problems);
        for (const std::string& p : problems) {
            std::fprintf(stderr, "mods: %s\n", p.c_str());
        }
    }
    viewer->m_banners =
        render::BannerTable::original(&viewer->m_figures, render::BannerOptions{options.original_flags});
    viewer->m_banners.add_more_players();
    viewer->m_building_figures =
        render::collect_building_figures(viewer->m_state->world, viewer->m_buildings, viewer->m_figures);
    viewer->m_local_player = sim::first_human_player(viewer->m_state->world);
    if (auto texts = assets::TextTable::load(game)) {
        viewer->m_city_names = texts->section("STAEDTE");
        viewer->m_ship_names = texts->section("SHIPS");
        viewer->m_campaign_labels = texts->section("KAMPAGNE");
        viewer->m_events.set_ware_names(texts->section("WARE"));
        viewer->m_state->simulation.offer_names(viewer->m_city_names, viewer->m_ship_names);
    }
    viewer->m_soldiers.reset(viewer->m_state->world, viewer->m_buildings, viewer->m_figures);
    viewer->m_show_owners = options.show_owners;
    auto sdl = platform::SdlSystem::init();
    if (!sdl) {
        return sdl.error();
    }
    viewer->m_sdl = std::move(sdl).value();
    // The window opens as it was left; --size wins over the size remembered.
    viewer->m_app_settings.display = remembered.display;
#ifdef O1602_EXPERIMENTAL_VITA
    normalize_vita_display(viewer->m_app_settings.display);
#endif
    const DisplaySettings& display = viewer->m_app_settings.display;
    platform::WindowConfig config;
    config.title = "Open1602";
    config.width = options.width > 0 ? options.width : (display.window_width > 0 ? display.window_width : 1280);
    config.height = options.height > 0 ? options.height : (display.window_height > 0 ? display.window_height : 800);
    config.fullscreen = display.fullscreen && options.width <= 0;
#ifdef O1602_EXPERIMENTAL_VITA
    config.width=960;config.height=544;config.fullscreen=false;
#endif
    config.vsync = display.vsync;
    auto window = platform::Window::create(config);
    if (!window) {
        return window.error();
    }
    viewer->m_window = std::move(window).value();
#ifdef O1602_EXPERIMENTAL_VITA
    viewer->m_vita_cache=render::PagedSpriteCache::acquire(viewer->m_window->sdl_renderer());
#endif
#ifdef O1602_EXPERIMENTAL_VITA
    // A fixed 8 MiB target covers the existing 2x-screen render-plan budget at 960x544.
    // Reuse it through zooms; an exceptional oversized plan draws directly instead.
    viewer->m_map_canvas = std::make_unique<render::SdlMapCanvas>(viewer->m_window->sdl_renderer(),2048,1024);
#else
    viewer->m_map_canvas = std::make_unique<render::SdlMapCanvas>(viewer->m_window->sdl_renderer());
#endif
    viewer->m_software_renderer = viewer->m_window->renderer_name() == "software";
    viewer->load_cursors();
    // The viewer's own copy: GameSound keeps a pointer, and `game` may be a temporary (UI-15).
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::phase("BEGIN original audio");
#endif
    viewer->m_sound = std::make_unique<GameSound>(viewer->m_game, options.effects, options.music);
    // The messages and build menu of the game's packages, before the panels take the menu.
    viewer->use_game_packages();
    viewer->m_sound->set_messages(&viewer->m_messages);
    viewer->m_sound->set_package_music(std::move(rules.music));
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::phase("BEGIN original video controller");
#endif
    viewer->m_video = std::make_unique<VideoController>(
        options.videos ? std::make_unique<VideoPlayer>(viewer->m_window->sdl_renderer(), viewer->m_game) : nullptr,
        *viewer->m_sound);
    if (options.videos) {
        viewer->m_sound->set_video_check(
            [self = viewer.get()](std::uint16_t n) { return self->m_video->available(n); });
        if (options.intro) {
            // The start of the original: the two logos and the intro (0x004959AA, chained 8 -> 9 -> 58).
            for (const std::uint16_t n : std::array<std::uint16_t, 3>{8, 9, 58}) {
                viewer->m_video->request(n, 0, false, true);
            }
            // Host starts now so Escape at frame 0 skips it. Vita defers this until its
            // startup cache is ready. A --video may instead belong in the minimap.
#ifndef O1602_EXPERIMENTAL_VITA
            viewer->m_video->frame(platform::ticks_ms(), false);
#endif
        }
#ifdef O1602_EXPERIMENTAL_VITA
        else if(options.menu && !options.video && viewer->m_video->available(8)) {
            // Original sun/fireball logo, before the normal menu (user-selected movie 8).
            // Keep 58 as the menu's explicit Intro action; --intro keeps the full chain.
            viewer->m_video->request(8,0,false,true);
        }
#endif
        if (options.video) {
            const std::uint16_t n = *options.video;
            viewer->m_video->request(n, 0, false, n == 8 || n == 9 || n == 58);
        }
    }
    if (options.frame) {
        const auto texts = assets::TextTable::load(game);
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::phase("BEGIN original HUD / fonts / menu textures");
#endif
        viewer->m_hud = Hud::create(viewer->m_window->sdl_renderer(), game, viewer->m_palette,
                                    texts ? &texts.value() : nullptr, data_fixes);
        if (!viewer->m_hud) {
            std::fprintf(stderr, "no panel: the interface's files are missing\n");
        } else {
            viewer->m_hud->set_game(viewer->m_state->simulation, viewer->m_buildings, viewer->m_figures,
                                    viewer->m_local_player);
            viewer->m_hud->set_banners(&viewer->m_banners);
            viewer->m_hud->set_messages(&viewer->m_messages);
            viewer->m_hud->set_available([self = viewer.get()](std::uint32_t id) {
                const sim::Player* p = self->m_state->world.player(self->m_local_player);
                const assets::BuildingType* b = self->m_buildings.by_id(id);
                return p != nullptr && b != nullptr && sim::building_available(*p, *b);
            });
        }
    }
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::phase("OK original HUD / BEGIN input");
#endif
    viewer->m_input = std::make_unique<MapInput>(
        MapInput::Parts{*viewer, viewer->m_buildings, viewer->m_figures, viewer->m_state, viewer->m_local_player,
                        viewer->m_camera, viewer->m_tool, viewer->m_command_sink, *viewer->m_sound, *viewer->m_window,
                        viewer->m_hud.get(), options.original_mouse, options.confirm, options.keys_file});
    if (viewer->m_local_player < viewer->m_state->world.unit_groups().size()) {
        viewer->m_input->restore_groups(viewer->m_state->world.unit_groups()[viewer->m_local_player]);
    }
    if (viewer->m_hud) {
        viewer->m_hud->set_units(&viewer->m_input->units());
    }
    viewer->m_sound->set_listener([self = viewer.get()](MessageId message, std::uint16_t flags,
                                                        std::optional<std::pair<double, double>> tile,
                                                        std::optional<sim::PlayerId> about, std::string_view text) {
        if ((flags & audio::kMessageNormalSpeed) != 0 && self->m_state->simulation.speed() > 1) {
            // Fires, plagues and attacks: back to normal speed (0x004893F0), as a command like any
            // other change of the speed.
            self->m_command_sink.submit(
                sim::GameSpeedOrder{self->m_local_player, sim::GameSpeedOrder::Op::SetSpeed, 1});
        }
        if (self->m_hud) {
            // The icon in the colour of the player the message is about (a rival's new city).
            const sim::Player* p = self->m_state->world.player(about.value_or(self->m_local_player));
            self->m_hud->message(message, p != nullptr ? p->banner : 0, tile, platform::ticks_ms(), text);
        }
    });
    viewer->m_input->load_key_settings(data_folder() / "keys.cfg");
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::phase("BEGIN user settings / audio playback");
#endif
    viewer->load_options();
    if (options.width > 0) {
        viewer->m_app_settings.display.fullscreen = false;  // --size: a window this time
    }
    name_more_players(viewer->m_state->world);  // in the language the options set
    viewer->report_load_problems(warnings.size());
    if (options.new_game && options.new_game->name.empty() && viewer->m_state->world.time_ms() == 0) {
        // A new game started without --name: the name typed in the menu before, else "Nameless".
        if (sim::Player* p = viewer->m_state->world.player(viewer->m_local_player); p != nullptr) {
            p->name = viewer->m_app_settings.player_name.empty() ? ui::tr("menu.default_name")
                                                                 : viewer->m_app_settings.player_name;
        }
    }
    if (options.menu && viewer->m_hud) {
        viewer->m_hud->open_menu();
    }
    if (!options.message.empty() && !viewer->announce(options.message)) {
        std::fprintf(stderr, "--message: there is no message %s\n", options.message.c_str());
    }
    std::fprintf(stderr, "renderer: %s, max texture %d\n", viewer->m_window->renderer_name().c_str(),
                 viewer->m_window->max_texture_size());

    std::int32_t w = 0;
    std::int32_t h = 0;
    viewer->m_window->output_size(w, h);
    viewer->m_display_pixel_scale = render::pixel_scale_for(viewer->m_window->display_scale(), kMaxPixelScale);
    viewer->m_camera.set_world_size(viewer->m_state->world.width(), viewer->m_state->world.height());
    viewer->m_camera.set_screen(w, h, viewer->m_display_pixel_scale);
    viewer->lay_out();
    // The level asked for at the screen's whole pixel scale: the same picture as the original's
    // level, sharp, with or without the free zoom.
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::phase("BEGIN camera / graphics preparation");
#endif
    viewer->m_camera.set_zoom(options.zoom, w / 2.0, h / 2.0);
    // Start over the player's own city.
    viewer->m_camera.center_on(viewer->home_tile());
    // Townsfolk and sea creatures follow the game tick by tick, the same on every computer.
    MapViewer* self = viewer.get();
    viewer->m_state->simulation.set_tick_observer(
        [self](const sim::World& now) { self->m_ambient.tick(now, self->m_buildings, self->m_figures); });
    viewer->m_ambient.tick(viewer->m_state->world, viewer->m_buildings, viewer->m_figures);
    if (options.select && viewer->m_hud && (*options.select)[0] < viewer->m_state->world.islands().size()) {
        const auto& [isl, sx, sy] = *options.select;
        const sim::WorldIsland& island = viewer->m_state->world.islands()[isl];
        if (island.map.contains(sx, sy)) {
            const sim::SubTile t = island.map.sub_tile(viewer->m_buildings, sx, sy);
            Selection sel;
            sel.kind = Selection::Kind::Building;
            sel.island = isl;
            sel.x = static_cast<std::uint16_t>(t.building != nullptr ? t.origin_x : sx);
            sel.y = static_cast<std::uint16_t>(t.building != nullptr ? t.origin_y : sy);
            viewer->m_hud->set_selection(sel);
            viewer->m_camera.center_on(render::TilePos{island.x + sx, island.y + sy});
        }
    }
    if (options.select_ship && *options.select_ship < viewer->m_state->world.ships().size()) {
        const sim::Ship& ship = viewer->m_state->world.ships()[*options.select_ship];
        viewer->m_input->units().select(viewer->m_state->world, ui::UnitRef{ui::UnitRef::Kind::Ship, ship.id});
        viewer->m_input->selection_changed(false);
        viewer->m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max<std::int32_t>(ship.x, 0)),
                                                   static_cast<std::uint32_t>(std::max<std::int32_t>(ship.y, 0))});
    }
    if (options.select_soldier && *options.select_soldier < viewer->m_state->world.soldiers().size()) {
        const sim::Soldier& s = viewer->m_state->world.soldiers()[*options.select_soldier];
        viewer->m_input->units().select(viewer->m_state->world, ui::UnitRef{ui::UnitRef::Kind::Soldier, s.id});
        viewer->m_input->selection_changed(false);
        viewer->m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, s.x())),
                                                   static_cast<std::uint32_t>(std::max(0.0, s.y()))});
    }
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::trace("INITIAL_ZOOM direct=%u",unsigned(options.zoom));
#endif
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::session_data(game.root().generic_string(),unsigned(options.zoom),game.history_texts()!=nullptr);
#endif
    viewer->m_world_requested_at=platform::ticks_ms();
#ifdef O1602_EXPERIMENTAL_VITA
    render::test_prepared_cache_no_replace(std::filesystem::exists("ux0:data/open1602/cache-test-no-replace.flag"));
#endif
    viewer->start_preloading();
#ifdef O1602_EXPERIMENTAL_VITA
    viewer->prepare_startup_graphics();
    if(viewer->m_running && (options.intro || (options.menu && !options.video)))viewer->m_video->frame(platform::ticks_ms(),false);
#endif
    // The menu draws no world: its sprites are made while the player chooses, and a quit from the
    // menu does not wait for them. A game that starts without the menu needs them now.
#ifndef O1602_EXPERIMENTAL_VITA
    if (!viewer->in_menu()) {
        if (auto status = viewer->ensure_graphics(viewer->m_camera.view().zoom); !status) {
            return status.error();
        }
    }
#endif
    if (options.multiplayer) {
        if (auto status = viewer->start_multiplayer(); !status) {
            return status.error();
        }
        if (viewer->client_session() != nullptr && viewer->m_hud && viewer->m_hud->multiplayer() != nullptr) {
            // Joining from the command line: the menu shows the connection and the lobby.
            viewer->m_hud->open_multiplayer();
            viewer->m_hud->multiplayer()->show_connecting(options.multiplayer->address + ":" +
                                                          std::to_string(options.multiplayer->port));
        }
    }
    return viewer;
}

MapViewer::MapViewer(assets::GameDirectory game, assets::BuildingTable buildings, sim::World world,
                     assets::Palette palette, assets::FigureTable figures, ViewerOptions options)
    : m_game(std::move(game)),
      m_buildings(std::move(buildings)),
      m_original_buildings(m_buildings),
      m_palette(std::move(palette)),
      m_figures(std::move(figures)),
      m_original_figures(m_figures),
      m_options(std::move(options)),
      m_state(make_state(std::move(world), m_options.rules)),
      m_events(
          [this](const sim::Player& p, sim::PlayerId id) { return player_label(p, id, m_banners, m_state->world); }),
      m_clock(m_buildings),
      m_tool(m_buildings) {
    m_command_sink.set_simulation(&m_state->simulation);
    m_events.set_banners(&m_banners);
    warn_if_state_damaged();
}

void MapViewer::warn_if_state_damaged() const {
    if (m_state->simulation.state_damaged()) {
        std::fprintf(stderr,
                     "warning: the savegame's running state could not be read; the game continues "
                     "from the world alone (computer players, walkers and clocks start anew)\n");
    }
}

MapViewer::~MapViewer() {
    // GPU resources must go before the renderer/window, which go before SDL_Quit: the textures of
    // the video and the HUD as well as the world's sprites.
    m_preloader.reset();  // cancels the zoom level being loaded
    m_video.reset();
    m_hud.reset();
    for (auto& g : m_graphics) {
        g.reset();
    }
#ifdef O1602_EXPERIMENTAL_VITA
    m_vita_cache.reset();
#endif
    m_map_canvas.reset();
    m_sound.reset();
    m_window.reset();
    m_sdl.reset();
}

void MapViewer::start_preloading() {
    m_requested_graphics_zoom.reset();
    for(auto& p:m_preparation_progress)p=std::make_shared<render::GraphicsProgress>();
    render::GraphicsOptions options;
#ifdef O1602_EXPERIMENTAL_VITA
    options.backing_directory="ux0:data/open1602/cache";
    options.page_size = 2048U; // 16 MiB per RGBA page instead of 64 MiB; original sprite pixels.
#else
    options.page_size = m_window->max_texture_size() >= 4096 ? 4096U : 2048U;
#endif
    options.derive_missing_sheets = m_options.derive_small_figures;
    options.dither = m_app_settings.display.classic_graphics;
    options.package_images = m_package_images;
    // The thread works on its own copies: nothing it reads changes under it.
    m_preloader = std::make_unique<render::GraphicsPreloader>(
        [game = m_game, palette = m_palette, banners = m_banners, figures = m_figures, options, progress=m_preparation_progress](
            render::Zoom zoom, const std::atomic<bool>& cancel) {
#ifdef O1602_EXPERIMENTAL_VITA
            platform::vita::configure_background_worker("graphics_prepare");
#endif
            render::GraphicsOptions own = options;
            own.cancel = &cancel;
            own.progress=progress[static_cast<std::size_t>(zoom)];
            return render::prepare_world_graphics(game, palette, zoom, render::kWorldFigureSheets, own, &banners,
                                                  &figures);
        },
#ifdef O1602_EXPERIMENTAL_VITA
        true); // One requested zoom at a time; CPU pixels spill to bounded private storage.
#else
        true);
#endif
    // All zoom levels, the one shown first: switching to another one then waits for nothing.
#ifdef O1602_EXPERIMENTAL_VITA
    const std::array order{m_camera.view().zoom};m_preloader->request(order);
#else
    m_preloader->request(render::preload_order(m_camera.view().zoom));
#endif
}

void MapViewer::prepare_startup_graphics(){
#ifdef O1602_EXPERIMENTAL_VITA
    if(std::filesystem::exists("ux0:data/open1602/skip-startup-cache.flag")){
        m_preloader->cancel_pending();platform::vita::trace("STARTUP_CACHE skipped=1 reason=explicit_flag");return;
    }
    if(!m_preloader->background()){
        platform::vita::trace("STARTUP_CACHE skipped=1 reason=background_unavailable");return;
    }
    const auto begin=platform::ticks_ms();bool skipped=false,failed=false;unsigned prepared=0;
    for(const auto zoom:render::preload_order(m_camera.view().zoom)){
        ++m_startup_cache_step;m_pending_discrete_zoom=zoom;m_requested_graphics_zoom=zoom;
        m_preloader->request_latest(zoom);
        platform::vita::trace("STARTUP_CACHE level=%u step=%u begin=1 cursor=hidden",unsigned(zoom),m_startup_cache_step);
        while(!m_graphics[static_cast<std::size_t>(zoom)]){
            platform::vita::PadContext context{};context.loading=true;context.dialog=true;
            platform::vita::poll(m_window->sdl_window(),{},context,false);
            SDL_Event event;
            while(SDL_PollEvent(&event)){
                if(event.type==SDL_EVENT_QUIT){m_running=false;skipped=true;}
                // Input is consumed here; only closing the application interrupts preparation.
            }
            if(skipped)break;
            while(auto ready=m_preloader->take_ready()){
                if(!ready->second){
                    if(ready->second.error().message=="cancelled"){m_preloader->request_latest(zoom);continue;}
                    m_last_graphics_error=ready->second.error().message;failed=true;break;
                }
                if(auto status=install_graphics(ready->first,std::move(*ready->second));!status){
                    m_last_graphics_error=status.error().message;failed=true;break;
                }
            }
            if(failed)break;
            if(m_sound)m_sound->prefill();render_loading();
        }
        if(skipped||failed)break;
        ++prepared;platform::vita::trace("STARTUP_CACHE level=%u ready=1",unsigned(zoom));
    }
    if(skipped||failed)m_preloader->cancel_pending();
    platform::vita::trace("STARTUP_CACHE done=%u total=3 skipped=%d failed=%d elapsed_ms=%llu",prepared,skipped,failed,
        static_cast<unsigned long long>(platform::ticks_ms()-begin));
    if(failed)platform::vita::trace("STARTUP_CACHE_ERROR %s",m_last_graphics_error.c_str());
    m_startup_cache_step=0;m_pending_discrete_zoom.reset();m_requested_graphics_zoom.reset();
#endif
}

core::Status MapViewer::ensure_graphics(render::Zoom zoom) {
    if (m_graphics[static_cast<std::size_t>(zoom)]) {
        return core::Ok{};
    }
    // Wanted before the background has it: the frames stand still until it is there, the sound
    // goes on.
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::phase("BEGIN requested world atlas");
    // CPU metadata/backing files can coexist; all zooms share a fixed GPU cache.
#endif
    const std::uint64_t start = platform::ticks_ms();
    auto prepared = m_preloader->take(zoom, [this] {
        if (m_sound) {
            m_sound->prefill();
        }
    });
    if (const std::uint64_t waited = platform::ticks_ms() - start; waited > 0) {
        std::fprintf(stderr, "%s: waited %llu ms for it\n", zoom_name(zoom), static_cast<unsigned long long>(waited));
    }
    if (!prepared) {
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::trace("ZOOM_PREPARE_FAILED %s",prepared.error().message.c_str());
#endif
        return prepared.error();
    }
    return install_graphics(zoom, std::move(prepared).value());
}

void MapViewer::adopt_preloaded_graphics() {
    while (auto ready = m_preloader->take_ready()) {
        auto& [zoom, prepared] = *ready;
        if (!prepared) {
#ifdef O1602_EXPERIMENTAL_VITA
            if(prepared.error().message=="cancelled"){
                platform::vita::trace("ZOOM_OBSOLETE_CANCELLED zoom=%u",unsigned(zoom));continue;
            }
#endif
            // Tried again (and reported) when the player switches to it; the level shown at once.
            std::fprintf(stderr, "warning: %s: %s\n", zoom_name(zoom), prepared.error().message.c_str());
#ifdef O1602_EXPERIMENTAL_VITA
            if(zoom==m_camera.view().zoom&&!graphics()){
                m_last_graphics_error=prepared.error().message;
                platform::vita::trace("WORLD_LOADING_FAILED %s",m_last_graphics_error.c_str());
            }
            m_events.notify(ui::tr("vita.graphics.zoom_error",{prepared.error().message}),"vita.zoom_failed",platform::ticks_ms());
            m_zoom_target=m_camera.magnification();m_pending_discrete_zoom.reset();m_requested_graphics_zoom.reset();
#endif
            if (zoom == m_camera.view().zoom) {
                m_graphics_reloading = false;
            }
            continue;
        }
        if (m_graphics[static_cast<std::size_t>(zoom)]) {
            continue;
        }
        if (m_sound) {
            m_sound->prefill();  // the upload stands this frame still for a moment
        }
        if (auto status = install_graphics(zoom, std::move(prepared).value()); !status) {
            std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
#ifdef O1602_EXPERIMENTAL_VITA
            if(zoom==m_camera.view().zoom&&!graphics()){
                m_last_graphics_error=status.error().message;
                platform::vita::trace("WORLD_LOADING_FAILED %s",m_last_graphics_error.c_str());
            }
            m_events.notify(ui::tr("vita.graphics.zoom_error",{status.error().message}),"vita.zoom_failed",platform::ticks_ms());
            m_zoom_target=m_camera.magnification();m_pending_discrete_zoom.reset();m_requested_graphics_zoom.reset();
#endif
        }
#ifdef O1602_EXPERIMENTAL_VITA
        if(m_pending_discrete_zoom&&*m_pending_discrete_zoom==zoom&&m_graphics[static_cast<std::size_t>(zoom)]) {
            m_camera.set_zoom(zoom,m_zoom_anchor_x,m_zoom_anchor_y);m_pending_discrete_zoom.reset();
            platform::vita::trace("ZOOM_VISIBLE zoom=%u since_world_ms=%llu",unsigned(zoom),(unsigned long long)(platform::ticks_ms()-m_world_requested_at));
        }
#endif
    }
}

core::Status MapViewer::install_graphics(render::Zoom zoom, render::PreparedGraphics prepared) {
    auto& slot = m_graphics[static_cast<std::size_t>(zoom)];
    const std::uint32_t made_ms = prepared.ms;
    const std::uint64_t start = platform::ticks_ms();
    slot.emplace(ZoomGraphics{std::move(prepared.graphics), nullptr, std::move(prepared.hit_mask)});
    slot->renderer = std::make_unique<render::SdlSpriteRenderer>(m_window->sdl_renderer());
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::phase("BEGIN world atlas GPU upload");
#endif
    if (auto status = slot->renderer->upload(slot->graphics.atlas); !status) {
        slot.reset();
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::trace("ZOOM_UPLOAD_FAILED %s",status.error().message.c_str());
#endif
        return status;
    }
    slot->graphics.atlas.drop_pixels();
    if(m_requested_graphics_zoom==zoom)m_requested_graphics_zoom.reset();
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::trace("ATLAS_READY zoom=%u pages=%u sprites=%u disk_bytes=%llu masks_bytes=%u gpu_limit_bytes=50331648",
        unsigned(zoom),unsigned(slot->graphics.atlas.pages().size()),unsigned(slot->graphics.atlas.sprite_count()),
        (unsigned long long)slot->graphics.atlas.backing_bytes(),unsigned(slot->hit_mask.bytes()));
    platform::vita::phase("OK world atlas / CPU pages released");
    platform::vita::trace("GRAPHICS_PROGRESS zoom=%u phase=5 percent=100 cache_hit=%d",unsigned(zoom),slot->graphics.atlas.prepared_cache_hit());
    if(!in_menu() && (!m_world_presented || (!graphics()&&m_graphics_reloading)))render_loading();
#endif
    std::fprintf(stderr,
                 "%s: %u sprites in %u page(s) of %u px, figure masks %u KB, made in %u ms, upload %llu ms\n",
                 zoom_name(zoom), unsigned(slot->graphics.atlas.sprite_count()), unsigned(slot->graphics.atlas.pages().size()),
                 slot->graphics.atlas.page_size(), unsigned(slot->hit_mask.bytes() / 1024), made_ms,
                 static_cast<unsigned long long>(platform::ticks_ms() - start));
    return core::Ok{};
}

int MapViewer::run() {
    std::uint64_t last = platform::ticks_ms();
    const std::uint64_t run_start = last;
    std::uint64_t fps_start = last;
    std::uint32_t fps_frames = 0;
    std::uint32_t frames = 0;
#ifdef O1602_EXPERIMENTAL_VITA
    // Opt-in QA clock: real controls keep polling while scripts await a rendered world.
    // No fixed loading delay, and no change to simulation ticks or normal frame limits.
    const bool wait_script_world=platform::vita::scripts_wait_for_world();
    std::optional<std::uint32_t> script_origin=wait_script_world?std::nullopt:std::optional<std::uint32_t>{0};
#endif
    if (!m_options.save.empty()) {
        // A .gam the original cannot take (more than 7 players, a world too large) is told now, not
        // only after the game, when nothing can be done about it any more.
        std::string ending = core::path_to_utf8(m_options.save.extension());
        std::transform(ending.begin(), ending.end(), ending.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ending == ".gam") {
            if (auto bytes = sim::write_savegame(m_state->world, m_buildings, &m_state->simulation); !bytes) {
                std::fprintf(stderr, "error: --save %s: %s\n", core::path_to_utf8(m_options.save).c_str(),
                             bytes.error().message.c_str());
                return 1;
            }
        }
    }
    update_title(0.0);
    core::profile::begin();
    while (m_running) {
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::watch_frame(frames,m_state->simulation.next_tick(),unsigned(m_camera.view().zoom));
#endif
        if(auto* network=net::connectivity())network->update(); // pending dialog only; no normal network polling
        const auto profile_start=core::profile::enabled()?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        core::profile::scene(m_hud&&m_hud->menu_active()?core::profile::Menu:core::profile::World);
#ifdef O1602_EXPERIMENTAL_VITA
        if(!script_origin&&m_world_presented&&!in_menu()&&graphics()){
            script_origin=frames;
            platform::vita::trace("TEST_SCRIPT_READY application_frame=%u script_frame=0",frames);
        }
        if(!script_origin&&platform::ticks_ms()-run_start>=45000){
            platform::vita::trace("TEST_SCRIPT_TIMEOUT world_not_presented=1 timeout_ms=45000");
            std::fprintf(stderr,"error: scripted test: no rendered world within 45 seconds\n");
            return 2;
        }
        const auto scripted_frame=script_origin?frames-*script_origin:std::numeric_limits<std::uint32_t>::max();
        platform::vita::watch_phase("pad_poll");
        platform::vita::poll(m_window->sdl_window(),[&](int dx,int dy,float& x,float& y){
            return !m_video->full_screen()&&m_input->grid_cursor(dx,dy,x,y);
        },[&] {
            platform::vita::PadContext c;
            c.world=!in_menu()&&!m_video->full_screen(); c.building=m_tool.active();
            c.road=m_tool.active()&&m_tool.selected()&&m_tool.selected()->kind==sim::kind::kRoad;
            c.modal_active=m_hud&&m_hud->modal(); c.text_active=m_hud&&m_hud->typing();
            c.dialog=c.modal_active||c.text_active; c.loading=c.world&&(!graphics()||!m_world_presented);
            c.tool=m_tool.selected()?m_tool.selected()->id:0; c.zoom=unsigned(m_camera.view().zoom);
            c.world_id=reinterpret_cast<std::uintptr_t>(m_state.get()); c.popup=m_hud&&m_hud->build_popup_open();
            c.world_loaded=graphics()!=nullptr&&m_world_presented; c.menu_active=in_menu(); c.video_active=m_video->playing();
            c.options_active=m_hud&&m_hud->mode()==Hud::kModeOptions;
            c.quick_allowed=c.world&&c.world_loaded&&m_hud&&!c.dialog&&!c.video_active&&!c.options_active;
            return c;
        }(),script_origin.has_value());
        if(platform::vita::cache_failure()&&m_vita_cache)m_vita_cache->fail_next_prepare();
#endif
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::watch_phase("adopt_graphics");
#endif
        adopt_preloaded_graphics();
#ifdef O1602_EXPERIMENTAL_VITA
        if(m_options.performance_test && !in_menu() && graphics()){
            static unsigned benchmark_frame=0;
            const unsigned n=benchmark_frame++;
            core::profile::scene(n<1200?core::profile::BenchmarkIdle:core::profile::BenchmarkCursor);
            if(n==0||n==1200){
                double camera_x=0,camera_y=0;m_camera.screen_origin(camera_x,camera_y);
                platform::vita::trace("BENCHMARK_PHASE phase=%s tick=%llu paused=%d camera=%.2f,%.2f",n?"cursor":"idle",(unsigned long long)m_state->simulation.next_tick(),m_state->simulation.paused(),camera_x,camera_y);
                core::profile::mark(n?"benchmark_cursor":"benchmark_idle",m_state->simulation.next_tick());
            }
            if(n>=2400){m_running=false;break;}
            SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.windowID=SDL_GetWindowID(m_window->sdl_window());
            motion.motion.x=n<1200?400.f:100.f+float((n-1200)%240)*2.5f;
            motion.motion.y=n<1200?272.f:180.f+float(((n-1200)/240)%5)*48.f;
            SDL_PushEvent(&motion);
        }
#endif
        SDL_Event event;
        for (const ViewerOptions::ScriptedInput& in : m_options.input) {
#ifdef O1602_EXPERIMENTAL_VITA
            if(!script_origin||in.frame!=scripted_frame){
#else
            if (in.frame != frames) {
#endif
                continue;
            }
            SDL_Event e{};
            switch (in.kind) {
                case ViewerOptions::ScriptedInput::Kind::Move:
                    e.type = SDL_EVENT_MOUSE_MOTION;
                    e.motion.x = in.x;
                    e.motion.y = in.y;
                    break;
                case ViewerOptions::ScriptedInput::Kind::Down:
                case ViewerOptions::ScriptedInput::Kind::Up:
                    e.type = in.kind == ViewerOptions::ScriptedInput::Kind::Down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                                                                 : SDL_EVENT_MOUSE_BUTTON_UP;
                    e.button.button = in.right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
                    e.button.down = in.kind == ViewerOptions::ScriptedInput::Kind::Down;
                    e.button.x = in.x;
                    e.button.y = in.y;
                    SDL_SetModState(static_cast<SDL_Keymod>(in.mod));  // what the click reads as held
                    break;
                case ViewerOptions::ScriptedInput::Kind::Key:
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = static_cast<SDL_Keycode>(in.key);
                    e.key.scancode = SDL_GetScancodeFromKey(e.key.key, nullptr);
                    e.key.mod = in.mod;
                    e.key.down = true;
                    break;
                case ViewerOptions::ScriptedInput::Kind::Text:
                    e.type = SDL_EVENT_TEXT_INPUT;
                    e.text.text = in.text.c_str();  // the options outlive the event
                    break;
                case ViewerOptions::ScriptedInput::Kind::Wheel:
                    e.type = SDL_EVENT_MOUSE_WHEEL;
                    e.wheel.y = static_cast<float>(in.key);
                    e.wheel.mouse_x = in.x;
                    e.wheel.mouse_y = in.y;
                    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
                    break;
            }
            SDL_PushEvent(&e);
        }
        {
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::WatchPhase watch("events");
#endif
        core::profile::Scope events_timer(core::profile::Events);
        while (SDL_PollEvent(&event)) {
            switch(event.type){
                case SDL_EVENT_KEY_DOWN:case SDL_EVENT_KEY_UP:case SDL_EVENT_TEXT_INPUT:
                case SDL_EVENT_MOUSE_MOTION:case SDL_EVENT_MOUSE_BUTTON_DOWN:case SDL_EVENT_MOUSE_BUTTON_UP:
                case SDL_EVENT_MOUSE_WHEEL:case SDL_EVENT_FINGER_DOWN:case SDL_EVENT_FINGER_UP:case SDL_EVENT_FINGER_MOTION:
                case SDL_EVENT_GAMEPAD_AXIS_MOTION:case SDL_EVENT_GAMEPAD_BUTTON_DOWN:case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    m_window->input_received(event.common.timestamp);break;
                default:break;
            }
            if(auto* network=net::connectivity();network && network->input_blocked() && event.type!=SDL_EVENT_QUIT) {
                if(event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE)(void)network->select(net::NetworkMode::Internet);
                continue; // the native dialog owns input; never click through into lobby/chat
            }
#ifdef O1602_EXPERIMENTAL_VITA
            platform::vita::event(event);
            if(platform::vita::consume_event(event))continue;
            if(!in_menu()&&!graphics()&&!m_video->full_screen()){
                if(event.type==SDL_EVENT_KEY_DOWN&&event.key.key==SDLK_ESCAPE){if(m_hud)m_hud->open_menu();continue;}
                if(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP||event.type==SDL_EVENT_TEXT_INPUT||
                    event.type==SDL_EVENT_MOUSE_MOTION||event.type==SDL_EVENT_MOUSE_BUTTON_DOWN||
                    event.type==SDL_EVENT_MOUSE_BUTTON_UP||event.type==SDL_EVENT_MOUSE_WHEEL){
                    static unsigned ignored=0;
                    if(ignored++<8)platform::vita::trace("LOADING_INPUT_IGNORED type=%u",unsigned(event.type));
                    continue;
                }
            }
#endif
            switch (m_video->route(event, m_hud.get())) {
                case VideoController::Input::Game:
                    handle_event(event);
                    break;
                case VideoController::Input::TogglePause:
                    toggle_pause_video();
                    break;
                case VideoController::Input::LeftReleased:
                    m_input->release_left();
                    break;
                case VideoController::Input::Taken:
                    break;
            }
        }
        }
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::watch_phase("video_and_input");
#endif
        const std::uint64_t now = platform::ticks_ms();
        const auto delta = static_cast<std::uint32_t>(std::min<std::uint64_t>(now - last, 1000));
        last = now;
        // A video that starts now plays in the minimap's window only while one is on screen.
        const bool video = m_video->frame(now, m_hud && !m_hud->menu_active());
#ifdef O1602_EXPERIMENTAL_VITA
        if(video&&m_video->full_screen()){
            if(!m_atlas_paused_for_video){m_preloader->cancel_pending();m_atlas_paused_for_video=true;platform::vita::trace("VIDEO_ATLAS suspended=1");}
        }else if(m_atlas_paused_for_video){
            m_atlas_paused_for_video=false;std::vector<render::Zoom> missing;
            for(auto zoom:render::preload_order(m_camera.view().zoom))if(!m_graphics[static_cast<std::size_t>(zoom)])missing.push_back(zoom);
            if(!missing.empty())m_preloader->request(missing);
            platform::vita::trace("VIDEO_ATLAS suspended=0 missing=%u",unsigned(missing.size()));
        }
#endif
        update_cursor(video && m_video->full_screen());
        follow_mouse_grab(video && m_video->full_screen());
        if (video && m_video->full_screen()) {
            core::profile::scene(core::profile::VideoScene);
            // The game stands still while a video fills the screen (the original's main loop
            // 0x00408293); the time does not reach the simulation afterwards. Not in a multiplayer
            // game: the others would wait (M-018).
            if (m_session) {
                update(delta);
            }
            std::int32_t w = 0;
            std::int32_t h = 0;
            m_window->output_size(w, h);
            m_video->draw(w, h);
            present();
        } else {
            m_input->sync_text_input();
            if (m_hud && m_hud->menu_active()) {
                apply_menu();  // no game runs behind the menu, but for a multiplayer game
                if (m_session) {
                    update(delta);
                }
                render();
            } else {
#ifdef O1602_EXPERIMENTAL_VITA
                const bool waiting=!graphics();
                if(waiting){
                    if(!m_preloader->background()&&m_last_graphics_error.empty()){
                        m_last_graphics_error=ui::tr_utf8("vita.graphics.worker_error");
                        platform::vita::trace("WORLD_LOADING_FAILED %s",m_last_graphics_error.c_str());
                    }
                    core::profile::scene(core::profile::Loading);
                    if(!m_world_requested_at)m_world_requested_at=platform::ticks_ms();
                    render_loading();
                }else{
                    m_graphics_reloading=false;
#else
                // Leaving the menu: the game waits for the sprites of the zoom level if the
                // background has not made them yet; after a device reset it does not.
                if (m_graphics_reloading && m_graphics[static_cast<std::size_t>(m_camera.view().zoom)]) {
                    m_graphics_reloading = false;
                }
                if (!m_graphics_reloading) {
                    if(!m_graphics[static_cast<std::size_t>(m_camera.view().zoom)])core::profile::scene(core::profile::Loading);
                    if (auto status = ensure_graphics(m_camera.view().zoom); !status) {
                        std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
#ifdef O1602_EXPERIMENTAL_VITA
                        if(m_hud)m_hud->open_menu();
                        m_events.notify(ui::tr("vita.graphics.error",{status.error().message}),"vita.graphics_failed",platform::ticks_ms());
                        continue;
#else
                        return 1;
#endif
                    }
                }
#endif
                if (m_hud) {
                    // What the windows of the panel ordered (scuttling and disbanding ask first).
                    m_input->submit_orders(m_hud->take_commands(), false);
                    apply_options();
                }
                update(delta);
                m_input->reproject_grid_cursor();
                check_end_screen(now);
                render();
#ifdef O1602_EXPERIMENTAL_VITA
                if(!m_world_presented&&!in_menu()&&m_sprites_drawn>0&&graphics()&&graphics()->renderer->error().empty()){m_world_presented=true;platform::vita::session_world(unsigned(m_camera.view().zoom),unsigned(m_world_requested_at?platform::ticks_ms()-m_world_requested_at:0),graphics()->graphics.atlas.prepared_cache_hit());platform::vita::trace("WORLD_FRAME ready=1 zoom=%u wait_ms=%llu tick=%llu",unsigned(m_camera.view().zoom),
                    (unsigned long long)(m_world_requested_at?platform::ticks_ms()-m_world_requested_at:0),(unsigned long long)m_state->simulation.next_tick());}
                }
#endif
            }
        }
        {
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::WatchPhase watch("audio_mix");
#endif
        core::profile::Scope audio_timer(core::profile::Audio);m_sound->frame(m_state->world.time_ms(), delta);}

        ++frames;
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::watch_phase("frame_diagnostics");
        std::uint64_t diagnostic_hash=0;
        if(platform::vita::frame_hash_requested(frames)){
            core::profile::Scope hash_timer(core::profile::DiagnosticHash);
            diagnostic_hash=m_state->simulation.state_hash();
        }
        platform::vita::frame(frames,m_hud&&m_hud->menu_active(),m_state->simulation.next_tick(),diagnostic_hash);
        if(platform::vita::logging_enabled()&&(frames==1||frames%120==0)) {
            double cx=0,cy=0;m_camera.screen_origin(cx,cy);
            platform::vita::trace("CAMERA x=%.1f y=%.1f zoom=%u",cx,cy,unsigned(m_camera.view().zoom));
            if(const auto t=m_input->grid_tile())platform::vita::trace("GRID_ANCHOR frame=%u tile=%u,%u zoom=%u rotation=%u dragging=%d",frames,t->x,t->y,unsigned(m_camera.view().zoom),unsigned(m_camera.view().rotation),m_tool.dragging());
        }
#endif
        ++fps_frames;
        if (now - fps_start >= 500) {
            update_title(fps_frames * 1000.0 / static_cast<double>(now - fps_start));
            fps_start = now;
            fps_frames = 0;
        }
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::watch_phase("profile_frame");
#endif
        if(core::profile::enabled())core::profile::frame(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-profile_start).count()));
        if (m_options.max_frames != 0 &&
#ifdef O1602_EXPERIMENTAL_VITA
            script_origin&&frames-*script_origin>=m_options.max_frames
#else
            frames>=m_options.max_frames
#endif
        ) {
            break;
        }
    }
    if (m_options.max_frames == 0) {
        // A name still being typed in the multiplayer menu counts, as when leaving its field.
        if (m_hud && m_hud->multiplayer_open() && !m_hud->multiplayer()->name().empty() &&
            m_hud->multiplayer()->name()!=m_options.player_name) {
            m_app_settings.player_name = m_hud->multiplayer()->name();
        }
        save_settings();  // the window's size for the next start
    }
    if (m_options.max_frames != 0) {
        const std::uint64_t elapsed = std::max<std::uint64_t>(platform::ticks_ms() - run_start, 1);
        std::fprintf(stderr, "%u frames in %llu ms (%.1f fps), %u sprites in %u batches in the last frame\n", frames,
                     static_cast<unsigned long long>(elapsed), frames * 1000.0 / static_cast<double>(elapsed),
                     unsigned(m_sprites_drawn), unsigned(m_batches_drawn));
    }
    if (!m_options.save.empty()) {
        if (auto status = save_game(m_options.save); !status) {
            std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
            return 1;
        }
        std::fprintf(stderr, "game saved: %s\n", core::path_to_utf8(m_options.save).c_str());
    }
    if (!m_options.screenshot.empty()) {
        // Read back a complete frame.
        m_screenshot = ScreenshotRequest{m_options.screenshot};
        if (m_video->full_screen()) {
            std::int32_t w = 0;
            std::int32_t h = 0;
            m_window->output_size(w, h);
            m_video->draw(w, h);
            present();
        } else {
            render();
        }
        if (m_screenshot_error) {
            std::fprintf(stderr, "error: %s\n", m_screenshot_error->c_str());
            return 1;
        }
    }
#ifndef O1602_EXPERIMENTAL_VITA
    if(core::environment_variable("O1602_PROFILE")==std::optional<std::string>("1")){
        const auto report=core::profile::summary();const auto* data=reinterpret_cast<const std::uint8_t*>(report.data());
        std::error_code ec;std::filesystem::create_directories(data_folder(),ec);
        if(auto written=core::write_file_atomic(data_folder()/"profile.json",{data,report.size()});!written)std::fprintf(stderr,"profile: %s\n",written.error().message.c_str());
    }
#endif
    return 0;
}

void MapViewer::handle_event(const SDL_Event& original) {
    core::profile::Scope dispatch(core::profile::EventDispatch);
    SDL_Event event = original;
    // Mouse positions in output pixels, independent of the screen's pixel density.
    SDL_ConvertEventToRenderCoordinates(m_window->sdl_renderer(), &event);
    // SDL_EVENT_RENDER_TARGETS_RESET needs nothing: the map's canvas is drawn anew every frame.
    switch (m_window->render_reset(event)) {
        case platform::RenderReset::Textures:
            std::fprintf(stderr, "the render device was reset: textures are made again\n");
            recreate_textures();
            return;
        case platform::RenderReset::Renderer:
            recreate_renderer();
            return;
        case platform::RenderReset::None:
            break;
    }
    switch (event.type) {
        case SDL_EVENT_QUIT:
            m_running = false;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            std::int32_t w = 0;
            std::int32_t h = 0;
            m_window->output_size(w, h);
            m_camera.set_screen(w, h, m_camera.scale());
            lay_out();
            follow_display_scale();  // a move to another screen may change both
            remember_window_size();
            break;
        }
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            follow_display_scale();
            break;
        default:
            m_input->handle_event(event);
            break;
    }
}

void MapViewer::zoom_by(double steps, double sx, double sy) {
    if (steps == 0.0 || !std::isfinite(steps)) {
        return;
    }
    if (m_app_settings.display.free_zoom) {
        // Four notches of the wheel are one level (twice or half the size); the animation goes
        // on from where the last one aimed, so quick notches add up.
        const double from = m_zoom_target > 0.0 ? m_zoom_target : m_camera.magnification();
        m_zoom_target =
            std::clamp(from * std::exp2(-steps / 4.0), render::kMinMagnification, render::kMaxMagnification);
        m_zoom_anchor_x = sx;
        m_zoom_anchor_y = sy;
        return;
    }
    const int current = static_cast<int>(m_camera.view().zoom);
    const int next = std::clamp(current + (steps > 0.0 ? 1 : -1), 0, 2);
    if (next == current) {
        return;
    }
    const auto zoom = static_cast<render::Zoom>(next);
#ifdef O1602_EXPERIMENTAL_VITA
    if(!m_graphics[static_cast<std::size_t>(zoom)]) {
        m_requested_graphics_zoom=zoom;m_pending_discrete_zoom=zoom;m_zoom_anchor_x=sx;m_zoom_anchor_y=sy;
        m_preloader->request_latest(zoom);return;
    }
#endif
    if (auto status = ensure_graphics(zoom); !status) {
        std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
        return;
    }
    m_camera.set_zoom(zoom, sx, sy);
}

void MapViewer::scale_by(int direction, double sx, double sy) {
    if (m_app_settings.display.free_zoom) {
        zoom_by(direction > 0 ? -4.0 : 4.0, sx, sy);
        return;
    }
    // The original's levels: whole pixel scales.
    const auto scale = static_cast<std::int32_t>(std::lround(m_camera.scale()));
    m_camera.set_scale(std::clamp(scale + (direction > 0 ? 1 : -1), 1, kMaxPixelScale), sx, sy);
}

void MapViewer::show_magnification(double magnification, double sx, double sy) {
    const render::ZoomChoice choice = render::zoom_for_magnification(magnification);
#ifdef O1602_EXPERIMENTAL_VITA
    if(choice.zoom==m_camera.view().zoom&&m_requested_graphics_zoom&&graphics()){
        m_preloader->cancel_pending();m_requested_graphics_zoom.reset();m_pending_discrete_zoom.reset();
        platform::vita::trace("ZOOM_PREPARE_CANCELLED reason=target_returned_to_ready_zoom");
    }
#endif
    if (choice.zoom != m_camera.view().zoom) {
    #ifdef O1602_EXPERIMENTAL_VITA
    if(!m_graphics[static_cast<std::size_t>(choice.zoom)]) {
        m_requested_graphics_zoom=choice.zoom;m_preloader->request_latest(choice.zoom);return;
    }
#endif
    if (auto status = ensure_graphics(choice.zoom); !status) {
            std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
            m_zoom_target = 0.0;
            return;
        }
    }
    m_camera.set_magnification(magnification, sx, sy);
}

void MapViewer::animate_zoom(std::uint32_t delta_ms) {
    if (m_zoom_target <= 0.0) {
        return;
    }
    // Exponentially towards the target in log space: the same speed zooming in and out, about
    // 0.15 s to get there.
    const double now = m_camera.magnification();
    const double t = 1.0 - std::exp(-static_cast<double>(delta_ms) / 45.0);
    double next = std::exp(std::log(now) + (std::log(m_zoom_target) - std::log(now)) * t);
    const bool arrived=std::abs(std::log(m_zoom_target / next)) < 0.004 || delta_ms >= 1000;
    if (arrived) {
        next = m_zoom_target;  // there: exactly, so a whole scale draws without the canvas
    }
    show_magnification(next, m_zoom_anchor_x, m_zoom_anchor_y);
    // A missing original zoom level leaves the old camera usable while its
    // worker prepares. Retain the requested target until it was really shown.
    if(arrived&&std::abs(std::log(m_camera.magnification()/next))<0.004)m_zoom_target=0.0;
}

void MapViewer::update(std::uint32_t delta_ms) {
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::WatchPhase watch("simulation_and_events");
#endif
    animate_zoom(delta_ms);
    m_input->scroll_with_keys(delta_ms);
    if (m_app_settings.display.edge_scroll) {
        m_input->scroll_at_edges(delta_ms, m_app_settings.display.edge_speed);
    }
    // The pause of the video ended another way (F3, another player): the video goes too.
    m_video->follow_pause(m_state->simulation.paused());
    m_input->follow_selection();
    // While the game is paused this only carries out the commands (building is possible then).
    // Ticks run while the frame has time (at least one); before each, the positions of the ships,
    // soldiers and walkers are kept so that they move smoothly between ticks. A computer too slow
    // for the speed runs the game slower instead of stalling (review point 20).
    const auto frame_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTickBudgetMs);
    bool first = true;
    const auto before_tick = [&] {
        if (!first && std::chrono::steady_clock::now() >= frame_end) {
            return false;
        }
        first = false;
        m_ship_trail.capture(m_state->world);
        m_soldiers.capture(m_state->world);
        m_walkers.capture(m_state->simulation.logistics(), m_state->world);
        return true;
    };
    if (m_session) {
        // A multiplayer game runs the frames the host has closed (ADR 0011).
        {core::profile::Scope timer(host_session() ? core::profile::NetworkHost : core::profile::NetworkClient);
        m_session->update(delta_ms, before_tick); }
        update_lan_beacon();
        m_tick_fraction = m_session->tick_fraction();
        announce_take_overs();
        apply_session_events();  // may end the session
        // As it is now, for the frame drawn next: the menu's model is filled before the session
        // runs, and a game that came whole is loaded with the next update (and holds that frame).
        join_to_model();
    } else {
        m_session_roles.clear();
        {core::profile::Scope sim_timer(core::profile::Simulation);m_state->simulation.advance(delta_ms, before_tick);}
        m_tick_fraction = m_state->simulation.tick_fraction();
    }
    update_chat();
    m_ship_trail.set_fraction(m_tick_fraction);
    if (!m_state->simulation.paused()) {
        place_listener();
        apply_production_events();
        apply_game_events();
        m_clock.advance(delta_ms, m_state->simulation.speed());
        update_ambient();  // also what is on screen (m_drawn_tiles)
        m_soldiers.sync(m_state->world, m_buildings, m_figures, m_tick_fraction, m_drawn_tiles);
        m_soldiers.advance(static_cast<std::uint32_t>(delta_ms), m_figures);
        m_walkers.update(m_state->simulation.logistics(), m_state->world, m_buildings, m_figures,
                         static_cast<std::uint32_t>(delta_ms) * m_state->simulation.speed(), m_tick_fraction);
        m_carts.update(m_state->world, m_buildings, m_figures, m_ship_trail,
                       static_cast<std::uint32_t>(delta_ms) * m_state->simulation.speed());
        m_combat.frame(m_state->world, m_figures, m_tick_fraction);
        m_sound->figures({&m_walkers.instances(), &m_soldiers.instances(), &m_ambient.walkers()}, m_effects, m_figures,
                         m_state->world.time_ms(), m_state->simulation.speed(), m_drawn_tiles);
    }
    apply_construction();
}

std::string MapViewer::tile_status() const {
    core::profile::Scope timer(core::profile::HoverStatus);
    if (!m_hud || m_hud->texts() == nullptr ||
        !m_hud->game_view().contains(static_cast<std::int32_t>(m_input->mouse_x()),
                                     static_cast<std::int32_t>(m_input->mouse_y()))) {
        return {};
    }
    // A unit drawn over the tile takes the line, as it would take the click.
    if (const auto unit = m_input->unit_at_mouse()) {
        return app::unit_status(m_state->world, *unit, m_figures, *m_hud->texts());
    }
    const auto spot = m_input->spot_at_mouse();
    if (!spot) {
        return {};
    }
    return app::tile_status(m_state->world.islands()[spot->island], spot->x, spot->y, m_local_player, m_buildings,
                            *m_hud->texts());
}

sim::Catchment MapViewer::world_catchment(std::uint16_t island, sim::Catchment area) const {
    const sim::WorldIsland& i = m_state->world.islands()[island];  // island tiles -> world tiles
    area.centre.x += static_cast<std::int32_t>(i.x);
    area.centre.y += static_cast<std::int32_t>(i.y);
    return area;
}

std::optional<render::TileLighting> MapViewer::building_lighting(const Selection& clicked) const {
    // A second click on the same building lights all of its type on the island (Open1602's own).
    const bool same_type = clicked.again > 0;
    CircleCache& cache = m_circle_cache;
    if (!cache.valid || !cache.clicked.same_object(clicked) || cache.clicked.serial != clicked.serial ||
        cache.same_type != same_type || cache.construction != m_construction_serial) {
        cache.clicked = clicked;
        cache.same_type = same_type;
        cache.construction = m_construction_serial;
        cache.circles = catchment_circles(m_state->world, m_buildings, m_local_player, clicked.island, clicked.x,
                                          clicked.y, same_type);
        cache.valid = true;
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::trace("CATCHMENT_UI island=%u x=%u y=%u same_type=%u circles=%u",
            unsigned(clicked.island),unsigned(clicked.x),unsigned(clicked.y),unsigned(same_type),
            unsigned(cache.circles?cache.circles->circles.size():0));
#endif
    }
    if (!cache.circles || cache.circles->circles.empty()) {
        return std::nullopt;
    }
    const CatchmentCircles& c = *cache.circles;
    render::TileLighting l =
        render::TileLighting::circle(m_local_player, world_catchment(clicked.island, c.circles[0]));
    for (std::size_t i = 1; i < c.circles.size(); ++i) {
        l.add_circle(world_catchment(clicked.island, c.circles[i]));
    }
    if (sim::catchment_covers_water(*c.type)) {
        l.water();  // where a fisher's boat fishes, or a tower shoots at ships
    }
    // A Kontor and a shipyard: also the water their ships come from, as in the original.
    if (c.type->prod_kind == sim::prod::kKontor || c.type->prod_kind == sim::prod::kShipyard) {
        for (const sim::TilePos& o : c.origins) {
            l.harbour(m_state->world, m_buildings, clicked.island, static_cast<std::uint32_t>(o.x),
                      static_cast<std::uint32_t>(o.y));
        }
    }
    return l;
}

render::TileLighting MapViewer::tile_lighting() const {
    const Selection* clicked =
        m_hud && m_hud->selection().kind == Selection::Kind::Building ? &m_hud->selection() : nullptr;
    const bool building = m_tool.mode() == BuildTool::Mode::Build || (m_hud && m_hud->mode() == Hud::kModeBuild);
    if (building) {
        // A building clicked before opening the build menu stays selected, and its circle stays
        // lit whatever is placed: the fields of a farm, another sheep farm beside its pasture,
        // houses around a chapel. Otherwise all the player's land.
        std::optional<render::TileLighting> lit;
        if (clicked != nullptr) {
            lit = building_lighting(*clicked);
        }
        render::TileLighting l = lit ? std::move(*lit) : render::TileLighting::territory(m_local_player);
        // Other players' land with a dotted border, as in the History Edition (the original only dims it).
        if (!m_app_settings.display.classic_graphics) {
            l.foreign_borders();
        }
        // A building with a catchment being placed: the border of its circle at the mouse.
        const assets::BuildingType* b = m_tool.mode() == BuildTool::Mode::Build ? m_tool.selected() : nullptr;
        const auto spot = m_input->spot_at_mouse();
        if (b != nullptr && spot && shows_catchment(*b)) {
            l.outline(world_catchment(spot->island,
                                      sim::catchment_of(*b, m_tool.rotation(), sim::TilePos{spot->x, spot->y})));
        }
        return l;
    }
    if (clicked != nullptr && m_hud->mode() == Hud::kModeInfo) {
        if (auto l = building_lighting(*clicked)) {
            return *l;
        }
    }
    return {};
}

void MapViewer::apply_construction() {
    const auto results=m_state->simulation.take_command_results();
#ifdef O1602_EXPERIMENTAL_VITA
    for(const auto& result:results)platform::vita::trace("COMMAND sequence=%llu accepted=%d build=%d moved=%u late=%d",
        (unsigned long long)result.sequence,result.accepted,result.build.has_value(),result.moved,result.late);
#endif
    m_events.command_results(results);
    const std::vector<sim::ConstructionEvent> events = m_state->simulation.take_construction_events();
    const bool changed = !events.empty();
    for(const auto& e:events)if(e.kind==sim::ConstructionEvent::Kind::CityFounded){
        const auto* city=m_state->world.city(e.island,e.city);
        core::profile::mark(city&&city->owner==m_local_player?"own_city_founded":"city_founded",m_state->simulation.next_tick());
    }
    for(const auto& e:events)if(e.kind==sim::ConstructionEvent::Kind::Placed) {core::profile::mark("building_completed",m_state->simulation.next_tick());break;}
    if (changed) {
        ++m_construction_serial;  // the circles of a clicked building's type may change
    }
    m_sound->construction(m_state->world, events);
    for (const sim::ConstructionEvent& e : events) {
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::trace("CONSTRUCTION kind=%u player=%u island=%u x=%ld y=%ld",unsigned(e.kind),unsigned(e.player),unsigned(e.island),long(e.x),long(e.y));
#endif
        // A production entry that was freed or taken by a new building starts from its saved phase.
        if (e.kind == sim::ConstructionEvent::Kind::Removed && e.production) {
            m_production_phases.forget(*e.production);
        } else if (e.kind == sim::ConstructionEvent::Kind::Placed && e.x >= 0 && e.y >= 0) {
            if (const auto index = m_state->world.production_index(e.island, static_cast<std::uint16_t>(e.x),
                                                                   static_cast<std::uint16_t>(e.y))) {
                m_production_phases.forget(*index);
            }
            // With the setting on, a farm the local player placed plants its fields at once, as
            // key F would (clearing the forest for a crop or pasture). It is an ordinary order of
            // the local player (a farm of another player gives none), so every computer in a
            // multiplayer game sees the same.
            if (m_app_settings.display.plant_fields && e.type != nullptr &&
                (e.type->prod_kind == sim::prod::kFarm || e.type->prod_kind == sim::prod::kPasture)) {
                m_input->plant_fields_at(e.island, e.x, e.y);
            }
        }
    }
    m_stock_notes.add_treasures(m_state->world, m_buildings, events, m_local_player, stock_note_time());
    show_notices(m_events.construction_events(m_state->world, m_local_player, events));
    if (changed) {
        // Flags, smoke and status icons of the new buildings: only the islands built on are
        // collected again. The list stays ordered by island, as collect_building_figures() gives it.
        std::vector<std::uint16_t> islands;
        for (const sim::ConstructionEvent& e : events) {
            islands.push_back(e.island);
        }
        std::sort(islands.begin(), islands.end());
        islands.erase(std::unique(islands.begin(), islands.end()), islands.end());
        std::vector<render::BuildingFigure> fresh;
        for (const std::uint16_t island : islands) {
            if (island >= m_state->world.islands().size()) {
                continue;
            }
            fresh.clear();
            render::collect_island_building_figures(m_state->world, m_buildings, m_figures, island, fresh);
            const auto by_island = [](const render::BuildingFigure& f, std::uint16_t i) { return f.island < i; };
            const auto first =
                std::lower_bound(m_building_figures.begin(), m_building_figures.end(), island, by_island);
            auto last = first;
            while (last != m_building_figures.end() && last->island == island) {
                ++last;
            }
            const auto at = m_building_figures.erase(first, last);
            m_building_figures.insert(at, fresh.begin(), fresh.end());
        }
    }
}

render::WorldLayers MapViewer::world_layers(const ZoomGraphics& graphics) const {
    render::WorldLayers layers;
    const render::SpriteAtlas& atlas = graphics.graphics.atlas;
    layers.extent = render::SpriteExtent{atlas.max_sprite_width(), atlas.max_sprite_height()};
    layers.figures = &m_figures;
    layers.atlas = &atlas;
    layers.ship_base = graphics.graphics.sheet_base(assets::FigureSheet::Ship).value_or(0);
    layers.ship_trail = &m_ship_trail;
    layers.sheet_base = graphics.graphics.base;
    // The deer walk as figures of the simulation (m_walkers).
    layers.land_figures = {&m_soldiers.instances(), &m_walkers.instances(), &m_carts.instances(), &m_ambient.walkers()};
    layers.unit_tint = &graphics.graphics.unit_tint;
    layers.banners = &m_banners;
    layers.banner_flags = &graphics.graphics.banner_flags;
    layers.owner_tint = m_show_owners;
    layers.building_figures = &m_building_figures;
    layers.local_player = m_local_player;
    layers.production_phases = &m_production_phases;
    layers.ship_smoke = &m_combat.ship_smoke();
    layers.dither = graphics.graphics.dither.sheets.empty() ? nullptr : &graphics.graphics.dither;
    return layers;
}

std::optional<render::FigureSprites> MapViewer::unit_sprites(const ui::UnitRef& ref) const {
    const auto& graphics = m_graphics[static_cast<std::size_t>(m_camera.view().zoom)];
    if (!graphics) {
        return std::nullopt;
    }
    const render::WorldLayers layers = world_layers(*graphics);
    render::FigureSprites out;
    const sim::Ship* cart = ref.kind == ui::UnitRef::Kind::Ship ? m_state->world.ship(ref.id) : nullptr;
    if (ref.kind == ui::UnitRef::Kind::Ship && (cart == nullptr || !cart->cart())) {
        const sim::Ship* ship = m_state->world.ship(ref.id);
        render::ShipSprites s;
        if (ship == nullptr ||
            !render::ship_sprites(*ship, m_state->world, m_camera.view(), layers, m_clock.figure_ms(), s)) {
            return std::nullopt;
        }
        out.row = s.row;
        out.depth = s.depth;
        out.parts[out.count++] = s.hull;
        if (s.has_flag) {
            out.parts[out.count++] = s.flag;
        }
        return out;
    }
    const render::FigureInstance* figure = cart != nullptr ? m_carts.find(ref.id) : m_soldiers.find(ref.id);
    if (figure == nullptr || !render::land_figure_sprites(*figure, m_state->world, m_camera.view(), layers, out)) {
        return std::nullopt;
    }
    // The shadow is not part of the unit.
    std::uint8_t kept = 0;
    for (std::uint8_t i = 0; i < out.count; ++i) {
        if (out.parts[i].mode != render::DrawCommand::Mode::Shadow) {
            out.parts[kept++] = out.parts[i];
        }
    }
    out.count = kept;
    return out.count > 0 ? std::optional<render::FigureSprites>(out) : std::nullopt;
}

void MapViewer::tiles_at_pixel(std::int32_t px, std::int32_t py, std::vector<render::TileHit>& out) const {
    out.clear();
    const auto& graphics = m_graphics[static_cast<std::size_t>(m_camera.view().zoom)];
    if (!graphics) {
        return;
    }
    render::tiles_at_pixel(m_state->world, m_buildings, m_clock, m_camera.view(), world_layers(*graphics),
                           graphics->hit_mask, px, py, out);
}

const ZoomGraphics* MapViewer::graphics() const {
    const auto& graphics = m_graphics[static_cast<std::size_t>(m_camera.view().zoom)];
    return graphics ? &*graphics : nullptr;
}

void MapViewer::render_loading(){
#ifdef O1602_EXPERIMENTAL_VITA
    const auto zoom=m_pending_discrete_zoom.value_or(m_camera.view().zoom);
#else
    const auto zoom=m_camera.view().zoom;
#endif
    const auto& progress=m_preparation_progress[static_cast<unsigned>(zoom)];
    const unsigned state=progress?progress->snapshot():0,percent=state&255;
#ifdef O1602_EXPERIMENTAL_VITA
    static std::array<unsigned,3> last_progress{~0U,~0U,~0U};
    if(last_progress[unsigned(zoom)]!=state){last_progress[unsigned(zoom)]=state;
        platform::vita::trace("GRAPHICS_PROGRESS zoom=%u phase=%u percent=%u",unsigned(zoom),state>>8,percent);}
#endif
#ifdef O1602_EXPERIMENTAL_VITA
    if(m_hud)m_hud->draw_preparing(percent,m_startup_cache_step,m_last_graphics_error);
#else
    if(m_hud)m_hud->draw_preparing(percent,m_startup_cache_step,{});
#endif
    present(false); // Preparation has no pointer interaction; restore on the next normal present.
}

void MapViewer::render() {
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::WatchPhase watch("render");
#endif
    core::profile::Scope timer(core::profile::Render);
    core::profile::gauge(core::profile::WorldZoom,unsigned(m_camera.view().zoom));
    SDL_Renderer* r = m_window->sdl_renderer();
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (m_hud && m_hud->menu_active()) {
        m_hud->draw_menu();
        if (m_video->in_frame()) {
            m_video->draw_in(m_hud->menu_video_rect());
        }
        present();
        return;
    }

    auto& graphics = m_graphics[static_cast<std::size_t>(m_camera.view().zoom)];
    if (graphics) {
        render::WorldLayers layers = world_layers(*graphics);
        collect_effects();
        layers.effects = &m_effects;
        // The building the tool would place, drawn into the map for this frame only. The guard puts
        // back every cell whole (random bits included) even if drawing throws, so that the world and
        // with it the savegame never depend on where the mouse was (ADR 0004).
        struct Saved {
            std::uint16_t island;
            std::uint32_t x;
            std::uint32_t y;
            sim::MapCell cell;
        };
        struct PreviewGuard {
            sim::World& world;
            std::vector<Saved> cells;
            explicit PreviewGuard(sim::World& w) : world(w) {}
            PreviewGuard(const PreviewGuard&) = delete;
            PreviewGuard& operator=(const PreviewGuard&) = delete;
            ~PreviewGuard() { restore(); }
            void restore() noexcept {
                for (auto it = cells.rbegin(); it != cells.rend(); ++it) {
                    world.island(it->island).map.set_top_cell(it->x, it->y, it->cell);
                }
                cells.clear();
            }
        };
        PreviewGuard preview{m_state->world};
        std::vector<Saved>& saved = preview.cells;
        m_tool_status.clear();
        m_tool_short_of_wood = false;
        m_pending_bill.reset();
        m_tile_marks.clear();
        layers.tile_marks = &m_tile_marks;
        // First, so that the build preview's marks lie over it.
        add_fertility_marks();
        m_lighting = tile_lighting();
        layers.lighting = &m_lighting;
        if (m_tool.mode() == BuildTool::Mode::Build) {
            if (const auto spot = m_input->spot_at_mouse()) {
                if (const auto order = m_tool.build_order(m_state->world, m_local_player, *spot)) {
                    const sim::BuildPlan plan = sim::plan_build(m_state->world, m_buildings, *order);
                    m_pending_bill = plan.shown;
                    sim::IslandMap& map = m_state->world.island(order->island).map;
                    // A field or forest being dragged: its whole rectangle hatched on the ground,
                    // with no crops or trees in it, as in the History Edition.
                    const bool hatched = m_tool.hatched_drag();
                    std::vector<std::pair<std::int32_t, std::int32_t>> fitted;  // road and wall tiles
                    if (hatched) {
                        for (const sim::BuildItem& item : order->items) {
                            if (map.contains(item.x, item.y)) {
                                render::TileMark mark{order->island, static_cast<std::uint16_t>(item.x),
                                                      static_cast<std::uint16_t>(item.y), render::kDragHatch};
                                mark.hatched = true;
                                m_tile_marks.push_back(mark);
                            }
                        }
                    }
                    const bool multi = m_tool.multi_drag();
                    const bool blink_off = (platform::ticks_ms() / kShortOfMeansBlinkMs) % 2 == 1;
                    for (const sim::PlannedBuild& item : plan.items) {
                        // What only lacks gold or goods blinks, on unmarked ground; anything else that
                        // cannot be built only shows its ground, marked (it might stand over a building
                        // or the sea).
                        const bool short_of_means =
                            item.refusal == sim::BuildRefusal::Money || item.refusal == sim::BuildRefusal::Tools ||
                            item.refusal == sim::BuildRefusal::Wood || item.refusal == sim::BuildRefusal::Bricks ||
                            item.refusal == sim::BuildRefusal::Cannons;
                        // Houses and piers dragged out: what does not fit there is left out, not
                        // shown (History Edition), and the gaps say nothing in the status line.
                        if (multi && !item.ok() && !short_of_means) {
                            continue;
                        }
                        if (!item.ok() && m_tool_status.empty()) {
                            m_tool_status = refusal_text(item.refusal);
                            // Where only Open1602's fix refuses, the text names the original setting
                            // that allows it (S-008 at the edge of the own land, S-003 on poor ground).
                            const sim::OriginalBugs& bugs = m_state->world.original_bugs();
                            if (item.refusal == sim::BuildRefusal::Infertile && !bugs.infertile_fields) {
                                m_tool_status = ui::tr("build.refusal.infertile.original");
                            } else if (item.refusal == sim::BuildRefusal::Territory && !bugs.building_radius &&
                                       item.type != nullptr && touches_own_land(map, item, m_local_player)) {
                                m_tool_status = ui::tr("build.refusal.territory.original");
                            }
                        }
                        m_tool_short_of_wood |= item.refusal == sim::BuildRefusal::Wood;
                        if (item.type == nullptr || hatched) {
                            continue;
                        }
                        // Ground that cannot be built on is checkered yellow, as in the original and the
                        // History Edition, and drawn over the buildings so that none hides it.
                        std::uint32_t w = 1;
                        std::uint32_t h = 1;
                        sim::rotated_size(*item.type, item.rotation, w, h);
                        bool inside = true;
                        for (std::uint32_t j = 0; j < h; ++j) {
                            for (std::uint32_t i = 0; i < w; ++i) {
                                const std::int32_t tx = item.x + static_cast<std::int32_t>(i);
                                const std::int32_t ty = item.y + static_cast<std::int32_t>(j);
                                if (!map.contains(tx, ty)) {
                                    inside = false;
                                    continue;
                                }
                                if (!item.ok() && !short_of_means) {
                                    render::TileMark mark{order->island, static_cast<std::uint16_t>(tx),
                                                          static_cast<std::uint16_t>(ty), render::kDragHatch};
                                    mark.hatched = true;
                                    mark.on_top = true;
                                    m_tile_marks.push_back(mark);
                                }
                            }
                        }
                        if (!inside || (!item.ok() && (!short_of_means || blink_off))) {
                            continue;
                        }
                        for (std::uint32_t j = 0; j < h; ++j) {
                            for (std::uint32_t i = 0; i < w; ++i) {
                                const auto tx = static_cast<std::uint32_t>(item.x) + i;
                                const auto ty = static_cast<std::uint32_t>(item.y) + j;
                                saved.push_back(Saved{order->island, tx, ty, map.top(tx, ty)});
                            }
                        }
                        const sim::MapCell& at =
                            map.top(static_cast<std::uint32_t>(item.x), static_cast<std::uint32_t>(item.y));
                        // A field or tree is shown grown, a seedling would hardly be seen.
                        const assets::BuildingType* shown = item.type;
                        if (const assets::BuildingType* ripe = sim::ripe_record(m_buildings, *item.type)) {
                            shown = ripe;
                        }
                        (void)map.build(*shown, item.x, item.y, item.rotation, 0, at.owner, at.city);
                        if (shown->kreuz_base > 0) {
                            fitted.push_back({item.x, item.y});
                        }
                    }
                    // Roads and walls fit their neighbours as they will once built, and the
                    // neighbours them (both put back after drawing like the rest).
                    for (const auto& [fx, fy] : fitted) {
                        sim::fit_connection(m_state->world, m_buildings, order->island, fx, fy);
                    }
                    for (const auto& [fx, fy] : fitted) {
                        for (const auto& [dx, dy] :
                             {std::pair{0, -1}, std::pair{1, 0}, std::pair{0, 1}, std::pair{-1, 0}}) {
                            const std::int32_t nx = fx + dx;
                            const std::int32_t ny = fy + dy;
                            if (map.contains(nx, ny)) {
                                const auto ux = static_cast<std::uint32_t>(nx);
                                const auto uy = static_cast<std::uint32_t>(ny);
                                saved.push_back(Saved{order->island, ux, uy, map.top(ux, uy)});
                                sim::fit_connection(m_state->world, m_buildings, order->island, nx, ny);
                            }
                        }
                    }
                    if (m_tool_status.empty()) {
                        m_tool_status = ui::tr("build.cost", {std::to_string(plan.bill.money)});
                    }
                }
            }
        }
        if (m_tool.mode() == BuildTool::Mode::Demolish) {
            // Only what the click or drag would really remove is hatched, over the buildings, so the
            // player sees what changes before letting go; in the original's yellow checkerboard.
            if (const auto spot = m_input->spot_at_mouse()) {
                for (const sim::TilePos& t : m_tool.demolish_preview(m_state->world, m_local_player, *spot)) {
                    render::TileMark mark{spot->island, static_cast<std::uint16_t>(t.x),
                                          static_cast<std::uint16_t>(t.y), render::kDragHatch};
                    mark.hatched = true;
                    mark.on_top = true;
                    m_tile_marks.push_back(mark);
                }
            }
        }
        {core::profile::Scope commands(core::profile::Commands);
        render::build_world_draw_list(m_state->world, m_buildings, m_clock, m_camera.view(), m_camera.viewport(),
                                      layers, m_commands, m_draw_scratch);}

        preview.restore();
        draw_map(*graphics);
        draw_unit_arms();
        draw_stock_notes();
        draw_corner_info();
        m_input->draw_overlay();
    }
    if (m_hud) {
        // The corners of the visible map in world tiles, for the minimap's frame. A corner beyond
        // the map's edge stays where it is (the minimap clips the frame), so the frame keeps its shape.
        const ui::Rect& v = m_hud->game_view();
        std::array<std::pair<double, double>, 4> corners{};
        const std::array<std::pair<double, double>, 4> screen = {
            std::make_pair(0.0, 0.0), std::make_pair(static_cast<double>(v.w), 0.0),
            std::make_pair(static_cast<double>(v.w), static_cast<double>(v.h)),
            std::make_pair(0.0, static_cast<double>(v.h))};
        for (std::size_t i = 0; i < 4; ++i) {
            corners[i] = m_camera.point_at_screen(screen[i].first, screen[i].second);
        }
        const std::uint64_t now = platform::ticks_ms();
        if (const std::string* notice = m_events.notice(now); notice != nullptr && !m_tool.active()) {
            m_hud->set_status(*notice);
        } else {
            if (m_tool_status.empty() && !m_tool.active()) {
                const std::pair<double, double> mouse{m_input->mouse_x(), m_input->mouse_y()};
                if (mouse != m_tile_status_mouse || now >= m_tile_status_ms + kTileStatusMs || now < m_tile_status_ms) {
                    m_tile_status = tile_status();
                    m_tile_status_ms = now;
                    m_tile_status_mouse = mouse;
                }
                m_hud->set_status(m_tile_status);
            } else {
                m_tile_status_mouse = {-1.0, -1.0};  // worked out afresh once the tool is gone
                m_hud->set_status(m_tool_status);
            }
        }
#ifdef O1602_EXPERIMENTAL_VITA
        if(m_requested_graphics_zoom){
            const auto z=unsigned(*m_requested_graphics_zoom);const auto& p=m_preparation_progress[z];
            const auto state=p?p->snapshot():0;
            static constexpr const char* steps[]={"vita.loading.read","vita.loading.build","vita.loading.load","vita.loading.save","vita.loading.masks","vita.loading.ready"};
            m_hud->set_status(ui::tr("vita.loading.title")+": "+std::to_string(state&255)+" % - "+ui::tr(steps[std::min(state>>8,5U)]));
        }
#endif
        m_hud->set_rotation(m_camera.view().rotation);
        m_hud->set_building_rotation(m_tool.rotation());
        m_hud->set_build_tool(m_tool.mode() == BuildTool::Mode::Build && m_tool.selected() != nullptr,
                              m_tool.mode() == BuildTool::Mode::Build && m_tool_short_of_wood);
        if (m_hud->mode() == Hud::kModeInfo && (now >= m_figures_ms + 1000 || now < m_figures_ms)) {
            // The player status: sums over all cities and units, once a second is enough.
            m_player_figures = sim::player_figures(m_state->world, m_buildings, m_figures, m_local_player);
            m_player_points = sim::player_score(m_state->world, m_buildings, m_local_player);
            m_figures_ms = now;
        }
        const sim::Player* local = m_state->world.player(m_local_player);
        m_hud->set_resources(resources());
        // "Bewertung" would stop the game on one computer only: alone only (Screen_PlayerStatus).
        // Never in the tutorial (0x0043D76B).
        m_hud->set_points(m_player_points, humans() <= 1 && !m_state->world.game_settings().tutorial);
        m_hud->draw(m_state->world, m_buildings, m_banners, corners, now, m_player_figures,
                    static_cast<std::uint8_t>(local != nullptr ? std::min<int>(local->banner, 255) : 0));
        if (m_video->in_minimap()) {
            // The video of a message over the minimap; the game goes on (ADR 0010).
            m_video->draw_in(m_hud->minimap_area());
        }
    }
    present();
}

void MapViewer::apply_production_events() {
    const std::vector<sim::ProductionEvent> production = m_state->simulation.take_production_events();
    if (m_hud != nullptr && m_hud->texts() != nullptr) {
        m_stock_notes.add(m_state->world, m_buildings, *m_hud->texts(), production, m_local_player, stock_note_time());
    }
    for (const sim::ProductionEvent& e : production) {
        if (e.kind != sim::ProductionEvent::Kind::Started && e.kind != sim::ProductionEvent::Kind::Stopped) {
            continue;
        }
        // The picture continues smoothly; the phase follows the animation clock and so stays with
        // the presentation, the game state keeps its own (ADR 0004).
        const sim::Production& p = m_state->world.productions()[e.production];
        const sim::MapCell& cell = m_state->world.islands()[p.island].map.top(p.x, p.y);
        if (const assets::BuildingType* b = m_buildings.by_sprite(cell.sprite)) {
            const std::uint8_t phase = m_production_phases.get(e.production).value_or(p.anim_phase);
            m_production_phases.set(e.production,
                                    render::phase_after_change(*b, phase, m_clock.frame(*b),
                                                               e.kind == sim::ProductionEvent::Kind::Started));
        }
    }
    // Messages for the local player.
    show_notices(m_events.city_events(m_state->world, m_local_player, m_state->simulation.take_city_events()));
    const std::vector<sim::ShipEvent> ship_events = m_state->simulation.take_ship_events();
    m_sound->ships(m_state->world, m_figures, ship_events, m_local_player, m_state->world.time_ms());
    m_events.ship_events(m_state->world, m_local_player, ship_events);
    m_combat.ships(m_state->world, m_figures, ship_events);
    show_notices(m_events.disaster_events(m_state->world, m_local_player, m_state->simulation.take_disaster_events()));
    show_notices(m_events.island_events(m_state->world, m_local_player, m_state->simulation.take_island_events()));
    show_notices(m_events.volcano_events(m_state->world, m_local_player, m_state->simulation.take_volcano_events()));
}

void MapViewer::collect_effects() {
    m_effects.clear();
    const sim::VolcanoState& v = m_state->world.volcanoes();
    for (const sim::Eruption& e : v.eruptions) {
        const sim::WorldIsland& island = m_state->world.islands()[e.crater.island];
        render::EffectFigure f;
        f.x = island.x + e.crater.x + 2.0;  // the centre of the 4x4 mountain
        f.y = island.y + e.crater.y + 2.0;
        f.z = sim::kVolcanoHeight;
        f.figure = sim::kFigureVolcano;
        f.island = e.crater.island;
        m_effects.push_back(f);
    }
    const std::uint64_t now_us = m_state->world.time_ms() * 1000;
    for (const sim::LavaLump& p : v.lava) {
        const sim::LavaPoint at = sim::lava_position(p, now_us);
        render::EffectFigure f;
        f.x = at.x;
        f.y = at.y;
        f.z = at.z;
        f.ground = sim::lava_ground(m_state->world, m_buildings, at.x, at.y);
        f.figure = sim::kFigureLava;
        f.island = p.island;
        f.anim = p.anim;
        f.phase = static_cast<std::uint32_t>(p.launch_us / 1000);
        m_effects.push_back(f);
    }
    const auto& combat = m_combat.effects();
    m_effects.insert(m_effects.end(), combat.begin(), combat.end());
    const auto& ambient = m_ambient.effects();
    m_effects.insert(m_effects.end(), ambient.begin(), ambient.end());
}

void MapViewer::update_ambient() {
    // Decided tick by tick through the simulation's observer; here only the pictures and sounds.
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window->output_size(w, h);
    render::AmbientView view;
    view.game_speed = m_state->simulation.speed();
    view.tick_fraction = m_tick_fraction;
    view.local_player = m_local_player;
    // Islands on screen: those overlapping the box of the tiles at the screen's corners and edges.
    std::uint32_t x0 = m_state->world.width();
    std::uint32_t y0 = m_state->world.height();
    std::uint32_t x1 = 0;
    std::uint32_t y1 = 0;
    for (const double u : {0.0, 0.5, 1.0}) {
        for (const double v : {0.0, 0.5, 1.0}) {
            if (const auto t = m_camera.tile_at_screen(u * w, v * h)) {
                x0 = std::min(x0, t->x);
                y0 = std::min(y0, t->y);
                x1 = std::max(x1, t->x);
                y1 = std::max(y1, t->y);
            }
        }
    }
    // What is drawn: the box of the screen's corners in world tiles (also beyond the map's edge),
    // widened by a few tiles for tall pictures and high ground.
    {
        constexpr double kMargin = 6.0;
        double bx0 = std::numeric_limits<double>::infinity();
        double by0 = bx0;
        double bx1 = -bx0;
        double by1 = -bx0;
        for (const auto& [u, v] :
             {std::pair{0.0, 0.0}, std::pair{1.0, 0.0}, std::pair{0.0, 1.0}, std::pair{1.0, 1.0}}) {
            const auto [px, py] = m_camera.point_at_screen(u * w, v * h);
            bx0 = std::min(bx0, px);
            by0 = std::min(by0, py);
            bx1 = std::max(bx1, px);
            by1 = std::max(by1, py);
        }
        view.drawn = render::TileBox{bx0 - kMargin, by0 - kMargin, bx1 + kMargin, by1 + kMargin};
        m_drawn_tiles = view.drawn;
    }
    view.islands.assign(m_state->world.islands().size(), false);
    for (std::size_t i = 0; i < m_state->world.islands().size(); ++i) {
        const sim::WorldIsland& island = m_state->world.islands()[i];
        view.islands[i] = island.x <= x1 && island.x + island.map.width() > x0 && island.y <= y1 &&
                          island.y + island.map.height() > y0;
    }
    m_ambient.frame(m_state->world, m_buildings, m_figures, view);
    m_sound->scenery(m_ambient.take_sounds());
}

void MapViewer::place_listener() {
    const render::Viewport v = m_camera.viewport();
    const render::WorldView view = m_camera.view();
    const double cx = v.left + v.width / 2.0;
    const double cy = v.top + v.height / 2.0;
    const double hw = std::max(1.0, v.width / 2.0);
    const double hh = std::max(1.0, v.height / 2.0);
    const double large_per_pixel = m_camera.scale() / m_camera.magnification();
    m_sound->set_view(
        [view, cx, cy, hw, hh](double tx, double ty) {
            // World pixels of the point: the tile centres around it, in between by its fraction.
            const auto x0 = static_cast<std::uint32_t>(std::max(0.0, std::floor(tx)));
            const auto y0 = static_cast<std::uint32_t>(std::max(0.0, std::floor(ty)));
            std::int32_t ax = 0;
            std::int32_t ay = 0;
            std::int32_t bx = 0;
            std::int32_t by = 0;
            std::int32_t c_x = 0;
            std::int32_t c_y = 0;
            render::world_tile_center(view, x0, y0, ax, ay);
            render::world_tile_center(view, x0 + 1, y0, bx, by);
            render::world_tile_center(view, x0, y0 + 1, c_x, c_y);
            const double fx = tx - x0;
            const double fy = ty - y0;
            const double px = ax + fx * (bx - ax) + fy * (c_x - ax);
            const double py = ay + fx * (by - ay) + fy * (c_y - ay);
            return std::make_pair(static_cast<float>((px - cx) / hw), static_cast<float>((py - cy) / hh));
        },
        static_cast<float>(v.width), static_cast<float>(v.height),
        // The view in pixels of the large zoom level.
        audio::scenery_reach(v.width * large_per_pixel, v.height * large_per_pixel));
}

void MapViewer::show_notices(const std::vector<Notice>& notices) {
    for (const Notice& n : notices) {
        if (const auto* a = std::get_if<Announcement>(&n)) {
            // The listener puts it into the status bar.
            core::profile::mark("announcement",m_state->simulation.next_tick());
            if(const auto* message=m_messages.get(a->message);message&&message->meldung){
                const auto id=*message->meldung;
                if(id>=0x2A&&id<=0x30)core::profile::mark("discovery",m_state->simulation.next_tick());
                if(id>=0x34&&id<=0x3B)core::profile::mark("goods_message",m_state->simulation.next_tick());
            }
            m_sound->message(a->message, a->tile, a->about, a->text, a->voice);
            if (const auto video = m_sound->take_video()) {
                m_video->request(*video, a->tracks);
            }
        } else if (const auto* v = std::get_if<DirectVideo>(&n)) {
            m_video->request(v->number);
        } else if (const auto* u = std::get_if<ScoreUpdate>(&n)) {
            update_high_scores(u->defeated);
            if (u->progress) {
                record_progress();
            }
        }
    }
}

bool MapViewer::announce(std::string_view id, const MessageArgs& args, std::optional<std::pair<double, double>> tile) {
    const auto message = m_messages.find(id);
    if (!message) {
        return false;
    }
    Announcement a;
    a.message = *message;
    a.tile = tile;
    a.text = m_messages.text(*message, ui::current_language(), args);
    show_notices({a});
    return true;
}

std::pair<std::uint8_t, std::optional<std::uint16_t>> MapViewer::fertility_view_crop() const {
    // Only after Ctrl+B: for the field, plantation or farm in the build tool, else the selected farm.
    if (!m_show_fertility) {
        return {std::uint8_t{0}, std::nullopt};
    }
    if (m_tool.mode() == BuildTool::Mode::Build) {
        if (const assets::BuildingType* t = m_tool.selected()) {
            if (const std::uint8_t crop = fertility_crop(m_buildings, *t); crop != 0) {
                return {crop, std::nullopt};
            }
        }
    }
    if (!m_hud || m_hud->selection().kind != Selection::Kind::Building) {
        return {std::uint8_t{0}, std::nullopt};
    }
    const Selection& sel = m_hud->selection();
    if (sel.island >= m_state->world.islands().size()) {
        return {std::uint8_t{0}, std::nullopt};
    }
    const sim::SubTile t = m_state->world.islands()[sel.island].map.sub_tile(m_buildings, sel.x, sel.y);
    return {t.building != nullptr ? fertility_crop(m_buildings, *t.building) : std::uint8_t{0}, sel.island};
}

void MapViewer::add_fertility_marks() {
    const auto [crop, selected_island] = fertility_view_crop();
    if (crop == 0) {
        return;
    }
    // The island under the mouse, else the selected farm's.
    std::optional<std::uint16_t> island = selected_island;
    if (const auto spot = m_input->spot_at_mouse()) {
        island = spot->island;
    }
    if (island) {
        fertility_marks(m_state->world, m_buildings, m_local_player, *island, crop, m_tile_marks);
    }
}

void MapViewer::toggle_fertility() {
    m_show_fertility = !m_show_fertility;
    if (m_show_fertility && fertility_view_crop().first == 0) {
        // Nothing to show: it stays off, so the next Ctrl+B with a farm or field turns it on.
        m_show_fertility = false;
        notify(ui::tr("overlay.fertility_none"), "overlay.fertility_none");
    }
}

void MapViewer::notify(std::string text, std::string_view log) {
    m_events.notify(std::move(text), log, platform::ticks_ms());
}

void MapViewer::toggle_pause_video() {
    if (m_session && !m_session->may_pause()) {
        // Only the host pauses (MS-20): the order goes in to tell the player, the video stays off.
        m_command_sink.submit(
            sim::GameSpeedOrder{m_local_player, sim::GameSpeedOrder::Op::Pause, m_state->simulation.speed()});
        return;
    }
    const bool on = m_video->toggle_pause(m_state->simulation.paused());
    m_command_sink.submit(sim::GameSpeedOrder{m_local_player,
                                              on ? sim::GameSpeedOrder::Op::Pause : sim::GameSpeedOrder::Op::Resume,
                                              m_state->simulation.speed()});
}

double MapViewer::stock_note_time() const {
    // Between the ticks, so that the texts rise smoothly.
    return static_cast<double>(m_state->world.time_ms()) + m_tick_fraction * sim::Simulation::kTickMs;
}

void MapViewer::draw_stock_notes() {
    const double now = stock_note_time();
    m_stock_notes.expire(now);
    if (m_hud == nullptr || m_stock_notes.notes().empty()) {
        return;
    }
    const render::WorldView view = render::fit_view(m_camera.view(), m_state->world);
    std::vector<ui::DrawItem> items;
    items.reserve(m_stock_notes.notes().size());
    for (const StockNote& n : m_stock_notes.notes()) {
        // Figures_DrawRow 0x0044FA90, kind 7: centred, white with a black shadow (colour 0), in the
        // small font (ZEILITTLE) whatever the zoom.
        const auto [px, py] = render::text_figure_point(view, n.x, n.y, n.height(now));
        double sx = 0.0;
        double sy = 0.0;
        m_camera.world_to_screen(px, py, sx, sy);
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = 1;
        d.colour = 0;
        d.align = ui::Align::Centre;
        d.x = static_cast<std::int32_t>(std::floor(sx));
        d.y = static_cast<std::int32_t>(std::floor(sy));
        d.text = n.text;
        items.push_back(std::move(d));
    }
    m_hud->draw_over_map(items);
}

void MapViewer::draw_unit_arms() {
    const sim::Player* local = m_state->world.player(m_local_player);
    if (m_hud == nullptr || !m_show_unit_arms || local == nullptr) {
        return;
    }
    // As the History Edition shows them: a small coat of arms on a thin pole over each of the
    // player's own units on land, none over ships or other players' units. Heights in half tile
    // widths over the figure's feet, so the mark grows and shrinks with the zoom.
    constexpr double kPoleFoot = 1.4;
    constexpr double kPoleTop = 2.0;
    constexpr double kArmsTop = 2.9;
    const render::WorldView view = render::fit_view(m_camera.view(), m_state->world);
    const ui::Rect& v = m_hud->game_view();
    const auto screen = [&](const render::FigureInstance& f, double z) {
        const auto [px, py] = render::text_figure_point(view, f.x, f.y, f.z + z);
        double sx = 0.0;
        double sy = 0.0;
        m_camera.world_to_screen(px, py, sx, sy);
        return std::pair{static_cast<std::int32_t>(std::floor(sx)), static_cast<std::int32_t>(std::floor(sy))};
    };
    std::vector<Hud::UnitArms> marks;
    for (const sim::Soldier& s : m_state->world.soldiers()) {
        if (s.owner != m_local_player || s.away()) {
            continue;
        }
        const render::FigureInstance* f = m_soldiers.find(s.id);
        if (f == nullptr) {
            continue;
        }
        const auto [x, foot] = screen(*f, kPoleFoot);
        const auto top = screen(*f, kPoleTop).second;
        const auto arms = screen(*f, kArmsTop).second;
        const std::int32_t h = std::max(4, top - arms);
        if (x < v.x - h || x > v.x + v.w + h || foot < v.y || arms > v.y + v.h) {
            continue;
        }
        marks.push_back(Hud::UnitArms{ui::Rect{x - h / 2, top - h, h, h}, x, top, foot});
    }
    m_hud->draw_unit_arms(marks, local->banner);
}

void MapViewer::draw_corner_info() {
    if (m_hud == nullptr || (!m_show_fps && !m_show_coordinates)) {
        return;
    }
    std::vector<std::string> lines;
    if (m_show_fps) {
        lines.push_back(ui::tr("overlay.fps", {std::to_string(static_cast<long>(std::lround(m_fps)))}));
    }
    if (m_show_coordinates) {
        if (const auto tile = m_input->tile_at_mouse()) {
            const sim::World::Cell& cell = m_state->world.cell(tile->x, tile->y);
            lines.push_back(
                cell.island == sim::World::kSea
                    ? ui::tr("overlay.coordinates", {std::to_string(tile->x), std::to_string(tile->y)})
                    : ui::tr("overlay.coordinates_island",
                             {std::to_string(tile->x), std::to_string(tile->y), std::to_string(cell.island + 1),
                              std::to_string(cell.local_x), std::to_string(cell.local_y)}));
        }
    }
    // Under the right end of the top bar, in the small font, white with a black shadow.
    const ui::Rect& v = m_hud->game_view();
    const double scale = m_hud->scale();
    std::vector<ui::DrawItem> items;
    std::int32_t y = v.y + ui::to_pixels(40, scale);
    for (std::string& line : lines) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = 1;
        d.colour = 0;
        d.align = ui::Align::Right;
        d.x = v.x + v.w - ui::to_pixels(8, scale);
        d.y = y;
        d.text = std::move(line);
        items.push_back(std::move(d));
        y += ui::to_pixels(14, scale);
    }
    m_hud->draw_over_map(items);
}

void MapViewer::draw_map(ZoomGraphics& graphics) {
    // The canvas costs what is drawn into it: at most twice the window's pixels (and 16 million)
    // keeps weak GPUs fluent; just above a whole scale that means the scale below, stretched a
    // little (slightly softer) instead of the one above, shrunk. Without a GPU (SDL's software
    // renderer) never more than the window: the scale below, which is cheaper than drawing directly.
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window->output_size(w, h);
    // O1602_MAP_CANVAS=N: N times the window's pixels instead (to try it on a GPU, 0: no limit).
    static const std::optional<std::int64_t> wanted = [] {
        const auto value = core::environment_variable("O1602_MAP_CANVAS");
        return value ? std::optional<std::int64_t>(std::strtoll(value->c_str(), nullptr, 10)) : std::nullopt;
    }();
    const std::int64_t times = wanted ? std::max<std::int64_t>(*wanted, 0) : (m_software_renderer ? 1 : 2);
    const std::int32_t max_side = m_window->max_texture_size();
    const std::int64_t max_pixels = times == 0 ? 0 : std::min<std::int64_t>(std::int64_t{w} * h * times, 16'777'216);
    render::RenderPlan plan = m_camera.render_plan(max_pixels, max_side > 0 ? max_side : 8192);
    if (!plan.direct && !m_app_settings.display.smooth_zoom) {
        plan.direct = true;  // the player wants plain pixels: drawn straight at the scale, unevenly
    }
    if (plan.direct || !m_map_canvas || !m_map_canvas->begin(plan)) {
        double ox = 0.0;
        double oy = 0.0;
        m_camera.screen_origin(ox, oy);
        if (m_map_canvas) {
            m_map_canvas->begin(render::RenderPlan{});  // nothing went through it this frame
        }
        m_sprites_drawn = graphics.renderer->draw(m_commands, ox, oy, m_camera.scale());
        m_batches_drawn = graphics.renderer->batches();
    } else {
    m_sprites_drawn = graphics.renderer->draw(m_commands, plan.origin_x, plan.origin_y, plan.scale);
    m_batches_drawn = graphics.renderer->batches();
    m_map_canvas->end();
    }
#ifdef O1602_EXPERIMENTAL_VITA
    if(!graphics.renderer->error().empty()) {
        if(m_last_graphics_error!=graphics.renderer->error()) {
            m_last_graphics_error=graphics.renderer->error();
            m_events.notify(ui::tr("vita.graphics.error",{m_last_graphics_error}),"vita.cache_failed",platform::ticks_ms());
            platform::vita::trace("CACHE_RECOVERABLE %s",m_last_graphics_error.c_str());
        }
        if(m_last_good_zoom&&*m_last_good_zoom!=m_camera.view().zoom) {
            m_camera.set_zoom(*m_last_good_zoom,m_zoom_anchor_x,m_zoom_anchor_y);
            m_zoom_target=m_camera.magnification();
        }
    } else {m_last_good_zoom=m_camera.view().zoom;m_last_graphics_error.clear();}
#endif
}

void MapViewer::toggle_fullscreen() {
    DisplaySettings display = m_app_settings.display;
    display.fullscreen = !m_window->fullscreen();
    apply_display(display);
    if (m_hud && m_hud->display_open()) {
        m_hud->open_display(display_model());  // the window shows the change
    }
}

DisplayModel MapViewer::display_model() const {
    DisplayModel model;
    model.settings = m_app_settings.display;
    model.chat_available=m_hud&&m_hud->chat_available();
    m_window->window_size(model.window.width, model.window.height);
    std::int32_t uw = 0;
    std::int32_t uh = 0;
    m_window->usable_area(uw, uh);
    model.sizes = window_sizes_for(uw, uh);
#ifdef O1602_EXPERIMENTAL_VITA
    model.fixed_window = true; model.window = {960, 544}; model.sizes.clear();
#endif
    return model;
}

void MapViewer::remember_window_size() {
    if (m_hud) {
        WindowSize size;
        m_window->window_size(size.width, size.height);
        m_hud->set_display_window(size);
    }
    // Only a window the player sized: not full screen, not maximised (it comes back as it was).
    if (m_window->fullscreen() || m_window->maximized()) {
        return;
    }
    m_window->window_size(m_app_settings.display.window_width, m_app_settings.display.window_height);
}

void MapViewer::apply_display(const DisplaySettings& input) {
    DisplaySettings display=input;
#ifdef O1602_EXPERIMENTAL_VITA
    normalize_vita_display(display);
#endif
    const DisplaySettings before = m_app_settings.display;
    m_app_settings.display = display;
#ifdef O1602_EXPERIMENTAL_VITA
    m_app_settings.display.window_width=960; m_app_settings.display.window_height=544;
    platform::vita::stick_sensitivity(display.vita_left_stick,display.vita_right_stick);
    if(display.vita_cpu_mhz!=before.vita_cpu_mhz&&!platform::vita::cpu_profile(unsigned(display.vita_cpu_mhz))){
        m_events.notify(ui::tr("vita.display.cpu_failed"),"vita.cpu_failed",platform::ticks_ms());
    }
#else
    if (display.fullscreen != m_window->fullscreen()) {
        m_window->set_fullscreen(display.fullscreen);
    }
    if (!display.fullscreen && display.window_width > 0 && display.window_height > 0 &&
        (display.window_width != before.window_width || display.window_height != before.window_height)) {
        m_window->set_window_size(display.window_width, display.window_height);
    }
#endif
    if (display.vsync != before.vsync) {
        m_window->set_vsync(display.vsync);
    }
    if (m_hud) {
        m_hud->set_goods_trend(display.goods_trend);
#ifdef O1602_EXPERIMENTAL_VITA
        m_hud->set_handheld_chat(display.chat_button);
#endif
    }
    if (display.classic_graphics != before.classic_graphics) {
        // The dithered sheets are only in the atlas while they are used.
        start_preloading();
        reload_graphics();
    }
    if (display.free_zoom != before.free_zoom) {
        m_zoom_target = 0.0;
        if (!display.free_zoom) {
            // Back to the original's levels: the current level at the screen's whole pixel scale.
            std::int32_t w = 0;
            std::int32_t h = 0;
            m_window->output_size(w, h);
            m_camera.set_scale(m_display_pixel_scale, w / 2.0, h / 2.0);
        }
    }
    // The window's size and full screen arrive as events (the layout follows them); the
    // interface's size is laid out now.
    lay_out();
    if (m_hud) {
        m_hud->options_model().fullscreen = display.fullscreen;
    }
    save_settings();
}

void MapViewer::follow_mouse_grab(bool video_full_screen) {
    // Full screen keeps the mouse for edge scrolling (as the original's did), so that it does not
    // slip onto a second screen, but lets it go while edge scrolling pauses (a menu or a window is
    // open, a video plays), so that the mouse can reach another screen then. In a window only if
    // the player asks for it, then always.
    const DisplaySettings& display = m_app_settings.display;
    const bool scrolling = display.edge_scroll && m_window->fullscreen() && !video_full_screen &&
                           !(m_input && m_input->edge_scroll_paused());
    m_window->set_mouse_grab(display.grab_mouse || scrolling);
}

void MapViewer::lay_out() {
    if (!m_hud) {
        return;
    }
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window->output_size(w, h);
    const ui::Rect view =
        m_hud->layout(w, h, ui_scale_for(m_app_settings.display.ui_scale, w, h, m_window->display_scale()));
    m_camera.set_screen(view.w, view.h, m_camera.scale());
}

void MapViewer::follow_display_scale() {
    const std::int32_t scale = render::pixel_scale_for(m_window->display_scale(), kMaxPixelScale);
    if (scale == m_display_pixel_scale) {
        return;
    }
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window->output_size(w, h);
    if (m_app_settings.display.free_zoom) {
        // The same size on the new screen: the magnification grows or shrinks with its scale.
        const double m = m_camera.magnification() * scale / m_display_pixel_scale;
        m_display_pixel_scale = scale;
        m_zoom_target = 0.0;
        show_magnification(m, w / 2.0, h / 2.0);
    } else {
        // Ctrl +/- of the player stays on top of the screen's scale.
        const auto own = static_cast<std::int32_t>(std::lround(m_camera.scale())) - m_display_pixel_scale;
        m_display_pixel_scale = scale;
        m_camera.set_scale(std::clamp(scale + own, 1, kMaxPixelScale), w / 2.0, h / 2.0);
    }
    lay_out();  // the interface's size may follow the desktop's scale
    std::fprintf(stderr, "display scale %.2f: map scale %.2f\n", static_cast<double>(m_window->display_scale()),
                 m_camera.scale());
}

void MapViewer::recreate_renderer() {
    std::fprintf(stderr, "the render device was lost: the renderer is made again\n");
    // Every texture goes before the old renderer does; the world's sprites with their renderers.
    for (auto& g : m_graphics) {
        g.reset();
    }
#ifdef O1602_EXPERIMENTAL_VITA
    m_vita_cache.reset();
#endif
    if (m_hud) {
        m_hud->release_textures();
    }
    m_video->release_texture();
    m_map_canvas->release();
    if (auto status = m_window->recreate_renderer(); !status) {
        std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
        m_running = false;
        return;
    }
    SDL_Renderer* renderer = m_window->sdl_renderer();
#ifdef O1602_EXPERIMENTAL_VITA
    m_vita_cache=render::PagedSpriteCache::acquire(renderer);
#endif
    if (m_hud) {
        m_hud->set_renderer(renderer);
    }
    m_video->set_renderer(renderer);
    m_map_canvas->set_renderer(renderer);
    m_software_renderer = m_window->renderer_name() == "software";
    std::fprintf(stderr, "renderer: %s\n", m_window->renderer_name().c_str());
    reload_graphics();
}

void MapViewer::reload_graphics() {
    // The world's sprites keep no pixels after the upload (drop_pixels): they are loaded again in
    // the background, the one shown first; until it is there the map stays black (FB-11).
    for (auto& g : m_graphics) {
        g.reset();
    }
#ifdef O1602_EXPERIMENTAL_VITA
    const std::array order{m_camera.view().zoom};m_preloader->request(order);
#else
    m_preloader->request(render::preload_order(m_camera.view().zoom));
#endif
    m_graphics_reloading = true;
}

void MapViewer::recreate_textures() {
    m_map_canvas->release();
    reload_graphics();
    if (m_hud) {
        m_hud->release_textures();
    }
    m_video->recreate_texture();
}

Hud::Resources MapViewer::resources() const {
    core::profile::Scope timer(core::profile::HudResources);
    Hud::Resources r;
    const sim::Player* local = m_state->world.player(m_local_player);
    if (local != nullptr && local->role == sim::PlayerRole::Human) {
        r.money = local->money;
    }
    if (m_pending_bill) {
        r.money_cost = m_pending_bill->money;
    }
    // The city on the tile under the mouse.
    const auto tile = m_input->tile_at_mouse();
    if (!tile || tile->x >= m_state->world.width() || tile->y >= m_state->world.height()) {
        return r;
    }
    const sim::World::Cell& cell = m_state->world.cell(tile->x, tile->y);
    if (cell.island == sim::World::kSea) {
        return r;
    }
    const sim::WorldIsland& island = m_state->world.islands()[cell.island];
    const sim::City* city = m_state->world.city(cell.island, island.map.top(cell.local_x, cell.local_y).city);
    if (city == nullptr || !island.discovered_by(m_local_player)) {
        return r;
    }
    if (city->owner == m_local_player) {
        constexpr std::array<std::uint8_t, 3> kWares{sim::ware::kTools, sim::ware::kWood, sim::ware::kBricks};
        std::array<std::int64_t, 3> goods{};
        for (std::size_t i = 0; i < 3; ++i) {
            goods[i] = city->stock(kWares[i]) >> 5U;
        }
        r.goods = goods;
        if (m_pending_bill) {
            r.goods_cost = std::array<std::int64_t, 3>{m_pending_bill->tools >> 5U, m_pending_bill->wood >> 5U,
                                                       m_pending_bill->bricks >> 5U};
        }
    }
    // Einwohner as the original counts them here (0x0040D1C8 calls City_Einwohner 0x0047F790):
    // residents plus the workers of the city's buildings, the same figure as the player status,
    // the city window and the population messages.
    auto& c=m_worker_cache;const auto tick=m_state->simulation.next_tick();
    if(c.state!=m_state.get()||c.tick!=tick||c.construction!=m_construction_serial||c.island!=city->island||c.city!=city->index){
        c={m_state.get(),tick,m_construction_serial,city->island,city->index,sim::city_workers(m_state->world,m_buildings,*city)};
    }
    r.inhabitants = sim::city_einwohner(*city,c.workers);
    // The owner's coat of arms for humans and computer opponents alike: 0x0040D22F tests the
    // player kind (0x005B7680 + 0x280 * owner) for 0 or 0x0C and draws the plain shield only
    // for the other factions.
    const sim::Player* owner = m_state->world.player(city->owner);
    if (owner != nullptr && (owner->role == sim::PlayerRole::Human || owner->role == sim::PlayerRole::Computer)) {
        r.shield = owner->banner;
    }
    return r;
}

void MapViewer::apply_hud(const Hud::Action& action) {
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::touch_action("hud",unsigned(action.kind));
#endif
    const ui::Rect view = m_hud->game_view();
    const double cx = view.w / 2.0;
    const double cy = view.h / 2.0;
    switch (action.kind) {
        case Hud::Action::Kind::RotateLeft:
            m_camera.set_rotation(static_cast<std::uint8_t>(m_camera.view().rotation + 3), cx, cy);
            break;
        case Hud::Action::Kind::RotateRight:
            m_camera.set_rotation(static_cast<std::uint8_t>(m_camera.view().rotation + 1), cx, cy);
            break;
        case Hud::Action::Kind::ZoomIn:
            zoom_by(-kKeyZoomSteps, cx, cy);
            break;
        case Hud::Action::Kind::ZoomOut:
            zoom_by(kKeyZoomSteps, cx, cy);
            break;
        case Hud::Action::Kind::Minimap:
        case Hud::Action::Kind::Message:
            m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, action.tile_x)),
                                               static_cast<std::uint32_t>(std::max(0.0, action.tile_y))});
            break;
        case Hud::Action::Kind::Rating:
            // "Bewertung" (INFO.GAD 57028): the score sheets while the game stands still.
            show_end_screen(local_outcome(), true);
            break;
        case Hud::Action::Kind::SelectShip:
            // The ship list (0x00434020): look at the ship and select it.
            m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, action.tile_x)),
                                               static_cast<std::uint32_t>(std::max(0.0, action.tile_y))});
            if (m_state->world.ship(action.ship) != nullptr) {
                m_input->units().select(m_state->world, ui::UnitRef{ui::UnitRef::Kind::Ship, action.ship});
                m_input->selection_changed(false);
            }
            break;
        case Hud::Action::Kind::Build:
            if (m_tool.select(action.building)) {
                m_tool.set_mode(BuildTool::Mode::Build);
                m_input->drop_units();
#ifdef O1602_EXPERIMENTAL_VITA
                platform::vita::tool_selected();
#endif
            } else {
                m_tool.set_mode(BuildTool::Mode::Off);  // not the building picked before
            }
            break;
        case Hud::Action::Kind::Demolish:
            m_tool.set_mode(BuildTool::Mode::Demolish);
            m_input->drop_units();
            break;
        case Hud::Action::Kind::TurnBuilding:
            m_tool.rotate(action.step);
            break;
        case Hud::Action::Kind::ToolOff:
            m_tool.set_mode(BuildTool::Mode::Off);
            break;
        case Hud::Action::Kind::OrientToRoad:
            m_tool.set_orient_to_road(action.step != 0);
            break;
        case Hud::Action::Kind::Answer:
            if (m_fetch_offer) {
                answer_package_offer(action.step != 0);
                break;
            }
            if (std::exchange(m_quit_pending, false)) {
                if (action.step == 0) {
                    break;
                }
                if (m_hud && m_hud->menu_active()) {
                    m_running = false;  // "Beenden" in the main menu
                    break;
                }
                // "Ende": the game is kept as lastgame (0x0043BCF0), then back to the main menu
                // (no score sheet, no high score), or the end without one.
                const auto status = save_slot(kLastGameSlot, std::string(kLastGameName));
                if (!status) {
                    std::fprintf(stderr, "lastgame: %s\n", status.error().message.c_str());
                }
                // Leaving a multiplayer game leaves its session: the others go on without us (or
                // learn that the host left), and the menu can open or join the next one.
                end_session();
                if (m_hud && m_hud->menu()) {
                    m_events.drop_end_screen();
                    {core::profile::Scope opening(core::profile::MenuOpen);m_hud->open_menu();}
                } else {
                    m_running = false;
                }
                break;
            }
            // The question before scuttling, disbanding or war.
            m_input->answer(action.step != 0);
            break;
        case Hud::Action::Kind::EndTutorial: {
            // „Einführungsspiel beenden“ (0x004326E0): the voice stops, the next chapter is kept in
            // the settings and starts at once; after the last one the main menu, at chapter 0 again.
            if (m_sound) {
                m_sound->stop_voices();
            }
            const auto next = Tutorial::next_chapter(static_cast<std::uint8_t>(action.step));
            m_app_settings.tutorial_chapter = next.value_or(0);
            save_settings();
            MainMenu* menu = m_hud->menu();
            if (menu != nullptr) {
                menu->set_tutorial_chapter(m_app_settings.tutorial_chapter);
            }
            if (next && menu != nullptr) {
                const std::filesystem::path path = menu->tutorial_file(*next);
                const core::Status status =
                    path.empty() ? core::Status(core::make_error(core::ErrorCode::NotFound, "no such chapter"))
                                 : load_game(path);
                if (status) {
                    break;
                }
                std::fprintf(stderr, "tutorial: %s\n", status.error().message.c_str());
            }
            m_events.drop_end_screen();
            if (menu != nullptr) {
                {core::profile::Scope opening(core::profile::MenuOpen);m_hud->open_menu();}
            } else {
                m_running = false;
            }
            break;
        }
        case Hud::Action::Kind::FoundKontor:
            if (m_tool.select_kontor(action.ship)) {
                m_tool.set_mode(BuildTool::Mode::Build);
                if (const assets::BuildingType* kontor = m_tool.selected()) {
                    m_hud->found_kontor(action.ship, kontor->id);
                }
            }
            break;
    }
}

void MapViewer::apply_game_events() {
    const std::vector<sim::MilitaryEvent> military = m_state->simulation.take_military_events();
    m_sound->combat(m_state->world, m_figures, military, m_local_player, m_state->world.time_ms());
    m_combat.military(m_state->world, m_buildings, m_figures, military);
    m_events.set_rival_wars(m_app_settings.display.rival_wars);
    show_notices(
        m_events.diplomacy_notices(m_state->world, m_local_player, m_state->simulation.take_diplomacy_notices()));
    show_notices(m_events.game_events(m_state->world, m_local_player, m_state->simulation.take_game_events(),
                                      platform::ticks_ms()));
    apply_script_output();
}

void MapViewer::apply_script_output() {
    if (!m_state->scripts) {
        return;
    }
    for (const script::ScriptNote& note : m_state->scripts->take_notes()) {
        std::fprintf(stderr, "script %s: %s\n", note.package.c_str(), note.text.c_str());
        if (note.error) {
            const std::string text = core::utf8_to_cp1252(note.package + ": " + note.text);
            m_events.notify(ui::tr("error.script", {text}), "error.script " + note.package, platform::ticks_ms());
        }
    }
    for (const script::ScriptMessage& message : m_state->scripts->take_messages()) {
        if (message.player != sim::kNoPlayer && message.player != m_local_player) {
            continue;
        }
        std::vector<std::pair<std::string, std::string>> values;
        for (const auto& [name, value] : message.args) {
            values.emplace_back(name, core::utf8_to_cp1252(value));
        }
        MessageArgs args;
        for (const auto& [name, value] : values) {
            args.emplace_back(name, value);
        }
        if (!announce(message.id, args)) {
            std::fprintf(stderr, "script: no message \"%s\" (messages.toml)\n", message.id.c_str());
        }
    }
}

render::TilePos MapViewer::home_tile() const {
    for (const sim::City& c : m_state->world.cities()) {
        if (c.owner == m_local_player && c.warehouse_x && c.warehouse_y && c.island < m_state->world.islands().size()) {
            const sim::WorldIsland& island = m_state->world.islands()[c.island];
            return render::TilePos{island.x + *c.warehouse_x, island.y + *c.warehouse_y};
        }
    }
    for (const sim::Ship& s : m_state->world.ships()) {
        if (s.owner == m_local_player) {
            return render::TilePos{static_cast<std::uint32_t>(s.x), static_cast<std::uint32_t>(s.y)};
        }
    }
    if (!m_state->world.islands().empty()) {
        const auto& island = m_state->world.islands().front();
        return render::TilePos{island.x + island.map.width() / 2, island.y + island.map.height() / 2};
    }
    return render::TilePos{m_state->world.width() / 2, m_state->world.height() / 2};
}

std::string MapViewer::debug_title(double fps) {
    // For development (O1602_DEBUG_TITLE): what lies under the mouse and is selected, in English.
    const auto& view = m_camera.view();
    char scale[32];
    std::snprintf(scale, sizeof scale, " x%.2f%s", m_camera.scale(), m_map_canvas->used() ? " (canvas)" : "");
    std::string title = "Open1602 - " + game_title() + " | " + zoom_name(view.zoom) + scale + " | rotation " +
                        std::to_string(view.rotation);
    if (const auto tile = m_input->tile_at_mouse()) {
        title += " | tile " + std::to_string(tile->x) + "," + std::to_string(tile->y);
        const auto& cell = m_state->world.cell(tile->x, tile->y);
        if (cell.island != sim::World::kSea) {
            title += " (island " + std::to_string(cell.island);
            const auto& island = m_state->world.islands()[cell.island];
            const sim::MapCell& top = island.map.top(cell.local_x, cell.local_y);
            if (const sim::City* city = m_state->world.city(cell.island, top.city)) {
                title += ", " + core::cp1252_to_utf8(city->name);
            }
            if (const sim::Player* p = m_state->world.player(top.owner)) {
                title += ", " + player_label(*p, top.owner, m_banners, m_state->world);
            }
            title += ")";
        }
    }
    const ui::UnitSelection& units = m_input->units();
    const sim::Soldier* soldier = units.size() == 1 && units.kind() == ui::UnitRef::Kind::Soldier
                                      ? m_state->world.soldier(units.units().front().id)
                                      : nullptr;
    if (soldier != nullptr) {
        const assets::FigureType* type = m_figures.get(soldier->figure);
        const std::uint32_t max = type != nullptr ? std::max<std::uint32_t>(1, type->combat.max_health) : 1U;
        const std::uint32_t health = soldier->health != 0 ? soldier->health : max;
        title += " | unit #" + std::to_string(soldier->figure);
        if (const sim::Player* p = m_state->world.player(soldier->owner)) {
            title += " (" + player_label(*p, soldier->owner, m_banners, m_state->world) + ")";
        }
        title += " " + std::to_string(health * 100 / max) + " %";
    } else if (units.size() > 1) {
        title += " | " + std::to_string(units.size()) + " selected";
    }
    if (m_input->pick() != MapInput::Pick::None) {
        title += m_input->pick() == MapInput::Pick::Attack ? " | ATTACK" : " | PATROL";
    }
    const sim::Ship* selected_ship = units.size() == 1 && units.kind() == ui::UnitRef::Kind::Ship
                                         ? m_state->world.ship(units.units().front().id)
                                         : nullptr;
    if (const sim::Ship* ship = selected_ship) {
        title += " | ship " + core::cp1252_to_utf8(ship->name);
        if (const sim::Player* p = m_state->world.player(ship->owner)) {
            title += " (" + player_label(*p, ship->owner, m_banners, m_state->world) + ")";
        }
        char info[64];
        std::snprintf(info, sizeof info, " %.1f tiles/s", m_state->simulation.fleet().speed_milli(*ship) / 1000.0);
        title += info;
        for (const sim::CargoSlot& c : ship->cargo) {
            if (c.ware != 0) {
                title += ", " + std::to_string(c.amount / 32) + " t #" + std::to_string(c.ware);
            }
        }
        if (ship->route.active) {
            title += ", trade route";
        }
        if (ship->explore_steps != 0) {
            title += ", exploring " + std::to_string((11 - ship->explore_steps) * 100 / 11) + " %";
        } else if (ship->owner == m_local_player &&
                   m_state->simulation.fleet().explore_state(*ship, m_local_player) == sim::ExploreState::Possible) {
            title += ", X: explore island";
        }
    }
    // Over an island its player has explored: the crops, as in the original's help bar.
    if (const auto spot = m_input->spot_at_mouse()) {
        const sim::WorldIsland& isl = m_state->world.islands()[spot->island];
        if (isl.explored_by(m_local_player)) {
            title += " | " + crops_text(isl);
        }
    }
    if (m_tool.active()) {
        title += " | " + m_tool.describe();
        if (!m_tool_status.empty()) {
            title += ": " + m_tool_status;
        }
    }
    char perf[96];
    std::snprintf(perf, sizeof perf, " | %.0f fps, %u sprites, %u batches", fps, unsigned(m_sprites_drawn), unsigned(m_batches_drawn));
    title += perf;
    return title;
}

std::string MapViewer::game_title() const {
    // The game's own name, the same on every computer of a multiplayer game; else its file.
    // The History Edition writes a placeholder ("[[48]]" for "Endlosspiel").
    if (const std::string name = sim::game_identity(m_state->world).name; !name.empty()) {
        return core::cp1252_to_utf8(m_game.expand(name));
    }
    return core::path_to_utf8(m_options.scenario.stem());
}

void MapViewer::update_title(double fps) {
    // The window title: the game, its time and speed, in the player's language. With
    // O1602_DEBUG_TITLE set it also shows what lies under the mouse, the selection and the frame rate.
    // In the menus no game is played (the world behind them is only a backdrop): the name alone.
    m_fps = fps;
    const bool in_menu = m_hud && m_hud->menu_active() && !m_debug_title;
    if (in_menu) {
        m_title_real_ms = 0;  // the time in the menus does not count as a slow game
        m_window->set_title("Open1602" + session_title());
        return;
    }
    std::string title = m_debug_title ? debug_title(fps) : "Open1602 - " + game_title();
    const std::uint64_t seconds = m_state->simulation.game_time_ms() / 1000;
    char clock[32];
    std::snprintf(clock, sizeof clock, " | %02llu:%02llu:%02llu", static_cast<unsigned long long>(seconds / 3600),
                  static_cast<unsigned long long>(seconds / 60 % 60), static_cast<unsigned long long>(seconds % 60));
    title += clock;
    if (m_state->simulation.paused()) {
        title += " | " + ui::tr("title.paused");
    } else if (m_state->simulation.speed() != 1) {
        title += " | " + ui::tr("title.speed", {std::to_string(m_state->simulation.speed())});
    }
    // A computer too slow for the speed lets the game run slower (Simulation::behind()): the speed
    // reached since the last title.
    const std::uint64_t now = platform::ticks_ms();
    const std::uint64_t game_ms = m_state->simulation.game_time_ms();
    // A multiplayer game that ended or waits for a player stands still on purpose (the title says
    // why), not because this computer is too slow.
    const bool stands = m_state->simulation.paused() || (m_session && (m_session->ended() || m_session->waiting()));
    if (!stands && now > m_title_real_ms && game_ms >= m_title_game_ms && m_title_real_ms != 0) {
        const double reached =
            static_cast<double>(game_ms - m_title_game_ms) / static_cast<double>(now - m_title_real_ms);
        if (reached < 0.9 * m_state->simulation.speed()) {
            char speed[16];
            std::snprintf(speed, sizeof speed, "%.1f", reached);
            title += " | " + ui::tr("title.too_slow", {speed});
        }
    }
    m_title_real_ms = now;
    m_title_game_ms = game_ms;
    m_window->set_title(title + session_title());
}

std::string MapViewer::session_title() const {
    if (!m_session) {
        return {};
    }
    std::size_t players = 0;
    for (const net::Member& m : m_session->roster()) {
        players += m.seat != sim::kNoPlayer ? 1 : 0;
    }
    std::string title = " | " + ui::tr("title.multiplayer", {std::to_string(players)});
    if (m_session->ended()) {
        title += ", " + ui::tr("title.multiplayer_ended");
    } else if (m_session->waiting()) {
        title += ", " + ui::tr("title.multiplayer_waiting");
    }
    return title;
}

std::filesystem::path MapViewer::data_folder() {
#ifdef O1602_EXPERIMENTAL_VITA
    return "ux0:data/open1602";
#endif
    std::filesystem::path folder;
    if (char* pref = SDL_GetPrefPath(nullptr, "Open1602")) {
        folder = core::path_from_utf8(pref);
        SDL_free(pref);
    } else {
        // Never the working folder: started from the installation, the game would write into it.
        std::error_code ec;
        folder = std::filesystem::temp_directory_path(ec) / "Open1602";
        static bool warned = false;
        if (!std::exchange(warned, true)) {
            std::fprintf(stderr, "warning: no user data folder (%s), using %s\n", SDL_GetError(),
                         core::path_to_utf8(folder).c_str());
        }
    }
    return folder;
}

namespace {

/// Whether `path` lies in the folder `root`: by name, and by identity for the folders on the way
/// that exist (FB-27: on a file system that ignores case, "anno 1602" is "Anno 1602").
bool inside_folder(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path rel = path.lexically_relative(root);
    if (!rel.empty() && *rel.begin() != "..") {
        return true;
    }
    for (std::filesystem::path p = path; !p.empty(); p = p.parent_path()) {
        std::error_code ec;
        if (std::filesystem::equivalent(p, root, ec) && !ec) {
            return true;
        }
        if (p == p.parent_path()) {
            break;
        }
    }
    return false;
}

}  // namespace

std::filesystem::path MapViewer::save_folder() {
    return data_folder() / "saves";
}

core::Status MapViewer::save_game(const std::filesystem::path& path) {
    // The installation stays as it is: no savegame goes into it (nor into Game.dat). A path that
    // cannot be resolved is not written either.
    std::error_code ec;
#ifdef O1602_EXPERIMENTAL_VITA
    const std::filesystem::path target=platform::vita::save_path(path,ec);
    if(ec)return core::make_error(core::ErrorCode::Unsupported,"Vita saves must stay in ux0:data/open1602/saves: "+core::path_to_utf8(path));
#else
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    const std::filesystem::path target = ec ? std::filesystem::path() : std::filesystem::weakly_canonical(absolute, ec);
    std::error_code root_ec;
    const std::filesystem::path root = std::filesystem::weakly_canonical(m_game.root(), root_ec);
    if (ec || root_ec || target.empty() || root.empty()) {
        return core::make_error(core::ErrorCode::NotFound,
                                "cannot check the savegame's path " + core::path_to_utf8(path) +
                                    " against the installation: " + (ec ? ec : root_ec).message());
    }
    if (inside_folder(target, root)) {
        return core::make_error(core::ErrorCode::Unsupported,
                                "will not write into the game's installation: " + core::path_to_utf8(target));
    }
#endif
    // Open1602's own savegame, or with the ending .gam one the original loads (as long as the game
    // fits its limits; without the file the game came from, the fields Open1602 does not keep are 0).
    std::string ending = core::path_to_utf8(target.extension());
    std::transform(ending.begin(), ending.end(), ending.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // The groups of this computer's player go with the game (the original forgets them).
    if (m_local_player != sim::kNoPlayer && m_input) {
        m_state->world.set_unit_groups(m_local_player, m_input->saved_groups());
    }
    auto bytes = [&]{core::profile::Scope serial(core::profile::SaveSerialize);return ending == ".gam" ? sim::write_savegame(m_state->world, m_buildings, &m_state->simulation)
                                  : sim::write_game(m_state->world, m_buildings, &m_state->simulation);}();
    if (!bytes) {
        return bytes.error();
    }
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    // A failed write (a full disk) never leaves half a file in place of the last good one.
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::diagnostics_pause(true);
#endif
    auto written=[&]{core::profile::Scope writing(core::profile::SaveWrite);return core::write_file_atomic(target, *bytes);}();
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::diagnostics_pause(false);
#endif
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::trace("SAVE ok=%d path=%s bytes=%u tick=%llu hash=%llu",bool(written),target.string().c_str(),unsigned(bytes->size()),
        (unsigned long long)m_state->simulation.next_tick(),(unsigned long long)m_state->simulation.state_hash());
#endif
    return written;
}

core::Status MapViewer::save_slot(std::size_t slot, std::string name) {
    if (auto status = save_game(slot_file(save_folder(), slot)); !status) {
        return status;
    }
    std::uint8_t humans = 0;
    for (const sim::Player& p : m_state->world.players()) {
        humans = static_cast<std::uint8_t>(humans + (p.role == sim::PlayerRole::Human ? 1 : 0));
    }
    m_slots.set(slot, std::move(name), humans);
    read_slot_meta(m_slots, save_folder(), slot);
    ++m_slots_revision;
    if (m_hud) {
        OptionsModel& model = m_hud->options_model();
        model.slot_names = m_slots.names;
        model.slot_humans = m_slots.humans;
        model.slot_details = slot_details(m_slots);
    }
    const std::string text = write_slots(m_slots);
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    if (auto written =
            core::write_file_atomic(save_folder() / "slots.cfg", std::span<const std::uint8_t>(data, text.size()));
        !written) {
        std::fprintf(stderr, "slots: %s\n", written.error().message.c_str());
    }
    return core::Ok{};
}

void MapViewer::load_options() {
    // Defaults: the sounds as GameSound took them (the original's Game.dat, read only).
    if (m_sound) {
        m_app_settings.audio = m_sound->settings();
    }
    m_app_settings = load_user_settings(data_folder(), m_app_settings,
#ifdef O1602_EXPERIMENTAL_VITA
        true
#else
        false
#endif
    );
    // Command-line sound switches apply to this run without overwriting menu preferences.
    if(m_sound)m_sound->set_settings(m_app_settings.audio);
    apply_language();
#ifdef O1602_EXPERIMENTAL_VITA
    normalize_vita_display(m_app_settings.display);
    platform::vita::stick_sensitivity(m_app_settings.display.vita_left_stick,m_app_settings.display.vita_right_stick);
    m_window->set_vsync(m_app_settings.display.vsync);
    if(!platform::vita::cpu_profile(unsigned(m_app_settings.display.vita_cpu_mhz))){
        m_events.notify(ui::tr("vita.display.cpu_failed"),"vita.cpu_failed",platform::ticks_ms());
    }
#endif
    m_ambient.set_config(ambient_config());
    if (auto bytes = core::read_file(save_folder() / "slots.cfg")) {
        m_slots = parse_slots(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    }
    find_last_game(m_slots, save_folder());
    read_slot_meta(m_slots, save_folder());
    ++m_slots_revision;
    if (!m_hud) {
        return;
    }
    m_hud->set_goods_trend(m_app_settings.display.goods_trend);
#ifdef O1602_EXPERIMENTAL_VITA
    m_hud->set_handheld_chat(m_app_settings.display.chat_button);
#endif
    OptionsModel& model = m_hud->options_model();
    model.audio = m_app_settings.audio;
    model.mission_window = m_app_settings.mission_window;
    model.tracks = m_sound ? m_sound->tracks() : std::vector<std::string>{};
    model.slot_names = m_slots.names;
    model.slot_humans = m_slots.humans;
    model.slot_details = slot_details(m_slots);
    model.reserved_slots = kLastGameSlot + 1;
    if (MainMenu* menu = m_hud->menu()) {
        menu->set_slots(m_slots);
        menu->set_score_folder(data_folder() / "scores");
        menu->set_progress_file(data_folder() / "progress.cfg");
        menu->set_name(m_app_settings.player_name);
        menu->set_endless_setup(m_app_settings.endless);
        menu->set_scenario_setup(m_app_settings.scenario);
        menu->set_tutorial_chapter(m_app_settings.tutorial_chapter);
    }
}

void MapViewer::apply_language() {
    std::string code = m_options.language;
    if (code.empty() && m_app_settings.language != "auto") {
        code = m_app_settings.language;
    }
    if (code.empty()) {
        const auto texts = assets::TextTable::load(m_game);
        code = ui::installation_language(m_game, texts ? &texts.value() : nullptr);
    }
    ui::set_language(code, data_folder() / "locale");
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::ui_language(ui::current_language().c_str());
#endif
    std::fprintf(stderr, "language: %s\n", ui::current_language().c_str());
}

void MapViewer::save_settings() {
#ifdef O1602_EXPERIMENTAL_VITA
    const auto read_text=[&](const char* name){
        auto bytes=core::read_file(data_folder()/name);
        return bytes?std::string(reinterpret_cast<const char*>(bytes->data()),bytes->size()):std::string{};
    };
    const std::string text=menu_preferences_text(m_app_settings,read_text("menu-preferences.cfg"),read_text("settings.cfg"));
    const std::filesystem::path path=data_folder()/"menu-preferences.cfg";
#else
    const std::string text = write_settings(m_app_settings);
    const std::filesystem::path path = data_folder() / "settings.cfg";
#endif
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    if (auto status = core::write_file_atomic(path, std::span<const std::uint8_t>(data, text.size())); !status) {
        std::fprintf(stderr, "settings: %s\n", status.error().message.c_str());
    }
}

render::Ambient::Config MapViewer::ambient_config() const {
    render::Ambient::Config config;
    config.sea_interval_ms = static_cast<std::uint32_t>(std::max(m_app_settings.sea_creatures_every, 0)) * 1000U;
    return config;
}

void MapViewer::apply_options() {
    if (!m_hud) {
        return;
    }
    OptionsModel& model = m_hud->options_model();
    if (m_sound) {
        model.playing = m_sound->playing();
    }
    model.fullscreen = m_app_settings.display.fullscreen;
    if (const auto display = m_hud->take_display_change()) {
        apply_display(*display);
    }
    for (const OptionsEvent& e : m_hud->take_option_events()) {
        switch (e.kind) {
            case OptionsEvent::Kind::Save: {
                const std::string name =
                    e.text.empty() ? ui::tr("save.default_name", {std::to_string(e.value + 1)}) : e.text;
                if (e.value == kLastGameSlot) {
                    break;  // kept by "Ende" alone
                }
                const auto status = save_slot(e.value, name);
                if (!status) {
                    m_events.notify(ui::tr("notice.save_failed", {ui::error_text(status.error(), "save")}),
                                    "notice.save_failed", platform::ticks_ms());
                    break;
                }
                // The voice confirms it (0x0043F71E: Speech_Queue(0x79, 2): files 121 or 123).
                m_sound->speak(121, 2);
                // As the original: after saving, the info mode (0x0043F751).
                if (const auto a = m_hud->set_mode(Hud::kModeInfo)) {
                    apply_hud(*a);
                }
                break;
            }
            case OptionsEvent::Kind::Load: {
                const std::filesystem::path path = slot_file(save_folder(), e.value);
                std::error_code ec;
                if (!std::filesystem::exists(path, ec)) {
                    std::fprintf(stderr, "load: slot %u is empty\n", unsigned(e.value + 1));
                    break;
                }
                if (const auto status = load_game(path); !status) {
                    m_events.notify(ui::tr("notice.load_failed", {ui::error_text(status.error(), "load")}),
                                    "notice.load_failed", platform::ticks_ms());
                } else {
                    std::fprintf(stderr, "game loaded: %s\n", core::path_to_utf8(path).c_str());
                }
                break;
            }
            case OptionsEvent::Kind::Quit:
                // The original leaves at once (M1 in the report); Open1602 asks first.
                if (m_options.confirm) {
                    m_quit_pending = true;
                    m_hud->ask(ui::tr("dialog.quit_game"));
                } else {
                    m_quit_pending = true;
                    Hud::Action yes{Hud::Action::Kind::Answer};
                    yes.step = 1;
                    apply_hud(yes);
                }
                break;
            case OptionsEvent::Kind::Fullscreen: {
                // The original's three resolutions became window and full screen (and the window
                // "Anzeige" for the rest).
                DisplaySettings display = m_app_settings.display;
                display.fullscreen = e.value != 0;
                apply_display(display);
                break;
            }
            case OptionsEvent::Kind::Display:
                m_hud->open_display(display_model());
                break;
            case OptionsEvent::Kind::Audio:
                m_app_settings.audio = model.audio;
                if (m_sound) {
                    m_sound->set_settings(model.audio);
                }
                // Upstream #501: hear dragging immediately, persist only on release.
                if(e.value!=0)break;
                if(m_sound){
                    model.tracks = m_sound->tracks();
                }
                save_settings();
                break;
            case OptionsEvent::Kind::PlayTrack:
                if (m_sound) {
                    m_sound->play_track(e.text);
                }
                break;
            case OptionsEvent::Kind::TestEffects:
                if (m_sound) {
                    m_sound->test_effects();
                }
                break;
            case OptionsEvent::Kind::MissionWindow:
                m_app_settings.mission_window = model.mission_window;
                save_settings();
                break;
        }
    }
}

core::Status MapViewer::load_game(const std::filesystem::path& path, const sim::Rules* rules) {
    if (m_session && m_session->ended()) {
        end_session();  // one that ended in its game stood only to show the game
    }
    if (m_session) {
        return core::make_error(core::ErrorCode::Unsupported, "no other game while a multiplayer game runs");
    }
    if (m_sound) {
        m_sound->prefill();  // the frames stand still while the game loads
    }
    std::size_t warnings = 0;
    auto world = read_world(path, &warnings);
    if (!world) {
        return world.error();
    }
    if (auto replaced = replace_world(std::move(world).value(), path, rules); !replaced) {
        return replaced;
    }
    report_load_problems(warnings);
    return core::Ok{};
}

void MapViewer::give_mod_content(sim::World& world) const {
    world.set_factions(m_factions);
    world.set_balance(m_balance);
    world.set_packages(assets::content_of(m_packages));
    world.set_own_buildings(m_own_buildings);
    world.set_goods(m_goods);
    world.set_resident_classes(m_classes);
    world.set_infra_levels(m_levels);
}

void MapViewer::use_game_packages() {
    std::vector<std::string> problems;
    const auto& wanted = m_state->world.packages();
    const std::vector<assets::ModPackage> known = known_packages();
    const std::vector<assets::ModPackage> used = wanted ? assets::packages_for(known, *wanted, problems) : m_packages;
    // The icons first: the messages and the build menu may name them.
    ui::set_package_icons(load_package_icons(used, problems));
    if (m_hud) {
        m_hud->forget_package_icons();
    }
    m_messages = MessageTable();
    m_messages.load_mods(used, problems);
    ui::set_build_menu(
        ui::load_build_menu_mods(ui::original_build_menu(), used, m_buildings, problems, m_own_buildings));
    try {
        m_state->use_scripts(script::read_package_scripts(used, problems));
    } catch (const std::bad_alloc&) {
        problems.emplace_back("not enough memory for the scripts");
    }
    for (const std::string& p : problems) {
        std::fprintf(stderr, "mods: %s\n", p.c_str());
    }
    if (wanted) {
        std::string names;
        for (const assets::ContentPackage& p : assets::missing_packages(*wanted, assets::content_of(known))) {
            names += (names.empty() ? "" : ", ") + assets::package_label(p);
        }
        if (!names.empty()) {
            m_events.notify(ui::tr("notice.packages", {names}), "notice.packages " + names, platform::ticks_ms());
        }
    }
}

ModsModel MapViewer::mods_model() const {
    using State = ModsModel::Entry::State;
    const std::filesystem::path mods = data_folder() / "mods";
    const std::vector<std::string>& off = m_app_settings.disabled_mods;
    ModsModel model;
    for (assets::ModPackage& p : assets::mod_packages(mods)) {
        std::vector<std::string> ignored;  // load_mod_packages wrote them into the log
        if (const auto bytes = core::read_file(p.folder / assets::kManifestFile)) {
            (void)assets::read_manifest(
                p, std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), ignored);
        }
        ModsModel::Entry e;
        e.id = p.id;
        e.folder = p.folder;
        e.name = core::utf8_to_cp1252(p.name.empty() ? p.id : p.name);
        e.version = core::utf8_to_cp1252(p.version);
        e.description = core::utf8_to_cp1252(p.description);
        const bool loaded = std::any_of(m_packages.begin(), m_packages.end(),
                                        [&](const assets::ModPackage& m) { return m.id == p.id; });
        e.state = std::find(off.begin(), off.end(), p.id) != off.end() ? State::Off
                  : loaded                                             ? State::On
                                                                       : State::Broken;
        model.entries.push_back(std::move(e));
    }
    for (const assets::ModPackage& p : m_cached_packages) {
        if (std::any_of(m_packages.begin(), m_packages.end(),
                        [&](const assets::ModPackage& m) { return m.content() == p.content(); })) {
            continue;  // the same files are among this computer's own
        }
        ModsModel::Entry e;
        e.id = p.id;
        e.folder = p.folder;
        e.name = core::utf8_to_cp1252(p.name.empty() ? p.id : p.name);
        e.version = core::utf8_to_cp1252(p.version);
        e.description = core::utf8_to_cp1252(p.description);
        e.state = State::Received;
        std::error_code ec;
        e.adoptable = !std::filesystem::exists(mods / core::path_from_utf8(p.id), ec);
        model.entries.push_back(std::move(e));
    }
    return model;
}

void MapViewer::reload_mods() {
    std::vector<std::string> problems;
    m_packages = assets::load_mod_packages(data_folder() / "mods", problems, m_app_settings.disabled_mods);
    for (const std::string& p : problems) {
        std::fprintf(stderr, "mods: %s\n", p.c_str());
    }
    // The packages' own buildings go into tables without any (the game's and the next game's), so
    // that turning a package on or off gives the same Ids again (docs/modding/own-buildings.md).
    if (auto base = assets::BuildingTable::load(m_game)) {
        if (m_app_settings.data_fixes && !m_options.original_data) {
            assets::fixes::fix_buildings(*base);
        }
        m_original_buildings = std::move(base).value();
    }
    ModRules rules = load_mod_rules(m_packages, m_original_buildings, m_original_figures);
    m_factions = std::move(rules.factions);
    m_balance = rules.balance;
    m_own_buildings = std::move(rules.own);
    m_goods = std::move(rules.goods);
    m_classes = std::move(rules.classes);
    m_levels = std::move(rules.levels);
    m_sound->set_package_music(std::move(rules.music));
    if (rules.looks != m_figure_looks) {
        m_figure_looks = std::move(rules.looks);
        assets::apply_figure_looks(m_figures, m_original_figures, m_figure_looks);
    }
    if (rules.images != m_package_images) {
        // Other pictures: the zoom levels are made again with them.
        m_package_images = std::move(rules.images);
        start_preloading();
        reload_graphics();
    }
    // Packages are turned on and off in the main menu, where no game is running: the game's table
    // takes the buildings the packages give now, with the values of the world behind the menu.
    m_buildings = m_original_buildings;
    sim::apply_building_values(m_buildings, m_original_buildings, m_state->world.balance());
    sim::apply_resident_classes(m_buildings, m_original_buildings, m_state->world.resident_classes());
    sim::apply_infra_levels(m_buildings, m_original_buildings, m_state->world.infra_levels());
}

std::string MapViewer::adopt_mod(const std::filesystem::path& folder) {
    const auto cached = std::find_if(m_cached_packages.begin(), m_cached_packages.end(),
                                     [&](const assets::ModPackage& p) { return p.folder == folder; });
    if (cached == m_cached_packages.end()) {
        return {};
    }
    const std::string name = core::utf8_to_cp1252(cached->name.empty() ? cached->id : cached->name);
    const auto adopted = assets::adopt_package(data_folder() / "mods", *cached);
    if (!adopted) {
        std::fprintf(stderr, "mods: %s\n", adopted.error().message.c_str());
        return ui::tr("mods.adopt_failed", {name, ui::error_text(adopted.error(), "mods")});
    }
    std::fprintf(stderr, "mods: %s added to the mods folder\n", cached->id.c_str());
    std::erase(m_app_settings.disabled_mods, cached->id);
    save_settings();
    reload_mods();
    return ui::tr("mods.adopted", {name});
}

std::vector<assets::ModPackage> MapViewer::known_packages() const {
    std::vector<assets::ModPackage> known = m_packages;
    known.insert(known.end(), m_cached_packages.begin(), m_cached_packages.end());
    return known;
}

void MapViewer::report_load_problems(std::size_t warnings) {
    m_load_problems = warnings + (m_state->simulation.state_damaged() ? 1 : 0);
    if (m_load_problems != 0) {
        const std::string count = std::to_string(m_load_problems);
        m_events.notify(ui::tr("notice.load_problems", {count}), "notice.load_problems " + count, platform::ticks_ms());
    }
}

core::Result<sim::World> MapViewer::read_world(const std::filesystem::path& path, std::size_t* warning_count) const {
    auto bytes = core::read_file(path);
    if (!bytes) {
        return bytes.error();
    }
    std::vector<std::string> warnings;
    core::Result<sim::World> world = core::make_error(core::ErrorCode::NotFound, "no game");
    if (sim::is_open1602_save(*bytes)) {
        world = sim::read_game(*bytes, m_buildings, &warnings, &m_figures);
    } else {
        auto file = assets::ChunkFile::from_bytes(std::move(bytes).value());
        if (!file) {
            return file.error();
        }
        world = sim::load_world(*file, m_buildings, m_game, &warnings, m_options.world);
        if (world) {
            give_mod_content(*world);  // the original's files have none of their own
        }
    }
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    if (warning_count != nullptr) {
        *warning_count = warnings.size();
    }
    return world;
}

std::unique_ptr<GameState> MapViewer::make_state(sim::World world, const sim::Rules& rules) {
    if (!world.saved_rules()) {
        // A new game, or one from a file of the original: the rule's values under the mods'.
        sim::Balance balance = world.balance();
        sim::add_building_values(balance, sim::rule_building_values(rules));
        world.set_balance(balance);
    }
    sim::apply_building_values(m_buildings, m_original_buildings, world.balance());
    sim::apply_resident_classes(m_buildings, m_original_buildings, world.resident_classes());
    sim::apply_infra_levels(m_buildings, m_original_buildings, world.infra_levels());
    sim::apply_figure_values(m_figures, m_original_figures, world.balance());
    return std::make_unique<GameState>(std::move(world), m_buildings, m_figures, rules);
}

core::Status MapViewer::replace_world(sim::World world, const std::filesystem::path& source, const sim::Rules* rules) {
    // The new game is built whole before anything of the old one goes: if memory runs out on the
    // way, the old game goes on.
    std::unique_ptr<GameState> next;
    // The old game's building values come back if the new one does not start.
    const auto keep_old = [&] {
        if (m_state) {
            sim::apply_building_values(m_buildings, m_original_buildings, m_state->world.balance());
            sim::apply_resident_classes(m_buildings, m_original_buildings, m_state->world.resident_classes());
            sim::apply_infra_levels(m_buildings, m_original_buildings, m_state->world.infra_levels());
            sim::apply_figure_values(m_figures, m_original_figures, m_state->world.balance());
        }
    };
    try {
        next = make_state(std::move(world), rules != nullptr ? *rules : m_options.rules);
    } catch (const std::bad_alloc&) {
        keep_old();
        return core::make_error(core::ErrorCode::PlatformError, "not enough memory for the game");
    } catch (const std::exception& e) {
        keep_old();
        return core::make_error(core::ErrorCode::PlatformError, std::string("the game could not start: ") + e.what());
    }
    // What belongs to the old game goes: videos, selection, orders waiting for an answer, tools.
    m_video->reset();
    m_events.reset_game();
    m_quit_pending = false;
    m_input->reset_game();
    m_ship_trail = render::ShipTrail{};
    m_tick_fraction = 1.0;
    m_tool.set_mode(BuildTool::Mode::Off);
    // Another game: nothing changed hands. The host's new game would otherwise be compared with
    // the one shown behind the lobby, whose people the computer plays in the new game.
    m_session_roles.clear();
    m_state = std::move(next);
    m_circle_cache = {};  // a replacement world must not reuse the previous world's circles
    m_campaign_part = source.empty() ? std::nullopt : campaign_part_of(source);
    use_game_packages();
    m_command_sink.set_simulation(&m_state->simulation);
    m_options.scenario = source;
    m_title_real_ms = 0;  // loading took real time the game did not run: no "too slow" for it
    warn_if_state_damaged();
    m_state->simulation.offer_names(m_city_names, m_ship_names);  // a saved game keeps its own
    m_state->simulation.set_tick_observer(
        [this](const sim::World& now) { m_ambient.tick(now, m_buildings, m_figures); });
    // In a multiplayer game the slot the host gave this computer (kNoPlayer: watching).
    m_local_player = m_session ? m_session->local_seat() : sim::first_human_player(m_state->world);
    if (m_local_player < m_state->world.unit_groups().size()) {
        m_input->restore_groups(m_state->world.unit_groups()[m_local_player]);
    }
    m_building_figures = render::collect_building_figures(m_state->world, m_buildings, m_figures);
    m_production_phases.clear();
    m_stock_notes.clear();
    m_soldiers.reset(m_state->world, m_buildings, m_figures);
    m_walkers = render::WalkerFigures{};
    m_carts = render::CartFigures{};
    m_ambient = render::Ambient{1602, ambient_config()};
    m_ambient.tick(m_state->world, m_buildings, m_figures);
    m_combat.reset();
    m_effects.clear();
    m_player_figures = sim::PlayerFigures{};
    m_figures_ms = 0;
    if (m_hud) {
        m_hud->set_game(m_state->simulation, m_buildings, m_figures, m_local_player);
        m_hud->reset_game();
#ifdef O1602_EXPERIMENTAL_VITA
        // Loading from the disk/options sidebar must leave that old input context.
        if(const auto action=m_hud->set_mode(Hud::kModeInfo))apply_hud(*action);
#endif
        // The tutorial (docs/re/tutorial.md): the chapter of the settings, its window open
        // (0x00484EAB), its speech once at the start (0x0043B500). Not saved: a loaded tutorial
        // game starts its chapter again.
        if (m_state->world.game_settings().tutorial && !m_session) {
            const std::uint8_t chapter = m_app_settings.tutorial_chapter;
            m_hud->set_tutorial(chapter);
            m_hud->options_model().mission_window = true;
            if (m_sound) {
                m_sound->speak(Tutorial::speech(chapter), 1);
            }
        }
    }
    m_camera.set_world_size(m_state->world.width(), m_state->world.height());
    m_camera.center_on(home_tile());
#ifdef O1602_EXPERIMENTAL_VITA
    m_world_requested_at=platform::ticks_ms();m_world_presented=false;
    platform::vita::trace("WORLD_REPLACED source=%s tick=%llu hash=%llu",source.string().c_str(),
        (unsigned long long)m_state->simulation.next_tick(),(unsigned long long)m_state->simulation.state_hash());
#endif
    return core::Ok{};
}

core::Status MapViewer::start_endless(std::uint8_t level, sim::BannerId banner, const std::string& name,
                                      const EndlessSetup& setup) {
    if (m_session && m_session->ended()) {
        end_session();  // one that ended in its game stood only to show the game
    }
    if (m_session) {
        return core::make_error(core::ErrorCode::Unsupported, "no other game while a multiplayer game runs");
    }
    auto texts = assets::TextTable::load(m_game);
    if (!texts) {
        return texts.error();
    }
    // What the original rolls with rand() (map layout, start of the island table) and the game id
    // from the clock (0x0048F5A0, 0x00469410).
    std::random_device device;
    sim::NewGameOptions o;
    o.difficulty = std::min<std::uint8_t>(level, sim::NewGameOptions::kDifficulties - 1);
    // The level's file knows the original's four colours: a further one comes after the start.
    const bool further = banner >= sim::kOriginalPlayers;
    o.colour = further ? 0 : static_cast<std::uint8_t>(banner);
    o.name = name;
    o.world = m_options.world;
    o.world.seed = (std::uint64_t{device()} << 32U) | device();
    o.layout = sim::layout_for_seed(o.world.seed);
    o.world.guarantee_resources = setup.rules.guarantee_resources;
    if (setup.own_world) {
        sim::WorldSettings custom = setup.world_settings();
        custom.players = static_cast<std::uint8_t>(std::min<std::size_t>(custom.players, sim::kMaxPlayers));
        o.custom = custom;
    }
    const std::time_t now = std::time(nullptr);
    if (const std::tm* t = std::localtime(&now)) {
        o.game_id =
            sim::new_game_id(static_cast<std::uint32_t>(t->tm_year + 1900), static_cast<std::uint32_t>(t->tm_mon + 1),
                             static_cast<std::uint32_t>(t->tm_mday), static_cast<std::uint32_t>(t->tm_hour),
                             static_cast<std::uint32_t>(t->tm_min), device());
    }
    std::vector<std::string> warnings;
    auto world = sim::start_new_game(m_game, m_buildings, *texts, o, &warnings);
    if (!world) {
        return world.error();
    }
    give_mod_content(*world);
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    if (further) {
        // A computer with that colour takes the human's, and the name of its new colour.
        if (const auto other = sim::take_banner(*world, sim::first_human_player(*world), banner)) {
            if (sim::Player* p = world->player(*other); p != nullptr && p->role == sim::PlayerRole::Computer) {
                p->name = colour_player_name(p->banner, texts->section("PLAYCOL"));
            }
        }
    }
    name_more_players(*world);
    return replace_world(std::move(world).value(), {}, &setup.rules);
}

void MapViewer::apply_menu() {
    if (m_hud && m_hud->end_screen_active()) {
        if (const auto choice = m_hud->take_end_choice()) {
            m_hud->close_end_screen(*choice);
            // To the menus: out of a multiplayer game too, as "Ende" in the settings does.
            if (*choice != EndScreen::Choice::Resume && m_session) {
                end_session();
            }
            // "Neues Spiel": the mission screen of the same scenario alone, the main menu with
            // several players.
            if (*choice == EndScreen::Choice::NewGame && humans() == 1 && m_hud->menu() != nullptr) {
                m_hud->menu()->open_mission(sim::game_identity(m_state->world).name);
            }
        }
        return;
    }
    MainMenu* menu = m_hud ? m_hud->menu() : nullptr;
    if (menu == nullptr) {
        return;
    }
    if (m_hud->multiplayer_open()) {
        apply_multiplayer_menu();
        return;
    }
    if (const auto display = m_hud->take_display_change()) {
        apply_display(*display);  // the window "Anzeige" from the History Edition's "Options"
    }
    // Only when they changed: a copy of thousands of names each frame costs (FB-15).
    if (menu != m_slots_menu || m_slots_shown != m_slots_revision) {
        menu->set_slots(m_slots);
        m_slots_menu = menu;
        m_slots_shown = m_slots_revision;
    }
    for (const MenuEvent& e : m_hud->take_menu_events()) {
#ifdef O1602_EXPERIMENTAL_VITA
        platform::vita::trace("MENU_EVENT kind=%u",unsigned(e.kind));
        platform::vita::touch_action("menu",unsigned(e.kind));
#endif
        core::Status started = core::make_error(core::ErrorCode::NotFound, "nothing started");
        switch (e.kind) {
            case MenuEvent::Kind::Quit:
                // The original quits at once (U-036).
                m_quit_pending = true;
                if (m_options.confirm) {
                    m_hud->ask(ui::tr("dialog.quit_anno"));
                } else {
                    Hud::Action yes{Hud::Action::Kind::Answer};
                    yes.step = 1;
                    apply_hud(yes);
                }
                continue;
            case MenuEvent::Kind::Credits:
                m_video->request(10, 0, false, false, true);
                continue;
            case MenuEvent::Kind::Intro:
                m_video->request(58, 0, false, true);
                continue;
            case MenuEvent::Kind::Multiplayer:
                open_multiplayer_menu();
                return;  // the rest belongs to the main menu, which is gone
            case MenuEvent::Kind::NewEndless: {
                const EndlessSetup setup = e.setup.value_or(m_app_settings.endless);
                started = start_endless(e.level, e.banner, e.name, setup);
                if (started && !(setup == m_app_settings.endless)) {
                    m_app_settings.endless = setup;
                    save_settings();
                }
                break;
            }
            case MenuEvent::Kind::Scenario:
                if (e.tutorial_chapter) {
                    // The chapter is a setting, as the original's "Tutornr": the game reads it.
                    m_app_settings.tutorial_chapter = *e.tutorial_chapter;
                    save_settings();
                    menu->set_tutorial_chapter(*e.tutorial_chapter);
                }
                started = load_game(e.path, e.setup ? &e.setup->rules : nullptr);
                if (started && e.setup && !(e.setup->rules == m_app_settings.scenario.rules)) {
                    m_app_settings.scenario = *e.setup;
                    save_settings();
                }
                if (started && !e.name.empty()) {
                    // The stored name goes to the human (0x0048A680).
                    m_command_sink.submit(sim::PlayerRename{m_local_player, e.name});
                }
                break;
            case MenuEvent::Kind::LoadSlot:
                started = load_game(slot_file(save_folder(), e.slot));
                break;
            case MenuEvent::Kind::ContinueLast:
                started = load_game(slot_file(save_folder(), kLastGameSlot));
                break;
            case MenuEvent::Kind::Display:
                m_hud->open_display(display_model());
                continue;
            case MenuEvent::Kind::Mods:
                menu->set_mods(mods_model());
                continue;
            case MenuEvent::Kind::ToggleMod: {
                std::vector<std::string>& off = m_app_settings.disabled_mods;
                if (std::erase(off, e.name) == 0) {
                    off.push_back(e.name);
                }
                save_settings();
                reload_mods();
                menu->set_mods(mods_model());
                continue;
            }
            case MenuEvent::Kind::AdoptMod: {
                std::string notice = adopt_mod(e.path);
                ModsModel model = mods_model();
                model.notice = std::move(notice);
                menu->set_mods(std::move(model));
                continue;
            }
            case MenuEvent::Kind::NameEntered:
                // "Unter welcher Flagge wollt Ihr segeln?" and four more, one at random (Speech8
                // 107..115). The original speaks on entering the screen; Open1602 once the name is in.
                m_sound->speak(107, 5);
                continue;
        }
        if (!started) {
            // In the start dialog, where "Start" was clicked; error_text() also writes the log.
            const std::string why = ui::error_text(started.error(), "menu");
            menu->show_start_error(ui::tr("menu.start_failed", {why}));
            continue;
        }
        if (!e.name.empty() && e.name != m_app_settings.player_name) {
            m_app_settings.player_name = e.name;
            save_settings();
        }
        m_hud->close_menu();
        if (const auto a = m_hud->set_mode(Hud::kModeInfo)) {
            apply_hud(*a);
        }
        break;  // the rest belongs to the menu that is gone
    }
}

void MapViewer::load_cursors() {
    const auto dll = m_game.resolve("Language.dll");
    if (!dll) {
        std::fprintf(stderr, "cursors: Language.dll is missing\n");
        return;
    }
    const auto bytes = core::read_file(*dll);
    if (!bytes) {
        std::fprintf(stderr, "cursors: %s\n", bytes.error().message.c_str());
        return;
    }
    using Shape = platform::MouseCursors::Shape;
    for (const auto& [shape, id] :
         {std::pair{Shape::Arrow, assets::kArrowCursor}, std::pair{Shape::Target, assets::kTargetCursor}}) {
        const auto image = assets::read_cursor(*bytes, id);
        if (!image) {
            std::fprintf(stderr, "cursors: %s\n", image.error().message.c_str());
        } else if (!m_cursors.set_image(shape, image->width, image->height, image->hot_x, image->hot_y, image->rgba)) {
            std::fprintf(stderr, "cursors: %s\n", SDL_GetError());
        }
    }
}

void MapViewer::update_cursor(bool video_full_screen) {
    using Shape = platform::MouseCursors::Shape;
    // Cursor_Apply (0x00489460): the arrow, the target in the modes where a window waits for a
    // click on the map, none while a video plays.
    Shape shape = Shape::Arrow;
    if (video_full_screen) {
        shape = Shape::Hidden;
    } else if (m_hud && !m_hud->menu_active() && (m_hud->picking() || m_input->pick() != MapInput::Pick::None)) {
        shape = Shape::Target;
    }
    m_cursors.show(shape);
}

void MapViewer::present(bool show_cursor) {
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::WatchPhase watch("present");
#endif
    if (auto request = std::exchange(m_screenshot, std::nullopt)) {
        const auto status = save_screenshot(request->path);
        m_screenshot_error.reset();
        if (!status) {
            m_screenshot_error = status.error().message;
        }
        if (request->notify) {
            const std::string file = core::path_to_utf8(request->path.filename());
            if (status) {
                m_events.notify(ui::tr("notice.screenshot_saved", {utf8_to_cp1252(file)}),
                                "notice.screenshot_saved " + file, platform::ticks_ms());
            } else {
                m_events.notify(ui::tr("notice.screenshot_failed", {ui::error_text(status.error(), "screenshot")}),
                                "notice.screenshot_failed", platform::ticks_ms());
            }
        }
    }
    if(m_video){int w=0,h=0;m_window->output_size(w,h);m_video->draw_notice(w,h);}
    m_window->present(show_cursor);
    if(m_video)m_video->presented();
    if (m_hud) {
        m_hud->next_frame();
    }
}

void MapViewer::quick_save() {
    const std::filesystem::path path = save_folder() / ("quicksave" + std::string(sim::kSaveExtension));
    const auto status = save_game(path);
    const std::string file = core::path_to_utf8(path.filename());
    if (status) {
        m_events.notify(ui::tr("notice.game_saved", {utf8_to_cp1252(file)}), "notice.game_saved " + file,
                        platform::ticks_ms());
    } else {
        m_events.notify(ui::tr("notice.save_failed", {ui::error_text(status.error(), "save")}), "notice.save_failed",
                        platform::ticks_ms());
    }
}

void MapViewer::quick_load() {
    const std::filesystem::path path = save_folder() / ("quicksave" + std::string(sim::kSaveExtension));
    const auto status = load_game(path);
    if (!status) {
        m_events.notify(ui::tr("notice.load_failed", {ui::error_text(status.error(), "load")}), "notice.load_failed",
                        platform::ticks_ms());
    } else if (m_load_problems == 0) {  // else the note says what went wrong while loading
        const std::string file = core::path_to_utf8(path.filename());
        m_events.notify(ui::tr("notice.game_loaded", {utf8_to_cp1252(file)}), "notice.game_loaded " + file,
                        platform::ticks_ms());
    }
}

void MapViewer::request_screenshot() {
    // Read in the next present(), when the frame is complete.
    m_screenshot = ScreenshotRequest{next_screenshot_path(), true};
}

std::filesystem::path MapViewer::next_screenshot_path() {
    const std::filesystem::path folder = data_folder() / "screenshots";
    const std::time_t now = std::time(nullptr);
    char stamp[32] = "open1602";
    if (const std::tm* local = std::localtime(&now)) {
        std::strftime(stamp, sizeof stamp, "open1602-%Y%m%d-%H%M%S", local);
    }
    std::filesystem::path path = folder / (std::string(stamp) + ".png");
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(path, ec) && n < 1000; ++n) {
        path = folder / (std::string(stamp) + "-" + std::to_string(n) + ".png");
    }
    return path;
}

core::Status MapViewer::save_screenshot(const std::filesystem::path& path) {
    SDL_Surface* surface = SDL_RenderReadPixels(m_window->sdl_renderer(), nullptr);
    if (surface == nullptr) {
        return platform::sdl_error("SDL_RenderReadPixels");
    }
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (rgba == nullptr) {
        return platform::sdl_error("SDL_ConvertSurface");
    }
    const auto width = static_cast<std::uint32_t>(rgba->w);
    const auto height = static_cast<std::uint32_t>(rgba->h);
    std::vector<std::uint8_t> pixels(std::size_t{width} * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto* row =
            static_cast<const std::uint8_t*>(rgba->pixels) + std::size_t{y} * static_cast<std::size_t>(rgba->pitch);
        std::copy_n(row, std::size_t{width} * 4,
                    pixels.begin() + static_cast<std::ptrdiff_t>(std::size_t{y} * width * 4));
    }
    SDL_DestroySurface(rgba);
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        pixels[i] = 255;
    }
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    return core::write_png_rgba(path, width, height, pixels);
}

sim::GameOutcome MapViewer::local_outcome() const {
    const sim::Player* p = m_state->world.player(m_local_player);
    if (p != nullptr && p->won) {
        return sim::GameOutcome::Won;
    }
    if ((p != nullptr && p->status == sim::PlayerStatus::Defeated) || m_state->world.game_end()) {
        return sim::GameOutcome::Lost;
    }
    return sim::GameOutcome::Left;
}

std::size_t MapViewer::humans() const {
    std::size_t n = 0;
    for (const sim::Player& p : m_state->world.players()) {
        n += p.role == sim::PlayerRole::Human && p.status != sim::PlayerStatus::Absent ? 1U : 0U;
    }
    return n;
}

void MapViewer::check_end_screen(std::uint64_t now) {
    if (!m_hud) {
        return;
    }
    const auto outcome = m_events.end_screen_due(now, m_video->busy(), m_state->simulation.paused());
    // Never in the tutorial (mission 0): it goes on.
    if (!outcome || sim::game_identity(m_state->world).mission_number == 0) {
        return;
    }
    show_end_screen(*outcome, false);
}

void MapViewer::show_end_screen(sim::GameOutcome outcome, bool view_only) {
    if (!m_hud) {
        return;
    }
    if (!view_only) {
        m_events.end_screen_shown();
    }
    m_hud->open_end_screen(sim::end_screen_summary(m_state->world, m_buildings, m_local_player, outcome), view_only);
}

sim::GameIdentity MapViewer::game_identity() const {
    sim::GameIdentity game = sim::game_identity(m_state->world);
    // The campaign and part as the menu has them, not the file's header (campaign_part_of).
    if (m_campaign_part && game.campaign != sim::GameIdentity::kNone) {
        game.campaign = m_campaign_part->campaign;
        game.ranking = m_campaign_part->part;
    }
    return game;
}

void MapViewer::update_high_scores(sim::PlayerId defeated) {
    const sim::GameIdentity game = game_identity();
    const std::filesystem::path name = high_score_file(game, m_campaign_labels, humans() > 1);
    if (name.empty()) {
        return;
    }
    // Open1602's own list in the data folder; the first line starts from the original's (only read).
    const std::filesystem::path own = data_folder() / "scores" / name;
    auto bytes = core::read_file(own);
    if (!bytes) {
        const std::u8string relative = (std::filesystem::path(u8"Szenes") / name).generic_u8string();
        if (const auto original = m_game.resolve(std::string(relative.begin(), relative.end()))) {
            bytes = core::read_file(*original);
        }
    }
    std::vector<assets::HighScore> scores;
    if (bytes) {
        scores =
            assets::parse_high_scores(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    }
    bool changed = false;
    for (std::size_t i = 0; i < m_state->world.players().size(); ++i) {
        const sim::Player& p = m_state->world.players()[i];
        const auto id = static_cast<sim::PlayerId>(i);
        if (p.role != sim::PlayerRole::Human || (p.status != sim::PlayerStatus::Playing && id != defeated)) {
            continue;  // computers never enter the list
        }
        assets::HighScore entry;
        entry.name = p.name;
        entry.points = sim::player_score(m_state->world, m_buildings, id);
        entry.stars = game.ranking == sim::GameIdentity::kNone ? 0 : game.ranking;
        entry.game_id = game.game_id;
        changed = assets::insert_high_score(scores, std::move(entry)).has_value() || changed;
    }
    if (!changed) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(own.parent_path(), ec);
    const std::string text = assets::write_high_scores(scores);
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    if (auto status = core::write_file_atomic(own, std::span<const std::uint8_t>(data, text.size())); !status) {
        std::fprintf(stderr, "high scores: %s\n", status.error().message.c_str());
    }
}

void MapViewer::record_progress() {
    const sim::Player* local = m_state->world.player(m_local_player);
    if (local == nullptr || !local->won || humans() != 1) {
        return;
    }
    // Open1602 keeps it in its own file instead of Game.dat.
    const std::filesystem::path file = data_folder() / "progress.cfg";
    assets::CampaignProgress progress;
    if (auto bytes = core::read_file(file)) {
        progress =
            assets::parse_progress(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    }
    const sim::GameIdentity game = game_identity();
    if (progress.ends_campaigns(game.campaign, game.ranking, game.mission_number)) {
        m_video->follow(assets::kCampaignsEndVideo);  // after the success video, whatever the switches
    }
    if (progress.record_win(game.campaign, game.ranking, game.mission_number)) {
        const std::string text = assets::write_progress(progress);
        const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
        if (auto status = core::write_file_atomic(file, std::span<const std::uint8_t>(data, text.size())); !status) {
            std::fprintf(stderr, "progress: %s\n", status.error().message.c_str());
        }
    }
}

}  // namespace o1602::app
