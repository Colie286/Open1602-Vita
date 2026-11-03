// SPDX-License-Identifier: GPL-3.0-or-later
#include <utility>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/editor/resources.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kForest = 20202;
constexpr std::uint32_t kBigMountain = 21101;
constexpr std::uint32_t kSmallMountain = 21102;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20202\n  Kind: 10\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 21101\n  Kind: 15\n  Gfx: 2\n  Size: 3, 3\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 12\n    Ware: 55\n    Erzbergnr: 0\n  EndObj\n"
        "  @Nummer: 3\n  Id: 21102\n  Kind: 15\n  Gfx: 11\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 12\n    Ware: 55\n    Erzbergnr: 1\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of 20 x 20 ground at world (10, 10): a big mountain at (2, 2), a small one at (10, 2),
/// forest from (2, 10) to (5, 13).
struct Island {
    assets::BuildingTable buildings = make_buildings();
    sim::World world{60, 60};

    Island() {
        sim::IslandMap map(20, 20);
        for (std::int32_t y = 0; y < 20; ++y) {
            for (std::int32_t x = 0; x < 20; ++x) {
                const bool forest = x >= 2 && x <= 5 && y >= 10 && y <= 13;
                REQUIRE(map.place(*buildings.by_id(forest ? kForest : kGround), x, y, 0, 0).ok());
            }
        }
        REQUIRE(map.place(*buildings.by_id(kBigMountain), 2, 2, 0, 0).ok());
        REQUIRE(map.place(*buildings.by_id(kSmallMountain), 10, 2, 0, 0).ok());
        map.capture_ground();
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
    }
};

}  // namespace

TEST_CASE("editor resources: veins go into ore mountains, at their origin, one each") {
    Island island;
    using editor::Resource;
    using editor::ResourceRefusal;
    const auto fits = [&](sim::TilePos t, Resource r) {
        return editor::resource_fits(island.world, island.buildings, 0, t, r);
    };
    CHECK(fits({3, 3}, Resource::BigOre) == ResourceRefusal::None);
    CHECK(fits({11, 3}, Resource::BigOre) == ResourceRefusal::SmallMount);
    CHECK(fits({11, 3}, Resource::Gold) == ResourceRefusal::None);
    CHECK(fits({15, 15}, Resource::SmallOre) == ResourceRefusal::NoMountain);
    CHECK(fits({25, 3}, Resource::SmallOre) == ResourceRefusal::NoIsland);
    CHECK(!editor::place_resource(island.world, island.buildings, 0, {15, 15}, Resource::Gold).ok());

    auto vein = editor::place_resource(island.world, island.buildings, 0, {4, 3}, Resource::BigOre);
    REQUIRE(vein.ok());
    (*vein)->apply(island.world);
    const auto& deposits = island.world.islands()[0].header.deposits;
    REQUIRE(deposits.size() == 1);
    CHECK(deposits[0].ware == assets::ware::kOre);
    CHECK(deposits[0].x == 2);  // the mountain's origin, where mines look
    CHECK(deposits[0].y == 2);
    CHECK(deposits[0].kind == 0);
    CHECK(deposits[0].amount == editor::kBigOreAmount);
    CHECK(deposits[0].mask == 0);
    CHECK(fits({2, 4}, Resource::Gold) == ResourceRefusal::Taken);

    auto gold = editor::place_resource(island.world, island.buildings, 0, {10, 3}, Resource::Gold);
    REQUIRE(gold.ok());
    (*gold)->apply(island.world);
    REQUIRE(island.world.islands()[0].header.deposits.size() == 2);
    CHECK(island.world.islands()[0].header.deposits[1].ware == assets::ware::kGold);
    CHECK(island.world.islands()[0].header.deposits[1].amount == editor::kGoldAmount);

    // Taken away by any tile of the mountain; undone, back again.
    auto gone = editor::remove_resource(island.world, island.buildings, 0, {11, 3});
    REQUIRE(gone.ok());
    (*gone)->apply(island.world);
    CHECK(island.world.islands()[0].header.deposits.size() == 1);
    (*gone)->revert(island.world);
    CHECK(island.world.islands()[0].header.deposits.size() == 2);
    CHECK(!editor::remove_resource(island.world, island.buildings, 0, {15, 15}).ok());
}

TEST_CASE("editor resources: treasures under open ground, deer in a forest, at most 8") {
    Island island;
    using editor::Resource;
    using editor::ResourceRefusal;
    const auto fits = [&](sim::TilePos t, Resource r) {
        return editor::resource_fits(island.world, island.buildings, 0, t, r);
    };
    CHECK(fits({15, 15}, Resource::Treasure) == ResourceRefusal::None);
    CHECK(fits({3, 11}, Resource::Treasure) == ResourceRefusal::NoGround);
    CHECK(fits({3, 3}, Resource::Treasure) == ResourceRefusal::NoGround);
    CHECK(fits({15, 15}, Resource::Deer) == ResourceRefusal::NoForest);

    auto treasure = editor::place_resource(island.world, island.buildings, 0, {15, 15}, Resource::Treasure);
    REQUIRE(treasure.ok());
    (*treasure)->apply(island.world);
    const auto& deposits = island.world.islands()[0].header.deposits;
    REQUIRE(deposits.size() == 1);
    CHECK(deposits[0].ware == assets::ware::kTreasure);
    CHECK(deposits[0].x == 15);
    CHECK(deposits[0].amount == editor::kTreasureAmount);
    CHECK(fits({15, 15}, Resource::Treasure) == ResourceRefusal::Taken);

    for (std::int32_t i = 0; i < 8; ++i) {
        const sim::TilePos t{2 + i % 4, 10 + i / 4};
        auto deer = editor::place_resource(island.world, island.buildings, 0, t, Resource::Deer);
        REQUIRE(deer.ok());
        (*deer)->apply(island.world);
    }
    CHECK(fits({2, 10}, Resource::Deer) == ResourceRefusal::Taken);
    CHECK(fits({5, 13}, Resource::Deer) == ResourceRefusal::Full);
    CHECK(island.world.islands()[0].deer_spots[7].used);
    CHECK(island.world.islands()[0].deer_spots[7].x == 5);

    auto deer_gone = editor::remove_resource(island.world, island.buildings, 0, {3, 10});
    REQUIRE(deer_gone.ok());
    (*deer_gone)->apply(island.world);
    CHECK(!island.world.islands()[0].deer_spots[1].used);
    CHECK(fits({5, 13}, Resource::Deer) == ResourceRefusal::None);
    auto treasure_gone = editor::remove_resource(island.world, island.buildings, 0, {15, 15});
    REQUIRE(treasure_gone.ok());
    (*treasure_gone)->apply(island.world);
    CHECK(island.world.islands()[0].header.deposits.empty());
}
