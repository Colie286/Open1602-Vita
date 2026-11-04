// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's statistics of the start areas (ADR 0013): what each player finds around the place
// it starts at (land, the crops its islands are fertile for, ore and gold) and how far its nearest
// rival starts, so that an author sees before the game whether the starts are fair.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// How far around its start a player's area reaches: islands whose nearest tile is at most so many
/// tiles away (the longer axis), about what the first ship explores in the first minutes.
inline constexpr std::uint32_t kStartReach = 60;
/// The crops of the statistics, in this order: tobacco, spices, sugar, cotton, wine, cocoa.
inline constexpr std::size_t kStartCrops = 6;

struct StartArea {
    sim::PlayerId player = sim::kNoPlayer;
    std::uint32_t x = 0;  ///< the start: the first warehouse, else the first ship (world tile)
    std::uint32_t y = 0;
    std::uint32_t islands = 0;                       ///< islands within kStartReach
    std::uint64_t land = 0;                          ///< their tiles that are no water
    std::array<std::uint32_t, kStartCrops> crops{};  ///< of them fertile for each crop
    std::uint32_t ore = 0;                           ///< iron ore deposits on them
    std::uint32_t gold = 0;                          ///< gold deposits on them
    /// The distance to the nearest other start (the longer axis plus a quarter of the shorter, as
    /// the ships count it); 0 without another start.
    std::uint32_t rival = 0;
};

/// The start areas of the humans and computers in the game with a warehouse or a ship, by slot.
[[nodiscard]] std::vector<StartArea> start_areas(const sim::World& world, const assets::BuildingTable& buildings);

/// What is uneven between the start areas: more than this share less land than the richest
/// (percent), a crop that some areas have and others not, ore or gold in some only.
inline constexpr std::uint32_t kFairLandPercent = 70;
struct Unfairness {
    enum class Kind : std::uint8_t { Land, Crop, Ore, Gold };
    Kind kind = Kind::Land;
    sim::PlayerId player = sim::kNoPlayer;  ///< the player short of it
    std::size_t crop = 0;                   ///< Crop: the index into the crops
    friend bool operator==(const Unfairness&, const Unfairness&) = default;
};
[[nodiscard]] std::vector<Unfairness> unfairness(const std::vector<StartArea>& areas);

}  // namespace o1602::editor
