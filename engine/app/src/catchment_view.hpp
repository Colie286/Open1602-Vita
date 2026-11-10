// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which catchment circles a clicked building lights. One click lights the circle of the building
// itself, as in the original. Clicked a second time (Open1602's own, for planning a city), it lights
// the circles of all the player's buildings of the same type on its island together: every chapel,
// tavern or fire station, so that the gaps between them show. Pure logic, so that the tests have it.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

/// The circles a clicked building lights, in island tiles.
struct CatchmentCircles {
    const assets::BuildingType* type = nullptr;  ///< the clicked building's type
    std::vector<sim::Catchment> circles;         ///< the clicked building's own circle first
    std::vector<sim::TilePos> origins;           ///< the buildings' origins, in the order of `circles`
};

/// Whether a building of `type` shows a catchment circle when clicked: production and public
/// buildings, not houses (their radius is only how far they look for a market).
[[nodiscard]] bool shows_catchment(const assets::BuildingType& type) noexcept;

/// The circles lit for the building of `player` with its origin at (x, y) on `island`: its own, and
/// with `same_type` those of all the player's other buildings of its type on the island. Nothing
/// when there is no such building of the player there or it shows no circle.
[[nodiscard]] std::optional<CatchmentCircles> catchment_circles(const sim::World& world,
                                                                const assets::BuildingTable& buildings,
                                                                sim::PlayerId player, std::uint16_t island,
                                                                std::uint16_t x, std::uint16_t y, bool same_type);

}  // namespace o1602::app
