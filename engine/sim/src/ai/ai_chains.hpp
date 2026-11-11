// SPDX-License-Identifier: GPL-3.0-or-later
//
// The production chains of the packages' goods for the computer players (MD-4.md and
// docs/modding/computer-players.md): derived from the building table, not compiled in. A good of a
// package that a class of inhabitants needs gets its producer, the producer's raw material its own,
// and so on down to a farm or a raw material of the original. Each producer counts in a slot of its
// own after the original's (kSlots and up).
#pragma once

#include <cstdint>
#include <vector>

#include "ai_state.hpp"

#include "o1602/assets/building_table.hpp"

namespace o1602::sim::ai {

/// No slot: a raw material nobody builds for.
inline constexpr std::uint8_t kNoSlot = 0xFF;
/// At most so many producers of packages' goods: the slots from kSlots up to those of the public
/// buildings of a kind of their own below kNoSlot (computer_slots.hpp, kOwnPublicSlot).
inline constexpr std::size_t kMaxPackageNeeds = kNoSlot - kSlots - assets::kMaxOwnServices;
/// At most so many producers of one good in a city.
inline constexpr std::uint32_t kMaxPackageProducers = 8;

/// A producer the computer players build for a good of a package.
struct PackageNeed {
    std::uint8_t slot = kNoSlot;  ///< its slot in the city record, kSlots and up
    std::uint32_t building = 0;   ///< the record the computers build
    std::uint8_t ware = 0;        ///< the good it makes
    std::uint8_t task = 0;        ///< the task that searches its site: a farm's or a workshop's
    std::uint8_t field = 0;       ///< the crop of its fields (farms), 0 none
    std::uint8_t fertile = 0;     ///< the island must grow this crop, 0 no test
    /// The slot of the producer of its raw material, kNoSlot for none: one of a package (kSlots and
    /// up) or of the original's Needs (ai_tables.hpp).
    std::uint8_t raw_slot = kNoSlot;
    std::uint32_t output = 0;  ///< what it makes per minute at full rate, 1/32 t
    std::uint32_t input = 0;   ///< what it uses of its raw material per minute, 1/32 t
};

/// The producers of every good of a package that a class of `table` needs, and of their raw
/// materials, in the order they are found: a good first, then its raw material. A good the
/// original's Needs make and a producer no task can place end the chain; in a chain that comes
/// round to itself, the first Need waits for no raw material.
std::vector<PackageNeed> package_needs(const assets::BuildingTable& table);

/// The slot of the original's Need that makes ware `w` (ai_tables.hpp), kNoSlot for none.
std::uint8_t original_need_slot(std::uint8_t w);

/// How many producers of each Need a city wants when its inhabitants use `demand[i]` (1/32 t per
/// minute) of the good of `needs[i]`: enough for them and for the producers that use the good, at
/// most kMaxPackageProducers each.
std::vector<std::uint32_t> package_wanted(const std::vector<PackageNeed>& needs,
                                          const std::vector<std::uint32_t>& demand);

}  // namespace o1602::sim::ai
