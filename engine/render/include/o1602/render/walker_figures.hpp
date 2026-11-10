// SPDX-License-Identifier: GPL-3.0-or-later
//
// Carriers and carts of the logistics as the renderer sees them. The simulation moves them; here
// only their walking animations run: empty or loaded (ANIM 1 / 0 of TRAEGER and KARREN), slower
// on slow ground like in the original (0x0045D0B0 stretches a frame by Wegspeed / 32). Between two
// ticks they are drawn on their way (TickTrail).
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/render/figure_animation.hpp"
#include "o1602/render/tick_trail.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::render {

class WalkerFigures {
public:
    /// Before a tick: the positions the walkers leave.
    void capture(const sim::Logistics& logistics, const sim::World& world);
    /// Brings the figures in line with the walkers of `logistics` and advances their animations
    /// by `dt_ms` of game time. `fraction` is the share of the next tick that has passed
    /// (Simulation::tick_fraction): a walker is drawn that far on its way from before the last
    /// tick (capture) to where it is.
    void update(const sim::Logistics& logistics, const sim::World& world, const assets::BuildingTable& buildings,
                const assets::FigureTable& figures, std::uint32_t dt_ms, double fraction = 1.0);
    [[nodiscard]] const std::vector<FigureInstance>& instances() const noexcept { return m_instances; }

private:
    struct Slot {
        std::uint32_t generation = 0;
        bool used = false;    ///< seen in this update
        bool active = false;  ///< `state` belongs to the walker in this slot
        /// Animation the walker asked for; `state` may have moved on to a successor (RANDOM and
        /// JUMPTO animations: grazing deer look up, a shot deer falls and lies).
        std::uint8_t requested = 0;
        AnimState state;
        double z = 0.0;               ///< height, from the tile it was first seen on (render::spawn_height)
        std::uint32_t pace_rest = 0;  ///< walking time not yet passed on, ms * 32 below one Wegspeed
    };
    std::vector<Slot> m_slots;  ///< by walker handle index
    std::vector<FigureInstance> m_instances;
    TickTrail m_trail;  ///< by walker handle (index, generation)
};

}  // namespace o1602::render
