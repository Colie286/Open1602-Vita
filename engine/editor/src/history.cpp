// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/history.hpp"

namespace o1602::editor {

void History::perform(sim::World& world, std::unique_ptr<Operation> op) {
    if (!op) {
        return;
    }
    op->apply(world);
    for (const Step& s : m_undone) {
        m_memory -= s.op->memory();
    }
    m_undone.clear();
    m_memory += op->memory();
    m_done.push_back(Step{std::move(op), m_next_serial++});
    trim();
}

bool History::undo(sim::World& world) {
    if (m_done.empty()) {
        return false;
    }
    Step step = std::move(m_done.back());
    m_done.pop_back();
    step.op->revert(world);
    m_undone.push_back(std::move(step));
    return true;
}

bool History::redo(sim::World& world) {
    if (m_undone.empty()) {
        return false;
    }
    Step step = std::move(m_undone.back());
    m_undone.pop_back();
    step.op->apply(world);
    m_done.push_back(std::move(step));
    return true;
}

std::string_view History::next_undo() const noexcept {
    return m_done.empty() ? std::string_view() : m_done.back().op->kind();
}

std::string_view History::next_redo() const noexcept {
    return m_undone.empty() ? std::string_view() : m_undone.back().op->kind();
}

void History::clear() noexcept {
    m_done.clear();
    m_undone.clear();
    m_memory = 0;
    m_base = 0;
}

void History::trim() {
    // The newest step stays even when it alone is over the limit: what was just done can always
    // be taken back.
    while (m_memory > m_limit && m_done.size() > 1) {
        m_memory -= m_done.front().op->memory();
        m_base = m_done.front().serial;
        m_done.pop_front();
    }
}

}  // namespace o1602::editor
