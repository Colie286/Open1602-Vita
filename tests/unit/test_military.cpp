// SPDX-License-Identifier: GPL-3.0-or-later
// Soldiers, towers and building damage.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "golden.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/own_figures.hpp"
#include "o1602/script/game_scripts.hpp"
#include "o1602/sim/combat.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/military.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kHouse = 20103;
constexpr std::uint32_t kDoctor = 20104;
constexpr std::uint32_t kTowerBuilding = 20105;
constexpr std::uint32_t kForest = 20106;
constexpr std::uint32_t kCastle = 20107;
constexpr std::uint32_t kKontor = 20108;
constexpr std::uint32_t kKontorDestroyed = 20109;
constexpr std::uint32_t kNativeHut = 20110;
constexpr std::uint32_t kTree = 20111;
constexpr std::uint32_t kKontorRuin = 20109;
constexpr std::uint32_t kVillageHut = 20112;
constexpr std::uint16_t kSword = 1;
constexpr std::uint16_t kMusket = 9;

/// A tree of 3 HP to shoot down, only in the tables that ask for it (the golden hashes stay).
constexpr const char* kTreeRecord =
    "  @Nummer: 9\n  Id: 20111\n  Kind: 10\n  Gfx: 60\n  Size: 1, 1\n  Maxenergy: 3\n  NoShotFlg: 1\n"
    "  Ruinenr: 255\n";

/// A hut of the natives (PIRATWOHN) and its ruin for the tables that ask for a village (the golden
/// hashes stay).
constexpr const char* kVillageRecord =
    "  @Nummer: 10\n  Id: 20112\n  Kind: 14\n  Gfx: 70\n  Size: 1, 1\n  Maxenergy: 3\n  NoShotFlg: 1\n"
    "  Ruinenr: 0\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 31\n  EndObj\n"
    "  @Nummer: 11\n  Id: 22601\n  Kind: 12\n  Gfx: 74\n  Size: 1, 1\n";  // its ruin

assets::BuildingTable make_buildings(bool tree = false, bool village = false) {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const std::string text =
        std::string(
            "Objekt: HAUS\n"
            "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"  // open ground
            "  @Nummer: 1\n  Id: 20103\n  Kind: 14\n  Gfx: 2\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
            "  Ruinenr: 255\n"  // a house
            "  Objekt: HAUS_PRODTYP\n    Kind: 13\n  EndObj\n"
            "  @Nummer: 2\n  Id: 20104\n  Kind: 14\n  Gfx: 6\n  Size: 2, 2\n  Maxenergy: 50\n  NoShotFlg: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 22\n  EndObj\n"  // doctor (KLINIK)
            "  @Nummer: 3\n  Id: 20105\n  Kind: 6\n  Gfx: 10\n  Size: 1, 1\n  Maxenergy: 55\n  NoShotFlg: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 16\n    Figurnr: 38\n  EndObj\n"                 // Wachturm
            "  @Nummer: 4\n  Id: 20106\n  Kind: 10\n  Gfx: 11\n  Size: 1, 1\n  NoShotFlg: 1\n"  // forest
            "  @Nummer: 5\n  Id: 20107\n  Kind: 14\n  Gfx: 20\n  Size: 2, 2\n  Maxenergy: 100\n  NoShotFlg: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 15\n    Maxware: 3\n    Prodmenge: 0.5\n    Interval: 10\n  EndObj\n"
            "  @Nummer: 6\n  Id: 20108\n  Kind: 35\n  Gfx: 30\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
            "  KreuzBase: 6\n  Ausbauflg: 1\n  Ruinenr: 255\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"  // Kontor
            "  @Nummer: 7\n  Id: 20109\n  Kind: 35\n  Gfx: 40\n  Size: 2, 2\n  NoShotFlg: 1\n"
            "  KreuzBase: 6\n  Ausbauflg: 1\n  Destroyflg: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"  // destroyed Kontor
            "  @Nummer: 8\n  Id: 20110\n  Kind: 35\n  Gfx: 50\n  Size: 2, 2\n  Maxenergy: 5\n  NoShotFlg: 1\n"
            "  Ruinenr: 255\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n    Nativflg: 1\n  EndObj\n"  // natives' main hut
            ) +
        (tree ? kTreeRecord : "") + (village ? kVillageRecord : "") + "EndObj\n";
    const auto doc = assets::cod::parse(text, types);
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
        "  Nummer: 9\n  Kind: 3\n  Gfx: 0\n  Blocknr: 1\n  Speed: 210\n  Shotradius: 4\n  Maxstepcnt: 2\n"
        "  Worktime: 2.0\n  Shottime: 0.7\n  Maxenergy: 15\n  Hitpoint: 2.4\n"
        "  Nummer: 38\n  Gfx: 0\n  Blocknr: 1\n  Shotradius: 7.5\n  Worktime: 3.0\n  Shottime: 0.6\n"
        "  Maxenergy: 24\n  Hitpoint: 4.0\n"
        // A small ship with two holds.
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 1\n  Maxware: 2\n"
        // A trade cart (TRADER1).
        "  Nummer: 37\n  Gfx: 0\n  Blocknr: 3\n  Maxware: 3\n  Speed: 300\n  Speedtyp: 2\n  Maxenergy: 14\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of 80 x 60 tiles with an island of open ground (30 x 20 at 10, 10). Player 0 is human,
/// 1 a computer player, 2 the natives; nobody is at war yet.
struct Field {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{80, 60};
    sim::Rules rules = sim::Rules::open1602();
    std::unique_ptr<sim::Combat> combat;
    std::unique_ptr<sim::Military> military;
    std::vector<sim::MilitaryEvent> events;
    std::vector<sim::ConstructionEvent> construction;

    explicit Field(bool tree = false, bool village = false) : buildings(make_buildings(tree, village)) {
        std::vector<sim::Player> players(3);
        players[0].role = sim::PlayerRole::Human;
        players[0].money = 10000;
        players[1].role = sim::PlayerRole::Computer;
        sim::make_neutral(players[2], sim::faction::kNatives);
        world.set_players(players);
        sim::IslandMap map(30, 20);
        for (std::int32_t y = 0; y < 20; ++y) {
            for (std::int32_t x = 0; x < 30; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
    }

    void start() {
        combat = std::make_unique<sim::Combat>(world, buildings, figures);
        military = std::make_unique<sim::Military>(world, buildings, figures, rules, *combat);
    }

    /// Places a building of `owner` (city `city` of the owner) at island tile (x, y).
    void build(std::uint32_t id, std::int32_t x, std::int32_t y, sim::PlayerId owner, std::uint8_t city = 0) {
        sim::IslandMap& map = world.island(0).map;
        REQUIRE(map.place(*buildings.by_id(id), x, y, 0, 0, owner, city).ok());
        if (world.city(0, city) == nullptr) {
            sim::City c;
            c.island = 0;
            c.index = city;
            c.owner = owner;
            world.add_city(c);
        }
    }

    sim::Soldier& add(std::uint16_t figure, sim::PlayerId owner, std::int32_t cx, std::int32_t cy) {
        sim::Soldier s;
        s.place_cell(cx, cy);
        s.figure = figure;
        s.owner = owner;
        s.island = 0;
        world.add_soldier(s);
        return world.soldiers().back();
    }

    void war(sim::PlayerId a, sim::PlayerId b) {
        CHECK(world.diplomacy().set_peace(world, a, b, sim::Treaty::None, world.time_ms()));
    }

    void run(std::uint32_t ms) {
        for (std::uint32_t t = 0; t < ms; t += 50) {
            world.set_time_ms(world.time_ms() + 50);
            combat->update();
            military->tick(50, &construction);
            for (const auto& e : military->take_events()) {
                events.push_back(e);
            }
        }
    }

    [[nodiscard]] std::size_t count(sim::MilitaryEvent::Kind kind) const {
        return static_cast<std::size_t>(
            std::count_if(events.begin(), events.end(), [&](const auto& e) { return e.kind == kind; }));
    }
};

}  // namespace

TEST_CASE("military: a soldier walks to a place over open ground at its speed") {
    Field f;
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 2 * 12, 2 * 15).id;  // world tile (12, 15)
    sim::SoldierOrder order{0, {id}, sim::Target::spot(2 * 20, 2 * 15)};
    REQUIRE(f.military->order(order));
    // 8 tiles on open ground (Wegspeed 100 % here): 16 cells of 0.5 / (0.16 * 260 / 32) s = 6.15 s.
    f.run(5'000);
    const sim::Soldier* s = f.world.soldier(id);
    REQUIRE(s != nullptr);
    CHECK(s->cell_x() < 40);
    f.run(2'000);
    CHECK(s->cell_x() == 40);
    CHECK(s->cell_y() == 30);
    CHECK(s->target.empty());
}

TEST_CASE("military: next to a unit with a lower number a soldier walks slower (also across squares)") {
    // Cells 47 and 48 lie in different squares of the units' grid (16 cells, review AU7).
    const auto walked = [](bool crowded) {
        Field f;
        f.start();
        if (crowded) {
            f.add(kSword, 0, 47, 30);  // stands; the lower number
        }
        const std::uint32_t id = f.add(kSword, 0, 48, 30).id;
        REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::spot(60, 30)}));
        f.run(300);
        return f.world.soldier(id)->pos_x;
    };
    CHECK(walked(true) < walked(false));
}

TEST_CASE("military: soldiers walk around forest and buildings") {
    Field f;
    // A wall of forest across the island with a gap at the bottom.
    for (std::int32_t y = 0; y < 17; ++y) {
        REQUIRE(f.world.island(0).map.place(*f.buildings.by_id(kForest), 15, y, 0, 0).ok());
    }
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 2 * 20, 2 * 12).id;  // island tile (10, 2)
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::spot(2 * 30, 2 * 12)}));
    f.run(40'000);
    const sim::Soldier* s = f.world.soldier(id);
    REQUIRE(s != nullptr);
    CHECK(s->cell_x() == 60);
    CHECK(s->cell_y() == 24);
}

TEST_CASE("military: hostile swordsmen fight until one falls; at peace nobody fights") {
    Field f;
    f.start();
    const std::uint32_t a = f.add(kSword, 0, 40, 30).id;
    sim::Soldier& wounded = f.add(kSword, 1, 43, 30);
    wounded.health = 500;  // equal units at full health would kill each other with the same blow
    const std::uint32_t b = wounded.id;
    f.run(10'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::Shot) == 0);  // at peace
    f.war(0, 1);
    f.run(60'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 1);
    const sim::Soldier* sa = f.world.soldier(a);
    const sim::Soldier* sb = f.world.soldier(b);
    CHECK(sa != nullptr);
    CHECK(sb == nullptr);
    const sim::Soldier* survivor = sa;
    REQUIRE(survivor != nullptr);
    CHECK(survivor->health > 0);
    CHECK(survivor->health < 640);
    // The first hit does full damage: Hitpoint 1.0 = 32.
    const auto hit = std::find_if(f.events.begin(), f.events.end(),
                                  [](const auto& e) { return e.kind == sim::MilitaryEvent::Kind::Hit; });
    CHECK(hit != f.events.end());
    // The loser's player remembers who killed his unit.
    CHECK(f.world.diplomacy().blamed(1) == 0);
    CHECK(f.world.diplomacy().relation(0, 1).damage + f.world.diplomacy().relation(1, 0).damage > 0);
}

TEST_CASE("military: after the fight and after the way the unit stands, its attack or walk does not play on") {
    constexpr std::uint8_t kStand = 1;  // ANIM 1 of the soldiers; 0 walks, 2 attacks
    Field f;
    f.start();
    const std::uint32_t a = f.add(kSword, 0, 40, 30).id;
    f.add(kSword, 1, 43, 30).health = 100;
    f.war(0, 1);
    f.run(30'000);
    REQUIRE(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 1);
    f.run(5'000);
    REQUIRE(f.world.soldier(a) != nullptr);
    CHECK(f.world.soldier(a)->anim == kStand);
    // At the end of a way it stands too.
    REQUIRE(f.military->order(sim::SoldierOrder{0, {a}, sim::Target::spot(56, 30)}));
    f.run(20'000);
    CHECK(f.world.soldier(a)->anim == kStand);
}

TEST_CASE("military: a musketeer shoots from afar, a computer's swordsman closes in") {
    Field f;
    f.start();
    f.war(0, 1);
    const std::uint32_t musket = f.add(kMusket, 0, 40, 30).id;
    const std::uint32_t sword = f.add(kSword, 1, 52, 30).id;
    f.run(2'000);
    // The swordsman came closer (computer players' units chase); the musketeer stays.
    const sim::Soldier* m = f.world.soldier(musket);
    const sim::Soldier* s = f.world.soldier(sword);
    REQUIRE(m != nullptr);
    REQUIRE(s != nullptr);
    CHECK(m->cell_x() == 40);
    CHECK(s->cell_x() < 52);
    f.run(60'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 1);
    // A musket shot does 2.4 HP (76/32) at full health.
    bool musket_hit = false;
    for (const auto& e : f.events) {
        musket_hit = musket_hit || (e.kind == sim::MilitaryEvent::Kind::Hit && e.unit == sim::Target::soldier(musket));
    }
    CHECK(musket_hit);
}

TEST_CASE("military: an attack order on a building means war; the building falls, damage heals") {
    Field f;
    f.build(kHouse, 20, 5, 1);
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 2 * 26, 2 * 15).id;  // island tile (16, 5)
    sim::SoldierOrder order{0, {id}, sim::Target::building(0, 20, 5), true};
    REQUIRE(f.military->order(order));
    CHECK(f.world.diplomacy().hostile(0, 1));
    // 20 HP; a sword does 40 % against buildings (12 per hit), about one hit a second.
    f.run(30'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::WarDeclared) == 1);

    CHECK(f.military->building_damage(0, 20, 5) > 0);
    CHECK(f.military->building_damage(0, 20, 5) < 20 * 32);
    f.run(60'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::BuildingDestroyed) == 1);
    CHECK(f.world.islands()[0].map.top(20, 5).sprite != f.buildings.by_id(kHouse)->gfx);
    CHECK(f.world.soldiers().front().target.empty());  // nothing left to attack

    // Damage heals by 1 HP every 10 s.
    f.build(kHouse, 24, 10, 1, 1);
    f.military->damage_building(0, 24, 10, 100, true, 0, &f.construction);
    CHECK(f.military->building_damage(0, 24, 10) == 100);
    f.run(10'000);
    CHECK(f.military->building_damage(0, 24, 10) == 68);
}

TEST_CASE("military: a tree of nobody is shot down on order without war; one's own is left alone") {
    Field f(true);
    REQUIRE(f.world.island(0).map.place(*f.buildings.by_id(kTree), 20, 5, 0, 0).ok());
    f.build(kTree, 20, 12, 0);
    f.start();
    const std::uint32_t id = f.add(kMusket, 0, 2 * 26, 2 * 15).id;  // island tile (16, 5)
    // Without an order nobody shoots at trees.
    f.run(5'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::Shot) == 0);
    // Ctrl + click: 3 HP, a musket does 2.4 at full damage.
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::building(0, 20, 5), true}));
    f.run(20'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::WarDeclared) == 0);
    CHECK(f.count(sim::MilitaryEvent::Kind::BuildingDestroyed) == 1);
    CHECK(f.world.islands()[0].map.top(20, 5).sprite != f.buildings.by_id(kTree)->gfx);
    // A tree of one's own city: the unit walks there and shoots nothing.
    const std::size_t shots = f.count(sim::MilitaryEvent::Kind::Shot);
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::building(0, 20, 12), true}));
    f.run(20'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::Shot) == shots);
    CHECK(f.world.islands()[0].map.top(20, 12).sprite == f.buildings.by_id(kTree)->gfx);
}

TEST_CASE("military: shooting at another player's tree means war with him") {
    Field f(true);
    f.build(kTree, 20, 5, 1);
    f.start();
    const std::uint32_t id = f.add(kMusket, 0, 2 * 26, 2 * 15).id;
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::building(0, 20, 5), true}));
    CHECK(f.world.diplomacy().hostile(0, 1));
    f.run(20'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::BuildingDestroyed) == 1);
}

TEST_CASE("military: a cannon tower fires at hostile soldiers in range") {
    Field f;
    f.build(kTowerBuilding, 10, 10, 1);
    for (std::uint32_t y = 0; y < 20; ++y) {
        for (std::uint32_t x = 0; x < 30; ++x) {
            if (f.world.islands()[0].map.top(x, y).city == sim::MapCell::kNoCity) {
                f.world.island(0).map.set_city(x, y, 0, 1);
            }
        }
    }
    f.start();
    f.military->update_towers(0);
    REQUIRE(f.world.towers().size() == 1);
    CHECK(f.world.towers()[0].owner == 1);
    CHECK(f.world.towers()[0].figure == 38);
    // A musketeer of player 0 at 6 tiles: in the tower's range (7.5) but out of its own (4).
    f.add(kMusket, 0, 53, 41);
    f.run(5'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::Shot) == 0);  // at peace
    f.war(0, 1);
    // 4.0 HP every 3 s: a musketeer (15 HP) falls with the fourth hit.
    f.run(8'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 0);
    f.run(4'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 1);
}

TEST_CASE("military: a doctor heals a wounded soldier") {
    Field f;
    f.build(kDoctor, 20, 5, 0);
    f.start();
    sim::Soldier& s = f.add(kSword, 0, 2 * 26, 2 * 15);
    s.health = 100;
    const std::uint32_t id = s.id;
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::building(0, 20, 5)}));
    f.run(20'000);
    REQUIRE(f.world.soldier(id) != nullptr);
    CHECK(f.world.soldier(id)->health == 640);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierHealed) == 1);
}

TEST_CASE("military: disbanding costs the unit's price and removes it") {
    Field f;
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 40, 30).id;
    REQUIRE(f.military->command(sim::SoldierCommand{0, id, sim::SoldierCommand::Op::Disband, 0, {}}));
    f.run(2'000);
    CHECK(f.world.soldier(id) == nullptr);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 0);
}

TEST_CASE("military: patrols walk between two points") {
    Field f;
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 40, 30).id;
    REQUIRE(f.military->command(
        sim::SoldierCommand{0, id, sim::SoldierCommand::Op::SetWaypoint, 0, sim::Target::spot(40, 30)}));
    REQUIRE(f.military->command(
        sim::SoldierCommand{0, id, sim::SoldierCommand::Op::SetWaypoint, 1, sim::Target::spot(50, 30)}));
    REQUIRE(f.military->command(sim::SoldierCommand{0, id, sim::SoldierCommand::Op::Patrol, 0, {}}));
    std::int32_t max_x = 0;
    std::int32_t min_x_after = 100;
    for (int i = 0; i < 40; ++i) {
        f.run(500);
        const sim::Soldier* s = f.world.soldier(id);
        REQUIRE(s != nullptr);
        max_x = std::max(max_x, s->cell_x());
        if (max_x == 50) {
            min_x_after = std::min(min_x_after, s->cell_x());
        }
    }
    CHECK(max_x == 50);
    CHECK(min_x_after < 50);
}

TEST_CASE("military: a castle trains units with weapons and sends them out") {
    Field f;
    f.build(kCastle, 20, 5, 0);
    f.world.add_cost_building(sim::CostBuilding{sim::CostBuilding::Kind::Castle, 0, 20, 5, true});
    f.world.cost_buildings().back().weapons[0] = 3 * 32;  // 3 t swords
    f.start();
    using Op = sim::CastleOrder::Op;
    // Only soldiers: a tower (figure 38) has hit points too, but no castle trains it.
    CHECK(!f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, 38, 0, {}}));
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    const sim::CostBuilding& castle = f.world.cost_buildings().back();
    REQUIRE(castle.slots.size() == 2);
    CHECK(castle.slots[0].armed);
    CHECK(!castle.slots[1].armed);  // no swords left
    CHECK(castle.weapons[0] == 0);
    CHECK(f.military->army_size(0) == 2);
    // The second asks for a pack donkey.
    f.run(2'000);
    const auto fetch = f.military->take_fetch_requests();
    REQUIRE(fetch.size() == 1);
    CHECK(fetch[0].ware == 11);
    // 16 points every 10 s, 320 needed: the first unit is done after 200 s; it cannot leave before.
    CHECK(!f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Release, 0, 0, sim::Target::spot(40, 40)}));
    f.run(200'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::UnitTrained) == 1);
    CHECK(!castle.slots[0].training);
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Release, 0, 0, sim::Target::spot(40, 40)}));
    REQUIRE(f.world.soldiers().size() == 1);
    CHECK(f.world.soldiers()[0].health == 640);
    CHECK(f.world.soldiers()[0].target == sim::Target::spot(40, 40));
    CHECK(castle.slots.size() == 1);
    // The second has its points but no weapons; the swords arrive and it is done.
    f.run(200'000);
    CHECK(castle.slots[0].training);
    f.military->castle_delivery(0, 20, 5, 11, 3 * 32);
    f.run(1'000);
    CHECK(!castle.slots[0].training);
    // Cancelling gives the weapons back.
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Cancel, 0, 0, {}}));
    CHECK(castle.weapons[0] == 3 * 32);
    CHECK(castle.slots.empty());
}

TEST_CASE("military: Shift on the send-out arrow sends every trained unit, lined up around the spot") {
    Field f;
    f.build(kCastle, 20, 5, 0);
    f.world.add_cost_building(sim::CostBuilding{sim::CostBuilding::Kind::Castle, 0, 20, 5, true});
    sim::CastleSlot trained;
    trained.figure = kSword;
    trained.training = false;
    trained.armed = true;
    trained.progress = 320;
    trained.health = 640;
    sim::CastleSlot learning = trained;
    learning.training = true;
    learning.progress = 100;
    f.world.cost_buildings().back().slots = {trained, learning, trained};
    f.start();
    using Op = sim::CastleOrder::Op;
    const sim::CostBuilding& castle = f.world.cost_buildings().back();
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::ReleaseAll, 0, 0, sim::Target::spot(40, 40)}));
    // The unit in training stays; the others leave as one group, a tile apart, centred on the spot.
    REQUIRE(castle.slots.size() == 1);
    CHECK(castle.slots[0].training);
    REQUIRE(f.world.soldiers().size() == 2);
    CHECK(f.world.soldiers()[0].target == sim::Target::spot(39, 40));
    CHECK(f.world.soldiers()[1].target == sim::Target::spot(41, 40));
    CHECK(f.military->army_size(0) == 3);
    // Nothing trained: nothing to send.
    CHECK(!f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::ReleaseAll, 0, 0, sim::Target::spot(40, 40)}));
    CHECK(f.world.soldiers().size() == 2);
    // A full fortress: three rows of three at most, every place its own.
    f.world.cost_buildings().back().slots.assign(8, trained);
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::ReleaseAll, 0, 0, sim::Target::spot(40, 40)}));
    REQUIRE(f.world.soldiers().size() == 10);
    std::set<std::pair<std::uint32_t, std::uint32_t>> places;
    for (std::size_t i = 2; i < 10; ++i) {
        const sim::Target& t = f.world.soldiers()[i].target;
        CHECK(t.x >= 38);
        CHECK(t.x <= 42);
        CHECK(t.y >= 38);
        CHECK(t.y <= 42);
        places.emplace(t.x, t.y);
    }
    CHECK(places.size() == 8);
}

TEST_CASE("military: the unit limit is a rule, 250 by default (L-008)") {
    Field f;
    f.build(kCastle, 20, 5, 0);
    f.world.add_cost_building(sim::CostBuilding{sim::CostBuilding::Kind::Castle, 0, 20, 5, true});
    CHECK(f.rules.max_army == 250);
    CHECK(sim::Rules::original().max_army == 100);
    f.rules.max_army = 2;
    f.start();
    using Op = sim::CastleOrder::Op;
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    CHECK(!f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    f.run(50);
    CHECK(f.count(sim::MilitaryEvent::Kind::ArmyFull) == 1);
    f.rules.max_army = 0;
    CHECK(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    CHECK(f.military->army_size(0) == 3);
}

TEST_CASE("military: soldiers go on board a ship and land elsewhere") {
    Field f;
    f.start();
    // A ship of player 0 at sea just west of the island (world tile 8, 15).
    sim::Ship ship = sim::make_ship("Boat", 8, 15, 21, 0);
    ship.cargo.resize(4);
    f.world.add_ship(ship);
    const std::uint32_t ship_id = f.world.ships().back().id;
    const std::uint32_t id = f.add(kSword, 0, 2 * 14, 2 * 15).id;
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::other_ship(ship_id)}));
    f.run(10'000);
    CHECK(f.world.soldier(id) == nullptr);
    const sim::Ship* boat = f.world.ship(ship_id);
    REQUIRE(boat != nullptr);
    CHECK(boat->cargo[0].ware == 25);
    CHECK(boat->cargo[0].amount == 1600);
    CHECK(f.military->army_size(0) == 1);
    REQUIRE(f.military->unload(sim::UnloadOrder{0, ship_id}));
    REQUIRE(f.world.soldiers().size() == 1);
    CHECK(f.world.soldiers()[0].health == 640);
    CHECK(f.world.soldiers()[0].figure == kSword);
    CHECK(boat->cargo[0].ware == 0);
}

TEST_CASE("military: a package's unit trains in a castle's place, goes on board and lands as itself") {
    Field f;
    // A unit family of a package beside the swordsman (own_figures.toml), with more health.
    assets::FigureType pike = *f.figures.get(kSword);
    pike.combat.max_health = 40;
    const std::vector<assets::FigureType> colours(4, pike);
    const auto family = f.figures.add_unit_family(colours);
    REQUIRE(family.ok());
    f.start();
    sim::Ship ship = sim::make_ship("Boat", 8, 15, 21, 0);
    ship.cargo.resize(2);
    f.world.add_ship(ship);
    const std::uint32_t ship_id = f.world.ships().back().id;
    sim::Soldier& pikeman = f.add(static_cast<std::uint16_t>(*family), 0, 2 * 14, 2 * 15);
    pikeman.health = 20;  // half its health
    const std::uint32_t id = pikeman.id;
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::other_ship(ship_id)}));
    f.run(10'000);
    CHECK(f.world.soldier(id) == nullptr);
    const sim::Ship* boat = f.world.ship(ship_id);
    REQUIRE(boat != nullptr);
    // The hold shows the swordsman, the original's unit of its kind, and names the package's.
    CHECK(boat->cargo[0].ware == 25);
    CHECK(boat->cargo[0].unit == *family);
    CHECK(boat->cargo[0].amount == 800);
    CHECK(sim::hold_unit(boat->cargo[0]) == *family);
    CHECK(f.military->army_size(0) == 1);

    // A savegame keeps it (FLEE version 6).
    const auto bytes = sim::write_game(f.world, f.buildings);
    REQUIRE(bytes.ok());
    auto read = sim::read_game(*bytes, f.buildings, nullptr, &f.figures);
    REQUIRE(read.ok());
    const sim::Ship* kept = read->ship(ship_id);
    REQUIRE(kept != nullptr);
    CHECK(kept->cargo[0].unit == *family);

    REQUIRE(f.military->unload(sim::UnloadOrder{0, ship_id}));
    REQUIRE(f.world.soldiers().size() == 1);
    CHECK(f.world.soldiers()[0].figure == *family);
    CHECK(f.world.soldiers()[0].health == 20);
    CHECK(boat->cargo[0].ware == 0);
    CHECK(boat->cargo[0].unit == 0);
}

TEST_CASE("military: a script sets down units of the original and of a package") {
    Field f;
    assets::FigureType pike = *f.figures.get(kSword);
    pike.combat.max_health = 40;
    const auto family = f.figures.add_unit_family(std::vector<assets::FigureType>(4, pike));
    REQUIRE(family.ok());
    f.world.set_own_figures({assets::OwnFigure{"guards:pike", *family}});
    sim::Simulation simulation(f.world, f.buildings, f.figures);
    std::vector<script::PackageScript> sources;
    sources.push_back(script::PackageScript{"guards",
                                            "function on_start()\n"
                                            "  print(game.add_unit(0, 'anno1602:swordsman', 14, 15) ~= false,\n"
                                            "        game.add_unit(1, 'pike', 16, 15) ~= false,\n"
                                            "        game.add_unit(1, " +
                                                std::to_string(*family + 1) +
                                                ", 16, 16),\n"  // a colour, not a family
                                                "        game.add_unit(0, 'anno1602:carrier', 14, 16),\n"
                                                "        game.add_unit(0, 'pike', 2, 2))\n"  // the sea
                                                "  print(pcall(game.add_unit, 5, 'pike', 14, 15))\n"
                                                "end\n",
                                            {}});
    auto scripts = std::make_unique<script::GameScripts>(std::move(sources), f.world);
    simulation.set_scripts(scripts.get());
    simulation.tick();
    const auto notes = scripts->take_notes();
    simulation.set_scripts(nullptr);
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].text == "true\ttrue\tfalse\tfalse\tfalse");
    CHECK(notes[1].text.find("no such player") != std::string::npos);
    REQUIRE(f.world.soldiers().size() == 2);
    const sim::Soldier& sword = f.world.soldiers()[0];
    CHECK(sword.figure == kSword);
    CHECK(sword.owner == 0);
    CHECK(sword.island == 0);
    CHECK(sword.health == f.figures.get(kSword)->combat.max_health);
    const sim::Soldier& guard = f.world.soldiers()[1];
    CHECK(guard.figure == *family);
    CHECK(guard.owner == 1);
    CHECK(guard.health == 40);
}

TEST_CASE("military: a ship from the original's files takes as many soldiers as its type has holds") {
    Field f;
    f.start();
    // Eight cargo slots as the original keeps them for every ship; the type has two holds.
    sim::Ship ship = sim::make_ship("Boat", 8, 15, 21, 0);
    ship.cargo.resize(8);
    f.world.add_ship(ship);
    const std::uint32_t ship_id = f.world.ships().back().id;
    std::vector<std::uint32_t> ids;
    for (std::int32_t i = 0; i < 3; ++i) {
        ids.push_back(f.add(kSword, 0, 2 * 14, 2 * (14 + i)).id);
    }
    REQUIRE(f.military->order(sim::SoldierOrder{0, ids, sim::Target::other_ship(ship_id)}));
    f.run(10'000);
    const sim::Ship* boat = f.world.ship(ship_id);
    REQUIRE(boat != nullptr);
    CHECK(sim::usable_holds(*boat, f.figures) == 2);
    CHECK(boat->cargo[0].ware == 25);
    CHECK(boat->cargo[1].ware == 25);
    for (std::size_t h = 2; h < boat->cargo.size(); ++h) {
        CHECK(boat->cargo[h].ware == 0);
    }
    CHECK(f.world.soldiers().size() == 1);  // the third stays ashore
}

TEST_CASE("military: a Kontor over the destroyed Kontor of a defenceless city takes the city over") {
    Field f;
    f.build(kKontorDestroyed, 20, 5, 1);
    for (std::uint32_t y = 0; y < 12; ++y) {
        for (std::uint32_t x = 14; x < 28; ++x) {
            if (f.world.islands()[0].map.top(x, y).city == sim::MapCell::kNoCity) {
                f.world.island(0).map.set_city(x, y, 0, 1);
            }
        }
    }
    f.world.city(0, 0)->warehouse_x = 20;
    f.world.city(0, 0)->warehouse_y = 5;
    CHECK(sim::city_defenceless(f.world, f.buildings, 0, 0));
    sim::BuildOrder order;
    order.player = 0;
    order.items = {sim::BuildItem{kKontor, 20, 5, 0, 0}};
    std::vector<sim::ConstructionEvent> events;
    const sim::BuildPlan plan = sim::execute_build(f.world, f.buildings, order, {}, &events);
    CHECK(plan.takeover);
    REQUIRE(plan.placed == 1);
    CHECK(f.world.city(0, 0)->owner == 0);
    CHECK(f.world.islands()[0].map.top(15, 2).owner == 0);
    CHECK(std::any_of(events.begin(), events.end(),
                      [](const auto& e) { return e.kind == sim::ConstructionEvent::Kind::CityTaken; }));
    // The same from a ship before the ruin, as its Kontor button offers it.
    {
        Field h;
        h.build(kKontorDestroyed, 20, 5, 1);
        h.world.city(0, 0)->warehouse_x = 20;
        h.world.city(0, 0)->warehouse_y = 5;
        sim::Ship ship = sim::make_ship("Boat", 30, 8, 21, 0);
        ship.cargo.resize(2);
        const auto& m = h.buildings.materials();
        ship.cargo[0] = sim::CargoSlot{m.tools, 30 * 32, 0};
        ship.cargo[1] = sim::CargoSlot{m.wood, 30 * 32, 0};
        h.world.add_ship(ship);
        sim::BuildOrder from_ship = order;
        from_ship.ship = h.world.ships().back().id;
        const sim::BuildPlan taken = sim::execute_build(h.world, h.buildings, from_ship, {}, nullptr);
        CHECK(taken.takeover);
        CHECK(taken.placed == 1);
        CHECK(h.world.city(0, 0)->owner == 0);
    }
    // A city with a standing Kontor cannot be taken.
    Field g;
    g.build(kKontor, 20, 5, 1);
    CHECK(!sim::city_defenceless(g.world, g.buildings, 0, 0));
    sim::BuildOrder again;
    again.player = 0;
    again.items = {sim::BuildItem{kKontor, 20, 5, 0, 0}};
    CHECK(!sim::plan_build(g.world, g.buildings, again).takeover);
}

TEST_CASE("military: natives curse who destroyed their village") {
    Field f;
    f.build(kNativeHut, 20, 5, 2, 1);
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 2 * 26, 2 * 15).id;
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::building(0, 20, 5), true}));
    f.run(40'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::BuildingDestroyed) == 1);
    CHECK(f.world.diplomacy().blamed(2) == 0);
    REQUIRE(f.count(sim::MilitaryEvent::Kind::NativesCurse) == 1);
    const auto curse = std::find_if(f.events.begin(), f.events.end(),
                                    [](const auto& e) { return e.kind == sim::MilitaryEvent::Kind::NativesCurse; });
    CHECK(curse->player == 0);
    CHECK(curse->pirates == f.world.islands()[0].drought.curse_pending);
}

TEST_CASE("military: a village without huts is gone, its land free, and it curses once") {
    Field f(false, true);
    // The ruin of the chief's house holds the land, as a destroyed Kontor does; three huts.
    f.build(kKontorRuin, 20, 5, 2, 1);
    f.world.city(0, 1)->warehouse_x = 20;
    f.world.city(0, 1)->warehouse_y = 5;
    for (const std::int32_t x : {14, 16, 18}) {
        f.build(kVillageHut, x, 9, 2, 1);
    }
    f.world.island(0).map.set_city(24, 12, 1, 2);
    CHECK(sim::city_defenceless(f.world, f.buildings, 0, 1));
    CHECK(!sim::native_village_gone(f.world, f.buildings, 0, 1));
    f.world.diplomacy().set_blamed(2, 0);  // player 0 destroyed the chief's house
    f.start();
    for (const std::uint16_t x : std::array<std::uint16_t, 3>{14, 16, 18}) {
        REQUIRE(f.world.city(0, 1) != nullptr);
        f.military->damage_building(0, x, 9, 32 * 3, true, 0, &f.construction);
        f.run(50);
    }
    CHECK(f.count(sim::MilitaryEvent::Kind::BuildingDestroyed) == 3);
    // Fewer than three huts cursed the destroyer; the last hut dissolved the village, no second curse.
    CHECK(f.count(sim::MilitaryEvent::Kind::NativesCurse) == 1);
    CHECK(f.world.city(0, 1) == nullptr);
    CHECK(f.world.islands()[0].map.top(24, 12).city == sim::MapCell::kNoCity);
    CHECK(f.world.islands()[0].map.top(20, 5).city == sim::MapCell::kNoCity);
    CHECK(std::any_of(f.construction.begin(), f.construction.end(),
                      [](const auto& e) { return e.kind == sim::ConstructionEvent::Kind::CityDissolved; }));
}

TEST_CASE("military: as in the original, the ruins of a village without huts keep its land") {
    Field f(false, true);
    sim::OriginalBugs bugs;
    bugs.native_land = true;
    f.world.set_original_bugs(bugs);
    f.build(kKontorRuin, 20, 5, 2, 1);
    f.world.city(0, 1)->warehouse_x = 20;
    f.world.city(0, 1)->warehouse_y = 5;
    for (const std::int32_t x : {14, 16, 18}) {
        f.build(kVillageHut, x, 9, 2, 1);
    }
    f.world.island(0).map.set_city(24, 12, 1, 2);
    f.start();
    for (const std::uint16_t x : std::array<std::uint16_t, 3>{14, 16, 18}) {
        f.military->damage_building(0, x, 9, 32 * 3, true, 0, &f.construction);
        f.run(50);
    }
    CHECK(f.count(sim::MilitaryEvent::Kind::BuildingDestroyed) == 3);
    CHECK(sim::native_village_gone(f.world, f.buildings, 0, 1));
    CHECK(f.world.city(0, 1) != nullptr);
    CHECK(f.world.islands()[0].map.top(24, 12).city == 1);
    CHECK(std::none_of(f.construction.begin(), f.construction.end(),
                       [](const auto& e) { return e.kind == sim::ConstructionEvent::Kind::CityDissolved; }));
}

TEST_CASE("military: a computer gives up a city without Kontor, market, tower and houses") {
    for (const bool original : {false, true}) {
        Field f;
        sim::OriginalBugs bugs;
        bugs.native_land = original;
        f.world.set_original_bugs(bugs);
        // Player 1, a computer: its Kontor destroyed, two houses left.
        f.build(kKontorDestroyed, 20, 5, 1);
        f.world.city(0, 0)->warehouse_x = 20;
        f.world.city(0, 0)->warehouse_y = 5;
        for (const std::int32_t x : {14, 16}) {
            f.build(kHouse, x, 9, 1);
        }
        f.world.island(0).map.set_city(24, 12, 0, 1);
        CHECK(sim::city_defenceless(f.world, f.buildings, 0, 0));
        CHECK(!sim::computer_city_gone(f.world, f.buildings, 0, 0));
        f.start();
        f.military->damage_building(0, 14, 9, 32 * 30, true, 0, &f.construction);
        f.run(50);
        REQUIRE(f.world.city(0, 0) != nullptr);  // one house still stands: it can be taken over
        f.military->damage_building(0, 16, 9, 32 * 30, true, 0, &f.construction);
        f.run(50);
        CHECK(f.count(sim::MilitaryEvent::Kind::BuildingDestroyed) == 2);
        if (original) {
            // The ruin of the Kontor holds the land, as in the original.
            CHECK(f.world.city(0, 0) != nullptr);
            CHECK(f.world.islands()[0].map.top(24, 12).city == 0);
        } else {
            CHECK(f.world.city(0, 0) == nullptr);
            CHECK(f.world.islands()[0].map.top(24, 12).city == sim::MapCell::kNoCity);
            CHECK(f.world.islands()[0].map.top(20, 5).city == sim::MapCell::kNoCity);
        }
    }
    // Razed before (a savegame): given up with the cities' next check.
    {
        Field r;
        r.build(kKontorDestroyed, 20, 5, 1);
        r.world.city(0, 0)->warehouse_x = 20;
        r.world.city(0, 0)->warehouse_y = 5;
        std::vector<sim::ConstructionEvent> events;
        CHECK(sim::give_up_razed_cities(r.world, r.buildings, &events) == 1);
        CHECK(r.world.city(0, 0) == nullptr);
        CHECK(std::any_of(events.begin(), events.end(),
                          [](const auto& e) { return e.kind == sim::ConstructionEvent::Kind::CityDissolved; }));
        // Not a city whose Kontor stands, nor one of a human player.
        Field k;
        k.build(kKontor, 20, 5, 1);
        k.world.city(0, 0)->warehouse_x = 20;
        k.world.city(0, 0)->warehouse_y = 5;
        k.build(kKontorDestroyed, 10, 5, 0, 1);
        CHECK(sim::give_up_razed_cities(k.world, k.buildings, nullptr) == 0);
    }
    // A human player's city stays.
    Field h;
    h.build(kKontorDestroyed, 20, 5, 0);
    h.build(kHouse, 14, 9, 0);
    h.start();
    h.military->damage_building(1, 14, 9, 32 * 30, true, 0, &h.construction);
    h.run(50);
    CHECK(h.world.city(0, 0) != nullptr);
}

TEST_CASE("military: a group keeps its shape; queued orders follow one after another") {
    Field f;
    f.start();
    const std::uint32_t a = f.add(kSword, 0, 2 * 12, 2 * 15).id;
    const std::uint32_t b = f.add(kSword, 0, 2 * 12, 2 * 17).id;
    // b one tile below a: offsets in 1/8 tile around the spot.
    sim::SoldierOrder order{0, {a, b}, sim::Target::spot(2 * 20, 2 * 15)};
    order.offsets = {{0, -4}, {0, 4}};
    REQUIRE(f.military->order(order));
    CHECK(f.world.soldier(a)->target == sim::Target::spot(40, 29));
    CHECK(f.world.soldier(b)->target == sim::Target::spot(40, 31));
    // Two more places queued for a: it walks to them in turn.
    sim::SoldierOrder next{0, {a}, sim::Target::spot(2 * 20, 2 * 20)};
    next.queue = true;
    REQUIRE(f.military->order(next));
    next.target = sim::Target::spot(2 * 14, 2 * 20);
    REQUIRE(f.military->order(next));
    CHECK(f.world.soldier(a)->queue.size() == 2);
    CHECK(f.world.soldier(a)->target == sim::Target::spot(40, 29));
    f.run(40'000);
    const sim::Soldier* s = f.world.soldier(a);
    CHECK(s->queue.empty());
    CHECK(s->cell_x() == 28);
    CHECK(s->cell_y() == 40);
    // A plain order clears the queue.
    REQUIRE(f.military->order(next));
    REQUIRE(f.military->order(next));
    CHECK(!f.world.soldier(a)->queue.empty());
    REQUIRE(f.military->order(sim::SoldierOrder{0, {a}, sim::Target::spot(2 * 12, 2 * 15)}));
    CHECK(f.world.soldier(a)->queue.empty());
}

TEST_CASE("military: an attack ends a pending offer of peace too") {
    Field f;
    f.start();
    f.war(0, 1);
    f.world.diplomacy().relation(0, 1).peace = sim::Treaty::Offered;
    f.world.diplomacy().relation(1, 0).peace = sim::Treaty::Received;
    const sim::Soldier& enemy = f.add(kSword, 1, 2 * 25, 2 * 15);
    CHECK(f.military->declare_war_by_attack(0, sim::Target::soldier(enemy.id)));
    CHECK(f.world.diplomacy().relation(0, 1).peace == sim::Treaty::None);
    CHECK(!f.military->declare_war_by_attack(0, sim::Target::soldier(enemy.id)));  // at war already
}

TEST_CASE("military: an attack move fights on the way; holding the position chases nobody") {
    for (const bool attack_move : {false, true}) {
        Field f;
        f.start();
        f.war(0, 1);
        const std::uint32_t id = f.add(kSword, 0, 2 * 12, 2 * 15).id;
        sim::Soldier& enemy = f.add(kSword, 1, 2 * 20, 2 * 18);
        enemy.health = 200;
        enemy.hold = true;  // it stays where it is
        sim::SoldierOrder order{0, {id}, sim::Target::spot(2 * 28, 2 * 15)};
        order.attack_move = attack_move;
        REQUIRE(f.military->order(order));
        f.run(30'000);
        // A melee unit of the player walking somewhere leaves enemies off its way alone (original);
        // with the attack move it fights them and goes on.
        CHECK((f.count(sim::MilitaryEvent::Kind::Shot) > 0) == attack_move);
        f.run(30'000);
        const sim::Soldier* s = f.world.soldier(id);
        REQUIRE(s != nullptr);
        CHECK(s->cell_x() == 56);
        CHECK(s->cell_y() == 30);
    }
    // Holding the position: no chase of an enemy three tiles away.
    Field f;
    f.start();
    f.war(0, 1);
    const std::uint32_t id = f.add(kSword, 0, 2 * 12, 2 * 15).id;
    f.add(kSword, 1, 2 * 15, 2 * 15).hold = true;
    sim::SoldierCommand hold;
    hold.player = 0;
    hold.soldier = id;
    hold.op = sim::SoldierCommand::Op::Hold;
    REQUIRE(f.military->command(hold));
    f.run(10'000);
    CHECK(f.world.soldier(id)->cell_x() == 24);
    CHECK(f.count(sim::MilitaryEvent::Kind::Shot) == 0);
    // The next order ends it.
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::spot(2 * 12, 2 * 17)}));
    CHECK(!f.world.soldier(id)->hold);
}

TEST_CASE("military: a unit sees the nearest of many enemies, nearest first (S-114)") {
    Field f;
    f.war(0, 1);
    const std::uint32_t me = f.add(kSword, 0, 50, 58).id;
    // Forty enemies to the north, 8 to 10 tiles away, and one right beside.
    for (std::int32_t k = 0; k < 40; ++k) {
        f.add(kSword, 1, 40 + k % 10, 38 + k / 10);
    }
    const std::uint32_t beside = f.add(kSword, 1, 52, 58).id;
    f.start();
    f.combat->update();
    const auto list = f.combat->hostiles(sim::Target::soldier(me));
    REQUIRE(list.size() == sim::Combat::kMaxNeighbours);
    CHECK(list[0].unit == sim::Target::soldier(beside));
    for (std::size_t i = 1; i < list.size(); ++i) {
        CHECK(list[i - 1].distance8 <= list[i].distance8);
    }
    // Each of the enemies sees the one unit of player 0.
    const auto theirs = f.combat->hostiles(sim::Target::soldier(beside));
    REQUIRE(theirs.size() == 1);
    CHECK(theirs[0].unit == sim::Target::soldier(me));
}

TEST_CASE("military: at most a fixed number of way searches per tick, the others follow in order (TB-1)") {
    Field f;
    f.start();
    std::vector<std::uint32_t> ids;
    for (std::int32_t i = 0; i < 18; ++i) {
        ids.push_back(f.add(kSword, 0, 2 * 12, 2 * 11 + 2 * i).id);
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const std::uint32_t cy = 2 * 11 + 2 * static_cast<std::uint32_t>(i);
        REQUIRE(f.military->order(sim::SoldierOrder{0, {ids[i]}, sim::Target::spot(2 * 30, cy)}));
    }
    const auto planned = [&] {
        std::size_t n = 0;
        for (const std::uint32_t id : ids) {
            n += f.world.soldier(id)->motion.path.empty() ? 0U : 1U;
        }
        return n;
    };
    f.run(50);
    CHECK(planned() == sim::Military::kPlansPerTick);
    for (std::size_t i = 0; i < sim::Military::kPlansPerTick; ++i) {
        CHECK(!f.world.soldier(ids[i])->motion.path.empty());
    }
    f.run(50);
    CHECK(planned() == 2 * sim::Military::kPlansPerTick);
    f.run(50);
    CHECK(planned() == ids.size());
    f.run(20'000);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        CHECK(f.world.soldier(ids[i])->cell_x() == 2 * 30);
        CHECK(f.world.soldier(ids[i])->target.empty());
    }
}

TEST_CASE("military: idle units look again at times spread by their number (TB-1)") {
    Field f;
    f.start();
    const std::uint32_t a = f.add(kSword, 0, 2 * 12, 2 * 12).id;
    const std::uint32_t b = f.add(kSword, 0, 2 * 14, 2 * 12).id;
    f.run(50);
    const sim::Soldier* sa = f.world.soldier(a);
    const sim::Soldier* sb = f.world.soldier(b);
    REQUIRE(sa->motion.phase == sim::SoldierMotion::Phase::Wait);
    REQUIRE(sb->motion.phase == sim::SoldierMotion::Phase::Wait);
    CHECK(sa->motion.duration_us != sb->motion.duration_us);
    CHECK(sa->motion.duration_us >= 1'000'000);
    CHECK(sb->motion.duration_us >= 1'000'000);
    CHECK(sa->motion.duration_us < 1'200'000);
    CHECK(sb->motion.duration_us < 1'200'000);
}

TEST_CASE("military: finding units by id follows adds and removals without a new build (TB-6)") {
    sim::World world(64, 64);
    // Ids out of order, as after loading an original game, then new units at the end.
    for (const std::uint32_t id : {9U, 3U, 7U, 1U, 5U}) {
        sim::Soldier s;
        s.id = id;
        world.add_soldier(s);
    }
    const auto check_all = [&] {
        for (std::size_t i = 0; i < world.soldiers().size(); ++i) {
            const sim::Soldier& s = world.soldiers()[i];
            REQUIRE(world.soldier(s.id) == &s);
        }
    };
    check_all();
    for (int i = 0; i < 6; ++i) {
        world.add_soldier(sim::Soldier{});
        check_all();
    }
    CHECK(world.soldiers().back().id == 15);
    world.erase_soldiers([](const sim::Soldier& s) { return s.id % 3 == 0; });
    CHECK(world.soldiers().size() == 7);
    CHECK(world.soldier(3) == nullptr);
    CHECK(world.soldier(9) == nullptr);
    CHECK(world.soldier(12) == nullptr);
    check_all();
    // The order of the others stays.
    std::vector<std::uint32_t> ids;
    for (const sim::Soldier& s : world.soldiers()) {
        ids.push_back(s.id);
    }
    CHECK((ids == std::vector<std::uint32_t>{7, 1, 5, 10, 11, 13, 14}));
    world.erase_soldiers([](const sim::Soldier&) { return false; });
    world.add_soldier(sim::Soldier{});
    check_all();
    world.erase_soldiers([](const sim::Soldier&) { return true; });
    CHECK(world.soldiers().empty());
    CHECK(world.soldier(7) == nullptr);
}

TEST_CASE("military: a battle of two armies develops the same on every platform (golden hash)") {
    Field f;
    f.start();
    f.war(0, 1);
    for (std::int32_t i = 0; i < 4; ++i) {
        f.add(kSword, 0, 36, 24 + 2 * i);
        f.add(kSword, 1, 50, 25 + 2 * i);
    }
    for (std::int32_t i = 0; i < 3; ++i) {
        f.add(kMusket, 0, 33, 25 + 2 * i);
        f.add(kMusket, 1, 53, 24 + 2 * i);
    }
    f.run(45'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) > 0);
    test::check_golden("battle after 45 s", test::world_hash(f.world, f.buildings), 0xe7930b594ea8cc19ULL);
}

namespace {

/// The original's ruin records (assets::id::kRuins): a Kontor I (Ruinenr 8) falls to the
/// destroyed Kontor I (22121), a pier (Ruinenr 2) to the land ruin 22611 or on beach 22620.
assets::BuildingTable make_ruin_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"                   // open ground
        "  @Nummer: 1\n  Id: 20102\n  Kind: 23\n  Gfx: 1\n  Size: 1, 1\n"                   // beach
        "  @Nummer: 2\n  Id: 20103\n  Kind: 14\n  Gfx: 2\n  Size: 2, 2\n  Maxenergy: 20\n"  // a house
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20108\n  Kind: 35\n  Gfx: 30\n  Size: 2, 2\n  Maxenergy: 20\n"
        "  KreuzBase: 6\n  Ausbauflg: 1\n  Ruinenr: 8\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"  // Kontor I
        "  @Nummer: 4\n  Id: 22121\n  Kind: 35\n  Gfx: 40\n  Size: 2, 2\n"
        "  KreuzBase: 6\n  Ausbauflg: 1\n  Destroyflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"                     // destroyed Kontor I
        "  @Nummer: 5\n  Id: 21900\n  Kind: 30\n  Gfx: 50\n  Size: 1, 1\n  Maxenergy: 10\n"  // a pier
        "  Ruinenr: 2\n"
        "  @Nummer: 6\n  Id: 22611\n  Kind: 12\n  Gfx: 60\n  Size: 1, 1\n"  // land ruin
        "  @Nummer: 7\n  Id: 22620\n  Kind: 29\n  Gfx: 61\n  Size: 1, 1\n"  // beach ruin
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("military: a destroyed Kontor becomes the destroyed Kontor; the city keeps its land (S-018)") {
    const assets::BuildingTable buildings = make_ruin_buildings();
    const assets::FigureTable figures = make_figures();
    sim::World world{80, 60};
    std::vector<sim::Player> players(2);
    players[0].role = sim::PlayerRole::Human;
    players[1].role = sim::PlayerRole::Computer;
    world.set_players(players);
    sim::IslandMap map(30, 20);
    for (std::int32_t y = 0; y < 20; ++y) {
        for (std::int32_t x = 0; x < 30; ++x) {
            REQUIRE(map.place(*buildings.by_id(x == 5 ? 20102 : 20101), x, y, 0, 0, 1, 0).ok());
        }
    }
    map.capture_ground();
    REQUIRE(map.build(*buildings.by_id(20108), 10, 5, 0, 0, 1, 0).ok());
    REQUIRE(map.build(*buildings.by_id(20103), 13, 5, 0, 0, 1, 0).ok());
    REQUIRE(map.build(*buildings.by_id(21900), 5, 6, 0, 0, 1, 0).ok());  // on beach
    REQUIRE(map.build(*buildings.by_id(21900), 6, 6, 0, 0, 1, 0).ok());  // on ground
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
    sim::City c;
    c.island = 0;
    c.index = 0;
    c.owner = 1;
    c.warehouse_x = 10;
    c.warehouse_y = 5;
    world.add_city(c);
    const sim::Rules rules = sim::Rules::open1602();
    sim::Combat combat(world, buildings, figures);
    sim::Military military(world, buildings, figures, rules, combat);
    std::vector<sim::ConstructionEvent> construction;

    military.damage_building(0, 10, 5, 20 * 32, true, 0, &construction);
    const sim::IslandMap& m = world.islands()[0].map;
    CHECK(m.top(10, 5).sprite == buildings.by_id(22121)->gfx);
    CHECK(m.top(11, 6).owner == 1);
    REQUIRE(world.city(0, 0) != nullptr);  // not dissolved
    CHECK(world.city(0, 0)->warehouse_x == std::optional<std::uint16_t>{10});
    CHECK(m.top(13, 5).sprite == buildings.by_id(20103)->gfx);  // the house stands
    CHECK(m.top(14, 9).city == 0);                              // the land stays the city's
    CHECK(sim::city_defenceless(world, buildings, 0, 0));
    // The destroyed Kontor is no target any more.
    CHECK(!combat.fighter(sim::Target::building(0, 10, 5)).has_value());
    // Ships still trade at the destroyed Kontor of a computer player in the game, but not of a
    // defeated or neutral owner.
    CHECK(sim::kontor_open(world, buildings, *world.city(0, 0)));
    world.player(1)->status = sim::PlayerStatus::Defeated;
    CHECK(!sim::kontor_open(world, buildings, *world.city(0, 0)));
    world.player(1)->status = sim::PlayerStatus::Playing;
    world.player(1)->role = sim::PlayerRole::Neutral;
    CHECK(!sim::kontor_open(world, buildings, *world.city(0, 0)));
    world.player(1)->role = sim::PlayerRole::Computer;

    // A pier on beach falls to the beach ruin, drawn at the height of the sand; on land to the land ruin.
    military.damage_building(0, 5, 6, 10 * 32, true, 0, &construction);
    military.damage_building(0, 6, 6, 10 * 32, true, 0, &construction);
    CHECK(m.top(5, 6).sprite == buildings.by_id(22620)->gfx);
    CHECK(m.top(6, 6).sprite == buildings.by_id(22611)->gfx);
}

TEST_CASE("military: a soldier walks onto a place however near; a taken place is near enough") {
    for (const std::int32_t d : {1, 2, 4, 6}) {
        Field f;
        f.start();
        const std::uint32_t id = f.add(kSword, 0, 24, 30).id;
        REQUIRE(
            f.military->order(sim::SoldierOrder{0, {id}, sim::Target::spot(static_cast<std::uint32_t>(24 + d), 30)}));
        f.run(5'000);
        CHECK(f.world.soldier(id)->cell_x() == 24 + d);
        CHECK(f.world.soldier(id)->target.empty());
    }
    // Another unit stands on the place: the soldier stays where it is, near enough.
    Field f;
    f.start();
    f.add(kSword, 0, 27, 30);
    const std::uint32_t id = f.add(kSword, 0, 24, 30).id;
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::spot(27, 30)}));
    f.run(5'000);
    CHECK(f.world.soldier(id)->cell_x() == 24);
    CHECK(f.world.soldier(id)->target.empty());
}

TEST_CASE("military: units sent to one place never stand on one cell (History Edition)") {
    Field f;
    f.start();
    std::vector<std::uint32_t> ids;
    for (std::int32_t i = 0; i < 6; ++i) {
        ids.push_back(f.add(kSword, 0, 24 + 2 * i, 24).id);
    }
    // Two orders to the same place, the second while the first group stands there.
    REQUIRE(f.military->order(sim::SoldierOrder{0, {ids[0], ids[1], ids[2]}, sim::Target::spot(40, 34)}));
    f.run(20'000);
    REQUIRE(f.military->order(sim::SoldierOrder{0, {ids[3], ids[4], ids[5]}, sim::Target::spot(40, 34)}));
    f.run(20'000);
    std::set<std::pair<std::int32_t, std::int32_t>> cells;
    for (const std::uint32_t id : ids) {
        const sim::Soldier* s = f.world.soldier(id);
        CHECK(s->target.empty());
        CHECK(std::abs(s->cell_x() - 40) <= 8);
        CHECK(std::abs(s->cell_y() - 34) <= 8);
        cells.emplace(s->cell_x(), s->cell_y());
    }
    CHECK(cells.size() == ids.size());
}

TEST_CASE("military: soldiers destroy an enemy trade cart; it does not fight back, ships leave it alone") {
    Field f;
    f.start();
    sim::Fleet fleet(f.world, f.buildings, f.figures, f.rules);
    f.military->set_fleet(&fleet);
    // Player 1's cart on the island at world tile (20, 15), player 0's armed ship just off the coast.
    sim::Ship cart = sim::make_ship("Cart", 20, 15, assets::figure::kTradeCart, 1, 4);
    cart.cargo.resize(3);
    f.world.add_ship(cart);
    const std::uint32_t cart_id = f.world.ships().back().id;
    sim::Ship ship = sim::make_ship("Boat", 8, 15, 21, 0);
    ship.cargo.resize(2);
    ship.cannons = 4;
    f.world.add_ship(ship);
    const std::uint32_t ship_id = f.world.ships().back().id;
    const std::uint32_t id = f.add(kMusket, 0, 2 * 24, 2 * 15).id;
    f.war(0, 1);
    f.combat->update();
    const auto sees = [&](const sim::Target& who, std::uint32_t cart_ship) {
        const auto list = f.combat->hostiles(who);
        return std::any_of(list.begin(), list.end(), [&](const sim::Neighbour& n) {
            return n.unit.kind == sim::Target::Kind::Ship && n.unit.ship == cart_ship;
        });
    };
    CHECK(sees(sim::Target::soldier(id), cart_id));
    CHECK(!sees(sim::Target::other_ship(ship_id), cart_id));
    CHECK(f.combat->hostiles(sim::Target::other_ship(cart_id)).empty());  // a cart does not fight
    const auto fighter = f.combat->fighter(sim::Target::other_ship(cart_id));
    REQUIRE(fighter.has_value());
    CHECK(fighter->damage == 0);
    CHECK(!fighter->ship);
    CHECK(fighter->island == 0);

    const std::uint32_t sunk_before = f.world.player(0)->ships_sunk;
    std::vector<sim::ShipEvent> ship_events;
    for (std::uint32_t t = 0; t < 30'000; t += 50) {
        f.world.set_time_ms(f.world.time_ms() + 50);
        f.combat->update();
        fleet.tick(50);
        f.military->tick(50, &f.construction);
        for (const auto& e : fleet.take_events()) {
            ship_events.push_back(e);
        }
    }
    CHECK(f.world.ship(cart_id) == nullptr);  // destroyed and gone
    CHECK(std::count_if(ship_events.begin(), ship_events.end(), [&](const sim::ShipEvent& e) {
              return e.kind == sim::ShipEvent::Kind::Sunk && e.ship == cart_id;
          }) == 1);
    CHECK(f.world.player(0)->ships_sunk == sunk_before);  // no ship of the score
}
