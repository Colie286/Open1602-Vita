// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/land_paths.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>

#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/sea_paths.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint8_t kBlocked = 1;
constexpr std::uint8_t kVisited = 4;
constexpr std::uint8_t kKnown = 8;  // the cell's tile has been looked at
constexpr std::uint32_t kInfinite = 0xFFFFFFFFU;

// Neighbour order of the original's wavefront: the diagonals first, then the straight steps.
constexpr std::uint8_t kOrder[8] = {7, 1, 5, 3, 0, 4, 6, 2};

}  // namespace

bool soldiers_walk_on(const assets::BuildingType& b) noexcept {
    switch (b.kind) {
        case building_kind::kRoad:
        case building_kind::kGate:
        case building_kind::kGround:
        case building_kind::kRuin:
        case building_kind::kSquare:
        case building_kind::kBridge:
        case building_kind::kBeachRuin:
        case building_kind::kPier:
            return true;
        default:
            return false;
    }
}

void LandMap::rebuild(const World& world, const assets::BuildingTable& buildings) {
    m_world = &world;
    m_buildings = &buildings;
}

void LandMap::update_island(const World& world, const assets::BuildingTable& buildings, std::uint16_t /*island*/) {
    rebuild(world, buildings);
}

std::uint16_t LandMap::island_at(std::int32_t x, std::int32_t y) const noexcept {
    if (m_world == nullptr || x < 0 || y < 0 ||
        !m_world->contains(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y))) {
        return World::kSea;
    }
    return m_world->cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).island;
}

const assets::BuildingType* LandMap::building(std::int32_t x, std::int32_t y) const noexcept {
    const std::uint16_t island = island_at(x, y);
    if (island == World::kSea || island >= m_world->islands().size() || m_buildings == nullptr) {
        return nullptr;
    }
    const World::Cell& c = m_world->cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    const IslandMap& map = m_world->islands()[island].map;
    if (c.local_x >= map.width() || c.local_y >= map.height()) {
        return nullptr;
    }
    const MapCell& top = map.top(c.local_x, c.local_y);
    return top.empty() ? nullptr : m_buildings->by_sprite(top.sprite);
}

LandMap::Cell LandMap::cell(std::int32_t x2, std::int32_t y2, std::uint8_t speed_type,
                            std::uint8_t* cost) const noexcept {
    if (x2 < 0 || y2 < 0) {
        return Cell::Off;
    }
    return tile(x2 / 2, y2 / 2, speed_type, cost);
}

LandMap::Cell LandMap::tile(std::int32_t x, std::int32_t y, std::uint8_t speed_type, std::uint8_t* cost,
                            PlayerId walker) const noexcept {
    // island_at() and building() in one look at the world cell.
    if (m_world == nullptr || x < 0 || y < 0 ||
        !m_world->contains(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y))) {
        return Cell::Off;
    }
    const World::Cell& c = m_world->cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (c.island == World::kSea) {
        return Cell::Off;
    }
    if (c.island >= m_world->islands().size() || m_buildings == nullptr) {
        return Cell::Open;
    }
    const IslandMap& map = m_world->islands()[c.island].map;
    if (c.local_x >= map.width() || c.local_y >= map.height()) {
        return Cell::Open;
    }
    const MapCell& top = map.top(c.local_x, c.local_y);
    const assets::BuildingType* b = top.empty() ? nullptr : m_buildings->by_sprite(top.sprite);
    if (b == nullptr) {
        return Cell::Open;
    }
    if (soldiers_walk_on(*b) && !closed_to(*b, top, walker)) {
        if (cost != nullptr) {
            *cost = b->walk_speed[speed_type & 3U];
        }
        return Cell::Walk;
    }
    return b->blocks_shots ? Cell::Wall : Cell::Open;
}

bool LandMap::closed_to(const assets::BuildingType& b, const MapCell& top, PlayerId walker) const noexcept {
    if (b.kind != building_kind::kGate || walker == kNoPlayer || top.owner == walker) {
        return false;
    }
    const Player* owner = m_world->player(top.owner);
    return owner != nullptr && owner->gates_closed;
}

std::int32_t CellRect::distance(std::int32_t x, std::int32_t y) const noexcept {
    const auto [nx, ny] = nearest(x, y);
    return sea_distance(nx - x, ny - y);
}

std::pair<std::int32_t, std::int32_t> CellRect::nearest(std::int32_t x, std::int32_t y) const noexcept {
    return {std::clamp(x, x0, x1), std::clamp(y, y0, y1)};
}

bool line_of_sight(const LandMap& map, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                   const CellRect& ignore) noexcept {
    // Bresenham over the cells between the ends.
    const std::int32_t dx = std::abs(x1 - x0);
    const std::int32_t dy = -std::abs(y1 - y0);
    const std::int32_t sx = x0 < x1 ? 1 : -1;
    const std::int32_t sy = y0 < y1 ? 1 : -1;
    std::int32_t err = dx + dy;
    std::int32_t x = x0;
    std::int32_t y = y0;
    while (x != x1 || y != y1) {
        const std::int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
        if ((x == x1 && y == y1) || ignore.contains(x, y)) {
            continue;
        }
        if (map.cell(x, y, 0) == LandMap::Cell::Wall) {
            return false;
        }
    }
    return true;
}

std::uint8_t direction_to(std::int32_t dx, std::int32_t dy) noexcept {
    const std::int32_t ax = std::abs(dx);
    const std::int32_t ay = std::abs(dy);
    if (ax == 0 && ay == 0) {
        return 0;
    }
    // tan(22.5 degrees) ~ 5/12.
    if (ay * 12 <= ax * 5) {
        return dx > 0 ? 2 : 6;
    }
    if (ax * 12 <= ay * 5) {
        return dy > 0 ? 4 : 0;
    }
    if (dx > 0) {
        return dy > 0 ? 3 : 1;
    }
    return dy > 0 ? 5 : 7;
}

LandCourse LandPlanner::plan(const LandMap& map, const LandPlan& r) {
    LandCourse course;
    if (r.radius <= 0) {
        return course;
    }
    const std::int32_t size = 2 * r.radius + 1;
    const std::int32_t x0 = r.start_x - r.radius + r.shift_x;
    const std::int32_t y0 = r.start_y - r.radius + r.shift_y;
    const std::int32_t x_end = x0 + size;  // exclusive
    const std::int32_t y_end = y0 + size;
    if (r.start_x < x0 || r.start_y < y0 || r.start_x >= x_end || r.start_y >= y_end) {
        return course;
    }
    // The window with a frame of blocked cells around it: a step out of the window meets the
    // frame, so the search needs no bounds test. The tiles are looked at only when the search
    // reaches them (most searches end long before the window is full). Both give the same ways as
    // filling the whole window first (review point 18).
    const std::int32_t stride = size + 2;
    const auto cells = static_cast<std::size_t>(stride) * static_cast<std::size_t>(stride);
    m_cost.assign(cells, kInfinite);
    m_state.assign(cells, kKnown | kBlocked);
    m_speed.resize(cells);
    for (std::int32_t row = 1; row <= size; ++row) {
        const auto first = m_state.begin() + static_cast<std::ptrdiff_t>(row) * stride + 1;
        std::fill(first, first + size, std::uint8_t{0});
    }
    const auto at = [&](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y - y0 + 1) * static_cast<std::size_t>(stride) +
               static_cast<std::size_t>(x - x0 + 1);
    };
    // Looks at the tile of a cell and sets its cells in the window that are not known yet.
    const auto load = [&](std::size_t cell) {
        const std::int32_t x = static_cast<std::int32_t>(cell % static_cast<std::size_t>(stride)) - 1 + x0;
        const std::int32_t y = static_cast<std::int32_t>(cell / static_cast<std::size_t>(stride)) - 1 + y0;
        const std::int32_t tx = (x >= 0 ? x : x - 1) / 2;
        const std::int32_t ty = (y >= 0 ? y : y - 1) / 2;
        std::uint8_t speed = 32;
        const bool walk = ty >= 0 && tx >= 0 && map.tile(tx, ty, r.speed_type, &speed, r.walker) == LandMap::Cell::Walk;
        const std::uint8_t state = walk ? kKnown : static_cast<std::uint8_t>(kKnown | kBlocked);
        for (std::int32_t cy = std::max(2 * ty, y0); cy < std::min(2 * ty + 2, y_end); ++cy) {
            for (std::int32_t cx = std::max(2 * tx, x0); cx < std::min(2 * tx + 2, x_end); ++cx) {
                const std::size_t i = at(cx, cy);
                if ((m_state[i] & kKnown) == 0) {
                    m_state[i] = state;
                    m_speed[i] = speed;
                }
            }
        }
    };
    const auto state_of = [&](std::size_t cell) {
        if ((m_state[cell] & kKnown) == 0) {
            load(cell);
        }
        return m_state[cell];
    };
    const std::size_t start = at(r.start_x, r.start_y);
    load(start);
    for (const auto& [bx, by] : r.blocked) {
        if (bx >= x0 && by >= y0 && bx < x_end && by < y_end) {
            m_state[at(bx, by)] = kKnown | kBlocked;
        }
    }
    m_state[start] = kKnown;  // the soldier's own cell is always open
    if (r.goal && r.goal(r.start_x, r.start_y)) {
        course.reached_goal = true;
        course.end_x = r.start_x;
        course.end_y = r.start_y;
        return course;
    }

    // Index offsets of the eight steps in the framed window.
    std::array<std::ptrdiff_t, 8> offset{};
    for (std::uint8_t dir = 0; dir < 8; ++dir) {
        offset[dir] = static_cast<std::ptrdiff_t>(step_y(dir)) * stride + step_x(dir);
    }
    const auto moved = [](std::size_t cell, std::ptrdiff_t by) {
        return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(cell) + by);
    };
    m_heap.clear();
    std::uint32_t seq = 0;
    const auto later = [](const Node& a, const Node& b) { return a.cost != b.cost ? a.cost > b.cost : a.seq > b.seq; };
    const auto push = [&](std::uint32_t cost, std::size_t cell) {
        m_heap.push_back(Node{cost, seq++, static_cast<std::uint32_t>(cell)});
        std::push_heap(m_heap.begin(), m_heap.end(), later);
    };
    m_cost[start] = 0;
    push(0, start);
    std::optional<std::size_t> goal;
    std::int32_t goal_distance = 0x7FFFFFFF;
    std::optional<std::size_t> edge;
    std::int32_t edge_distance = sea_distance(r.aim_x - r.start_x, r.aim_y - r.start_y);
    // Only an aim beyond the window is approached over its edge; one inside it that cannot be
    // reached gives no way at all, as at sea.
    const bool aim_outside = r.aim_x < x0 || r.aim_y < y0 || r.aim_x >= x_end || r.aim_y >= y_end;
    while (!m_heap.empty()) {
        std::pop_heap(m_heap.begin(), m_heap.end(), later);
        const Node n = m_heap.back();
        m_heap.pop_back();
        if ((m_state[n.cell] & kVisited) != 0) {
            continue;
        }
        m_state[n.cell] |= kVisited;
        const auto cx = static_cast<std::int32_t>(n.cell % static_cast<std::size_t>(stride)) - 1 + x0;
        const auto cy = static_cast<std::int32_t>(n.cell / static_cast<std::size_t>(stride)) - 1 + y0;
        if (n.cell != start && r.goal && r.goal(cx, cy)) {
            const std::int32_t d = sea_distance(r.aim_x - cx, r.aim_y - cy);
            if (r.first_goal) {
                goal = n.cell;
                break;
            }
            if (d < goal_distance) {
                goal_distance = d;
                goal = n.cell;
            }
        }
        if (aim_outside && (cx == x0 || cy == y0 || cx == x_end - 1 || cy == y_end - 1)) {
            const std::int32_t d = sea_distance(r.aim_x - cx, r.aim_y - cy);
            if (d < edge_distance) {
                edge_distance = d;
                edge = n.cell;
            }
        }
        const std::uint8_t speed = m_speed[n.cell];
        for (const std::uint8_t dir : kOrder) {
            const std::size_t nc = moved(n.cell, offset[dir]);
            if ((state_of(nc) & (kBlocked | kVisited)) != 0) {
                continue;
            }
            const bool diagonal = (dir & 1U) != 0;
            if (diagonal &&
                ((state_of(moved(n.cell, step_x(dir))) & kBlocked) != 0 ||
                 (state_of(moved(n.cell, static_cast<std::ptrdiff_t>(step_y(dir)) * stride)) & kBlocked) != 0)) {
                continue;  // no cut corners
            }
            const std::uint32_t cost = n.cost + (diagonal ? diagonal_cost(speed) : straight_cost(speed));
            if (cost < m_cost[nc]) {
                m_cost[nc] = cost;
                m_state[nc] = static_cast<std::uint8_t>((m_state[nc] & 0x0FU) | ((dir + 1U) << 4U));
                push(cost, nc);
            }
        }
    }
    const std::optional<std::size_t> end = goal ? goal : edge;
    if (!end) {
        return course;
    }
    course.reached_goal = goal.has_value();
    m_dirs.clear();
    std::size_t cell = *end;
    while (cell != start) {
        const auto from = static_cast<std::uint8_t>(m_state[cell] >> 4U);
        if (from == 0) {
            return LandCourse{};
        }
        const auto dir = static_cast<std::uint8_t>(from - 1U);
        m_dirs.push_back(dir);
        cell = moved(cell, -offset[dir]);
    }
    std::reverse(m_dirs.begin(), m_dirs.end());
    std::int32_t x = r.start_x;
    std::int32_t y = r.start_y;
    std::size_t i = 0;
    const std::uint8_t max_run = std::clamp<std::uint8_t>(r.max_run, 1, 15);
    while (i < m_dirs.size() && course.path.size() < kMaxRuns) {
        const std::uint8_t dir = m_dirs[i];
        const std::uint8_t speed = m_speed[at(x, y)];
        std::uint8_t n = 0;
        // A run: same direction and walking cost, at most Maxstepcnt cells (0x0046CF70).
        while (i < m_dirs.size() && m_dirs[i] == dir && n < max_run && m_speed[at(x, y)] == speed) {
            x += step_x(dir);
            y += step_y(dir);
            ++n;
            ++i;
        }
        course.path.push_back(path_code(dir, n));
    }
    if (i < m_dirs.size()) {
        course.reached_goal = false;
    }
    course.end_x = x;
    course.end_y = y;
    return course;
}

}  // namespace o1602::sim
