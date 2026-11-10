// SPDX-License-Identifier: GPL-3.0-or-later
//
// The path a dragged road or wall takes (docs/re/construction.md#dragging-roads-and-walls).
//
// When the player drags a road or a wall, the original searches a path from the tile where the
// drag began to the tile under the cursor (0x0040A890, 0x0040AB20, 0x0040C230, 0x0040C050): a
// breadth-first search over the tiles of the start tile's city that may carry the road or wall,
// at most 257 steps. Of several shortest paths it takes the one whose tiles were reached first,
// with the neighbours visited north, south, west, east; in open land that is all steps along y,
// then all along x. A cursor it cannot reach gives just the cursor tile.
//
// This is a pure function of the map, so the interface can show it as a preview and the build
// order carries only its result. Other drag behaviours can live next to it later; the original is
// the default.
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

enum class DragLine : std::uint8_t {
    Road,  ///< roads (drag mode 3): road, forest, ground, ruin, square, river, bridge, gate centre
    Wall,  ///< walls (drag mode 2): gates, walls, towers, forest, ground, ruins, beach
};

/// One tile of the path and the rotation of its step: x+1 → 0, y+1 → 1, x−1 → 2, y−1 → 3.
struct DragStep {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint8_t rotation = 0;

    friend bool operator==(const DragStep&, const DragStep&) = default;
};

/// Steps of the original's drag path from `from` to `to` on an island, start and cursor included.
/// The tile under the cursor repeats the rotation of the last step; a click (from == to), a cursor
/// outside the reach of the search or on a tile the line cannot take give one step on the cursor
/// with `rotation` (the rotation the player chose).
[[nodiscard]] std::vector<DragStep> drag_path(const World& world, const assets::BuildingTable& buildings,
                                              std::uint16_t island, DragLine line, TilePos from, TilePos to,
                                              std::uint8_t rotation);

/// Whether a tile with this building may be part of a dragged line (0x0040AEF0, 0x0040AE30).
/// `gate_centre`: the tile is the middle tile of a gate (roads pass gates only there).
[[nodiscard]] bool drag_passable(DragLine line, std::int32_t kind, bool gate_centre) noexcept;

}  // namespace o1602::sim
