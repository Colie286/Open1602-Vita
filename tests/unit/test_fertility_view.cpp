// SPDX-License-Identifier: GPL-3.0-or-later
// The fertility view of fields (engine/app/src/fertility_view.hpp).

#include <vector>

#include "../test_framework.hpp"
#include "fertility_view.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/construction.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kGrowing = 21501;
constexpr std::uint32_t kWithered = 21503;
constexpr std::uint32_t kTreeGrowing = 21504;
constexpr std::uint32_t kFarm = 21510;
constexpr std::uint32_t kHouse = 21520;
constexpr std::uint8_t kTobacco = 46;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        // Tobacco: growing, ripe, withered.
        "  @Nummer: 1\n  Id: 21501\n  Kind: 10\n  Gfx: 10\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 175\n  EndObj\n"
        "  @Nummer: 2\n  Id: 21502\n  Kind: 10\n  Gfx: 15\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 46\n  EndObj\n"
        "  @Nummer: 3\n  Id: 21503\n  Kind: 10\n  Gfx: 16\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 175\n    Doerrflg: 1\n  EndObj\n"
        // A tree: growing and ripe; trees grow everywhere.
        "  @Nummer: 4\n  Id: 21504\n  Kind: 10\n  Gfx: 21\n  Size: 1, 1\n  AnimAnz: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 300\n  EndObj\n"
        "  @Nummer: 5\n  Id: 21505\n  Kind: 10\n  Gfx: 22\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        // The tobacco plantation and a house.
        "  @Nummer: 6\n  Id: 21510\n  Kind: 14\n  Gfx: 30\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 3\n    Ware: 9\n    Rohstoff: 46\n    Radius: 4\n  EndObj\n"
        "  @Nummer: 7\n  Id: 21520\n  Kind: 14\n  Gfx: 40\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("fertility view: the crop of a field and of a farm, none for trees and houses") {
    const assets::BuildingTable buildings = make_buildings();
    CHECK(app::fertility_crop(buildings, *buildings.by_id(kGrowing)) == kTobacco);
    CHECK(app::fertility_crop(buildings, *buildings.by_id(kFarm)) == kTobacco);
    CHECK(app::fertility_crop(buildings, *buildings.by_id(kTreeGrowing)) == 0);
    CHECK(app::fertility_crop(buildings, *buildings.by_id(kHouse)) == 0);
}

TEST_CASE("fertility view: open ground and fields of the player and of nobody, green where the crop bears") {
    const assets::BuildingTable buildings = make_buildings();
    sim::World world{100, 100};
    std::vector<sim::Player> players(2);
    world.set_players(players);
    sim::IslandMap map(32, 4);
    for (std::int32_t y = 0; y < 4; ++y) {
        for (std::int32_t x = 0; x < 32; ++x) {
            REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
        }
    }
    map.capture_ground();
    // Row 0 the player's, row 1 another player's, row 2 nobody's with a house, a field and a
    // withered field, row 3 nobody's.
    for (std::int32_t x = 0; x < 32; ++x) {
        REQUIRE(map.place(*buildings.by_id(kGround), x, 0, 0, 0, 0, 0).ok());
        REQUIRE(map.place(*buildings.by_id(kGround), x, 1, 0, 0, 1, 0).ok());
    }
    REQUIRE(map.place(*buildings.by_id(kHouse), 0, 2, 0, 0).ok());
    REQUIRE(map.place(*buildings.by_id(kGrowing), 1, 2, 0, 0).ok());
    REQUIRE(map.place(*buildings.by_id(kWithered), 2, 2, 0, 0).ok());
    sim::IslandMap barren = map;
    assets::IslandHeader header;
    header.fertility = 1U << (kTobacco - 45);  // every tile bears
    REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), 10, 10}).ok());
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(barren), 50, 10}).ok());

    std::vector<render::TileMark> marks;
    app::fertility_marks(world, buildings, 0, 0, kTobacco, marks);
    CHECK(marks.size() == 32 + 31 + 32);  // all but the other player's row and the house
    for (const render::TileMark& m : marks) {
        CHECK(m.island == 0);
        CHECK(m.y != 1);
        CHECK(!(m.x == 0 && m.y == 2));
        CHECK((m.colour == app::kFertileTint));
    }
    // Where the island lacks the crop, only some tiles bear.
    marks.clear();
    app::fertility_marks(world, buildings, 0, 1, kTobacco, marks);
    REQUIRE(marks.size() == 32 + 31 + 32);
    std::size_t green = 0;
    for (const render::TileMark& m : marks) {
        const bool bears = sim::tile_fertile_at(world, buildings, 1, kTobacco, m.x, m.y, 0);
        CHECK((m.colour == (bears ? app::kFertileTint : app::kInfertileTint)));
        green += bears ? 1U : 0U;
    }
    CHECK(green < marks.size());
    // No crop, no island: nothing.
    marks.clear();
    app::fertility_marks(world, buildings, 0, 0, 0, marks);
    app::fertility_marks(world, buildings, 0, 5, kTobacco, marks);
    CHECK(marks.empty());
}

TEST_CASE("fertility view: nothing on an island the player has neither explored nor settled on") {
    const assets::BuildingTable buildings = make_buildings();
    sim::World world{100, 100};
    std::vector<sim::Player> players(2);
    world.set_players(players);
    sim::IslandMap map(8, 2);
    for (std::int32_t y = 0; y < 2; ++y) {
        for (std::int32_t x = 0; x < 8; ++x) {
            REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
        }
    }
    map.capture_ground();
    assets::IslandHeader header;
    header.fertility = 1U << (kTobacco - 45);
    REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), 10, 10}).ok());

    std::vector<render::TileMark> marks;
    app::fertility_marks(world, buildings, 0, 0, kTobacco, marks);
    CHECK(marks.empty());
    // Explored by another player only: still unknown.
    world.island(0).explore(1);
    app::fertility_marks(world, buildings, 0, 0, kTobacco, marks);
    CHECK(marks.empty());
    // Explored by the player: every open tile.
    world.island(0).explore(0);
    app::fertility_marks(world, buildings, 0, 0, kTobacco, marks);
    CHECK(marks.size() == 16);
}
