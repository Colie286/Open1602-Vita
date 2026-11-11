// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's page "Rules" (ADR 0013): the game settings a scenario is made for, the same as the
// start dialog offers for a scenario, all at once as in the original or as Open1602 has them. The
// start dialog begins from them; without them the player's own settings apply.

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "endless_setup.hpp"
#include "map_viewer.hpp"

#include "o1602/assets/game_data.hpp"
#include "o1602/editor/random_elements.hpp"
#include "o1602/editor/scenario_rules.hpp"
#include "o1602/sim/new_game.hpp"
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

/// What an entry of the world plan gives, in the player's language.
std::string random_label(const editor::RandomElement& e) {
    using Kind = editor::RandomElement::Kind;
    namespace ware = assets::ware;
    switch (e.kind) {
        case Kind::Natives:
            return ui::tr(e.slot == 0 ? "editor.random.natives_north" : "editor.random.natives_south");
        case Kind::Ore:
            if (e.ware == ware::kGold) {
                return ui::tr("editor.objects.resource.2");
            }
            return ui::tr(e.type == 0 ? "editor.objects.resource.0" : "editor.objects.resource.1");
        case Kind::Treasure:
            return ui::tr("editor.objects.resource.3");
        case Kind::Crop:
            break;
    }
    static constexpr std::array<std::pair<std::uint8_t, const char*>, 6> kCrops = {
        {{ware::kTobaccoField, "editor.crop.tobacco"},
         {ware::kSpiceField, "editor.crop.spices"},
         {ware::kSugarField, "editor.crop.sugar"},
         {ware::kCottonField, "editor.crop.cotton"},
         {ware::kWineField, "editor.crop.wine"},
         {ware::kCocoaField, "editor.crop.cocoa"}}};
    for (const auto& [w, key] : kCrops) {
        if (w == e.ware) {
            return ui::tr("editor.random.crop", {ui::tr(key)});
        }
    }
    return ui::tr("editor.random.crop", {std::to_string(e.ware)});
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
    for (const editor::RandomElement& e : editor::random_elements(m_editor->document.world(), m_buildings)) {
        m.randoms.push_back(EditorRandom{random_label(e), e.random, e.fixed});
    }
    m.previewing = m_editor->preview_plan.has_value();
    m.preview_roll = m_editor->preview_rolls;
    m.preview_islands = m.previewing ? m_state->world.islands().size() : 0;
}

void MapViewer::editor_roll() {
    const sim::World& plan = m_editor->preview_plan ? *m_editor->preview_plan : m_state->world;
    if (!plan.planned_islands()) {
        return;
    }
    sim::WorldStart start = m_options.world;
    start.keep_plan = false;
#ifdef O1602_EXPERIMENTAL_VITA
    const auto device = [] { return static_cast<std::uint32_t>(new_player_id()); };
#else
    std::random_device device;
#endif
    start.seed = (static_cast<std::uint64_t>(device()) << 32U) | device();
    std::vector<std::string> warnings;
    auto rolled = sim::roll_planned_islands(plan, m_game, m_buildings, start, &warnings);
    if (!rolled) {
        editor_notice(ui::tr("editor.preview.failed", {ui::error_text(rolled.error(), "editor")}), true,
                      rolled.error().message);
        return;
    }
    if (!m_editor->preview_plan) {
        m_editor->preview_plan = std::move(m_state->world);
        m_editor->preview_rolls = 0;
    }
    m_state->world = std::move(rolled).value();
    ++m_editor->preview_rolls;
    m_editor->pick = EditorPick{};
    m_editor->placing.reset();
    m_editor->selection.reset();
    m_editor->notice.clear();
    editor_islands_changed();
    std::fprintf(stderr, "editor: dice preview, roll %u, %zu islands, %zu warnings\n",
                 static_cast<unsigned>(m_editor->preview_rolls), m_state->world.islands().size(), warnings.size());
}

void MapViewer::editor_end_preview() {
    if (!m_editor->preview_plan) {
        return;
    }
    m_state->world = std::move(*m_editor->preview_plan);
    m_editor->preview_plan.reset();
    m_editor->preview_rolls = 0;
    m_editor->pick = EditorPick{};
    m_editor->notice.clear();
    editor_islands_changed();
    std::fprintf(stderr, "editor: dice preview ended, the plan back\n");
}

void MapViewer::editor_rules_request(const EditorRequest& request) {
    if (request.kind == EditorRequest::Kind::RandomRoll) {
        editor_roll();
        return;
    }
    if (request.kind == EditorRequest::Kind::RandomPreviewEnd) {
        editor_end_preview();
        return;
    }
    const sim::World& world = m_editor->document.world();
    if (request.kind == EditorRequest::Kind::RandomStep) {
        const auto list = editor::random_elements(world, m_buildings);
        if (request.index >= list.size()) {
            return;
        }
        const editor::RandomElement& e = list[request.index];
        // One more or one fewer, around at the ends.
        const std::uint32_t count = request.amount != 0 ? (e.random >= editor::kMaxRandom ? 0 : e.random + 1)
                                                        : (e.random == 0 ? editor::kMaxRandom : e.random - 1);
        auto op = editor::set_random_count(world, e.kind, e.slot, count);
        if (!op) {
            editor_notice(ui::tr("editor.random.refused"), true, op.error().message);
            return;
        }
        m_editor->document.perform(std::move(op).value());
        m_editor->notice.clear();
        std::fprintf(stderr, "editor: random element %zu now %u at random\n", request.index,
                     static_cast<unsigned>(count));
        return;
    }
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
