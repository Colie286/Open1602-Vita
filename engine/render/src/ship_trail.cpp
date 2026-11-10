// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/ship_trail.hpp"

namespace o1602::render {

namespace {

/// A ship without a position lies on its tile's centre.
void now_at(const sim::Ship& ship, double& x, double& y) {
    const bool placed = ship.pos_x != 0 || ship.pos_y != 0;
    x = placed ? static_cast<double>(ship.pos_x) / sim::kShipUnit : ship.x + 0.5;
    y = placed ? static_cast<double>(ship.pos_y) / sim::kShipUnit : ship.y + 0.5;
}

}  // namespace

void ShipTrail::capture(const sim::World& world) {
    m_trail.capture([&world](const auto& add) {
        for (const sim::Ship& s : world.ships()) {
            if (s.pos_x != 0 || s.pos_y != 0) {
                add(s.id, static_cast<double>(s.pos_x) / sim::kShipUnit, static_cast<double>(s.pos_y) / sim::kShipUnit);
            }
        }
    });
}

void ShipTrail::position(const sim::Ship& ship, double& x, double& y) const {
    now_at(ship, x, y);
    if (ship.pos_x != 0 || ship.pos_y != 0) {
        m_trail.position(ship.id, x, y);
    }
}

}  // namespace o1602::render
