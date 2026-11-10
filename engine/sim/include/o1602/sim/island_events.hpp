// SPDX-License-Identifier: GPL-3.0-or-later
//
// Events of the islands, every 30 s per island (0x0046B3E0, docs/re/growth.md#dürre): droughts.
//
// A drought lowers the fertility of an island. While it lasts, ripe fields that are not fertile at
// its strength wither, also when they are harvested. When it ends, its strength halves every
// epoch, and each time the withered fields that are fertile again grow anew.
//
// Droughts come by chance once the game time has passed the next drought time: each island epoch
// of an island without a drought rolls on the drought table of the difficulty. A hit starts a
// drought if the island has a city of a human or computer player and pushes the next drought time
// 27 to 34 minutes on, whichever island it fell on. A curse of the natives starts a weaker one.
//
// Unlike the original, every island is handled at the same moment of its epoch and the events
// draw from their own random numbers (M-006). The volcanoes, which the original checks in the same
// place, have their own roll (volcanoes.hpp, S-042).
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

inline constexpr std::uint8_t kDroughtStrength = 0x40;  ///< a drought by chance
inline constexpr std::uint8_t kCurseStrength = 0x26;    ///< a drought by the natives' curse

struct IslandEvent {
    enum class Kind : std::uint8_t {
        DroughtStarted,  ///< "Die Trockenheit vernichtet Eure Ernte!" for the players with a city there
        DroughtOver,     ///< the strength reached 0 (the original says nothing)
        Withered,        ///< `count` fields withered in this epoch
        Recovered,       ///< `count` fields grow again
    };
    Kind kind = Kind::DroughtStarted;
    std::uint16_t island = 0;
    std::uint32_t count = 0;
};

/// Whether a figure is on its way to harvest the tile (such fields are left to the harvest).
using FieldReserved = std::function<bool(std::uint16_t island, TilePos tile)>;

/// One island epoch: curse, drought roll, withering or recovery; of the islands marked in `due`
/// (all without it).
void advance_island_events(World& world, const assets::BuildingTable& buildings, const Rules& rules,
                           const FieldReserved& reserved, std::vector<IslandEvent>* events,
                           const std::vector<bool>* due = nullptr);

/// Starts a drought on an island (0x0046B3A0): `duration_ms` from now, replacing one that runs.
void start_drought(World& world, std::uint16_t island, std::uint8_t strength, std::uint64_t duration_ms,
                   std::vector<IslandEvent>* events);

/// The drought table row the rules and the game ask for: 0 none .. 3 hard.
[[nodiscard]] std::uint8_t drought_level(const World& world, const Rules& rules) noexcept;

}  // namespace o1602::sim
