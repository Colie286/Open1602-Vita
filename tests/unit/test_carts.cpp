// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <string>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/diplomacy.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint8_t kWood = 23;
constexpr std::uint32_t kTon = 32;
constexpr std::uint32_t kGround = 20101;  // Wegspeed 200 % for the cart (Speedtyp 2)
constexpr std::uint32_t kRoad = 20102;
constexpr std::uint32_t kKontor = 20103;
constexpr std::uint32_t kMarket = 20104;
constexpr std::uint32_t kHouse = 20105;
constexpr std::uint32_t kGate = 20106;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Wegspeed: 100, 100, 200, 100\n"
        "  @Nummer: 1\n  Id: 20102\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20103\n  Kind: 14\n  Gfx: 2\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20104\n  Kind: 14\n  Gfx: 6\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20105\n  Kind: 14\n  Gfx: 10\n  Size: 1, 1\n"
        "  @Nummer: 5\n  Id: 20106\n  Kind: 3\n  Gfx: 11\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Speed: 500\n  Maxenergy: 50\n"
        "  Nummer: 37\n  Gfx: 0\n  Blocknr: 3\n  Rotate: 1\n  Maxware: 3\n  Speed: 300\n  Speedtyp: 2\n"
        "  Maxenergy: 14\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of ground, 30 x 12 tiles at world (10, 10): player 0's city 0 in its west half with
/// the Kontor at island (0, 4) and a market at (12, 4), player 1's city 1 in the east half with the
/// Kontor at (27, 4). A road runs along island row 10. A second island of 4 x 4 lies at (50, 10).
struct Land {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{70, 40};
    sim::Rules rules = sim::Rules::open1602();

    Land() {
        std::vector<sim::Player> players(3);
        players[0].role = sim::PlayerRole::Human;
        players[0].money = 10000;
        players[1].role = sim::PlayerRole::Computer;
        players[1].money = 10000;
        players[2].role = sim::PlayerRole::Human;
        world.set_players(players);
        sim::IslandMap map(30, 12);
        for (std::int32_t y = 0; y < 12; ++y) {
            for (std::int32_t x = 0; x < 30; ++x) {
                REQUIRE(map.place(*buildings.by_id(y == 10 ? kRoad : kGround), x, y, 0, 0).ok());
                const auto half = static_cast<std::uint8_t>(x < 20 ? 0 : 1);
                map.set_city(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), half, half);
            }
        }
        REQUIRE(map.place(*buildings.by_id(kKontor), 0, 4, 0, 0, 0, 0).ok());
        REQUIRE(map.place(*buildings.by_id(kMarket), 12, 4, 0, 0, 0, 0).ok());
        REQUIRE(map.place(*buildings.by_id(kKontor), 27, 4, 0, 0, 1, 1).ok());
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
        sim::IslandMap small(4, 4);
        for (std::int32_t y = 0; y < 4; ++y) {
            for (std::int32_t x = 0; x < 4; ++x) {
                REQUIRE(small.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(small), 50, 10}).ok());
        add_city(0, 0, 0, 4, "West");
        add_city(1, 1, 27, 4, "East");
    }

    void add_city(std::uint8_t index, sim::PlayerId owner, std::uint32_t x, std::uint32_t y, const char* name) {
        sim::City city;
        city.island = 0;
        city.index = index;
        city.owner = owner;
        city.name = name;
        city.warehouse_x = x;
        city.warehouse_y = y;
        city.capacity = 60 * kTon;
        city.ensure_ware(24);
        city.trade.resize(25);
        world.add_city(city);
    }

    /// A wall of houses across the island at island column 19, with a gate of player 1 in row 6.
    void wall() {
        sim::IslandMap& map = world.island(0).map;
        for (std::int32_t y = 0; y < 12; ++y) {
            REQUIRE(map.place(*buildings.by_id(y == 6 ? kGate : kHouse), 19, y, 0, 0, 1, 1).ok());
        }
    }
};

std::vector<sim::ShipEvent> run(sim::Fleet& fleet, sim::World& world, std::uint32_t ms) {
    std::vector<sim::ShipEvent> events;
    for (std::uint32_t t = 0; t < ms; t += 50) {
        world.set_time_ms(world.time_ms() + 50);
        fleet.tick(50);
        for (const auto& e : fleet.take_events()) {
            events.push_back(e);
        }
    }
    return events;
}

std::size_t count(const std::vector<sim::ShipEvent>& events, sim::ShipEvent::Kind kind) {
    return static_cast<std::size_t>(
        std::count_if(events.begin(), events.end(), [&](const sim::ShipEvent& e) { return e.kind == kind; }));
}

}  // namespace

TEST_CASE("carts: a city's owner gets one cart per island at the Kontor, for nothing") {
    Land land;
    sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
    fleet.set_ship_names({"Altair", "Beteigeuze"});
    // Not for a city of another player, not to the Kontor itself, not to the sea or another island.
    CHECK(fleet.request_cart(sim::CartRequest{2, 0, 0, 20, 18}) == 0);
    CHECK(fleet.request_cart(sim::CartRequest{0, 0, 0, 10, 14}) == 0);
    CHECK(fleet.request_cart(sim::CartRequest{0, 0, 0, 5, 5}) == 0);
    CHECK(fleet.request_cart(sim::CartRequest{0, 0, 0, 51, 11}) == 0);
    const std::uint32_t id = fleet.request_cart(sim::CartRequest{0, 0, 0, 20, 18});
    REQUIRE(id != 0);
    const sim::Ship* cart = land.world.ship(id);
    REQUIRE(cart != nullptr);
    CHECK(cart->cart());
    CHECK(cart->x == 11);  // the middle of the 2 x 2 Kontor at (10, 14)
    CHECK(cart->y == 15);
    CHECK(cart->owner == 0);
    CHECK(sim::usable_holds(*cart, land.figures) == 3);
    CHECK(cart->health == 14 * 32);
    CHECK(cart->target == sim::Target::position(20, 18));
    CHECK(land.world.players()[0].money == 10000);
    CHECK(fleet.cart_of(0, 0) == cart);
    // One per island; it is no ship of the fleet, and ships do not sail to it.
    CHECK(fleet.request_cart(sim::CartRequest{0, 0, 0, 22, 18}) == 0);
    CHECK(fleet.fleet_size(0) == 0);
    land.world.add_ship(sim::make_ship("Kogge", 5, 5, 21, 0));
    const std::uint32_t kogge = land.world.ships().back().id;
    CHECK(!fleet.order(sim::ShipOrder{0, kogge, sim::Target::other_ship(id)}));
    // The other player gets their own.
    CHECK(fleet.request_cart(sim::CartRequest{1, 0, 1, 25, 18}) != 0);
}

TEST_CASE("ships: a new ship's name passes over the names ships have already") {
    Land land;
    sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
    fleet.set_ship_names({"Altair", "Beteigeuze", "Capella"});
    land.world.add_ship(sim::make_ship("Altair", 5, 5, 21, 0));
    land.world.add_ship(sim::make_ship("", 6, 5, 21, 0));
    land.world.add_ship(sim::make_ship("", 7, 5, 21, 0));
    fleet.name_unnamed_ships();
    const auto& ships = land.world.ships();
    // The two new ones take the names no ship has, each its own.
    const std::string a = ships[ships.size() - 2].name;
    const std::string b = ships[ships.size() - 1].name;
    CHECK(a != "Altair");
    CHECK(b != "Altair");
    CHECK(a != b);
    CHECK((a == "Beteigeuze" || a == "Capella"));
    CHECK((b == "Beteigeuze" || b == "Capella"));
}

TEST_CASE("ships: a trader's ship a map put there gets a name at the start, as the players' do") {
    Land land;
    std::vector<sim::Player> players = land.world.players();
    players.resize(3);
    sim::make_neutral(players[2], sim::faction::kTrader);
    land.world.set_players(players);
    sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
    fleet.set_ship_names({"Altair"});
    sim::Ship trader = sim::make_ship("", 6, 5, assets::figure::kTrader, 2);
    trader.trader = true;
    land.world.add_ship(std::move(trader));
    fleet.name_unnamed_ships();
    CHECK(land.world.ships().back().name == "Altair");
}

TEST_CASE("carts: asked for in a market's window, the cart comes out of that market") {
    // The market at island (12, 4), world (22, 14): its middle is world (23, 15).
    Land land;
    sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
    const std::uint32_t id = fleet.request_cart(sim::CartRequest{0, 0, 0, 25, 18, 22, 14});
    REQUIRE(id != 0);
    const sim::Ship* cart = land.world.ship(id);
    CHECK(cart->x == 23);
    CHECK(cart->y == 15);
    CHECK(cart->target == sim::Target::position(25, 18));
    run(fleet, land.world, 1500);
    CHECK(land.world.ship(id)->y > 15);  // on its way out
}

TEST_CASE("carts: a market of another city or a tile without a market sends the cart out of the Kontor") {
    for (const auto& [fx, fy] : {std::pair{37, 14}, std::pair{15, 15}}) {
        Land land;
        sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
        const auto from_x = static_cast<std::uint16_t>(fx);
        const auto from_y = static_cast<std::uint16_t>(fy);
        const std::uint32_t id = fleet.request_cart(sim::CartRequest{0, 0, 0, 20, 18, from_x, from_y});
        REQUIRE(id != 0);
        CHECK(land.world.ship(id)->x == 11);  // the middle of the Kontor at (10, 14)
        CHECK(land.world.ship(id)->y == 15);
    }
}

TEST_CASE("carts: with Shift a player gets another cart, one per Kontor and market on the island (Open1602)") {
    for (const bool more : {true, false}) {
        Land land;
        land.rules.more_carts = more;
        // Player 0's Kontor and market, player 1's Kontor.
        for (const auto& [x, y] : {std::pair{0, 4}, std::pair{12, 4}, std::pair{27, 4}}) {
            sim::Production p;
            p.x = static_cast<std::uint16_t>(x);
            p.y = static_cast<std::uint16_t>(y);
            land.world.productions().push_back(p);
        }
        CHECK(sim::cart_places(land.world, land.buildings, 0, 0) == 2);
        CHECK(sim::cart_places(land.world, land.buildings, 1, 0) == 1);
        sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
        CHECK(fleet.request_cart(sim::CartRequest{0, 0, 0, 20, 18}) != 0);
        sim::CartRequest another{0, 0, 0, 21, 18, 22, 14};
        CHECK(fleet.request_cart(sim::CartRequest{0, 0, 0, 21, 18}) == 0);  // without Shift: one
        another.another = true;
        CHECK((fleet.request_cart(another) != 0) == more);
        CHECK(sim::trade_carts_of(land.world, 0, 0).size() == (more ? 2U : 1U));
        CHECK(fleet.request_cart(another) == 0);  // no third: a Kontor and a market
        sim::CartRequest east{1, 0, 1, 25, 18};
        CHECK(fleet.request_cart(east) != 0);
        east.another = true;
        CHECK(fleet.request_cart(east) == 0);  // a Kontor only
    }
}

TEST_CASE("carts: the rule for more carts is kept in the savegame, off only under its own key") {
    const assets::BuildingTable buildings = make_buildings();
    for (const bool more : {false, true}) {
        sim::World world(60, 40);
        sim::Rules rules;
        rules.more_carts = more;
        world.set_saved_rules(rules);
        auto bytes = sim::write_game(world, buildings);
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, buildings);
        REQUIRE(loaded.ok());
        REQUIRE(loaded->saved_rules().has_value());
        CHECK(loaded->saved_rules()->more_carts == more);
    }
    CHECK(!sim::Rules::original().more_carts);
}

TEST_CASE("carts: a new cart leaves the Kontor from its middle even where houses close one side") {
    // The Kontor at island (0, 4) on the west coast; houses on its east and south side leave only
    // the way north, which the middle tile (1, 5) reaches through the Kontor itself.
    Land land;
    sim::IslandMap& map = land.world.island(0).map;
    for (const auto& [x, y] : {std::pair{2, 4}, std::pair{2, 5}, std::pair{2, 6}, std::pair{0, 6}, std::pair{1, 6}}) {
        REQUIRE(map.place(*land.buildings.by_id(kHouse), x, y, 0, 0, 0, 0).ok());
    }
    sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
    const std::uint32_t id = fleet.request_cart(sim::CartRequest{0, 0, 0, 15, 11});
    REQUIRE(id != 0);
    const auto events = run(fleet, land.world, 1500);
    CHECK(count(events, sim::ShipEvent::Kind::GaveUp) == 0);
    const sim::Ship* cart = land.world.ship(id);
    CHECK(cart->y < 14);  // out of the Kontor (island rows 4 and 5) to the north within 1.5 s
    CHECK(cart->target == sim::Target::position(15, 11));
}

TEST_CASE("carts: a cart drives over land to the clicked tile, faster on roads") {
    Land land;
    sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
    const std::uint32_t id = fleet.request_cart(sim::CartRequest{0, 0, 0, 18, 15});
    REQUIRE(id != 0);
    // From the Kontor's middle one tile at the cost of a building (40) and six tiles of open
    // ground (Wegspeed 64, 6.25 s * 64 / 300 = 1.3 s a tile straight): about 8.8 s.
    auto events = run(fleet, land.world, 8000);
    CHECK(count(events, sim::ShipEvent::Kind::Arrived) == 0);
    events = run(fleet, land.world, 8000);
    CHECK(count(events, sim::ShipEvent::Kind::Arrived) == 1);
    const sim::Ship* cart = land.world.ship(id);
    CHECK(cart->x == 18);
    CHECK(cart->y == 15);
    CHECK(cart->target.empty());
    // Along the road (Wegspeed 32): ten tiles in under 7 s.
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::position(18, 20)}));
    run(fleet, land.world, 9000);
    REQUIRE(land.world.ship(id)->y == 20);
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::position(28, 20)}));
    events = run(fleet, land.world, 7500);
    CHECK(count(events, sim::ShipEvent::Kind::Arrived) == 1);
    CHECK(land.world.ship(id)->x == 28);
    // A house is reached next to it; the sea, another island, attacking and patrols are refused.
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::position(23, 15)}));
    CHECK(!fleet.order(sim::ShipOrder{0, id, sim::Target::position(5, 5)}));
    CHECK(!fleet.order(sim::ShipOrder{0, id, sim::Target::position(51, 11)}));
    sim::ShipOrder attack{0, id, sim::Target::position(24, 15)};
    attack.attack = true;
    CHECK(!fleet.order(attack));
    sim::ShipCommand patrol;
    patrol.player = 0;
    patrol.ship = id;
    patrol.op = sim::ShipCommand::Op::Patrol;
    patrol.x = 24;
    patrol.y = 15;
    CHECK(!fleet.command(patrol));
}

TEST_CASE("carts: at its own market it exchanges goods, three holds of 10 t") {
    Land land;
    sim::City& west = *land.world.city(0, 0);
    west.goods[kWood] = 50 * kTon;
    sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
    const std::uint32_t id = fleet.request_cart(sim::CartRequest{0, 0, 0, 20, 15});
    REQUIRE(id != 0);
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::city_of(0, 0)}));
    // At the Kontor already; the market is in reach from two tiles away.
    CHECK(fleet.transfer(sim::CargoTransfer{0, id, kWood, true, 50 * kTon}) == 30 * kTon);
    CHECK(land.world.ship(id)->load(kWood) == 30 * kTon);
    CHECK(west.goods[kWood] == 20 * kTon);
    // Two tiles from the market: a route stop reaches it, the exchange by hand only from a tile
    // that touches it, as in the original.
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::position(25, 14)}));
    run(fleet, land.world, 20000);
    CHECK(sim::cities_near_cart(land.world, land.buildings, land.world.ship(id)->x, land.world.ship(id)->y) ==
          std::vector<std::size_t>{0});
    CHECK(sim::cities_near_cart(land.world, land.buildings, 25, 14, sim::kCartHandReach).empty());
    CHECK(fleet.transfer(sim::CargoTransfer{0, id, kWood, false, 10 * kTon}) == 0);
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::position(24, 14)}));
    run(fleet, land.world, 20000);
    REQUIRE(land.world.ship(id)->x == 24);
    CHECK(fleet.transfer(sim::CargoTransfer{0, id, kWood, false, 10 * kTon}) == 10 * kTon);
    CHECK(west.goods[kWood] == 30 * kTon);
    // Away from both: nothing.
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::position(17, 20)}));
    run(fleet, land.world, 20000);
    CHECK(sim::cities_near_cart(land.world, land.buildings, 17, 20).empty());
    CHECK(fleet.transfer(sim::CargoTransfer{0, id, kWood, false, 10 * kTon}) == 0);
}

TEST_CASE("carts: a foreign Kontor trades only with a trade treaty, at its prices") {
    Land land;
    sim::City& east = *land.world.city(0, 1);
    east.goods[kWood] = 40 * kTon;
    east.trade[kWood].sell = true;
    east.trade[kWood].sell_price = 10;
    sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
    const std::uint32_t id = fleet.request_cart(sim::CartRequest{0, 0, 0, 20, 15});
    REQUIRE(id != 0);
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::city_of(0, 1)}));
    const auto events = run(fleet, land.world, 40000);
    CHECK(count(events, sim::ShipEvent::Kind::Arrived) == 1);
    // Sent to the city, it stops beside the Kontor, where the window offers the trade.
    CHECK(sim::cities_near_cart(land.world, land.buildings, land.world.ship(id)->x, land.world.ship(id)->y,
                                sim::kCartHandReach) == std::vector<std::size_t>{1});
    CHECK(fleet.transfer(sim::CargoTransfer{0, id, kWood, true, 10 * kTon}) == 0);
    land.world.diplomacy().relation(0, 1).trade = sim::Treaty::Signed;
    land.world.diplomacy().relation(1, 0).trade = sim::Treaty::Signed;
    CHECK(fleet.transfer(sim::CargoTransfer{0, id, kWood, true, 10 * kTon}) == 10 * kTon);
    CHECK(land.world.players()[0].money == 10000 - 100);
    CHECK(east.goods[kWood] == 30 * kTon);
}

TEST_CASE("carts: a trade route between the cities of its island") {
    Land land;
    sim::City& west = *land.world.city(0, 0);
    sim::City& east = *land.world.city(0, 1);
    west.goods[kWood] = 60 * kTon;
    east.trade[kWood].buy = true;
    east.trade[kWood].buy_price = 10;
    east.trade[kWood].limit = 60 * kTon;
    land.world.diplomacy().relation(0, 1).trade = sim::Treaty::Signed;
    land.world.diplomacy().relation(1, 0).trade = sim::Treaty::Signed;
    sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
    const std::uint32_t id = fleet.request_cart(sim::CartRequest{0, 0, 0, 20, 15});
    REQUIRE(id != 0);
    using Op = sim::RouteEdit::Op;
    sim::RouteEdit e{0, id, Op::SetStop, 0, 0, sim::Target::city_of(0, 0)};
    REQUIRE(fleet.edit_route(e));
    e.op = Op::SetEntry;
    e.value = sim::RouteEntry{kWood, false, 30 * kTon};
    REQUIRE(fleet.edit_route(e));
    e = sim::RouteEdit{0, id, Op::SetStop, 1, 0, sim::Target::city_of(0, 1)};
    REQUIRE(fleet.edit_route(e));
    e.op = Op::SetEntry;
    e.value = sim::RouteEntry{kWood, true, 30 * kTon};
    REQUIRE(fleet.edit_route(e));
    // Only cities of its island are stops.
    CHECK(!fleet.edit_route(sim::RouteEdit{0, id, Op::SetStop, 2, 0, sim::Target::position(20, 15)}));
    REQUIRE(fleet.edit_route(sim::RouteEdit{0, id, Op::Start}));
    run(fleet, land.world, 60000);
    CHECK(east.goods[kWood] >= 30 * kTon);
    CHECK(west.goods[kWood] <= 30 * kTon);
    CHECK(land.world.players()[0].money >= 10000 + 300);
}

TEST_CASE("carts: the closed gates of another player stop a cart") {
    Land land;
    land.wall();
    land.world.player(1)->gates_closed = true;
    sim::Fleet fleet(land.world, land.buildings, land.figures, land.rules);
    const std::uint32_t id = fleet.request_cart(sim::CartRequest{0, 0, 0, 34, 15});
    REQUIRE(id != 0);
    auto events = run(fleet, land.world, 20000);
    CHECK(count(events, sim::ShipEvent::Kind::GaveUp) == 1);
    CHECK(land.world.ship(id)->x < 29);
    land.world.player(1)->gates_closed = false;
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::position(34, 15)}));
    events = run(fleet, land.world, 60000);
    CHECK(count(events, sim::ShipEvent::Kind::Arrived) == 1);
    CHECK(land.world.ship(id)->x == 34);
}

TEST_CASE("carts: a game saved while a cart drives and gates are closed runs on the same after loading") {
    // What a computer joining a multiplayer game gets (ADR 0011 point 6): the game must go on there
    // exactly as on the host, so everything of a cart and of the gates has to be in the savegame.
    Land land;
    land.wall();
    for (std::uint16_t i = 0; i < land.world.islands().size(); ++i) {
        sim::WorldIsland& isl = land.world.island(i);
        isl.header.number = i;  // a savegame needs them as a real map has them
        isl.header.width = static_cast<std::uint16_t>(isl.map.width());
        isl.header.height = static_cast<std::uint16_t>(isl.map.height());
        isl.header.pos_x = static_cast<std::uint16_t>(isl.x);
        isl.header.pos_y = static_cast<std::uint16_t>(isl.y);
    }
    sim::City& west = *land.world.city(0, 0);
    sim::City& east = *land.world.city(0, 1);
    west.goods[kWood] = 60 * kTon;
    east.trade[kWood].buy = true;
    east.trade[kWood].buy_price = 10;
    east.trade[kWood].limit = 60 * kTon;
    land.world.diplomacy().relation(0, 1).trade = sim::Treaty::Signed;
    land.world.diplomacy().relation(1, 0).trade = sim::Treaty::Signed;
    sim::Simulation host(land.world, land.buildings, land.figures);
    host.submit(sim::CartRequest{0, 0, 0, 20, 15});
    host.submit(sim::CartRequest{1, 0, 1, 25, 18});
    host.tick();
    const sim::Ship* cart = land.world.ships().empty() ? nullptr : &land.world.ships().front();
    REQUIRE(cart != nullptr);
    REQUIRE(cart->cart());
    const std::uint32_t id = cart->id;
    using Op = sim::RouteEdit::Op;
    host.submit(sim::RouteEdit{0, id, Op::SetStop, 0, 0, sim::Target::city_of(0, 0)});
    host.submit(sim::RouteEdit{0, id, Op::SetEntry, 0, 0, {}, sim::RouteEntry{kWood, false, 30 * kTon}});
    host.submit(sim::RouteEdit{0, id, Op::SetStop, 1, 0, sim::Target::city_of(0, 1)});
    host.submit(sim::RouteEdit{0, id, Op::SetEntry, 1, 0, {}, sim::RouteEntry{kWood, true, 30 * kTon}});
    host.submit(sim::RouteEdit{0, id, Op::Start});
    host.submit(sim::GateOrder{1, true});
    host.tick();  // the commands are applied: queued ones are not part of a savegame
    bool crossed = false;
    for (int step = 0; step < 12; ++step) {
        if (step == 6) {
            host.submit(sim::GateOrder{1, false});
            host.tick();
        }
        auto bytes = sim::write_game(land.world, land.buildings, &host, sim::SaveOptions{false});
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, land.buildings, nullptr, &land.figures);
        REQUIRE(loaded.ok());
        sim::World world = std::move(loaded).value();
        sim::Simulation joined(world, land.buildings, land.figures);
        CHECK(!joined.state_damaged());
        CHECK(joined.state_hash() == host.state_hash());
        for (int t = 0; t < 211; ++t) {
            host.tick();
            joined.tick();
        }
        CHECK(joined.state_hash() == host.state_hash());
        crossed = crossed || land.world.ship(id)->x > 29;  // east of the wall, through the opened gate
    }
    CHECK(crossed);
}
