// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <utility>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/script/game_scripts.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

/// One game: the world and the simulation that runs on it. A game that is loaded or started is
/// built whole first and then takes the place of the old one, which stays as it was if building
/// fails. Not movable: the simulation keeps a reference to the world next to it.
struct GameState {
    GameState(sim::World w, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
              const sim::Rules& rules)
        : world(std::move(w)), simulation(world, buildings, figures, rules) {}
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;
    ~GameState() { simulation.set_scripts(nullptr); }

    /// Runs the scripts of `packages` in this game (none: no scripts); the scripts that ran so far
    /// hand their state over, so that a game whose packages are loaded again goes on with it.
    void use_scripts(std::vector<script::PackageScript> packages) {
        if (scripts) {
            world.set_script_states(scripts->save_states());
        }
        simulation.set_scripts(nullptr);
        scripts.reset();
        if (!packages.empty()) {
            scripts = std::make_unique<script::GameScripts>(std::move(packages), world);
            simulation.set_scripts(scripts.get());
        }
    }

    sim::World world;
    sim::Simulation simulation;
    /// The scripts of the game's packages (docs/modding/scripts.md); the simulation runs them.
    std::unique_ptr<script::GameScripts> scripts;
};

}  // namespace o1602::app
