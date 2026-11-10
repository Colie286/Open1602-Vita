// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ships drawn between two ticks (ADR 0004, review point 20): a TickTrail keyed by Ship::id.
#pragma once

#include "o1602/render/tick_trail.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::render {

class ShipTrail {
public:
    /// Before a tick: the positions the ships leave.
    void capture(const sim::World& world);
    /// Share of the next tick that has passed (Simulation::tick_fraction).
    void set_fraction(double fraction) noexcept { m_trail.set_fraction(fraction); }

    /// Where to draw the ship, in tiles: between its position before the last tick and its
    /// position now. A ship that was not there before, or jumped (more than two tiles), is drawn
    /// where it is.
    void position(const sim::Ship& ship, double& x, double& y) const;

private:
    TickTrail m_trail;  ///< placed ships only
};

}  // namespace o1602::render
