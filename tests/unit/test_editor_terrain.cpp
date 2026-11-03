// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's terrain (ADR 0013): the palette's groups, a stroke of the brush, filling and
// replacing, each one step that undo takes back to the same bytes.

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/editor/document.hpp"
#include "o1602/editor/islands.hpp"
#include "o1602/editor/terrain.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kMeadow = 22722;
constexpr std::uint32_t kGrass = 22728;
constexpr std::uint32_t kSea = 21201;
constexpr std::uint32_t kBeach = 21012;
constexpr std::uint32_t kMountain = 21109;  // 4 x 4 tiles
constexpr std::uint32_t kHall = 20701;      // 3 x 2 tiles

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 22722\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 22728\n  Kind: 11\n  Gfx: 1\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 2\n  Id: 21201\n  Kind: 19\n  Gfx: 2\n  Size: 1, 1\n"
        "  @Nummer: 3\n  Id: 21012\n  Kind: 23\n  Gfx: 3\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 4\n  Id: 21109\n  Kind: 15\n  Gfx: 10\n  Size: 4, 4\n"
        "  @Nummer: 5\n  Id: 20701\n  Kind: 14\n  Gfx: 30\n  Size: 3, 2\n"
        "  @Nummer: 6\n  Id: 21203\n  Kind: 19\n  Gfx: 40\n  Size: 1, 1\n"
        "  @Nummer: 7\n  Id: 21214\n  Kind: 20\n  Gfx: 41\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 8\n  Id: 21207\n  Kind: 21\n  Gfx: 45\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 9\n  Id: 21206\n  Kind: 21\n  Gfx: 49\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 10\n  Id: 21051\n  Kind: 25\n  Gfx: 53\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 11\n  Id: 21031\n  Kind: 26\n  Gfx: 57\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 12\n  Id: 21202\n  Kind: 19\n  Gfx: 61\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 13\n  Id: 21204\n  Kind: 19\n  Gfx: 65\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 14\n  Id: 21209\n  Kind: 19\n  Gfx: 69\n  Size: 1, 1\n  Rotate: 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of w x h tiles: meadow with a ring of sea, at (x, y).
sim::WorldIsland make_island(const assets::BuildingTable& buildings, std::uint32_t w, std::uint32_t h,
                             std::uint16_t number, std::uint32_t x, std::uint32_t y) {
    sim::IslandMap map(w, h);
    for (std::uint32_t ty = 0; ty < h; ++ty) {
        for (std::uint32_t tx = 0; tx < w; ++tx) {
            const bool edge = tx == 0 || ty == 0 || tx + 1 == w || ty + 1 == h;
            REQUIRE(map.place(*buildings.by_id(edge ? kSea : kMeadow), static_cast<std::int32_t>(tx),
                              static_cast<std::int32_t>(ty), 0, 0)
                        .ok());
        }
    }
    map.capture_ground();
    assets::IslandHeader header;
    header.number = number;
    header.width = static_cast<std::uint16_t>(w);
    header.height = static_cast<std::uint16_t>(h);
    header.pos_x = static_cast<std::uint16_t>(x);
    header.pos_y = static_cast<std::uint16_t>(y);
    return sim::WorldIsland{header, std::move(map), x, y};
}

/// The original's map with an island of 20 x 16 at (40, 30).
sim::World make_world(const assets::BuildingTable& buildings) {
    sim::World world(500, 350);
    REQUIRE(world.add_island(make_island(buildings, 20, 16, 0, 40, 30), &buildings).ok());
    return world;
}

std::uint32_t record_at(const sim::World& world, const assets::BuildingTable& buildings, std::uint32_t x,
                        std::uint32_t y) {
    const auto* b = buildings.by_sprite(world.islands()[0].map.top(x, y).sprite);
    return b != nullptr ? b->id : 0;
}

}  // namespace

TEST_CASE("editor terrain: the palette has the terrain by group, no building") {
    const auto buildings = make_buildings();
    const auto ground = editor::terrain_pieces(buildings, editor::TerrainGroup::Ground);
    REQUIRE(ground.size() == 2);
    CHECK(ground[0]->id == kMeadow);
    CHECK(ground[1]->id == kGrass);
    CHECK(editor::terrain_pieces(buildings, editor::TerrainGroup::Rock).size() == 1);
    CHECK(editor::terrain_pieces(buildings, editor::TerrainGroup::Beach).size() == 3);  // beach and its corners
    CHECK(!editor::is_terrain(buildings, *buildings.by_id(kHall)));
    CHECK(editor::is_terrain(buildings, *buildings.by_id(kSea)));
}

TEST_CASE("editor terrain: a stroke is one step, the ground below too, and undo gives the same bytes") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    auto document = editor::Document::edit(world);
    const auto before = document.save(buildings);
    REQUIRE(before.ok());

    editor::TerrainStroke stroke(world, buildings);
    for (const auto& [x, y] : editor::brush_tiles(45, 35, 3, world.width(), world.height())) {
        CHECK(stroke.stamp_world(x, y, *buildings.by_id(kGrass), 0));
    }
    CHECK(stroke.changed());
    // The map shows the stroke while it is drawn.
    CHECK(record_at(world, buildings, 5, 5) == kGrass);
    CHECK(record_at(world, buildings, 4, 4) == kGrass);
    CHECK(record_at(world, buildings, 7, 7) == kMeadow);
    CHECK(world.islands()[0].map.ground(5, 5).sprite == world.islands()[0].map.top(5, 5).sprite);
    auto op = stroke.finish("terrain");
    REQUIRE(op != nullptr);
    CHECK(op->kind() == "terrain");
    document.perform(std::move(op));
    CHECK(record_at(world, buildings, 6, 6) == kGrass);
    CHECK(sim::validate(world).ok());
    const auto after = document.save(buildings);
    REQUIRE(after.ok());
    CHECK(*after != *before);
    REQUIRE(document.undo());
    CHECK(record_at(world, buildings, 5, 5) == kMeadow);
    const auto undone = document.save(buildings);
    REQUIRE(undone.ok());
    CHECK(*undone == *before);
    REQUIRE(document.redo());
    const auto redone = document.save(buildings);
    REQUIRE(redone.ok());
    CHECK(*redone == *after);
}

TEST_CASE("editor terrain: a stroke taken back leaves the world as it was") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    editor::TerrainStroke stroke(world, buildings);
    CHECK(stroke.stamp(0, 3, 3, *buildings.by_id(kGrass), 2));
    CHECK(world.islands()[0].map.top(3, 3).rotation == 2);
    stroke.cancel();
    CHECK(record_at(world, buildings, 3, 3) == kMeadow);
    CHECK(stroke.finish("terrain") == nullptr);
}

TEST_CASE("editor terrain: a mountain painted over in part goes whole, with its deposit") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    {
        editor::TerrainStroke stroke(world, buildings);
        REQUIRE(stroke.stamp(0, 4, 4, *buildings.by_id(kMountain), 0));
        REQUIRE(stroke.finish("terrain") != nullptr);
    }
    CHECK(record_at(world, buildings, 7, 7) == kMountain);
    world.island(0).header.deposits.push_back({2, 4, 4, 0, 0, 100});
    // The same mountain laid again changes nothing, and its deposit stays.
    {
        editor::TerrainStroke stroke(world, buildings);
        CHECK(stroke.stamp(0, 4, 4, *buildings.by_id(kMountain), 0));
        CHECK(stroke.finish("terrain") == nullptr);
        CHECK(world.islands()[0].header.deposits.size() == 1);
    }
    editor::TerrainStroke stroke(world, buildings);
    REQUIRE(stroke.stamp(0, 5, 5, *buildings.by_id(kGrass), 0));
    CHECK(record_at(world, buildings, 5, 5) == kGrass);
    CHECK(record_at(world, buildings, 4, 4) == kMeadow);
    CHECK(record_at(world, buildings, 7, 7) == kMeadow);
    CHECK(world.islands()[0].header.deposits.empty());
    auto op = stroke.finish("terrain");
    REQUIRE(op != nullptr);
    op->revert(world);
    CHECK(record_at(world, buildings, 5, 5) == kMountain);
    CHECK(world.islands()[0].header.deposits.size() == 1);
    op->apply(world);
    CHECK(world.islands()[0].header.deposits.empty());
}

TEST_CASE("editor terrain: buildings, cities and ships keep their tiles") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    REQUIRE(world.island(0).map.place(*buildings.by_id(kHall), 10, 10, 0, 0).ok());
    world.island(0).map.set_city(2, 2, 0, 0);
    sim::Ship ship;
    ship.id = 1;
    ship.x = 40;
    ship.y = 31;
    world.ships().push_back(ship);
    editor::TerrainStroke stroke(world, buildings);
    CHECK(!stroke.stamp(0, 11, 10, *buildings.by_id(kGrass), 0));
    CHECK(stroke.first_refusal() == editor::TerrainStroke::Refusal::NotTerrain);
    CHECK(stroke.refusal(0, 2, 2) == editor::TerrainStroke::Refusal::City);
    CHECK(stroke.refusal(0, 0, 1) == editor::TerrainStroke::Refusal::Unit);
    CHECK(stroke.refusal(0, 25, 1) == editor::TerrainStroke::Refusal::NoIsland);
    CHECK(!stroke.stamp_world(10, 10, *buildings.by_id(kGrass), 0));
    // A mountain whose footprint holds a refused tile is not laid at all.
    CHECK(!stroke.stamp(0, 8, 8, *buildings.by_id(kMountain), 0));
    CHECK(!stroke.changed());
    // Land on the sea or the beach would leave a gap in the picture: the coast is shape()'s.
    CHECK(!stroke.stamp(0, 0, 5, *buildings.by_id(kGrass), 0));
    CHECK(!stroke.stamp(0, 5, 5, *buildings.by_id(kBeach), 0));
    CHECK(!stroke.stamp(0, 0, 5, *buildings.by_id(kBeach), 0));
    CHECK(!stroke.changed());
}

TEST_CASE("editor terrain: the pipette, filling and replacing") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    auto document = editor::Document::edit(world);
    {
        editor::TerrainStroke stroke(world, buildings);
        REQUIRE(stroke.stamp(0, 5, 5, *buildings.by_id(kGrass), 3));
        document.perform(stroke.finish("terrain"));
    }
    const auto picked = editor::terrain_at(world, buildings, 45, 35);
    REQUIRE(picked.has_value());
    CHECK(picked->id == kGrass);
    CHECK(picked->rotation == 3);
    CHECK(!editor::terrain_at(world, buildings, 10, 10).has_value());  // open sea

    // Filling the meadow: every meadow tile joined to it, not the beach tile or the sea.
    auto fill = editor::fill_terrain(world, buildings, 42, 32, {kGrass, 0});
    REQUIRE(fill.ok());
    CHECK((*fill)->kind() == "terrain_fill");
    document.perform(std::move(*fill));
    CHECK(record_at(world, buildings, 18, 14) == kGrass);
    CHECK(world.islands()[0].map.top(5, 5).rotation == 3);  // not the meadow's: not filled
    CHECK(record_at(world, buildings, 0, 0) == kSea);
    // Nothing to fill: the same piece.
    CHECK(!editor::fill_terrain(world, buildings, 42, 32, {kGrass, 0}).ok());
    CHECK(!editor::fill_terrain(world, buildings, 42, 32, {kMountain, 0}).ok());

    // Replacing the deep sea by the light, every tile of it on the island; land is of another level.
    CHECK(!editor::replace_terrain(world, buildings, 40, 30, {kMeadow, 0}).ok());
    auto replace = editor::replace_terrain(world, buildings, 40, 30, {21203, 0});
    REQUIRE(replace.ok());
    document.perform(std::move(*replace));
    CHECK(record_at(world, buildings, 0, 0) == 21203);
    CHECK(record_at(world, buildings, 19, 15) == 21203);
    CHECK(record_at(world, buildings, 6, 6) == kGrass);
    REQUIRE(document.undo());
    CHECK(record_at(world, buildings, 19, 15) == kSea);
}

TEST_CASE("editor terrain: brushes, lines and random pieces") {
    CHECK(editor::brush_tiles(5, 5, 1, 100, 100).size() == 1);
    CHECK(editor::brush_tiles(5, 5, 3, 100, 100).size() == 9);
    CHECK(editor::brush_tiles(0, 0, 3, 100, 100).size() == 4);  // cut at the map's edge
    const auto even = editor::brush_tiles(5, 5, 2, 100, 100);
    REQUIRE(even.size() == 4);
    CHECK((even.front() == std::pair<std::uint32_t, std::uint32_t>{5, 5}));

    const auto line = editor::line_tiles(0, 0, 3, 2);
    CHECK(line.size() == 6);  // 3 + 2 steps, by sides
    CHECK((line.front() == std::pair<std::int64_t, std::int64_t>{0, 0}));
    CHECK((line.back() == std::pair<std::int64_t, std::int64_t>{3, 2}));
    for (std::size_t i = 1; i < line.size(); ++i) {
        const auto dx = line[i].first - line[i - 1].first;
        const auto dy = line[i].second - line[i - 1].second;
        CHECK((dx == 0) != (dy == 0));
    }
    CHECK(editor::line_tiles(4, 4, 4, 4).size() == 1);

    const auto buildings = make_buildings();
    bool meadow = false;
    bool grass = false;
    for (std::uint64_t r = 0; r < 64; ++r) {
        const auto piece = editor::random_piece(buildings, editor::TerrainGroup::Ground, 0, r);
        REQUIRE(piece.has_value());
        meadow = meadow || piece->id == kMeadow;
        grass = grass || piece->id == kGrass;
        CHECK(piece == editor::random_piece(buildings, editor::TerrainGroup::Ground, 0, r));
    }
    CHECK(meadow);
    CHECK(grass);
    // A group whose only piece is larger than a tile has none to draw.
    CHECK(!editor::random_piece(buildings, editor::TerrainGroup::Rock, 0, 1).has_value());
    // Pieces that turn are turned at random.
    bool turned = false;
    for (std::uint64_t r = 0; r < 32; ++r) {
        turned = turned || editor::random_piece(buildings, editor::TerrainGroup::Beach, 0, r)->rotation != 0;
    }
    CHECK(turned);
}

TEST_CASE("editor terrain: the sea brush lays surf and beach about the water, one step") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    auto document = editor::Document::edit(world);
    const auto before = document.save(buildings);
    REQUIRE(before.ok());
    editor::TerrainStroke stroke(world, buildings);
    // One tile of water inside the land has no tiles of the game: the brush widens it to 3 x 3.
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> one = {{9, 7}};
    CHECK(stroke.shape(0, one, false, 1));
    CHECK(record_at(world, buildings, 9, 7) == 21203);  // open water in the middle
    CHECK(record_at(world, buildings, 8, 7) == 21214);  // surf with land to the west
    CHECK(record_at(world, buildings, 8, 6) == 21206);  // the surf's bay in the corner
    CHECK(record_at(world, buildings, 7, 7) == kBeach);
    CHECK(record_at(world, buildings, 7, 5) == 21031);  // the land's inner corner
    CHECK(record_at(world, buildings, 6, 7) == kMeadow);
    // The beach faces the water: west of the hole it is turned the other way than east of it.
    CHECK(world.islands()[0].map.top(7, 7).rotation != world.islands()[0].map.top(11, 7).rotation);
    document.perform(stroke.finish("terrain"));
    CHECK(sim::validate(world).ok());
    REQUIRE(document.undo());
    const auto undone = document.save(buildings);
    REQUIRE(undone.ok());
    CHECK(*undone == *before);
}

TEST_CASE("editor terrain: the land brush fills the water back, but keeps a tile of sea to the edge") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    editor::TerrainStroke stroke(world, buildings);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> hole = {{9, 7}};
    REQUIRE(stroke.shape(0, hole, false, 1));
    std::vector<std::pair<std::uint32_t, std::uint32_t>> fill;
    for (std::uint32_t y = 6; y <= 8; ++y) {
        for (std::uint32_t x = 8; x <= 10; ++x) {
            fill.emplace_back(x, y);
        }
    }
    CHECK(stroke.shape(0, fill, true, 2));
    for (std::uint32_t y = 4; y <= 10; ++y) {
        for (std::uint32_t x = 6; x <= 12; ++x) {
            CHECK(record_at(world, buildings, x, y) == kMeadow);
        }
    }
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> edge = {{0, 5}};
    (void)stroke.shape(0, edge, true, 3);
    CHECK(record_at(world, buildings, 0, 5) != kMeadow);
    CHECK(stroke.first_refusal() == editor::TerrainStroke::Refusal::Edge);
}

TEST_CASE("editor terrain: an island grows by open sea, every tile keeping its place, one step") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    world.island(0).header.deposits.push_back({2, 4, 4, 0, 0, 100});
    REQUIRE(world.island(0).map.place(*buildings.by_id(kGrass), 6, 6, 0, 0).ok());
    auto document = editor::Document::edit(world);
    const auto before = document.save(buildings);
    REQUIRE(before.ok());
    auto op = editor::grow_island(world, 0, 2, 3, 4, 5, buildings);
    REQUIRE(op.ok());
    CHECK((*op)->kind() == "island_grow");
    document.perform(std::move(*op));
    const sim::WorldIsland& isl = world.islands()[0];
    CHECK(isl.x == 38);
    CHECK(isl.y == 27);
    CHECK(isl.map.width() == 26);
    CHECK(isl.map.height() == 24);
    CHECK(isl.header.width == 26);
    CHECK(record_at(world, buildings, 8, 9) == kGrass);  // (6, 6) before
    CHECK(record_at(world, buildings, 0, 0) == kSea);
    CHECK(record_at(world, buildings, 25, 23) == kSea);
    CHECK(isl.header.deposits.front().x == 6);
    CHECK(isl.header.deposits.front().y == 7);
    CHECK(world.cell(46, 36).island == 0);
    CHECK(world.cell(46, 36).local_x == 8);
    CHECK(world.cell(37, 36).island == sim::World::kSea);
    CHECK(sim::validate(world).ok());
    REQUIRE(document.undo());
    const auto undone = document.save(buildings);
    REQUIRE(undone.ok());
    CHECK(*undone == *before);

    // Not off the map, not over another island's land, not an island with a city.
    CHECK(!editor::grow_island(world, 0, 41, 0, 0, 0, buildings).ok());
    REQUIRE(world.add_island(make_island(buildings, 10, 10, 1, 62, 30), &buildings).ok());
    // Over the other's land the grown sea lies below it: those tiles stay the other's.
    auto over = editor::grow_island(world, 0, 0, 0, 5, 0, buildings);
    REQUIRE(over.ok());
    (*over)->apply(world);
    CHECK(world.cell(63, 35).island == 1);
    CHECK(world.cell(61, 35).island == 0);
    world.island(0).map.set_city(5, 5, 0, 0);
    CHECK(!editor::grow_island(world, 0, 0, 0, 1, 0, buildings).ok());
}

TEST_CASE("editor terrain: a new island of open sea, where land is painted on the open sea") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    auto op = editor::add_sea_island(world, 200, 100, 16, 18, 1, buildings);
    REQUIRE(op.ok());
    (*op)->apply(world);
    REQUIRE(world.islands().size() == 2);
    const sim::WorldIsland& isl = world.islands()[1];
    CHECK(isl.x == 200);
    CHECK(isl.map.height() == 18);
    CHECK(isl.header.climate == 1);
    CHECK(isl.header.number == 1);
    CHECK(world.cell(205, 105).island == 1);
    CHECK(sim::validate(world).ok());
    // Over another island's land its sea lies below: those tiles stay the other's.
    auto over = editor::add_sea_island(world, 50, 35, 16, 16, 0, buildings);
    REQUIRE(over.ok());
    (*over)->apply(world);
    CHECK(world.cell(55, 38).island == 0);
    CHECK(world.cell(62, 38).island == 2);
}

TEST_CASE("editor terrain: land on a new island lays the sea by its depth, one undo step with the island") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    auto document = editor::Document::edit(world);
    const auto before = document.save(buildings);
    REQUIRE(before.ok());
    // As the land brush on open sea: the island is made, then the stroke goes on; one step.
    auto room = editor::add_sea_island(world, 200, 100, 31, 31, 0, buildings);
    REQUIRE(room.ok());
    std::vector<std::unique_ptr<editor::Operation>> steps;
    (*room)->apply(world);
    steps.push_back(std::move(room).value());
    editor::TerrainStroke stroke(world, buildings);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> tiles{{14, 15}, {15, 15}, {16, 15}};
    CHECK(stroke.shape(1, tiles, true, 7));
    steps.push_back(stroke.finish("terrain"));
    document.record(std::make_unique<editor::Steps>(std::move(steps), "terrain"));
    const sim::IslandMap& map = world.islands()[1].map;
    const auto id_at = [&](std::uint32_t x, std::uint32_t y) {
        const auto* b = buildings.by_sprite(map.top(x, y).sprite);
        return b != nullptr ? b->id : 0U;
    };
    // The land with its beach runs from row 14 to 16, the surf a tile out, the light sea past it,
    // then the middle sea, the dark open sea far out and at the island's edge.
    CHECK(id_at(15, 15) == kMeadow);
    CHECK(id_at(15, 12) == 21203);
    CHECK(id_at(15, 4) == kSea);
    CHECK(id_at(0, 0) == kSea);
    bool middle = false;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        middle = middle || id_at(15, y) == 21204 || id_at(15, y) == 21202;
    }
    CHECK(middle);
    REQUIRE(document.history().undo_steps() == 2);  // the save before described the scenario
    CHECK(document.undo());
    CHECK(world.islands().size() == 1);
    const auto undone = document.save(buildings);
    REQUIRE(undone.ok());
    CHECK(*undone == *before);
    CHECK(document.redo());
    CHECK(world.islands().size() == 2);
    CHECK(id_at(15, 15) == kMeadow);
}

TEST_CASE("editor terrain: a drop of water does not eat the land about it") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    auto room = editor::add_sea_island(world, 200, 100, 31, 31, 0, buildings);
    REQUIRE(room.ok());
    (*room)->apply(world);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> block;
    for (std::uint32_t y = 11; y < 20; ++y) {
        for (std::uint32_t x = 11; x < 20; ++x) {
            block.emplace_back(x, y);
        }
    }
    editor::TerrainStroke land(world, buildings);
    CHECK(land.shape(1, block, true, 3));
    (void)land.finish("terrain");
    const auto land_tiles = [&] {
        std::size_t n = 0;
        const sim::IslandMap& map = world.islands()[1].map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const auto* b = buildings.by_sprite(map.top(x, y).sprite);
                n += b != nullptr && editor::terrain_level(*b) > 0 ? 1U : 0U;
            }
        }
        return n;
    };
    const std::size_t before = land_tiles();
    CHECK(before >= 81);
    for (const auto& at : {std::pair<std::uint32_t, std::uint32_t>{15, 15}, {12, 15}}) {
        editor::TerrainStroke water(world, buildings);
        const std::vector<std::pair<std::uint32_t, std::uint32_t>> drop{at};
        const bool changed = water.shape(1, drop, false, 5);
        // A lake as small as the coast allows, or nothing and the reason.
        CHECK((changed || water.first_refusal() == editor::TerrainStroke::Refusal::Shape));
        CHECK(land_tiles() + 25 >= before);
        water.cancel();
    }
}
