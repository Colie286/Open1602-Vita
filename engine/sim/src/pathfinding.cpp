// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/pathfinding.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace o1602::sim {

namespace {

struct Step {
    std::int8_t dx;
    std::int8_t dy;
    std::uint8_t direction;
};

// Neighbour order of the original search: the diagonals first, then the straight steps.
constexpr std::array<Step, 8> kSteps = {{
    {-1, -1, 7},
    {1, -1, 1},
    {-1, 1, 5},
    {1, 1, 3},
    {0, -1, 0},
    {0, 1, 4},
    {-1, 0, 6},
    {1, 0, 2},
}};

constexpr std::array<Step, 8> kByDirection = {{
    {0, -1, 0},
    {1, -1, 1},
    {1, 0, 2},
    {1, 1, 3},
    {0, 1, 4},
    {-1, 1, 5},
    {-1, 0, 6},
    {-1, -1, 7},
}};

/// Footprint tiles of a building that a figure stands in or walks to: Wegspeed class 40.
constexpr std::uint8_t kFootprintCost = 40;

bool heap_after(const auto& a, const auto& b) noexcept {
    return a.cost != b.cost ? a.cost > b.cost : a.order > b.order;
}

}  // namespace

std::uint8_t direction_between(TilePos from, TilePos to) noexcept {
    const int dx = (to.x > from.x) - (to.x < from.x);
    const int dy = (to.y > from.y) - (to.y < from.y);
    for (const Step& s : kByDirection) {
        if (s.dx == dx && s.dy == dy) {
            return s.direction;
        }
    }
    return 0;
}

std::vector<std::uint8_t> circle_half_widths(std::uint32_t radius) {
    std::vector<std::uint8_t> half(std::size_t{radius} + 1, 0);
    std::int32_t x = 0;
    auto y = static_cast<std::int32_t>(radius);
    std::int32_t d = 1 - y;
    while (x <= y) {
        // Point (x, y) of the first octant and its mirror (y, x): row offset -> widest column.
        half[static_cast<std::size_t>(y)] = std::max(half[static_cast<std::size_t>(y)], static_cast<std::uint8_t>(x));
        half[static_cast<std::size_t>(x)] = std::max(half[static_cast<std::size_t>(x)], static_cast<std::uint8_t>(y));
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            --y;
        }
        ++x;
    }
    if (radius == 1) {
        half[1] = 1;  // the original's table (0x00404D70) has the corners at radius 1
    }
    return half;
}

const std::vector<std::uint8_t>& circle_table(std::uint8_t radius) {
    static const std::array<std::vector<std::uint8_t>, 256> kTables = [] {
        std::array<std::vector<std::uint8_t>, 256> t;
        for (std::uint32_t r = 0; r < t.size(); ++r) {
            t[r] = circle_half_widths(r);
        }
        return t;
    }();
    return kTables[radius];
}

Catchment catchment_of(const assets::BuildingType& building, std::uint8_t rotation, TilePos origin) noexcept {
    std::uint32_t w = 1;
    std::uint32_t h = 1;
    rotated_size(building, rotation, w, h);
    Catchment c;
    c.centre = {origin.x + static_cast<std::int32_t>((w - 1) / 2), origin.y + static_cast<std::int32_t>((h - 1) / 2)};
    c.radius = building.radius;
    c.parity_x = static_cast<std::uint8_t>((w - 1) & 1U);
    c.parity_y = static_cast<std::uint8_t>((h - 1) & 1U);
    return c;
}

bool catchment_covers_water(const assets::BuildingType& building) noexcept {
    return building.prod_kind == prod::kFisher || building.prod_kind == prod::kTower ||
           building.kind == building_kind::kTower || building.kind == building_kind::kTowerBeach;
}

bool walkable(const assets::BuildingType& building, const MapCell& cell, WalkClass walk) noexcept {
    using namespace building_kind;
    if (walk == WalkClass::Water) {
        return building.kind == kSea || building.kind == kSurf || building.kind == kSurfCorner ||
               building.kind == kRiverMouth;
    }
    switch (building.kind) {
        case kRoad:
        case kSquare:
        case kBridge:
        case kPier:
            return true;
        case kGate:
            // Only the middle of a gate is a passage.
            return cell.sprite >= building.gfx && cell.sprite - building.gfx == building.tile_count() / 2;
        case kGround:
        case kRuin:
        case kBeachRuin:
            return walk == WalkClass::Land;
        default:
            return false;
    }
}

void PathFinder::begin(const IslandMap& map, const assets::BuildingTable& buildings, const Catchment& area,
                       WalkClass walk, std::uint8_t speed_type, bool circle) {
    m_map = &map;
    m_buildings = &buildings;
    const TilePos min = area.min();
    m_x0 = min.x;
    m_y0 = min.y;
    m_width = area.width();
    m_height = area.height();
    const std::size_t n = std::size_t{m_width} * m_height;
    m_cells.assign(n, Cell::Blocked);
    m_circle.assign(n, false);
    m_cost.assign(n, 32);
    m_dist.assign(n, kUnreached);
    m_from.assign(n, 0);
    m_done.assign(n, false);
    m_heap.clear();
    m_order = 0;
    m_start.reset();

    // The original clips to the circle only from radius 2 on (0x00471280); below the window is
    // a square.
    std::vector<std::uint8_t>& half = m_half;
    half = circle_table(area.radius);
    if (area.radius <= 1 || !circle) {
        std::fill(half.begin(), half.end(), area.radius);
    }
    const std::uint8_t type = std::min<std::uint8_t>(speed_type, 3);
    for (std::uint32_t j = 0; j < m_height; ++j) {
        const std::int32_t y = m_y0 + static_cast<std::int32_t>(j);
        // Beyond the island is open sea: only boats go there.
        const bool on_rows = y >= 0 && static_cast<std::uint32_t>(y) < map.height();
        if (!on_rows && walk != WalkClass::Water) {
            continue;
        }
        // Row offset from the centre; an even depth doubles the centre row.
        std::int32_t dy = y - area.centre.y;
        if (dy > 0) {
            dy -= area.parity_y;
        }
        if (static_cast<std::uint32_t>(std::abs(dy)) > area.radius) {
            continue;
        }
        const std::int32_t hw = half[static_cast<std::size_t>(std::abs(dy))];
        for (std::uint32_t i = 0; i < m_width; ++i) {
            const std::int32_t x = m_x0 + static_cast<std::int32_t>(i);
            const bool on_map = x >= 0 && static_cast<std::uint32_t>(x) < map.width() && on_rows;
            if (!on_map && walk != WalkClass::Water) {
                continue;
            }
            std::int32_t dx = x - area.centre.x;
            if (dx > 0) {
                dx -= area.parity_x;
            }
            if (std::abs(dx) > hw) {
                continue;
            }
            const std::size_t k = std::size_t{j} * m_width + i;
            m_circle[k] = true;
            if (!on_map) {
                m_cells[k] = Cell::Walkable;
                continue;
            }
            const MapCell& cell = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
            if (b == nullptr || !walkable(*b, cell, walk)) {
                continue;
            }
            m_cells[k] = Cell::Walkable;
            m_cost[k] = b->walk_speed[type];
        }
    }
}

std::optional<std::uint32_t> PathFinder::index_of(TilePos p) const noexcept {
    const std::int32_t i = p.x - m_x0;
    const std::int32_t j = p.y - m_y0;
    if (i < 0 || j < 0 || static_cast<std::uint32_t>(i) >= m_width || static_cast<std::uint32_t>(j) >= m_height) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(j) * m_width + static_cast<std::uint32_t>(i);
}

bool PathFinder::inside(TilePos p) const noexcept {
    const auto k = index_of(p);
    return k && m_circle[*k];
}

PathFinder::Cell PathFinder::cell(TilePos p) const noexcept {
    const auto k = index_of(p);
    return k ? m_cells[*k] : Cell::Blocked;
}

void PathFinder::set(TilePos p, Cell cell, std::uint8_t cost) {
    if (const auto k = index_of(p); k && m_circle[*k]) {
        m_cells[*k] = cell;
        m_cost[*k] = cost;
    }
}

void PathFinder::set(TilePos p, Cell cell) {
    if (const auto k = index_of(p); k && m_circle[*k]) {
        m_cells[*k] = cell;
    }
}

void PathFinder::open_footprint(TilePos origin, std::uint32_t width, std::uint32_t height) {
    // 0x004710B0: a building with Grundflg opens only the tiles whose ground (the second layer) is
    // forest, open ground or ruins.
    const assets::BuildingType* building = nullptr;
    if (m_map != nullptr && m_buildings != nullptr && m_map->contains(origin.x, origin.y)) {
        const MapCell& c = m_map->top(static_cast<std::uint32_t>(origin.x), static_cast<std::uint32_t>(origin.y));
        building = c.empty() ? nullptr : m_buildings->by_sprite(c.sprite);
    }
    const bool ground_only = building != nullptr && building->draw_ground;
    for (std::uint32_t j = 0; j < height; ++j) {
        for (std::uint32_t i = 0; i < width; ++i) {
            const TilePos p{origin.x + static_cast<std::int32_t>(i), origin.y + static_cast<std::int32_t>(j)};
            if (ground_only && m_map->contains(p.x, p.y)) {
                const MapCell& g = m_map->ground(static_cast<std::uint32_t>(p.x), static_cast<std::uint32_t>(p.y));
                const assets::BuildingType* under = g.empty() ? nullptr : m_buildings->by_sprite(g.sprite);
                if (under == nullptr || under->kind < building_kind::kForest || under->kind > building_kind::kRuin) {
                    continue;
                }
            }
            set(p, Cell::Walkable, kFootprintCost);
        }
    }
}

bool PathFinder::start_search(TilePos start) {
    const auto k = index_of(start);
    if (!k || !m_circle[*k]) {
        return false;
    }
    if (m_cells[*k] == Cell::Blocked) {
        m_cells[*k] = Cell::Walkable;  // a figure can always leave the tile it stands on
    }
    std::fill(m_dist.begin(), m_dist.end(), kUnreached);
    std::fill(m_from.begin(), m_from.end(), std::uint8_t{0});
    std::fill(m_done.begin(), m_done.end(), false);
    m_heap.clear();
    m_order = 0;
    m_start = k;
    m_dist[*k] = 0;
    // The start is crossed even if it is a goal tile (a figure leaving a building).
    m_done[*k] = true;
    expand(*k);
    return true;
}

bool PathFinder::pop(std::uint32_t& index) {
    while (!m_heap.empty()) {
        std::pop_heap(m_heap.begin(), m_heap.end(), [](const Entry& a, const Entry& b) { return heap_after(a, b); });
        const Entry e = m_heap.back();
        m_heap.pop_back();
        if (m_done[e.index] || e.cost != m_dist[e.index]) {
            continue;  // stale entry
        }
        m_done[e.index] = true;
        index = e.index;
        return true;
    }
    return false;
}

void PathFinder::push(std::uint32_t index, std::uint32_t cost, std::uint8_t from) {
    if (m_done[index] || cost >= m_dist[index]) {
        return;
    }
    m_dist[index] = cost;
    m_from[index] = static_cast<std::uint8_t>(from + 1);
    m_heap.push_back(Entry{cost, m_order++, index});
    std::push_heap(m_heap.begin(), m_heap.end(), [](const Entry& a, const Entry& b) { return heap_after(a, b); });
}

void PathFinder::expand(std::uint32_t index) {
    const TilePos p = position(index);
    const std::uint32_t base = m_dist[index];
    const std::uint8_t cost = m_cost[index];
    for (const Step& s : kSteps) {
        const auto k = index_of({p.x + s.dx, p.y + s.dy});
        if (!k || m_cells[*k] == Cell::Blocked) {
            continue;
        }
        const bool diagonal = s.dx != 0 && s.dy != 0;
        if (diagonal) {
            // No corner cutting: both straight neighbours on the way must be walkable.
            const auto a = index_of({p.x + s.dx, p.y});
            const auto b = index_of({p.x, p.y + s.dy});
            if (!a || !b || m_cells[*a] != Cell::Walkable || m_cells[*b] != Cell::Walkable) {
                continue;
            }
        }
        push(*k, base + (diagonal ? diagonal_cost(cost) : straight_cost(cost)), s.direction);
    }
}

std::optional<std::uint32_t> PathFinder::cost(TilePos target) const noexcept {
    const auto k = index_of(target);
    if (!k || m_dist[*k] == kUnreached) {
        return std::nullopt;
    }
    return m_dist[*k];
}

bool PathFinder::reached(TilePos target) const noexcept {
    const auto k = index_of(target);
    return k && m_dist[*k] != kUnreached;
}

bool PathFinder::path_to(TilePos target, std::vector<TilePos>& out) const {
    out.clear();
    auto k = index_of(target);
    if (!k || m_dist[*k] == kUnreached || !m_start) {
        return false;
    }
    TilePos p = target;
    while (*k != *m_start) {
        out.push_back(p);
        const std::uint8_t from = m_from[*k];
        if (from == 0 || out.size() > m_cells.size()) {
            out.clear();
            return false;
        }
        const Step& s = kByDirection[from - 1U];
        p = {p.x - s.dx, p.y - s.dy};
        k = index_of(p);
        if (!k) {
            out.clear();
            return false;
        }
    }
    std::reverse(out.begin(), out.end());
    return true;
}

}  // namespace o1602::sim
