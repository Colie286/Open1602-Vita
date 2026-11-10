// SPDX-License-Identifier: GPL-3.0-or-later
//
// The players' trade carts (TRADER1) as the renderer sees them: the simulation keeps them with the
// ships (sim::Ship::cart) and moves them on land; here they are land figures, drawn on their way
// between two ticks (ShipTrail) with their driving animation, slower on slow ground like the
// market carts.
#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/render/figure_animation.hpp"
#include "o1602/render/ship_trail.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::render {

class CartFigures {
public:
    /// Brings the figures in line with the carts of `world` and advances their animations by
    /// `dt_ms` of game time; `trail` places them between two ticks.
    void update(const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                const ShipTrail& trail, std::uint32_t dt_ms);
    [[nodiscard]] const std::vector<FigureInstance>& instances() const noexcept { return m_instances; }
    /// The figure of cart `id` as last drawn, nullptr if it is not shown.
    [[nodiscard]] const FigureInstance* find(std::uint32_t id) const noexcept;

private:
    struct Slot {
        AnimState state;
        std::uint8_t requested = 0;  ///< animation the cart asked for (driving or standing)
        bool seen = false;
        std::uint32_t pace_rest = 0;  ///< driving time not yet passed on, ms * 32 below one Wegspeed
    };
    std::map<std::uint32_t, Slot> m_slots;  ///< by Ship::id
    std::vector<FigureInstance> m_instances;
    std::vector<std::uint32_t> m_ids;  ///< Ship::id of each instance
};

}  // namespace o1602::render
