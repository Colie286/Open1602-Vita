// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one way the local player's commands go into the game. Without SDL, so that the tests have it.
#pragma once

#include "o1602/net/session.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::app {

/// Takes the commands of the local player: to the simulation, or while a multiplayer session runs
/// to the session, which carries them out on every computer at the same frame. No other code of the
/// game calls Simulation::submit(). Whoever owns the game and the session tells it where they are.
class CommandSink {
public:
    /// The running game (nullptr: none; commands without a session are dropped).
    void set_simulation(sim::Simulation* simulation) noexcept { m_simulation = simulation; }
    /// The multiplayer session that takes the commands in place of the simulation (nullptr: none).
    void set_session(net::Session* session) noexcept { m_session = session; }

    void submit(sim::Command command);

private:
    sim::Simulation* m_simulation = nullptr;
    net::Session* m_session = nullptr;
};

}  // namespace o1602::app
