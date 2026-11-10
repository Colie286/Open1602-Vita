// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ways of soldiers on land (docs/re/military.md, "Movement").
//
// Soldiers walk on a grid of half tiles: every island tile is 2 x 2 cells with the walking cost of
// its building. They walk only on roads, gates, open ground, ruins, squares, bridges and piers, never
// through forest, fields or buildings (0x0046F460). Unlike carriers they have no catchment circle:
// the original plans in a square window around the soldier (0x004581F0), with a goal that depends on
// the target: cells from which a hostile target is in range and in sight, or the target itself and
// the cells next to it. A target beyond the window is reached in legs through the window edge.
//
// Open1602 runs an exact Dijkstra on the same integer costs with a fixed neighbour order, as for
// ships and carriers (ADR 0004).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// What soldiers find on the world's half-tile cells, kept up to date with building.
class LandMap {
public:
    enum class Cell : std::uint8_t {
        Off,   ///< sea or outside the world
        Walk,  ///< soldiers walk here
        Open,  ///< blocked for walking, shots pass (fields, beach, water inside an island)
        Wall,  ///< blocked, and blocks shots (buildings, walls, trees, rocks: NoShotFlg)
    };

    /// The map reads the islands' tiles directly, so it cannot go stale when a tile changes without
    /// a construction event (houses falling to ruins, fire, lava): what a soldier sees is always
    /// what a freshly loaded game sees (ADR 0004).
    void rebuild(const World& world, const assets::BuildingTable& buildings);
    /// Kept for the callers that announce a changed island; the map holds nothing to update.
    void update_island(const World& world, const assets::BuildingTable& buildings, std::uint16_t island);

    /// Cell kind at world half tile (x2, y2); `cost` gets the Wegspeed of the tile for `speed_type`.
    [[nodiscard]] Cell cell(std::int32_t x2, std::int32_t y2, std::uint8_t speed_type,
                            std::uint8_t* cost = nullptr) const noexcept;
    /// The same for world tile (x, y): the kind of all four of its cells. A gate of a player who
    /// closed his gates (Player::gates_closed) is no way for the figures of `walker` when it is
    /// another player.
    [[nodiscard]] Cell tile(std::int32_t x, std::int32_t y, std::uint8_t speed_type, std::uint8_t* cost = nullptr,
                            PlayerId walker = kNoPlayer) const noexcept;
    /// Building on top of world tile (x, y), nullptr for sea, empty tiles or outside.
    [[nodiscard]] const assets::BuildingType* building(std::int32_t x, std::int32_t y) const noexcept;
    /// Island index at world tile (x, y), World::kSea for none.
    [[nodiscard]] std::uint16_t island_at(std::int32_t x, std::int32_t y) const noexcept;

private:
    [[nodiscard]] bool closed_to(const assets::BuildingType& b, const MapCell& top, PlayerId walker) const noexcept;

    const World* m_world = nullptr;
    const assets::BuildingTable* m_buildings = nullptr;
};

/// HAUS kinds soldiers walk on (0x0046F460).
[[nodiscard]] bool soldiers_walk_on(const assets::BuildingType& building) noexcept;

/// A rectangle of half-tile cells (world coordinates), inclusive.
struct CellRect {
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;

    /// The original's distance from a cell to the nearest cell of the rectangle.
    [[nodiscard]] std::int32_t distance(std::int32_t x, std::int32_t y) const noexcept;
    [[nodiscard]] bool contains(std::int32_t x, std::int32_t y) const noexcept {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1;
    }
    /// Nearest cell of the rectangle to (x, y).
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> nearest(std::int32_t x, std::int32_t y) const noexcept;
};

/// Whether nothing that blocks shots lies between two cells (the line-of-fire grid of 0x00453E50):
/// the cells on the line, without its ends, inside `ignore` excepted.
[[nodiscard]] bool line_of_sight(const LandMap& map, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                                 const CellRect& ignore) noexcept;

/// Direction 0..7 from a cell towards another (0 = -y, 2 = +x, ...), as 0x00454050.
[[nodiscard]] std::uint8_t direction_to(std::int32_t dx, std::int32_t dy) noexcept;

struct LandPlan {
    std::int32_t start_x = 0;  ///< half-tile cell of the soldier
    std::int32_t start_y = 0;
    std::int32_t radius = 45;  ///< half width of the window, cells
    std::int32_t shift_x = 0;  ///< window shift after a failure
    std::int32_t shift_y = 0;
    std::uint8_t speed_type = 0;
    std::uint8_t max_run = 1;                                    ///< cells per run (Maxstepcnt)
    PlayerId walker = kNoPlayer;                                 ///< owner of the soldier (closed gates)
    std::vector<std::pair<std::int32_t, std::int32_t>> blocked;  ///< cells of other figures nearby
    /// Goal test; with `first_goal` the first goal reached wins (attacks), otherwise the reached goal
    /// closest to the aim point (moving to an object).
    std::function<bool(std::int32_t, std::int32_t)> goal;
    bool first_goal = false;
    std::int32_t aim_x = 0;  ///< where the soldier wants to be: the target, for the window edge
    std::int32_t aim_y = 0;
};

struct LandCourse {
    std::vector<std::uint8_t> path;  ///< runs as path codes (sea_paths.hpp)
    bool reached_goal = false;       ///< false: a leg to the window edge
    std::int32_t end_x = 0;
    std::int32_t end_y = 0;
};

class LandPlanner {
public:
    static constexpr std::size_t kMaxRuns = 128;

    /// Plans a way; an empty path without `reached_goal` means no way (or already there).
    [[nodiscard]] LandCourse plan(const LandMap& map, const LandPlan& request);

private:
    struct Node {
        std::uint32_t cost;
        std::uint32_t seq;
        std::uint32_t cell;
    };
    std::vector<std::uint32_t> m_cost;
    std::vector<std::uint8_t> m_state;
    std::vector<std::uint8_t> m_speed;
    std::vector<Node> m_heap;
    std::vector<std::uint8_t> m_dirs;  ///< scratch: the steps of the way found, back to front
};

}  // namespace o1602::sim
