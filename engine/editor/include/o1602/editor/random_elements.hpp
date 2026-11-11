// SPDX-License-Identifier: GPL-3.0-or-later
//
// The random elements of a scenario's world plan (ADR 0013, assets::ScenarioPlan): native villages,
// ore and gold, crops and treasures the generator gives the islands it makes when the game starts,
// "x at random", beside those the map has on its islands already, "y fixed". Each change of a
// number is made on a copy of the plan and given back for undo, as the placeholders'.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/core/result.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// The most of one element the plan gives at random.
inline constexpr std::uint32_t kMaxRandom = 99;

/// One entry of the plan.
struct RandomElement {
    enum class Kind : std::uint8_t { Natives, Ore, Crop, Treasure };
    Kind kind = Kind::Natives;
    std::size_t slot = 0;      ///< Natives: 0 north, 1 south; else the entry's index in its list
    std::uint8_t ware = 0;     ///< what the entry's building names (Natives: 0)
    std::uint32_t type = 0;    ///< Ore: 0 big vein, 1 small vein
    std::uint32_t random = 0;  ///< how many the generator gives
    std::uint32_t fixed = 0;   ///< how many the map's islands have already
    friend bool operator==(const RandomElement&, const RandomElement&) = default;
};

/// The entries of the plan of `world` that name something, in the order natives, ore, crops,
/// treasures; nothing for a world without a plan.
[[nodiscard]] std::vector<RandomElement> random_elements(const sim::World& world,
                                                         const assets::BuildingTable& buildings);

/// The entry `slot` of `kind` given at random `count` times. Fails (NotFound) for a world without a
/// plan or an entry that names nothing, (InvalidFormat) beyond kMaxRandom.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> set_random_count(const sim::World& world,
                                                                        RandomElement::Kind kind, std::size_t slot,
                                                                        std::uint32_t count);

}  // namespace o1602::editor
