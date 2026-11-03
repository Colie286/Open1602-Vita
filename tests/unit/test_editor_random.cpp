// SPDX-License-Identifier: GPL-3.0-or-later
// The random elements of a scenario's world plan in the editor (random_elements.hpp).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/editor/random_elements.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/sim/world_generator.hpp"

using namespace o1602;
using Kind = editor::RandomElement::Kind;

namespace {

/// Three buildings naming iron ore, tobacco and jewellery, as the plan's entries do.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 2\n  EndObj\n"
        "  @Nummer: 1\n  Id: 20102\n  Kind: 11\n  Gfx: 1\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 46\n  EndObj\n"
        "  @Nummer: 2\n  Id: 20103\n  Kind: 11\n  Gfx: 2\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 21\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("editor random: the plan's entries, x at random beside y on the map, and their numbers changed") {
    const assets::BuildingTable buildings = make_buildings();
    sim::World world(100, 100);
    CHECK(editor::random_elements(world, buildings).empty());
    CHECK(!editor::set_random_count(world, Kind::Ore, 0, 1).ok());

    sim::PlannedIslands planned;
    planned.plan.natives = {2, 1};
    planned.plan.ores[0] = {20101, 3, 0};  // big iron veins
    planned.plan.ores[1] = {20101, 5, 1};  // small ones
    planned.plan.crops[0] = {20102, 4, 0};
    planned.plan.treasures[0] = {20103, 6, 0};
    world.set_planned_islands(planned);
    sim::IslandMap map(4, 4);
    for (std::int32_t y = 0; y < 4; ++y) {
        for (std::int32_t x = 0; x < 4; ++x) {
            REQUIRE(map.place(*buildings.by_id(20101), x, y, 0, 0).ok());
        }
    }
    map.capture_ground();
    assets::IslandHeader header;
    header.width = 4;
    header.height = 4;
    assets::IslandHeader::Deposit big;
    big.ware = 2;
    big.kind = 0;
    header.deposits.push_back(big);
    header.fertility = 1U << (46 - 45);
    REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), 40, 30}).ok());

    const auto list = editor::random_elements(world, buildings);
    REQUIRE(list.size() == 6);
    CHECK(list[0].kind == Kind::Natives);
    CHECK(list[0].random == 2);
    CHECK(list[1].random == 1);
    CHECK(list[2].kind == Kind::Ore);
    CHECK(list[2].random == 3);
    CHECK(list[2].fixed == 1);  // the big vein on the island
    CHECK(list[3].type == 1);
    CHECK(list[3].fixed == 0);
    CHECK(list[4].kind == Kind::Crop);
    CHECK(list[4].ware == 46);
    CHECK(list[4].fixed == 1);
    CHECK(list[5].kind == Kind::Treasure);
    CHECK(list[5].random == 6);

    auto op = editor::set_random_count(world, Kind::Ore, 1, 7);
    REQUIRE(op.ok());
    (*op)->apply(world);
    CHECK(world.planned_islands()->plan.ores[1].count == 7);
    auto villages = editor::set_random_count(world, Kind::Natives, 1, 0);
    REQUIRE(villages.ok());
    (*villages)->apply(world);
    CHECK(world.planned_islands()->plan.natives[1] == 0);
    (*villages)->revert(world);
    CHECK(world.planned_islands()->plan.natives[1] == 1);
    CHECK(!editor::set_random_count(world, Kind::Crop, 5, 1).ok());  // names nothing
    CHECK(!editor::set_random_count(world, Kind::Crop, 0, editor::kMaxRandom + 1).ok());
}
