// SPDX-License-Identifier: GPL-3.0-or-later
// The circles a clicked building lights (engine/app/src/catchment_view.hpp): its own, and clicked
// again those of all the player's buildings of its type on the island.

#include <vector>

#include "../test_framework.hpp"
#include "catchment_view.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/production.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kChapel = 20201;
constexpr std::uint32_t kTavern = 20202;
constexpr std::uint32_t kTower = 20203;
constexpr std::uint32_t kHouse = 20204;
constexpr std::uint32_t kKontor = 20205;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20201\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 18\n    Radius: 4\n  EndObj\n"
        "  @Nummer: 2\n  Id: 20202\n  Kind: 14\n  Gfx: 20\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 17\n    Radius: 4\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20203\n  Kind: 14\n  Gfx: 30\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 16\n    Radius: 5\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20204\n  Kind: 14\n  Gfx: 40\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    Radius: 8\n  EndObj\n"
        "  @Nummer: 5\n  Id: 20205\n  Kind: 14\n  Gfx: 50\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of 40 x 10 tiles of the player 0, with chapels at x = 0, 10 and 20 (the one at 20
/// another player's), a tavern at 30, towers at x = 5 and 15 and a house at 25, all in row 2.
/// Chapels, taverns and houses are listed among the productions, towers are not. Player 1's city 0
/// has its Kontor at x = 34.
sim::World make_world(const assets::BuildingTable& buildings) {
    sim::World world{100, 100};
    std::vector<sim::Player> players(2);
    world.set_players(players);
    sim::IslandMap map(40, 10);
    for (std::int32_t y = 0; y < 10; ++y) {
        for (std::int32_t x = 0; x < 40; ++x) {
            REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0, 0, 0).ok());
        }
    }
    map.capture_ground();
    const auto put = [&](std::uint32_t id, std::int32_t x, sim::PlayerId owner, bool listed) {
        REQUIRE(map.place(*buildings.by_id(id), x, 2, 0, 0, owner, 0).ok());
        if (listed) {
            sim::Production p;
            p.island = 0;
            p.x = static_cast<std::uint16_t>(x);
            p.y = 2;
            world.set_production(p);
        }
    };
    put(kChapel, 0, 0, true);
    put(kChapel, 10, 0, true);
    put(kChapel, 20, 1, true);
    put(kTavern, 30, 0, true);
    put(kTower, 5, 0, false);
    put(kTower, 15, 0, false);
    put(kHouse, 25, 0, true);
    REQUIRE(map.place(*buildings.by_id(kKontor), 34, 2, 0, 0, 1, 0).ok());
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
    sim::City city;
    city.island = 0;
    city.index = 0;
    city.owner = 1;
    city.warehouse_x = 34;
    city.warehouse_y = 2;
    world.add_city(std::move(city));
    return world;
}

}  // namespace

TEST_CASE("catchment view: one click lights the clicked building's circle only") {
    const assets::BuildingTable buildings = make_buildings();
    const sim::World world = make_world(buildings);
    const auto c = app::catchment_circles(world, buildings, 0, 0, 10, 2, false);
    REQUIRE(c.has_value());
    CHECK(c->type == buildings.by_id(kChapel));
    REQUIRE(c->circles.size() == 1);
    CHECK(c->circles[0].centre.x == 10);
    CHECK(c->circles[0].radius == 4);
}

TEST_CASE("catchment view: clicked again, all the player's buildings of its type on the island") {
    const assets::BuildingTable buildings = make_buildings();
    const sim::World world = make_world(buildings);
    const auto c = app::catchment_circles(world, buildings, 0, 0, 10, 2, true);
    REQUIRE(c.has_value());
    // The clicked chapel first, then the player's other chapel; not the other player's chapel,
    // not the tavern.
    REQUIRE(c->circles.size() == 2);
    CHECK(c->circles[0].centre.x == 10);
    CHECK(c->circles[1].centre.x == 0);
    REQUIRE(c->origins.size() == 2);
    CHECK((c->origins[0] == sim::TilePos{10, 2}));
    CHECK((c->origins[1] == sim::TilePos{0, 2}));
    // A tavern has no other tavern.
    const auto tavern = app::catchment_circles(world, buildings, 0, 0, 30, 2, true);
    REQUIRE(tavern.has_value());
    CHECK(tavern->circles.size() == 1);
}

TEST_CASE("catchment view: towers, which are not listed among the productions, are found too") {
    const assets::BuildingTable buildings = make_buildings();
    const sim::World world = make_world(buildings);
    const auto c = app::catchment_circles(world, buildings, 0, 0, 15, 2, true);
    REQUIRE(c.has_value());
    REQUIRE(c->circles.size() == 2);
    CHECK(c->circles[0].centre.x == 15);
    CHECK(c->circles[1].centre.x == 5);
}

TEST_CASE("catchment view: nothing for houses, other players' buildings and empty tiles") {
    const assets::BuildingTable buildings = make_buildings();
    const sim::World world = make_world(buildings);
    CHECK(!app::catchment_circles(world, buildings, 0, 0, 25, 2, true).has_value());  // a house
    CHECK(!app::catchment_circles(world, buildings, 0, 0, 20, 2, true).has_value());  // player 1's chapel
    CHECK(app::catchment_circles(world, buildings, 1, 0, 20, 2, true).has_value());   // ... lit for player 1
    CHECK(!app::catchment_circles(world, buildings, 0, 0, 3, 7, true).has_value());   // open ground
    CHECK(!app::catchment_circles(world, buildings, 0, 5, 10, 2, true).has_value());  // no such island
}

TEST_CASE("catchment view: several lit circles light the land inside any of them") {
    sim::Catchment a;
    a.centre = sim::TilePos{10, 10};
    a.radius = 2;
    sim::Catchment b = a;
    b.centre = sim::TilePos{20, 10};
    render::TileLighting l = render::TileLighting::circle(0, a);
    l.add_circle(b);
    CHECK(l.circles() == 2);
    sim::MapCell own;
    own.sprite = 100;
    own.owner = 0;
    CHECK(!l.dimmed(10, 10, &own));
    CHECK(!l.dimmed(21, 11, &own));
    CHECK(l.dimmed(15, 10, &own));  // between the circles
    CHECK(l.in_circle(14, 9, 7, 1));
    CHECK(!l.in_circle(14, 9, 3, 1));
}

TEST_CASE("catchment view: the harbours of several buildings keep the water lit around each") {
    const assets::BuildingTable buildings = make_buildings();
    const sim::World world = make_world(buildings);  // the island at world tiles (10, 10)
    sim::Catchment a;
    a.centre = sim::TilePos{11, 13};
    a.radius = 1;
    render::TileLighting l = render::TileLighting::circle(0, a);
    CHECK(l.dimmed(10, 5, nullptr));  // the sea north of the island, 5 rows above the chapel at x 0
    l.harbour(world, buildings, 0, 0, 2);
    CHECK(!l.dimmed(10, 9, nullptr));  // within 5 tiles of the chapel at x 0
    CHECK(l.dimmed(20, 9, nullptr));   // not near the chapel at x 0
    l.harbour(world, buildings, 0, 10, 2);
    CHECK(!l.dimmed(10, 9, nullptr));  // both
    CHECK(!l.dimmed(20, 9, nullptr));
}

TEST_CASE("catchment view: another owner's Kontor with a trade treaty lights the water its ships trade at") {
    const assets::BuildingTable buildings = make_buildings();
    sim::World world = make_world(buildings);
    // No trade treaty between the two players: nothing.
    world.diplomacy().relation(0, 1).trade = sim::Treaty::None;
    CHECK(!app::foreign_trade_kontor(world, buildings, 0, 0, 34, 2).has_value());
    // With one (as with the natives from the start of a game): any tile of the Kontor gives its origin.
    world.diplomacy().relation(0, 1).trade = sim::Treaty::Signed;
    const auto kontor = app::foreign_trade_kontor(world, buildings, 0, 0, 35, 3);
    REQUIRE(kontor.has_value());
    CHECK((*kontor == sim::TilePos{34, 2}));
    // Not the owner's own Kontor (its circle does that), not other buildings, not open ground.
    CHECK(!app::foreign_trade_kontor(world, buildings, 1, 0, 34, 2).has_value());
    CHECK(!app::foreign_trade_kontor(world, buildings, 0, 0, 20, 2).has_value());  // player 1's chapel
    CHECK(!app::foreign_trade_kontor(world, buildings, 0, 0, 3, 7).has_value());
    CHECK(!app::foreign_trade_kontor(world, buildings, sim::kNoPlayer, 0, 34, 2).has_value());

    // Lit: only the water within the ships' reach of the Kontor (the island at world tiles (10, 10)).
    render::TileLighting l = render::TileLighting::harbours_only(0);
    l.harbour(world, buildings, 0, static_cast<std::uint32_t>(kontor->x), static_cast<std::uint32_t>(kontor->y));
    CHECK(!l.dimmed(44, 9, nullptr));  // the sea just north of the Kontor
    CHECK(l.dimmed(20, 9, nullptr));   // the sea far from it
    sim::MapCell own;
    own.sprite = 100;
    own.owner = 0;
    CHECK(l.dimmed(10, 12, &own));  // the player's land stays dark
}
