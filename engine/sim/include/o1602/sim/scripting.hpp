// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the scripts of content packages join the simulation (ADR 0007 section 6,
// docs/modding/scripts.md). Every computer of a multiplayer game and every replay runs the same
// scripts, so they run inside the tick, after its steps, with what happened in that tick: the game
// goes on the same everywhere without sending anything the scripts do over the network. The
// simulation knows only this interface; the scripts themselves are engine/script.
#pragma once

#include <span>
#include <vector>

#include "o1602/sim/construction.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/sim/script_state.hpp"

namespace o1602::sim {

class Simulation;

/// What happened in one tick, for the scripts. At most EventBuffer::kMaxPending / 2 events of each
/// kind (the latest), so that every computer sees the same however often its interface takes them.
struct TickEvents {
    std::span<const ConstructionEvent> construction;
    std::span<const HouseEvent> houses;
    std::span<const GameEvent> game;
};

class ScriptHost {
public:
    ScriptHost() = default;
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;
    virtual ~ScriptHost() = default;

    /// At the end of every tick that ran (not while paused).
    virtual void after_tick(Simulation& simulation, const TickEvents& events) = 0;
    /// The state of every package's scripts, for the savegame and the game's fingerprint. Not
    /// const: saving runs in the scripts' Lua states, which is invisible to the scripts.
    [[nodiscard]] virtual std::vector<ScriptState> save_states() = 0;
};

}  // namespace o1602::sim
