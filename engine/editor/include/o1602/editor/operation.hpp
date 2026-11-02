// SPDX-License-Identifier: GPL-3.0-or-later
//
// An edit of the editor's document that can be done and taken back (ADR 0013). Every change of a
// document is one; History keeps them for undo and redo.
#pragma once

#include <cstddef>
#include <string_view>

#include "o1602/sim/world.hpp"

namespace o1602::editor {

class Operation {
public:
    Operation() = default;
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;
    virtual ~Operation() = default;

    /// Does the edit. The world is the one it was made for, as it was when it was made or after
    /// revert().
    virtual void apply(sim::World& world) = 0;
    /// Takes the edit back: the world is as it was before apply().
    virtual void revert(sim::World& world) = 0;
    /// About how much memory the operation holds, for History's limit.
    [[nodiscard]] virtual std::size_t memory() const noexcept = 0;
    /// What it does, as a key of the interface's texts ("tiles", "markers").
    [[nodiscard]] virtual std::string_view kind() const noexcept = 0;
};

}  // namespace o1602::editor
