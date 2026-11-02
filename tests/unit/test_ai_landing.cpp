// SPDX-License-Identifier: GPL-3.0-or-later
// A computer's attack from the sea (BT-1): at war, it picks an enemy city with a
// market or Kontor, sends an armed ship home for the soldiers beside its Kontor, plans a landing on
// a beach near the target's market and puts the troops ashore there.

#include <cstdint>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kBeach = 20501;
constexpr std::uint32_t kKontor = 22101;
constexpr std::uint32_t kMarket = 20809;
constexpr std::uint32_t kHouse = 20605;
constexpr std::uint16_t kSword = 1;
constexpr std::uint16_t kShipFigure = 21;

constexpr sim::PlayerId kHuman = 0;
constexpr sim::PlayerId kComputer = 1;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}};
    const auto doc = assets::cod::parse(
        "Objekt: BGRUPPE\n  Nummer: 0\n  Prozent: 0\n  Maxwohn: 8\n  Steuer: 1.4\nEndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: +1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: +1\n  Id: 20501\n  Kind: 23\n  Gfx: 2\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: +1\n  Id: 22101\n  Kind: 35\n  Gfx: 30\n  Size: 2, 3\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  KreuzBase: 15\n  Ausbauflg: 1\n  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 14\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20809\n  Kind: 14\n  Gfx: 40\n  Size: 4, 3\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 16\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20605\n  Kind: 14\n  Gfx: 50\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"
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
        "  Nummer: 1\n  Kind: 1\n  Gfx: 0\n  Blocknr: 1\n  Speed: 260\n  Shotradius: 0.75\n  Maxstepcnt: 1\n"
        "  Rohstoff: 11\n  Rohmenge: 3\n  Worktime: 0.8\n  Shottime: 0.6\n  Maxenergy: 20\n  Hitpoint: 1.0\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
        "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of 100 x 50 tiles of sea with two islands of 30 x 30: the computer's at (10, 10) with
/// its beach to the east (column 29), the human's at (60, 10) with its beach to the west (column 0).
struct Coast {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{100, 50};

    Coast() {
        std::vector<sim::Player> players(2);
        players[kHuman].role = sim::PlayerRole::Human;
        players[kComputer].role = sim::PlayerRole::Computer;
        players[kComputer].money = 20000;
        world.set_players(players);
        add_island(10, 10, 29);
        add_island(60, 10, 0);
    }

    void add_island(std::uint32_t at_x, std::uint32_t at_y, std::int32_t beach) {
        sim::IslandMap map(30, 30);
        for (std::int32_t y = 0; y < 30; ++y) {
            for (std::int32_t x = 0; x < 30; ++x) {
                const std::uint32_t id = x == beach ? kBeach : kGround;
                REQUIRE(map.place(*buildings.by_id(id), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), at_x, at_y};
        island.header.number = static_cast<std::uint8_t>(world.islands().size());
        island.header.width = 30;
        island.header.height = 30;
        REQUIRE(world.add_island(std::move(island)).ok());
    }

    void build(std::uint32_t id, std::int32_t x, std::int32_t y, sim::PlayerId owner, std::uint16_t island) {
        const std::uint8_t city = static_cast<std::uint8_t>(island);
        REQUIRE(world.island(island).map.place(*buildings.by_id(id), x, y, 0, 0, owner, city).ok());
        if (world.city(island, city) == nullptr) {
            sim::City c;
            c.island = island;
            c.index = city;
            c.owner = owner;
            world.add_city(c);
        }
        const auto ux = static_cast<std::uint16_t>(x);
        const auto uy = static_cast<std::uint16_t>(y);
        if (id == kHouse) {
            sim::House h;
            h.island = island;
            h.x = ux;
            h.y = uy;
            h.city = city;
            h.inhabitants64 = 4 * 64;
            world.add_house(h);
            // Houses without inhabitants are given up, and a city of three buildings is no target.
            auto& n = world.city(island, city)->inhabitants;
            n.resize(1, 0);
            n[0] += 4;
        } else {
            sim::Production p;
            p.island = island;
            p.x = ux;
            p.y = uy;
            world.set_production(p);
        }
        if (id == kKontor) {
            world.city(island, city)->warehouse_x = ux;
            world.city(island, city)->warehouse_y = uy;
        }
    }

    void add(sim::PlayerId owner, std::int32_t cx, std::int32_t cy, std::uint16_t island) {
        sim::Soldier s;
        s.place_cell(cx, cy);
        s.figure = kSword;
        s.owner = owner;
        s.island = island;
        world.add_soldier(s);
    }

    [[nodiscard]] std::size_t soldiers_on(sim::PlayerId owner, std::uint16_t island) const {
        std::size_t n = 0;
        for (const sim::Soldier& s : world.soldiers()) {
            n += s.owner == owner && s.island == island && !s.away() ? 1U : 0U;
        }
        return n;
    }
};

void run(sim::Simulation& s, std::uint32_t ms) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
    }
}

}  // namespace

TEST_CASE("ai landing: at war, a computer ships the soldiers beside its Kontor to a beach of an enemy city") {
    Coast a;
    a.build(kKontor, 28, 14, kComputer, 0);
    a.build(kMarket, 18, 14, kComputer, 0);
    a.build(kKontor, 0, 14, kHuman, 1);
    a.build(kMarket, 6, 6, kHuman, 1);
    for (std::int32_t k = 0; k < 4; ++k) {
        a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
    }
    for (std::int32_t k = 0; k < 6; ++k) {
        a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
    }
    sim::Ship ship = sim::make_ship("Ship", 41, 15, kShipFigure, kComputer, 2);
    ship.cargo.resize(4);
    ship.cannons = 4;
    a.world.add_ship(ship);
    REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 75'000);
    // Four went aboard (the ship's holds) and are ashore on the enemy's island, beside the beach
    // nearest its market (column 0); two stayed at home.
    CHECK(a.soldiers_on(kComputer, 0) == 2);
    REQUIRE(a.soldiers_on(kComputer, 1) == 4);
    for (const sim::Soldier& soldier : a.world.soldiers()) {
        if (soldier.owner == kComputer && soldier.island == 1) {
            CHECK(soldier.pos_x / sim::kPosUnit - 60 <= 12);
        }
    }
    REQUIRE(a.world.islands()[1].map.top(0, 14).owner == kHuman);
    run(s, 2 * 60'000);
    CHECK(a.world.islands()[1].map.top(0, 14).owner != kHuman);  // the Kontor has fallen
}
