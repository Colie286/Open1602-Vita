// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdexcept>
#include "o1602/sim/simulation.hpp"
// A fixed regular ship command through the engine's command queue, shared by host and ARM.
inline void probe_ship_order(o1602::sim::Simulation& simulation) {
    for (const auto& ship : simulation.world().ships()) {
        const auto* player=simulation.world().player(ship.owner);
        if (player && player->role==o1602::sim::PlayerRole::Human) {
            o1602::sim::ShipOrder order;
            order.player=ship.owner; order.ship=ship.id;
            order.target=o1602::sim::Target::position(ship.x+2,ship.y);
            simulation.submit(order);
            return;
        }
    }
    throw std::runtime_error("no human ship for probe order");
}
