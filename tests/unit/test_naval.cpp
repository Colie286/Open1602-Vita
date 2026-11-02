// SPDX-License-Identifier: GPL-3.0-or-later
// Ships in a fight.

#include <algorithm>
#include <memory>
#include <string>

#include "../test_framework.hpp"
#include "golden.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/combat.hpp"
#include "o1602/sim/military.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint16_t kWarship = 25;  // KRIEG1
constexpr std::uint16_t kTrade = 21;    // HANDEL1

/// A tree of 3 HP to shoot down, only in the tables that ask for it (the golden hashes stay).
constexpr const char* kTreeRecord =
    "  @Nummer: 2\n  Id: 20106\n  Kind: 10\n  Gfx: 11\n  Size: 1, 1\n  Maxenergy: 3\n  NoShotFlg: 1\n"
    "  Ruinenr: 255\n";

/// A road of 4 HP, only in the tables that ask for it.
constexpr const char* kRoadRecord = "  @Nummer: 4\n  Id: 20108\n  Kind: 1\n  Gfx: 20\n  Size: 1, 1\n  Maxenergy: 4\n";

/// A Kontor of 20 HP, only in the tables that ask for it.
constexpr const char* kKontorRecord =
    "  @Nummer: 3\n  Id: 20107\n  Kind: 35\n  Gfx: 12\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
    "  Ruinenr: 255\n";

assets::BuildingTable make_buildings(bool tree = false, bool kontor = false) {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const std::string text =
        std::string(
            "Objekt: HAUS\n"
            "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
            "  @Nummer: 1\n  Id: 20105\n  Kind: 6\n  Gfx: 10\n  Size: 1, 1\n  Maxenergy: 55\n  NoShotFlg: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 16\n    Figurnr: 38\n  EndObj\n") +
        (tree ? kTreeRecord : "") + (kontor ? kKontorRecord : "") + (kontor ? kRoadRecord : "") + "EndObj\n";
    const auto doc = assets::cod::parse(text, types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
        "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
        "  Stirbtime: 3.0\n"
        "  Nummer: 25\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 3\n  Maxkanon: 8\n  Speed: 550\n"
        "  Drehtime: 0.13\n  Maxenergy: 65\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
        "  Stirbtime: 4.0\n"
        "  Nummer: 38\n  Gfx: 0\n  Blocknr: 1\n  Shotradius: 7.5\n  Worktime: 3.0\n  Shottime: 0.6\n"
        "  Maxenergy: 24\n  Hitpoint: 4.0\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// Open sea of 100 x 60 tiles with a small island (10 x 8 at 40, 20). Player 0 human, 1 computer,
/// 2 pirates.
struct Sea {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{100, 60};
    sim::Rules rules = sim::Rules::open1602();
    std::unique_ptr<sim::Fleet> fleet;
    std::unique_ptr<sim::Combat> combat;
    std::unique_ptr<sim::Military> military;
    std::vector<sim::ShipEvent> ship_events;
    std::vector<sim::MilitaryEvent> events;
    std::vector<sim::ConstructionEvent> construction;

    explicit Sea(bool tree = false, bool kontor = false) : buildings(make_buildings(tree, kontor)) {
        std::vector<sim::Player> players(3);
        players[0].role = sim::PlayerRole::Human;
        players[1].role = sim::PlayerRole::Computer;
        sim::make_neutral(players[2], sim::faction::kPirates);
        world.set_players(players);
        sim::IslandMap map(10, 8);
        for (std::int32_t y = 0; y < 8; ++y) {
            for (std::int32_t x = 0; x < 10; ++x) {
                REQUIRE(map.place(*buildings.by_id(20101), x, y, 0, 0, 1, 0).ok());
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 40, 20}).ok());
        sim::City c;
        c.island = 0;
        c.index = 0;
        c.owner = 1;
        world.add_city(c);
    }

    void start() {
        fleet = std::make_unique<sim::Fleet>(world, buildings, figures, rules);
        combat = std::make_unique<sim::Combat>(world, buildings, figures);
        military = std::make_unique<sim::Military>(world, buildings, figures, rules, *combat);
        military->set_fleet(fleet.get());
        fleet->set_combat(combat.get());
    }

    std::uint32_t add(std::uint32_t x, std::uint32_t y, std::uint16_t figure, sim::PlayerId owner, std::uint8_t cannons,
                      std::uint8_t direction = 2) {
        sim::Ship s = sim::make_ship("Ship", x, y, figure, owner, direction);
        s.cargo.resize(4);
        s.cannons = cannons;
        world.add_ship(s);
        return world.ships().back().id;
    }

    void run(std::uint32_t ms) {
        for (std::uint32_t t = 0; t < ms; t += 50) {
            world.set_time_ms(world.time_ms() + 50);
            combat->update();
            fleet->tick(50);
            military->tick(50, &construction);
            for (const auto& e : fleet->take_events()) {
                ship_events.push_back(e);
            }
            for (const auto& e : military->take_events()) {
                events.push_back(e);
            }
        }
    }

    [[nodiscard]] std::size_t count(sim::ShipEvent::Kind kind) const {
        return static_cast<std::size_t>(
            std::count_if(ship_events.begin(), ship_events.end(), [&](const auto& e) { return e.kind == kind; }));
    }
};

}  // namespace

TEST_CASE("naval: an idle warship turns its broadside to an enemy in range and sinks it") {
    Sea s;
    s.start();
    s.world.diplomacy().set_peace(s.world, 0, 1, sim::Treaty::None, 0);
    // Enemy straight east, 5 tiles: the warship (heading east) must turn north or south first.
    const std::uint32_t warship = s.add(10, 40, kWarship, 0, 8, 2);
    const std::uint32_t victim = s.add(15, 40, kTrade, 1, 0, 0);
    s.run(1'000);
    const sim::Ship* w = s.world.ship(warship);
    REQUIRE(w != nullptr);
    CHECK((w->direction == 0 || w->direction == 4));
    // 8 cannons: 4.0 HP a shot (k = 2), reload 5 s * 2 / 8 = 1.25 s, a shot every 2 s while standing.
    // A HANDEL1 (50 HP) sinks with the 13th hit.
    s.run(20'000);
    CHECK(s.count(sim::ShipEvent::Kind::Fired) >= 10);
    CHECK(s.count(sim::ShipEvent::Kind::Attacked) >= 10);
    s.run(15'000);
    CHECK(s.count(sim::ShipEvent::Kind::Sunk) == 1);
    CHECK(s.world.ship(victim) == nullptr);
    CHECK(s.world.ship(warship) != nullptr);
    CHECK(s.world.diplomacy().relation(1, 0).damage >= 50 * 32);
}

TEST_CASE("naval: a sinking ship keeps health 0 when the game is loaded during its sinking") {
    Sea s;
    s.start();
    const std::uint32_t victim = s.add(15, 40, kTrade, 1, 0, 0);
    s.fleet->hit(victim, 1'000'000, 0, false);
    const sim::Ship* v = s.world.ship(victim);
    REQUIRE(v != nullptr);
    REQUIRE(v->sinking);
    CHECK(v->health == 0);
    // What a load or a resync does: the parts of the game are built anew on the world.
    const sim::Fleet loaded(s.world, s.buildings, s.figures, s.rules);
    CHECK(s.world.ship(victim)->health == 0);
    // A ship of an older file, which has no health, still gets the full health.
    const std::uint32_t old = s.add(30, 40, kTrade, 1, 0, 0);
    s.world.ship(old)->health = 0;
    const sim::Fleet again(s.world, s.buildings, s.figures, s.rules);
    CHECK(s.world.ship(old)->health != 0);
}

TEST_CASE("naval: a loaded ship under way must start and end its run on the map") {
    Sea s;
    const std::uint32_t id = s.add(15, 40, kTrade, 0, 0, 2);
    sim::Ship& ship = *s.world.ship(id);
    sim::ShipMotion& m = ship.motion;
    m.phase = sim::ShipMotion::Phase::Run;
    m.count = 3;
    m.from_x = 15 * sim::kShipUnit + sim::kShipUnit / 2;
    m.from_y = 40 * sim::kShipUnit + sim::kShipUnit / 2;
    m.duration_us = 3'000'000;
    CHECK(sim::validate(s.world).ok());
    // The crafted save of the device run: far east of the map, for about 71 minutes.
    m.from_x = 6'488'063;
    m.duration_us = 0xFF000000U;
    CHECK(!sim::validate(s.world).ok());
    // On the map at the start but off it at the end.
    m.from_x = 98 * sim::kShipUnit;
    m.count = 5;
    CHECK(!sim::validate(s.world).ok());
    m.count = 1;
    CHECK(sim::validate(s.world).ok());
    m.path_index = 1;  // past the end of an empty course
    CHECK(!sim::validate(s.world).ok());
}

TEST_CASE("naval: ships at peace and unarmed ships do not fight") {
    Sea s;
    s.start();
    s.add(10, 40, kWarship, 0, 8);
    s.add(14, 40, kWarship, 1, 8);
    s.run(10'000);
    CHECK(s.count(sim::ShipEvent::Kind::Fired) == 0);
    s.world.diplomacy().set_peace(s.world, 0, 1, sim::Treaty::None, 0);
    sim::Ship& a = s.world.ships()[0];
    sim::Ship& b = s.world.ships()[1];
    a.cannons = 0;
    b.cannons = 0;
    s.run(10'000);
    CHECK(s.count(sim::ShipEvent::Kind::Fired) == 0);
}

TEST_CASE("naval: a computer's ship hit by pirates surrenders; the pirates plunder it and make peace") {
    Sea s;
    s.start();
    // Pirates are at war with everyone from the start in a real game; here by hand.
    s.world.diplomacy().set_peace(s.world, 2, 1, sim::Treaty::None, 0);
    s.add(10, 40, kWarship, 2, 8);
    const std::uint32_t merchant = s.add(14, 40, kTrade, 1, 0);
    s.world.ship(merchant)->cargo[0] = sim::CargoSlot{21, 20 * 32, 0};  // jewellery: worth taking
    s.run(10'000);
    const sim::Ship* m = s.world.ship(merchant);
    REQUIRE(m != nullptr);
    CHECK(s.count(sim::ShipEvent::Kind::WhiteFlag) == 1);
    CHECK(s.count(sim::ShipEvent::Kind::Plundered) == 1);
    // Without a pirate base there is nowhere to take the loot: the computer's ship sails on, and
    // the pirates are at peace with its owner all the same (original).
    CHECK(!m->held);
    CHECK(!s.world.diplomacy().hostile(2, 1));
}

TEST_CASE("naval: a warship sent against a tree of nobody shoots it down, without war") {
    Sea s(true);
    REQUIRE(s.world.island(0).map.place(*s.buildings.by_id(20106), 0, 4, 0, 0).ok());
    s.start();
    const std::uint32_t warship = s.add(34, 24, kWarship, 0, 8);
    // Ctrl + click on the tree: 3 HP, one broadside of 4.0 HP.
    sim::ShipOrder o;
    o.player = 0;
    o.ship = warship;
    o.target = sim::Target::building(0, 0, 4);
    o.attack = true;
    REQUIRE(s.fleet->order(o));
    s.run(15'000);
    CHECK(s.count(sim::ShipEvent::Kind::Fired) >= 1);
    CHECK(s.world.islands()[0].map.top(0, 4).sprite != s.buildings.by_id(20106)->gfx);
    CHECK(std::count_if(s.events.begin(), s.events.end(),
                        [](const auto& e) { return e.kind == sim::MilitaryEvent::Kind::BuildingDestroyed; }) == 1);
    CHECK(!s.world.diplomacy().hostile(0, 1));
}

TEST_CASE("naval: a warship sent against another player's Kontor shoots at it") {
    Sea s(false, true);
    REQUIRE(s.world.island(0).map.place(*s.buildings.by_id(20107), 0, 4, 0, 0, 1, 0).ok());
    s.start();
    s.world.diplomacy().set_peace(s.world, 0, 1, sim::Treaty::None, 0);
    const std::uint32_t warship = s.add(34, 24, kWarship, 0, 8);
    // A right click on the Kontor of a player one may not trade with: the building is the target.
    sim::ShipOrder o;
    o.player = 0;
    o.ship = warship;
    o.target = sim::Target::building(0, 0, 4);
    o.attack = true;
    REQUIRE(s.fleet->order(o));
    s.run(20'000);
    CHECK(s.count(sim::ShipEvent::Kind::Fired) >= 1);
    std::uint32_t damage = 0;
    for (const sim::BuildingDamage& d : s.world.building_damage()) {
        if (d.island == 0 && d.x == 0 && d.y == 4) {
            damage = d.damage;
        }
    }
    const bool destroyed = std::count_if(s.events.begin(), s.events.end(), [](const auto& e) {
                               return e.kind == sim::MilitaryEvent::Kind::BuildingDestroyed;
                           }) > 0;
    CHECK(damage > 0 || destroyed);
}

TEST_CASE("naval: a ship stops shooting once its target building is gone; terrain is no target") {
    Sea s(false, true);
    REQUIRE(s.world.island(0).map.place(*s.buildings.by_id(20107), 0, 4, 0, 0, 1, 0).ok());
    s.start();
    // The ground of another player is terrain: nothing to shoot at.
    CHECK(!s.combat->fighter(sim::Target::building(0, 5, 0)).has_value());
    CHECK(s.combat->fighter(sim::Target::building(0, 0, 4)).has_value());
    s.world.diplomacy().set_peace(s.world, 0, 1, sim::Treaty::None, 0);
    const std::uint32_t warship = s.add(34, 24, kWarship, 0, 8);
    sim::ShipOrder o{0, warship, sim::Target::building(0, 0, 4)};
    o.attack = true;
    REQUIRE(s.fleet->order(o));
    for (int t = 0;
         t < 60 && std::none_of(s.events.begin(), s.events.end(),
                                [](const auto& e) { return e.kind == sim::MilitaryEvent::Kind::BuildingDestroyed; });
         ++t) {
        s.run(1'000);
    }
    REQUIRE(std::any_of(s.events.begin(), s.events.end(),
                        [](const auto& e) { return e.kind == sim::MilitaryEvent::Kind::BuildingDestroyed; }));
    s.run(2'000);  // shots already in the air land
    const std::size_t fired = s.count(sim::ShipEvent::Kind::Fired);
    s.run(30'000);
    CHECK(s.count(sim::ShipEvent::Kind::Fired) == fired);
}

TEST_CASE("naval: a warship sent against another player's road (Ctrl or F) shoots it") {
    Sea s(false, true);
    REQUIRE(s.world.island(0).map.build(*s.buildings.by_id(20108), 0, 4, 0, 0, 1, 0).ok());
    s.start();
    s.world.diplomacy().set_peace(s.world, 0, 1, sim::Treaty::None, 0);
    const std::uint32_t warship = s.add(34, 24, kWarship, 0, 8);
    sim::ShipOrder o{0, warship, sim::Target::building(0, 0, 4)};
    o.attack = true;
    REQUIRE(s.fleet->order(o));
    s.run(15'000);
    CHECK(s.count(sim::ShipEvent::Kind::Fired) >= 1);
    CHECK(std::any_of(s.events.begin(), s.events.end(),
                      [](const auto& e) { return e.kind == sim::MilitaryEvent::Kind::BuildingDestroyed; }));
}

TEST_CASE("naval: what units can shoot at") {
    assets::BuildingType b;
    b.kind = assets::kind::kBuilding;
    CHECK(sim::destroyable(b, false));
    b.kind = assets::kind::kHq;
    CHECK(sim::destroyable(b, false));
    b.destroyed = true;  // "Kontor zerstört"
    CHECK(!sim::destroyable(b, true));
    b.destroyed = false;
    for (const std::int32_t ctrl :
         {assets::kind::kForest, assets::kind::kRoad, assets::kind::kBridge, assets::kind::kPier}) {
        b.kind = ctrl;
        CHECK(!sim::destroyable(b, false));
        CHECK(sim::destroyable(b, true));
    }
    for (const std::int32_t terrain : {assets::kind::kBeach, assets::kind::kGround, assets::kind::kRock,
                                       assets::kind::kRuin, assets::kind::kRiver, assets::kind::kSlope}) {
        b.kind = terrain;
        CHECK(!sim::destroyable(b, false));
        CHECK(!sim::destroyable(b, true));
    }
}

TEST_CASE("naval: a cannon tower fires at hostile ships near its island") {
    Sea s;
    REQUIRE(s.world.island(0).map.place(*s.buildings.by_id(20105), 0, 0, 0, 0, 1, 0).ok());
    s.start();
    s.military->update_towers(0);
    REQUIRE(s.world.towers().size() == 1);
    s.world.diplomacy().set_peace(s.world, 0, 1, sim::Treaty::None, 0);
    // A trade ship of player 0 five tiles west of the tower; the fleet marks it near the island.
    const std::uint32_t ship = s.add(35, 20, kTrade, 0, 0);
    s.run(12'000);
    const sim::Ship* t = s.world.ship(ship);
    REQUIRE(t != nullptr);
    CHECK(t->health > 0);
    CHECK(t->health < 50 * 32);
    bool tower_shot = false;
    for (const auto& e : s.events) {
        tower_shot =
            tower_shot || (e.kind == sim::MilitaryEvent::Kind::Shot && e.unit.kind == sim::Target::Kind::Building);
    }
    CHECK(tower_shot);
}

TEST_CASE("naval: an attack move hunts an enemy off the way and sails on; holding stops the hunt") {
    Sea s;
    s.start();
    s.world.diplomacy().set_peace(s.world, 0, 1, sim::Treaty::None, 0);
    // A warship sailing east along row 50; an enemy off its way at (40, 42), out of range.
    const std::uint32_t warship = s.add(10, 50, kWarship, 0, 8, 2);
    const std::uint32_t victim = s.add(40, 42, kTrade, 1, 0, 0);
    sim::ShipOrder order{0, warship, sim::Target::position(80, 50)};
    order.attack_move = true;
    REQUIRE(s.fleet->order(order));
    // Once it sees the enemy it turns to it; the way east waits in the queue.
    const sim::Ship* w = s.world.ship(warship);
    bool hunting = false;
    for (int t = 0; t < 40 && !hunting; ++t) {
        s.run(1'000);
        w = s.world.ship(warship);
        REQUIRE(w != nullptr);
        hunting = w->target == sim::Target::other_ship(victim);
    }
    CHECK(hunting);
    CHECK(!w->queue.empty());
    s.run(120'000);
    CHECK(s.world.ship(victim) == nullptr);
    w = s.world.ship(warship);
    CHECK(w->x == 80);
    CHECK(w->y == 50);
    // Holding its place a ship chases nothing.
    const std::uint32_t other = s.add(80, 43, kTrade, 1, 0, 0);
    sim::ShipCommand hold;
    hold.player = 0;
    hold.ship = warship;
    hold.op = sim::ShipCommand::Op::Hold;
    REQUIRE(s.fleet->command(hold));
    s.run(20'000);
    CHECK(s.world.ship(warship)->x == 80);
    CHECK(s.world.ship(other) != nullptr);
}

TEST_CASE("naval: a sea fight with pirates develops the same on every platform (golden hash)") {
    Sea s;
    s.start();
    s.world.diplomacy().set_peace(s.world, 0, 1, sim::Treaty::None, 0);
    s.world.diplomacy().set_peace(s.world, 2, 1, sim::Treaty::None, 0);
    s.world.diplomacy().set_peace(s.world, 2, 0, sim::Treaty::None, 0);
    // Two warships of the human sail east, hunting on the way; a computer's warship and merchant,
    // and a pirate, wait off the island.
    for (std::uint32_t i = 0; i < 2; ++i) {
        const std::uint32_t w = s.add(8, 44 + 4 * i, kWarship, 0, 8, 2);
        sim::ShipOrder order{0, w, sim::Target::position(90, 46)};
        order.attack_move = true;
        REQUIRE(s.fleet->order(order));
    }
    s.add(60, 45, kWarship, 1, 6, 6);
    const std::uint32_t merchant = s.add(30, 50, kTrade, 1, 0, 2);
    s.world.ship(merchant)->cargo[0] = sim::CargoSlot{21, 20 * 32, 0};
    s.add(26, 52, kWarship, 2, 8, 0);
    s.run(90'000);
    CHECK(s.count(sim::ShipEvent::Kind::Sunk) + s.count(sim::ShipEvent::Kind::WhiteFlag) > 0);
    test::check_golden("sea fight after 90 s", test::world_hash(s.world, s.buildings), 0x5fc48cd29a13999fULL);
}
