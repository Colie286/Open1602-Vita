// SPDX-License-Identifier: GPL-3.0-or-later
// Missions of scenarios, victory and defeat.

#include <algorithm>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;
using sim::GameEvent;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint8_t kGold = 3;
constexpr std::uint8_t kTools = 6;

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
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
        "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

sim::Mission::CityGoal goal(std::int32_t inhabitants, std::uint8_t resident_class, std::uint32_t count) {
    sim::Mission::CityGoal g;
    g.inhabitants = inhabitants;
    g.resident_class = resident_class;
    g.count = count;
    return g;
}

/// Three islands of 10 x 10 tiles; the first two have gold. Players: 0 and 1 human, 2 computer,
/// 3 natives.
struct Game {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{120, 60};

    Game() {
        for (std::uint16_t i = 0; i < 3; ++i) {
            sim::IslandMap map(10, 10);
            for (std::int32_t y = 0; y < 10; ++y) {
                for (std::int32_t x = 0; x < 10; ++x) {
                    REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
                }
            }
            assets::IslandHeader header;
            header.number = static_cast<std::uint8_t>(i);
            header.width = 10;
            header.height = 10;
            if (i < 2) {
                header.deposits.push_back({kGold, 2, 2, 0, 0, 900});
            }
            REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), 10U + 30U * i, 20}).ok());
        }
        std::vector<sim::Player> players(4);
        players[0].role = sim::PlayerRole::Human;
        players[1].role = sim::PlayerRole::Human;
        players[2].role = sim::PlayerRole::Computer;
        sim::make_neutral(players[3], sim::faction::kNatives);
        for (sim::Player& p : players) {
            p.money = 10'000;
        }
        world.set_players(players);
    }

    sim::City& city(std::uint16_t island, std::uint8_t index, sim::PlayerId owner, std::vector<std::uint32_t> inh) {
        sim::City c;
        c.island = island;
        c.index = index;
        c.owner = owner;
        c.inhabitants = std::move(inh);
        c.inhabitants.resize(5, 0);
        world.add_city(c);
        return *world.city(island, index);
    }
    void ship(sim::PlayerId owner) {
        sim::Ship s;
        s.owner = owner;
        s.x = 5;
        s.y = 5;
        s.figure = 21;
        world.add_ship(s);
    }
    /// Gives `player` mission `m` (as a new mission).
    void mission(sim::PlayerId player, const sim::Mission& m) {
        auto missions = world.missions();
        missions.push_back(m);
        world.set_missions(missions);
        world.player(player)->mission = static_cast<sim::MissionId>(missions.size() - 1);
        sim::prepare_missions(world);
    }
    [[nodiscard]] sim::MissionCheck check(sim::PlayerId p) const {
        return sim::check_mission(world, buildings, figures, p);
    }
};

std::vector<GameEvent> run(sim::Simulation& s, std::uint32_t ms) {
    std::vector<GameEvent> events;
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
        for (const GameEvent& e : s.take_game_events()) {
            events.push_back(e);
        }
    }
    return events;
}

bool has(const std::vector<GameEvent>& events, GameEvent::Kind kind, sim::PlayerId player) {
    return std::any_of(events.begin(), events.end(),
                       [&](const GameEvent& e) { return e.kind == kind && e.player == player; });
}

}  // namespace

TEST_CASE("missions: a record is taken like the original's loader takes it") {
    assets::MissionRecord r;
    r.goals = 0x40 | 0x80 | 0x4;  // impossible bit, inhabitants, a stray city bit
    r.losses = 0x5;               // bit 2 is unknown
    r.target = 7;
    r.defeat = {0, 1, 1, 0, 0, 0, 0};
    r.cities[0] = {1500, 4, 1500};
    r.cities[1] = {2500, 4, 2500};
    r.every_city = {10, 9, 1};  // class out of range
    std::vector<std::string> warnings;
    const sim::Mission m = sim::mission_from_record(r, &warnings);
    // City goals sorted by size, the bits follow the goals; 0x40 and loss bit 2 dropped (S-111).
    CHECK(m.goals == (sim::goal::kCity1 | sim::goal::kCity2 | sim::goal::kInhabitants));
    CHECK(m.losses == sim::loss::kProtectedCity);
    CHECK(m.cities[0].inhabitants == 2500);
    CHECK(m.cities[1].inhabitants == 1500);
    CHECK(m.target == sim::kNoPlayer);
    CHECK((m.defeat == std::vector<sim::PlayerId>{1, 2}));
    CHECK(m.every_city.resident_class == 4);
    CHECK(warnings.size() == 3);
    // And back: the record the original would write.
    const assets::MissionRecord back = sim::mission_to_record(m, 2);
    CHECK(back.slot == 2);
    CHECK(back.goals == m.goals);
    CHECK(back.cities[0].size == 2500);
    CHECK(back.target == 7);
    CHECK(back.defeat[1] == 1);
    CHECK(back.defeat[0] == 0);
    const auto bytes = assets::encode_mission(back);
    CHECK(bytes.size() == assets::MissionRecord::kSize);
}

TEST_CASE("missions: city goals are matched so that every goal finds a city if it can (S-108)") {
    Game g;
    sim::Mission m;
    m.goals = sim::goal::kCity1 | sim::goal::kCity2;
    m.cities[0] = goal(200, 0, 0);    // a city of 200
    m.cities[1] = goal(100, 1, 100);  // a city with 100 settlers
    g.mission(0, m);
    // The first city meets both goals, the second only the first. The original gives the first
    // city goal 1 and then finds no city for goal 2.
    g.city(0, 0, 0, {150, 100});
    g.city(1, 0, 0, {210});
    const sim::MissionCheck c = g.check(0);
    CHECK(c.achieved == (sim::goal::kCity1 | sim::goal::kCity2));
    CHECK(c.won);
    // One city cannot fill two goals.
    g.world.remove_city(1, 0);
    CHECK(!(g.check(0).won));
}

TEST_CASE("missions: every city, the target's city, city count, inhabitants, goods") {
    Game g;
    sim::Mission m;
    m.goals = sim::goal::kEveryCity;
    m.every_city = goal(50, 0, 0);
    g.mission(0, m);
    // Without a city the original counts "every city" as met (S-109).
    CHECK(!(g.check(0).won));
    g.city(0, 0, 0, {60});
    CHECK(g.check(0).won);
    g.city(1, 0, 0, {40});
    CHECK(!(g.check(0).won));

    Game h;
    sim::Mission t;
    t.goals = sim::goal::kTargetCity | sim::goal::kCityCount | sim::goal::kInhabitants | sim::goal::kGoods1;
    t.target = 2;
    t.target_city = goal(100, 0, 0);
    t.min_cities = 2;
    t.min_inhabitants = 150;
    t.goods[0] = {kTools, 20 * 32};
    h.mission(0, t);
    h.city(2, 0, 2, {120});  // the computer's city
    h.city(0, 0, 0, {80});
    h.city(1, 0, 0, {80});
    sim::City& a = *h.world.city(0, 0);
    sim::City& b = *h.world.city(1, 0);
    a.ensure_ware(kTools);
    a.goods[kTools] = 10 * 32;
    CHECK(h.check(0).achieved == (sim::goal::kTargetCity | sim::goal::kCityCount | sim::goal::kInhabitants));
    b.ensure_ware(kTools);
    b.goods[kTools] = 10 * 32;  // the goods of all cities count together
    CHECK(h.check(0).won);
}

TEST_CASE("missions: settle every gold island alone; the islands are fixed when the mission starts (S-110)") {
    Game g;
    sim::Mission m;
    m.goals = sim::goal::kIslands1;
    m.islands[0].resource = kGold;
    g.mission(0, m);
    REQUIRE((g.world.missions()[0].islands[0].islands == std::vector<std::uint16_t>{0, 1}));
    g.city(0, 0, 0, {10});
    g.city(0, 1, 0, {10});  // two cities on one island are still one island
    CHECK(!(g.check(0).won));
    g.city(1, 0, 0, {10});
    CHECK(g.check(0).won);
    // A computer's city on a gold island spoils it; natives do not.
    g.city(1, 1, 3, {10});
    CHECK(g.check(0).won);
    g.city(1, 2, 2, {10});
    CHECK(!(g.check(0).won));
    // The gold running out does not take the island from the list.
    g.world.remove_city(1, 2);
    g.world.island(1).header.deposits[0].amount = 0;
    CHECK(g.check(0).won);
}

TEST_CASE("missions: defeat players; natives by destroying their villages") {
    Game g;
    sim::Mission m;
    m.goals = sim::goal::kDefeat;
    m.defeat = {0, 2, 3};  // the owner himself is ignored
    g.mission(0, m);
    g.city(0, 0, 0, {10});
    g.city(2, 0, 3, {10});  // the natives' village
    CHECK(!(g.check(0).won));
    g.world.player(2)->status = sim::PlayerStatus::Defeated;
    CHECK(!(g.check(0).won));
    g.world.remove_city(2, 0);
    CHECK(!g.check(0).won);  // someone else destroyed it
    g.world.diplomacy().set_blamed(3, 0);
    CHECK(g.check(0).won);
    // A slot nobody plays counts as defeated.
    g.world.player(2)->status = sim::PlayerStatus::Absent;
    CHECK(g.check(0).won);
}

TEST_CASE("missions: computers only with goal 0x800; a mission with only a text does nothing") {
    Game g;
    sim::Mission m;
    m.goals = sim::goal::kCityCount;
    m.min_cities = 1;
    g.mission(2, m);
    g.city(0, 0, 2, {10});
    CHECK(!(g.check(2).won));
    auto missions = g.world.missions();
    missions[0].goals |= sim::goal::kComputers;
    g.world.set_missions(missions);
    CHECK(g.check(2).won);

    Game h;
    sim::Mission text;
    text.text = "Besiedelt eine Insel";
    text.losses = sim::loss::kProtectedCount;
    text.protected_min_cities = 5;
    h.mission(0, text);
    const sim::MissionCheck c = h.check(0);
    CHECK(!(c.won));
    CHECK(!(c.lost));
}

TEST_CASE("missions: the protected city must stay large enough") {
    Game g;
    sim::Mission m;
    m.goals = sim::goal::kInhabitants;
    m.min_inhabitants = 1000;
    m.losses = sim::loss::kProtectedCity | sim::loss::kProtectedCount;
    m.target = 1;
    m.protected_city = goal(100, 0, 0);
    m.protected_min_cities = 1;
    g.mission(0, m);
    sim::City& ally = g.city(1, 0, 1, {150});
    CHECK(!(g.check(0).lost));
    ally.inhabitants[0] = 90;
    CHECK(g.check(0).lost);
}

TEST_CASE("missions: trade statistics are a moving average per minute") {
    sim::Player p;
    sim::earn_from_trade(p, 600);
    sim::pay_for_trade(p, 120);
    CHECK(p.money == 480);
    CHECK(sim::trade_balance(p) == 600 * 32 / 480 - 120 * 32 / 480);
}

TEST_CASE("missions: alone, a human with no city and no ship is out at once; others wait 200 s") {
    Game g;
    g.world.player(1)->status = sim::PlayerStatus::Absent;  // single player
    g.ship(2);
    sim::Simulation s(g.world, g.buildings, g.figures);
    auto events = run(s, 5000);
    REQUIRE(has(events, GameEvent::Kind::PlayerDefeated, 0));
    CHECK(g.world.players()[0].status == sim::PlayerStatus::Defeated);
    // A defeated player gives no orders.
    sim::DiplomacyOrder order;
    order.player = 0;
    CHECK(!(s.execute(order).accepted));

    Game h;
    h.ship(1);
    h.ship(2);
    sim::Simulation t(h.world, h.buildings, h.figures);
    events = run(t, 195'000);
    CHECK(!(has(events, GameEvent::Kind::PlayerDefeated, 0)));
    events = run(t, 5000);
    CHECK(has(events, GameEvent::Kind::PlayerDefeated, 0));
}

TEST_CASE("missions: bankrupt after 40 checks in a row; the count starts over when the money is back (S-107)") {
    Game g;
    g.ship(0);
    g.ship(1);
    g.ship(2);
    g.world.player(0)->money = -2000;
    sim::Simulation s(g.world, g.buildings, g.figures);
    auto events = run(s, 150'000);  // 30 checks
    CHECK(g.world.players()[0].bankrupt_checks == 30);
    g.world.player(0)->money = 100;
    run(s, 5000);
    CHECK(g.world.players()[0].bankrupt_checks == 0);
    g.world.player(0)->money = -2000;
    events = run(s, 195'000);
    CHECK(!(has(events, GameEvent::Kind::PlayerDefeated, 0)));
    events = run(s, 5000);
    REQUIRE(has(events, GameEvent::Kind::PlayerDefeated, 0));
    const auto e = std::find_if(events.begin(), events.end(),
                                [](const GameEvent& x) { return x.kind == GameEvent::Kind::PlayerDefeated; });
    CHECK(e->reason == GameEvent::Reason::Bankrupt);
    CHECK(e->by == sim::kNoPlayer);
}

TEST_CASE("missions: a computer with nothing left is retired; the blamed player gets the credit") {
    Game g;
    g.ship(0);
    g.ship(1);
    g.city(0, 0, 0, {0, 0, 250});  // 250 inhabitants of the third class: a triumphal arch
    g.world.diplomacy().set_blamed(2, 0);
    sim::Simulation s(g.world, g.buildings, g.figures);
    const auto events = run(s, 5000);
    CHECK(has(events, GameEvent::Kind::PlayerDefeated, 2));
    CHECK(has(events, GameEvent::Kind::ArchAllowed, 0));
    CHECK(g.world.players()[2].status == sim::PlayerStatus::Defeated);
    CHECK(g.world.players()[0].opponents_defeated == 1);
    CHECK(g.world.players()[0].arches_allowed == 1);
    CHECK(g.world.players()[0].arches_built == 0);
    // The allowance unlocks the arch's level, which no city reaches (25 000 aristocrats).
    CHECK(g.world.players()[0].unlocked(13));
    const auto e = std::find_if(events.begin(), events.end(),
                                [](const GameEvent& x) { return x.kind == GameEvent::Kind::PlayerDefeated; });
    CHECK(e->by == 0);
}

TEST_CASE("missions: without 200 citizens in the largest city the arch counts as used up (op 3 sub 9)") {
    Game g;
    g.ship(0);
    g.ship(1);
    // 250 of the third class and up, but only 150 in the largest city.
    g.city(0, 0, 0, {300, 0, 150});
    g.city(1, 0, 0, {0, 0, 100});
    g.world.diplomacy().set_blamed(2, 0);
    sim::Simulation s(g.world, g.buildings, g.figures);
    const auto events = run(s, 5000);
    CHECK(has(events, GameEvent::Kind::PlayerDefeated, 2));
    CHECK(!has(events, GameEvent::Kind::ArchAllowed, 0));
    const sim::Player& p = g.world.players()[0];
    CHECK(p.opponents_defeated == 1);
    CHECK(p.arches_allowed == 1);  // counted for the score and missions all the same
    CHECK(p.arches_built == 1);
    CHECK(!p.unlocked(13));
}

TEST_CASE("missions: a defeat nobody caused credits nobody") {
    Game g;
    g.ship(0);
    g.ship(1);
    g.city(0, 0, 0, {0, 0, 250});
    sim::Simulation s(g.world, g.buildings, g.figures);
    const auto events = run(s, 5000);
    const auto e = std::find_if(events.begin(), events.end(),
                                [](const GameEvent& x) { return x.kind == GameEvent::Kind::PlayerDefeated; });
    REQUIRE(e != events.end());
    CHECK(e->player == 2);
    CHECK(e->by == sim::kNoPlayer);
    CHECK(g.world.players()[0].arches_allowed == 0);
}

TEST_CASE("missions: the winner ends the game; others win only by their own mission (S-106)") {
    Game g;
    g.ship(0);
    g.ship(1);
    g.ship(2);
    sim::Mission m;
    m.goals = sim::goal::kInhabitants;
    m.min_inhabitants = 100;
    m.success_message = 0x63;
    g.mission(0, m);
    g.city(0, 0, 0, {120});
    g.city(1, 0, 1, {500});  // player 1 has no mission: no win for him
    sim::Simulation s(g.world, g.buildings, g.figures);
    const auto events = run(s, 10'000);
    REQUIRE(has(events, GameEvent::Kind::MissionAccomplished, 0));
    CHECK(has(events, GameEvent::Kind::GameOver, 0));
    CHECK(!(has(events, GameEvent::Kind::MissionAccomplished, 1)));
    CHECK(std::count_if(events.begin(), events.end(),
                        [](const GameEvent& e) { return e.kind == GameEvent::Kind::GameOver; }) == 1);
    const auto e = std::find_if(events.begin(), events.end(),
                                [](const GameEvent& x) { return x.kind == GameEvent::Kind::MissionAccomplished; });
    CHECK(e->message == 0x63);
    REQUIRE(g.world.game_end().has_value());
    CHECK(g.world.game_end()->winner == 0);
    CHECK(g.world.players()[0].won);
    CHECK(!(g.world.players()[1].won));
}

TEST_CASE("missions: missions and the players' standing survive the own savegame") {
    Game g;
    g.ship(0);
    sim::Mission m;
    m.goals = sim::goal::kIslands1 | sim::goal::kCity1 | sim::goal::kDefeat;
    m.islands[0].resource = kGold;
    m.cities[0] = goal(500, 4, 500);
    m.defeat = {2};
    m.text = "Kolonialbuch";
    g.mission(0, m);
    g.world.player(2)->status = sim::PlayerStatus::Defeated;
    g.world.player(0)->opponents_defeated = 3;
    g.world.player(0)->sales = 12345;
    g.world.player(0)->bankrupt_checks = 7;
    g.world.player(0)->soldiers_killed = 11;
    g.world.player(0)->soldiers_lost = 12;
    g.world.player(1)->ships_sunk = 13;
    g.world.player(1)->ships_lost = 14;
    g.world.set_game_end(sim::World::GameEnd{60'000, 0});
    auto bytes = sim::write_game(g.world, g.buildings);
    REQUIRE(bytes.ok());
    auto back = sim::read_game(*bytes, g.buildings);
    REQUIRE(back.ok());
    CHECK(back->missions() == g.world.missions());
    CHECK(back->players()[0].mission == 0);
    CHECK(back->players()[2].status == sim::PlayerStatus::Defeated);
    CHECK(back->players()[0].opponents_defeated == 3);
    CHECK(back->players()[0].sales == 12345);
    CHECK(back->players()[0].bankrupt_checks == 7);
    CHECK(back->players()[0].soldiers_killed == 11);
    CHECK(back->players()[0].soldiers_lost == 12);
    CHECK(back->players()[1].ships_sunk == 13);
    CHECK(back->players()[1].ships_lost == 14);
    CHECK(back->game_end() == g.world.game_end());
}
