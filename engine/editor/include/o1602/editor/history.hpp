// SPDX-License-Identifier: GPL-3.0-or-later
//
// Undo and redo of the editor (ADR 0013): the operations done, the newest last, and those taken
// back. There is no fixed number of steps: the oldest steps are forgotten when the operations
// together hold more than the limit.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string_view>
#include <vector>

#include "o1602/editor/operation.hpp"

namespace o1602::editor {

class History {
public:
    /// The memory the steps may hold together by default.
    static constexpr std::size_t kDefaultLimit = 256U * 1024U * 1024U;

    explicit History(std::size_t limit = kDefaultLimit) noexcept : m_limit(limit) {}

    /// Applies `op` to `world` and keeps it as the newest step; the steps taken back are gone.
    void perform(sim::World& world, std::unique_ptr<Operation> op);
    /// Keeps `op`, which the world has already been through, as the newest step without applying
    /// it again; the steps taken back are gone.
    void record(std::unique_ptr<Operation> op);
    /// Takes the newest step back; false if there is none.
    bool undo(sim::World& world);
    /// Does the step taken back last again; false if there is none.
    bool redo(sim::World& world);

    [[nodiscard]] bool can_undo() const noexcept { return !m_done.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !m_undone.empty(); }
    [[nodiscard]] std::size_t undo_steps() const noexcept { return m_done.size(); }
    [[nodiscard]] std::size_t redo_steps() const noexcept { return m_undone.size(); }
    /// What undo() and redo() would take back or do again; empty if nothing.
    [[nodiscard]] std::string_view next_undo() const noexcept;
    [[nodiscard]] std::string_view next_redo() const noexcept;
    /// The memory the steps hold together.
    [[nodiscard]] std::size_t memory() const noexcept { return m_memory; }
    /// Forgets every step (a document opened anew).
    void clear() noexcept;

    /// A number for the state the steps lead to: the same after undo and redo back to it, another
    /// after any new step. 0 is the state with no step done. Document::modified() compares it.
    [[nodiscard]] std::uint64_t state() const noexcept { return m_done.empty() ? m_base : m_done.back().serial; }

private:
    struct Step {
        std::unique_ptr<Operation> op;
        std::uint64_t serial = 0;
    };
    void trim();

    std::size_t m_limit;
    std::size_t m_memory = 0;
    std::uint64_t m_next_serial = 1;
    /// The state below the oldest step kept (0 until steps were forgotten).
    std::uint64_t m_base = 0;
    std::deque<Step> m_done;
    std::vector<Step> m_undone;
};

}  // namespace o1602::editor
