// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paths of carriers, market carts and workers on an island (docs/re/logistics.md).
//
// Like the original, a search never covers the whole island: it runs in the catchment of the
// figure's home building, the circle of the building's Radius around its centre tile
// (0x00471280), so a figure never leaves it. Each tile of that window is blocked, walkable or a
// goal; walking off a tile costs 2 * Wegspeed straight and 91/32 * Wegspeed diagonally (64 and
// 91 on a road, 0x0046F8A0), corners are never cut, and goal tiles (the buildings a figure walks
// to) are entered but not crossed.
//
// The original runs the search in rounds of 64 cost units and assigns each tile to the neighbour
// that discovers it first, which is close to but not always the cheapest route. Open1602 runs an
// exact Dijkstra on the same integer costs with a fixed neighbour order and first-in-first-out
// ties: the result is deterministic on every computer (ADR 0004) and never worse than the
// original's.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/island_map.hpp"

namespace o1602::sim {

/// A tile of an island.
struct TilePos {
    std::int32_t x = 0;
    std::int32_t y = 0;
    friend constexpr bool operator==(const TilePos&, const TilePos&) = default;
};

/// Directions as the original numbers them (figure +0x03): 0 = -y, 1 = +x-y, 2 = +x, 3 = +x+y,
/// 4 = +y, 5 = -x+y, 6 = -x, 7 = -x-y (island tile axes).
[[nodiscard]] std::uint8_t direction_between(TilePos from, TilePos to) noexcept;

/// Half widths of the integer circle of radius `radius` for the row offsets 0..radius, built with
/// the midpoint circle algorithm like the original's table at 0x005B7460 (0x00404D70).
/// Radius 10: 10, 10, 10, 10, 9, 9, 8, 7, 6, 5, 3.
[[nodiscard]] std::vector<std::uint8_t> circle_half_widths(std::uint32_t radius);
/// The same for radius 0..255 from a table built once: no allocation in the tick.
[[nodiscard]] const std::vector<std::uint8_t>& circle_table(std::uint8_t radius);

/// The circle a building's figures stay in: Radius tiles around its centre tile. Buildings of even
/// width or depth have no centre tile; their circle is one tile wider in that direction.
struct Catchment {
    TilePos centre;  ///< origin + (rotated size - 1) / 2
    std::uint8_t radius = 0;
    std::uint8_t parity_x = 0;  ///< 1 for an even (rotated) width
    std::uint8_t parity_y = 0;

    /// Window around the circle (island tiles, may exceed the island).
    [[nodiscard]] TilePos min() const noexcept { return {centre.x - radius, centre.y - radius}; }
    [[nodiscard]] std::uint32_t width() const noexcept { return 2U * radius + 1U + parity_x; }
    [[nodiscard]] std::uint32_t height() const noexcept { return 2U * radius + 1U + parity_y; }
};

/// Catchment of a building placed at `origin` with `rotation`.
[[nodiscard]] Catchment catchment_of(const assets::BuildingType& building, std::uint8_t rotation,
                                     TilePos origin) noexcept;

/// Whether a building's circle reaches out over the water: a fisher's hut, whose boat fishes
/// there, and towers, which shoot at ships. Its circle is shown lit on the sea as well.
[[nodiscard]] bool catchment_covers_water(const assets::BuildingType& building) noexcept;

/// What figures walk on besides the buildings they enter (grid builders 0x004706E0, 0x004704D0):
/// - Roads: roads, squares, bridges, piers and the centre of gates (market carts);
/// - Land: also open ground and ruins (carriers, farmers), at their slower Wegspeed;
/// - Water: the sea, surf and river mouths (fishing boats, 0x0046FB50), also beyond the island.
enum class WalkClass : std::uint8_t { Roads, Land, Water };

/// HAUS Kind values under the name the walking code, the renderer and the interface use.
namespace building_kind = kind;

/// Whether a figure of `walk` class may walk on this tile (without a building it enters).
[[nodiscard]] bool walkable(const assets::BuildingType& building, const MapCell& cell, WalkClass walk) noexcept;

/// Search state for one figure; keep one and reuse it, it allocates only while it grows.
class PathFinder {
public:
    enum class Cell : std::uint8_t {
        Blocked,
        Walkable,
        Goal,  ///< can be the end of the path; entered, never crossed
    };

    /// Prepares a search in `area` of `map`: every tile inside the circle that `walk` allows
    /// becomes walkable with the Wegspeed of its building for `speed_type`, everything else
    /// blocked. Goals are added with set().
    /// Without `circle` the whole square window is used (the house neighbourhood search).
    void begin(const IslandMap& map, const assets::BuildingTable& buildings, const Catchment& area, WalkClass walk,
               std::uint8_t speed_type, bool circle = true);

    /// Makes the footprint of a building walkable (the one a figure stands in, or the one it
    /// returns to), as 0x004710B0 does: cost class 40 (125 %). Of a building drawn over its ground
    /// (`Grundflg`) only the tiles whose ground is forest, open ground or ruins.
    void open_footprint(TilePos origin, std::uint32_t width, std::uint32_t height);

    /// Whether `p` lies in the circle of the area.
    [[nodiscard]] bool inside(TilePos p) const noexcept;
    [[nodiscard]] Cell cell(TilePos p) const noexcept;
    /// Window of the area (island tiles; the circle lies inside).
    [[nodiscard]] TilePos window_min() const noexcept { return {m_x0, m_y0}; }
    [[nodiscard]] std::uint32_t window_width() const noexcept { return m_width; }
    [[nodiscard]] std::uint32_t window_height() const noexcept { return m_height; }
    /// Changes a tile inside the circle (ignored outside); `cost` is its Wegspeed class.
    void set(TilePos p, Cell cell, std::uint8_t cost);
    void set(TilePos p, Cell cell);

    /// What the goal callback answers for a goal tile, which is reached in cost order.
    enum class Answer : std::uint8_t {
        Reject,    ///< not this one, go on
        Accept,    ///< take it, stop
        Remember,  ///< a candidate; go on (the caller keeps the best)
        Cross,     ///< not the end, but walk on through it (houses in the neighbourhood search)
    };

    /// Searches from `start` (made walkable if it is not); `goal(TilePos)` is asked once per goal
    /// tile in the order they are reached. Returns the accepted tile; after a search without
    /// acceptance the caller can still build a path to a remembered tile.
    template <class Goal>
    std::optional<TilePos> search(TilePos start, Goal&& goal) {
        if (!start_search(start)) {
            return std::nullopt;
        }
        std::uint32_t index = 0;
        while (pop(index)) {
            const TilePos p = position(index);
            if (m_cells[index] == Cell::Goal) {
                const Answer a = goal(p);
                if (a == Answer::Accept) {
                    return p;
                }
                if (a == Answer::Cross) {
                    expand(index);
                }
                continue;  // goal tiles are not crossed otherwise
            }
            expand(index);
        }
        return std::nullopt;
    }

    /// Cost of the best route to `target` found by the last search (64 = one road tile), nothing if
    /// it was not reached.
    [[nodiscard]] std::optional<std::uint32_t> cost(TilePos target) const noexcept;

    /// Whether the last search reached `target`.
    [[nodiscard]] bool reached(TilePos target) const noexcept;

    /// The tiles from the start (excluded) to `target` (included) of the last search; false if it
    /// was not reached.
    bool path_to(TilePos target, std::vector<TilePos>& out) const;

private:
    static constexpr std::uint32_t kUnreached = 0xFFFFFFFFU;

    struct Entry {
        std::uint32_t cost;
        std::uint32_t order;  ///< insertion counter: equal costs leave in insertion order
        std::uint32_t index;
    };

    [[nodiscard]] TilePos position(std::uint32_t index) const noexcept {
        return {m_x0 + static_cast<std::int32_t>(index % m_width), m_y0 + static_cast<std::int32_t>(index / m_width)};
    }
    [[nodiscard]] std::optional<std::uint32_t> index_of(TilePos p) const noexcept;
    bool start_search(TilePos start);
    bool pop(std::uint32_t& index);
    void push(std::uint32_t index, std::uint32_t cost, std::uint8_t from);
    void expand(std::uint32_t index);

    const IslandMap* m_map = nullptr;
    const assets::BuildingTable* m_buildings = nullptr;
    std::int32_t m_x0 = 0;
    std::int32_t m_y0 = 0;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::vector<Cell> m_cells;
    std::vector<std::uint8_t> m_half;   ///< scratch of begin(): half widths of the window
    std::vector<bool> m_circle;         ///< tile lies in the circle and on the island
    std::vector<std::uint8_t> m_cost;   ///< Wegspeed class of the tile (32 = road)
    std::vector<std::uint32_t> m_dist;  ///< cost of the best route found so far
    std::vector<std::uint8_t> m_from;   ///< direction + 1 the tile was reached with, 0 = start/none
    std::vector<bool> m_done;
    std::vector<Entry> m_heap;
    std::uint32_t m_order = 0;
    std::optional<std::uint32_t> m_start;
};

/// Cost of a step on a road or the open sea, the original's ORTH[32] and DIAG[32]: straight and
/// diagonal. The land search, the sea search and the deer planner all count in these units.
inline constexpr std::uint32_t kStepStraight = 64;
inline constexpr std::uint32_t kStepDiagonal = 91;
/// Wegspeed[3]: the same cost everywhere (fire, plague and the houses' market search).
inline constexpr std::uint8_t kUniformSpeed = 3;

/// Cost of leaving a tile of Wegspeed class `cost` (0x0046F8A0): straight 2 * cost, diagonal
/// 91 * cost / 32, never less than a road (64 / 91).
[[nodiscard]] constexpr std::uint32_t straight_cost(std::uint8_t cost) noexcept {
    return cost < 32 ? kStepStraight : 2U * cost;
}
[[nodiscard]] constexpr std::uint32_t diagonal_cost(std::uint8_t cost) noexcept {
    return cost < 32 ? kStepDiagonal : kStepDiagonal * cost / 32U;
}

}  // namespace o1602::sim
