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
        "  @Nummer: 1\n  Id: 22728\n  Kind: 11\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 21201\n  Kind: 19\n  Gfx: 2\n  Size: 1, 1\n"
        "  @Nummer: 3\n  Id: 21012\n  Kind: 23\n  Gfx: 3\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 4\n  Id: 21109\n  Kind: 15\n  Gfx: 10\n  Size: 4, 4\n"
        "  @Nummer: 5\n  Id: 20701\n  Kind: 14\n  Gfx: 30\n  Size: 3, 2\n"
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
    CHECK(editor::terrain_pieces(buildings, editor::TerrainGroup::Beach).size() == 1);
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
    CHECK(stroke.stamp(0, 3, 3, *buildings.by_id(kBeach), 2));
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
}

TEST_CASE("editor terrain: the pipette, filling and replacing") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    auto document = editor::Document::edit(world);
    {
        editor::TerrainStroke stroke(world, buildings);
        REQUIRE(stroke.stamp(0, 5, 5, *buildings.by_id(kBeach), 3));
        document.perform(stroke.finish("terrain"));
    }
    const auto picked = editor::terrain_at(world, buildings, 45, 35);
    REQUIRE(picked.has_value());
    CHECK(picked->id == kBeach);
    CHECK(picked->rotation == 3);
    CHECK(!editor::terrain_at(world, buildings, 10, 10).has_value());  // open sea

    // Filling the meadow: every meadow tile joined to it, not the beach tile or the sea.
    auto fill = editor::fill_terrain(world, buildings, 42, 32, {kGrass, 0});
    REQUIRE(fill.ok());
    CHECK((*fill)->kind() == "terrain_fill");
    document.perform(std::move(*fill));
    CHECK(record_at(world, buildings, 18, 14) == kGrass);
    CHECK(record_at(world, buildings, 5, 5) == kBeach);
    CHECK(record_at(world, buildings, 0, 0) == kSea);
    // Nothing to fill: the same piece.
    CHECK(!editor::fill_terrain(world, buildings, 42, 32, {kGrass, 0}).ok());
    CHECK(!editor::fill_terrain(world, buildings, 42, 32, {kMountain, 0}).ok());

    // Replacing the sea by meadow, every tile of it on the island.
    auto replace = editor::replace_terrain(world, buildings, 40, 30, {kMeadow, 0});
    REQUIRE(replace.ok());
    document.perform(std::move(*replace));
    CHECK(record_at(world, buildings, 0, 0) == kMeadow);
    CHECK(record_at(world, buildings, 19, 15) == kMeadow);
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
