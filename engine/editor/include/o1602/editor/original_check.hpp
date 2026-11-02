// SPDX-License-Identifier: GPL-3.0-or-later
//
// What keeps a scenario from the original's limits (sim/original_limits.hpp): the editor shows it
// while a scenario is to keep to them (ScenarioInfo::original_limits, ADR 0013), and
// `o1602-tool scenario check` prints it.
#pragma once

#include <string>
#include <vector>

#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// One line for everything of `world` the original cannot have: the map's size, more islands,
/// cities, ships, soldiers or towers, a larger island or more deposits on one, more than four
/// players with a start, anything of a content package, values of the balance changed, a script.
/// Empty if it fits.
[[nodiscard]] std::vector<std::string> check_original_limits(const sim::World& world);

}  // namespace o1602::editor
