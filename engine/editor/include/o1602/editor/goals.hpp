// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's goals (ADR 0013, sim::Mission): the missions of a scenario with all the original's
// goals and loss conditions, which player holds which, and what makes a goal impossible or
// meaningless before the game starts. Each change is made on a copy of the world and given back as
// a WorldEdit for undo, like the players'.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "o1602/core/result.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// The most missions a scenario of the editor has: one for each player at most.
inline constexpr std::size_t kMaxMissions = 16;
/// The longest text of a mission, Windows-1252.
inline constexpr std::size_t kMaxMissionText = 1000;
/// The classes of inhabitants a city goal can ask for (pioneers to aristocrats).
inline constexpr std::uint8_t kGoalClasses = 5;

/// What can be set of a mission, in the order the editor lists it. Each has the value "off" (the
/// goal is not asked for) and a list of values to go through; the goal's bit follows the value.
enum class GoalField : std::uint8_t {
    Cities,           ///< kCityCount: so many cities
    Inhabitants,      ///< kInhabitants: inhabitants in all cities together
    Balance,          ///< kBalance: balance per minute
    TradeBalance,     ///< kTradeBalance: trade balance per minute
    Defeated,         ///< kDefeatedCount: so many opponents defeated
    DefeatPlayer,     ///< kDefeat: this player defeated
    Castle,           ///< kCastle
    Cathedral,        ///< kCathedral
    Goods1,           ///< kGoods1: the good
    Goods1Amount,     ///< its tons
    Goods2,           ///< kGoods2
    Goods2Amount,     ///<
    Islands1,         ///< kIslands1: every island with this crop or deposit, alone
    Islands2,         ///< kIslands2
    City1,            ///< kCity1: a city of so many inhabitants
    City1Class,       ///< of whom so many of this class or higher
    City1Count,       ///<
    City2,            ///< kCity2: another city
    City2Class,       ///<
    City2Count,       ///<
    City3,            ///< kCity3: a third city
    City3Class,       ///<
    City3Count,       ///<
    EveryCity,        ///< kEveryCity: every own city so large
    EveryCityClass,   ///<
    EveryCityCount,   ///<
    Target,           ///< the target player of TargetCity and the protected player of the losses
    TargetCity,       ///< kTargetCity: a city of the target player so large
    ProtectedCity,    ///< loss kProtectedCity: lost if no city of the protected player stays so large
    ProtectedCities,  ///< loss kProtectedCount: lost if the protected player keeps fewer cities
    Computers,        ///< kComputers: computers holding the mission can win by it too
    Message,          ///< the message (and its video) for the winner
    Count
};
inline constexpr std::size_t kGoalFields = static_cast<std::size_t>(GoalField::Count);

/// The value of `field` in `m` as a number: 0 for off (or the first choice of Target and Message);
/// a ware, a resource, a player slot + 1, a class, or the amount (tons for goods).
[[nodiscard]] std::int64_t goal_value(const sim::Mission& m, GoalField field);
/// Whether the goal or loss condition of `field` is asked for in `m` (for a detail: its goal's).
/// Tells "off" from a balance of 0.
[[nodiscard]] bool goal_asked(const sim::Mission& m, GoalField field);
/// Whether `field` matters in `m`: a goal's details only while the goal is asked for (a class
/// while its city's size is set, the tons while the good is), the target while a goal or loss
/// condition uses it.
[[nodiscard]] bool goal_used(const sim::Mission& m, GoalField field);

/// The goods a goal can ask for (the trade goods of `world`), the resources an island goal can
/// (the crops by their fertility, iron ore and gold) and the players a goal can name (humans,
/// computers, pirates and natives that are in the game).
[[nodiscard]] std::vector<std::uint8_t> goal_goods(const sim::World& world);
[[nodiscard]] std::vector<std::uint8_t> goal_resources();
[[nodiscard]] std::vector<sim::PlayerId> goal_players(const sim::World& world);

/// The messages for the winner a mission can choose: the original's "mission accomplished" and
/// the multiplayer win in the winner's colour.
inline constexpr std::uint16_t kMessageAccomplished = 0x54;
inline constexpr std::uint16_t kMessageColourWin = 0x6B;

/// `m` with `field` at its next value (`forward`) or the one before, around through "off" at the
/// ends; the goal's bit follows. An island goal's list is cleared (the game's start makes it).
void step_goal(const sim::World& world, sim::Mission& m, GoalField field, bool forward);

/// The mission `index` of `world` as `mission`; `index` one past the last adds it (up to
/// kMaxMissions). Fails (NotFound) for an index beyond, (InvalidFormat) for a text the game could
/// not show or that is too long, (LimitExceeded) for too many missions.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> set_mission(const sim::World& world, std::size_t index,
                                                                   sim::Mission mission);
/// Takes the mission `index` away: whoever held it holds none, those after it move down. Fails
/// (NotFound) for no such mission.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> remove_mission(const sim::World& world, std::size_t index);
/// `player` (a human or computer) holding the mission `mission`, or none (sim::kNoMission). Fails
/// (NotFound) for no such player or mission.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> set_mission_holder(const sim::World& world, sim::PlayerId player,
                                                                          sim::MissionId mission);

/// Something about the missions that a scenario's author should know before the game starts.
struct GoalProblem {
    enum class Kind : std::uint8_t {
        NobodyHolds,      ///< the mission `mission` is held by no human or computer
        NoGoals,          ///< the mission has no goal: it never ends the game (an endless game)
        HumanWithout,     ///< the human `player` holds no mission while others do
        NoIsland,         ///< no island has the resource of island goal `detail` (0, 1): met at once
        UnknownGood,      ///< goods goal `detail` names no trade good of the game
        NoPlayer,         ///< the goal names `player`, who is not in the game
        DefeatSelf,       ///< the player to defeat holds the mission himself
        TooFewOpponents,  ///< more opponents to defeat than there are (`detail`)
        TargetNoCity,     ///< the protected player starts without the cities the losses ask for: lost at once
    };
    Kind kind = Kind::NoGoals;
    std::size_t mission = 0;
    sim::PlayerId player = sim::kNoPlayer;
    std::size_t detail = 0;
    friend bool operator==(const GoalProblem&, const GoalProblem&) = default;
};
/// What is impossible, met at once or meaningless in the missions of `world`, by mission.
[[nodiscard]] std::vector<GoalProblem> goal_problems(const sim::World& world);

}  // namespace o1602::editor
