// SPDX-License-Identifier: GPL-3.0-or-later
//
// What keeps a scenario from the original's limits (sim/original_limits.hpp): the editor shows it
// while a scenario is to keep to them (ScenarioInfo::original_limits, ADR 0013), and
// `o1602-tool scenario check` prints it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// One thing of a world the original cannot have.
struct LimitProblem {
    enum class Kind : std::uint8_t {
        MapSize,  ///< the map is not the original's size (`width` x `height`)
        Islands,  ///< more islands than the original holds (`count`, `limit`), so too:
        Cities,
        Ships,
        Soldiers,
        Towers,
        Players,          ///< players with a start
        IslandSize,       ///< an island wider or higher than the original's (`island`, `width`, `height`)
        Deposits,         ///< more deposits on an island (`island`, `count`, `limit`)
        PackageContent,   ///< buildings, figures or goods of a content package
        PackageClasses,   ///< classes of inhabitants or unlock levels of a content package
        PackageFactions,  ///< factions of a content package
        Balance,          ///< values of the balance changed by a content package
        Script,           ///< a script
    };
    Kind kind = Kind::MapSize;
    std::size_t count = 0;
    std::size_t limit = 0;
    std::size_t island = 0;  ///< IslandSize, Deposits: the island's index in World::islands()
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    friend bool operator==(const LimitProblem&, const LimitProblem&) = default;
};

/// Everything of `world` the original cannot have: the map's size, more islands, cities, ships,
/// soldiers or towers, a larger island or more deposits on one, more than four players with a
/// start, anything of a content package, values of the balance changed, a script. Empty if it fits.
[[nodiscard]] std::vector<LimitProblem> original_limit_problems(const sim::World& world);

/// A problem as one line in English (`world` names the island).
[[nodiscard]] std::string describe(const LimitProblem& problem, const sim::World& world);

/// One line for each of original_limit_problems().
[[nodiscard]] std::vector<std::string> check_original_limits(const sim::World& world);

}  // namespace o1602::editor
