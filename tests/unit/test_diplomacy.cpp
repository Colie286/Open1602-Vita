// SPDX-License-Identifier: GPL-3.0-or-later
// Diplomacy: treaties, events and the mood that follows from them.

#include "../test_framework.hpp"

#include "o1602/sim/diplomacy.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;
using sim::DiplomacyEventType;
using sim::Treaty;

namespace {

/// The seven slots of an original game: four players (one human), trader, pirates, natives.
std::vector<sim::Player> original_players() {
    std::vector<sim::Player> p(7);
    p[0].role = sim::PlayerRole::Human;
    for (std::size_t i = 1; i < 4; ++i) {
        p[i].role = sim::PlayerRole::Computer;
    }
    sim::make_neutral(p[4], sim::faction::kTrader);
    sim::make_neutral(p[5], sim::faction::kPirates);
    sim::make_neutral(p[6], sim::faction::kNatives);
    for (auto& player : p) {
        player.money = 20'000;
    }
    return p;
}

}  // namespace

TEST_CASE("diplomacy: a new game starts with the original's table") {
    sim::Diplomacy d;
    d.reset(original_players(), sim::base_factions());
    // Rows as the original's reset and the Endlosspiel files have them.
    const int peace[7][7] = {{3, 3, 3, 3, 3, 0, 3}, {3, 3, 3, 3, 3, 0, 3}, {3, 3, 3, 3, 3, 0, 3}, {3, 3, 3, 3, 3, 0, 3},
                             {3, 3, 3, 3, 3, 3, 3}, {0, 0, 0, 0, 3, 3, 0}, {3, 3, 3, 3, 3, 0, 3}};
    const int trade[7][7] = {{0, 0, 0, 0, 3, 3, 3}, {0, 0, 0, 0, 3, 3, 3}, {0, 0, 0, 0, 3, 3, 3}, {0, 0, 0, 0, 3, 3, 3},
                             {3, 3, 3, 3, 0, 0, 0}, {0, 0, 0, 0, 0, 3, 0}, {3, 3, 3, 3, 0, 0, 0}};
    for (sim::PlayerId a = 0; a < 7; ++a) {
        for (sim::PlayerId b = 0; b < 7; ++b) {
            CHECK(static_cast<int>(d.relation(a, b).peace) == peace[a][b]);
            CHECK(static_cast<int>(d.relation(a, b).trade) == trade[a][b]);
        }
    }
    CHECK(d.hostile(0, 5));
    CHECK(d.hostile(5, 6));
    CHECK(!d.hostile(0, 1));
    CHECK(!d.hostile(0, 0));
    CHECK(d.may_trade(0, 5));   // players buy at pirate Kontors from the start
    CHECK(!d.may_trade(5, 0));  // but not the other way round
    CHECK(!d.may_trade(0, 1));  // players need a treaty among themselves
}

TEST_CASE("diplomacy: war, offers and treaties as the original's commands") {
    sim::World world(64, 64);
    world.set_players(original_players());
    sim::Diplomacy& d = world.diplomacy();
    // A trade treaty between the human and computer 1.
    CHECK(d.set_trade(world, 0, 1, Treaty::Offered, 1000));
    CHECK(d.relation(0, 1).trade == Treaty::Offered);
    CHECK(d.relation(1, 0).trade == Treaty::Received);
    CHECK(d.set_trade(world, 1, 0, Treaty::Signed, 2000));
    CHECK(d.may_trade(0, 1));
    CHECK(d.may_trade(1, 0));
    CHECK(!d.set_trade(world, 1, 0, Treaty::Signed, 2000));  // nothing changes, nothing sent
    (void)d.take_notices();

    // War ends the trade treaty and is remembered by the victim.
    CHECK(d.set_peace(world, 0, 1, Treaty::None, 5000));
    CHECK(d.hostile(0, 1));
    CHECK(d.hostile(1, 0));
    CHECK(d.relation(0, 1).trade == Treaty::None);
    CHECK(d.relation(1, 1).war_time_ms == 5000);
    const auto notices = d.take_notices();
    REQUIRE(notices.size() == 2);
    CHECK(notices[0].kind == sim::DiplomacyNotice::Kind::TradeEnded);
    CHECK(notices[1].kind == sim::DiplomacyNotice::Kind::PeaceBroken);
    CHECK(notices[1].to == 1);
    int wars = 0;
    for (const auto& e : d.events(1)) {
        wars += e.type == DiplomacyEventType::WarDeclared && e.partner == 0 ? 1 : 0;
    }
    CHECK(wars == 1);
    // A pending offer still counts as hostile.
    CHECK(d.set_peace(world, 1, 0, Treaty::Offered, 6000));
    CHECK(d.hostile(0, 1));
    CHECK(d.set_peace(world, 0, 1, Treaty::Signed, 7000));
    CHECK(!d.hostile(0, 1));

    // War with the pirates keeps the trade with them.
    world.diplomacy().relation(0, 5).peace = Treaty::Signed;
    world.diplomacy().relation(5, 0).peace = Treaty::Signed;
    CHECK(d.set_peace(world, 5, 0, Treaty::None, 8000));
    CHECK(d.may_trade(0, 5));
}

TEST_CASE("diplomacy: events fade and weigh in the mood") {
    sim::World world(64, 64);
    world.set_players(original_players());
    sim::Diplomacy& d = world.diplomacy();
    CHECK(sim::diplomacy_event_value(DiplomacyEventType::WarDeclared, 0) == -30);
    // Age in 12.8 s steps: 75 steps -> -18, 200 steps -> 0.
    CHECK(sim::diplomacy_event_value(DiplomacyEventType::WarDeclared, 75 * 12'800) == -18);
    CHECK(sim::diplomacy_event_value(DiplomacyEventType::WarDeclared, 200 * 12'800) == 0);
    CHECK(sim::diplomacy_event_value(DiplomacyEventType::PeaceBought, 0) == 70);
    CHECK(sim::diplomacy_event_value(DiplomacyEventType::TradeOffered, 0) == 0);

    d.add_event(1, 0, DiplomacyEventType::HostileGesture, 0);
    CHECK(d.mood(world, 1, 0, 0, 0) == -30);
    CHECK(d.mood(world, 1, 0, 0, 80 * 12'800) == -15);
    // Damage counts against, a quarter, at most 100.
    d.add_damage(1, 0, 200);
    CHECK(d.mood(world, 1, 0, 0, 200 * 12'800) == -50);
    d.add_damage(1, 0, 10'000);
    CHECK(d.mood(world, 1, 0, 0, 200 * 12'800) == -100);
    // The counters fade by 1/256 every 5 s.
    const std::uint32_t before = d.relation(1, 0).damage;
    d.decay(1);
    CHECK(d.relation(1, 0).damage == before * 255 / 256);

    // Pirates: a bought peace replaces bribes against the payer and forgets the damage.
    d.add_damage(5, 0, 400);
    d.add_event(5, 0, DiplomacyEventType::BribedAgainst, 0);
    d.add_event(5, 0, DiplomacyEventType::PeaceBought, 0);
    CHECK(d.relation(5, 0).damage == 0);
    int bribes = 0;
    for (const auto& e : d.events(5)) {
        bribes += e.type == DiplomacyEventType::BribedAgainst ? 1 : 0;
    }
    CHECK(bribes == 0);
    // Types 6-11 are refreshed in place, not repeated.
    d.add_event(5, 0, DiplomacyEventType::PeaceBought, 1000);
    int bought = 0;
    for (const auto& e : d.events(5)) {
        bought += e.type == DiplomacyEventType::PeaceBought ? 1 : 0;
    }
    CHECK(bought == 1);
}

TEST_CASE("diplomacy: the panel's orders") {
    sim::World world(64, 64);
    world.set_players(original_players());
    using Order = sim::DiplomacyOrder;
    // Accepting needs an offer; natives and pirates cannot be addressed.
    CHECK(!sim::apply_diplomacy(world, Order{0, 1, Order::Kind::Trade, Treaty::Signed}));
    CHECK(!sim::apply_diplomacy(world, Order{0, 6, Order::Kind::Peace, Treaty::None}));
    CHECK(!sim::apply_diplomacy(world, Order{0, 5, Order::Kind::Peace, Treaty::Offered}));
    CHECK(sim::apply_diplomacy(world, Order{0, 1, Order::Kind::Trade, Treaty::Offered}));
    CHECK(sim::apply_diplomacy(world, Order{1, 0, Order::Kind::Trade, Treaty::Signed}));
    CHECK(world.diplomacy().may_trade(0, 1));
    // Tribute: the money moves, the partner likes it.
    Order tribute{0, 1, Order::Kind::Tribute};
    tribute.gold = 1700;
    CHECK(sim::apply_diplomacy(world, tribute));
    CHECK(world.players()[0].money == 18'300);
    CHECK(world.players()[1].money == 21'700);
    CHECK(world.diplomacy().relation(1, 0).tribute == 3200);
    CHECK(world.diplomacy().mood(world, 1, 0, 0, 0) > 0);
    tribute.gold = 30'000;
    CHECK(!sim::apply_diplomacy(world, tribute));
    // Breaking the peace is war.
    CHECK(sim::apply_diplomacy(world, Order{0, 2, Order::Kind::Peace, Treaty::None}));
    CHECK(world.diplomacy().hostile(2, 0));
}

TEST_CASE("diplomacy: a withdrawn offer is no declaration of war (S-113)") {
    sim::World world(64, 64);
    world.set_players(original_players());
    sim::Diplomacy& d = world.diplomacy();
    const auto count = [&](sim::PlayerId a, DiplomacyEventType type) {
        int n = 0;
        for (const auto& e : d.events(a)) {
            n += e.type == type && e.partner == 0 ? 1 : 0;
        }
        return n;
    };
    CHECK(d.set_peace(world, 0, 1, Treaty::None, 1000));
    CHECK(count(1, DiplomacyEventType::WarDeclared) == 1);
    // Offered and withdrawn: back at war, nothing new to remember.
    CHECK(d.set_peace(world, 0, 1, Treaty::Offered, 2000));
    CHECK(d.set_peace(world, 0, 1, Treaty::None, 3000));
    CHECK(d.relation(0, 1).peace == Treaty::None);
    CHECK(d.relation(1, 0).peace == Treaty::None);
    CHECK(count(1, DiplomacyEventType::WarDeclared) == 1);
    CHECK(d.relation(1, 1).war_time_ms == 1000);
    // The same for a trade offer.
    CHECK(d.set_trade(world, 0, 2, Treaty::Offered, 4000));
    CHECK(d.set_trade(world, 0, 2, Treaty::None, 5000));
    CHECK(d.relation(2, 0).trade == Treaty::None);
    CHECK(count(2, DiplomacyEventType::TradeCancelled) == 0);
}

namespace {

void add_city(sim::World& world, std::uint8_t index, sim::PlayerId owner, std::vector<std::uint32_t> inhabitants) {
    sim::City c;
    c.index = index;
    c.owner = owner;
    c.inhabitants = std::move(inhabitants);
    world.add_city(c);
}

}  // namespace

TEST_CASE("diplomacy: how far a city has come") {
    const auto development = [](std::vector<std::uint32_t> inhabitants) {
        sim::City c;
        c.inhabitants = std::move(inhabitants);
        return sim::city_development(c);
    };
    CHECK(development({}) == 0);
    CHECK(development({0, 0}) == 0);
    CHECK(development({100}) == 31);         // a whole class counts 31, not 32
    CHECK(development({90, 10}) == 32 + 3);  // 10 % of the city in the second class
    CHECK(development({50, 50}) == 32 + 16);
    CHECK(development({30, 70}) == 32 + 31);                  // two thirds and more: full
    CHECK(development({0, 0, 0, 0, 10, 99}) == 4 * 32 + 31);  // five classes, as the original
}

TEST_CASE("diplomacy: the attitude a scenario gives a computer") {
    namespace flag = sim::scenario_flag;
    sim::World world(64, 64);
    auto players = original_players();
    world.set_players(players);
    add_city(world, 0, 1, {250});       // the computer: 250 inhabitants, development 31
    add_city(world, 1, 0, {300, 300});  // the human: 600, development 48
    const auto mood = [&](const sim::ScenarioSwitches& switches, sim::PlayerId b = 0) {
        world.player(1)->scenario = switches;
        return world.diplomacy().mood(world, 1, b, 0, 0);
    };
    CHECK(mood({}) == 0);

    // A human beyond 500 inhabitants: always the table's end.
    CHECK(mood({flag::kRivalSize, 500, 0, 0, 0}) == -170);
    CHECK(mood({flag::kRivalSize, 600, 0, 0, 0}) == 0);
    CHECK(mood({flag::kRivalSize, 0, 0, 0, 0}, 2) == 0);  // only towards humans

    // Below its own target of 1000: 250 is a quarter = 150.
    CHECK(mood({flag::kOwnSize, 0, 1000, 0, 0}) == -150);
    CHECK(mood({flag::kOwnSize, 0, 250, 0, 0}) == 0);  // reached:
    CHECK(mood({flag::kOwnSize, 0, 200, 0, 0}) == 0);  // beyond
    CHECK(mood({flag::kOwnSize, 0, 0, 0, 0}) == 0);    // no target (the original divides by 0, S-118)

    // A human's city beyond development 40 by 8: = 22.
    CHECK(mood({flag::kRivalDevelopment, 0, 0, 0, 40}) == -22);
    CHECK(mood({flag::kRivalDevelopment, 0, 0, 0, 48}) == 0);
    // Its own best city 33 short of 64: = 98; towards anyone.
    CHECK(mood({flag::kOwnDevelopment, 0, 0, 64, 0}) == -98);
    CHECK(mood({flag::kOwnDevelopment, 0, 0, 64, 0}, 2) == -98);
    CHECK(mood({flag::kOwnDevelopment, 0, 0, 31, 0}) == 0);
    // Further than the table reaches: its last value (S-118).
    world.player(1)->scenario = {flag::kOwnDevelopment, 0, 0, 255, 0};
    world.set_cities({});
    CHECK(world.diplomacy().mood(world, 1, 0, 0, 0) == -170);
}

TEST_CASE("diplomacy: a computer minds others' cities on its small islands and troops on all of them (TB-8)") {
    sim::World world(200, 100);
    world.set_players(original_players());
    // Islands 0 and 2 are small (size class up to 3), island 1 is large.
    std::uint16_t x = 0;
    for (const std::uint8_t size : {std::uint8_t{2}, std::uint8_t{5}, std::uint8_t{1}}) {
        assets::IslandHeader header;
        header.template_size = size;
        REQUIRE(world.add_island(sim::WorldIsland{header, sim::IslandMap(10, 10), x, 10}).ok());
        x += 20;
    }
    const auto city = [&](std::uint16_t island, std::uint8_t index, sim::PlayerId owner, std::uint32_t people) {
        sim::City c;
        c.island = island;
        c.index = index;
        c.owner = owner;
        c.inhabitants = {people};
        world.add_city(c);
    };
    const auto unit = [&](std::uint16_t island, sim::PlayerId owner) {
        sim::Soldier s;
        s.owner = owner;
        s.island = island;
        world.add_soldier(s);
    };
    city(0, 0, 1, 100);
    city(2, 0, 1, 0);    // empty
    city(1, 0, 1, 100);  // large island
    const auto mood = [&] { return world.diplomacy().mood(world, 1, 0, 0, 0); };
    CHECK(mood() == 0);
    // A human city on the large island does not matter ...
    city(1, 1, 0, 50);
    CHECK(mood() == 0);
    // ... on a small one next to the empty city it does: 200.
    city(2, 1, 0, 50);
    CHECK(mood() == -200);
    // The human's troops there: 200 more; the computer's own troops with them lower the troop
    // penalty to 60.
    unit(2, 0);
    CHECK(mood() == -400);
    unit(2, 1);
    CHECK(mood() == -260);
    // Troops count on every island of the computer, the large one too (as the History
    // Edition): there the computer has none, so 200.
    unit(1, 0);
    CHECK(mood() == -400);
}
