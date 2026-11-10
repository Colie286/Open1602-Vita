// SPDX-License-Identifier: GPL-3.0-or-later
//
// Victory conditions of an endless game (ADR 0008): Open1602's own, after the endless games of
// later Anno titles (1503, 1701, 1404), where the player sets what wins the game: a population, a
// fortune, a share of the islands, the defeat of all opponents, a score or a time limit. The
// original's endless game never ends by a win; with all conditions off (the default) neither
// does Open1602's.
//
// The conditions (VictoryConditions, rules.hpp) are part of the rules (Rules::victory), so they stay with the game's
// savegames and every computer of a multiplayer game checks them alike from the synchronised state, as the missions
// (mission.hpp). Every value is a plain number: the start dialog offers a few of them, settings.cfg takes any.
#pragma once

#include <cstdint>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/player.hpp"
#include "o1602/sim/rules.hpp"

namespace o1602::sim {

class World;

/// One bit per condition (VictoryCheck).
namespace victory {
inline constexpr std::uint32_t kInhabitants = 0x1;
inline constexpr std::uint32_t kClass = 0x2;
inline constexpr std::uint32_t kMoney = 0x4;
inline constexpr std::uint32_t kBalance = 0x8;
inline constexpr std::uint32_t kEliminate = 0x10;
inline constexpr std::uint32_t kIslands = 0x20;
inline constexpr std::uint32_t kScore = 0x40;
inline constexpr std::uint32_t kMonument = 0x80;
}  // namespace victory

/// The condition bits that are on.
[[nodiscard]] std::uint32_t victory_required(const VictoryConditions& v) noexcept;

/// What a check of a player found.
struct VictoryCheck {
    std::uint32_t required = 0;  ///< condition bits that are on
    std::uint32_t achieved = 0;  ///< of those, the bits that hold now
    bool won = false;            ///< all required hold (or one, if any one is enough)
};

/// Whether `player` can win by the conditions: a human or computer still playing (a computer only
/// if `computers`).
[[nodiscard]] bool can_win(const World& world, const VictoryConditions& v, PlayerId player);

/// Checks the conditions for `player` (every 5 s of game time, Simulation::players_tick). A player
/// that cannot win, or conditions without goals, give nothing.
[[nodiscard]] VictoryCheck check_victory(const World& world, const assets::BuildingTable& buildings,
                                         const assets::FigureTable& figures, const VictoryConditions& v,
                                         PlayerId player);

/// Whether the time limit is over at `time_ms` of game time.
[[nodiscard]] constexpr bool time_limit_over(const VictoryConditions& v, std::uint64_t time_ms) noexcept {
    return v.time_limit_minutes != 0 && time_ms >= std::uint64_t{v.time_limit_minutes} * 60'000U;
}

/// The winner when the time is up: of the players that can win, the one with the most points; on
/// equal points the first slot. kNoPlayer if nobody can win.
[[nodiscard]] PlayerId time_limit_winner(const World& world, const assets::BuildingTable& buildings,
                                         const VictoryConditions& v);

}  // namespace o1602::sim
