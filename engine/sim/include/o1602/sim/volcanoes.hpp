// SPDX-License-Identifier: GPL-3.0-or-later
//
// Volcanoes (docs/re/volcanoes.md).
//
// A volcano is the big ore mountain with the record 21108; its origin tile is the crater. Once
// the game time has passed the next eruption time, the island epoch of an island with craters
// picks one of them and it erupts; the next eruption time moves on by an interval that shrinks
// with the number of craters in the world (35 minutes for one, 6 for twenty) plus up to 15.5
// minutes. The crater that erupted last may not be picked at the first try.
//
// An eruption throws 141 lumps of lava over 28.4 s of game time, slowly, then fast, then slowly
// again. Each lump flies in a steep arc 1 to 13.5 tiles away from the mountain. Where it lands, a
// house, workshop, farm, mine, market or shipyard catches fire; a Kontor, public and military
// buildings, walls, roads, bridges, fields and trees are destroyed to ruins. Figures are never
// hit, the ground stays as it was. An eruption also removes an ore deposit registered for the
// volcano mountain itself.
//
// Unlike the original, every computer computes the same lava at any frame rate (M-013, S-060), the
// eruption is checked independently of the drought (S-042), each eruption keeps its own crater
// (S-061), and the crater count follows the islands (S-063).
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/disasters.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

inline constexpr std::uint32_t kVolcanoRecordId = id::kBigOreMountain;
inline constexpr std::size_t kMaxCraters = 2;         ///< per island
inline constexpr std::uint32_t kMaxCraterCount = 20;  ///< the interval table ends there
inline constexpr std::uint8_t kLavaThrows = 141;
inline constexpr std::uint16_t kFigureVolcano = figure::kVolcano;
inline constexpr std::uint16_t kFigureLava = figure::kLava;
inline constexpr std::uint32_t kLavaSpeed = 600;  ///< Speed of VULKANFIRE1 in figuren.cod
inline constexpr double kVolcanoHeight = 3.2;     ///< height of the VULKAN figure, half tile widths
inline constexpr std::uint8_t kLavaFireDuration = 20;

struct VolcanoEvent {
    enum class Kind : std::uint8_t {
        Erupted,    ///< "Ein Vulkan ist ausgebrochen!" if `message`
        Ended,      ///< the last lump is thrown
        Fire,       ///< lava set the building with origin (x, y) on fire
        Destroyed,  ///< lava destroyed the building with origin (x, y)
    };
    Kind kind = Kind::Erupted;
    std::uint16_t island = 0;
    std::uint16_t x = 0;  ///< crater, or origin of the building hit
    std::uint16_t y = 0;
    /// Erupted: the original shows its message (not when a game is loaded during an eruption).
    bool message = false;
};

/// Craters of an island (0x0046C1C0): origins of record 21108, row by row from the top and from
/// right to left within a row; at most two.
[[nodiscard]] std::vector<Crater> find_craters(const IslandMap& map, const assets::BuildingTable& buildings);

/// Craters in the world, at most 20 (TIMERS +0xD0).
[[nodiscard]] std::uint32_t crater_count(const World& world) noexcept;

/// Time from one eruption to the earliest next one before the random part (0x004692F0):
/// 35 minutes for up to one crater in the world, 6 minutes for twenty.
[[nodiscard]] std::uint64_t eruption_interval_ms(std::uint32_t craters) noexcept;

/// Game time of the first eruption of a new game (0x00469270): 70 minutes and up to 15.5 more.
[[nodiscard]] std::uint64_t first_eruption_ms(Random& random);

/// Game time of step `step` of an eruption relative to its start, in microseconds (step
/// table 0x0062BD40): throw `step` for steps 0..140, the end for step 141.
[[nodiscard]] std::uint64_t eruption_step_us(std::uint32_t step) noexcept;

/// Whether volcanoes erupt in this game (settings bit 8: no volcanoes).
[[nodiscard]] bool volcanoes_enabled(const World& world, const Rules& rules) noexcept;

/// The eruption roll of the island epoch (0x0046B3E0): once the next eruption time has come, the
/// first island with craters (in island order) picks one and it erupts.
void roll_eruptions(World& world, const Rules& rules, std::vector<VolcanoEvent>* events);

/// Lets the crater erupt now (0x0045D380, kind 0x20). False if there is no such crater.
bool erupt(World& world, CraterRef crater, std::uint16_t seed, bool message, std::vector<VolcanoEvent>* events);

/// Runs the eruptions and the lava up to the current game time: throws, flights and what the lava
/// hits (fires go to `disasters`, destroyed buildings to `construction`).
void advance_volcanoes(World& world, const assets::BuildingTable& buildings, std::uint32_t lava_speed,
                       std::vector<VolcanoEvent>* events, std::vector<DisasterEvent>* disasters,
                       std::vector<ConstructionEvent>* construction);

/// Where a lump is at game time `now_us` (between the start and the end of its leg): world tiles
/// and height in half tile widths.
struct LavaPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};
[[nodiscard]] LavaPoint lava_position(const LavaLump& lump, std::uint64_t now_us) noexcept;

/// Height of the ground under a world point in half tile widths (Posoffs * 0.028; land 0.56,
/// sea 0): where lava lands.
[[nodiscard]] double lava_ground(const World& world, const assets::BuildingTable& buildings, double x,
                                 double y) noexcept;

}  // namespace o1602::sim
