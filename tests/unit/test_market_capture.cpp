// SPDX-License-Identifier: GPL-3.0-or-later
// Taking a market (Open1602): a market built over the destroyed market of another player's city
// joins the builder's city and takes the land the destroyed one held, with what stands on it.
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kHouse = 20103;
constexpr std::uint32_t kKontor = 20108;
constexpr std::uint32_t kMarket = 20120;
constexpr std::uint32_t kMarketDestroyed = 20121;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const std::string text =
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"  // open ground
        "  @Nummer: 1\n  Id: 20103\n  Kind: 14\n  Gfx: 2\n  Size: 2, 2\n  Maxenergy: 20\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n  EndObj\n"  // a house
        "  @Nummer: 2\n  Id: 20108\n  Kind: 35\n  Gfx: 30\n  Size: 2, 2\n  Maxenergy: 20\n"
        "  KreuzBase: 6\n  Ausbauflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"  // Kontor
        "  @Nummer: 3\n  Id: 20120\n  Kind: 14\n  Gfx: 40\n  Size: 2, 2\n  Maxenergy: 115\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 8\n  EndObj\n"  // market
        "  @Nummer: 4\n  Id: 20121\n  Kind: 14\n  Gfx: 50\n  Size: 2, 2\n  Destroyflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 8\n  EndObj\n"  // destroyed market
        "EndObj\n";
    const auto doc = assets::cod::parse(text, types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of 40 x 20: player 0's city 0 in the west (its Kontor at 2, 5, land up to x = `own_x`),
/// player 1's city 1 in the east (its Kontor at 34, 5) with a destroyed market at 20, 5 and a
/// house at 18, 9 on the land only that market held.
struct Island {
    assets::BuildingTable buildings = make_buildings();
    sim::World world{80, 60};

    explicit Island(std::int32_t own_x = 12, std::uint32_t ruin = kMarketDestroyed) {
        std::vector<sim::Player> players(2);
        players[0].role = sim::PlayerRole::Human;
        players[0].money = 10000;
        players[1].role = sim::PlayerRole::Computer;
        world.set_players(players);
        sim::IslandMap map(40, 20);
        for (std::int32_t y = 0; y < 20; ++y) {
            for (std::int32_t x = 0; x < 40; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
        for (const sim::PlayerId owner : {sim::PlayerId{0}, sim::PlayerId{1}}) {
            sim::City c;
            c.island = 0;
            c.index = static_cast<std::uint8_t>(owner);
            c.owner = owner;
            c.warehouse_x = static_cast<std::uint16_t>(owner == 0 ? 2 : 34);
            c.warehouse_y = 5;
            world.add_city(c);
        }
        sim::IslandMap& m = world.island(0).map;
        REQUIRE(m.place(*buildings.by_id(kKontor), 2, 5, 0, 0, 0, 0).ok());
        REQUIRE(m.place(*buildings.by_id(kKontor), 34, 5, 0, 0, 1, 1).ok());
        REQUIRE(m.place(*buildings.by_id(ruin), 20, 5, 0, 0, 1, 1).ok());
        REQUIRE(m.place(*buildings.by_id(kHouse), 18, 9, 0, 0, 1, 1).ok());
        for (std::uint32_t y = 0; y < 20; ++y) {
            for (std::uint32_t x = 0; x < 40; ++x) {
                const bool west = static_cast<std::int32_t>(x) <= own_x;
                const bool east = x >= 13;
                if (west) {
                    m.set_city(x, y, 0, 0);
                } else if (east) {
                    m.set_city(x, y, 1, 1);
                }
            }
        }
    }

    [[nodiscard]] sim::BuildOrder market_order(std::int32_t x = 21, std::int32_t y = 6) const {
        sim::BuildOrder order;
        order.player = 0;
        order.island = 0;
        order.items = {sim::BuildItem{kMarket, x, y, 0, 0}};
        return order;
    }
};

}  // namespace

TEST_CASE("market capture: a market over another player's destroyed market takes its land and buildings") {
    Island f;
    const sim::BuildPlan preview = sim::plan_build(f.world, f.buildings, f.market_order());
    CHECK(preview.captured_city == 1);
    CHECK(preview.city == 0);
    REQUIRE(preview.placed == 1);
    CHECK(preview.items[0].x == 20);  // where the destroyed one stands, whichever tile was clicked
    CHECK(preview.items[0].y == 5);

    std::vector<sim::ConstructionEvent> events;
    const sim::BuildPlan plan = sim::execute_build(f.world, f.buildings, f.market_order(), {}, &events);
    REQUIRE(plan.placed == 1);
    const sim::IslandMap& m = f.world.islands()[0].map;
    CHECK(m.top(20, 5).city == 0);
    CHECK(m.top(20, 5).owner == 0);
    CHECK(f.buildings.by_sprite(m.top(20, 5).sprite)->id == kMarket);
    // The house on the land only the destroyed market held goes along.
    CHECK(m.top(18, 9).city == 0);
    CHECK(m.top(18, 9).owner == 0);
    CHECK(f.buildings.by_sprite(m.top(18, 9).sprite)->id == kHouse);
    CHECK(m.top(15, 2).city == 0);
    // What the other city's Kontor holds stays its own, and its city goes on.
    CHECK(m.top(30, 5).city == 1);
    CHECK(m.top(30, 5).owner == 1);
    REQUIRE(f.world.city(0, 1) != nullptr);
    CHECK(f.world.city(0, 1)->owner == 1);
    CHECK(f.world.city(0, 0)->owner == 0);
}

TEST_CASE("market capture: only over a destroyed market, and only where the builder's land reaches") {
    // A standing market is another player's: no building there.
    Island standing(12, kMarket);
    const sim::BuildPlan over_standing = sim::plan_build(standing.world, standing.buildings, standing.market_order());
    CHECK(over_standing.placed == 0);
    CHECK(over_standing.captured_city == sim::MapCell::kNoCity);
    CHECK(over_standing.items[0].refusal == sim::BuildRefusal::ForeignCity);

    // The builder's land ends far from the destroyed market's circle.
    Island far(3);
    const sim::BuildPlan out_of_reach = sim::plan_build(far.world, far.buildings, far.market_order());
    CHECK(out_of_reach.placed == 0);
    CHECK(out_of_reach.captured_city == sim::MapCell::kNoCity);

    // The owner itself rebuilds its market as usual, nothing is captured.
    Island own;
    sim::BuildOrder rebuild = own.market_order();
    rebuild.player = 1;
    const sim::BuildPlan by_owner = sim::plan_build(own.world, own.buildings, rebuild);
    CHECK(by_owner.captured_city == sim::MapCell::kNoCity);
}
