// SPDX-License-Identifier: GPL-3.0-or-later
//
// The document of the editor (ADR 0013): the world being edited with its undo history. It opens
// Open1602's own files and the original's scenarios and savegames, and is always saved as an
// Open1602 scenario (.o1602 with the section SCNI). The simulation never runs on it.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/result.hpp"
#include "o1602/editor/history.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

class Document {
public:
    /// A document of `world`, unchanged (as if just saved).
    explicit Document(sim::World world, std::size_t history_limit = History::kDefaultLimit);

    /// Opens a file: an Open1602 savegame or scenario, or a scenario (.szs, .szm) or savegame
    /// (.gam) of the original. Problems that do not stop it are added to `warnings`. With the
    /// `figures` an Open1602 file whose running game cannot be read is refused (sim::read_game).
    /// Files of the original need the `game` (its islands); without it only Open1602's open.
    [[nodiscard]] static core::Result<Document> open(std::span<const std::uint8_t> bytes,
                                                     const assets::BuildingTable& buildings,
                                                     const assets::GameDirectory* game,
                                                     std::vector<std::string>* warnings = nullptr,
                                                     const assets::FigureTable* figures = nullptr);

    [[nodiscard]] const sim::World& world() const noexcept { return m_world; }

    /// Does `op` and keeps it for undo.
    void perform(std::unique_ptr<Operation> op) { m_history.perform(m_world, std::move(op)); }
    bool undo() { return m_history.undo(m_world); }
    bool redo() { return m_history.redo(m_world); }
    [[nodiscard]] const History& history() const noexcept { return m_history; }

    /// Whether the world differs from what was opened or saved last (undo back to it: not modified).
    [[nodiscard]] bool modified() const noexcept { return m_history.state() != m_saved_state; }

    /// The file of the document as an Open1602 scenario: a world that is no scenario yet gets an
    /// empty description (ScenarioInfo) first, as one step of the history. Refused, with nothing
    /// changed, while the world does not hold together (sim::validate) or its description or
    /// markers are wrong. Saving makes the document unmodified.
    [[nodiscard]] core::Result<core::Bytes> save(const assets::BuildingTable& buildings);

    /// What is wrong with the scenario's own parts (description, markers, script), one line each.
    [[nodiscard]] std::vector<std::string> check_scenario() const;

private:
    sim::World m_world;
    History m_history;
    std::uint64_t m_saved_state = 0;
};

}  // namespace o1602::editor
