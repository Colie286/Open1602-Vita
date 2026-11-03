// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/game_data.hpp"
#include "o1602/editor/goals.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;
using editor::GoalField;
using editor::GoalProblem;

namespace {

/// Four players (the first a human), the trader, the pirates and the natives.
sim::World seven_slots() {
    sim::World world(40, 40);
    std::vector<sim::Player> players(7);
    for (std::size_t i = 0; i < 4; ++i) {
        players[i].role = i == 0 ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
        players[i].banner = static_cast<sim::BannerId>(i);
    }
    sim::make_neutral(players[4], sim::faction::kTrader);
    sim::make_neutral(players[5], sim::faction::kPirates);
    sim::make_neutral(players[6], sim::faction::kNatives);
    world.set_players(players);
    return world;
}

bool has(const std::vector<GoalProblem>& problems, GoalProblem::Kind kind) {
    return std::any_of(problems.begin(), problems.end(), [&](const GoalProblem& p) { return p.kind == kind; });
}

}  // namespace

TEST_CASE("editor goals: each goal goes through its values and off, its bit following") {
    const sim::World world = seven_slots();
    sim::Mission m;
    CHECK(!editor::goal_asked(m, GoalField::Cities));
    editor::step_goal(world, m, GoalField::Cities, true);
    CHECK((m.goals & sim::goal::kCityCount) != 0);
    CHECK(m.min_cities == 1);
    editor::step_goal(world, m, GoalField::Cities, false);  // back to off
    CHECK((m.goals & sim::goal::kCityCount) == 0);
    editor::step_goal(world, m, GoalField::Cities, false);  // around to the largest
    CHECK(m.min_cities == 30);

    // A balance of 0 is a goal of its own, not off.
    editor::step_goal(world, m, GoalField::Balance, true);
    CHECK(editor::goal_asked(m, GoalField::Balance));
    CHECK(m.min_balance == 0);
    editor::step_goal(world, m, GoalField::Balance, true);
    CHECK(m.min_balance == 10);
    editor::step_goal(world, m, GoalField::Balance, false);
    editor::step_goal(world, m, GoalField::Balance, false);
    CHECK(!editor::goal_asked(m, GoalField::Balance));

    // Castle and cathedral switch on and off.
    editor::step_goal(world, m, GoalField::Castle, true);
    CHECK((m.goals & sim::goal::kCastle) != 0);
    editor::step_goal(world, m, GoalField::Castle, true);
    CHECK((m.goals & sim::goal::kCastle) == 0);

    // A good: its tons start at the first value and matter only while the good is asked for.
    CHECK(!editor::goal_used(m, GoalField::Goods1Amount));
    editor::step_goal(world, m, GoalField::Goods1, true);
    CHECK((m.goals & sim::goal::kGoods1) != 0);
    CHECK(m.goods[0].ware == assets::ware::kOre);
    CHECK(editor::goal_value(m, GoalField::Goods1Amount) == 10);
    CHECK(editor::goal_used(m, GoalField::Goods1Amount));
    editor::step_goal(world, m, GoalField::Goods1Amount, true);
    CHECK(m.goods[0].amount == 20 * assets::kStockUnitsPerTon);

    // The player to defeat: the humans, computers, pirates and natives, not the trader.
    const auto players = editor::goal_players(world);
    CHECK((players == std::vector<sim::PlayerId>{0, 1, 2, 3, 5, 6}));
    editor::step_goal(world, m, GoalField::DefeatPlayer, false);
    CHECK((m.defeat == std::vector<sim::PlayerId>{6}));
    CHECK(editor::goal_value(m, GoalField::DefeatPlayer) == 7);

    // An island goal: the crops and the deposits; the list is made at the start.
    m.islands[0].islands = {3, 4};
    editor::step_goal(world, m, GoalField::Islands1, true);
    CHECK(m.islands[0].resource == assets::ware::kTobaccoField);
    CHECK(m.islands[0].islands.empty());
}

TEST_CASE("editor goals: the city goals keep their details, each in its row") {
    const sim::World world = seven_slots();
    sim::Mission m;
    CHECK(!editor::goal_used(m, GoalField::City1Class));
    editor::step_goal(world, m, GoalField::City1, true);  // 50
    CHECK((m.goals & sim::goal::kCity1) != 0);
    CHECK(editor::goal_used(m, GoalField::City1Count));
    CHECK(!editor::goal_used(m, GoalField::City1Class));  // no number of a class yet
    editor::step_goal(world, m, GoalField::City1Count, true);
    CHECK(m.cities[0].count == 10);
    editor::step_goal(world, m, GoalField::City1Class, true);
    CHECK(m.cities[0].resident_class == 1);
    CHECK(editor::goal_used(m, GoalField::City1Class));

    // A second city larger than the first stays the second (the check pairs the goals with the
    // cities whatever their order).
    for (int i = 0; i < 3; ++i) {
        editor::step_goal(world, m, GoalField::City2, true);  // 50, 100, 200
    }
    CHECK(m.cities[0].inhabitants == 50);
    CHECK(m.cities[0].count == 10);
    CHECK(m.cities[1].inhabitants == 200);
    CHECK((m.goals & sim::goal::kCities3) == (sim::goal::kCity1 | sim::goal::kCity2));

    // The protected city is a loss condition.
    editor::step_goal(world, m, GoalField::ProtectedCity, true);
    CHECK((m.losses & sim::loss::kProtectedCity) != 0);
    CHECK(editor::goal_used(m, GoalField::Target));
    editor::step_goal(world, m, GoalField::ProtectedCity, false);
    CHECK(m.losses == 0);
}

TEST_CASE("editor goals: missions added, held and taken away, each one step of undo") {
    sim::World world = seven_slots();
    sim::Mission m;
    m.goals = sim::goal::kCastle;
    m.text = "Build a castle.";
    auto op = editor::set_mission(world, 0, m);
    REQUIRE(op.ok());
    (*op)->apply(world);
    REQUIRE(world.missions().size() == 1);
    CHECK(!editor::set_mission(world, 2, m).ok());
    sim::Mission bad = m;
    bad.text = std::string("a\x01");
    CHECK(!editor::set_mission(world, 0, bad).ok());

    auto hold = editor::set_mission_holder(world, 0, 0);
    REQUIRE(hold.ok());
    (*hold)->apply(world);
    CHECK(world.players()[0].mission == 0);
    CHECK(!editor::set_mission_holder(world, 4, 0).ok());  // the trader holds none
    CHECK(!editor::set_mission_holder(world, 1, 3).ok());

    // A second mission for the computers; taking the first away moves it down.
    (*editor::set_mission(world, 1, m))->apply(world);
    (*editor::set_mission_holder(world, 1, 1))->apply(world);
    auto removed = editor::remove_mission(world, 0);
    REQUIRE(removed.ok());
    (*removed)->apply(world);
    CHECK(world.missions().size() == 1);
    CHECK(world.players()[0].mission == sim::kNoMission);
    CHECK(world.players()[1].mission == 0);
    (*removed)->revert(world);
    CHECK(world.players()[0].mission == 0);
    CHECK(world.players()[1].mission == 1);
}

TEST_CASE("editor goals: what no game can meet, or meets at once, is found before the start") {
    sim::World world = seven_slots();
    sim::Mission m;
    std::vector<sim::Mission> missions{m};
    world.set_missions(missions);
    auto problems = editor::goal_problems(world);
    CHECK(has(problems, GoalProblem::Kind::NobodyHolds));
    CHECK(has(problems, GoalProblem::Kind::NoGoals));

    // A monopoly on tobacco without an island for it is met at once; too many to defeat; the
    // player to defeat gone; the protected player without the cities the losses ask for.
    m.goals = sim::goal::kIslands1 | sim::goal::kDefeatedCount | sim::goal::kDefeat;
    m.islands[0].resource = assets::ware::kTobaccoField;
    m.min_defeated = 9;
    m.defeat = {11};
    m.losses = sim::loss::kProtectedCount;
    m.target = 2;
    m.protected_min_cities = 1;
    world.set_missions({m});
    std::vector<sim::Player> players = world.players();
    players[0].mission = 0;
    world.set_players(players);
    problems = editor::goal_problems(world);
    CHECK(!has(problems, GoalProblem::Kind::NobodyHolds));
    CHECK(has(problems, GoalProblem::Kind::NoIsland));
    CHECK(has(problems, GoalProblem::Kind::TooFewOpponents));
    CHECK(has(problems, GoalProblem::Kind::NoPlayer));
    CHECK(has(problems, GoalProblem::Kind::TargetNoCity));
    CHECK(!has(problems, GoalProblem::Kind::HumanWithout));
}
