// SPDX-License-Identifier: GPL-3.0-or-later
// Ways of soldiers on land: LandMap and LandPlanner.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/sim/land_paths.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;  // walk, Wegspeed 100 % (soldiers 150 %)
constexpr std::uint32_t kRoad = 20102;    // walk
constexpr std::uint32_t kForest = 20103;  // wall
constexpr std::uint32_t kField = 20104;   // open: blocks walking, not shots
constexpr std::uint32_t kSlow = 20105;    // walk, Wegspeed 200 %
constexpr std::uint32_t kHouse = 20106;   // wall, 2 x 2
constexpr std::uint32_t kWater = 20107;   // open: sea inside the island
constexpr std::uint32_t kGate = 20108;    // walk, a city gate

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Wegspeed: 100, 150, 100, 100\n"
        "  @Nummer: 1\n  Id: 20102\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20103\n  Kind: 10\n  Gfx: 2\n  Size: 1, 1\n  NoShotFlg: 1\n"
        "  @Nummer: 3\n  Id: 20104\n  Kind: 14\n  Gfx: 3\n  Size: 1, 1\n"
        "  @Nummer: 4\n  Id: 20105\n  Kind: 11\n  Gfx: 4\n  Size: 1, 1\n  Wegspeed: 200, 200, 200, 200\n"
        "  @Nummer: 5\n  Id: 20106\n  Kind: 14\n  Gfx: 6\n  Size: 2, 2\n  NoShotFlg: 1\n"
        "  @Nummer: 6\n  Id: 20107\n  Kind: 19\n  Gfx: 10\n  Size: 1, 1\n"
        "  @Nummer: 7\n  Id: 20108\n  Kind: 3\n  Gfx: 11\n  Size: 1, 1\n  NoShotFlg: 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of `w` x `h` tiles with an island of ground (`iw` x `ih` at `ix`, `iy`); its tile
/// (`hole_x`, `hole_y`) has no building when given.
struct Field {
    assets::BuildingTable buildings = make_buildings();
    sim::World world;
    sim::LandMap land;

    Field(std::uint32_t w, std::uint32_t h, std::uint32_t ix, std::uint32_t iy, std::uint32_t iw, std::uint32_t ih,
          std::optional<std::pair<std::int32_t, std::int32_t>> hole = std::nullopt)
        : world(w, h) {
        sim::IslandMap map(iw, ih);
        for (std::int32_t y = 0; y < static_cast<std::int32_t>(ih); ++y) {
            for (std::int32_t x = 0; x < static_cast<std::int32_t>(iw); ++x) {
                if (!hole || *hole != std::pair{x, y}) {
                    REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
                }
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), ix, iy}).ok());
        land.rebuild(world, buildings);
    }
    /// The usual field: 40 x 30 tiles, the island 24 x 16 at (4, 4), i.e. cells 8..55 x 8..39.
    Field() : Field(40, 30, 4, 4, 24, 16) {}

    /// Places a building at island tile (x, y) of the first island.
    void put(std::uint32_t id, std::int32_t x, std::int32_t y) {
        REQUIRE(world.island(0).map.place(*buildings.by_id(id), x, y, 0, 0).ok());
    }
};

sim::LandPlan request(std::int32_t sx, std::int32_t sy, std::int32_t gx, std::int32_t gy) {
    sim::LandPlan plan;
    plan.start_x = sx;
    plan.start_y = sy;
    plan.aim_x = gx;
    plan.aim_y = gy;
    plan.first_goal = true;
    plan.goal = [gx, gy](std::int32_t x, std::int32_t y) { return x == gx && y == gy; };
    return plan;
}

/// The planner's window as the test sees it: blocked cells and the Wegspeed of each cell.
struct Grid {
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t size = 0;
    std::vector<std::uint8_t> blocked;
    std::vector<std::uint8_t> speed;

    [[nodiscard]] bool inside(std::int32_t x, std::int32_t y) const {
        return x >= x0 && y >= y0 && x < x0 + size && y < y0 + size;
    }
    [[nodiscard]] std::size_t at(std::int32_t x, std::int32_t y) const {
        return static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x - x0);
    }
    [[nodiscard]] bool open(std::int32_t x, std::int32_t y) const { return inside(x, y) && blocked[at(x, y)] == 0; }
    [[nodiscard]] std::uint32_t step_cost(std::int32_t x, std::int32_t y, bool diagonal) const {
        const std::uint8_t s = speed[at(x, y)];
        return diagonal ? sim::diagonal_cost(s) : sim::straight_cost(s);
    }
};

Grid make_grid(const sim::LandMap& land, const sim::LandPlan& r) {
    Grid g;
    g.size = 2 * r.radius + 1;
    g.x0 = r.start_x - r.radius + r.shift_x;
    g.y0 = r.start_y - r.radius + r.shift_y;
    const auto cells = static_cast<std::size_t>(g.size) * static_cast<std::size_t>(g.size);
    g.blocked.assign(cells, 0);
    g.speed.assign(cells, 32);
    for (std::int32_t y = g.y0; y < g.y0 + g.size; ++y) {
        for (std::int32_t x = g.x0; x < g.x0 + g.size; ++x) {
            std::uint8_t cost = 32;
            g.blocked[g.at(x, y)] = land.cell(x, y, r.speed_type, &cost) == sim::LandMap::Cell::Walk ? 0 : 1;
            g.speed[g.at(x, y)] = cost;
        }
    }
    for (const auto& [x, y] : r.blocked) {
        if (g.inside(x, y)) {
            g.blocked[g.at(x, y)] = 1;
        }
    }
    g.blocked[g.at(r.start_x, r.start_y)] = 0;
    return g;
}

/// Plain Dijkstra over the grid: the cost of the cheapest way to a goal cell other than the start.
std::optional<std::uint32_t> reference_cost(const Grid& g, std::int32_t sx, std::int32_t sy,
                                            const std::function<bool(std::int32_t, std::int32_t)>& goal) {
    std::vector<std::uint32_t> best(g.blocked.size(), 0xFFFFFFFFU);
    using Item = std::pair<std::uint32_t, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
    best[g.at(sx, sy)] = 0;
    open.emplace(0, g.at(sx, sy));
    while (!open.empty()) {
        const auto [cost, cell] = open.top();
        open.pop();
        if (cost != best[cell]) {
            continue;
        }
        const std::int32_t x = static_cast<std::int32_t>(cell % static_cast<std::size_t>(g.size)) + g.x0;
        const std::int32_t y = static_cast<std::int32_t>(cell / static_cast<std::size_t>(g.size)) + g.y0;
        if ((x != sx || y != sy) && goal(x, y)) {
            return cost;
        }
        for (std::uint8_t dir = 0; dir < 8; ++dir) {
            const std::int32_t nx = x + sim::step_x(dir);
            const std::int32_t ny = y + sim::step_y(dir);
            const bool diagonal = (dir & 1U) != 0;
            if (!g.open(nx, ny) || (diagonal && (!g.open(nx, y) || !g.open(x, ny)))) {
                continue;
            }
            const std::uint32_t c = cost + g.step_cost(x, y, diagonal);
            if (c < best[g.at(nx, ny)]) {
                best[g.at(nx, ny)] = c;
                open.emplace(c, g.at(nx, ny));
            }
        }
    }
    return std::nullopt;
}

/// Follows a way over the grid; checks every run and step and adds up the cost.
struct Trace {
    bool valid = true;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t cost = 0;
};

Trace trace(const Grid& g, std::int32_t x, std::int32_t y, const std::vector<std::uint8_t>& path,
            std::uint8_t max_run) {
    Trace t{true, x, y, 0};
    for (const std::uint8_t code : path) {
        const auto dir = static_cast<std::uint8_t>(code >> 4U);
        const std::uint32_t cells = code & 0x0FU;
        if (dir > 7 || cells < 1 || cells > max_run) {
            t.valid = false;
            return t;
        }
        const std::uint8_t run_speed = g.speed[g.at(t.x, t.y)];
        for (std::uint32_t i = 0; i < cells; ++i) {
            const std::int32_t nx = t.x + sim::step_x(dir);
            const std::int32_t ny = t.y + sim::step_y(dir);
            const bool diagonal = (dir & 1U) != 0;
            // Walkable cells, no corner cut, one walking cost for all cells a run leaves.
            if (!g.open(nx, ny) || (diagonal && (!g.open(nx, t.y) || !g.open(t.x, ny))) ||
                g.speed[g.at(t.x, t.y)] != run_speed) {
                t.valid = false;
                return t;
            }
            t.cost += g.step_cost(t.x, t.y, diagonal);
            t.x = nx;
            t.y = ny;
        }
    }
    return t;
}

struct Lcg {
    std::uint32_t state;
    std::uint32_t next(std::uint32_t bound) {
        state = state * 1664525U + 1013904223U;
        return (state >> 8U) % bound;
    }
};

}  // namespace

TEST_CASE("land paths: soldiers walk on roads, gates, ground, ruins, squares, bridges and piers") {
    assets::BuildingType b;
    for (std::int32_t kind = 0; kind < 40; ++kind) {
        b.kind = kind;
        const bool walk = kind == sim::building_kind::kRoad || kind == sim::building_kind::kGate ||
                          kind == sim::building_kind::kGround || kind == sim::building_kind::kRuin ||
                          kind == sim::building_kind::kSquare || kind == sim::building_kind::kBridge ||
                          kind == sim::building_kind::kBeachRuin || kind == sim::building_kind::kPier;
        CHECK(sim::soldiers_walk_on(b) == walk);
    }
    // Never through forest or water.
    b.kind = sim::building_kind::kForest;
    CHECK(!sim::soldiers_walk_on(b));
    b.kind = sim::building_kind::kSea;
    CHECK(!sim::soldiers_walk_on(b));
}

TEST_CASE("land paths: the map tells walkable, open, wall and off cells, with the walking cost") {
    Field f(40, 30, 4, 4, 24, 16, std::pair{9, 2});
    f.put(kForest, 2, 2);  // world tile (6, 6)
    f.put(kField, 3, 2);
    f.put(kSlow, 4, 2);
    f.put(kRoad, 5, 2);
    f.put(kHouse, 6, 2);  // world tiles (10..11, 6..7)
    f.put(kWater, 8, 2);
    using Cell = sim::LandMap::Cell;
    const sim::LandMap& land = f.land;
    std::uint8_t cost = 0;
    CHECK(land.tile(4, 4, 0, &cost) == Cell::Walk);
    CHECK(cost == 32);
    CHECK(land.tile(4, 4, 1, &cost) == Cell::Walk);
    CHECK(cost == 48);  // the soldiers' speed type: 150 %
    CHECK(land.tile(4, 4, 5, &cost) == Cell::Walk);
    CHECK(cost == 48);  // only the low two bits of the speed type count
    CHECK(land.tile(8, 6, 1, &cost) == Cell::Walk);
    CHECK(cost == 64);
    CHECK(land.tile(9, 6, 0) == Cell::Walk);
    cost = 99;
    CHECK(land.tile(6, 6, 0, &cost) == Cell::Wall);
    CHECK(cost == 99);  // set for walkable tiles only
    CHECK(land.tile(7, 6, 0) == Cell::Open);
    CHECK(land.tile(10, 6, 0) == Cell::Wall);
    CHECK(land.tile(11, 7, 0) == Cell::Wall);
    CHECK(land.tile(12, 6, 0) == Cell::Open);  // water inside the island
    CHECK(land.tile(13, 6, 0) == Cell::Open);  // island tile without a building
    CHECK(land.tile(3, 4, 0) == Cell::Off);    // sea
    CHECK(land.tile(-1, 5, 0) == Cell::Off);
    CHECK(land.tile(40, 5, 0) == Cell::Off);
    CHECK(land.tile(5, 30, 0) == Cell::Off);

    // A tile is 2 x 2 cells of its kind.
    for (std::int32_t dy = 0; dy < 2; ++dy) {
        for (std::int32_t dx = 0; dx < 2; ++dx) {
            CHECK(land.cell(12 + dx, 12 + dy, 0) == Cell::Wall);
            CHECK(land.cell(16 + dx, 12 + dy, 1, &cost) == Cell::Walk);
            CHECK(cost == 64);
        }
    }
    CHECK(land.cell(11, 12, 0) == Cell::Walk);
    CHECK(land.cell(-1, -1, 0) == Cell::Off);
    CHECK(land.cell(-1, 10, 0) == Cell::Off);

    CHECK(land.building(6, 6) == f.buildings.by_id(kForest));
    CHECK(land.building(11, 7) == f.buildings.by_id(kHouse));
    CHECK(land.building(13, 6) == nullptr);
    CHECK(land.building(0, 0) == nullptr);
    CHECK(land.building(-3, 2) == nullptr);
    CHECK(land.island_at(6, 6) == 0);
    CHECK(land.island_at(0, 0) == sim::World::kSea);
    CHECK(land.island_at(-3, 2) == sim::World::kSea);
    CHECK(land.island_at(5, 99) == sim::World::kSea);

    // The map reads the island directly: a change shows without a rebuild.
    f.put(kForest, 0, 0);
    CHECK(land.tile(4, 4, 0) == Cell::Wall);
    // A map that was never built knows nothing.
    const sim::LandMap none;
    CHECK(none.tile(5, 5, 0) == Cell::Off);
    CHECK(none.building(5, 5) == nullptr);
    CHECK(none.island_at(5, 5) == sim::World::kSea);
}

TEST_CASE("land paths: a player's closed gates stop foreign soldiers, not his own") {
    Field f;
    f.world.set_players(std::vector<sim::Player>(3));
    // A wall of forest across the island at island column 10 with a gate of player 1 in it.
    for (std::int32_t y = 0; y < 16; ++y) {
        if (y != 8) {
            f.put(kForest, 10, y);
        }
    }
    REQUIRE(f.world.island(0).map.place(*f.buildings.by_id(kGate), 10, 8, 0, 0, 1).ok());
    using Cell = sim::LandMap::Cell;
    const sim::LandMap& land = f.land;
    // Open gates: every soldier walks through, whoever he belongs to (as in the original).
    CHECK(land.tile(14, 12, 0, nullptr, 0) == Cell::Walk);
    CHECK(land.tile(14, 12, 0, nullptr, 1) == Cell::Walk);
    sim::LandPlanner planner;
    sim::LandPlan r = request(20, 24, 40, 24);
    r.walker = 2;
    CHECK(planner.plan(land, r).reached_goal);

    f.world.player(1)->gates_closed = true;
    CHECK(land.tile(14, 12, 0, nullptr, 0) == Cell::Wall);
    CHECK(land.tile(14, 12, 0, nullptr, 2) == Cell::Wall);
    CHECK(land.tile(14, 12, 0, nullptr, 1) == Cell::Walk);
    CHECK(land.tile(14, 12, 0) == Cell::Walk);  // no walker: the map as it is
    CHECK(!planner.plan(land, r).reached_goal);
    r.walker = 1;
    CHECK(planner.plan(land, r).reached_goal);
}

TEST_CASE("land paths: a cell rectangle's distance and nearest cell") {
    const sim::CellRect r{10, 10, 20, 15};
    CHECK(r.contains(10, 10));
    CHECK(r.contains(20, 15));
    CHECK(!r.contains(21, 15));
    CHECK(!r.contains(15, 9));
    CHECK(r.distance(14, 12) == 0);
    CHECK((r.nearest(14, 12) == std::pair{14, 12}));
    CHECK((r.nearest(0, 0) == std::pair{10, 10}));
    CHECK(r.distance(0, 0) == 12);  // 10 + 10 / 4
    CHECK((r.nearest(25, 12) == std::pair{20, 12}));
    CHECK(r.distance(25, 12) == 5);
    CHECK((r.nearest(15, 30) == std::pair{15, 15}));
    CHECK(r.distance(15, 30) == 15);
    CHECK((r.nearest(-4, 13) == std::pair{10, 13}));
    CHECK(r.distance(-4, 13) == 14);
    CHECK((r.nearest(30, 0) == std::pair{20, 10}));
    CHECK(r.distance(30, 0) == 12);
    const sim::CellRect one{5, 5, 5, 5};
    CHECK(one.distance(5, 5) == 0);
    CHECK(one.distance(1, 8) == 4);
    CHECK((one.nearest(1, 8) == std::pair{5, 5}));
}

TEST_CASE("land paths: walls block the line of sight, open cells and the sea do not") {
    Field f;
    f.put(kField, 5, 5);   // cells 18..19 x 18..19
    f.put(kForest, 8, 5);  // cells 24..25 x 18..19
    const sim::LandMap& land = f.land;
    const sim::CellRect none{-100, -100, -100, -100};
    CHECK(sim::line_of_sight(land, 10, 18, 22, 18, none));  // over the field
    CHECK(!sim::line_of_sight(land, 10, 18, 40, 18, none));
    CHECK(!sim::line_of_sight(land, 40, 19, 10, 19, none));
    CHECK(sim::line_of_sight(land, 10, 18, 40, 18, sim::CellRect{24, 18, 25, 19}));  // the target itself
    CHECK(sim::line_of_sight(land, 10, 18, 24, 18, none));                           // ends are not looked at
    CHECK(sim::line_of_sight(land, 25, 18, 40, 18, none));
    CHECK(sim::line_of_sight(land, 10, 17, 40, 17, none));   // the row above passes
    CHECK(!sim::line_of_sight(land, 20, 14, 30, 24, none));  // diagonally through (24, 18)
    CHECK(sim::line_of_sight(land, 0, 0, 7, 7, none));       // over the sea
    CHECK(sim::line_of_sight(land, 24, 18, 24, 18, none));
}

TEST_CASE("land paths: direction_to names the eight directions") {
    CHECK(sim::direction_to(0, 0) == 0);
    for (std::uint8_t d = 0; d < 8; ++d) {
        CHECK(sim::direction_to(sim::step_x(d), sim::step_y(d)) == d);
        CHECK(sim::direction_to(7 * sim::step_x(d), 7 * sim::step_y(d)) == d);
    }
    // The borders lie at about 22.5 degrees from the axes (5 / 12).
    CHECK(sim::direction_to(12, 5) == 2);
    CHECK(sim::direction_to(12, 6) == 3);
    CHECK(sim::direction_to(-12, -5) == 6);
    CHECK(sim::direction_to(5, 12) == 4);
    CHECK(sim::direction_to(6, -12) == 1);
    CHECK(sim::direction_to(-6, 12) == 5);
    CHECK(sim::direction_to(-5, -12) == 0);
}

TEST_CASE("land paths: ways are as cheap as a reference Dijkstra finds, on walkable cells only") {
    std::size_t reached = 0;
    std::size_t unreachable = 0;
    for (std::uint32_t seed = 1; seed <= 4; ++seed) {
        // An island at the world's corner (the window reaches beyond the world) and a second one.
        Field f(48, 40, 0, 0, 16, 12);
        sim::IslandMap second(20, 24);
        Lcg rng{seed};
        const std::uint32_t kinds[] = {kGround, kGround, kGround, kRoad, kSlow, kSlow, kForest, kField, kWater};
        const auto fill = [&](sim::IslandMap& map) {
            for (std::int32_t y = 0; y < static_cast<std::int32_t>(map.height()); ++y) {
                for (std::int32_t x = 0; x < static_cast<std::int32_t>(map.width()); ++x) {
                    REQUIRE(map.place(*f.buildings.by_id(kinds[rng.next(9)]), x, y, 0, 0).ok());
                }
            }
        };
        fill(f.world.island(0).map);
        fill(second);
        REQUIRE(f.world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(second), 18, 2}).ok());
        sim::LandPlanner planner;
        for (int round = 0; round < 40; ++round) {
            const bool west = rng.next(2) == 0;
            const std::int32_t x_from = west ? 0 : 36;
            const std::int32_t x_to = west ? 32 : 76;
            const std::int32_t y_to = west ? 24 : 52;
            const auto pick = [&](std::int32_t& x, std::int32_t& y) {
                do {
                    x = x_from + static_cast<std::int32_t>(rng.next(static_cast<std::uint32_t>(x_to - x_from)));
                    y = (west ? 0 : 4) + static_cast<std::int32_t>(rng.next(static_cast<std::uint32_t>(y_to)));
                } while (f.land.cell(x, y, 1) != sim::LandMap::Cell::Walk);
            };
            std::int32_t sx = 0;
            std::int32_t sy = 0;
            std::int32_t gx = 0;
            std::int32_t gy = 0;
            pick(sx, sy);
            do {
                pick(gx, gy);
            } while (gx == sx && gy == sy);
            sim::LandPlan plan = request(sx, sy, gx, gy);
            plan.radius = 80;  // both islands lie in the window, its edge on the sea
            plan.speed_type = 1;
            plan.max_run = static_cast<std::uint8_t>(1 + rng.next(4));
            plan.first_goal = rng.next(2) == 0;
            for (std::uint32_t i = rng.next(6); i > 0; --i) {
                plan.blocked.emplace_back(sx - 4 + static_cast<std::int32_t>(rng.next(9)),
                                          sy - 4 + static_cast<std::int32_t>(rng.next(9)));
            }
            const Grid grid = make_grid(f.land, plan);
            const auto expected = reference_cost(grid, sx, sy, plan.goal);
            const sim::LandCourse course = planner.plan(f.land, plan);
            if (!expected) {
                CHECK(course.path.empty());
                CHECK(!course.reached_goal);
                ++unreachable;
                continue;
            }
            REQUIRE(course.reached_goal);
            const Trace t = trace(grid, sx, sy, course.path, plan.max_run);
            CHECK(t.valid);
            CHECK(t.x == gx);
            CHECK(t.y == gy);
            CHECK(course.end_x == gx);
            CHECK(course.end_y == gy);
            CHECK(t.cost == *expected);
            ++reached;
        }
    }
    CHECK(reached > 80);
    CHECK(unreachable > 0);
}

TEST_CASE("land paths: an aim inside the window that cannot be reached gives no way") {
    // A hole in the island at tile (5, 9), cells 18..19 x 26..27; the window (radius 10 around
    // the start) has land on its edges.
    Field f(40, 30, 4, 4, 24, 16, std::pair{5, 9});
    sim::LandPlanner planner;
    sim::LandPlan plan = request(12, 30, 18, 26);
    plan.radius = 10;
    const sim::LandCourse course = planner.plan(f.land, plan);
    CHECK(!course.reached_goal);
    CHECK(course.path.empty());
}

TEST_CASE("land paths: the same request gives the same way on a reused and on a fresh planner") {
    Field f;
    for (std::int32_t y = 2; y < 14; ++y) {
        f.put(kForest, 10, y);
    }
    f.put(kSlow, 6, 8);
    sim::LandPlan a = request(12, 30, 40, 12);
    sim::LandPlan b = request(50, 36, 10, 10);
    b.first_goal = false;
    b.max_run = 2;
    sim::LandPlanner planner;
    const sim::LandCourse first = planner.plan(f.land, a);
    const sim::LandCourse other = planner.plan(f.land, b);
    const sim::LandCourse again = planner.plan(f.land, a);
    sim::LandPlanner fresh;
    const sim::LandCourse anew = fresh.plan(f.land, a);
    REQUIRE(first.reached_goal);
    REQUIRE(other.reached_goal);
    CHECK(first.path == again.path);
    CHECK(first.path == anew.path);
    CHECK(first.end_x == anew.end_x);
    CHECK(first.end_y == anew.end_y);
    CHECK(other.path == fresh.plan(f.land, b).path);
}

TEST_CASE("land paths: runs hold up to max_run cells of one walking cost") {
    Field f;
    for (std::int32_t y = 0; y < 16; ++y) {
        f.put(kSlow, 8, y);  // a strip of slow ground across the island: cells 24..25
    }
    sim::LandPlanner planner;
    sim::LandPlan plan = request(10, 24, 30, 24);
    const auto east = [](std::uint8_t cells) { return sim::path_code(2, cells); };
    plan.max_run = 1;
    sim::LandCourse course = planner.plan(f.land, plan);
    REQUIRE(course.reached_goal);
    CHECK(course.path == std::vector<std::uint8_t>(20, east(1)));
    plan.max_run = 0;  // at least one cell
    CHECK(planner.plan(f.land, plan).path == std::vector<std::uint8_t>(20, east(1)));
    plan.max_run = 4;
    course = planner.plan(f.land, plan);
    CHECK(course.path == (std::vector<std::uint8_t>{east(4), east(4), east(4), east(2), east(2), east(4)}));
    plan.max_run = 20;  // at most 15, what a path code holds
    course = planner.plan(f.land, plan);
    CHECK(course.path == (std::vector<std::uint8_t>{east(14), east(2), east(4)}));
    CHECK(course.end_x == 30);
    CHECK(course.end_y == 24);
    plan = request(10, 20, 40, 20);
    plan.max_run = 20;
    for (std::int32_t y = 0; y < 16; ++y) {
        f.put(kGround, 8, y);
    }
    CHECK(planner.plan(f.land, plan).path == (std::vector<std::uint8_t>{east(15), east(15)}));
}

TEST_CASE("land paths: an attack takes the first goal reached, a move the goal closest to the aim") {
    Field f;
    sim::LandPlanner planner;
    sim::LandPlan plan;
    plan.start_x = 20;
    plan.start_y = 20;
    plan.aim_x = 44;
    plan.aim_y = 20;
    plan.goal = [](std::int32_t x, std::int32_t y) { return (x == 24 && y == 20) || (x == 40 && y == 20); };
    plan.first_goal = true;
    sim::LandCourse course = planner.plan(f.land, plan);
    CHECK(course.reached_goal);
    CHECK(course.end_x == 24);
    plan.first_goal = false;
    course = planner.plan(f.land, plan);
    CHECK(course.reached_goal);
    CHECK(course.end_x == 40);  // goals are crossed on the way
    CHECK(course.path.size() == 20);
    // A goal that can only be reached around a wall still wins when it is the closest to the aim.
    for (std::int32_t y = 0; y < 15; ++y) {
        f.put(kForest, 14, y);  // cells 36..37, open only in the bottom tile row (cells 38..39)
    }
    course = planner.plan(f.land, plan);
    CHECK(course.reached_goal);
    CHECK(course.end_x == 40);
    CHECK(course.end_y == 20);
}

TEST_CASE("land paths: other figures' cells are blocked, the soldier's own cell is not") {
    Field f;
    // A wall of forest across the island with a gap of one tile (cells 28..29 x 22..23).
    for (std::int32_t y = 0; y < 16; ++y) {
        if (y != 7) {
            f.put(kForest, 10, y);
        }
    }
    sim::LandPlanner planner;
    sim::LandPlan plan = request(20, 22, 36, 22);
    sim::LandCourse course = planner.plan(f.land, plan);
    CHECK(course.reached_goal);
    plan.blocked = {{28, 22}};
    course = planner.plan(f.land, plan);
    REQUIRE(course.reached_goal);
    CHECK(trace(make_grid(f.land, plan), 20, 22, course.path, 1).valid);  // through (28, 23)
    plan.blocked = {{28, 22}, {28, 23}};
    course = planner.plan(f.land, plan);
    CHECK(course.path.empty());
    CHECK(!course.reached_goal);
    plan.blocked = {{20, 22}, {21, 22}};  // the soldier itself and one in front of it
    course = planner.plan(f.land, plan);
    REQUIRE(course.reached_goal);
    CHECK(trace(make_grid(f.land, plan), 20, 22, course.path, 1).valid);
    // A soldier standing in the forest (cells 28..29 outside the gap) walks out of it.
    plan = request(28, 10, 20, 10);
    course = planner.plan(f.land, plan);
    CHECK(course.reached_goal);
    CHECK(course.path.front() == sim::path_code(6, 1));
}

TEST_CASE("land paths: an enclosed goal has no way; a soldier at the goal is there already") {
    Field f;
    // A ring of forest round island tile (15, 10) (cells 38..39 x 28..29) with its north-west
    // corner missing: the gap is diagonal, between two forest tiles.
    for (std::int32_t y = 9; y <= 11; ++y) {
        for (std::int32_t x = 14; x <= 16; ++x) {
            if ((x != 15 || y != 10) && (x != 14 || y != 9)) {
                f.put(kForest, x, y);
            }
        }
    }
    sim::LandPlanner planner;
    sim::LandPlan plan = request(20, 20, 38, 28);
    plan.radius = 45;  // the edge of the window lies on the sea
    sim::LandCourse course = planner.plan(f.land, plan);
    CHECK(course.path.empty());
    CHECK(!course.reached_goal);
    plan = request(20, 20, 37, 27);  // the corner itself is open
    course = planner.plan(f.land, plan);
    CHECK(course.reached_goal);
    plan = request(20, 20, 20, 20);
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 20 && y == 20; };
    course = planner.plan(f.land, plan);
    CHECK(course.reached_goal);
    CHECK(course.path.empty());
    CHECK(course.end_x == 20);
    CHECK(course.end_y == 20);
}

TEST_CASE("land paths: a target beyond the window gives a leg to its edge; shifts move the window") {
    Field f;
    sim::LandPlanner planner;
    sim::LandPlan plan = request(20, 20, 50, 20);
    plan.radius = 5;
    sim::LandCourse course = planner.plan(f.land, plan);
    CHECK(!course.reached_goal);
    CHECK(course.end_x == 25);
    CHECK(course.end_y == 20);
    CHECK(course.path == std::vector<std::uint8_t>(5, sim::path_code(2, 1)));
    plan.shift_x = 3;
    course = planner.plan(f.land, plan);
    CHECK(!course.reached_goal);
    CHECK(course.end_x == 28);
    plan.shift_x = 6;  // the soldier is no longer in the window
    CHECK(planner.plan(f.land, plan).path.empty());
    plan.shift_x = 0;
    plan.radius = 0;
    CHECK(planner.plan(f.land, plan).path.empty());
    // Off the island's west coast the window holds no walkable edge cell closer to the aim.
    plan = request(9, 20, -30, 20);
    plan.radius = 5;
    course = planner.plan(f.land, plan);
    CHECK(course.path.empty());
    CHECK(!course.reached_goal);
}

TEST_CASE("land paths: a long way stops after 128 runs and goes on from its end") {
    Field f(100, 12, 2, 2, 90, 6);  // cells 4..183 x 4..15
    sim::LandPlanner planner;
    sim::LandPlan plan = request(6, 6, 180, 6);
    plan.radius = 180;
    sim::LandCourse course = planner.plan(f.land, plan);
    CHECK(course.path.size() == sim::LandPlanner::kMaxRuns);
    CHECK(!course.reached_goal);
    CHECK(course.end_x == 6 + 128);
    plan.start_x = course.end_x;
    course = planner.plan(f.land, plan);
    CHECK(course.reached_goal);
    CHECK(course.path.size() == 46);
    CHECK(course.end_x == 180);
}
