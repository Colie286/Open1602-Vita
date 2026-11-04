// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/document.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/editor/operations.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/scenario_data.hpp"
#include "o1602/sim/validate.hpp"

namespace o1602::editor {

using core::ErrorCode;
using core::make_error;

namespace {

/// A scenario that makes its islands at the start keeps its plan in the editor (ADR 0013).
sim::WorldStart keep_plan() {
    sim::WorldStart start;
    start.keep_plan = true;
    return start;
}

}  // namespace

Document::Document(sim::World world, std::size_t history_limit)
    : m_own(std::make_unique<sim::World>(std::move(world))), m_world(m_own.get()), m_history(history_limit) {}

Document Document::edit(sim::World& world, std::size_t history_limit) {
    return Document(&world, history_limit);
}

core::Result<sim::World> Document::read(std::span<const std::uint8_t> bytes, const assets::BuildingTable& buildings,
                                        const assets::GameDirectory* game, std::vector<std::string>* warnings,
                                        const assets::FigureTable* figures) {
    if (sim::is_open1602_save(bytes)) {
        return sim::read_game(bytes, buildings, warnings, figures);
    }
    if (game == nullptr) {
        return make_error(ErrorCode::Unsupported, "editor: a file of the original needs the game's directory");
    }
    auto file = assets::ChunkFile::from_bytes(core::Bytes(bytes.begin(), bytes.end()));
    if (!file) {
        return file.error();
    }
    return sim::load_world(*file, buildings, *game, warnings, keep_plan());
}

core::Result<Document> Document::open(std::span<const std::uint8_t> bytes, const assets::BuildingTable& buildings,
                                      const assets::GameDirectory* game, std::vector<std::string>* warnings,
                                      const assets::FigureTable* figures) {
    auto world = read(bytes, buildings, game, warnings, figures);
    if (!world) {
        return world.error();
    }
    return Document(std::move(world).value());
}

std::vector<std::string> Document::check_scenario() const {
    std::vector<std::string> problems;
    if (const auto& info = m_world->scenario_info()) {
        problems = sim::check_scenario_info(*info);
    }
    for (std::string& p : sim::check_markers(m_world->markers(), m_world->width(), m_world->height())) {
        problems.push_back(std::move(p));
    }
    if (m_world->scenario_script().size() > sim::kMaxScenarioScriptLength) {
        problems.push_back("the script is larger than 1 MiB");
    }
    if (m_world->editor_state().size() > sim::kMaxEditorStateLength) {
        problems.push_back("the editor's state is larger than 4 MiB");
    }
    return problems;
}

core::Result<core::Bytes> Document::save(const assets::BuildingTable& buildings) {
    if (auto problems = check_scenario(); !problems.empty()) {
        return make_error(ErrorCode::InvalidFormat, "scenario: " + problems.front());
    }
    if (auto valid = sim::validate(*m_world); !valid) {
        return valid.error();
    }
    const bool described = !m_world->scenario_info();
    if (described) {
        perform(std::make_unique<ScenarioInfoEdit>(std::nullopt, sim::ScenarioInfo{}));
    }
    auto bytes = sim::write_game(*m_world, buildings);
    if (!bytes) {
        if (described) {
            undo();  // refused: the description goes again, the step can be done once more
        }
        return bytes;
    }
    m_saved_state = m_history.state();
    return bytes;
}

core::Result<core::Bytes> Document::save_game(const assets::BuildingTable& buildings) {
    if (auto valid = sim::validate(*m_world); !valid) {
        return valid.error();
    }
    // The mark is no step of the history: once saved changed, the game stays changed.
    sim::World::GameSettings settings = m_world->game_settings();
    settings.edited = true;
    m_world->set_game_settings(settings);
    auto bytes = sim::write_game(*m_world, buildings);
    if (!bytes) {
        return bytes;
    }
    m_saved_state = m_history.state();
    return bytes;
}

}  // namespace o1602::editor
