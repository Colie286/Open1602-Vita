// SPDX-License-Identifier: GPL-3.0-or-later
//
// Courses of ships (docs/re/ships.md, "Pathfinding at sea").
//
// The original plans a ship's course in a square window around the ship (161 x 161 tiles for an
// ordinary order, 0x00455A20) with the wavefront of the land figures: open sea and the water tiles
// inside an island's rectangle are sailable, coast, beach, surf, piers and buildings are not, other
// ships nearby are blocked out as 3 x 3 squares. A target further away than the window is reached in
// legs through the window edge closest to it. The course is stored as runs of one or two tiles in one
// of eight directions.
//
// Open1602 runs an exact Dijkstra on the same integer costs (64 straight, 91 diagonal, no cut
// corners) with a fixed neighbour order, so the result is the same on every computer (ADR 0004).
// Cells outside the world are blocked; the original leaves them open (S-070).
//
// On large worlds a window is not enough: an island can be wider than the window, and the edge
// cell closest to the target as the crow flies often lies deep in a bay, where the next leg turns
// back. Open1602 therefore keeps a coarse map of the sea: the world is cut into blocks of
// 16 x 16 tiles, and the water of each block into its connected regions. When no goal lies in
// the window, a route over these regions to the target is planned, and the leg ends where the
// window gets furthest along that route.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// Which world tiles ships may sail on, kept up to date with building.
class SeaMap {
public:
    /// Builds the map of a world.
    void rebuild(const World& world, const assets::BuildingTable& buildings);
    /// Updates the tiles of one island (something was built or demolished on it).
    void update_island(const World& world, const assets::BuildingTable& buildings, std::uint16_t island);
    /// The same for the island tiles x0..x1, y0..y1 only (inclusive, clipped to the island).
    void update_island(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                       std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1);

    [[nodiscard]] bool sailable(std::int32_t x, std::int32_t y) const noexcept {
        return x >= 0 && y >= 0 && static_cast<std::uint32_t>(x) < m_width &&
               static_cast<std::uint32_t>(y) < m_height &&
               m_cells[static_cast<std::size_t>(y) * m_width + static_cast<std::uint32_t>(x)] != 0;
    }
    [[nodiscard]] std::uint32_t width() const noexcept { return m_width; }
    [[nodiscard]] std::uint32_t height() const noexcept { return m_height; }

    // ---- Coarse map: water regions of 16 x 16 tile blocks ----

    /// Side of a block in tiles.
    static constexpr std::int32_t kBlock = 16;
    /// No region (land, or outside the world).
    static constexpr std::uint32_t kNoRegion = 0xFFFFFFFFU;

    [[nodiscard]] std::uint32_t blocks_x() const noexcept { return m_blocks_x; }
    [[nodiscard]] std::uint32_t blocks_y() const noexcept { return m_blocks_y; }
    /// Number of water regions of all blocks.
    [[nodiscard]] std::uint32_t regions() const noexcept { return static_cast<std::uint32_t>(m_region_block.size()); }
    /// The block a region lies in.
    [[nodiscard]] std::uint32_t region_block(std::uint32_t region) const noexcept { return m_region_block[region]; }
    /// The region of a sailable tile, kNoRegion for land and the outside. Two tiles of one block
    /// share a region exactly when a ship can sail from one to the other inside the block.
    [[nodiscard]] std::uint32_t region(std::int32_t x, std::int32_t y) const noexcept {
        if (!sailable(x, y)) {
            return kNoRegion;
        }
        const auto ux = static_cast<std::uint32_t>(x);
        const auto uy = static_cast<std::uint32_t>(y);
        const std::uint32_t block = (uy / kBlock) * m_blocks_x + ux / kBlock;
        return m_region_first[block] + m_region[std::size_t{uy} * m_width + ux] - 1U;
    }

private:
    void set_island_cells(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                          std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1);
    /// Labels the regions of the blocks bx0..bx1, by0..by1 (inclusive) and renumbers all regions.
    void label_blocks(std::uint32_t bx0, std::uint32_t by0, std::uint32_t bx1, std::uint32_t by1);

    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::vector<std::uint8_t> m_cells;  ///< 1 = sailable
    std::uint32_t m_blocks_x = 0;
    std::uint32_t m_blocks_y = 0;
    std::vector<std::uint8_t> m_region;         ///< per tile: 0 land, else region number in its block from 1
    std::vector<std::uint8_t> m_region_count;   ///< per block: regions in it
    std::vector<std::uint32_t> m_region_first;  ///< per block: index of its first region
    std::vector<std::uint32_t> m_region_block;  ///< per region: its block
    std::vector<std::uint16_t> m_label_stack;   ///< scratch of label_blocks()
};

/// Whether a building tile is water ships sail on (HAUS Kind MEER).
[[nodiscard]] bool sailable_building(const assets::BuildingType& building) noexcept;

/// The original's distance of two tiles: the longer axis plus a quarter of the shorter one.
[[nodiscard]] constexpr std::int32_t sea_distance(std::int32_t dx, std::int32_t dy) noexcept {
    const std::int32_t ax = dx < 0 ? -dx : dx;
    const std::int32_t ay = dy < 0 ? -dy : dy;
    return ax > ay ? ax + ay / 4 : ay + ax / 4;
}

/// A path code: (direction << 4) | tiles, as the original stores ship paths.
[[nodiscard]] constexpr std::uint8_t path_code(std::uint8_t direction, std::uint8_t tiles) noexcept {
    return static_cast<std::uint8_t>((std::uint32_t{direction} << 4U) | (std::uint32_t{tiles} & 0x0FU));
}
/// Tile step of a direction 0..7 (0 = -y, 2 = +x, 4 = +y, 6 = -x).
[[nodiscard]] constexpr std::int32_t step_x(std::uint8_t direction) noexcept {
    constexpr std::int32_t kX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    return kX[direction & 7U];
}
[[nodiscard]] constexpr std::int32_t step_y(std::uint8_t direction) noexcept {
    constexpr std::int32_t kY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    return kY[direction & 7U];
}

/// One planning request.
struct SeaPlan {
    std::int32_t start_x = 0;  ///< world tile of the ship
    std::int32_t start_y = 0;
    std::int32_t radius = 80;  ///< half width of the window
    std::int32_t shift_x = 0;  ///< window moved off-centre (the original does this after a failure)
    std::int32_t shift_y = 0;
    /// Point the planner aims at when no goal lies in the window (target outside it).
    std::int32_t aim_x = 0;
    std::int32_t aim_y = 0;
    /// Whether a sailable tile is a goal.
    std::function<bool(std::int32_t, std::int32_t)> goal;
    /// Other ships close by: blocked as 3 x 3 squares; standing ones only on their own tile and
    /// expensive around it when `soft_ships` (after a failed plan).
    struct Obstacle {
        std::int32_t x = 0;
        std::int32_t y = 0;
        bool moving = false;
    };
    std::vector<Obstacle> ships;
    bool soft_ships = false;
    /// Water within reach of hostile cannon towers: about eight times as expensive (cost class
    /// 0x7F, 0x0046D720); the ship still sails there if there is no other way. World tiles,
    /// inclusive.
    struct Danger {
        std::int32_t x0 = 0;
        std::int32_t y0 = 0;
        std::int32_t x1 = 0;
        std::int32_t y1 = 0;
    };
    std::vector<Danger> danger;
    /// When no goal lies in the reach of the window: follow a route over the coarse map of the
    /// sea to the aim point (and the goal tiles near it) instead of heading for the edge cell
    /// closest to the aim as the crow flies, which on large islands often lies in a bay.
    bool guided = false;
};

/// What the planner found.
struct SeaCourse {
    std::vector<std::uint8_t> path;  ///< runs, at most 99
    bool reached_goal = false;       ///< ends on a goal; otherwise a leg towards the aim point
    std::int32_t end_x = 0;
    std::int32_t end_y = 0;
};

/// Planner with reusable buffers.
class SeaPlanner {
public:
    static constexpr std::size_t kMaxRuns = 99;
    static constexpr std::uint8_t kMaxRunTiles = 2;

    /// Plans a course; an empty path means there is no way (or the ship is already at a goal).
    [[nodiscard]] SeaCourse plan(const SeaMap& sea, const SeaPlan& request);

private:
    std::vector<std::uint32_t> m_cost;
    std::vector<std::uint8_t> m_state;  ///< bit 0-1: 0 free, 1 blocked; bit 2 visited; bits 4-7 came-from + 1
    std::vector<std::uint8_t> m_class;  ///< cost class: 0 sea, 1 around a standing ship, 2 near hostile towers
    struct Node {
        std::uint32_t cost;
        std::uint32_t seq;
        std::uint32_t cell;
    };
    std::vector<Node> m_heap;

    /// Plans a route over the regions of the coarse map from the start to the regions near the
    /// aim; on success `m_route_step` holds 1, 2, ... for the regions along it.
    bool plan_route(const SeaMap& sea, const SeaPlan& request);
    /// Removes the marks of the last route.
    void clear_route();

    std::vector<std::uint32_t> m_route_cost;   ///< per region: cost so far, valid when stamped
    std::vector<std::uint32_t> m_route_from;   ///< per region: region it was reached from
    std::vector<std::uint32_t> m_route_stamp;  ///< per region: search it was reached in
    std::vector<std::uint32_t> m_route_goal;   ///< per region: search it is a goal in
    std::vector<std::uint32_t> m_route_step;   ///< per region: place on the last route (1 = start), 0 = off it
    std::vector<std::uint32_t> m_route;        ///< regions of the last route, start first
    std::uint32_t m_route_search = 0;
    std::vector<Node> m_route_heap;
};

}  // namespace o1602::sim
