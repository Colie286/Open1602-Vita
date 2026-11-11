// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game settings a scenario of the editor is made for (ADR 0013, sim::ScenarioInfo::rules): the
// start dialog begins from them, and the player can still change them. A scenario without them
// starts with the player's own settings, as any other.
#pragma once

#include <memory>
#include <optional>

#include "o1602/editor/operation.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// The settings the scenario `world` is made for; nothing if it has none (or is no scenario).
[[nodiscard]] std::optional<sim::Rules> scenario_rules(const sim::World& world);

/// The scenario made for `rules` (nothing: for the player's own), as a step of undo. A world
/// without a description gets one. Nothing to do (nullptr) if they are the same already.
[[nodiscard]] std::unique_ptr<Operation> set_scenario_rules(const sim::World& world, std::optional<sim::Rules> rules);

}  // namespace o1602::editor
