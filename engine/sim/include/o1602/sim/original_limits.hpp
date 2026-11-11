// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the original's files and game can hold, in one place: the export to a .GAM checks a world
// against them (savegame_writer.hpp), and the editor keeps a scenario within them when it is to
// keep to the original's limits (ScenarioInfo::original_limits, ADR 0013). Open1602's own savegame
// knows none of them (sim/limits.hpp has its own).
#pragma once

#include <cstddef>
#include <cstdint>

namespace o1602::sim::original {

/// The world map of every file of the original.
inline constexpr std::uint32_t kWorldWidth = 500;
inline constexpr std::uint32_t kWorldHeight = 350;
/// Islands of a world of the original's size.
inline constexpr std::size_t kMaxIslands = 50;
/// Island coordinates are one byte in the original's records, so an island is at most this wide
/// and high.
inline constexpr std::uint32_t kMaxIslandSide = 255;
/// Deposits in an island's record.
inline constexpr std::size_t kMaxDeposits = 2;
inline constexpr std::size_t kMaxCities = 75;
inline constexpr std::size_t kMaxShips = 150;
inline constexpr std::size_t kMaxSoldiers = 400;
inline constexpr std::size_t kMaxTowers = 350;
/// Players with a start that a person or the computer plays (red, blue, yellow, white).
inline constexpr std::size_t kMaxStartPlayers = 4;

}  // namespace o1602::sim::original
