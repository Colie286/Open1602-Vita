// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/scenario_rules.hpp"

#include <utility>

#include "o1602/editor/operations.hpp"
#include "o1602/sim/scenario_data.hpp"

namespace o1602::editor {

std::optional<sim::Rules> scenario_rules(const sim::World& world) {
    const auto& info = world.scenario_info();
    return info ? info->rules : std::nullopt;
}

std::unique_ptr<Operation> set_scenario_rules(const sim::World& world, std::optional<sim::Rules> rules) {
    const std::optional<sim::ScenarioInfo>& before = world.scenario_info();
    sim::ScenarioInfo after = before.value_or(sim::ScenarioInfo{});
    if (before && after.rules == rules) {
        return nullptr;
    }
    after.rules = std::move(rules);
    return std::make_unique<ScenarioInfoEdit>(before, std::move(after));
}

}  // namespace o1602::editor
