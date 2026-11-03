// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's page "Rules" (ADR 0013): the game settings a scenario is made for, the same as the
// start dialog offers for a scenario, all at once as in the original or as Open1602 has them. The
// start dialog begins from them; without them the player's own settings apply.

#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "endless_setup.hpp"
#include "map_viewer.hpp"

#include "o1602/editor/scenario_rules.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// The settings a scenario can have, in the start dialog's order.
std::vector<SetupItem> scenario_items() {
    std::vector<SetupItem> items;
    for (std::size_t i = 0; i < static_cast<std::size_t>(SetupItem::Count); ++i) {
        const auto item = static_cast<SetupItem>(i);
        if (setup_item_offered(SetupScope::Scenario, item)) {
            items.push_back(item);
        }
    }
    return items;
}

/// The start dialog's label, or a shorter one where the panel is too narrow for it.
std::string rule_label(SetupItem item) {
    switch (item) {
        case SetupItem::TradeCarts:
            return ui::tr("editor.rules.label.trade_carts");
        case SetupItem::BalanceShips:
            return ui::tr("editor.rules.label.ship_holds");
        case SetupItem::ShipPurchases:
            return ui::tr("editor.rules.label.ship_purchases");
        case SetupItem::Reveal:
            return ui::tr("editor.rules.label.reveal");
        default:
            return setup_label(item);
    }
}

}  // namespace

void MapViewer::editor_rules_model(EditorModel& m) const {
    const std::optional<sim::Rules> own = editor::scenario_rules(m_editor->document.world());
    m.rules_own = own.has_value();
    m.rules_original = own && *own == sim::Rules::original();
    EndlessSetup setup = EndlessSetup::defaults(SetupScope::Scenario);
    setup.rules = own.value_or(m_app_settings.scenario.rules);
    for (const SetupItem item : scenario_items()) {
        m.rules.push_back(EditorRule{rule_label(item), setup_value(setup, item), setup_item_used(setup, item)});
    }
}

void MapViewer::editor_rules_request(const EditorRequest& request) {
    const sim::World& world = m_editor->document.world();
    std::optional<sim::Rules> rules = editor::scenario_rules(world);
    if (request.kind == EditorRequest::Kind::RulesPreset) {
        switch (request.index) {
            case 0:
                rules.reset();
                break;
            case 1:
                rules = sim::Rules::original();
                break;
            case 2:
                rules = sim::Rules::open1602();
                break;
            default:
                rules = m_app_settings.scenario.rules;
                break;
        }
    } else {
        const std::vector<SetupItem> items = scenario_items();
        if (!rules || request.index >= items.size()) {
            return;
        }
        EndlessSetup setup = EndlessSetup::defaults(SetupScope::Scenario);
        setup.rules = *rules;
        if (request.amount != 0) {
            choose_next(setup, items[request.index]);
        } else {
            choose_previous(setup, items[request.index]);
        }
        rules = setup.rules;
    }
    if (auto op = editor::set_scenario_rules(world, rules)) {
        m_editor->document.perform(std::move(op));
        std::fprintf(stderr, "editor: rules %s\n",
                     !rules                             ? "of the player"
                     : *rules == sim::Rules::original() ? "as in the original"
                                                        : "of the scenario changed");
    }
    m_editor->notice.clear();
}

}  // namespace o1602::app
