// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/drag_path.hpp"

#include <algorithm>

#include "o1602/sim/game_data.hpp"

namespace o1602::sim {

namespace {

// The search spends 0x40 of 0x4000 per round and stops below 0: 257 rounds.
constexpr std::uint32_t kMaxSteps = 257;

// Field of the search: how a tile was reached.
enum Reach : std::uint8_t { Open, Blocked, Start, FromSouth, FromNorth, FromEast, FromWest };

struct Neighbour {
    std::int32_t dx;
    std::int32_t dy;
    Reach reach;  // the step that reaches it
};
// The order the original visits the neighbours in: north, south, west, east.
constexpr Neighbour kNeighbours[] = {
    {0, -1, FromSouth},  // a step north (y − 1)
    {0, 1, FromNorth},   // a step south
    {-1, 0, FromEast},   // a step west
    {1, 0, FromWest},    // a step east
};

}  // namespace

bool drag_passable(DragLine line, std::int32_t kind, bool gate_centre) noexcept {
    if (line == DragLine::Road) {
        if (kind == sim::kind::kGate) {
            return gate_centre;
        }
        return kind == sim::kind::kRoad || (kind >= sim::kind::kForest && kind <= sim::kind::kSquare) ||
               kind == sim::kind::kRiver || kind == sim::kind::kBridge;
    }
    return (kind >= sim::kind::kGate && kind <= sim::kind::kTowerBeach) ||
           (kind >= sim::kind::kForest && kind <= sim::kind::kRuin) ||
           (kind >= sim::kind::kBeach && kind <= sim::kind::kBeachVariant) || kind == sim::kind::kBeachRuin;
}

std::vector<DragStep> drag_path(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                                DragLine line, TilePos from, TilePos to, std::uint8_t rotation) {
    std::vector<DragStep> cursor_only{DragStep{to.x, to.y, rotation}};
    if (island >= world.islands().size() || from == to) {
        return cursor_only;
    }
    const IslandMap& map = world.islands()[island].map;
    if (!map.contains(from.x, from.y) || !map.contains(to.x, to.y)) {
        return cursor_only;
    }
    const auto w = static_cast<std::int32_t>(map.width());
    const auto h = static_cast<std::int32_t>(map.height());
    const auto at = [w](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    const auto cell_of = [&](std::int32_t x, std::int32_t y) -> const MapCell& {
        return map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    };

    // Only tiles of the start tile's city (unclaimed land counts as one city) that take the line.
    // Unlike the original, which keeps the field of an earlier drag outside the city (U-007), the
    // field is built anew for every search.
    const std::uint8_t city = cell_of(from.x, from.y).city;
    const auto passable = [&](std::int32_t x, std::int32_t y) {
        const MapCell& c = cell_of(x, y);
        if (c.city != city || c.empty()) {
            return false;
        }
        const assets::BuildingType* b = buildings.by_sprite(c.sprite);
        if (b == nullptr) {
            return false;
        }
        const bool gate_centre = c.sprite == b->gfx + (b->size_x * b->size_y) / 2;
        return drag_passable(line, b->kind, gate_centre);
    };

    std::vector<Reach> field(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), Open);
    const auto reach_of = [&](std::int32_t x, std::int32_t y) -> Reach& { return field[at(x, y)]; };
    const auto open = [&](std::int32_t x, std::int32_t y) {
        Reach& r = reach_of(x, y);
        if (r == Open && !passable(x, y)) {
            r = Blocked;  // the original marks the whole island first; checking on the way is the same
        }
        return r == Open;
    };

    // Breadth first from the start (even if its own tile is blocked), round by round; a tile
    // keeps the step that reached it first.
    reach_of(from.x, from.y) = Start;
    std::vector<TilePos> layer{from};
    std::vector<TilePos> next;
    for (std::uint32_t round = 0; round < kMaxSteps && !layer.empty(); ++round) {
        next.clear();
        for (const TilePos p : layer) {
            for (const Neighbour& n : kNeighbours) {
                const std::int32_t x = p.x + n.dx;
                const std::int32_t y = p.y + n.dy;
                if (x < 0 || y < 0 || x >= w || y >= h || !open(x, y)) {
                    continue;
                }
                reach_of(x, y) = n.reach;
                next.push_back({x, y});
            }
        }
        layer.swap(next);
    }

    // Back from the cursor to the start.
    std::vector<std::uint8_t> steps;  // rotation of each step, cursor first
    TilePos p = to;
    for (;;) {
        const Reach r = reach_of(p.x, p.y);
        if (r == FromWest) {
            steps.push_back(0);
            --p.x;
        } else if (r == FromNorth) {
            steps.push_back(1);
            --p.y;
        } else if (r == FromEast) {
            steps.push_back(2);
            ++p.x;
        } else if (r == FromSouth) {
            steps.push_back(3);
            ++p.y;
        } else {
            break;
        }
    }
    if (reach_of(p.x, p.y) != Start) {
        return cursor_only;
    }
    // Forward: each step on the tile it leaves, the cursor with the last step's rotation.
    std::vector<DragStep> path;
    path.reserve(steps.size() + 1);
    for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
        path.push_back({p.x, p.y, *it});
        switch (*it) {
            case 0:
                ++p.x;
                break;
            case 1:
                ++p.y;
                break;
            case 2:
                --p.x;
                break;
            default:
                --p.y;
                break;
        }
    }
    path.push_back({to.x, to.y, steps.empty() ? rotation : steps.front()});
    return path;
}

}  // namespace o1602::sim
