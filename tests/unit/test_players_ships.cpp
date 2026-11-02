// SPDX-License-Identifier: GPL-3.0-or-later
// Players (PLAYER4), ships (SHIP4), figure table and their drawing (docs/formats/gam.md).

#include <algorithm>
#include <cstring>
#include <optional>
#include <set>
#include <tuple>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/render/animation_clock.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

void put_u16(std::vector<std::uint8_t>& out, std::size_t at, std::uint16_t v) {
    out[at] = static_cast<std::uint8_t>(v);
    out[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

void put_chunk(std::vector<std::uint8_t>& file, const char* name, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> header(16, 0);
    std::memcpy(header.data(), name, std::strlen(name));
    file.insert(file.end(), header.begin(), header.end());
    put_u32(file, static_cast<std::uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
}

/// BSH with solid sprites of the given sizes (palette index 1).
assets::SpriteSheet make_sheet(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& sizes) {
    std::vector<std::vector<std::uint8_t>> records;
    for (const auto& [w, h] : sizes) {
        std::vector<std::uint8_t> rle;
        for (std::uint32_t y = 0; y < h; ++y) {
            rle.push_back(0);
            rle.push_back(static_cast<std::uint8_t>(w));
            rle.insert(rle.end(), w, 1);
            rle.push_back(y + 1 == h ? 0xFF : 0xFE);
        }
        std::vector<std::uint8_t> rec;
        put_u32(rec, w);
        put_u32(rec, h);
        put_u32(rec, 1);
        put_u32(rec, static_cast<std::uint32_t>((16 + rle.size() + 3) & ~std::size_t{3}));
        rec.insert(rec.end(), rle.begin(), rle.end());
        rec.resize((rec.size() + 3) & ~std::size_t{3}, 0);
        records.push_back(std::move(rec));
    }
    std::vector<std::uint8_t> payload;
    auto offset = static_cast<std::uint32_t>(4 * records.size());
    for (const auto& rec : records) {
        put_u32(payload, offset);
        offset += static_cast<std::uint32_t>(rec.size());
    }
    for (const auto& rec : records) {
        payload.insert(payload.end(), rec.begin(), rec.end());
    }
    std::vector<std::uint8_t> file;
    put_chunk(file, "BSH", payload);
    auto chunks = assets::ChunkFile::from_bytes(file);
    auto sheet = assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    return std::move(sheet).value();
}

assets::Palette make_palette() {
    std::vector<std::uint8_t> payload;
    for (int i = 0; i < 256; ++i) {
        payload.insert(payload.end(), {static_cast<std::uint8_t>(i), 0, 0, 0});
    }
    std::vector<std::uint8_t> file;
    put_chunk(file, "COL", payload);
    auto chunks = assets::ChunkFile::from_bytes(file);
    return std::move(assets::Palette::from_chunk_file(*chunks)).value();
}

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Posoffs: -19, 34\n  Fahnoffs: 0, 0, 2.0\n"
        "  Nummer: 106\n  Gfx: 8\n  Blocknr: 2\n  Rotate: 8\n  Posoffs: 0, 0\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("scenario: PLAYER4 and SHIP4 records") {
    std::vector<std::uint8_t> players(2 * 0x430, 0);
    put_u16(players, 0, 10000);
    players[4] = 0x00;  // human
    players[5] = 0;
    players[6] = 1;
    players[7] = 1;  // blue
    std::memcpy(&players[0x3C0], "Anna", 4);
    // A scenario's switches with their thresholds (as „Dunkle Wolken am Horizont“ has them).
    players[0x1C] = 0x88;
    players[0x1D] = 0x02;
    put_u16(players, 0x20, 500);
    put_u16(players, 0x22, 2200);
    players[0x24] = 97;
    players[0x25] = 96;
    players[0x430 + 0] = 0x50;  // 50000 = 0xC350
    players[0x430 + 1] = 0xC3;
    players[0x430 + 4] = 0x0E;  // pirate
    players[0x430 + 5] = 1;
    players[0x430 + 7] = 4;

    std::vector<std::uint8_t> ships(0x1B4, 0);
    std::memcpy(ships.data(), "Patria", 6);
    put_u16(ships, 0x1C, 249);
    put_u16(ships, 0x1E, 156);
    put_u16(ships, 0x48, 21);
    ships[0x4B] = 0;
    ships[0x4D] = 0xFF;  // at sea
    ships[0x50] = 3;

    std::vector<std::uint8_t> file;
    put_chunk(file, "PLAYER4", players);
    put_chunk(file, "SHIP4", ships);
    auto chunks = assets::ChunkFile::from_bytes(file);
    REQUIRE(chunks.ok());

    auto p = assets::load_players(*chunks);
    REQUIRE(p.ok());
    REQUIRE(p->size() == 2);
    CHECK((*p)[0].money == 10000);
    CHECK((*p)[0].kind == assets::PlayerKind::Human);
    CHECK((*p)[0].colour == assets::PlayerColour::Blue);
    CHECK((*p)[0].name == "Anna");
    CHECK((*p)[0].scenario_flags == 0x288);
    CHECK((*p)[0].rival_inhabitants == 500);
    CHECK((*p)[0].own_inhabitants == 2200);
    CHECK((*p)[0].own_development == 97);
    CHECK((*p)[0].rival_development == 96);
    CHECK((*p)[1].scenario_flags == 0);
    CHECK((*p)[1].money == 50000);
    CHECK((*p)[1].kind == assets::PlayerKind::Pirate);
    CHECK((*p)[1].slot == 1);
    CHECK((*p)[1].colour == assets::PlayerColour::Pirate);

    auto s = assets::load_ships(*chunks);
    REQUIRE(s.ok());
    REQUIRE(s->size() == 1);
    CHECK((*s)[0].name == "Patria");
    CHECK((*s)[0].x == 249);
    CHECK((*s)[0].y == 156);
    CHECK((*s)[0].figure == 21);
    CHECK((*s)[0].owner == 0);
    CHECK((*s)[0].direction == 3);
    CHECK((*s)[0].island == 0xFF);

    // Truncated records are an error, missing chunks just mean "none".
    std::vector<std::uint8_t> bad;
    put_chunk(bad, "PLAYER4", std::vector<std::uint8_t>(100, 0));
    auto bad_chunks = assets::ChunkFile::from_bytes(bad);
    CHECK(!assets::load_players(*bad_chunks).ok());
    CHECK(assets::load_ships(*bad_chunks).ok());
    CHECK(assets::load_ships(*bad_chunks)->empty());
}

TEST_CASE("figure table: fields and flag lookup by player colour") {
    const auto figures = make_figures();
    const auto* ship = figures.get(21);
    REQUIRE(ship != nullptr);
    CHECK(ship->sheet == assets::FigureSheet::Ship);
    CHECK(ship->rotate == 1);
    CHECK(ship->posoffs[0] == -19);
    CHECK(ship->fahnoffs[2] == 2.0);
    CHECK(figures.get(22) == nullptr);
    CHECK(figures.flag_for_colour(0) == figures.get(106));
    CHECK(figures.flag_for_colour(1) == nullptr);  // FAHNE2 not defined in this test table
    CHECK(figures.flag_for_colour(5) == nullptr);  // natives: no flag
}

TEST_CASE("sprite atlas: several sheets get consecutive index ranges") {
    const auto a = make_sheet({{4, 2}, {3, 3}});
    const auto b = make_sheet({{5, 1}});
    const auto palette = make_palette();
    const assets::SpriteSheet* sheets[] = {&a, &b};
    auto atlas = render::SpriteAtlas::build(sheets, palette, 64);
    REQUIRE(atlas.ok());
    CHECK(atlas->sprite_count() == 3);
    CHECK(atlas->sheet_base(0) == 0);
    CHECK(atlas->sheet_base(1) == 2);
    CHECK(atlas->rect(2).width == 5);
    CHECK(atlas->rect(2).height == 1);
}

TEST_CASE("world renderer: ship hull, owner flag and row") {
    // Sheets: terrain (1 sprite), ships: 8 hull directions 20x10, 8 flag frames 6x4.
    const auto terrain = make_sheet({{4, 2}});
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ship_sizes(8, {20, 10});
    ship_sizes.insert(ship_sizes.end(), 8, {6, 4});
    const auto ships = make_sheet(ship_sizes);
    const auto palette = make_palette();
    const assets::SpriteSheet* sheets[] = {&terrain, &ships};
    auto atlas = render::SpriteAtlas::build(sheets, palette, 256);
    REQUIRE(atlas.ok());
    const auto figures = make_figures();

    sim::World world;
    world.set_players({sim::Player{sim::PlayerRole::Human, 0, 0, ""},
                       sim::Player{sim::PlayerRole::Neutral, 5, 0, "", sim::faction::kNatives}});
    const sim::Ship ship = sim::make_ship("Patria", 10, 20, 21, 0, 3);

    // Original flag images for this test (the tinted generic flag is covered in test_figures).
    const auto banners = render::BannerTable::original(&figures, render::BannerOptions{true});
    render::WorldLayers layers;
    layers.figures = &figures;
    layers.atlas = &*atlas;
    layers.ship_base = atlas->sheet_base(1);
    layers.banners = &banners;
    const render::WorldView view{render::Zoom::Large, 0};

    render::ShipSprites s;
    REQUIRE(render::ship_sprites(ship, world, view, layers, 0, s));
    // Tile centre: x = (10.5 - 20.5 + 350) * 32, y = 31 * 16 = 496; the figure is kept at
    // Y = y + Posoffs[1] = 530 (also its depth). Hull: sprite Gfx + direction, centred, bottom at
    // Y + Posoffs[0], one pixel up for the tile rows of Open1602 starting one lower.
    CHECK(s.hull.sprite == layers.ship_base + 3);
    CHECK(s.hull.x == 340 * 32 - 10);
    CHECK(s.hull.y == 530 - 19 - 1);
    CHECK(s.row == (530 - 1) / 16);
    // Flag: FAHNE1 (red), frame 0, Fahnoffs height 2.0 * 32 above Y, hanging down from there.
    REQUIRE(s.has_flag);
    CHECK(s.flag.sprite == layers.ship_base + 8);
    CHECK(s.flag.x == 340 * 32 - 3);
    CHECK(s.flag.y == 530 - 64 + 4 - 1);
    REQUIRE(render::ship_sprites(ship, world, view, layers, 200, s));
    CHECK(s.flag.sprite == layers.ship_base + 10);  // 90 ms per flag frame

    // View rotation adds two direction steps per quarter turn.
    REQUIRE(render::ship_sprites(ship, world, render::WorldView{render::Zoom::Large, 1}, layers, 0, s));
    CHECK(s.hull.sprite == layers.ship_base + 5);

    // Natives have no flag; unknown figures are not drawn.
    const sim::Ship native = sim::make_ship("Kanu", 10, 20, 21, 1, 0);
    REQUIRE(render::ship_sprites(native, world, view, layers, 0, s));
    CHECK(!s.has_flag);
    const sim::Ship unknown = sim::make_ship("?", 10, 20, 99, 0, 0);
    CHECK(!render::ship_sprites(unknown, world, view, layers, 0, s));
}

TEST_CASE("world renderer: ownership view adds diamonds in the owner's colour") {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse("Objekt: HAUS\n@Nummer: 0\nId: 20100\nGfx: 0\nSize: 1, 1\nEndObj\n", types);
    auto buildings = assets::BuildingTable::from_cod(doc);
    REQUIRE(buildings.ok());

    sim::World world;
    world.set_players({sim::Player{}, sim::Player{sim::PlayerRole::Computer, 2, 0, ""}});
    sim::WorldIsland island{assets::IslandHeader{}, sim::IslandMap(2, 1), 5, 5};
    REQUIRE(island.map.place(*buildings->by_id(20100), 0, 0, 0, 0, 1).ok());  // owned by slot 1
    REQUIRE(island.map.place(*buildings->by_id(20100), 1, 0, 0, 0).ok());     // nobody
    REQUIRE(world.add_island(std::move(island)).ok());

    const render::AnimationClock clock(*buildings);
    const auto banners = render::BannerTable::original();
    render::WorldLayers layers;
    layers.extent = render::SpriteExtent{16, 8};
    layers.banners = &banners;
    layers.owner_tint = true;
    const render::WorldView view{render::Zoom::Small, 0};
    render::Viewport all;
    render::world_pixel_size(view, all.width, all.height);
    std::vector<render::DrawCommand> commands;
    render::build_world_draw_list(world, *buildings, clock, view, all, layers, commands);
    const auto diamonds = std::count_if(commands.begin(), commands.end(), [](const render::DrawCommand& c) {
        return c.sprite == render::DrawCommand::kOverlayDiamond;
    });
    CHECK(diamonds == 1);
    const auto it = std::find_if(commands.begin(), commands.end(), [](const render::DrawCommand& c) {
        return c.sprite == render::DrawCommand::kOverlayDiamond;
    });
    REQUIRE(it != commands.end());
    CHECK(it->tint == (assets::Rgba{255, 219, 57, render::kOverlayAlpha}));  // yellow banner
    CHECK(it->tint_size == 16);

    layers.owner_tint = false;
    render::build_world_draw_list(world, *buildings, clock, view, all, layers, commands);
    CHECK(std::none_of(commands.begin(), commands.end(),
                       [](const render::DrawCommand& c) { return c.sprite == render::DrawCommand::kOverlayDiamond; }));
}

TEST_CASE("world renderer: warehouse flags in the owner's colour") {
    const assets::CodPredefinedSymbol haus_types[] = {{"HAUS", 1}};
    const auto haeuser = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 22101\n  Gfx: 0\n  Size: 2, 3\n  Rotate: 6\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Rauchfignr: 60\n  EndObj\n"
        "  @Nummer: +1\n  Id: 22102\n  Gfx: 24\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n  EndObj\n"
        "EndObj\n",
        haus_types);
    auto buildings = assets::BuildingTable::from_cod(haeuser);
    REQUIRE(buildings.ok());
    const assets::BuildingType& kontor = *buildings->by_id(22101);
    CHECK(kontor.prod_kind == 8);
    CHECK(kontor.attached_figures[0] == 60);
    CHECK(buildings->by_id(22102)->attached_figures[0] == 0);

    const assets::CodPredefinedSymbol fig_types[] = {{"FIGUR", 1}};
    const auto figuren = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 60\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 0\n  Posoffs: -40, 40\n  Fahnoffs: 0.5, -0.5, 2.0\n"
        "EndObj\n",
        fig_types);
    auto figures = assets::FigureTable::from_cod(figuren);
    REQUIRE(figures.ok());

    sim::World world;
    world.set_players({sim::Player{}, sim::Player{sim::PlayerRole::Computer, 2, 0, ""}});
    sim::WorldIsland island{assets::IslandHeader{}, sim::IslandMap(8, 8), 5, 5};
    REQUIRE(island.map.place(kontor, 1, 1, 0, 0, 1).ok());                    // owned, rotation 0
    REQUIRE(island.map.place(kontor, 0, 4, 1, 0, 1).ok());                    // owned, rotation 1 (3x2)
    REQUIRE(island.map.place(kontor, 4, 0, 0, 0).ok());                       // no city: no flag
    REQUIRE(island.map.place(*buildings->by_id(22102), 7, 7, 0, 0, 1).ok());  // no Rauchfignr
    REQUIRE(world.add_island(std::move(island)).ok());

    const auto flags = render::collect_building_figures(world, *buildings, *figures);
    REQUIRE(flags.size() == 2);
    // Centre of the footprint plus Fahnoffs turned with the building.
    CHECK(flags[0].x == 7.5);  // 5 + 1 + 2/2 + 0.5
    CHECK(flags[0].y == 7.0);  // 5 + 1 + 3/2 - 0.5
    CHECK(flags[0].z == 2.0);
    CHECK(flags[0].figure == 60);
    CHECK(flags[0].owner == 1);
    CHECK(flags[1].x == 7.0);   // 5 + 0 + 3/2 - (-0.5)
    CHECK(flags[1].y == 10.5);  // 5 + 4 + 2/2 + 0.5

    // Ship sheet: 6 colours x 8 frames.
    const auto terrain = make_sheet({{4, 2}});
    const auto ship_sheet = make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(48, {8, 4}));
    const auto palette = make_palette();
    const assets::SpriteSheet* sheets[] = {&terrain, &ship_sheet};
    auto atlas = render::SpriteAtlas::build(sheets, palette, 256);
    REQUIRE(atlas.ok());
    // Banners with flags at 0, 8, 16 of the ship sheet (as the original red, blue, yellow).
    render::BannerTable banners;
    for (std::uint32_t c = 0; c < 3; ++c) {
        banners.add(
            render::Banner{"test:" + std::to_string(c), {}, render::FlagStyle::Sprites, 8U * c, 8, std::nullopt});
    }
    render::WorldLayers layers;
    layers.figures = &*figures;
    layers.atlas = &*atlas;
    layers.ship_base = atlas->sheet_base(1);
    layers.banners = &banners;
    render::BuildingFigure flag = flags[0];
    flag.phase = 0;
    render::FigureSprites s;
    const render::WorldView view{render::Zoom::Large, 0};
    REQUIRE(render::building_figure_sprites(flag, world, *buildings, view, layers, 200, s));
    REQUIRE(s.count == 1);
    // Yellow (banner 2), frame 200 / 90 = 2.
    CHECK(s.parts[0].sprite == layers.ship_base + 16 + 2);
    // Point: x = (7.5 - 7.0 + 350) * 32, y = 14.5 * 16 = 232; centred; bottom at
    // y + Posoffs[1] + Posoffs[0] - 2.0 * 32 = y - 64 (the offsets cancel out), one pixel up.
    CHECK(s.parts[0].x == 11216 - 4);
    CHECK(s.parts[0].y == 232 - 64 - 1);
    CHECK(s.row == (232 + 40 - 1) / 16);  // sorted by y + Posoffs[1]

    // No display figures on an island the local player has not discovered.
    layers.local_player = 0;
    CHECK(!render::building_figure_sprites(flag, world, *buildings, view, layers, 200, s));
    world.island(0).discover(0);
    CHECK(render::building_figure_sprites(flag, world, *buildings, view, layers, 200, s));
    layers.local_player = sim::kNoPlayer;

    // A banner without flag sprites (like the free trader) or an unknown banner shows no flag.
    banners.add(render::Banner{"test:noflag", {}, render::FlagStyle::None, 0, 8, std::nullopt});
    world.set_players({sim::Player{}, sim::Player{sim::PlayerRole::Neutral, 3, 0, "", sim::faction::kTrader}});
    CHECK(!render::building_figure_sprites(flag, world, *buildings, view, layers, 0, s));
    world.set_players({sim::Player{}, sim::Player{sim::PlayerRole::Neutral, 99, 0, "", sim::faction::kTrader}});
    CHECK(!render::building_figure_sprites(flag, world, *buildings, view, layers, 0, s));
}

TEST_CASE("players: more than the original seven slots and extra banners (ADR 0007)") {
    // Original owner numbers: 0..6 are players, 7 is nobody.
    CHECK(sim::player_from_original(0) == 0);
    CHECK(sim::player_from_original(6) == 6);
    CHECK(sim::player_from_original(7) == sim::kNoPlayer);
    CHECK(sim::role_from_original(assets::PlayerKind::Pirate) == sim::PlayerRole::Neutral);
    CHECK(sim::faction_from_original(assets::PlayerKind::Pirate) == sim::faction::kPirates);
    CHECK(sim::faction_from_original(assets::PlayerKind::Computer) == sim::faction::kSettlers);

    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse("Objekt: HAUS\n@Nummer: 0\nId: 20100\nGfx: 0\nSize: 1, 1\nEndObj\n", types);
    auto buildings = assets::BuildingTable::from_cod(doc);
    REQUIRE(buildings.ok());

    // Twelve players; player 10 uses a banner that the original does not have.
    auto banners = render::BannerTable::original();
    const sim::BannerId teal = banners.add_colour("meinmod:tuerkis", {0, 160, 160, 255});
    CHECK(teal == 7);
    std::vector<sim::Player> players(12);
    players[10] = sim::Player{sim::PlayerRole::Human, teal, 0, "Fünfter"};
    sim::World world;
    world.set_players(players);
    sim::WorldIsland island{assets::IslandHeader{}, sim::IslandMap(1, 1), 5, 5};
    REQUIRE(island.map.place(*buildings->by_id(20100), 0, 0, 0, 0, 10).ok());
    REQUIRE(world.add_island(std::move(island)).ok());
    REQUIRE(world.player(10) != nullptr);
    CHECK(world.player(sim::kNoPlayer) == nullptr);

    const render::AnimationClock clock(*buildings);
    render::WorldLayers layers;
    layers.extent = render::SpriteExtent{16, 8};
    layers.banners = &banners;
    layers.owner_tint = true;
    const render::WorldView view{render::Zoom::Small, 0};
    render::Viewport all;
    render::world_pixel_size(view, all.width, all.height);
    std::vector<render::DrawCommand> commands;
    render::build_world_draw_list(world, *buildings, clock, view, all, layers, commands);
    const auto it = std::find_if(commands.begin(), commands.end(), [](const render::DrawCommand& c) {
        return c.sprite == render::DrawCommand::kOverlayDiamond;
    });
    REQUIRE(it != commands.end());
    CHECK(it->tint == (assets::Rgba{0, 160, 160, render::kOverlayAlpha}));
}

TEST_CASE("savegame: cities, warehouse goods, production list and deer") {
    std::vector<std::uint8_t> city(0xA8, 0);
    city[0] = 1;  // island number
    city[1] = 1;  // second city on the island
    city[2] = 0;  // owner
    std::memcpy(&city[0x87], "Lemonhall", 9);

    std::vector<std::uint8_t> kontor(0x3EC, 0);
    kontor[0] = 1;                              // island
    kontor[1] = 13;                             // x
    kontor[2] = 38;                             // y
    kontor[3] = 1;                              // city
    const std::size_t ziegel = 0x2C + 22 * 20;  // slot 22 = ware 24
    put_u16(kontor, ziegel + 12, 800);
    put_u16(kontor, ziegel + 16, 513);  // producer 20513

    std::vector<std::uint8_t> prod(0x18, 0);
    prod[0] = 1;
    prod[1] = 20;
    prod[2] = 21;
    prod[3] = 0x5A;
    prod[5] = 0x20;     // stock: 32 = 1 t
    prod[0x0F] = 0x80;  // working
    put_u16(prod, 0x10, 196);
    prod[0x14] = 0x01 | (5 << 2);  // enabled, animation phase 5
    prod[0x15] = 0x03;             // three cycles without raw material

    std::vector<std::uint8_t> deer(16, 0);
    deer[0] = 1;
    deer[1] = 10;
    deer[2] = 19;
    put_u16(deer, 4, 899);
    deer[8] = 2;

    std::vector<std::uint8_t> file;
    put_chunk(file, "STADT4", city);
    put_chunk(file, "KONTOR2", kontor);
    put_chunk(file, "PRODLIST2", prod);
    put_chunk(file, "HIRSCH2", deer);
    auto chunks = assets::ChunkFile::from_bytes(file);
    REQUIRE(chunks.ok());
    auto state = assets::load_game_state(*chunks);
    REQUIRE(state.ok());

    REQUIRE(state->cities.size() == 1);
    CHECK(state->cities[0].island == 1);
    CHECK(state->cities[0].index == 1);
    CHECK(state->cities[0].owner == 0);
    CHECK(state->cities[0].name == "Lemonhall");

    REQUIRE(state->warehouses.size() == 1);
    const auto& w = state->warehouses[0];
    CHECK(w.island == 1);
    CHECK(w.x == 13);
    CHECK(w.y == 38);
    CHECK(w.city == 1);
    REQUIRE(w.wares.size() == 23);
    CHECK(w.wares[0].ware == 2);
    CHECK(w.wares[22].ware == 24);
    CHECK(w.wares[22].producer_id == 20513);
    CHECK(w.wares[22].stock() == 800);
    CHECK(w.wares[22].stock() / assets::kStockUnitsPerTon == 25);  // 25 t Ziegel
    CHECK(w.wares[0].producer_id == 0);

    REQUIRE(state->production.size() == 1);
    CHECK(state->production[0].x == 20);
    CHECK(state->production[0].y == 21);
    CHECK(state->production[0].raw_bytes[0] == 0x5A);
    CHECK(state->production[0].enabled);
    CHECK(state->production[0].rate == 0x80);
    CHECK(state->production[0].anim_phase == 5);
    CHECK(state->production[0].idle_cycles == 3);
    CHECK(state->production[0].stock == 32);
    CHECK(state->production[0].produced == 196);

    REQUIRE(state->animals.size() == 2);
    CHECK(state->animals[0].island == 1);
    CHECK(state->animals[0].x == 10);
    CHECK(state->animals[0].y == 19);
    CHECK(state->animals[0].timer == 899);
    CHECK(state->animals[1].island == 2);

    // A chunk with a broken size is an error, not a partial read.
    std::vector<std::uint8_t> broken;
    put_chunk(broken, "STADT4", std::vector<std::uint8_t>(0xA0, 0));
    auto broken_chunks = assets::ChunkFile::from_bytes(broken);
    REQUIRE(broken_chunks.ok());
    CHECK(!assets::load_game_state(*broken_chunks).ok());
}

TEST_CASE("scenario: savegame names from Game.dat") {
    const char* game_dat =
        "  Lastfile: \"X:\\Games\\Anno 1602\\Savegame\\lastgame.gam\"\n"
        "  Objekt:   SPIELNAME\n"
        "\n"
        "    Name:      0, \"Merlin01\", 1\n"
        "    Name:      1, \"Leer01\", 1\n"
        "    Name:      12, \"Insel mit \xfc\", 0\n"
        "  EndObj;\n"
        "  Name: 5, \"not a slot\", 1\n";
    const auto slots = assets::parse_savegame_slots(game_dat);
    REQUIRE(slots.size() == 3);
    CHECK(slots[0].slot == 0);
    CHECK(slots[0].name == "Merlin01");
    CHECK(slots[0].file == "Savegame/game00.gam");
    CHECK(slots[1].name == "Leer01");
    CHECK(slots[2].slot == 12);
    CHECK(slots[2].file == "Savegame/game12.gam");
    CHECK(slots[2].name == "Insel mit \xfc");
}

TEST_CASE("scenario: high scores and the head of a scenario") {
    const char* hss =
        "\n  Objekt:   HISCORE\n\n"
        "    Entry:    1, \"\", 0, 0, 0\n"
        "    Entry:    0, \"Merlin\", 381, 2, 713262714\n"
        "  EndObj;\n";
    const auto scores = assets::parse_high_scores(hss);
    REQUIRE(scores.size() == 2);
    CHECK(scores[0].name == "Merlin");  // in the order of the index
    CHECK(scores[0].points == 381);
    CHECK(scores[0].stars == 2);
    CHECK(scores[0].game_id == 713262714U);
    CHECK(scores[1].name.empty());

    // A file without the SZENE_* chunks has the original's defaults.
    assets::ChunkFile empty;
    const assets::ScenarioHeader h = assets::load_scenario_header(empty);
    CHECK(h.mission_number == assets::ScenarioHeader::kNone);
    CHECK(h.campaign == assets::ScenarioHeader::kNone);
    CHECK(h.ranking == 2);
    CHECK(h.player_min == 1);
    CHECK(h.player_max == 1);
}

TEST_CASE("banners: players five to twelve get colours of their own, apart from the factions'") {
    auto banners = render::BannerTable::original();
    banners.add_more_players();
    REQUIRE(banners.size() == sim::kFirstExtraBanner + sim::kMaxPlayers - sim::kOriginalPlayers);
    std::set<std::tuple<std::uint8_t, std::uint8_t, std::uint8_t>> colours;
    for (const render::Banner& b : banners.banners()) {
        colours.emplace(b.colour.r, b.colour.g, b.colour.b);
    }
    CHECK(colours.size() == banners.size());  // no two alike
    CHECK(banners.get(sim::kFirstExtraBanner)->id == "open1602:orange");
    CHECK(banners.get(sim::kFirstExtraBanner)->flag == render::FlagStyle::Tinted);
    banners.add_more_players();  // only once
    CHECK(banners.size() == sim::kFirstExtraBanner + sim::kMaxPlayers - sim::kOriginalPlayers);
}
