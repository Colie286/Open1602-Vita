// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/sea_paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>

#include "o1602/sim/pathfinding.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint8_t kBlocked = 1;
constexpr std::uint8_t kVisited = 4;
constexpr std::uint32_t kStraight = kStepStraight;  // open sea (0x0046F8A0 class 0)
constexpr std::uint32_t kDiagonal = kStepDiagonal;
constexpr std::uint32_t kSoftStraight = 500;  // around a ship lying still (cost class 0x7E)
constexpr std::uint32_t kSoftDiagonal = 711;
constexpr std::uint32_t kDangerStraight = 504;  // near hostile towers (cost class 0x7F)
constexpr std::uint32_t kDangerDiagonal = 716;
constexpr std::uint8_t kClassSoft = 1;
constexpr std::uint8_t kClassDanger = 2;
constexpr std::uint32_t kInfinite = 0xFFFFFFFFU;

// Neighbour order of the original's wavefront: the diagonals NW, NE, SW, SE first, then N, S, W, E.
constexpr std::uint8_t kOrder[8] = {7, 1, 5, 3, 0, 4, 6, 2};

}  // namespace

bool sailable_building(const assets::BuildingType& building) noexcept {
    return building.kind == building_kind::kSea;
}

void SeaMap::rebuild(const World& world, const assets::BuildingTable& buildings) {
    m_width = world.width();
    m_height = world.height();
    m_cells.assign(std::size_t{m_width} * m_height, 0);
    for (std::uint32_t y = 0; y < m_height; ++y) {
        for (std::uint32_t x = 0; x < m_width; ++x) {
            m_cells[std::size_t{y} * m_width + x] = world.cell(x, y).island == World::kSea ? 1 : 0;
        }
    }
    for (std::size_t i = 0; i < world.islands().size(); ++i) {
        set_island_cells(world, buildings, static_cast<std::uint16_t>(i), 0, 0,
                         std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max());
    }
    m_blocks_x = (m_width + kBlock - 1) / kBlock;
    m_blocks_y = (m_height + kBlock - 1) / kBlock;
    m_region.assign(m_cells.size(), 0);
    m_region_count.assign(std::size_t{m_blocks_x} * m_blocks_y, 0);
    m_region_first.assign(m_region_count.size(), 0);
    m_region_block.clear();
    if (m_blocks_x != 0 && m_blocks_y != 0) {
        label_blocks(0, 0, m_blocks_x - 1, m_blocks_y - 1);
    }
}

void SeaMap::update_island(const World& world, const assets::BuildingTable& buildings, std::uint16_t index) {
    update_island(world, buildings, index, 0, 0, std::numeric_limits<std::int32_t>::max(),
                  std::numeric_limits<std::int32_t>::max());
}

void SeaMap::update_island(const World& world, const assets::BuildingTable& buildings, std::uint16_t index,
                           std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    if (index >= world.islands().size()) {
        return;
    }
    set_island_cells(world, buildings, index, x0, y0, x1, y1);
    // The blocks the changed tiles lie in get new regions.
    const WorldIsland& island = world.islands()[index];
    const std::int64_t wx0 = std::int64_t{island.x} + std::max(x0, 0);
    const std::int64_t wy0 = std::int64_t{island.y} + std::max(y0, 0);
    const std::int64_t wx1 = std::int64_t{island.x} + std::min<std::int64_t>(x1, std::int64_t{island.map.width()} - 1);
    const std::int64_t wy1 = std::int64_t{island.y} + std::min<std::int64_t>(y1, std::int64_t{island.map.height()} - 1);
    if (wx1 < wx0 || wy1 < wy0 || wx0 >= m_width || wy0 >= m_height) {
        return;
    }
    const auto bx1 = static_cast<std::uint32_t>(std::min<std::int64_t>(wx1, m_width - 1) / kBlock);
    const auto by1 = static_cast<std::uint32_t>(std::min<std::int64_t>(wy1, m_height - 1) / kBlock);
    label_blocks(static_cast<std::uint32_t>(wx0 / kBlock), static_cast<std::uint32_t>(wy0 / kBlock), bx1, by1);
}

void SeaMap::set_island_cells(const World& world, const assets::BuildingTable& buildings, std::uint16_t index,
                              std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    const WorldIsland& island = world.islands()[index];
    const IslandMap& map = island.map;
    const auto from_x = static_cast<std::uint32_t>(std::max(x0, 0));
    const auto from_y = static_cast<std::uint32_t>(std::max(y0, 0));
    const std::uint32_t to_x = x1 < 0 ? 0 : std::min(map.width(), static_cast<std::uint32_t>(x1) + 1);
    const std::uint32_t to_y = y1 < 0 ? 0 : std::min(map.height(), static_cast<std::uint32_t>(y1) + 1);
    for (std::uint32_t ly = from_y; ly < to_y; ++ly) {
        for (std::uint32_t lx = from_x; lx < to_x; ++lx) {
            const std::uint32_t wx = island.x + lx;
            const std::uint32_t wy = island.y + ly;
            if (wx >= m_width || wy >= m_height) {
                continue;
            }
            const MapCell& c = map.top(lx, ly);
            const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
            m_cells[std::size_t{wy} * m_width + wx] = b != nullptr && sailable_building(*b) ? 1 : 0;
        }
    }
}

void SeaMap::label_blocks(std::uint32_t bx0, std::uint32_t by0, std::uint32_t bx1, std::uint32_t by1) {
    bool renumber = false;
    for (std::uint32_t by = by0; by <= by1; ++by) {
        for (std::uint32_t bx = bx0; bx <= bx1; ++bx) {
            const std::uint32_t ox = bx * kBlock;
            const std::uint32_t oy = by * kBlock;
            const std::uint32_t w = std::min<std::uint32_t>(kBlock, m_width - ox);
            const std::uint32_t h = std::min<std::uint32_t>(kBlock, m_height - oy);
            for (std::uint32_t y = 0; y < h; ++y) {
                std::fill_n(m_region.begin() + static_cast<std::ptrdiff_t>(std::size_t{oy + y} * m_width + ox), w,
                            std::uint8_t{0});
            }
            // Four-connected flood fill in scan order: a ship never cuts a corner, so two water
            // tiles touching only diagonally are not joined by that step alone.
            std::uint8_t count = 0;
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const std::size_t at = std::size_t{oy + y} * m_width + ox + x;
                    if (m_cells[at] == 0 || m_region[at] != 0) {
                        continue;
                    }
                    ++count;
                    m_region[at] = count;
                    m_label_stack.clear();
                    m_label_stack.push_back(static_cast<std::uint16_t>(y * kBlock + x));
                    while (!m_label_stack.empty()) {
                        const std::uint32_t p = m_label_stack.back();
                        m_label_stack.pop_back();
                        const std::uint32_t px = p % kBlock;
                        const std::uint32_t py = p / kBlock;
                        const auto visit = [&](std::uint32_t nx, std::uint32_t ny) {
                            const std::size_t n = std::size_t{oy + ny} * m_width + ox + nx;
                            if (m_cells[n] != 0 && m_region[n] == 0) {
                                m_region[n] = count;
                                m_label_stack.push_back(static_cast<std::uint16_t>(ny * kBlock + nx));
                            }
                        };
                        if (px > 0) {
                            visit(px - 1, py);
                        }
                        if (px + 1 < w) {
                            visit(px + 1, py);
                        }
                        if (py > 0) {
                            visit(px, py - 1);
                        }
                        if (py + 1 < h) {
                            visit(px, py + 1);
                        }
                    }
                }
            }
            std::uint8_t& stored = m_region_count[std::size_t{by} * m_blocks_x + bx];
            renumber = renumber || stored != count;
            stored = count;
        }
    }
    if (!renumber && !m_region_block.empty()) {
        return;
    }
    // Regions are numbered block by block; only a changed count moves the numbers.
    m_region_block.clear();
    for (std::uint32_t b = 0; b < m_region_count.size(); ++b) {
        m_region_first[b] = static_cast<std::uint32_t>(m_region_block.size());
        m_region_block.insert(m_region_block.end(), m_region_count[b], b);
    }
}

SeaCourse SeaPlanner::plan(const SeaMap& sea, const SeaPlan& r) {
    SeaCourse course;
    const std::int32_t size = 2 * r.radius + 1;
    if (r.radius <= 0) {
        return course;
    }
    const std::int32_t x0 = r.start_x - r.radius + r.shift_x;
    const std::int32_t y0 = r.start_y - r.radius + r.shift_y;
    const auto cells = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    m_cost.assign(cells, kInfinite);
    m_state.assign(cells, 0);
    m_class.assign(cells, 0);
    const auto inside = [&](std::int32_t x, std::int32_t y) {
        return x >= x0 && y >= y0 && x < x0 + size && y < y0 + size;
    };
    const auto at = [&](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x - x0);
    };
    if (!inside(r.start_x, r.start_y)) {
        return course;
    }
    for (std::int32_t y = y0; y < y0 + size; ++y) {
        for (std::int32_t x = x0; x < x0 + size; ++x) {
            if (!sea.sailable(x, y)) {
                m_state[at(x, y)] = kBlocked;
            }
        }
    }
    // Water near hostile towers first; the ships close by are marked over it (0x00455A20).
    for (const SeaPlan::Danger& d : r.danger) {
        for (std::int32_t y = std::max(d.y0, y0); y <= std::min(d.y1, y0 + size - 1); ++y) {
            for (std::int32_t x = std::max(d.x0, x0); x <= std::min(d.x1, x0 + size - 1); ++x) {
                m_class[at(x, y)] = kClassDanger;
            }
        }
    }
    // Other ships (0x004560A0): 3 x 3 squares, softened after a failure for ships lying still.
    for (const SeaPlan::Obstacle& o : r.ships) {
        if (o.x == r.start_x && o.y == r.start_y) {
            continue;
        }
        const bool soft = r.soft_ships && !o.moving;
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                const std::int32_t x = o.x + dx;
                const std::int32_t y = o.y + dy;
                if (!inside(x, y)) {
                    continue;
                }
                if (!soft || (dx == 0 && dy == 0)) {
                    m_state[at(x, y)] = kBlocked;
                } else {
                    m_class[at(x, y)] = kClassSoft;
                }
            }
        }
    }
    const std::size_t start = at(r.start_x, r.start_y);
    m_state[start] = 0;  // the ship's own tile is always open
    if (r.goal && r.goal(r.start_x, r.start_y)) {
        course.reached_goal = true;
        course.end_x = r.start_x;
        course.end_y = r.start_y;
        return course;
    }

    // Dijkstra with a binary heap; ties in insertion order.
    m_heap.clear();
    std::uint32_t seq = 0;
    const auto push = [&](std::uint32_t cost, std::size_t cell) {
        m_heap.push_back(Node{cost, seq++, static_cast<std::uint32_t>(cell)});
        std::push_heap(m_heap.begin(), m_heap.end(),
                       [](const Node& a, const Node& b) { return a.cost != b.cost ? a.cost > b.cost : a.seq > b.seq; });
    };
    const auto pop = [&]() {
        std::pop_heap(m_heap.begin(), m_heap.end(),
                      [](const Node& a, const Node& b) { return a.cost != b.cost ? a.cost > b.cost : a.seq > b.seq; });
        const Node n = m_heap.back();
        m_heap.pop_back();
        return n;
    };
    m_cost[start] = 0;
    push(0, start);
    std::optional<std::size_t> goal;
    std::optional<std::size_t> edge;  // best visited cell on the window edge towards the aim point
    std::int32_t edge_distance = sea_distance(r.aim_x - r.start_x, r.aim_y - r.start_y);
    const bool aim_outside = !inside(r.aim_x, r.aim_y);
    while (!m_heap.empty()) {
        const Node n = pop();
        if ((m_state[n.cell] & kVisited) != 0) {
            continue;
        }
        m_state[n.cell] |= kVisited;
        const auto cx = static_cast<std::int32_t>(n.cell % static_cast<std::size_t>(size)) + x0;
        const auto cy = static_cast<std::int32_t>(n.cell / static_cast<std::size_t>(size)) + y0;
        if (n.cell != start && r.goal && r.goal(cx, cy)) {
            goal = n.cell;
            break;
        }
        if (aim_outside && (cx == x0 || cy == y0 || cx == x0 + size - 1 || cy == y0 + size - 1)) {
            const std::int32_t d = sea_distance(r.aim_x - cx, r.aim_y - cy);
            if (d < edge_distance) {
                edge_distance = d;
                edge = n.cell;
            }
        }
        for (const std::uint8_t dir : kOrder) {
            const std::int32_t nx = cx + step_x(dir);
            const std::int32_t ny = cy + step_y(dir);
            if (!inside(nx, ny)) {
                continue;
            }
            const std::size_t nc = at(nx, ny);
            if ((m_state[nc] & (kBlocked | kVisited)) != 0) {
                continue;
            }
            const bool diagonal = (dir & 1U) != 0;
            if (diagonal) {
                // No cut corners: both straight neighbours must be open.
                if ((m_state[at(cx + step_x(dir), cy)] & kBlocked) != 0 ||
                    (m_state[at(cx, cy + step_y(dir))] & kBlocked) != 0) {
                    continue;
                }
            }
            // The cost is that of the tile left (as the land search).
            std::uint32_t step = diagonal ? kDiagonal : kStraight;
            if (m_class[n.cell] == kClassSoft) {
                step = diagonal ? kSoftDiagonal : kSoftStraight;
            } else if (m_class[n.cell] == kClassDanger) {
                step = diagonal ? kDangerDiagonal : kDangerStraight;
            }
            const std::uint32_t cost = n.cost + step;
            if (cost < m_cost[nc]) {
                m_cost[nc] = cost;
                m_state[nc] = static_cast<std::uint8_t>((m_state[nc] & 0x0FU) | ((dir + 1U) << 4U));
                push(cost, nc);
            }
        }
    }
    std::optional<std::size_t> end = goal ? goal : edge;
    if (!goal && r.guided && plan_route(sea, r)) {
        // The leg ends on the visited cell furthest along the route, the cheapest one of those.
        std::uint32_t best_step = 1;  // the start's own region is no progress
        std::uint32_t best_cost = kInfinite;
        std::optional<std::size_t> best;
        for (std::int32_t y = y0; y < y0 + size; ++y) {
            for (std::int32_t x = x0; x < x0 + size; ++x) {
                const std::size_t c = at(x, y);
                if ((m_state[c] & kVisited) == 0) {
                    continue;
                }
                const std::uint32_t region = sea.region(x, y);
                if (region == SeaMap::kNoRegion) {
                    continue;  // the ship's own tile can lie on land (piers)
                }
                const std::uint32_t step = m_route_step[region];
                if (step > best_step || (step == best_step && step > 1 && m_cost[c] < best_cost)) {
                    best_step = step;
                    best_cost = m_cost[c];
                    best = c;
                }
            }
        }
        clear_route();
        if (best) {
            end = best;
        }
    }
    if (!end) {
        return course;
    }
    course.reached_goal = goal.has_value();
    // Walk back from the end to the start, then emit runs from the start side.
    std::vector<std::uint8_t> dirs;
    std::size_t cell = *end;
    while (cell != start) {
        const std::uint8_t from = static_cast<std::uint8_t>(m_state[cell] >> 4U);
        if (from == 0) {
            return SeaCourse{};  // cannot happen: every visited cell but the start has a parent
        }
        const auto dir = static_cast<std::uint8_t>(from - 1U);
        dirs.push_back(dir);
        const auto cx = static_cast<std::int32_t>(cell % static_cast<std::size_t>(size)) + x0;
        const auto cy = static_cast<std::int32_t>(cell / static_cast<std::size_t>(size)) + y0;
        cell = at(cx - step_x(dir), cy - step_y(dir));
    }
    std::reverse(dirs.begin(), dirs.end());
    std::int32_t x = r.start_x;
    std::int32_t y = r.start_y;
    std::size_t i = 0;
    while (i < dirs.size() && course.path.size() < kMaxRuns) {
        const std::uint8_t dir = dirs[i];
        const std::uint8_t cls = m_class[at(x, y)];
        std::uint8_t n = 0;
        // A run: same direction and cost class, at most two tiles (0x0046CF70).
        while (i < dirs.size() && dirs[i] == dir && n < kMaxRunTiles && m_class[at(x, y)] == cls) {
            x += step_x(dir);
            y += step_y(dir);
            ++n;
            ++i;
        }
        course.path.push_back(path_code(dir, n));
    }
    if (i < dirs.size()) {
        course.reached_goal = false;  // truncated: the ship plans again at the end
    }
    course.end_x = x;
    course.end_y = y;
    return course;
}

bool SeaPlanner::plan_route(const SeaMap& sea, const SeaPlan& r) {
    const std::uint32_t start = sea.region(r.start_x, r.start_y);
    const std::uint32_t total = sea.regions();
    if (start == SeaMap::kNoRegion || sea.blocks_x() == 0 || sea.blocks_y() == 0) {
        return false;
    }
    if (m_route_cost.size() != total) {
        m_route_cost.assign(total, 0);
        m_route_from.assign(total, 0);
        m_route_stamp.assign(total, 0);
        m_route_goal.assign(total, 0);
        m_route_step.assign(total, 0);
        m_route_search = 0;
    }
    if (++m_route_search == 0) {
        std::fill(m_route_stamp.begin(), m_route_stamp.end(), 0U);
        std::fill(m_route_goal.begin(), m_route_goal.end(), 0U);
        m_route_search = 1;
    }
    const std::uint32_t search = m_route_search;
    constexpr std::int32_t kB = SeaMap::kBlock;
    const auto bxs = static_cast<std::int32_t>(sea.blocks_x());
    const auto bys = static_cast<std::int32_t>(sea.blocks_y());
    const auto block_of = [](std::int32_t v) { return v >= 0 ? v / kB : -((-v + kB - 1) / kB); };
    const std::int32_t aim_bx = block_of(r.aim_x);
    const std::int32_t aim_by = block_of(r.aim_y);

    // Goals: the regions of the blocks around the aim point holding a goal tile; if none does,
    // all regions there (the target may need a closer look once it is in the window).
    bool any_goal = false;
    for (int pass = 0; pass < 2 && !any_goal; ++pass) {
        for (std::int32_t by = std::max(aim_by - 1, 0); by <= std::min(aim_by + 1, bys - 1); ++by) {
            for (std::int32_t bx = std::max(aim_bx - 1, 0); bx <= std::min(aim_bx + 1, bxs - 1); ++bx) {
                for (std::int32_t y = by * kB; y < (by + 1) * kB; ++y) {
                    for (std::int32_t x = bx * kB; x < (bx + 1) * kB; ++x) {
                        const std::uint32_t region = sea.region(x, y);
                        if (region == SeaMap::kNoRegion || m_route_goal[region] == search) {
                            continue;
                        }
                        if (pass == 1 || (r.goal && r.goal(x, y))) {
                            m_route_goal[region] = search;
                            any_goal = true;
                        }
                    }
                }
            }
        }
    }
    if (!any_goal) {
        return false;
    }

    // A* over the regions; a step to a neighbouring block costs as much as 16 tiles of open sea
    // that way, the estimate is the same distance to the aim's block. Ties in insertion order.
    const auto estimate = [&](std::uint32_t block) {
        const std::int32_t dx = std::abs(static_cast<std::int32_t>(block % sea.blocks_x()) - aim_bx);
        const std::int32_t dy = std::abs(static_cast<std::int32_t>(block / sea.blocks_x()) - aim_by);
        const auto lo = static_cast<std::uint32_t>(std::min(dx, dy));
        const auto hi = static_cast<std::uint32_t>(std::max(dx, dy));
        return static_cast<std::uint32_t>(kB) * (kStraight * (hi - lo) + kDiagonal * lo);
    };
    const auto later = [](const Node& a, const Node& b) { return a.cost != b.cost ? a.cost > b.cost : a.seq > b.seq; };
    m_route_heap.clear();
    std::uint32_t seq = 0;
    const auto reach = [&](std::uint32_t region, std::uint32_t from, std::uint32_t cost) {
        if (m_route_stamp[region] == search && m_route_cost[region] <= cost) {
            return;
        }
        m_route_stamp[region] = search;
        m_route_cost[region] = cost;
        m_route_from[region] = from;
        m_route_heap.push_back(Node{cost + estimate(sea.region_block(region)), seq++, region});
        std::push_heap(m_route_heap.begin(), m_route_heap.end(), later);
    };
    reach(start, start, 0);
    std::optional<std::uint32_t> found;
    while (!m_route_heap.empty()) {
        std::pop_heap(m_route_heap.begin(), m_route_heap.end(), later);
        const Node n = m_route_heap.back();
        m_route_heap.pop_back();
        const std::uint32_t region = n.cell;
        const std::uint32_t g = m_route_cost[region];
        if (n.cost != g + estimate(sea.region_block(region))) {
            continue;  // a cheaper way was found after this entry
        }
        if (m_route_goal[region] == search) {
            found = region;
            break;
        }
        const std::uint32_t block = sea.region_block(region);
        const auto bx = static_cast<std::int32_t>(block % sea.blocks_x());
        const auto by = static_cast<std::int32_t>(block / sea.blocks_x());
        const std::int32_t ox = bx * kB;
        const std::int32_t oy = by * kB;
        const std::int32_t ex = std::min(ox + kB, static_cast<std::int32_t>(sea.width())) - 1;  // last tile
        const std::int32_t ey = std::min(oy + kB, static_cast<std::int32_t>(sea.height())) - 1;
        const std::uint32_t straight = g + static_cast<std::uint32_t>(kB) * kStraight;
        const std::uint32_t diagonal = g + static_cast<std::uint32_t>(kB) * kDiagonal;
        // Across the four sides: a tile of this region next to water of the neighbouring block.
        const auto across = [&](std::int32_t x, std::int32_t y, std::int32_t nx, std::int32_t ny) {
            if (sea.region(x, y) == region) {
                const std::uint32_t next = sea.region(nx, ny);
                if (next != SeaMap::kNoRegion) {
                    reach(next, region, straight);
                }
            }
        };
        for (std::int32_t x = ox; x <= ex; ++x) {
            across(x, oy, x, oy - 1);
            across(x, ey, x, ey + 1);
        }
        for (std::int32_t y = oy; y <= ey; ++y) {
            across(ox, y, ox - 1, y);
            across(ex, y, ex + 1, y);
        }
        // Over the four corners, without cutting one.
        const std::int32_t cx[2] = {ox, ex};
        const std::int32_t cy[2] = {oy, ey};
        for (int i = 0; i < 4; ++i) {
            const std::int32_t x = cx[i & 1];
            const std::int32_t y = cy[i >> 1];
            const std::int32_t dx = (i & 1) != 0 ? 1 : -1;
            const std::int32_t dy = (i >> 1) != 0 ? 1 : -1;
            if (sea.region(x, y) != region || !sea.sailable(x + dx, y) || !sea.sailable(x, y + dy)) {
                continue;
            }
            const std::uint32_t next = sea.region(x + dx, y + dy);
            if (next != SeaMap::kNoRegion) {
                reach(next, region, diagonal);
            }
        }
    }
    if (!found) {
        return false;
    }
    m_route.clear();
    for (std::uint32_t region = *found;; region = m_route_from[region]) {
        m_route.push_back(region);
        if (region == start) {
            break;
        }
    }
    std::reverse(m_route.begin(), m_route.end());
    for (std::size_t i = 0; i < m_route.size(); ++i) {
        m_route_step[m_route[i]] = static_cast<std::uint32_t>(i + 1);
    }
    return true;
}

void SeaPlanner::clear_route() {
    for (const std::uint32_t region : m_route) {
        m_route_step[region] = 0;
    }
    m_route.clear();
}

}  // namespace o1602::sim
