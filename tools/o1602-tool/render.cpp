// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool: rendering an island or the world map to a PNG.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mod_rules.hpp"
#include "tool.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/data_fixes.hpp"
#include "o1602/assets/figure_looks.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/core/png_writer.hpp"
#include "o1602/render/animation_clock.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/render/land_figures.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/walker_figures.hpp"
#include "o1602/render/world_graphics.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::tool {

int cmd_render_island(const Args& args) {
    if (args.positional.size() != 2) {
        return usage();
    }
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        return fail(game.error());
    }

    auto buildings = assets::BuildingTable::load(*game);
    if (!buildings) {
        return fail(buildings.error());
    }

    // Island
    auto file = assets::ChunkFile::load(fs::path(args.positional[0]));
    if (!file) {
        return fail(file.error());
    }
    auto islands = assets::load_islands(*file);
    if (!islands) {
        return fail(islands.error());
    }
    const std::size_t island_index = args.number("--island").value_or(0);
    if (island_index >= islands->size()) {
        return fail(core::make_error(core::ErrorCode::OutOfRange,
                                     "file has " + std::to_string(islands->size()) + " island(s)"));
    }
    // Base terrain: islands in scenarios/savegames only store deviations from a template .SCP.
    const assets::Island& island = (*islands)[island_index];
    std::vector<assets::Island> base;
    std::string base_path;
    if (const auto it = args.options.find("--base"); it != args.options.end()) {
        base_path = std::string(it->second);
    } else if (!args.flag("--no-template") && !island.header.template_path().empty()) {
        const auto resolved = game->resolve(island.header.template_path());
        if (!resolved) {
            return fail(core::make_error(core::ErrorCode::NotFound,
                                         "island template not found: " + island.header.template_path()));
        }
        base_path = resolved->string();
    }
    if (!base_path.empty()) {
        auto base_file = assets::ChunkFile::load(fs::path(base_path));
        if (!base_file) {
            return fail(base_file.error());
        }
        auto loaded = assets::load_islands(*base_file);
        if (!loaded || loaded->empty()) {
            return fail(core::make_error(core::ErrorCode::InvalidFormat, base_path + " has no island"));
        }
        base = std::move(loaded).value();
        std::printf("template: %s\n", base_path.c_str());
    }
    std::vector<std::string> warnings;
    const auto map = sim::IslandMap::from_island(island, *buildings, &warnings, base.empty() ? nullptr : &base[0]);

    // Graphics
    render::RenderOptions options;
    const std::size_t zoom = std::min<std::size_t>(args.number("--zoom").value_or(0), 2);
    options.zoom = static_cast<render::Zoom>(zoom);
    options.view_rotation = static_cast<std::uint8_t>(args.number("--rotation").value_or(0) & 3U);
    options.anim_step = static_cast<std::uint32_t>(args.number("--anim").value_or(0));
    auto sheet = assets::SpriteSheet::load(*game->resolve(render::terrain_sheet_path(options.zoom)));
    if (!sheet) {
        return fail(sheet.error());
    }
    auto palette = assets::Palette::load(*game->resolve(render::kTerrainPalettePath));
    if (!palette) {
        return fail(palette.error());
    }

    const auto commands = render::build_draw_list(map, *buildings, options);
    std::size_t missing = 0;
    const auto image = render::rasterize(commands, *sheet, *palette, assets::Rgba{24, 40, 72, 255}, &missing);
    if (image.width == 0) {
        return fail(core::make_error(core::ErrorCode::InvalidFormat, "nothing to draw"));
    }
    if (auto status = core::write_png_rgba(fs::path(args.positional[1]), image.width, image.height, image.pixels);
        !status) {
        return fail(status.error());
    }
    std::printf("island %zu: %ux%u tiles, %zu sprites drawn, %zu missing -> %s (%ux%u)\n", island_index, map.width(),
                map.height(), commands.size() - missing, missing, std::string(args.positional[1]).c_str(), image.width,
                image.height);
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    return 0;
}

int cmd_render_world(const Args& args) {
    if (args.positional.size() != 2) {
        return usage();
    }
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        return fail(game.error());
    }
    auto buildings = assets::BuildingTable::load(*game);
    if (!buildings) {
        return fail(buildings.error());
    }
    auto file = assets::ChunkFile::load(fs::path(args.positional[0]));
    if (!file) {
        return fail(file.error());
    }
    std::vector<std::string> warnings;
    auto world = sim::load_world(*file, *buildings, *game, &warnings);
    if (!world) {
        return fail(world.error());
    }
    render::WorldView view;
    view.zoom = static_cast<render::Zoom>(std::min<std::size_t>(args.number("--zoom").value_or(2), 2));
    view.world_width = world->width();
    view.world_height = world->height();
    view.rotation = static_cast<std::uint8_t>(args.number("--rotation").value_or(0) & 3U);

    const auto palette_path = game->resolve(render::kTerrainPalettePath);
    if (!palette_path) {
        return fail(core::make_error(core::ErrorCode::NotFound, "stadtfld.col not found"));
    }
    auto palette = assets::Palette::load(*palette_path);
    if (!palette) {
        return fail(palette.error());
    }
    auto figures = assets::FigureTable::load(*game);
    if (!figures) {
        return fail(figures.error());
    }
    if (!args.flag("--original-data")) {
        (void)assets::fixes::fix_buildings(*buildings);
        (void)assets::fixes::fix_figures(*figures);
    }
    // --mods FOLDER: the packages' pictures, of their own and in place of the original's
    // (graphics.toml, figure_images.toml, building_images.toml).
    render::GraphicsOptions graphics_options;
    if (const auto it = args.options.find("--mods"); it != args.options.end()) {
        std::vector<std::string> problems;
        const auto packages = assets::load_mod_packages(std::filesystem::path(std::string(it->second)), problems);
        const assets::FigureTable original_figures = *figures;
        app::ModRules rules = app::load_mod_rules(packages, *buildings, *figures, problems);
        assets::apply_figure_looks(*figures, original_figures, rules.looks);
        graphics_options.package_images = std::move(rules.images);
        graphics_options.sprite_redirects = std::move(rules.redirects);
        for (const std::string& p : problems) {
            std::fprintf(stderr, "mods: %s\n", p.c_str());
        }
    }
    // --build "ID@X,Y[,R];..." places buildings for the first human player (world tiles).
    if (const auto it = args.options.find("--build"); it != args.options.end()) {
        std::vector<std::string> names;
        if (auto texts = assets::TextTable::load(*game)) {
            names = texts->section("STAEDTE");
        }
        std::string spec(it->second);
        std::size_t pos = 0;
        while (pos < spec.size()) {
            const std::size_t end = std::min(spec.find(';', pos), spec.size());
            const std::string item = spec.substr(pos, end - pos);
            pos = end + 1;
            unsigned id = 0;
            unsigned x = 0;
            unsigned y = 0;
            unsigned r = 0;
            if (std::sscanf(item.c_str(), "%u@%u,%u,%u", &id, &x, &y, &r) < 3 || !world->contains(x, y)) {
                std::fprintf(stderr, "bad --build item '%s'\n", item.c_str());
                continue;
            }
            const auto& cell = world->cell(x, y);
            if (cell.island == sim::World::kSea) {
                std::fprintf(stderr, "--build %s: open sea\n", item.c_str());
                continue;
            }
            sim::BuildOrder order;
            order.player = sim::first_human_player(*world);
            order.island = cell.island;
            order.items = {{id, cell.local_x, cell.local_y, static_cast<std::uint8_t>(r & 3U), 0}};
            const sim::BuildPlan plan = sim::execute_build(*world, *buildings, order, names, nullptr);
            std::printf("build %s: %s\n", item.c_str(),
                        plan.placed == 1 ? "ok" : sim::refusal_name(plan.items[0].refusal));
        }
    }
    std::optional<sim::Simulation> simulation;
    // --reveal: the game setting of the towns known from the start, which the simulation applies.
    sim::Rules rules = sim::Rules::open1602();
    if (const auto it = args.options.find("--reveal"); it != args.options.end()) {
        if (it->second == "none") {
            rules.reveal = sim::Reveal::None;
        } else if (it->second == "all") {
            rules.reveal = sim::Reveal::All;
        } else if (it->second != "original") {
            return usage();
        }
        if (!args.number("--simulate")) {
            simulation.emplace(*world, *buildings, *figures, rules);
        }
    }
    if (const auto seconds = args.number("--simulate")) {
        simulation.emplace(*world, *buildings, *figures, rules);
        std::size_t produced = 0;
        std::size_t stopped = 0;
        for (std::uint64_t s = 0; s < static_cast<std::uint64_t>(*seconds); ++s) {
            simulation->advance(1000);
            for (const auto& e : simulation->take_production_events()) {
                produced += e.kind == sim::ProductionEvent::Kind::Produced ? 1U : 0U;
                stopped += e.kind == sim::ProductionEvent::Kind::Stopped ? 1U : 0U;
            }
        }
        std::size_t working = 0;
        for (const auto& p : world->productions()) {
            working += p.enabled && p.working() ? 1U : 0U;
        }
        const sim::LogisticsStats& ls = simulation->logistics().stats();
        std::printf("simulated %lld s: %zu tons produced, %zu buildings stopped, %zu working\n",
                    static_cast<long long>(*seconds), produced, stopped, working);
        std::printf(
            "logistics: %llu carrier trips (%.2f t raw material delivered), %llu cart trips (%.2f t "
            "collected), %zu figures on their way\n",
            static_cast<unsigned long long>(ls.carrier_trips),
            static_cast<double>(ls.raw_delivered) / assets::kStockUnitsPerTon,
            static_cast<unsigned long long>(ls.cart_trips),
            static_cast<double>(ls.goods_collected) / assets::kStockUnitsPerTon,
            simulation->logistics().walkers().size());
        std::printf("deer: %llu appeared, %llu hunted\n", static_cast<unsigned long long>(ls.deer_spawned),
                    static_cast<unsigned long long>(ls.deer_hunted));
        std::printf("raw producers: %llu worker trips, %.2f t harvested, %.2f t ore mined, %zu growing tiles\n",
                    static_cast<unsigned long long>(ls.worker_trips),
                    static_cast<double>(ls.harvested) / assets::kStockUnitsPerTon,
                    static_cast<double>(ls.ore_mined) / assets::kStockUnitsPerTon, world->growth().entries.size());
        if (args.flag("--walkers")) {
            simulation->logistics().walkers().for_each([&](sim::Handle, const sim::Walker& w) {
                const sim::WorldIsland& island = world->islands()[w.island];
                const sim::WalkerPosition pos = sim::walker_position(w);
                static constexpr const char* kKinds[] = {"carrier", "cart", "harvester", "grazer",
                                                         "fisher",  "deer", "hunter"};
                std::printf("  %-9s figure %u at %.2f,%.2f (island %u), ware %u, %s %.2f t\n",
                            kKinds[static_cast<int>(w.kind)], w.figure, island.x + pos.x, island.y + pos.y, w.island,
                            w.ware, w.returning ? "carrying" : "reserved",
                            static_cast<double>(w.returning ? w.load : w.reserved) / assets::kStockUnitsPerTon);
            });
        }
    }
    graphics_options.derive_missing_sheets = !args.flag("--original-sheets");
    auto banners = render::BannerTable::original(&*figures, render::BannerOptions{args.flag("--original-flags")});
    // --banner P:RRGGBB gives player P a new banner in any colour (tinted generic flag).
    if (const auto it = args.options.find("--banner"); it != args.options.end()) {
        const std::string spec(it->second);
        const auto colon = spec.find(':');
        if (colon == std::string::npos || spec.size() - colon - 1 != 6) {
            return usage();
        }
        const auto slot = static_cast<std::size_t>(std::strtoul(spec.substr(0, colon).c_str(), nullptr, 10));
        const auto rgb = static_cast<std::uint32_t>(std::strtoul(spec.substr(colon + 1).c_str(), nullptr, 16));
        auto players = world->players();
        if (slot >= players.size()) {
            return fail(core::make_error(core::ErrorCode::OutOfRange, "no player " + std::to_string(slot)));
        }
        players[slot].banner =
            banners.add_colour("custom:" + spec.substr(colon + 1),
                               assets::Rgba{static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                                            static_cast<std::uint8_t>(rgb), 255});
        world->set_players(std::move(players));
    }
    auto graphics = render::load_world_graphics(*game, *palette, view.zoom, render::kWorldFigureSheets,
                                                graphics_options, &banners, &*figures);
    if (!graphics) {
        return fail(graphics.error());
    }
    const render::SpriteAtlas* atlas = &graphics->atlas;

    render::AnimationClock clock(*buildings);
    clock.advance(static_cast<std::uint32_t>(args.number("--time").value_or(0)));

    render::Viewport viewport;
    world_pixel_size(view, viewport.width, viewport.height);
    if (const auto size = args.pair("--size", 'x')) {
        viewport.width = std::max(1, size->first);
        viewport.height = std::max(1, size->second);
    }
    if (const auto tile = args.pair("--tile", ',')) {
        std::int32_t cx = 0;
        std::int32_t cy = 0;
        render::world_tile_center(view, static_cast<std::uint32_t>(std::clamp(tile->first, 0, 499)),
                                  static_cast<std::uint32_t>(std::clamp(tile->second, 0, 349)), cx, cy);
        viewport.left = cx - viewport.width / 2;
        viewport.top = cy - viewport.height / 2;
    }
    if (std::int64_t{viewport.width} * viewport.height > 200'000'000) {
        return fail(core::make_error(core::ErrorCode::OutOfRange,
                                     "image too large; use --zoom 2 or --size/--tile for a part of the world"));
    }

    render::WorldLayers layers;
    layers.extent = render::SpriteExtent{atlas->max_sprite_width(), atlas->max_sprite_height()};
    layers.figures = &*figures;
    layers.atlas = atlas;
    layers.ship_base = graphics->sheet_base(assets::FigureSheet::Ship).value_or(0);
    layers.sheet_base = graphics->base;
    layers.banners = &banners;
    layers.banner_flags = &graphics->banner_flags;
    layers.owner_tint = args.flag("--owners");
    const auto building_figures = render::collect_building_figures(*world, *buildings, *figures);
    layers.building_figures = &building_figures;
    layers.local_player = sim::first_human_player(*world);
    render::AnimalFigures animals;
    animals.reset(*world, *buildings, *figures);
    animals.advance(static_cast<std::uint32_t>(args.number("--time").value_or(0)), *figures);
    render::SoldierFigures soldiers;
    soldiers.reset(*world, *buildings, *figures);
    soldiers.advance(static_cast<std::uint32_t>(args.number("--time").value_or(0)), *figures);
    render::WalkerFigures walkers;
    if (simulation) {
        walkers.update(simulation->logistics(), *world, *buildings, *figures, 0);
    }
    if (simulation) {
        layers.land_figures = {&soldiers.instances(), &walkers.instances()};  // deer walk with the simulation
    } else {
        layers.land_figures = {&animals.instances(), &soldiers.instances()};
    }
    layers.unit_tint = &graphics->unit_tint;
    std::vector<render::DrawCommand> commands;
    render::build_world_draw_list(*world, *buildings, clock, view, viewport, layers, commands);
    render::RgbaImage image;
    image.width = static_cast<std::uint32_t>(viewport.width);
    image.height = static_cast<std::uint32_t>(viewport.height);
    image.pixels.assign(std::size_t{image.width} * image.height * 4, 0);
    for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
        image.pixels[i] = 255;  // black outside the map, as in the original
    }
    render::blit_draw_list(commands, *atlas, image, viewport.left, viewport.top);
    if (auto status = core::write_png_rgba(fs::path(args.positional[1]), image.width, image.height, image.pixels);
        !status) {
        return fail(status.error());
    }
    for (const auto& p : world->players()) {
        const render::Banner* b = banners.get(p.banner);
        std::printf("player: %s, %s, banner %s, money %d\n", sim::role_name(p.role), world->faction(p).id.c_str(),
                    b != nullptr ? b->id.c_str() : "?", p.money);
    }
    std::printf("%zu building figures (flags, smoke, status icons)\n", building_figures.size());

    std::printf("%zu ships, %zu islands, %zu sprites, atlas %zu page(s) -> %s (%ux%u)\n", world->ships().size(),
                world->islands().size(), commands.size(), atlas->pages().size(),
                std::string(args.positional[1]).c_str(), image.width, image.height);
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    return 0;
}

}  // namespace o1602::tool
