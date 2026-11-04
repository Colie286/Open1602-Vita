// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's dialog "Goals" (ADR 0013): the scenario's missions with all the original's goals and
// loss conditions, who holds which, and their texts; editor::goals has the rules.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "map_viewer.hpp"
#include "names.hpp"

#include "o1602/assets/game_data.hpp"
#include "o1602/core/text.hpp"
#include "o1602/editor/goals.hpp"
#include "o1602/editor/operations.hpp"
#include "o1602/editor/players.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using editor::GoalField;

/// The label of each field, by editor::GoalField.
constexpr std::array<const char*, editor::kGoalFields> kGoalLabels = {
    "editor.goals.cities",        "editor.goals.inhabitants",    "editor.goals.balance",
    "editor.goals.trade_balance", "editor.goals.defeated",       "editor.goals.defeat_player",
    "editor.goals.castle",        "editor.goals.cathedral",      "editor.goals.goods1",
    "editor.goals.goods1_amount", "editor.goals.goods2",         "editor.goals.goods2_amount",
    "editor.goals.islands1",      "editor.goals.islands2",       "editor.goals.city1",
    "editor.goals.city_class",    "editor.goals.city_count",     "editor.goals.city2",
    "editor.goals.city_class",    "editor.goals.city_count",     "editor.goals.city3",
    "editor.goals.city_class",    "editor.goals.city_count",     "editor.goals.every_city",
    "editor.goals.city_class",    "editor.goals.city_count",     "editor.goals.target",
    "editor.goals.target_city",   "editor.goals.protected_city", "editor.goals.protected_cities",
    "editor.goals.computers",     "editor.goals.message",
};

std::string number(std::int64_t n) {
    return std::to_string(n);
}

}  // namespace

std::string MapViewer::editor_goal_text(const sim::Mission& m, std::size_t field) const {
    const auto f = static_cast<GoalField>(field);
    const sim::World& world = m_editor->document.world();
    const std::int64_t v = editor::goal_value(m, f);
    const bool asked = editor::goal_asked(m, f);
    const auto off = [] { return ui::tr("editor.goals.off"); };
    const auto who = [&](std::int64_t slot_plus_one) {
        const sim::Player* p = world.player(static_cast<sim::PlayerId>(slot_plus_one - 1));
        return p != nullptr ? editor_player_label(*p) : ui::tr("editor.goals.nobody");
    };
    switch (f) {
        case GoalField::Balance:
        case GoalField::TradeBalance:
            return asked ? ui::tr("editor.goals.per_minute", {number(v)}) : off();
        case GoalField::DefeatPlayer:
            return v == 0 ? off() : who(v);
        case GoalField::Castle:
        case GoalField::Cathedral:
        case GoalField::Computers:
            return ui::tr(asked ? "editor.goals.yes" : "editor.goals.no");
        case GoalField::Goods1:
        case GoalField::Goods2: {
            if (v == 0) {
                return off();
            }
            const auto ware = static_cast<std::uint8_t>(v);
            if (const assets::OwnGood* own = world.goods().own(ware)) {
                return own->name(ui::current_language());
            }
            const assets::TextTable* texts = m_hud ? m_hud->texts() : nullptr;
            const std::vector<std::string> names =
                texts != nullptr ? texts->section("WARE") : std::vector<std::string>{};
            return ware < names.size() ? names[ware] : number(ware);
        }
        case GoalField::Goods1Amount:
        case GoalField::Goods2Amount:
            return ui::tr("editor.goals.tons", {number(v)});
        case GoalField::Islands1:
        case GoalField::Islands2: {
            namespace ware = assets::ware;
            switch (v) {
                case 0:
                    return off();
                case ware::kTobaccoField:
                    return ui::tr("editor.crop.tobacco");
                case ware::kSpiceField:
                    return ui::tr("editor.crop.spices");
                case ware::kSugarField:
                    return ui::tr("editor.crop.sugar");
                case ware::kCottonField:
                    return ui::tr("editor.crop.cotton");
                case ware::kWineField:
                    return ui::tr("editor.crop.wine");
                case ware::kCocoaField:
                    return ui::tr("editor.crop.cocoa");
                case ware::kOre:
                    return ui::tr("editor.goals.ore");
                case ware::kGold:
                    return ui::tr("editor.goals.gold");
                default:
                    return number(v);
            }
        }
        case GoalField::City1Class:
        case GoalField::City2Class:
        case GoalField::City3Class:
        case GoalField::EveryCityClass:
            return ui::tr("editor.goals.class." + number(v % editor::kGoalClasses));
        case GoalField::City1Count:
        case GoalField::City2Count:
        case GoalField::City3Count:
        case GoalField::EveryCityCount:
            return v == 0 ? off() : number(v);
        case GoalField::Target:
            return v == 0 ? ui::tr("editor.goals.any_opponent") : who(v);
        case GoalField::Message:
            return ui::tr(v == editor::kMessageColourWin ? "editor.goals.message.colour" : "editor.goals.message.done");
        default:
            return asked && v != 0 ? number(v) : off();
    }
}

void MapViewer::editor_goals_model(EditorModel& m) const {
    const sim::World& world = m_editor->document.world();
    const std::vector<sim::PlayerId> slots = editor::player_slots(world);
    m.max_missions = editor::kMaxMissions;
    static_assert(editor::kMaxMissions == 16 && editor::kMaxMissionText == 1000, "the panel's (kMissionTabs)");
    for (std::size_t mi = 0; mi < world.missions().size(); ++mi) {
        const sim::Mission& mission = world.missions()[mi];
        EditorMission e;
        e.text = mission.text;
        for (std::size_t f = 0; f < editor::kGoalFields; ++f) {
            e.goals.push_back(EditorRule{ui::tr(kGoalLabels[f]), editor_goal_text(mission, f),
                                         editor::goal_used(mission, static_cast<GoalField>(f))});
        }
        for (const sim::PlayerId s : slots) {
            e.holders.push_back(world.players()[s].mission == mi);
        }
        m.missions.push_back(std::move(e));
    }
}

void MapViewer::editor_goals_request(const EditorRequest& request) {
    using Kind = EditorRequest::Kind;
    const sim::World& world = m_editor->document.world();
    core::Result<std::unique_ptr<editor::Operation>> op = core::make_error(core::ErrorCode::NotFound, "nothing to do");
    switch (request.kind) {
        case Kind::MissionAdd: {
            sim::Mission fresh;
            fresh.success_message = editor::kMessageAccomplished;
            op = editor::set_mission(world, world.missions().size(), std::move(fresh));
            break;
        }
        case Kind::MissionRemove:
            op = editor::remove_mission(world, request.index);
            break;
        case Kind::MissionHolder: {
            const std::vector<sim::PlayerId> slots = editor::player_slots(world);
            if (request.amount >= slots.size()) {
                return;
            }
            const sim::PlayerId slot = slots[request.amount];
            const bool holds = world.players()[slot].mission == request.index;
            op = editor::set_mission_holder(world, slot,
                                            holds ? sim::kNoMission : static_cast<sim::MissionId>(request.index));
            break;
        }
        case Kind::GoalStep: {
            if (request.index >= world.missions().size() || request.goal >= editor::kGoalFields) {
                return;
            }
            sim::Mission m = world.missions()[request.index];
            editor::step_goal(world, m, static_cast<GoalField>(request.goal), request.amount != 0);
            op = editor::set_mission(world, request.index, std::move(m));
            break;
        }
        case Kind::MissionText: {
            if (request.index >= world.missions().size()) {
                return;
            }
            sim::Mission m = world.missions()[request.index];
            m.text = request.name;
            op = editor::set_mission(world, request.index, std::move(m));
            break;
        }
        default:
            return;
    }
    if (!op) {
        editor_notice(ui::tr("editor.goals.refused"), true, op.error().message);
        return;
    }
    m_editor->document.perform(std::move(op).value());
    m_editor->notice.clear();
    std::fprintf(stderr, "editor: missions changed (%zu)\n", m_editor->document.world().missions().size());
}

namespace {

/// The text of `texts` in `language` exactly (no other one in its place), UTF-8.
std::string text_in(const std::vector<sim::ScenarioText>& texts, std::string_view language) {
    for (const sim::ScenarioText& t : texts) {
        if (t.language == language) {
            return t.text;
        }
    }
    return {};
}

/// `texts` with `text` in `language`; an empty text takes the language away.
void set_text(std::vector<sim::ScenarioText>& texts, std::string_view language, std::string text) {
    const auto it =
        std::find_if(texts.begin(), texts.end(), [&](const sim::ScenarioText& t) { return t.language == language; });
    if (text.empty()) {
        if (it != texts.end()) {
            texts.erase(it);
        }
    } else if (it != texts.end()) {
        it->text = std::move(text);
    } else {
        texts.push_back(sim::ScenarioText{std::string(language), std::move(text)});
    }
}

}  // namespace

void MapViewer::editor_scenario_model(EditorModel& m) const {
    const sim::ScenarioInfo info = m_editor->document.world().scenario_info().value_or(sim::ScenarioInfo{});
    using Row = EditorScenarioRow;
    m.scenario_rows.resize(kEditorScenarioRows);
    m.scenario_texts.resize(kEditorScenarioRows);
    const auto text = [&](Row row, const char* label, const std::string& utf8) {
        const auto i = static_cast<std::size_t>(row);
        std::string shown = core::utf8_to_cp1252(utf8);
        m.scenario_texts[i] = shown;
        std::replace(shown.begin(), shown.end(), '\n', ' ');
        m.scenario_rows[i] = EditorRule{ui::tr(label), shown.empty() ? ui::tr("editor.scenario.empty") : shown, true};
    };
    text(Row::NameDe, "editor.scenario.name_de", text_in(info.names, "de"));
    text(Row::NameEn, "editor.scenario.name_en", text_in(info.names, "en"));
    text(Row::BriefingDe, "editor.scenario.briefing_de", text_in(info.briefings, "de"));
    text(Row::BriefingEn, "editor.scenario.briefing_en", text_in(info.briefings, "en"));
    text(Row::Author, "editor.scenario.author", info.author);
    text(Row::Version, "editor.scenario.version", info.version);
    m.scenario_rows[static_cast<std::size_t>(Row::Stars)] =
        EditorRule{ui::tr("editor.scenario.stars"),
                   ui::tr("editor.scenario.stars_value",
                          {std::to_string(info.stars), std::to_string(sim::ScenarioInfo::kMaxStars)}),
                   true};
    m.scenario_rows[static_cast<std::size_t>(Row::OriginalLimits)] = EditorRule{
        ui::tr("editor.scenario.limits"), ui::tr(info.original_limits ? "editor.goals.yes" : "editor.goals.no"), true};
}

void MapViewer::editor_scenario_request(const EditorRequest& request) {
    const sim::World& world = m_editor->document.world();
    const std::optional<sim::ScenarioInfo> before = world.scenario_info();
    sim::ScenarioInfo info = before.value_or(sim::ScenarioInfo{});
    using Row = EditorScenarioRow;
    const auto row = static_cast<Row>(request.index);
    if (request.kind == EditorRequest::Kind::ScenarioStep) {
        if (row == Row::Stars) {
            constexpr auto kMax = sim::ScenarioInfo::kMaxStars;
            info.stars = static_cast<std::uint8_t>(request.amount != 0 ? (info.stars + 1) % (kMax + 1)
                                                                       : (info.stars + kMax) % (kMax + 1));
        } else if (row == Row::OriginalLimits) {
            info.original_limits = !info.original_limits;
        } else {
            return;
        }
    } else {
        std::string text = core::cp1252_to_utf8(request.name);
        switch (row) {
            case Row::NameDe:
                set_text(info.names, "de", std::move(text));
                break;
            case Row::NameEn:
                set_text(info.names, "en", std::move(text));
                break;
            case Row::BriefingDe:
                set_text(info.briefings, "de", std::move(text));
                break;
            case Row::BriefingEn:
                set_text(info.briefings, "en", std::move(text));
                break;
            case Row::Author:
                info.author = std::move(text);
                break;
            case Row::Version:
                info.version = std::move(text);
                break;
            default:
                return;
        }
    }
    if (!sim::check_scenario_info(info).empty()) {
        editor_notice(ui::tr("editor.goals.refused"), true, sim::check_scenario_info(info).front());
        return;
    }
    if (before && *before == info) {
        return;
    }
    m_editor->document.perform(std::make_unique<editor::ScenarioInfoEdit>(before, std::move(info)));
    m_editor->notice.clear();
    std::fprintf(stderr, "editor: scenario description changed\n");
}

}  // namespace o1602::app
