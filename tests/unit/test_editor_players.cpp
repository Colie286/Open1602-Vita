// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>

#include "../test_framework.hpp"

#include "o1602/editor/original_check.hpp"
#include "o1602/editor/players.hpp"
#include "o1602/editor/scenario_rules.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

/// A world of the original's seven slots: four players (the second a computer with 5000 gold), the
/// trader, the pirates and the natives.
sim::World seven_slots() {
    sim::World world(40, 40);
    std::vector<sim::Player> players(7);
    for (std::size_t i = 0; i < 4; ++i) {
        players[i].role = i == 0 ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
        players[i].banner = static_cast<sim::BannerId>(i);
        players[i].money = i == 1 ? 5000 : 20000;
    }
    sim::make_neutral(players[4], sim::faction::kTrader);
    sim::make_neutral(players[5], sim::faction::kPirates);
    sim::make_neutral(players[6], sim::faction::kNatives);
    world.set_players(players);
    return world;
}

}  // namespace

TEST_CASE("editor players: the slots of the humans and computers, and their values changed") {
    sim::World world = seven_slots();
    CHECK((editor::player_slots(world) == std::vector<sim::PlayerId>{0, 1, 2, 3}));
    CHECK(editor::player_banners().size() == sim::kMaxPlayers);

    editor::PlayerValues v = editor::player_values(world.players()[2]);
    v.role = sim::PlayerRole::Human;
    v.name = "Merlin";
    v.money = 12000;
    v.monuments_allowed = 2;
    v.arches_allowed = 1;
    v.banner = 1;  // the second player's: they swap
    auto op = editor::set_player(world, 2, v);
    REQUIRE(op.ok());
    (*op)->apply(world);
    const sim::Player& p = world.players()[2];
    CHECK(p.role == sim::PlayerRole::Human);
    CHECK(p.name == "Merlin");
    CHECK(p.money == 12000);
    CHECK(p.monuments_allowed == 2);
    CHECK(p.arches_allowed == 1);
    CHECK(p.banner == 1);
    CHECK(world.players()[1].banner == 2);
    (*op)->revert(world);
    CHECK(world.players()[2].name.empty());
    CHECK(world.players()[1].banner == 1);

    // Refused: the neutral slots, a role that is none of the two, a foreign banner, too much money.
    CHECK(!editor::set_player(world, 5, v).ok());
    v.role = sim::PlayerRole::Neutral;
    CHECK(!editor::set_player(world, 2, v).ok());
    v.role = sim::PlayerRole::Computer;
    v.banner = 5;
    CHECK(!editor::set_player(world, 2, v).ok());
    v.banner = 2;
    v.money = editor::kMaxStartMoney + 1;
    CHECK(!editor::set_player(world, 2, v).ok());
}

TEST_CASE("editor players: slots added behind the neutral ones up to twelve, and the last taken away") {
    sim::World world = seven_slots();
    // The first four stay.
    CHECK(!editor::remove_last_player(world).ok());
    for (std::size_t n = 4; n < sim::kMaxPlayers; ++n) {
        auto op = editor::add_player(world);
        REQUIRE(op.ok());
        (*op)->apply(world);
    }
    CHECK(editor::player_slots(world).size() == sim::kMaxPlayers);
    CHECK(!editor::add_player(world).ok());
    const sim::Player& fifth = world.players()[7];
    CHECK(fifth.role == sim::PlayerRole::Computer);
    CHECK(fifth.money == 5000);  // as the first computer
    CHECK(fifth.banner == sim::kFirstExtraBanner);
    CHECK(world.players()[4].role == sim::PlayerRole::Neutral);  // the trader keeps his slot

    // The last goes while it owns nothing.
    world.add_ship(sim::make_ship("", 3, 3, 21, sim::player_slot(sim::kMaxPlayers - 1)));
    CHECK(!editor::remove_last_player(world).ok());
    world.ships().clear();
    auto gone = editor::remove_last_player(world);
    REQUIRE(gone.ok());
    (*gone)->apply(world);
    CHECK(editor::player_slots(world).size() == sim::kMaxPlayers - 1);
    CHECK(world.players().size() == std::size_t{sim::player_slot(sim::kMaxPlayers - 2)} + 1);
}

TEST_CASE("editor players: a map of four slots gets its fifth player behind three empty ones") {
    sim::World world(40, 40);
    std::vector<sim::Player> players(4);
    world.set_players(players);
    auto op = editor::add_player(world);
    REQUIRE(op.ok());
    (*op)->apply(world);
    REQUIRE(world.players().size() == 8);
    CHECK(world.players()[4].status == sim::PlayerStatus::Absent);
    CHECK(world.players()[7].status == sim::PlayerStatus::Playing);
    CHECK(editor::player_slots(world).size() == 5);
    // The empty slots are not players with a start.
    for (const editor::LimitProblem& p : editor::original_limit_problems(world)) {
        if (p.kind == editor::LimitProblem::Kind::Players) {
            CHECK(p.count == 5);
        }
    }
    auto gone = editor::remove_last_player(world);
    REQUIRE(gone.ok());
    (*gone)->apply(world);
    CHECK(world.players().size() == 4);  // the empty slots go with it
}

TEST_CASE("editor players: relations at the start are set on both sides and kept when a player comes") {
    sim::World world(40, 40);
    std::vector<sim::Player> players(4);
    for (auto& p : players) {
        p.role = sim::PlayerRole::Computer;
    }
    world.set_players(players);
    CHECK((editor::relations(world, 0, 1) == editor::Relations{true, false}));
    world.diplomacy().relation(0, 1).damage = 77;
    auto war = editor::set_relations(world, 0, 1, editor::Relations{false, false});
    REQUIRE(war.ok());
    (*war)->apply(world);
    CHECK((editor::relations(world, 0, 1) == editor::Relations{false, false}));
    CHECK((editor::relations(world, 1, 0) == editor::Relations{false, false}));
    CHECK(world.diplomacy().relation(0, 1).damage == 0);
    auto trade = editor::set_relations(world, 2, 3, editor::Relations{true, true});
    REQUIRE(trade.ok());
    (*trade)->apply(world);
    CHECK(!editor::set_relations(world, 2, 2, {}).ok());
    CHECK(!editor::set_relations(world, 2, 5, {}).ok());
    CHECK(!editor::set_relations(world, 0, 2, editor::Relations{false, true}).ok());

    auto added = editor::add_player(world);
    REQUIRE(added.ok());
    (*added)->apply(world);
    CHECK((editor::relations(world, 1, 0) == editor::Relations{false, false}));
    CHECK((editor::relations(world, 3, 2) == editor::Relations{true, true}));
    CHECK((editor::relations(world, 0, 7) == editor::Relations{true, false}));
    auto gone = editor::remove_last_player(world);
    REQUIRE(gone.ok());
    (*gone)->apply(world);
    CHECK((editor::relations(world, 0, 1) == editor::Relations{false, false}));
    CHECK((editor::relations(world, 2, 3) == editor::Relations{true, true}));
}

TEST_CASE("editor: a scenario's own settings are a step of undo") {
    sim::World world(40, 40);
    CHECK(!editor::scenario_rules(world).has_value());
    auto op = editor::set_scenario_rules(world, sim::Rules::original());
    REQUIRE(op != nullptr);
    op->apply(world);
    REQUIRE(editor::scenario_rules(world).has_value());
    CHECK(*editor::scenario_rules(world) == sim::Rules::original());
    CHECK(editor::set_scenario_rules(world, sim::Rules::original()) == nullptr);  // nothing to do
    op->revert(world);
    CHECK(!world.scenario_info().has_value());
}
