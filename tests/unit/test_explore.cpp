// SPDX-License-Identifier: GPL-3.0-or-later
// "Insel erkunden".

#include <algorithm>
#include <memory>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint16_t kTrade = 21;  // HANDEL1

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
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
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Speed: 500\n  Drehtime: 0.13\n"
        "  Maxenergy: 50\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// Open sea with an island of 10 x 8 land tiles at (40, 20) inside a rectangle of 14 x 12 (the
/// outer ring is water, as at a real coast). Two iron deposits and three gold veins.
struct Coast {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{100, 60};
    sim::Rules rules = sim::Rules::open1602();
    std::unique_ptr<sim::Fleet> fleet;
    std::vector<sim::ShipEvent> events;

    Coast() {
        std::vector<sim::Player> players(2);
        players[0].role = sim::PlayerRole::Human;
        players[1].role = sim::PlayerRole::Computer;
        world.set_players(players);
        sim::IslandMap map(14, 12);
        for (std::int32_t y = 2; y < 10; ++y) {
            for (std::int32_t x = 2; x < 12; ++x) {
                REQUIRE(map.place(*buildings.by_id(20101), x, y, 0, 0, 1, 0).ok());
            }
        }
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), 40, 20};
        auto deposit = [](std::uint8_t ware, std::uint8_t x, std::uint8_t y) {
            assets::IslandHeader::Deposit d;
            d.ware = ware;
            d.x = x;
            d.y = y;
            d.amount = 1000;
            return d;
        };
        island.header.deposits = {deposit(2, 3, 3), deposit(3, 5, 3), deposit(2, 7, 3), deposit(3, 9, 3),
                                  deposit(3, 3, 7)};
        REQUIRE(world.add_island(std::move(island)).ok());
        fleet = std::make_unique<sim::Fleet>(world, buildings, figures, rules);
    }

    std::uint32_t add(std::uint32_t x, std::uint32_t y, sim::PlayerId owner = 0) {
        sim::Ship s = sim::make_ship("Ship", x, y, kTrade, owner);
        s.cargo.resize(4);
        world.add_ship(s);
        return world.ships().back().id;
    }

    void run(std::uint32_t ms) {
        for (std::uint32_t t = 0; t < ms; t += 50) {
            world.set_time_ms(world.time_ms() + 50);
            fleet->tick(50);
            for (const auto& e : fleet->take_events()) {
                events.push_back(e);
            }
        }
    }

    [[nodiscard]] std::size_t count(sim::ShipEvent::Kind kind) const {
        return static_cast<std::size_t>(
            std::count_if(events.begin(), events.end(), [&](const auto& e) { return e.kind == kind; }));
    }
    [[nodiscard]] std::size_t found(std::uint8_t ware) const {
        return static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const auto& e) {
            return e.kind == sim::ShipEvent::Kind::DepositFound && e.ware == ware;
        }));
    }
};

}  // namespace

TEST_CASE("explore: crops after 3 s, an ore deposit at 7.5 s and 10.5 s, two gold veins at 15 s") {
    Coast c;
    const std::uint32_t id = c.add(41, 21);  // in the water ring, inside the island's rectangle
    const sim::Ship& ship = *c.world.ship(id);
    CHECK(c.fleet->explore_state(ship, 0) == sim::ExploreState::Possible);
    CHECK(c.fleet->explore_state(ship, 1) == sim::ExploreState::NotAllowed);
    CHECK(!c.fleet->explore(sim::ExploreOrder{1, id}));
    REQUIRE(c.fleet->explore(sim::ExploreOrder{0, id}));
    CHECK(c.fleet->explore_state(ship, 0) == sim::ExploreState::Running);
    CHECK(!c.fleet->explore(sim::ExploreOrder{0, id}));  // U-012: no second timer
    c.run(2'950);
    CHECK(c.count(sim::ShipEvent::Kind::CropsExplored) == 0);
    c.run(100);
    REQUIRE(c.count(sim::ShipEvent::Kind::CropsExplored) == 1);
    CHECK(c.world.islands()[0].explored_by(0));
    CHECK(!c.world.islands()[0].explored_by(1));
    c.run(4'500);  // 7.5 s
    CHECK(c.found(2) == 1);
    c.run(3'000);  // 10.5 s
    CHECK(c.found(2) == 2);
    CHECK(c.found(3) == 0);
    c.run(4'500);  // 15 s
    CHECK(c.found(3) == 2);
    CHECK(c.count(sim::ShipEvent::Kind::ExploreFinished) == 0);
    c.run(1'500);  // 16.5 s
    CHECK(c.count(sim::ShipEvent::Kind::ExploreFinished) == 1);
    CHECK(ship.explore_steps == 0);
    // From the end of the list: the gold vein at (3, 7) and the one at (9, 3); the first stays unknown.
    const auto& deposits = c.world.islands()[0].header.deposits;
    CHECK(sim::deposit_known(deposits[4], 0));
    CHECK(sim::deposit_known(deposits[3], 0));
    CHECK(!sim::deposit_known(deposits[1], 0));
    CHECK(sim::deposit_known(deposits[0], 0));
    CHECK(sim::deposit_known(deposits[2], 0));
    // A gold vein is left: a second round finds it, then the island is done.
    CHECK(c.fleet->explore_state(ship, 0) == sim::ExploreState::Possible);
    REQUIRE(c.fleet->explore(sim::ExploreOrder{0, id}));
    c.run(17'000);
    CHECK(c.count(sim::ShipEvent::Kind::CropsExplored) == 1);  // known: no second message
    CHECK(c.found(3) == 3);
    CHECK(c.fleet->explore_state(ship, 0) == sim::ExploreState::Done);
}

TEST_CASE("explore: only in the island's rectangle; an order ends it") {
    Coast c;
    // Within 5 tiles of the rectangle (discovering) but not in it: no exploring.
    const std::uint32_t outside = c.add(38, 21);
    CHECK(c.fleet->explore_state(*c.world.ship(outside), 0) == sim::ExploreState::NoIsland);
    CHECK(!c.fleet->explore(sim::ExploreOrder{0, outside}));
    c.run(100);
    CHECK(c.world.islands()[0].discovered_by(0));

    const std::uint32_t id = c.add(41, 21);
    REQUIRE(c.fleet->explore(sim::ExploreOrder{0, id}));
    c.run(2'000);
    REQUIRE(c.fleet->order(sim::ShipOrder{0, id, sim::Target::position(30, 30)}));
    CHECK(c.world.ship(id)->explore_steps == 0);
    c.run(500);
    CHECK(c.count(sim::ShipEvent::Kind::ExploreFinished) == 1);
    CHECK(c.fleet->explore_state(*c.world.ship(id), 0) != sim::ExploreState::Running);
    c.run(3'000);
    CHECK(c.count(sim::ShipEvent::Kind::CropsExplored) == 0);
}

TEST_CASE("explore: a running exploration is saved with the fleet (S-092)") {
    Coast c;
    const std::uint32_t id = c.add(41, 21);
    REQUIRE(c.fleet->explore(sim::ExploreOrder{0, id}));
    c.run(4'000);
    core::ByteWriter out;
    c.fleet->save(out);
    const std::uint8_t steps = c.world.ship(id)->explore_steps;
    const std::uint64_t next = c.world.ship(id)->explore_next_ms;
    c.world.ship(id)->explore_steps = 0;
    sim::Fleet loaded(c.world, c.buildings, c.figures, c.rules);
    core::ByteReader in(out.data());
    REQUIRE(loaded.load(in, c.fleet->state_version()));
    CHECK(c.world.ship(id)->explore_steps == steps);
    CHECK(c.world.ship(id)->explore_next_ms == next);
}
