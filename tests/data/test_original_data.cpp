// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests against an original installation. They are skipped unless ANNO1602_DIR points to it:
//
//   ANNO1602_DIR="$HOME/Games/Heroic/Anno 1602" ctest --preset debug
//
// Expected values were taken from the GOG "Koenigsedition" (German).

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/data_fixes.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/core/file.hpp"
#include "o1602/render/ambient.hpp"
#include "o1602/render/animation_clock.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/world_graphics.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/drag_path.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/island_events.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

assets::GameDirectory require_game() {
    auto dir = assets::GameDirectory::from_environment();
    if (!dir) {
        SKIP("ANNO1602_DIR not set");
    }
    return std::move(dir).value();
}

void check_sheet_decodes(const assets::GameDirectory& game, const char* file, std::size_t expected_count) {
    const auto path = game.resolve(file);
    REQUIRE(path.has_value());
    auto sheet = assets::SpriteSheet::load(*path);
    REQUIRE(sheet.ok());
    CHECK(sheet->size() == expected_count);
    std::size_t failures = 0;
    for (std::size_t i = 0; i < sheet->size(); ++i) {
        if (!sheet->decode(i).ok()) {
            ++failures;
        }
    }
    CHECK(failures == 0);
}

}  // namespace

TEST_CASE("data: palette STADTFLD.COL") {
    const auto game = require_game();
    auto palette = assets::Palette::load(*game.resolve("toolgfx/stadtfld.col"));
    REQUIRE(palette.ok());
    CHECK((*palette)[0] == (assets::Rgba{0, 0, 0, 255}));
}

TEST_CASE("data: all sprites of the main BSH files decode") {
    const auto game = require_game();
    check_sheet_decodes(game, "gfx/stadtfld.bsh", 5964);
    check_sheet_decodes(game, "mgfx/stadtfld.bsh", 5964);
    check_sheet_decodes(game, "sgfx/stadtfld.bsh", 5964);
    check_sheet_decodes(game, "gfx/ship.bsh", 368);
    check_sheet_decodes(game, "gfx/traeger.bsh", 1784);
    // The History Edition's START.BSH has a sixth main menu button (and its pressed image).
    check_sheet_decodes(game, "toolgfx/start.bsh", game.history_texts() == nullptr ? 90 : 92);
}

TEST_CASE("data: haeuser.cod decodes to readable text") {
    const auto game = require_game();
    auto text = assets::load_cod_utf8(*game.resolve("haeuser.cod"));
    REQUIRE(text.ok());
    CHECK(text->find("IDBODEN") != std::string::npos);
    CHECK(text->find("Objekt: HAUS") != std::string::npos);
}

TEST_CASE("data: island template NORD/big.scp") {
    const auto game = require_game();
    auto file = assets::ChunkFile::load(*game.resolve("nord/big.scp"));
    REQUIRE(file.ok());
    auto islands = assets::load_islands(*file);
    REQUIRE(islands.ok());
    REQUIRE(islands->size() == 1);
    const auto& island = (*islands)[0];
    CHECK(island.header.width == 70);
    CHECK(island.header.height == 60);
    REQUIRE(island.layers.size() == 1);
    for (const auto& tile : island.layers[0]) {
        CHECK(tile.x < island.header.width);
        CHECK(tile.y < island.header.height);
    }
}

// ---------------------------------------------------------------------------------------------
// .cod interpreter against the original files

namespace {

assets::cod::Document parse_original_cod(const assets::GameDirectory& game, const char* file,
                                         std::span<const assets::CodPredefinedSymbol> predefined) {
    const auto path = game.resolve(file);
    REQUIRE(path.has_value());
    auto bytes = core::read_file(*path);
    REQUIRE(bytes.ok());
    return assets::cod::parse(assets::decode_cod(*bytes), predefined);
}

std::set<std::string> undefined_symbols(const assets::cod::Document& doc) {
    std::set<std::string> names;
    for (const auto& d : doc.diagnostics) {
        const auto a = d.message.find('\'');
        const auto b = d.message.rfind('\'');
        if (d.message.starts_with("undefined symbol") && a != std::string::npos && b > a) {
            names.insert(d.message.substr(a + 1, b - a - 1));
        }
    }
    return names;
}

}  // namespace

TEST_CASE("data: haeuser.cod interprets with the predefined symbols") {
    const auto game = require_game();
    const auto doc = parse_original_cod(game, "haeuser.cod", assets::cod_predefined_haeuser());

    // Only the two references that are undefined in the original as well.
    CHECK(undefined_symbols(doc) == (std::set<std::string>{"WAV_BAECKER", "FAHNEKONTOR"}));
    CHECK(doc.diagnostics.size() == 2);
    CHECK(doc.symbols.size() < 650);  // capacity of the original symbol table

    const auto* haus = doc.root.records("HAUS");
    REQUIRE(haus != nullptr);
    CHECK(haus->size() == 500);  // ObjFill: 0,MAXHAUS

    const assets::cod::Object* meer = nullptr;
    const assets::cod::Object* baeckerei = nullptr;
    for (const auto& [nummer, record] : *haus) {
        if (record.integer("Id") == 21201) {  // IDMEER+0
            meer = &record;
        }
        if (record.integer("Id") == 20503) {  // IDHANDW+2
            baeckerei = &record;
        }
    }
    REQUIRE(meer != nullptr);
    CHECK(meer->integer("Gfx") == 758);  // GFXMEER+78 = (0+432+152+96)+78
    CHECK(meer->integer("Kind") == 19);  // MEER
    REQUIRE(baeckerei != nullptr);
    CHECK(baeckerei->integer("Size", 0) == 2);
    CHECK(baeckerei->integer("Size", 1) == 2);
    CHECK(baeckerei->records("HAUS_PRODTYP") != nullptr);
}

TEST_CASE("data: island tile ids of NORD/big.scp exist in haeuser.cod") {
    const auto game = require_game();
    const auto doc = parse_original_cod(game, "haeuser.cod", assets::cod_predefined_haeuser());
    std::set<std::int32_t> ids;
    for (const auto& [nummer, record] : *doc.root.records("HAUS")) {
        ids.insert(record.integer("Id"));
    }
    auto file = assets::ChunkFile::load(*game.resolve("nord/big.scp"));
    REQUIRE(file.ok());
    auto islands = assets::load_islands(*file);
    REQUIRE(islands.ok());
    std::size_t missing = 0;
    for (const auto& tile : (*islands)[0].layers[0]) {
        if (!ids.contains(static_cast<std::int32_t>(tile.cod_id()))) {
            ++missing;
        }
    }
    CHECK(missing == 0);
}

TEST_CASE("data: figuren.cod interprets with the predefined symbols") {
    const auto game = require_game();
    const auto doc = parse_original_cod(game, "figuren.cod", assets::cod_predefined_figuren());
    CHECK(undefined_symbols(doc) == (std::set<std::string>{"FIGTYP_KANONTURM", "FIGTYP_PIRATSHIP"}));
    CHECK(doc.diagnostics.size() == 2);
    const auto* figur = doc.root.records("FIGUR");
    REQUIRE(figur != nullptr);
    CHECK(figur->size() == 119);  // ObjFill: 0,MAXFIGUR

    std::size_t soldat_lines = 0;
    for (const auto& [name, values] : doc.root.history) {
        if (name == "Soldat") {
            ++soldat_lines;
        }
    }
    CHECK(soldat_lines == 4);  // Schwert, Kavalerie, Musketier, Kanonier
}

// ---------------------------------------------------------------------------------------------
// Building table, island maps and rendering against the original data

namespace {

assets::BuildingTable original_buildings(const assets::GameDirectory& game) {
    const auto doc = parse_original_cod(game, "haeuser.cod", assets::cod_predefined_haeuser());
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("data: building table from haeuser.cod") {
    const auto game = require_game();
    const auto table = original_buildings(game);
    CHECK(table.types().size() == 498);  // all records with an id; 3 ids occur twice in the original (D-007)
    const auto* meer = table.by_id(21201);
    REQUIRE(meer != nullptr);
    CHECK(meer->gfx == 758);
    CHECK(meer->anim_count == 6);
    CHECK(table.by_sprite(758) == meer);
    const auto* baeckerei = table.by_id(20503);
    REQUIRE(baeckerei != nullptr);
    CHECK(baeckerei->rotate == baeckerei->tile_count());  // 2x2 building: 4 sprites per rotation
    CHECK(table.by_sprite(baeckerei->gfx + 3) == baeckerei);
    // Production of the bakery: 2 t flour -> 1 t food (Prodmenge from the defaults) in 19 s.
    CHECK(baeckerei->prod_kind == 1);
    CHECK(baeckerei->raw_amount == 2 * assets::kStockUnitsPerTon);
    CHECK(baeckerei->produce_amount == assets::kStockUnitsPerTon);
    CHECK(baeckerei->raw2_amount == 0);
    CHECK(baeckerei->interval == 19);
    CHECK(baeckerei->max_stock == 4 * assets::kStockUnitsPerTon);
    CHECK(baeckerei->worker_count == 1);
    CHECK(baeckerei->ware != 0);
    CHECK(baeckerei->raw_ware != 0);
}

TEST_CASE("data: the corrections of the original's data find what they correct (D-001, D-002, D-014)") {
    const auto game = require_game();
    auto buildings = original_buildings(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(figures.ok());
    CHECK((assets::fixes::fix_buildings(buildings) == std::vector<std::string_view>{"D-001", "D-002"}));
    CHECK((assets::fixes::fix_figures(*figures) == std::vector<std::string_view>{"D-014"}));
    // The flag figure the pirate Kontor gets exists, and so does the bakery's sound file.
    CHECK(figures->get(buildings.by_id(assets::id::kPirateKontor)->attached_figures[0]) != nullptr);
    CHECK(game.resolve("SAMPLES/Backer.wav").has_value());
}

TEST_CASE("data: islands of a scenario build complete maps and render without missing sprites") {
    const auto game = require_game();
    const auto table = original_buildings(game);
    auto sheet = assets::SpriteSheet::load(*game.resolve("gfx/stadtfld.bsh"));
    REQUIRE(sheet.ok());
    auto palette = assets::Palette::load(*game.resolve("toolgfx/stadtfld.col"));
    REQUIRE(palette.ok());

    auto file = assets::ChunkFile::load(*game.resolve("szenes/Das Atoll.szs"));
    REQUIRE(file.ok());
    auto islands = assets::load_islands(*file);
    REQUIRE(islands.ok());
    REQUIRE(islands->size() == 34);

    CHECK((*islands)[0].header.template_path() == "Nord/big12.scp");
    CHECK((*islands)[2].header.template_path() == "Sued/big06.scp");

    // Islands store only deviations from their template; load it like the original loader.
    const auto load_template = [&](const assets::Island& island) -> std::vector<assets::Island> {
        if (island.header.template_path().empty()) {
            return {};
        }
        const auto path = game.resolve(island.header.template_path());
        REQUIRE(path.has_value());
        auto tfile = assets::ChunkFile::load(*path);
        REQUIRE(tfile.ok());
        auto tislands = assets::load_islands(*tfile);
        REQUIRE(tislands.ok());
        REQUIRE(!tislands->empty());
        return std::move(tislands).value();
    };

    std::size_t warnings_total = 0;
    std::size_t sub_tile_errors = 0;
    std::size_t empty_cells = 0;
    for (const auto& island : *islands) {
        const auto base = load_template(island);
        std::vector<std::string> warnings;
        const auto map = sim::IslandMap::from_island(island, table, &warnings, base.empty() ? nullptr : &base[0]);
        warnings_total += warnings.size();
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                if (map.top(x, y).empty()) {
                    if (!base.empty()) {
                        ++empty_cells;  // templates are complete (incl. sea), so no gaps allowed
                    }
                    continue;
                }
                const auto sub = map.sub_tile(table, x, y);
                if (sub.building == nullptr || sub.origin_x < 0 || sub.origin_y < 0 ||
                    table.by_sprite(
                        map.top(static_cast<std::uint32_t>(sub.origin_x), static_cast<std::uint32_t>(sub.origin_y))
                            .sprite) != sub.building) {
                    ++sub_tile_errors;
                }
            }
        }
    }
    CHECK(warnings_total == 0);
    CHECK(sub_tile_errors == 0);
    CHECK(empty_cells == 0);

    // Render the first island at full zoom.
    const auto base0 = load_template((*islands)[0]);
    REQUIRE(!base0.empty());
    const auto map = sim::IslandMap::from_island((*islands)[0], table, nullptr, &base0[0]);
    const auto commands = render::build_draw_list(map, table, render::RenderOptions{});
    std::size_t missing = 0;
    const auto image = render::rasterize(commands, *sheet, *palette, {}, &missing);
    CHECK(missing == 0);
    CHECK(commands.size() == 4200);  // 70 x 60, complete with template
    CHECK(image.width > 0);
    CHECK(image.height > 0);
}

TEST_CASE("data: all island header variants (INSEL3/4/5) load") {
    const auto game = require_game();
    for (const char* f : {"nord/big03.scp", "sued/big11.scp", "sued/big04.scp"}) {
        auto file = assets::ChunkFile::load(*game.resolve(f));
        REQUIRE(file.ok());
        auto islands = assets::load_islands(*file);
        REQUIRE(islands.ok());
        CHECK(islands->size() == 1);
    }
}

TEST_CASE("data: world of a scenario loads and draws without missing sprites") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(buildings->open_sea() != nullptr);
    CHECK(buildings->open_sea()->id == 21201);

    const auto scenario = game.resolve("szenes/das atoll.szs");
    REQUIRE(scenario.has_value());
    auto file = assets::ChunkFile::load(*scenario);
    REQUIRE(file.ok());
    std::vector<std::string> warnings;
    auto world = sim::load_world(*file, *buildings, game, &warnings);
    REQUIRE(world.ok());
    CHECK(warnings.empty());
    CHECK(world->islands().size() == 34);

    // Whole world at the smallest zoom, all four view rotations: every sprite exists in the atlas.
    auto sheet = assets::SpriteSheet::load(*game.resolve(render::terrain_sheet_path(render::Zoom::Small)));
    REQUIRE(sheet.ok());
    auto palette = assets::Palette::load(*game.resolve(render::kTerrainPalettePath));
    REQUIRE(palette.ok());
    auto atlas = render::SpriteAtlas::build(*sheet, *palette);
    REQUIRE(atlas.ok());
    const render::AnimationClock clock(*buildings);
    for (std::uint8_t rotation = 0; rotation < 4; ++rotation) {
        render::WorldView view;
        view.zoom = render::Zoom::Small;
        view.rotation = rotation;
        render::Viewport all;
        render::world_pixel_size(view, all.width, all.height);
        const auto commands =
            render::build_world_draw_list(*world, *buildings, clock, view, all,
                                          render::SpriteExtent{atlas->max_sprite_width(), atlas->max_sprite_height()});
        CHECK(commands.size() >= std::size_t{500} * 350);
        std::size_t missing = 0;
        for (const auto& c : commands) {
            if (atlas->rect(c.sprite).empty()) {
                ++missing;
            }
        }
        CHECK(missing == 0);
    }
}

TEST_CASE("data: players, colours and ships of scenarios") {
    const auto game = require_game();
    auto atoll = assets::ChunkFile::load(*game.resolve("szenes/das atoll.szs"));
    REQUIRE(atoll.ok());
    auto players = assets::load_players(*atoll);
    REQUIRE(players.ok());
    REQUIRE(players->size() == 7);
    using K = assets::PlayerKind;
    using C = assets::PlayerColour;
    const K kinds[] = {K::Human, K::Computer, K::Computer, K::Computer, K::Trader, K::Pirate, K::Natives};
    const C colours[] = {C::Red, C::Blue, C::Yellow, C::White, C::Trader, C::Pirate, C::Natives};
    for (std::size_t i = 0; i < 7; ++i) {
        CHECK((*players)[i].slot == i);
        CHECK((*players)[i].kind == kinds[i]);
        CHECK((*players)[i].colour == colours[i]);
    }
    CHECK((*players)[0].money == 10000);

    auto ships = assets::load_ships(*atoll);
    REQUIRE(ships.ok());
    REQUIRE(ships->size() == 8);
    CHECK((*ships)[7].name == "Patria");
    CHECK((*ships)[7].x == 249);
    CHECK((*ships)[7].y == 156);
    CHECK((*ships)[7].owner == 0);
    CHECK((*ships)[0].owner == 5);    // pirate ships
    CHECK((*ships)[0].figure == 31);  // PIRAT

    // Endlosspiel00: the human player has the blue flag, the first computer player the red one.
    auto endless = assets::ChunkFile::load(*game.resolve("szenes/endlosspiel00.szs"));
    REQUIRE(endless.ok());
    auto endless_players = assets::load_players(*endless);
    REQUIRE(endless_players.ok());
    CHECK((*endless_players)[0].colour == C::Blue);
    CHECK((*endless_players)[1].colour == C::Red);

    // Every ship refers to a ship figure; every flag colour has its sprites in SHIP.BSH.
    auto figures = assets::FigureTable::load(game);
    REQUIRE(figures.ok());
    for (const auto& ship : *ships) {
        const auto* f = figures->get(ship.figure);
        REQUIRE(f != nullptr);
        CHECK(f->sheet == assets::FigureSheet::Ship);
    }
    for (std::uint8_t c = 0; c < 5; ++c) {
        const auto* flag = figures->flag_for_colour(c);
        REQUIRE(flag != nullptr);
        CHECK(flag->gfx == 192U + 8U * c);
    }
    CHECK(figures->flag_for_colour(5) == nullptr);
    CHECK(figures->flag_for_colour(6) == nullptr);

    // The base game's banners: players tinted from the white flag, pirates with their skull,
    // natives and trader without flag; optionally the original images for the players.
    const auto banners = render::BannerTable::original(&*figures);
    REQUIRE(banners.size() == 7);
    for (sim::BannerId b = 0; b < 4; ++b) {
        CHECK(banners.get(b)->flag == render::FlagStyle::Tinted);
    }
    CHECK(banners.get(4)->flag == render::FlagStyle::Sprites);
    CHECK(banners.get(4)->flag_gfx == 224U);
    CHECK(banners.get(5)->flag == render::FlagStyle::None);
    CHECK(banners.get(6)->flag == render::FlagStyle::None);
    REQUIRE(banners.flag_template().has_value());
    CHECK(banners.flag_template()->gfx == 216U);  // FAHNE4
    const auto original = render::BannerTable::original(&*figures, render::BannerOptions{true});
    for (sim::BannerId b = 0; b < 5; ++b) {
        CHECK(original.get(b)->flag == render::FlagStyle::Sprites);
        CHECK(original.get(b)->flag_gfx == 192U + 8U * b);
    }

    // Tinted flags in the atlas: one block of 8 images per tinted banner, close to the originals.
    auto palette = assets::Palette::load(*game.resolve(render::kTerrainPalettePath));
    REQUIRE(palette.ok());
    auto g = render::load_world_graphics(game, *palette, render::Zoom::Large, render::kWorldFigureSheets, {}, &banners);
    REQUIRE(g.ok());
    REQUIRE(g->banner_flags.size() == 7);
    REQUIRE(g->banner_flags[0].has_value());
    REQUIRE(g->banner_flags[3].has_value());
    CHECK(*g->banner_flags[1] == *g->banner_flags[0] + 8);
    CHECK(!g->banner_flags[4].has_value());  // pirates use their own images
    const auto& red = g->atlas.rect(*g->banner_flags[0]);
    const auto& original_red = g->atlas.rect(*g->sheet_base(assets::FigureSheet::Ship) + 192);
    CHECK(red.width == original_red.width);
    CHECK(red.height == original_red.height);
}

TEST_CASE("data: warehouse, market and tower flags of a scenario") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    auto figures = assets::FigureTable::load(game);
    REQUIRE(figures.ok());
    auto file = assets::ChunkFile::load(*game.resolve("szenes/das atoll.szs"));
    REQUIRE(file.ok());
    auto world = sim::load_world(*file, *buildings, game, nullptr);
    REQUIRE(world.ok());

    const auto figures_on_buildings = render::collect_building_figures(*world, *buildings, *figures);
    // Island by island, as the game collects them again after building: the same list in order.
    std::vector<render::BuildingFigure> by_island;
    for (std::uint16_t i = 0; i < world->islands().size(); ++i) {
        render::collect_island_building_figures(*world, *buildings, *figures, i, by_island);
    }
    REQUIRE(by_island.size() == figures_on_buildings.size());
    for (std::size_t i = 0; i < by_island.size(); ++i) {
        CHECK(by_island[i].island == figures_on_buildings[i].island);
        CHECK(by_island[i].origin_x == figures_on_buildings[i].origin_x);
        CHECK(by_island[i].origin_y == figures_on_buildings[i].origin_y);
        CHECK(by_island[i].figure == figures_on_buildings[i].figure);
        CHECK((i == 0 || by_island[i - 1].island <= by_island[i].island));
    }
    // "Das Atoll": the three computer players start with warehouses and markets, the pirates
    // with a fortress full of towers; the natives' buildings and the pirate warehouse fly none.
    std::size_t per_owner[8] = {};
    for (const auto& f : figures_on_buildings) {
        if (f.role != render::BuildingFigureRole::Flag) {
            continue;
        }
        const auto* fig = figures->get(f.figure);
        REQUIRE(fig != nullptr);
        CHECK(fig->sheet == assets::FigureSheet::Ship);
        CHECK(fig->gfx == 192U);  // FAHNEMARKT and its copies: one 8-frame ANIM per colour
        ++per_owner[f.owner & 7U];
    }
    CHECK(per_owner[0] == 0);
    CHECK(per_owner[1] == 8);
    CHECK(per_owner[2] == 7);
    CHECK(per_owner[3] == 7);
    CHECK(per_owner[5] == 61);
    CHECK(per_owner[6] == 0);
}

TEST_CASE("data: units tinted for banners without drawn variants") {
    const auto game = require_game();
    auto figures = assets::FigureTable::load(game);
    REQUIRE(figures.ok());
    auto palette = assets::Palette::load(*game.resolve(render::kTerrainPalettePath));
    REQUIRE(palette.ok());
    auto banners = render::BannerTable::original(&*figures);
    const sim::BannerId green = banners.add_colour("test:green", render::BannerTable::kDefaultColours[4]);
    auto g = render::load_world_graphics(game, *palette, render::Zoom::Medium, render::kWorldFigureSheets, {}, &banners,
                                         &*figures);
    REQUIRE(g.ok());
    REQUIRE(g->sheet_base(assets::FigureSheet::Soldat).has_value());
    // Four families: swordsmen 280, riders 304, gunners 216, musketeers 136 images per colour.
    REQUIRE(g->unit_tint.groups.size() == 4);
    CHECK(g->unit_tint.block_size == 280U + 304U + 216U + 136U);
    REQUIRE(g->unit_tint.banner_base.size() == banners.size());
    CHECK(!g->unit_tint.banner_base[0].has_value());  // red: drawn images
    REQUIRE(g->unit_tint.banner_base[green].has_value());
    // A tinted swordsman has the size of the white one.
    const auto white = *g->sheet_base(assets::FigureSheet::Soldat) + 840;
    const auto tinted = g->unit_tint.sprite(green, 0);
    REQUIRE(tinted.has_value());
    CHECK(g->atlas.rect(*tinted).width == g->atlas.rect(white).width);
    CHECK(g->atlas.rect(*tinted).height == g->atlas.rect(white).height);
}

TEST_CASE("data: figure animations and sheets of all zoom levels") {
    const auto game = require_game();
    auto figures = assets::FigureTable::load(game);
    REQUIRE(figures.ok());
    // HIRSCH (89) and ANTILOPE (91): same animations, grazing = ANIM 1 (RANDOM -> 1, 1, 3, 1).
    for (const std::uint32_t number : {89U, 91U}) {
        const auto* deer = figures->get(number);
        REQUIRE(deer != nullptr);
        CHECK(deer->sheet == assets::FigureSheet::Tiere);
        CHECK(deer->shadow == 117);  // SCHATTENLANG
        REQUIRE(deer->anim(1) != nullptr);
        CHECK(deer->anim(1)->offset == 112);
        CHECK(deer->anim(1)->count == 6);
        CHECK(deer->anim(1)->rotate == 6);
        CHECK(deer->anim(1)->kind == assets::AnimKind::Random);
        CHECK(deer->anim(0)->rotate == 8);
    }
    CHECK(figures->get(91)->gfx == figures->get(89)->gfx + 208);  // GFXANTILOPE = GFXHIRSCH + 208

    auto palette = assets::Palette::load(*game.resolve(render::kTerrainPalettePath));
    REQUIRE(palette.ok());
    // Some editions have no animals at the smallest zoom level (SGFX/Tiere.bsh); the GOG edition has them.
    const bool small_animals =
        game.resolve(render::sheet_path(assets::FigureSheet::Tiere, render::Zoom::Small)).has_value();
    for (int z = 0; z < 3; ++z) {
        const auto zoom = static_cast<render::Zoom>(z);
        if (!game.resolve(render::sheet_path(assets::FigureSheet::Tiere, zoom == render::Zoom::Small && !small_animals
                                                                             ? render::Zoom::Medium
                                                                             : zoom))) {
            SKIP("animal sheets not available");
        }
        auto g = render::load_world_graphics(game, *palette, zoom, render::kWorldFigureSheets);
        REQUIRE(g.ok());
        CHECK(g->sheet_base(assets::FigureSheet::Ship).has_value());
        CHECK(g->sheet_base(assets::FigureSheet::Tiere).has_value());
        // Missing animals at the smallest zoom level come from MGFX at half size.
        CHECK(g->derived[static_cast<std::size_t>(assets::FigureSheet::Tiere)] ==
              (zoom == render::Zoom::Small && !small_animals));
        CHECK(!g->derived[static_cast<std::size_t>(assets::FigureSheet::Ship)]);
    }
    render::GraphicsOptions original;
    original.derive_missing_sheets = false;
    auto small = render::load_world_graphics(game, *palette, render::Zoom::Small, render::kWorldFigureSheets, original);
    REQUIRE(small.ok());
    CHECK(small->sheet_base(assets::FigureSheet::Tiere).has_value() == small_animals);
    CHECK(small->sheet_base(assets::FigureSheet::Ship).has_value());
}

TEST_CASE("data: savegames of the installation (if any) load like scenarios") {
    const auto game = require_game();
    const auto dat = game.resolve("Game.dat");
    if (!dat) {
        SKIP("no Game.dat (game never started)");
    }
    auto bytes = core::read_file(*dat);
    REQUIRE(bytes.ok());
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    std::size_t loaded = 0;
    for (const auto& slot : assets::parse_savegame_slots(text)) {
        const auto path = game.resolve(slot.file);
        if (!path) {
            continue;
        }
        auto file = assets::ChunkFile::load(*path);
        REQUIRE(file.ok());
        std::vector<std::string> warnings;
        auto world = sim::load_world(*file, *buildings, game, &warnings);
        REQUIRE(world.ok());
        CHECK(warnings.empty());
        CHECK(!world->islands().empty());
        REQUIRE(world->players().size() == 7);
        CHECK(world->players()[0].role == sim::PlayerRole::Human);
        CHECK(!world->players()[0].name.empty());  // savegames store the human player's name

        // Cities and warehouses: every city of a settler player has a warehouse whose position
        // is a warehouse building on that island.
        auto state = assets::load_game_state(*file);
        REQUIRE(state.ok());
        CHECK(!world->cities().empty());
        // Every animal of HIRSCH2 stands on its island (Open1602's own chunk brings its own list).
        if (file->find("O1602") == nullptr) {
            CHECK(world->animals().size() == state->animals.size());
        }
        for (const auto& a : world->animals()) {
            CHECK(a.x < world->islands()[a.island].map.width());
            CHECK((a.figure == 89 || a.figure == 91));
        }
        for (const auto& w : state->warehouses) {
            const auto it = std::find_if(world->cities().begin(), world->cities().end(), [&](const sim::City& c) {
                return world->islands()[c.island].header.number == w.island && c.index == w.city;
            });
            REQUIRE(it != world->cities().end());
            if (it->owner >= 4) {
                continue;  // natives, pirates and the trader have no regular warehouse
            }
            const auto& map = world->islands()[it->island].map;
            REQUIRE(w.x < map.width());
            REQUIRE(w.y < map.height());
            const sim::SubTile sub = map.sub_tile(*buildings, w.x, w.y);
            REQUIRE(sub.building != nullptr);
            CHECK(sub.building->prod_kind == 8);  // HAUS_PRODTYP Kind KONTOR
            CHECK(map.top(w.x, w.y).city == w.city);
            // The world books the goods at the city (ware number = index).
            const sim::City* city = world->city(it->island, it->index);
            REQUIRE(city != nullptr);
            CHECK(city->warehouse_x == std::optional<std::uint16_t>(w.x));
            for (const auto& ware_slot : w.wares) {
                CHECK(city->stock(ware_slot.ware) == ware_slot.stock());
            }
        }
        // Soldiers stand on land (SOLDAT3 positions in half tiles).
        CHECK(world->soldiers().size() == state->soldiers.size());
        for (const auto& soldier : world->soldiers()) {
            const auto& cell =
                world->cell(static_cast<std::uint32_t>(soldier.x()), static_cast<std::uint32_t>(soldier.y()));
            CHECK(cell.island != sim::World::kSea);
            CHECK(soldier.island == cell.island);
        }
        // Towers stand in their Wachturm buildings.
        CHECK(world->towers().size() == state->towers.size());
        for (const auto& tower : world->towers()) {
            const auto b = sim::building_at(*world, *buildings, tower.island, {tower.x, tower.y});
            REQUIRE(b.has_value());
            CHECK(b->origin.x == tower.x);
            CHECK(b->origin.y == tower.y);
        }
        // PRODLIST2 names buildings by their origin tile (the lookup): every entry
        // is the origin of a building with a HAUS_PRODTYP.
        CHECK(world->productions().size() == state->production.size());
        for (const auto& p : world->productions()) {
            const auto& map = world->islands()[p.island].map;
            const sim::SubTile sub = map.sub_tile(*buildings, p.x, p.y);
            REQUIRE(sub.building != nullptr);
            CHECK(sub.origin_x == p.x);
            CHECK(sub.origin_y == p.y);
            CHECK(sub.building->prod_kind > 0);
            CHECK(p.idle_cycles <= 15);
        }
        ++loaded;
    }
    if (loaded == 0) {
        SKIP("no savegames");
    }
}

TEST_CASE("data: logistics of a savegame keep their books balanced") {
    const auto game = require_game();
    const auto dat = game.resolve("Game.dat");
    if (!dat) {
        SKIP("no Game.dat (game never started)");
    }
    auto bytes = core::read_file(*dat);
    REQUIRE(bytes.ok());
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    const auto slots = assets::parse_savegame_slots(text);
    std::optional<std::filesystem::path> path;
    for (const auto& slot : slots) {
        if (!path) {
            path = game.resolve(slot.file);
        }
    }
    if (!path) {
        SKIP("no savegame");
    }
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto file = assets::ChunkFile::load(*path);
    REQUIRE(file.ok());
    auto world = sim::load_world(*file, *buildings, game);
    REQUIRE(world.ok());
    sim::Simulation simulation(*world, *buildings, *figures);

    // Every city with a warehouse has room: the Kontor's Maxlager plus 10 t per further market.
    for (const sim::City& c : world->cities()) {
        if (c.warehouse_x) {
            CHECK(c.capacity >= 30 * assets::kStockUnitsPerTon);
        }
    }
    for (int second = 0; second < 180; ++second) {
        simulation.advance(1000);
        if (second % 30 != 29) {
            continue;
        }
        // What the figures on their way have reserved is exactly what producers and warehouses
        // keep reserved; what carts bring is what the warehouses expect.
        std::vector<std::uint32_t> reserved(world->productions().size(), 0);
        std::map<std::tuple<std::uint16_t, std::uint8_t, std::uint8_t>, std::uint32_t> city_reserved;
        std::map<std::tuple<std::uint16_t, std::uint8_t, std::uint8_t>, std::uint32_t> city_incoming;
        simulation.logistics().walkers().for_each([&](sim::Handle, const sim::Walker& w) {
            if (w.from_warehouse) {
                city_reserved[{w.island, w.city, w.ware}] += w.reserved;
            } else if (w.source) {
                reserved[*w.source] += w.reserved;
            }
            city_incoming[{w.island, w.city, w.ware}] += w.expected;
        });
        for (std::size_t i = 0; i < reserved.size(); ++i) {
            CHECK(world->productions()[i].reserved == reserved[i]);
        }
        for (const sim::City& c : world->cities()) {
            for (std::size_t ware = 0; ware < c.goods.size(); ++ware) {
                const auto key = std::make_tuple(c.island, c.index, static_cast<std::uint8_t>(ware));
                CHECK(c.reserved_of(static_cast<std::uint8_t>(ware)) == city_reserved[key]);
                CHECK(c.incoming_of(static_cast<std::uint8_t>(ware)) == city_incoming[key]);
            }
        }
    }
    const sim::LogisticsStats& stats = simulation.logistics().stats();
    CHECK(stats.carrier_trips > 0);
    CHECK(stats.cart_trips > 0);
    CHECK(stats.raw_delivered > 0);
    CHECK(stats.goods_collected > 0);
}

TEST_CASE("data: growth classes, growing tiles and deposits of a savegame") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    // Classes from Interval / AnimAnz: wheat 0, sugar cane 2, the other plantations 3,
    // fish shoals 5, forest 20, meadows 31.
    std::set<std::uint32_t> classes;
    for (const auto& b : buildings->types()) {
        if (b.prod_kind == sim::prod::kGrowing && !b.withered) {
            classes.insert(b.growth_class);
            CHECK(sim::ripe_record(*buildings, b) != nullptr);
        }
    }
    CHECK(classes.contains(0));
    CHECK(classes.contains(2));
    CHECK(classes.contains(3));
    CHECK(classes.contains(5));
    CHECK(classes.contains(20));
    CHECK(classes.contains(31));

    const auto dat = game.resolve("Game.dat");
    if (!dat) {
        SKIP("no Game.dat (game never started)");
    }
    auto bytes = core::read_file(*dat);
    REQUIRE(bytes.ok());
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    std::optional<std::filesystem::path> path;
    for (const auto& slot : assets::parse_savegame_slots(text)) {
        if (!path) {
            path = game.resolve(slot.file);
        }
    }
    if (!path) {
        SKIP("no savegame");
    }
    auto file = assets::ChunkFile::load(*path);
    REQUIRE(file.ok());
    auto state = assets::load_game_state(*file);
    REQUIRE(state.ok());
    REQUIRE(state->growth_clocks.has_value());
    auto world = sim::load_world(*file, *buildings, game);
    REQUIRE(world.ok());
    // Entries on growing tiles have the class of their record plus 0..2 (the random part a newly
    // planted tile gets). The original also keeps orphan entries on tiles that are ripe again
    // (it never frees them); Open1602 drops those.
    std::size_t checked = 0;
    for (const auto& r : state->growth) {
        const auto island = world->island_index(r.island);
        REQUIRE(island.has_value());
        const sim::MapCell& cell = world->islands()[*island].map.top(r.x, r.y);
        const assets::BuildingType* b = buildings->by_sprite(cell.sprite);
        REQUIRE(b != nullptr);
        CHECK((b->prod_kind == sim::prod::kGrowing || b->prod_kind == sim::prod::kRipe));
        if (b->prod_kind != sim::prod::kGrowing) {
            continue;
        }
        CHECK(r.growth_class >= b->growth_class);
        CHECK(r.growth_class <= b->growth_class + 2);
        ++checked;
    }
    CHECK(checked > 0);
    // Every growing tile grows.
    std::size_t growing = 0;
    for (const auto& island : world->islands()) {
        for (std::uint32_t y = 0; y < island.map.height(); ++y) {
            for (std::uint32_t x = 0; x < island.map.width(); ++x) {
                const assets::BuildingType* b =
                    island.map.top(x, y).empty() ? nullptr : buildings->by_sprite(island.map.top(x, y).sprite);
                growing += b != nullptr && b->prod_kind == sim::prod::kGrowing && !b->withered ? 1U : 0U;
            }
        }
    }
    CHECK(world->growth().entries.size() >= growing);
    // Deposits: iron veins in mountains, amounts in 1/32 t.
    std::size_t deposits = 0;
    for (const auto& island : world->islands()) {
        for (const auto& d : island.header.deposits) {
            CHECK((d.ware == 2 || d.ware == 3));
            CHECK(d.amount <= 240 * assets::kStockUnitsPerTon);
            const sim::MapCell& cell = island.map.top(d.x, d.y);
            CHECK(!cell.empty());
            ++deposits;
        }
    }
    CHECK(deposits > 0);
}

TEST_CASE("data: consumption of a savegame matches the original's numbers") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    // haeuser.cod converted like the original: Nahrung 1.3 -> 17, Stoffe 0.6 -> 8, ...
    CHECK(buildings->food_need() == 17);
    CHECK(buildings->food_ware() == 14);
    REQUIRE(buildings->resident_classes().size() == 5);
    CHECK(buildings->resident_classes()[1].share == 60);
    CHECK(buildings->resident_classes()[4].share == 107);
    CHECK(buildings->resident_classes()[4].needs.size() == 6);
    CHECK(sim::tax_curve(128) == 128);
    CHECK(sim::tax_curve(64) == 86);
    CHECK(sim::tax_curve(160) == 197);
    CHECK(sim::tax_curve(192) == 352);

    const auto dat = game.resolve("Game.dat");
    if (!dat) {
        SKIP("no Game.dat (game never started)");
    }
    auto bytes = core::read_file(*dat);
    REQUIRE(bytes.ok());
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    std::optional<std::filesystem::path> path;
    for (const auto& slot : assets::parse_savegame_slots(text)) {
        if (!path) {
            path = game.resolve(slot.file);
        }
    }
    if (!path) {
        SKIP("no savegame");
    }
    auto file = assets::ChunkFile::load(*path);
    REQUIRE(file.ok());
    auto world = sim::load_world(*file, *buildings, game);
    REQUIRE(world.ok());
    // The saved satisfaction follows from the saved supply levels (MARKT2) and tax rates.
    std::size_t checked = 0;
    for (const sim::City& c : world->cities()) {
        const sim::Player* owner = world->player(c.owner);
        if (owner == nullptr || (owner->role != sim::PlayerRole::Human && owner->role != sim::PlayerRole::Computer)) {
            continue;
        }
        for (std::size_t k = 1; k < 5; ++k) {
            if (c.satisfaction[k] == 0) {
                continue;  // never computed
            }
            CHECK(sim::satisfaction_of(c, buildings->resident_classes()[k], c.tax_rate[k]) == c.satisfaction[k]);
            ++checked;
        }
    }
    CHECK(checked > 0);
    // A few minutes of game time: food leaves the warehouses, the cities stay fed or report it.
    auto figures = assets::FigureTable::load(game);
    REQUIRE(figures.ok());
    std::uint64_t food_before = 0;
    for (const sim::City& c : world->cities()) {
        food_before += c.stock(14);
    }
    sim::Simulation simulation(*world, *buildings, *figures);
    for (int s = 0; s < 120; ++s) {
        simulation.advance(1000);
    }
    std::uint64_t food_after = 0;
    for (const sim::City& c : world->cities()) {
        food_after += c.stock(14);
        for (std::size_t w = 0; w < c.supply.size(); ++w) {
            CHECK(c.supply[w].ratio <= 128);
        }
    }
    CHECK(food_after != food_before);
}

TEST_CASE("data: houses of a savegame add up to the cities' inhabitants") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    const auto dat = game.resolve("Game.dat");
    if (!dat) {
        SKIP("no Game.dat (game never started)");
    }
    auto bytes = core::read_file(*dat);
    REQUIRE(bytes.ok());
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    std::optional<std::filesystem::path> path;
    for (const auto& slot : assets::parse_savegame_slots(text)) {
        if (!path) {
            path = game.resolve(slot.file);
        }
    }
    if (!path) {
        SKIP("no savegame");
    }
    auto file = assets::ChunkFile::load(*path);
    REQUIRE(file.ok());
    auto state = assets::load_game_state(*file);
    REQUIRE(state.ok());
    auto world = sim::load_world(*file, *buildings, game);
    REQUIRE(world.ok());
    CHECK(!world->houses().empty());
    CHECK(world->houses().size() == state->houses.size());
    std::map<std::tuple<std::uint16_t, std::uint8_t, std::uint8_t>, std::uint32_t> sums;
    for (const sim::House& h : world->houses()) {
        CHECK(h.resident_class < buildings->resident_classes().size());
        CHECK(h.inhabitants() <= buildings->resident_classes()[h.resident_class].max_per_house);
        sums[{h.island, h.city, h.resident_class}] += h.inhabitants();
    }
    for (const sim::City& c : world->cities()) {
        for (std::uint8_t k = 0; k < 5 && k < c.inhabitants.size(); ++k) {
            const auto key = std::make_tuple(c.island, c.index, k);
            CHECK(sums[key] == c.inhabitants[k]);
        }
    }
    // The coverage by markets, Kontors and public buildings recomputed equals
    // what the original saved (bits 2..10, market, distance class, services complete).
    const std::vector<sim::House> saved = world->houses();
    sim::update_services(*world, *buildings);
    for (std::size_t i = 0; i < saved.size(); ++i) {
        const sim::House& a = saved[i];
        const sim::House& b = world->houses()[i];
        CHECK((a.services & 0x7FCU) == (b.services & 0x7FCU));
        CHECK(a.market == b.market);
        CHECK(a.market_distance == b.market_distance);
        CHECK(a.services_complete == b.services_complete);
    }
}

namespace {

/// A loaded savegame of the installation.
struct LoadedSave {
    std::string name;
    sim::World world;
};

/// A world plan, or a savegame of an endless game the original made from one (NAME "Endlosspiel",
/// in the History Edition its placeholder "[[48]]").
bool endless_start(const assets::ChunkFile& file) {
    if (sim::plans_islands(file)) {
        return true;
    }
    const assets::Chunk* name = file.find("NAME");
    if (name == nullptr) {
        return false;
    }
    const auto data = file.payload(*name);
    const std::string text(data.begin(), std::find(data.begin(), data.end(), std::uint8_t{0}));
    return text == "Endlosspiel" || text == "[[48]]";
}

/// All savegames in SAVEGAME/ that load, oldest game time first.
std::vector<LoadedSave> load_savegames(const assets::GameDirectory& game, const assets::BuildingTable& buildings) {
    std::vector<LoadedSave> saves;
    const auto dir = game.resolve("SAVEGAME");
    if (!dir || !std::filesystem::is_directory(*dir)) {
        return saves;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(*dir)) {
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (entry.is_regular_file() && ext == ".gam") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        auto file = assets::ChunkFile::load(f);
        if (!file.ok()) {
            continue;
        }
        if (endless_start(*file)) {
            continue;  // a fresh endless game: no cities of the players yet (test_new_game.cpp)
        }
        auto world = sim::load_world(*file, buildings, game);
        if (world.ok()) {
            saves.push_back(LoadedSave{f.filename().string(), std::move(world).value()});
        }
    }
    std::stable_sort(saves.begin(), saves.end(),
                     [](const LoadedSave& x, const LoadedSave& y) { return x.world.time_ms() < y.world.time_ms(); });
    return saves;
}

/// Whether two saves belong to the same game (same islands and cities).
bool same_game(const sim::World& a, const sim::World& b) {
    if (a.islands().size() != b.islands().size() || a.cities().size() != b.cities().size() ||
        a.players().size() != b.players().size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.cities().size(); ++i) {
        if (a.cities()[i].name != b.cities()[i].name) {
            return false;
        }
    }
    return true;
}

/// Players whose money a trade moved between two saves of the same game: the owners of ships whose
/// cargo (or the free trader's goods) changed, and of the cities those ships are bound for.
std::vector<bool> traders(const sim::World& a, const sim::World& b) {
    std::vector<bool> traded(a.players().size(), false);
    const auto mark = [&](sim::PlayerId p) {
        if (p < traded.size()) {
            traded[p] = true;
        }
    };
    for (const sim::Ship& ship : a.ships()) {
        const sim::Ship* other = b.ship(ship.id);
        if (other == nullptr) {
            continue;
        }
        bool changed = other->cargo.size() != ship.cargo.size();
        for (std::size_t i = 0; !changed && i < ship.cargo.size(); ++i) {
            changed = ship.cargo[i].ware != other->cargo[i].ware || ship.cargo[i].amount != other->cargo[i].amount;
        }
        for (std::size_t i = 0; !changed && i < ship.trader_goods.size() && i < other->trader_goods.size(); ++i) {
            changed = ship.trader_goods[i].amount != other->trader_goods[i].amount;
        }
        if (!changed) {
            continue;
        }
        mark(ship.owner);
        for (const sim::Target& t : {ship.target, other->target, ship.previous, other->previous}) {
            if (t.kind == sim::Target::Kind::City) {
                if (const sim::City* city = a.city(t.island, t.city)) {
                    mark(city->owner);
                }
            }
        }
    }
    return traded;
}

/// Players who paid for something between two saves of the same game: building materials left
/// one of their cities (game02 -> game03_BAK: a computer player spent 200 gold, 4 t tools and
/// 10 t wood without a tile changing yet). No production uses up these wares.
std::vector<bool> builders(const sim::World& a, const sim::World& b, const assets::BuildingTable& buildings) {
    std::vector<bool> built(a.players().size(), false);
    const auto& m = buildings.materials();
    for (std::size_t c = 0; c < a.cities().size(); ++c) {
        const sim::City& before = a.cities()[c];
        const sim::City& after = b.cities()[c];
        for (const std::uint8_t ware : {m.tools, m.wood, m.bricks, m.cannons}) {
            if (ware < before.goods.size() && ware < after.goods.size() && after.goods[ware] < before.goods[ware] &&
                before.owner < built.size()) {
                built[before.owner] = true;
            }
        }
    }
    return built;
}

}  // namespace

TEST_CASE("data: money of the next city tick matches the original (pairs of savegames)") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    const auto saves = load_savegames(game, *buildings);
    // Saves less than one city tick (10 s) apart whose money differs: every city ticked once
    // in between (the original works through all cities within a few frames).
    std::size_t pairs = 0;
    for (std::size_t i = 0; i + 1 < saves.size(); ++i) {
        const sim::World& world = saves[i].world;
        const sim::World& later = saves[i + 1].world;
        const std::uint64_t dt = later.time_ms() - world.time_ms();
        if (!same_game(world, later) || dt == 0 || dt >= 10'000 || world.players().size() < 4 ||
            later.players()[0].money == world.players()[0].money) {
            continue;
        }
        ++pairs;
        const std::vector<std::int32_t> costs = sim::running_costs(world, *buildings);
        std::vector<std::int32_t> delta(world.players().size(), 0);
        for (std::size_t c = 0; c < world.cities().size(); ++c) {
            const sim::City& city = world.cities()[c];
            const sim::Player* owner = world.player(city.owner);
            if (owner == nullptr ||
                (owner->role != sim::PlayerRole::Human && owner->role != sim::PlayerRole::Computer)) {
                continue;
            }
            delta[city.owner] += (sim::tax_income(city, *buildings) - costs[c]) / 6;
        }
        // A trade in between moves money too (game08 -> game09: the free trader sold tools).
        // So does building something.
        const std::vector<bool> traded = traders(world, later);
        const std::vector<bool> built = builders(world, later, *buildings);
        for (std::size_t p = 0; p < 4; ++p) {
            if (traded[p] || built[p]) {
                continue;
            }
            CHECK(world.players()[p].money + delta[p] == later.players()[p].money);
        }
    }
    if (pairs == 0) {
        SKIP("no two savegames one city tick apart");
    }
}

TEST_CASE("data: a savegame played on in the original replays tile for tile (O1602_REPLAY)") {
    // O1602_REPLAY=SAVEGAME/a.gam,SAVEGAME/b.gam: b was saved after loading a and playing on
    // (the original sets every deer back to its place when it loads). Open1602 plays a for the
    // same game time and must end up with the same deer slots and production states as b.
    const char* env = std::getenv("O1602_REPLAY");
    if (env == nullptr) {
        SKIP("set O1602_REPLAY=earlier.gam,later.gam");
    }
    const std::string spec(env);
    const auto comma = spec.find(',');
    REQUIRE(comma != std::string::npos);
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    const auto first = game.resolve(spec.substr(0, comma));
    const auto second = game.resolve(spec.substr(comma + 1));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    auto file = assets::ChunkFile::load(*first);
    auto file2 = assets::ChunkFile::load(*second);
    REQUIRE(file.ok());
    REQUIRE(file2.ok());
    auto world = sim::load_world(*file, *buildings, game);
    auto later = sim::load_world(*file2, *buildings, game);
    REQUIRE(world.ok());
    REQUIRE(later.ok());
    REQUIRE(later->time_ms() > world->time_ms());
    sim::Simulation simulation(*world, *buildings, *figures);
    const std::uint64_t ticks = (later->time_ms() - world->time_ms() + 25) / 50;
    for (std::uint64_t t = 0; t < ticks; ++t) {
        simulation.advance(50);
    }
    // Deer slots: same places in the same order; a wait set in between agrees within a second
    // (the original's clock has 1/10 s steps and frame-sized sub-steps).
    for (std::size_t i = 0; i < world->islands().size(); ++i) {
        const auto& ours = world->islands()[i].deer_spots;
        const auto& theirs = later->islands()[i].deer_spots;
        for (std::size_t s = 0; s < ours.size(); ++s) {
            CHECK(ours[s].used == theirs[s].used);
            if (!ours[s].used || !theirs[s].used) {
                continue;
            }
            CHECK(ours[s].x == theirs[s].x);
            CHECK(ours[s].y == theirs[s].y);
            const auto d =
                static_cast<std::int64_t>(ours[s].respawn_ms) - static_cast<std::int64_t>(theirs[s].respawn_ms);
            CHECK(std::abs(d) <= 1000);
        }
    }
    // Hunting lodges: raw material, cycle, statistics.
    std::size_t lodges = 0;
    for (const sim::Production& p : later->productions()) {
        const auto& cell = later->islands()[p.island].map.top(p.x, p.y);
        const assets::BuildingType* b = buildings->by_sprite(cell.sprite);
        if (b == nullptr || b->prod_kind != 5) {
            continue;
        }
        const auto idx = world->production_index(p.island, p.x, p.y);
        REQUIRE(idx.has_value());
        const sim::Production& q = world->productions()[*idx];
        ++lodges;
        CHECK(q.raw == p.raw);
        CHECK(q.stock == p.stock);
        CHECK(q.rate == p.rate);
        // The original's production second is not saved: its phase after loading may differ by
        // up to one second.
        CHECK(std::abs(static_cast<int>(q.countdown) - static_cast<int>(p.countdown)) <= 1);
        CHECK(q.idle_cycles == p.idle_cycles);
        CHECK(q.produced == p.produced);
        CHECK(q.elapsed == p.elapsed);
    }
    CHECK(lodges > 0);
}

namespace {

/// The first savegame named in Game.dat, if the installation has one.
std::optional<std::filesystem::path> first_savegame(const assets::GameDirectory& game) {
    const auto dat = game.resolve("Game.dat");
    if (!dat) {
        return std::nullopt;
    }
    auto bytes = core::read_file(*dat);
    if (!bytes) {
        return std::nullopt;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    for (const auto& slot : assets::parse_savegame_slots(text)) {
        if (auto path = game.resolve(slot.file)) {
            return path;
        }
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("data: deer spots of a savegame follow the census rule") {
    const auto game = require_game();
    const auto path = first_savegame(game);
    if (!path) {
        SKIP("no savegame");
    }
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    auto file = assets::ChunkFile::load(*path);
    REQUIRE(file.ok());
    auto world = sim::load_world(*file, *buildings, game);
    REQUIRE(world.ok());
    // Every island holds at most min(8, (n + 5) / 8) deer for its n forest spots, and almost all
    // hold exactly that many (the census draws that often; double picks leave a slot empty).
    std::size_t islands = 0;
    std::size_t exact = 0;
    for (std::uint16_t i = 0; i < world->islands().size(); ++i) {
        std::size_t used = 0;
        for (const auto& s : world->islands()[i].deer_spots) {
            used += s.used ? 1U : 0U;
        }
        if (used == 0) {
            continue;
        }
        const std::uint32_t allowed = sim::deer_allowed(sim::deer_census_spots(*world, *buildings, i).size());
        CHECK(used <= allowed);
        ++islands;
        exact += used == allowed ? 1U : 0U;
    }
    CHECK(islands > 0);
    CHECK(exact * 10 >= islands * 9);
}

TEST_CASE("data: a hunting lodge set into a savegame hunts with the original data") {
    const auto game = require_game();
    const auto path = first_savegame(game);
    if (!path) {
        SKIP("no savegame");
    }
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto file = assets::ChunkFile::load(*path);
    REQUIRE(file.ok());
    auto world = sim::load_world(*file, *buildings, game);
    REQUIRE(world.ok());
    const assets::BuildingType* lodge = nullptr;
    for (const auto& b : buildings->types()) {
        if (b.prod_kind == 5) {  // JAGDHAUS
            lodge = &b;
            break;
        }
    }
    REQUIRE(lodge != nullptr);
    // A deer spot on the territory of a city and an open ground tile of that city next to it.
    bool placed = false;
    std::uint32_t lodge_index = 0;
    for (std::uint16_t i = 0; i < world->islands().size() && !placed; ++i) {
        sim::IslandMap& map = world->island(i).map;
        for (const auto& spot : world->islands()[i].deer_spots) {
            if (!spot.used || map.top(spot.x, spot.y).city == sim::MapCell::kNoCity || placed) {
                continue;
            }
            const sim::MapCell centre = map.top(spot.x, spot.y);
            for (std::int32_t r = 2; r <= 5 && !placed; ++r) {
                for (std::int32_t dy = -r; dy <= r && !placed; ++dy) {
                    for (std::int32_t dx = -r; dx <= r && !placed; ++dx) {
                        const std::int32_t x = spot.x + dx;
                        const std::int32_t y = spot.y + dy;
                        if (!map.contains(x, y)) {
                            continue;
                        }
                        const sim::MapCell& c = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                        const assets::BuildingType* t = buildings->by_sprite(c.sprite);
                        if (t == nullptr || t->kind != 11 || t->prod_kind == sim::prod::kGrowing ||
                            c.city != centre.city) {
                            continue;
                        }
                        REQUIRE(map.place(*lodge, x, y, 0, 0, centre.owner, centre.city).ok());
                        sim::Production p;
                        p.island = i;
                        p.x = static_cast<std::uint16_t>(x);
                        p.y = static_cast<std::uint16_t>(y);
                        world->set_production(p);
                        lodge_index = *world->production_index(i, p.x, p.y);
                        placed = true;
                    }
                }
            }
        }
    }
    REQUIRE(placed);
    sim::Simulation simulation(*world, *buildings, *figures);
    for (int s = 0; s < 900; ++s) {
        simulation.advance(1000);
    }
    // In 15 minutes the hunter brought several deer; the lodge made food from them.
    CHECK(simulation.logistics().stats().deer_hunted >= 3);
    const sim::Production& p = world->productions()[lodge_index];
    CHECK(p.produced > 0);
}

TEST_CASE("data: unlocked build levels of the players follow the BAUINFRA table") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    const auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    CHECK(buildings->infra_levels().size() >= 33);
    CHECK(buildings->infra_levels()[4].min_inhabitants == 50);  // INFRA_WIRT: 50 settlers
    std::size_t checked = 0;
    for (const auto& save : saves) {
        // Whatever a city of a player has reached now is unlocked (levels are never locked again).
        for (const sim::City& c : save.world.cities()) {
            const sim::Player* owner = save.world.player(c.owner);
            if (owner == nullptr ||
                (owner->role != sim::PlayerRole::Human && owner->role != sim::PlayerRole::Computer)) {
                continue;
            }
            sim::World copy = save.world;
            sim::Player* p = copy.player(c.owner);
            const std::uint32_t before = p->infra_mask;
            sim::unlock_levels(copy, *buildings, c, nullptr);
            CHECK(p->infra_mask == before);
            ++checked;
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("data: crops of the players stand on fertile tiles") {
    // The original plants a crop withered where the tile is not fertile; living crops of the
    // players (not the natives' plantations of the scenario) therefore lie on fertile tiles.
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    const auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    const sim::World& world = saves.front().world;
    std::size_t fields = 0;
    std::size_t fertile = 0;
    for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
        const sim::IslandMap& map = world.islands()[i].map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const sim::MapCell& cell = map.top(x, y);
                const assets::BuildingType* b = cell.empty() ? nullptr : buildings->by_sprite(cell.sprite);
                if (b == nullptr || b->withered || (b->prod_kind != 9 && b->prod_kind != 10)) {
                    continue;
                }
                const std::uint8_t crop = sim::crop_ware(*buildings, *b);
                if (crop < 45 || crop > 51) {
                    continue;  // only field crops
                }
                const sim::Player* owner = world.player(cell.owner);
                if (owner == nullptr || owner->faction == sim::faction::kNatives) {
                    continue;
                }
                ++fields;
                fertile += sim::tile_fertile(world, *buildings, i, crop, static_cast<std::int32_t>(x),
                                             static_cast<std::int32_t>(y))
                               ? 1U
                               : 0U;
            }
        }
    }
    CHECK(fields > 50);
    CHECK(fertile * 100 >= fields * 98);
}

TEST_CASE("data: the human player builds and demolishes a house in a savegame") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    sim::World& world = saves.front().world;
    const sim::PlayerId human = sim::first_human_player(world);
    const sim::City* city = nullptr;
    for (const sim::City& c : world.cities()) {
        if (c.owner == human && c.warehouse_x) {
            city = &c;
            break;
        }
    }
    if (city == nullptr) {
        SKIP("the human player has no city");
    }
    const std::uint16_t island = city->island;
    const std::uint8_t index = city->index;
    const assets::BuildingType* house = buildings->first_house(0);
    REQUIRE(house != nullptr);
    // A free 2 x 2 spot of open ground in the city.
    const sim::IslandMap& map = world.islands()[island].map;
    std::optional<sim::TilePos> spot;
    for (std::uint32_t y = 0; y + 1 < map.height() && !spot; ++y) {
        for (std::uint32_t x = 0; x + 1 < map.width() && !spot; ++x) {
            bool free = true;
            for (std::uint32_t j = 0; j < 2; ++j) {
                for (std::uint32_t i = 0; i < 2; ++i) {
                    const sim::MapCell& c = map.top(x + i, y + j);
                    const assets::BuildingType* b = c.empty() ? nullptr : buildings->by_sprite(c.sprite);
                    free = free && b != nullptr && b->kind == 11 && c.city == index;
                }
            }
            if (free) {
                spot = sim::TilePos{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
            }
        }
    }
    REQUIRE(spot.has_value());
    // Enough building material and gold whatever the savegame holds.
    {
        sim::City* c = world.city(island, index);
        for (const std::uint8_t ware : {buildings->materials().tools, buildings->materials().wood}) {
            c->ensure_ware(ware);
            c->goods[ware] = std::max<std::uint32_t>(c->goods[ware], 20 * 32);
        }
        world.player(human)->money = std::max(world.players()[human].money, 1000);
    }
    sim::Simulation simulation(world, *buildings, *figures);
    const std::int32_t money = world.players()[human].money;
    const std::uint32_t pioneers = world.city(island, index)->inhabitants[0];
    sim::BuildOrder order;
    order.player = human;
    order.island = island;
    order.items = {{house->id, spot->x, spot->y, 0, 0}};
    simulation.submit(order);
    simulation.tick();
    const auto results = simulation.take_command_results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].build.has_value());
    if (results[0].build->placed != 1) {
        std::printf("refused: %s\n", sim::refusal_name(results[0].build->items[0].refusal));
    }
    REQUIRE(results[0].build->placed == 1);
    const sim::House* h = world.house(island, static_cast<std::uint16_t>(spot->x), static_cast<std::uint16_t>(spot->y));
    REQUIRE(h != nullptr);
    CHECK(world.city(island, index)->inhabitants[0] == pioneers + 1);
    CHECK(world.players()[human].money <= money - house->build_cost.money + 50);  // money moved on in the tick
    // A minute of game time with the new house.
    for (int t = 0; t < 20 * 60; ++t) {
        simulation.tick();
    }
    CHECK(world.house(island, static_cast<std::uint16_t>(spot->x), static_cast<std::uint16_t>(spot->y)) != nullptr);
    // Demolished: the ground comes back, the inhabitants are gone.
    simulation.submit(sim::DemolishOrder{human, island, *spot, *spot});
    simulation.tick();
    CHECK(world.house(island, static_cast<std::uint16_t>(spot->x), static_cast<std::uint16_t>(spot->y)) == nullptr);
    const sim::MapCell& after =
        world.islands()[island].map.top(static_cast<std::uint32_t>(spot->x), static_cast<std::uint32_t>(spot->y));
    const assets::BuildingType* ground = buildings->by_sprite(after.sprite);
    REQUIRE(ground != nullptr);
    CHECK((ground->kind == 11 || ground->kind == 10));
    CHECK(after.city == index);
}

TEST_CASE("data: a stone wall is built over a palisade in a savegame") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    sim::World& world = saves.front().world;
    const sim::PlayerId human = sim::first_human_player(world);
    const sim::City* city = nullptr;
    for (const sim::City& c : world.cities()) {
        if (c.owner == human && c.warehouse_x) {
            city = &c;
            break;
        }
    }
    if (city == nullptr) {
        SKIP("the human player has no city");
    }
    const std::uint16_t island = city->island;
    const std::uint8_t index = city->index;
    // Three free tiles of open ground in a row in the city.
    const sim::IslandMap& map = world.islands()[island].map;
    std::optional<sim::TilePos> spot;
    for (std::uint32_t y = 0; y < map.height() && !spot; ++y) {
        for (std::uint32_t x = 0; x + 2 < map.width() && !spot; ++x) {
            bool free = true;
            for (std::uint32_t i = 0; i < 3; ++i) {
                const sim::MapCell& c = map.top(x + i, y);
                const assets::BuildingType* b = c.empty() ? nullptr : buildings->by_sprite(c.sprite);
                free = free && b != nullptr && b->kind == 11 && c.city == index;
            }
            if (free) {
                spot = sim::TilePos{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
            }
        }
    }
    REQUIRE(spot.has_value());
    {
        sim::City* c = world.city(island, index);
        for (const std::uint8_t ware : {buildings->materials().wood, buildings->materials().bricks}) {
            c->ensure_ware(ware);
            c->goods[ware] = std::max<std::uint32_t>(c->goods[ware], 20 * 32);
        }
        world.player(human)->money = std::max(world.players()[human].money, 1000);
        world.player(human)->infra_mask = 0xFFFFFFFFU;
    }
    const auto row = [&](std::uint32_t id) {
        sim::BuildOrder order;
        order.player = human;
        order.island = island;
        for (std::int32_t i = 0; i < 3; ++i) {
            order.items.push_back({id, spot->x + i, spot->y, 0, 0});
        }
        const sim::BuildPlan plan = sim::execute_build(world, *buildings, order, {"Neustadt"}, nullptr);
        for (const sim::PlannedBuild& item : plan.items) {
            if (!item.ok()) {
                std::printf("%u refused: %s\n", id, sim::refusal_name(item.refusal));
            }
        }
        return plan.placed;
    };
    REQUIRE(row(22521) == 3);  // the palisade
    CHECK(row(22501) == 3);    // the stone wall over it
    const auto* b = buildings->by_sprite(
        map.top(static_cast<std::uint32_t>(spot->x) + 1, static_cast<std::uint32_t>(spot->y)).sprite);
    REQUIRE(b != nullptr);
    CHECK(b->kreuz_base == buildings->by_id(22501)->kreuz_base);
}

TEST_CASE("data: a dragged road follows the tiles of its city") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    std::size_t paths = 0;
    for (const auto& save : saves) {
        const sim::World& world = save.world;
        for (const sim::City& city : world.cities()) {
            const sim::IslandMap& map = world.islands()[city.island].map;
            // From the first to the last road tile of the city, row by row.
            std::optional<sim::TilePos> first;
            std::optional<sim::TilePos> last;
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::MapCell& c = map.top(x, y);
                    const assets::BuildingType* b = c.empty() ? nullptr : buildings->by_sprite(c.sprite);
                    if (b != nullptr && b->kind == 1 && c.city == city.index) {
                        const sim::TilePos p{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
                        first = first.value_or(p);
                        last = p;
                    }
                }
            }
            if (!first || *first == *last) {
                continue;
            }
            const auto path = sim::drag_path(world, *buildings, city.island, sim::DragLine::Road, *first, *last, 0);
            REQUIRE(!path.empty());
            CHECK(path.back().x == last->x);
            CHECK(path.back().y == last->y);
            if (path.size() == 1) {
                continue;  // out of reach
            }
            ++paths;
            CHECK(path.size() <= 258);
            CHECK(path.front().x == first->x);
            CHECK(path.front().y == first->y);
            for (std::size_t i = 0; i < path.size(); ++i) {
                const sim::MapCell& c =
                    map.top(static_cast<std::uint32_t>(path[i].x), static_cast<std::uint32_t>(path[i].y));
                const assets::BuildingType* b = buildings->by_sprite(c.sprite);
                REQUIRE(b != nullptr);
                CHECK(c.city == city.index);
                CHECK(
                    sim::drag_passable(sim::DragLine::Road, b->kind, c.sprite == b->gfx + (b->size_x * b->size_y) / 2));
                if (i + 1 < path.size()) {
                    CHECK(std::abs(path[i + 1].x - path[i].x) + std::abs(path[i + 1].y - path[i].y) == 1);
                }
            }
        }
    }
    CHECK(paths > 0);
}

TEST_CASE("data: walls on the beach and the piers of Kontors come out as in the savegames") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    std::size_t walls = 0;
    std::size_t kontors = 0;
    for (auto& save : saves) {
        sim::World& world = save.world;
        for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
            sim::IslandMap& map = world.island(i).map;
            const auto type_at = [&](std::uint32_t x, std::uint32_t y) {
                const sim::MapCell& c = map.top(x, y);
                return c.empty() ? nullptr : buildings->by_sprite(c.sprite);
            };
            // Every beach wall, taken away and built again from its land wall, row by row.
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const assets::BuildingType* b = type_at(x, y);
                    if (b == nullptr || b->kind != 5 || b->land_record == assets::BuildingType::kNoRecord) {
                        continue;
                    }
                    const sim::MapCell saved = map.top(x, y);
                    const assets::BuildingType* land = buildings->by_record(b->land_record);
                    REQUIRE(land != nullptr);
                    map.restore_ground(x, y);
                    sim::place_tiles(world, *buildings, i, *land, static_cast<std::int32_t>(x),
                                     static_cast<std::int32_t>(y), 0);
                    const sim::MapCell& now = map.top(x, y);
                    CHECK(now.sprite == saved.sprite);
                    CHECK(now.rotation == saved.rotation);
                    ++walls;
                }
            }
            // Every Kontor at the coast, built again on bare ground: the piers in front of it.
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const assets::BuildingType* b = type_at(x, y);
                    if (b == nullptr || !b->coastal || b->destroyed) {
                        continue;
                    }
                    const auto at = sim::building_at(world, *buildings, i,
                                                     {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
                    if (!at || at->origin.x != static_cast<std::int32_t>(x) ||
                        at->origin.y != static_cast<std::int32_t>(y)) {
                        continue;
                    }
                    // The line in front of the long side.
                    std::vector<sim::TilePos> line;
                    for (std::int32_t k = 0; k < 3; ++k) {
                        const std::int32_t ox = at->origin.x;
                        const std::int32_t oy = at->origin.y;
                        const auto w = static_cast<std::int32_t>(at->width);
                        const auto h = static_cast<std::int32_t>(at->height);
                        switch (at->rotation) {
                            case 0:
                                line.push_back({ox + w, oy + k});
                                break;
                            case 1:
                                line.push_back({ox + k, oy + h});
                                break;
                            case 2:
                                line.push_back({ox - 1, oy + k});
                                break;
                            default:
                                line.push_back({ox + k, oy - 1});
                                break;
                        }
                    }
                    bool piers = true;
                    for (const sim::TilePos p : line) {
                        const auto* t = map.contains(p.x, p.y)
                                            ? type_at(static_cast<std::uint32_t>(p.x), static_cast<std::uint32_t>(p.y))
                                            : nullptr;
                        piers = piers && t != nullptr && t->kind == 30;
                    }
                    CHECK(piers);
                    if (!piers) {
                        continue;
                    }
                    for (std::uint32_t j = 0; j < at->height; ++j) {
                        for (std::uint32_t k = 0; k < at->width; ++k) {
                            map.restore_ground(static_cast<std::uint32_t>(at->origin.x) + k,
                                               static_cast<std::uint32_t>(at->origin.y) + j);
                        }
                    }
                    for (const sim::TilePos p : line) {
                        map.restore_ground(static_cast<std::uint32_t>(p.x), static_cast<std::uint32_t>(p.y));
                    }
                    sim::place_tiles(world, *buildings, i, *at->type, at->origin.x, at->origin.y, at->rotation);
                    for (const sim::TilePos p : line) {
                        const sim::MapCell& c =
                            map.top(static_cast<std::uint32_t>(p.x), static_cast<std::uint32_t>(p.y));
                        const auto* t = buildings->by_sprite(c.sprite);
                        REQUIRE(t != nullptr);
                        CHECK(t->kind == 30);
                        CHECK(c.rotation == at->rotation);
                    }
                    ++kontors;
                }
            }
        }
    }
    CHECK(walls > 0);
    CHECK(kontors > 0);
}

TEST_CASE("data: a drought withers the fields of an island and returns them") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    for (const auto& save : saves) {
        // What the original keeps with a game: the difficulty and the next drought time.
        CHECK(save.world.game_settings().difficulty <= 3);
        CHECK(save.world.next_drought_ms() >= 36'000ULL * 100);
    }
    sim::World& world = saves.front().world;
    // Fields of human and computer players (natives' plantations are not fertile, construction.md).
    const auto settler = [&](const sim::City* city) {
        const sim::Player* p = city != nullptr ? world.player(city->owner) : nullptr;
        return p != nullptr && (p->role == sim::PlayerRole::Human || p->role == sim::PlayerRole::Computer);
    };
    // The island with most of their fields and plantations.
    std::optional<std::uint16_t> island;
    std::size_t best = 0;
    for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
        const sim::IslandMap& map = world.islands()[i].map;
        std::size_t fields = 0;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const sim::MapCell& c = map.top(x, y);
                const assets::BuildingType* b = c.empty() ? nullptr : buildings->by_sprite(c.sprite);
                const assets::BuildingType* ripe =
                    b == nullptr ? nullptr : (b->prod_kind == sim::prod::kRipe ? b : sim::ripe_record(*buildings, *b));
                const sim::City* city = world.city(i, c.city);
                if (ripe != nullptr && sim::withered_record(*buildings, *ripe) != nullptr && settler(city)) {
                    ++fields;
                }
            }
        }
        if (fields > best) {
            best = fields;
            island = i;
        }
    }
    if (!island) {
        SKIP("no fields of human or computer players");
    }
    const auto withered_fields = [&] {
        std::vector<sim::TilePos> out;
        const sim::IslandMap& map = world.islands()[*island].map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const sim::MapCell& c = map.top(x, y);
                const assets::BuildingType* b = c.empty() ? nullptr : buildings->by_sprite(c.sprite);
                const sim::City* city = world.city(*island, c.city);
                if (b != nullptr && b->withered && settler(city)) {
                    out.push_back({static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
                }
            }
        }
        return out;
    };
    const std::size_t before = withered_fields().size();
    sim::Rules rules;
    rules.droughts = sim::Droughts::Off;  // only the drought of this test
    sim::Simulation simulation(world, *buildings, *figures, rules);
    sim::start_drought(world, *island, sim::kDroughtStrength, 600'000, nullptr);
    std::size_t most = 0;
    bool started = false;
    for (std::uint32_t t = 0; t < 12 * 60 * 1000; t += 100) {
        simulation.advance(100);
        for (const sim::IslandEvent& e : simulation.take_island_events()) {
            started |= e.kind == sim::IslandEvent::Kind::Withered;
        }
        if (t % 30'000 == 0) {
            const auto now = withered_fields();
            most = std::max(most, now.size());
            if (t >= 600'000) {
                // After the drought its strength halves every epoch; a harvest then withers by the
                // strength of the moment, whose fertility pattern is not that of the full drought.
                continue;
            }
            for (const sim::TilePos p : now) {
                const assets::BuildingType* b =
                    buildings->by_sprite(world.islands()[*island]
                                             .map.top(static_cast<std::uint32_t>(p.x), static_cast<std::uint32_t>(p.y))
                                             .sprite);
                const assets::BuildingType* ripe = sim::ripe_of_withered(*buildings, *b);
                REQUIRE(ripe != nullptr);
                CHECK(!sim::tile_fertile_at(world, *buildings, *island, ripe->ware, p.x, p.y, sim::kDroughtStrength));
            }
        }
    }
    CHECK(started);
    CHECK(most > before);
    // Three epochs after its end the drought is gone and the fields that bear grow again.
    for (std::uint32_t t = 0; t < 2 * 60 * 1000; t += 100) {
        simulation.advance(100);
    }
    CHECK(world.islands()[*island].drought.strength == 0);
    CHECK(withered_fields().size() <= before);
}

TEST_CASE("data: craters of scenarios and savegames are the volcano mountains") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    const assets::FigureType* lava = figures->get(sim::kFigureLava);
    REQUIRE(lava != nullptr);
    CHECK(lava->speed == sim::kLavaSpeed);
    std::vector<std::filesystem::path> files;
    for (const char* dir : {"szenes", "SAVEGAME"}) {
        const auto d = game.resolve(dir);
        if (!d || !std::filesystem::is_directory(*d)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(*d)) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (entry.is_regular_file() && (ext == ".szs" || ext == ".szm" || ext == ".gam")) {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end());
    REQUIRE(!files.empty());
    std::size_t craters = 0;
    for (const auto& f : files) {
        auto file = assets::ChunkFile::load(f);
        REQUIRE(file.ok());
        auto world = sim::load_world(*file, *buildings, game);
        REQUIRE(world.ok());
        const bool scenario = file->find("SZENE") != nullptr;
        for (const sim::WorldIsland& island : world->islands()) {
            // A scenario's list is searched anew when its SZENE chunk loads (Bündnispolitik stores
            // none). Other files keep the list they store: craters on volcano mountains, in the
            // order of the search, perhaps fewer (the tutorial Einführungsspiel0 stores none for
            // its volcano, so it never erupts). An ore deposit may lie on a volcano (Auf zu neuen
            // Ufern, and the big iron veins of the endless games).
            const std::vector<sim::Crater> found = sim::find_craters(island.map, *buildings);
            if (scenario) {
                CHECK(found.size() == island.craters.size());
            }
            REQUIRE(island.craters.size() <= found.size());
            for (std::size_t i = 0; i < island.craters.size(); ++i) {
                CHECK(found[i].x == island.craters[i].x);
                CHECK(found[i].y == island.craters[i].y);
            }
            craters += found.size();
        }
        CHECK(world->volcanoes().eruptions.empty());
        if (f.extension() == ".gam" || f.extension() == ".GAM") {
            // A savegame keeps the next eruption time: 70 minutes and up to 15.5 more from the start.
            CHECK(world->volcanoes().next_ms >= 42'000ULL * 100);
        }
    }
    CHECK(craters > 0);
}

TEST_CASE("data: a volcano erupts in a savegame and its lava hits what it should") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    sim::World& world = saves.back().world;
    std::optional<sim::CraterRef> crater;
    for (std::uint16_t i = 0; i < world.islands().size() && !crater; ++i) {
        for (const sim::Crater& c : world.islands()[i].craters) {
            crater = sim::CraterRef{i, c.x, c.y};
        }
    }
    if (!crater) {
        SKIP("no volcano in the savegame");
    }
    sim::Simulation simulation(world, *buildings, *figures, sim::Rules::open1602());
    REQUIRE(sim::erupt(world, *crater, 4321, true, nullptr));
    simulation.set_speed(1);
    for (int i = 0; i < 400; ++i) {
        simulation.advance(100);
    }
    CHECK(world.volcanoes().eruptions.empty());
    CHECK(world.volcanoes().lava.empty());
    CHECK(world.islands()[crater->island].volcano_erupted);
    for (const sim::VolcanoEvent& e : simulation.take_volcano_events()) {
        const sim::MapCell& c = world.islands()[e.island].map.top(e.x, e.y);
        const assets::BuildingType* b = c.empty() ? nullptr : buildings->by_sprite(c.sprite);
        if (e.kind == sim::VolcanoEvent::Kind::Fire) {
            // Still standing (or burned to ruins by now), and of a kind that burns.
            CHECK(b != nullptr);
        } else if (e.kind == sim::VolcanoEvent::Kind::Destroyed) {
            CHECK((b == nullptr || b->kind != 35));  // no Kontor stays
        }
    }
}

TEST_CASE("data: a savegame written back without changes is the original file") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    for (const LoadedSave& save : saves) {
        const assets::ChunkFile& source = *save.world.source();
        auto chunks = sim::savegame_chunks(save.world, *buildings);
        REQUIRE(chunks.ok());
        // The original's chunks; a savegame of Open1602 has its own chunk after WIFF.
        std::size_t original_chunks = source.chunks().size();
        if (source.chunks().back().name == "O1602") {
            --original_chunks;
        }
        REQUIRE(chunks->size() + 1 == original_chunks);
        for (std::size_t k = 0; k < chunks->size(); ++k) {
            const assets::Chunk& c = source.chunks()[k];
            const auto original = source.payload(c);
            const sim::SavedChunk& out = (*chunks)[k];
            CHECK(out.name == c.name);
            if (c.name == "ROHWACHS2") {
                // Open1602 gives every growing tile an entry (S-020): the original's entries come
                // first, the added ones follow.
                REQUIRE(out.data.size() >= original.size());
                CHECK(std::equal(original.begin(), original.end(), out.data.begin()));
                continue;
            }
            const bool same =
                out.data.size() == original.size() && std::equal(original.begin(), original.end(), out.data.begin());
            if (!same) {
                std::fprintf(stderr, "  %s: chunk %zu (%s) differs\n", save.name.c_str(), k, c.name.c_str());
            }
            CHECK(same);
        }
        CHECK(source.chunks()[original_chunks - 1].name == "WIFF");
        // WIFF is the sum of the payload words of the original's own chunks.
        std::uint32_t sum = 0;
        for (std::size_t k = 0; k + 1 < original_chunks; ++k) {
            sum += assets::payload_checksum(source.payload(source.chunks()[k]));
        }
        const auto wiff = source.payload(source.chunks()[original_chunks - 1]);
        CHECK(wiff.size() == 4);
        CHECK((static_cast<std::uint32_t>(wiff[0]) | (static_cast<std::uint32_t>(wiff[1]) << 8) |
               (static_cast<std::uint32_t>(wiff[2]) << 16) | (static_cast<std::uint32_t>(wiff[3]) << 24)) == sum);
    }
}

TEST_CASE("data: a game played on in Open1602 saves and loads to the same state") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    sim::World& world = saves.back().world;
    {
        sim::Simulation simulation(world, *buildings, *figures);
        simulation.set_speed(8);
        for (int i = 0; i < 1500; ++i) {  // 20 minutes of game time
            simulation.advance(100);
        }
    }
    auto first = sim::write_savegame(world, *buildings);
    REQUIRE(first.ok());
    auto file = assets::ChunkFile::from_bytes(*first);
    REQUIRE(file.ok());
    std::vector<std::string> warnings;
    auto loaded = sim::load_world(*file, *buildings, game, &warnings);
    REQUIRE(loaded.ok());
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "  warning: %s\n", w.c_str());
    }
    CHECK(warnings.empty());
    // What was written is read back: writing again gives the same file.
    auto second = sim::write_savegame(*loaded, *buildings);
    REQUIRE(second.ok());
    if (*first != *second) {
        auto other = assets::ChunkFile::from_bytes(*second);
        REQUIRE(other.ok());
        for (std::size_t k = 0; k < std::min(file->chunks().size(), other->chunks().size()); ++k) {
            const auto a = file->payload(file->chunks()[k]);
            const auto b = other->payload(other->chunks()[k]);
            if (a.size() != b.size() || !std::equal(a.begin(), a.end(), b.begin())) {
                std::size_t d = 0;
                while (d < a.size() && d < b.size() && a[d] == b[d]) {
                    ++d;
                }
                std::fprintf(stderr, "  chunk %zu %s: %zu / %zu bytes, first difference at %zu\n", k,
                             file->chunks()[k].name.c_str(), a.size(), b.size(), d);
            }
        }
    }
    CHECK(*first == *second);
    // And the state matches.
    CHECK(loaded->time_ms() / 100 == world.time_ms() / 100);
    REQUIRE(loaded->islands().size() == world.islands().size());
    // Every tile comes back; the original saves no ripening of grass and fish outside the cities,
    // which therefore shows the ground's stage again.
    std::size_t tiles = 0;
    std::size_t grass = 0;
    for (std::size_t i = 0; i < world.islands().size(); ++i) {
        const sim::IslandMap& a = world.islands()[i].map;
        const sim::IslandMap& b = loaded->islands()[i].map;
        for (std::uint32_t y = 0; y < a.height(); ++y) {
            for (std::uint32_t x = 0; x < a.width(); ++x) {
                const sim::MapCell& p = a.top(x, y);
                const sim::MapCell& q = b.top(x, y);
                ++tiles;
                if (p.sprite == q.sprite && p.rotation == q.rotation && p.city == q.city && p.owner == q.owner) {
                    continue;
                }
                const auto ware = [&](const sim::MapCell& c) -> std::uint8_t {
                    const assets::BuildingType* t = c.empty() ? nullptr : buildings->by_sprite(c.sprite);
                    if (t != nullptr && t->prod_kind == sim::prod::kGrowing) {
                        t = buildings->by_record(t->record + 1);
                    }
                    return t != nullptr ? t->ware : 0;
                };
                const bool same_crop = p.city == sim::MapCell::kNoCity && q.city == sim::MapCell::kNoCity &&
                                       ware(p) == ware(q) && (ware(p) == 52 || ware(p) == 57);
                if (same_crop) {
                    ++grass;
                } else {
                    std::fprintf(stderr, "  island %zu tile %u,%u differs\n", i, x, y);
                    CHECK(same_crop);
                }
            }
        }
    }
    CHECK(tiles > 0);
    std::fprintf(stderr, "  %zu tiles of grass or fish show the ground's stage again\n", grass);
    REQUIRE(loaded->cities().size() == world.cities().size());
    for (std::size_t c = 0; c < world.cities().size(); ++c) {
        const sim::City& a = world.cities()[c];
        const sim::City* b = loaded->city(a.island, a.index);
        REQUIRE(b != nullptr);
        CHECK(b->inhabitants == a.inhabitants);
        for (std::uint8_t w = 2; w <= 24; ++w) {
            CHECK(b->stock(w) == a.stock(w));
        }
    }
    for (std::size_t p = 0; p < world.players().size(); ++p) {
        CHECK(loaded->players()[p].money == world.players()[p].money);
    }
    CHECK(loaded->houses().size() == world.houses().size());
    std::size_t productions = 0;
    for (const sim::Production& p : world.productions()) {
        productions += p.used ? 1U : 0U;
        if (p.used) {
            const sim::Production* q = loaded->production(p.island, p.x, p.y);
            REQUIRE(q != nullptr);
            CHECK(q->stock == p.stock);
            CHECK(q->countdown == p.countdown);
        }
    }
    CHECK(productions > 0);
}

TEST_CASE("data: a game saved in Open1602 continues exactly as if it had not stopped") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    sim::World& world = saves.back().world;
    sim::Simulation a(world, *buildings, *figures);
    a.set_speed(8);
    for (int i = 0; i < 130; ++i) {
        a.advance(100);
    }
    // Save in the middle of things: lava in the air, a fire with the fire brigade on its way.
    for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
        if (!world.islands()[i].craters.empty()) {
            const sim::Crater& c = world.islands()[i].craters.front();
            REQUIRE(sim::erupt(world, sim::CraterRef{i, c.x, c.y}, 999, true, nullptr));
            break;
        }
    }
    for (const sim::House& h : world.houses()) {
        if (world.city(h.island, h.city) != nullptr && world.city(h.island, h.city)->owner == 0) {
            REQUIRE(sim::start_disaster(world, *buildings, h.island, {h.x, h.y}, sim::Disaster::Kind::Fire,
                                        sim::kFireDuration, nullptr));
            break;
        }
    }
    for (int i = 0; i < 20; ++i) {
        a.advance(100);
    }
    CHECK(!world.volcanoes().lava.empty());
    CHECK(!world.disasters().empty());
    auto saved = sim::write_savegame(world, *buildings, &a);
    REQUIRE(saved.ok());
    auto file = assets::ChunkFile::from_bytes(*saved);
    REQUIRE(file.ok());
    REQUIRE(file->chunks().back().name == "O1602");
    std::vector<std::string> warnings;
    auto loaded = sim::load_world(*file, *buildings, game, &warnings);
    REQUIRE(loaded.ok());
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "  warning: %s\n", w.c_str());
    }
    CHECK(warnings.empty());
    sim::Simulation b(*loaded, *buildings, *figures);
    b.set_speed(8);
    const auto report = [](const core::Bytes& one, const core::Bytes& two) {
        auto fa = assets::ChunkFile::from_bytes(one);
        auto fb = assets::ChunkFile::from_bytes(two);
        for (std::size_t k = 0; k < std::min(fa->chunks().size(), fb->chunks().size()); ++k) {
            const auto x = fa->payload(fa->chunks()[k]);
            const auto y = fb->payload(fb->chunks()[k]);
            if (x.size() != y.size() || !std::equal(x.begin(), x.end(), y.begin())) {
                std::size_t d = 0;
                while (d < x.size() && d < y.size() && x[d] == y[d]) {
                    ++d;
                }
                std::fprintf(stderr, "  chunk %zu %s: %zu / %zu bytes, first difference at %zu\n", k,
                             fa->chunks()[k].name.c_str(), x.size(), y.size(), d);
                if (fa->chunks()[k].name == "O1602") {
                    std::size_t o = 20;  // magic, version, flags, fingerprint
                    while (o + 12 <= x.size()) {
                        const std::uint32_t size =
                            static_cast<std::uint32_t>(x[o + 8]) | static_cast<std::uint32_t>(x[o + 9]) << 8U |
                            static_cast<std::uint32_t>(x[o + 10]) << 16U | static_cast<std::uint32_t>(x[o + 11]) << 24U;
                        if (d >= o && d < o + 12 + size) {
                            std::fprintf(stderr, "  in section %c%c%c%c at %zu\n", x[o], x[o + 1], x[o + 2], x[o + 3],
                                         d - o - 12);
                        }
                        o += 12 + size;
                    }
                }
            }
        }
    };
    // Right after loading the state is the same ...
    auto again = sim::write_savegame(*loaded, *buildings, &b);
    REQUIRE(again.ok());
    if (*again != *saved) {
        report(*saved, *again);
    }
    CHECK(*again == *saved);
    // ... and five minutes later too.
    for (int i = 0; i < 375; ++i) {
        a.advance(100);
        b.advance(100);
    }
    auto first = sim::write_savegame(world, *buildings, &a);
    auto second = sim::write_savegame(*loaded, *buildings, &b);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    if (*first != *second) {
        auto fa = assets::ChunkFile::from_bytes(*first);
        auto fb = assets::ChunkFile::from_bytes(*second);
        for (std::size_t k = 0; k < std::min(fa->chunks().size(), fb->chunks().size()); ++k) {
            const auto x = fa->payload(fa->chunks()[k]);
            const auto y = fb->payload(fb->chunks()[k]);
            if (x.size() != y.size() || !std::equal(x.begin(), x.end(), y.begin())) {
                std::size_t d = 0;
                while (d < x.size() && d < y.size() && x[d] == y[d]) {
                    ++d;
                }
                std::fprintf(stderr, "  chunk %zu %s: %zu / %zu bytes, first difference at %zu\n", k,
                             fa->chunks()[k].name.c_str(), x.size(), y.size(), d);
                if (fa->chunks()[k].name == "O1602") {
                    std::size_t o = 20;  // magic, version, flags, fingerprint
                    while (o + 12 <= x.size()) {
                        const std::uint32_t size =
                            static_cast<std::uint32_t>(x[o + 8]) | static_cast<std::uint32_t>(x[o + 9]) << 8U |
                            static_cast<std::uint32_t>(x[o + 10]) << 16U | static_cast<std::uint32_t>(x[o + 11]) << 24U;
                        if (d >= o && d < o + 12 + size) {
                            std::fprintf(stderr, "  in section %c%c%c%c at %zu\n", x[o], x[o + 1], x[o + 2], x[o + 3],
                                         d - o - 12);
                        }
                        o += 12 + size;
                    }
                }
                if (fa->chunks()[k].name == "INSELHAUS" && d / 8 * 8 + 8 <= x.size()) {
                    const std::size_t o = d / 8 * 8;
                    std::fprintf(stderr, "    id %u at %u,%u bits %08x / id %u bits %08x\n", x[o] | x[o + 1] << 8,
                                 x[o + 2], x[o + 3], x[o + 4] | x[o + 5] << 8 | x[o + 6] << 16 | x[o + 7] << 24,
                                 y[o] | y[o + 1] << 8, y[o + 4] | y[o + 5] << 8 | y[o + 6] << 16 | y[o + 7] << 24);
                }
            }
        }
    }
    CHECK(*first == *second);
}

TEST_CASE("data: a game in Open1602's own savegame continues exactly as if it had not stopped") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    sim::World& world = saves.back().world;
    sim::Simulation a(world, *buildings, *figures);
    a.set_speed(8);
    for (int i = 0; i < 130; ++i) {
        a.advance(100);
    }
    // Save in the middle of things: lava in the air, a fire with the fire brigade on its way.
    for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
        if (!world.islands()[i].craters.empty()) {
            const sim::Crater& c = world.islands()[i].craters.front();
            REQUIRE(sim::erupt(world, sim::CraterRef{i, c.x, c.y}, 999, true, nullptr));
            break;
        }
    }
    for (const sim::House& h : world.houses()) {
        if (world.city(h.island, h.city) != nullptr && world.city(h.island, h.city)->owner == 0) {
            REQUIRE(sim::start_disaster(world, *buildings, h.island, {h.x, h.y}, sim::Disaster::Kind::Fire,
                                        sim::kFireDuration, nullptr));
            break;
        }
    }
    for (int i = 0; i < 20; ++i) {
        a.advance(100);
    }
    auto saved = sim::write_game(world, *buildings, &a);
    REQUIRE(saved.ok());
    auto legacy = sim::write_savegame(world, *buildings, &a);
    REQUIRE(legacy.ok());
    std::fprintf(stderr, "  own savegame %zu bytes, .GAM with chunk %zu bytes\n", saved->size(), legacy->size());
    CHECK(saved->size() * 3 < legacy->size());
    std::vector<std::string> warnings;
    auto loaded = sim::read_game(*saved, *buildings, &warnings);
    REQUIRE(loaded.ok());
    CHECK(warnings.empty());
    sim::Simulation b(*loaded, *buildings, *figures);
    b.set_speed(8);
    // The format is canonical: right after loading the bytes are the same ...
    auto again = sim::write_game(*loaded, *buildings, &b);
    REQUIRE(again.ok());
    CHECK(*again == *saved);
    // ... and five minutes later too.
    for (int i = 0; i < 375; ++i) {
        a.advance(100);
        b.advance(100);
    }
    auto first = sim::write_game(world, *buildings, &a);
    auto second = sim::write_game(*loaded, *buildings, &b);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(*first == *second);
    // An Open1602 savegame can still be exported to the original's format while it fits.
    auto exported = sim::write_savegame(*loaded, *buildings, &b);
    CHECK(exported.ok());
}

TEST_CASE("data: the city window sets the taxes, the name and the material lock") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    sim::World& world = saves.front().world;
    sim::Simulation simulation(world, *buildings, *figures);
    const sim::City* found = nullptr;
    for (const sim::City& c : world.cities()) {
        if (c.owner == 0 && c.inhabitants.size() > 1 && c.inhabitants[1] > 0) {
            found = &c;
        }
    }
    REQUIRE(found != nullptr);
    const std::uint16_t island = found->island;
    const std::uint8_t index = found->index;
    const std::uint8_t before = found->satisfaction[1];
    const auto order = [&](sim::CityOrder::Op op) {
        sim::CityOrder o;
        o.player = 0;
        o.island = island;
        o.city = index;
        o.op = op;
        return o;
    };
    sim::CityOrder tax = order(sim::CityOrder::Op::TaxRate);
    tax.resident_class = 1;
    tax.rate = 192;
    CHECK(simulation.execute(tax).accepted);
    const sim::City& city = *world.city(island, index);
    CHECK(city.tax_rate[1] == 192);
    CHECK(city.satisfaction[1] <= before);  // measured against the new tax at once
    tax.rate = 200;                         // beyond the slider
    CHECK(!simulation.execute(tax).accepted);
    tax.rate = 64;
    tax.player = 1;  // not his city
    CHECK(!simulation.execute(tax).accepted);
    sim::CityOrder rename = order(sim::CityOrder::Op::Rename);
    rename.name = "Neu-Lemonhall";
    CHECK(simulation.execute(rename).accepted);
    CHECK(city.name == "Neu-Lemonhall");
    sim::CityOrder lock = order(sim::CityOrder::Op::MaterialLock);
    CHECK(simulation.execute(lock).accepted);  // off first, so switching it on starts the clock
    lock.on = true;
    CHECK(simulation.execute(lock).accepted);
    CHECK(city.material_lock);
    CHECK(city.material_reminder_ms == world.time_ms());  // the people's clock starts
}

TEST_CASE("data: Open1602's chunk is left out when the original's chunks changed or it is damaged") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    sim::World& world = saves.back().world;
    sim::Rules rules;
    rules.droughts = sim::Droughts::Frequent;
    sim::Simulation a(world, *buildings, *figures, rules);
    a.set_speed(8);
    for (int i = 0; i < 40; ++i) {
        a.advance(100);
    }
    auto saved = sim::write_savegame(world, *buildings, &a);
    REQUIRE(saved.ok());
    const auto load = [&](const core::Bytes& bytes, std::vector<std::string>& warnings) {
        auto file = assets::ChunkFile::from_bytes(bytes);
        REQUIRE(file.ok());
        auto w = sim::load_world(*file, *buildings, game, &warnings);
        REQUIRE(w.ok());
        return std::move(w).value();
    };
    {
        std::vector<std::string> warnings;
        const sim::World w = load(*saved, warnings);
        CHECK(warnings.empty());
        REQUIRE(w.saved_rules().has_value());
        CHECK(w.saved_rules()->droughts == sim::Droughts::Frequent);
        CHECK(!w.simulation_state().empty());
    }
    auto file = assets::ChunkFile::from_bytes(*saved);
    REQUIRE(file.ok());
    const assets::Chunk* timers = file->find("TIMERS");
    const assets::Chunk* own = file->find("O1602");
    REQUIRE(timers != nullptr);
    REQUIRE(own != nullptr);
    {
        // Another tool changed a byte of the original's chunks (one the original ignores).
        core::Bytes changed = *saved;
        changed[timers->offset + 0x10] ^= 0x5A;
        std::vector<std::string> warnings;
        const sim::World w = load(changed, warnings);
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].find("changed") != std::string::npos);
        CHECK(!w.saved_rules().has_value());
        CHECK(w.simulation_state().empty());
    }
    {
        // A damaged chunk: cut off in the middle.
        core::Bytes cut(saved->begin(), saved->begin() + static_cast<std::ptrdiff_t>(own->offset + own->size / 2));
        const auto size = static_cast<std::uint32_t>(own->size / 2);
        for (std::size_t k = 0; k < 4; ++k) {
            cut[own->offset - 4 + k] = static_cast<std::uint8_t>(size >> (8 * k));
        }
        std::vector<std::string> warnings;
        const sim::World w = load(cut, warnings);
        REQUIRE(warnings.size() == 1);
        CHECK(!w.saved_rules().has_value());
        CHECK(w.simulation_state().empty());
        CHECK(w.time_ms() == world.time_ms() / 100 * 100);  // the original's chunks stand
    }
}

TEST_CASE("data: townsfolk, sea creatures and jugglers leave the game untouched") {
    const auto game = require_game();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto saves = load_savegames(game, *buildings);
    if (saves.empty()) {
        SKIP("no savegame");
    }
    sim::World& world = saves.back().world;
    auto before = sim::write_savegame(world, *buildings);
    REQUIRE(before.ok());
    render::Ambient ambient(42);
    render::AmbientView view;
    view.islands.assign(world.islands().size(), true);
    const std::uint64_t start_ms = world.time_ms();
    ambient.tick(world, *buildings, *figures);
    std::set<std::uint16_t> kinds;
    std::size_t jugglers = 0;
    for (int t = 0; t < 12'000; ++t) {  // 10 minutes
        world.set_time_ms(world.time_ms() + sim::Simulation::kTickMs);
        ambient.tick(world, *buildings, *figures);
        if (t % 2 == 0) {
            continue;
        }
        ambient.frame(world, *buildings, *figures, view);
        for (const render::FigureInstance& f : ambient.walkers()) {
            kinds.insert(f.figure);
            // Townsfolk are seen on roads, squares, bridges and piers only.
            const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(f.x), static_cast<std::uint32_t>(f.y));
            REQUIRE(c.island != sim::World::kSea);
        }
        for (const render::EffectFigure& e : ambient.effects()) {
            jugglers += e.sheet == assets::FigureSheet::Gaukler ? 1U : 0U;
            if (e.sheet == assets::FigureSheet::Fische) {
                kinds.insert(e.figure);
            }
        }
    }
    world.set_time_ms(start_ms);
    CHECK(ambient.stats().walkers > 0);
    CHECK(ambient.stats().creatures > 0);
    // Jugglers where a city has more than 59 citizens and better (and a market).
    bool big = false;
    for (const sim::City& c : world.cities()) {
        std::uint32_t n = 0;
        for (std::size_t k = 2; k < c.inhabitants.size(); ++k) {
            n += c.inhabitants[k];
        }
        big = big || n > 59;
    }
    CHECK((jugglers > 0) == big);
    for (const std::uint16_t k : kinds) {
        CHECK(((k >= 93 && k <= 103)));
        CHECK(figures->get(k) != nullptr);
    }
    // Nothing of it reaches the game.
    auto after = sim::write_savegame(world, *buildings);
    REQUIRE(after.ok());
    CHECK(*before == *after);
}
