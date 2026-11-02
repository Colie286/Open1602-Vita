// SPDX-License-Identifier: GPL-3.0-or-later
// The computers' soldiers on an island (BT-1, the island controller): at peace
// they gather at their Kontor or, without a city there, are disbanded; at war they go for the
// enemy's soldiers, or for the markets, Kontor and houses of an enemy city on the island. Every
// 6 s a colony sends the soldiers beside its Kontor to guard a market, and a city with a castle
// trains units there until its army is half again as strong as the strongest human one.

#include <algorithm>
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
constexpr std::uint32_t kHouse = 20103;
constexpr std::uint32_t kKontor = 20108;
constexpr std::uint32_t kMarket = 20809;
constexpr std::uint32_t kCastle = 22521;
constexpr std::uint32_t kSwordsmith = 20511;
constexpr std::uint32_t kWall = 20902;
constexpr std::uint32_t kGate = 20904;
constexpr std::uint32_t kTree = 20202;
constexpr std::uint16_t kSword = 1;

constexpr sim::PlayerId kHuman = 0;
constexpr sim::PlayerId kComputer = 1;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}};
    const auto doc = assets::cod::parse(
        "Objekt: BGRUPPE\n  Nummer: 0\n  Prozent: 0\n  Maxwohn: 8\n  Steuer: 1.4\nEndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"  // open ground
        "  @Nummer: 1\n  Id: 20103\n  Kind: 14\n  Gfx: 2\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"  // a house
        "  @Nummer: 2\n  Id: 20108\n  Kind: 35\n  Gfx: 30\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  KreuzBase: 6\n  Ausbauflg: 1\n  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"  // Kontor
        "  @Nummer: 3\n  Id: 20809\n  Kind: 14\n  Gfx: 40\n  Size: 4, 3\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 16\n  EndObj\n"  // market
        "  @Nummer: 4\n  Id: 22521\n  Kind: 14\n  Gfx: 60\n  Size: 3, 3\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 15\n    Maxware: 3\n    Prodmenge: 2\n  EndObj\n"  // castle
        "  @Nummer: 5\n  Id: 20511\n  Kind: 14\n  Gfx: 70\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Ware: 11\n    Rohstoff: 10\n    Radius: 4\n  EndObj\n"  // swords
        "  @Nummer: 6\n  Id: 20902\n  Kind: 4\n  Gfx: 80\n  Size: 1, 1\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"  // a stone wall
        "  @Nummer: 7\n  Id: 20904\n  Kind: 3\n  Gfx: 81\n  Size: 1, 1\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"  // a gate
        "  @Nummer: 8\n  Id: 20202\n  Kind: 10\n  Gfx: 90\n  Size: 1, 1\n  Maxenergy: 3\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"  // a tree
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
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of 120 x 40 tiles with an island of open ground (80 x 20 at 10, 10) and an islet
/// (8 x 8 at 100, 10). Player 0 is human, 1 a computer without a ship.
struct Island {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{120, 40};

    Island() {
        std::vector<sim::Player> players(2);
        players[kHuman].role = sim::PlayerRole::Human;
        players[kComputer].role = sim::PlayerRole::Computer;
        players[kComputer].money = 20000;
        world.set_players(players);
        add_island(10, 10, 80, 20);
        add_island(100, 10, 8, 8);
    }

    void add_island(std::uint32_t at_x, std::uint32_t at_y, std::int32_t width, std::int32_t height) {
        sim::IslandMap map(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
        for (std::int32_t y = 0; y < height; ++y) {
            for (std::int32_t x = 0; x < width; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), at_x, at_y}).ok());
    }

    /// A building of city `city` of `owner` at tile (x, y) of the island, with its house or
    /// production entry; a Kontor is the city's warehouse.
    void build(std::uint32_t id, std::int32_t x, std::int32_t y, sim::PlayerId owner, std::uint8_t city,
               std::uint16_t island = 0) {
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
            world.add_house(h);
        }
        if (id == kKontor || id == kMarket || id == kCastle || id == kSwordsmith) {
            sim::Production p;
            p.island = island;
            p.x = ux;
            p.y = uy;
            world.set_production(p);
        }
        if (id == kCastle) {
            sim::CostBuilding castle;
            castle.kind = sim::CostBuilding::Kind::Castle;
            castle.island = island;
            castle.x = ux;
            castle.y = uy;
            castle.weapons = {10 * 32, 0, 0};
            world.add_cost_building(castle);
        }
        if (id == kKontor) {
            world.city(island, city)->warehouse_x = ux;
            world.city(island, city)->warehouse_y = uy;
        }
    }

    void add(sim::PlayerId owner, std::int32_t cx, std::int32_t cy, std::uint16_t island = 0) {
        sim::Soldier s;
        s.place_cell(cx, cy);
        s.figure = kSword;
        s.owner = owner;
        s.island = island;
        world.add_soldier(s);
    }

    /// A tree of nobody at tile (x, y) of the island.
    void tree(std::int32_t x, std::int32_t y, std::uint16_t island = 0) {
        REQUIRE(world.island(island).map.place(*buildings.by_id(kTree), x, y, 0, 0).ok());
    }

    /// Tiles x0..x1, y0..y1 of the island become land of `owner`'s city `city`.
    void claim(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, sim::PlayerId owner,
               std::uint8_t city, std::uint16_t island = 0) {
        for (std::int32_t y = y0; y <= y1; ++y) {
            for (std::int32_t x = x0; x <= x1; ++x) {
                world.island(island).map.set_city(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), city,
                                                  owner);
            }
        }
    }

    void war() { REQUIRE(world.diplomacy().set_peace(world, kHuman, kComputer, sim::Treaty::None, world.time_ms())); }

    [[nodiscard]] std::vector<const sim::Soldier*> soldiers(sim::PlayerId owner) const {
        std::vector<const sim::Soldier*> out;
        for (const sim::Soldier& s : world.soldiers()) {
            if (s.owner == owner && !s.away()) {
                out.push_back(&s);
            }
        }
        return out;
    }
};

void run(sim::Simulation& s, std::uint32_t ms) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
    }
}

}  // namespace

TEST_CASE("ai military: at peace, soldiers on an island without a city of theirs are disbanded") {
    Island a;
    a.build(kKontor, 2, 2, kComputer, 0, 1);  // its city is on the islet
    for (std::int32_t k = 0; k < 3; ++k) {
        a.add(kComputer, 20 + 2 * k, 20);
    }
    a.add(kHuman, 140, 20);
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 60'000);
    CHECK(a.soldiers(kComputer).empty());
    CHECK(a.soldiers(kHuman).size() == 1);  // a human's units are the human's business
}

TEST_CASE("ai military: at peace, soldiers gather at their city's Kontor") {
    Island a;
    a.build(kKontor, 60, 8, kComputer, 0);
    for (std::int32_t k = 0; k < 3; ++k) {
        a.add(kComputer, 20 + 2 * k, 20);
    }
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 60'000);
    const auto mine = a.soldiers(kComputer);
    REQUIRE(mine.size() == 3);  // kept: there is a city of theirs here
    for (const sim::Soldier* soldier : mine) {
        // At the Kontor (island tile 60, 8; half tiles from the world's corner), not where they were.
        const std::int32_t cx = soldier->pos_x / (sim::kPosUnit / 2);
        const std::int32_t cy = soldier->pos_y / (sim::kPosUnit / 2);
        CHECK(std::abs(cx - 2 * (10 + 60)) <= 8);
        CHECK(std::abs(cy - 2 * (10 + 8)) <= 8);
    }
}

TEST_CASE("ai military: at war, soldiers go for the enemy's soldiers on the island, out of sight too") {
    Island a;
    a.build(kKontor, 4, 8, kComputer, 0);
    for (std::int32_t k = 0; k < 5; ++k) {
        a.add(kComputer, 30, 26 + 2 * k);
    }
    a.add(kHuman, 160, 30);
    a.add(kHuman, 160, 34);
    a.war();
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 20'000);
    std::size_t hunting = 0;
    for (const sim::Soldier* soldier : a.soldiers(kComputer)) {
        hunting += soldier->target.kind == sim::Target::Kind::Soldier ? 1U : 0U;
    }
    // Two enemies: a quarter more (rounded down) goes at each look, until all of them go.
    CHECK(hunting == 5);
    run(s, 5 * 60'000);
    CHECK(a.soldiers(kHuman).empty());
    CHECK(!a.soldiers(kComputer).empty());
}

TEST_CASE("ai military: soldiers kept out by a wall go for the wall, through an open gate for the Kontor") {
    for (const bool closed : {true, false}) {
        Island a;
        a.build(kKontor, 4, 8, kComputer, 0);
        a.build(kKontor, 70, 8, kHuman, 1);
        // A ring of wall around the human city with a gate on its west side.
        for (std::int32_t x = 62; x <= 78; ++x) {
            a.build(kWall, x, 2, kHuman, 1);
            a.build(kWall, x, 16, kHuman, 1);
        }
        for (std::int32_t y = 3; y <= 15; ++y) {
            a.build(y == 9 ? kGate : kWall, 62, y, kHuman, 1);
            a.build(kWall, 78, y, kHuman, 1);
        }
        a.world.player(kHuman)->gates_closed = closed;
        for (std::int32_t k = 0; k < 6; ++k) {
            a.add(kComputer, 100, 24 + 2 * k);
        }
        a.war();
        sim::Simulation s(a.world, a.buildings, a.figures);
        run(s, 20'000);
        std::size_t at_kontor = 0;
        std::size_t at_ring = 0;
        for (const sim::Soldier* soldier : a.soldiers(kComputer)) {
            if (soldier->target.kind == sim::Target::Kind::Building) {
                const bool ring = soldier->target.x == 62 || soldier->target.x == 78 || soldier->target.y == 2 ||
                                  soldier->target.y == 16;
                ++(ring ? at_ring : at_kontor);
            }
        }
        if (closed) {
            CHECK(at_kontor == 0);
            CHECK(at_ring == 6);  // a way in, not standing in front of the wall
        } else {
            CHECK(at_kontor == 6);  // through the gate, as in the original
            CHECK(at_ring == 0);
        }
    }
}

TEST_CASE("ai military: at war, soldiers go for the Kontor and the houses of an enemy city") {
    Island a;
    a.build(kKontor, 4, 8, kComputer, 0);
    a.build(kKontor, 70, 8, kHuman, 1);
    a.build(kHouse, 66, 4, kHuman, 1);
    a.build(kHouse, 66, 12, kHuman, 1);
    for (std::int32_t k = 0; k < 8; ++k) {
        a.add(kComputer, 30, 22 + 2 * k);
    }
    a.war();
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 20'000);
    std::size_t at_kontor = 0;
    std::size_t at_houses = 0;
    for (const sim::Soldier* soldier : a.soldiers(kComputer)) {
        if (soldier->target.kind == sim::Target::Kind::Building) {
            ++(soldier->target.x == 70 ? at_kontor : at_houses);
        }
    }
    CHECK(at_kontor == 4);  // the first goal gets four, each further one two
    CHECK(at_houses == 4);
    const sim::IslandMap& map = a.world.islands()[0].map;
    REQUIRE(map.top(70, 8).owner == kHuman);
    run(s, 5 * 60'000);
    CHECK(map.top(70, 8).empty() || map.top(70, 8).owner != kHuman);  // the Kontor has fallen
}

TEST_CASE("ai military: at peace, soldiers beside the Kontor of a colony are sent to guard its market") {
    Island a;
    a.build(kKontor, 10, 8, kComputer, 0);
    a.build(kMarket, 50, 8, kComputer, 0);
    for (std::int32_t k = 0; k < 4; ++k) {
        a.add(kComputer, 2 * (10 + 10) + k, 2 * (10 + 10));
    }
    a.add(kHuman, 2 * (100 + 4), 2 * (10 + 4), 1);  // an army to measure against, on the islet
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 10'000);  // the first 6-s look at the city
    const auto mine = a.soldiers(kComputer);
    REQUIRE(mine.size() == 4);
    const sim::Target post = sim::Target::spot(2 * (10 + 50) + 1, 2 * (10 + 8) + 1);
    std::size_t guarding = 0;
    for (const sim::Soldier* soldier : mine) {
        guarding += soldier->target == post ? 1U : 0U;
    }
    CHECK(guarding == 4);  // a colony keeps no reserve at its Kontor
}

TEST_CASE("ai military: a computer with a castle and a sword smith trains swordsmen and sends them to its Kontor") {
    Island a;
    a.build(kKontor, 10, 8, kComputer, 0);
    a.build(kMarket, 30, 8, kComputer, 0);  // recruiting starts from a guard post
    a.build(kCastle, 40, 8, kComputer, 0);  // found by the survey
    a.build(kSwordsmith, 46, 8, kComputer, 0);
    a.world.city(0, 0)->inhabitants = {48};         // room for 48 / 16 = 3 units
    a.add(kHuman, 2 * (100 + 4), 2 * (10 + 4), 1);  // the army to outdo: one swordsman
    sim::Simulation s(a.world, a.buildings, a.figures);
    const auto castle = [&]() -> const sim::CostBuilding* {
        for (const sim::CostBuilding& c : a.world.cost_buildings()) {
            if (c.kind == sim::CostBuilding::Kind::Castle) {
                return &c;
            }
        }
        return nullptr;
    };
    run(s, 30'000);
    REQUIRE(castle() != nullptr);
    REQUIRE(castle()->slots.size() == 1);
    CHECK(castle()->slots[0].figure == kSword);
    CHECK(castle()->weapons[0] < 10 * 32);  // paid with the castle's swords
    run(s, 4 * 60'000);
    REQUIRE(castle() != nullptr);
    CHECK(castle()->slots.empty());
    // One and a half times the human army: the second swordsman is enough.
    const auto mine = a.soldiers(kComputer);
    REQUIRE(mine.size() == 2);
    for (const sim::Soldier* soldier : mine) {
        CHECK(soldier->figure == kSword);
        CHECK(soldier->home == sim::Target::building(0, 40, 8));
        // Beside the Kontor (half tiles 40..43, 36..39), where the castle sends them.
        const std::int32_t cx = soldier->pos_x / (sim::kPosUnit / 2);
        const std::int32_t cy = soldier->pos_y / (sim::kPosUnit / 2);
        CHECK(std::abs(cx - 41) <= 4);
        CHECK(std::abs(cy - 37) <= 4);
    }
}

TEST_CASE("ai military: a computer trains soldiers of a package of the kind it wants too") {
    Island a;
    // Pikemen of a package: swordsmen by their kind, with more health.
    assets::FigureType pike = *a.figures.get(kSword);
    pike.combat.max_health += 32;
    const auto family = a.figures.add_unit_family(std::vector<assets::FigureType>(4, pike));
    REQUIRE(family.ok());
    a.build(kKontor, 10, 8, kComputer, 0);
    a.build(kMarket, 30, 8, kComputer, 0);
    a.build(kCastle, 40, 8, kComputer, 0);
    a.build(kSwordsmith, 46, 8, kComputer, 0);
    a.world.city(0, 0)->inhabitants = {160};  // room for 10 units
    for (int i = 0; i < 4; ++i) {
        a.add(kHuman, 2 * (100 + 1 + i), 2 * (10 + 4), 1);  // four swordsmen to outdo
    }
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 6 * 60'000);
    const auto mine = a.soldiers(kComputer);
    REQUIRE(mine.size() >= 2);
    std::size_t pikemen = 0;
    for (const sim::Soldier* soldier : mine) {
        CHECK((soldier->figure == kSword || soldier->figure == *family));
        pikemen += soldier->figure == *family ? 1U : 0U;
    }
    CHECK(pikemen > 0);
    CHECK(pikemen < mine.size());
    // Counted as swordsmen: the army stops at one and a half times the human one.
    CHECK(mine.size() <= 7);
}

TEST_CASE("ai military: soldiers pass over an enemy city with nothing left to attack for the next one") {
    // The first city of the enemy on the island has nothing to attack any more (its Kontor was
    // destroyed and kept the city, its houses are gone). The original takes it and its soldiers
    // stand; Open1602 takes the next city of the enemy (Windows feedback #8).
    Island a;
    a.build(kKontor, 4, 8, kComputer, 0);
    sim::City empty;
    empty.island = 0;
    empty.index = 1;
    empty.owner = kHuman;
    a.world.add_city(empty);
    a.build(kKontor, 70, 8, kHuman, 2);
    a.build(kHouse, 66, 4, kHuman, 2);
    a.build(kHouse, 66, 12, kHuman, 2);
    for (std::int32_t k = 0; k < 8; ++k) {
        a.add(kComputer, 30, 22 + 2 * k);
    }
    a.war();
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 20'000);
    std::size_t with_target = 0;
    for (const sim::Soldier* soldier : a.soldiers(kComputer)) {
        with_target += soldier->target.kind == sim::Target::Kind::Building ? 1U : 0U;
    }
    CHECK(with_target == 8);
}

TEST_CASE("ai military: soldiers kept out by a forest cut their way in and take the Kontor") {
    // A band of trees of nobody, three deep, closes the human city off from the computer's
    // soldiers. The original sends them to the Kontor all the same, where they cannot walk, and
    // they stand; Open1602 has them fell the tree in the way first.
    Island a;
    a.build(kKontor, 4, 8, kComputer, 0);
    a.build(kKontor, 70, 8, kHuman, 1);
    for (std::int32_t y = 0; y < 20; ++y) {
        for (std::int32_t x = 60; x <= 62; ++x) {
            a.tree(x, y);
        }
    }
    for (std::int32_t k = 0; k < 6; ++k) {
        a.add(kComputer, 100, 24 + 2 * k);
    }
    a.war();
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 20'000);
    std::size_t at_trees = 0;
    for (const sim::Soldier* soldier : a.soldiers(kComputer)) {
        if (soldier->target.kind == sim::Target::Kind::Building) {
            at_trees += soldier->target.x == 60 ? 1U : 0U;  // the first tree on the way
        }
    }
    CHECK(at_trees >= 4);
    const sim::IslandMap& map = a.world.islands()[0].map;
    REQUIRE(map.top(70, 8).owner == kHuman);
    run(s, 8 * 60'000);
    CHECK(map.top(70, 8).empty() || map.top(70, 8).owner != kHuman);  // through the forest, the Kontor has fallen
}

TEST_CASE("ai military: soldiers defend their own city before they go for an enemy city") {
    // The human has a city on the island too and sends two soldiers into the computer's city. The
    // original goes for the human city first; Open1602 meets the intruders.
    Island a;
    a.build(kKontor, 10, 8, kComputer, 0);
    a.claim(0, 0, 30, 19, kComputer, 0);
    a.build(kKontor, 70, 8, kHuman, 1);
    for (std::int32_t k = 0; k < 6; ++k) {
        a.add(kComputer, 2 * (10 + 40), 2 * (10 + 4) + 2 * k);  // halfway between the cities
    }
    a.add(kHuman, 2 * (10 + 20), 2 * (10 + 16));
    a.add(kHuman, 2 * (10 + 21), 2 * (10 + 16));
    a.war();
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 20'000);
    std::size_t hunting = 0;
    for (const sim::Soldier* soldier : a.soldiers(kComputer)) {
        hunting += soldier->target.kind == sim::Target::Kind::Soldier ? 1U : 0U;
    }
    CHECK(hunting == 6);  // every one of them, not a quarter more than the enemy has
    run(s, 3 * 60'000);
    CHECK(a.soldiers(kHuman).empty());
}
