// SPDX-License-Identifier: GPL-3.0-or-later
// Courses of ships: SeaMap and SeaPlanner.

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
#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kWater = 20102;  // sea inside an island's rectangle
constexpr std::uint32_t kPier = 20103;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20102\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20103\n  Kind: 30\n  Gfx: 2\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// Adds an island of ground at world tile (x, y) with water tiles at the given island tiles.
void add_island(sim::World& world, const assets::BuildingTable& buildings, std::uint32_t x, std::uint32_t y,
                std::uint32_t w, std::uint32_t h,
                const std::vector<std::pair<std::int32_t, std::int32_t>>& water = {}) {
    sim::IslandMap map(w, h);
    for (std::uint32_t ly = 0; ly < h; ++ly) {
        for (std::uint32_t lx = 0; lx < w; ++lx) {
            REQUIRE(
                map.place(*buildings.by_id(kGround), static_cast<std::int32_t>(lx), static_cast<std::int32_t>(ly), 0, 0)
                    .ok());
        }
    }
    for (const auto& [lx, ly] : water) {
        REQUIRE(map.place(*buildings.by_id(kWater), lx, ly, 0, 0).ok());
    }
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), x, y}).ok());
}

/// Small deterministic generator for the test worlds.
struct Lcg {
    std::uint32_t state;
    std::uint32_t next(std::uint32_t bound) {
        state = state * 1664525U + 1013904223U;
        return (state >> 8U) % bound;
    }
};

/// The planner's window as the test sees it, built independently from the request: blocked
/// cells and cost classes (0 sea, 1 around a standing ship, 2 near hostile towers).
struct Grid {
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t size = 0;
    std::vector<std::uint8_t> blocked;
    std::vector<std::uint8_t> cls;

    [[nodiscard]] bool inside(std::int32_t x, std::int32_t y) const {
        return x >= x0 && y >= y0 && x < x0 + size && y < y0 + size;
    }
    [[nodiscard]] std::size_t at(std::int32_t x, std::int32_t y) const {
        return static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x - x0);
    }
    [[nodiscard]] bool open(std::int32_t x, std::int32_t y) const { return inside(x, y) && blocked[at(x, y)] == 0; }
    [[nodiscard]] std::uint32_t step_cost(std::int32_t x, std::int32_t y, bool diagonal) const {
        constexpr std::uint32_t kCost[3][2] = {{64, 91}, {500, 711}, {504, 716}};
        return kCost[cls[at(x, y)]][diagonal ? 1 : 0];
    }
};

Grid make_grid(const sim::SeaMap& sea, const sim::SeaPlan& r) {
    Grid g;
    g.size = 2 * r.radius + 1;
    g.x0 = r.start_x - r.radius + r.shift_x;
    g.y0 = r.start_y - r.radius + r.shift_y;
    const auto cells = static_cast<std::size_t>(g.size) * static_cast<std::size_t>(g.size);
    g.blocked.assign(cells, 0);
    g.cls.assign(cells, 0);
    for (std::int32_t y = g.y0; y < g.y0 + g.size; ++y) {
        for (std::int32_t x = g.x0; x < g.x0 + g.size; ++x) {
            g.blocked[g.at(x, y)] = sea.sailable(x, y) ? 0 : 1;
            for (const auto& d : r.danger) {
                if (x >= d.x0 && x <= d.x1 && y >= d.y0 && y <= d.y1) {
                    g.cls[g.at(x, y)] = 2;
                }
            }
        }
    }
    for (const auto& o : r.ships) {
        if (o.x == r.start_x && o.y == r.start_y) {
            continue;
        }
        for (std::int32_t y = o.y - 1; y <= o.y + 1; ++y) {
            for (std::int32_t x = o.x - 1; x <= o.x + 1; ++x) {
                if (!g.inside(x, y)) {
                    continue;
                }
                if (r.soft_ships && !o.moving && (x != o.x || y != o.y)) {
                    g.cls[g.at(x, y)] = 1;
                } else {
                    g.blocked[g.at(x, y)] = 1;
                }
            }
        }
    }
    if (g.inside(r.start_x, r.start_y)) {
        g.blocked[g.at(r.start_x, r.start_y)] = 0;
    }
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

/// Follows a course over the grid; checks every run and step and adds up the cost.
struct Trace {
    bool valid = true;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t cost = 0;
};

Trace trace(const Grid& g, std::int32_t x, std::int32_t y, const std::vector<std::uint8_t>& path) {
    Trace t{true, x, y, 0};
    for (const std::uint8_t code : path) {
        const auto dir = static_cast<std::uint8_t>(code >> 4U);
        const std::uint32_t tiles = code & 0x0FU;
        if (dir > 7 || tiles < 1 || tiles > sim::SeaPlanner::kMaxRunTiles) {
            t.valid = false;
            return t;
        }
        const std::uint8_t run_class = g.cls[g.at(t.x, t.y)];
        for (std::uint32_t i = 0; i < tiles; ++i) {
            const std::int32_t nx = t.x + sim::step_x(dir);
            const std::int32_t ny = t.y + sim::step_y(dir);
            const bool diagonal = (dir & 1U) != 0;
            // On open cells, no corner cut, and one cost class for all tiles a run leaves.
            if (!g.open(nx, ny) || (diagonal && (!g.open(nx, t.y) || !g.open(t.x, ny))) ||
                g.cls[g.at(t.x, t.y)] != run_class) {
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

/// A world of 48 x 48 tiles with up to 16 islands of random size, some touching diagonally, with
/// lakes and bays of island water.
sim::World random_world(const assets::BuildingTable& buildings, std::uint32_t seed) {
    sim::World world{48, 48};
    Lcg rng{seed};
    for (std::uint32_t by = 0; by < 4; ++by) {
        for (std::uint32_t bx = 0; bx < 4; ++bx) {
            if (rng.next(4) == 0) {
                continue;
            }
            const std::uint32_t w = 2 + rng.next(9);
            const std::uint32_t h = 2 + rng.next(9);
            const std::uint32_t x = bx * 12 + rng.next(12 - w + 1);
            const std::uint32_t y = by * 12 + rng.next(12 - h + 1);
            std::vector<std::pair<std::int32_t, std::int32_t>> water;
            for (std::uint32_t ly = 0; ly < h; ++ly) {
                for (std::uint32_t lx = 0; lx < w; ++lx) {
                    if (rng.next(6) == 0) {
                        water.emplace_back(lx, ly);
                    }
                }
            }
            add_island(world, buildings, x, y, w, h, water);
        }
    }
    return world;
}

}  // namespace

TEST_CASE("sea paths: the distance is the longer axis plus a quarter of the shorter one") {
    static_assert(sim::sea_distance(0, 0) == 0);
    static_assert(sim::sea_distance(4, 4) == 5);
    CHECK(sim::sea_distance(8, 0) == 8);
    CHECK(sim::sea_distance(0, -8) == 8);
    CHECK(sim::sea_distance(-8, 3) == 8);  // 3 / 4 rounds down to nothing
    CHECK(sim::sea_distance(3, -7) == 7);
    CHECK(sim::sea_distance(-12, -9) == 14);
    CHECK(sim::sea_distance(5, 3) == sim::sea_distance(3, 5));
}

TEST_CASE("sea paths: path codes hold direction and tiles; the eight steps go round the compass") {
    static_assert(sim::path_code(5, 2) == 0x52);
    CHECK(sim::path_code(0, 1) == 0x01);
    CHECK(sim::path_code(7, 15) == 0x7F);
    CHECK(sim::path_code(1, 0x12) == 0x12);  // tiles keep to the low four bits
    // 0 = -y, 2 = +x, 4 = +y, 6 = -x, the odd directions between them.
    const std::int32_t xs[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    const std::int32_t ys[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    for (std::uint8_t d = 0; d < 8; ++d) {
        CHECK(sim::step_x(d) == xs[d]);
        CHECK(sim::step_y(d) == ys[d]);
        const auto back = static_cast<std::uint8_t>((d + 4U) & 7U);
        CHECK(sim::step_x(back) == -sim::step_x(d));
        CHECK(sim::step_y(back) == -sim::step_y(d));
        CHECK(((d & 1U) != 0) == (sim::step_x(d) != 0 && sim::step_y(d) != 0));
        CHECK(sim::step_x(static_cast<std::uint8_t>(d + 8)) == sim::step_x(d));  // only the low three bits count
    }
}

TEST_CASE("sea paths: open sea and island water are sailable, land, piers and the outside not") {
    const auto buildings = make_buildings();
    CHECK(sim::sailable_building(*buildings.by_id(kWater)));
    CHECK(!sim::sailable_building(*buildings.by_id(kGround)));
    CHECK(!sim::sailable_building(*buildings.by_id(kPier)));
    sim::SeaMap empty;
    CHECK(empty.width() == 0);
    CHECK(!empty.sailable(0, 0));

    sim::World world{20, 15};
    add_island(world, buildings, 5, 5, 4, 3, {{1, 1}});
    REQUIRE(world.island(0).map.place(*buildings.by_id(kPier), 2, 1, 0, 0).ok());
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    CHECK(sea.width() == 20);
    CHECK(sea.height() == 15);
    CHECK(sea.sailable(4, 5));  // open sea
    CHECK(sea.sailable(9, 7));
    CHECK(!sea.sailable(5, 5));  // ground
    CHECK(sea.sailable(6, 6));   // water inside the island
    CHECK(!sea.sailable(7, 6));  // pier
    CHECK(!sea.sailable(-1, 0));
    CHECK(!sea.sailable(0, -1));
    CHECK(!sea.sailable(20, 0));
    CHECK(!sea.sailable(0, 15));
    CHECK(sea.sailable(19, 14));

    // Building on the water shows only once the island is updated.
    REQUIRE(world.island(0).map.place(*buildings.by_id(kGround), 1, 1, 0, 0).ok());
    CHECK(sea.sailable(6, 6));
    sea.update_island(world, buildings, 7);  // no such island: nothing happens
    CHECK(sea.sailable(6, 6));
    sea.update_island(world, buildings, 0);
    CHECK(!sea.sailable(6, 6));
    CHECK(sea.sailable(4, 5));

    // A box of the island updates only its tiles (TB-19); boxes reaching past the island are clipped.
    REQUIRE(world.island(0).map.place(*buildings.by_id(kWater), 2, 1, 0, 0).ok());
    REQUIRE(world.island(0).map.place(*buildings.by_id(kWater), 1, 1, 0, 0).ok());
    sea.update_island(world, buildings, 0, 2, 0, 3, 2);
    CHECK(sea.sailable(7, 6));   // the pier's tile is water now
    CHECK(!sea.sailable(6, 6));  // outside the box: not looked at
    sea.update_island(world, buildings, 0, -5, -5, 1, 1);
    CHECK(sea.sailable(6, 6));
    sea.update_island(world, buildings, 0, 10, 10, 20, 20);  // beyond the island: nothing
    sea.update_island(world, buildings, 0, -3, -3, -1, -1);
    CHECK(sea.sailable(4, 5));
}

TEST_CASE("sea paths: courses are as cheap as a reference Dijkstra finds, on sailable tiles only") {
    const auto buildings = make_buildings();
    std::size_t reached = 0;
    std::size_t unreachable = 0;
    for (std::uint32_t seed = 1; seed <= 5; ++seed) {
        const sim::World world = random_world(buildings, seed);
        sim::SeaMap sea;
        sea.rebuild(world, buildings);
        sim::SeaPlanner planner;
        Lcg rng{seed * 7919U};
        for (int round = 0; round < 40; ++round) {
            sim::SeaPlan plan;
            do {
                plan.start_x = static_cast<std::int32_t>(rng.next(48));
                plan.start_y = static_cast<std::int32_t>(rng.next(48));
            } while (!sea.sailable(plan.start_x, plan.start_y));
            plan.radius = 20 + static_cast<std::int32_t>(rng.next(20));
            std::int32_t gx = 0;
            std::int32_t gy = 0;
            do {
                gx = plan.start_x - plan.radius +
                     static_cast<std::int32_t>(rng.next(static_cast<std::uint32_t>(2 * plan.radius + 1)));
                gy = plan.start_y - plan.radius +
                     static_cast<std::int32_t>(rng.next(static_cast<std::uint32_t>(2 * plan.radius + 1)));
            } while (!sea.sailable(gx, gy) || (gx == plan.start_x && gy == plan.start_y));
            plan.aim_x = gx;
            plan.aim_y = gy;
            plan.goal = [gx, gy](std::int32_t x, std::int32_t y) { return x == gx && y == gy; };
            // Every other round with ships around, some lying still, and water near towers.
            if (round % 2 == 1) {
                for (std::uint32_t i = rng.next(4); i > 0; --i) {
                    plan.ships.push_back({static_cast<std::int32_t>(rng.next(48)),
                                          static_cast<std::int32_t>(rng.next(48)), rng.next(2) == 0});
                }
                plan.soft_ships = rng.next(2) == 0;
                const auto x = static_cast<std::int32_t>(rng.next(40));
                const auto y = static_cast<std::int32_t>(rng.next(40));
                plan.danger.push_back(
                    {x, y, x + static_cast<std::int32_t>(rng.next(12)), y + static_cast<std::int32_t>(rng.next(12))});
            }
            const Grid grid = make_grid(sea, plan);
            const auto expected = reference_cost(grid, plan.start_x, plan.start_y, plan.goal);
            const sim::SeaCourse course = planner.plan(sea, plan);
            if (!expected) {
                CHECK(course.path.empty());
                CHECK(!course.reached_goal);
                ++unreachable;
                continue;
            }
            REQUIRE(course.reached_goal);
            REQUIRE(course.path.size() <= sim::SeaPlanner::kMaxRuns);
            const Trace t = trace(grid, plan.start_x, plan.start_y, course.path);
            CHECK(t.valid);
            CHECK(t.x == gx);
            CHECK(t.y == gy);
            CHECK(course.end_x == gx);
            CHECK(course.end_y == gy);
            CHECK(t.cost == *expected);
            ++reached;
        }
    }
    // The worlds give both kinds of request.
    CHECK(reached > 100);
    CHECK(unreachable > 0);
}

TEST_CASE("sea paths: the same request gives the same course on a reused and on a fresh planner") {
    const auto buildings = make_buildings();
    const sim::World world = random_world(buildings, 3);
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    const auto request = [](std::int32_t sx, std::int32_t sy, std::int32_t gx, std::int32_t gy) {
        sim::SeaPlan plan;
        plan.start_x = sx;
        plan.start_y = sy;
        plan.radius = 40;
        plan.aim_x = gx;
        plan.aim_y = gy;
        plan.goal = [gx, gy](std::int32_t x, std::int32_t y) { return sim::sea_distance(x - gx, y - gy) <= 1; };
        return plan;
    };
    // Four sailable tiles spread over the world.
    std::vector<std::pair<std::int32_t, std::int32_t>> spots;
    for (std::int32_t y = 0; y < 48 && spots.size() < 4; y += 5) {
        for (std::int32_t x = (y * 7) % 48; x < 48 && spots.size() < 4; x += 11) {
            if (sea.sailable(x, y)) {
                spots.emplace_back(x, y);
            }
        }
    }
    REQUIRE(spots.size() == 4);
    const sim::SeaPlan a = request(spots[0].first, spots[0].second, spots[3].first, spots[3].second);
    const sim::SeaPlan b = request(spots[1].first, spots[1].second, spots[2].first, spots[2].second);
    sim::SeaPlanner planner;
    const sim::SeaCourse first = planner.plan(sea, a);
    const sim::SeaCourse other = planner.plan(sea, b);
    const sim::SeaCourse again = planner.plan(sea, a);
    sim::SeaPlanner fresh;
    const sim::SeaCourse anew = fresh.plan(sea, a);
    CHECK(first.path == again.path);
    CHECK(first.path == anew.path);
    CHECK(first.reached_goal == again.reached_goal);
    CHECK(first.end_x == anew.end_x);
    CHECK(first.end_y == anew.end_y);
    CHECK(other.path == fresh.plan(sea, b).path);
}

TEST_CASE("sea paths: a lake, or water behind a diagonal gap between land, cannot be reached") {
    const auto buildings = make_buildings();
    sim::World world{30, 30};
    // A 3 x 3 island whose north-west corner tile and centre tile are water: they touch only
    // diagonally, between two land tiles.
    add_island(world, buildings, 10, 10, 3, 3, {{0, 0}, {1, 1}});
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    sim::SeaPlanner planner;
    sim::SeaPlan plan;
    plan.start_x = 5;
    plan.start_y = 5;
    plan.radius = 20;
    plan.aim_x = 10;
    plan.aim_y = 10;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 10 && y == 10; };
    sim::SeaCourse course = planner.plan(sea, plan);
    CHECK(course.reached_goal);  // the corner is open to the sea
    CHECK(course.end_x == 10);
    CHECK(course.end_y == 10);
    plan.aim_x = 11;
    plan.aim_y = 11;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 11 && y == 11; };
    course = planner.plan(sea, plan);
    CHECK(course.path.empty());
    CHECK(!course.reached_goal);
    // Nor out of it: a ship in the lake has no way to the sea.
    plan.start_x = 11;
    plan.start_y = 11;
    plan.aim_x = 5;
    plan.aim_y = 5;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 5 && y == 5; };
    course = planner.plan(sea, plan);
    CHECK(course.path.empty());
    CHECK(!course.reached_goal);
}

TEST_CASE("sea paths: a ship at its goal is there already; its own tile is always open") {
    const auto buildings = make_buildings();
    sim::World world{30, 30};
    add_island(world, buildings, 10, 10, 5, 5);
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    sim::SeaPlanner planner;
    sim::SeaPlan plan;
    plan.start_x = 3;
    plan.start_y = 4;
    plan.radius = 10;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 3 && y == 4; };
    sim::SeaCourse course = planner.plan(sea, plan);
    CHECK(course.reached_goal);
    CHECK(course.path.empty());
    CHECK(course.end_x == 3);
    CHECK(course.end_y == 4);
    // A ship stuck on the coast (land built under it) still sails off.
    plan.start_x = 10;
    plan.start_y = 12;
    plan.aim_x = 6;
    plan.aim_y = 12;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 6 && y == 12; };
    course = planner.plan(sea, plan);
    CHECK(course.reached_goal);
    CHECK(course.path == (std::vector<std::uint8_t>{sim::path_code(6, 2), sim::path_code(6, 2)}));
    // Without a goal test and with the aim in the window there is nowhere to go.
    plan.goal = nullptr;
    course = planner.plan(sea, plan);
    CHECK(course.path.empty());
    CHECK(!course.reached_goal);
}

TEST_CASE("sea paths: other ships block 3 x 3 squares; one lying still only its own tile when soft") {
    const auto buildings = make_buildings();
    // Two islands from the north and south edges leave a strait of three tiles (rows 9..11).
    sim::World world{30, 21};
    add_island(world, buildings, 10, 0, 10, 9);
    add_island(world, buildings, 10, 12, 10, 9);
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    sim::SeaPlanner planner;
    sim::SeaPlan plan;
    plan.start_x = 3;
    plan.start_y = 10;
    plan.radius = 30;
    plan.aim_x = 26;
    plan.aim_y = 10;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 26 && y == 10; };
    sim::SeaCourse course = planner.plan(sea, plan);
    REQUIRE(course.reached_goal);
    CHECK(course.path.size() == 12);  // 23 tiles straight east

    // A ship in the middle of the strait closes it.
    plan.ships = {{15, 10, false}};
    course = planner.plan(sea, plan);
    CHECK(course.path.empty());
    CHECK(!course.reached_goal);
    // After a failure the planner softens the squares of ships lying still, not of moving ones.
    plan.soft_ships = true;
    plan.ships = {{15, 10, true}};
    course = planner.plan(sea, plan);
    CHECK(!course.reached_goal);
    plan.ships = {{15, 10, false}};
    course = planner.plan(sea, plan);
    REQUIRE(course.reached_goal);
    const Grid grid = make_grid(sea, plan);
    const Trace t = trace(grid, plan.start_x, plan.start_y, course.path);
    CHECK(t.valid);  // never on the ship's own tile
    CHECK(t.x == 26);
    CHECK(t.cost == reference_cost(grid, plan.start_x, plan.start_y, plan.goal));

    // A ship on the planning ship's own tile is the ship itself; one next to it leaves its tile open.
    plan.soft_ships = false;
    plan.ships = {{3, 10, false}};
    CHECK(planner.plan(sea, plan).reached_goal);
    plan.ships = {{4, 10, false}};
    course = planner.plan(sea, plan);
    REQUIRE(course.reached_goal);
    CHECK((course.path.front() >> 4U) == 6);  // out to the west first, round the square
}

TEST_CASE("sea paths: water near hostile towers costs about eight times as much, runs end at its border") {
    const auto buildings = make_buildings();
    sim::World world{30, 21};
    add_island(world, buildings, 10, 0, 10, 9);
    add_island(world, buildings, 10, 12, 10, 9);
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    sim::SeaPlanner planner;
    sim::SeaPlan plan;
    plan.start_x = 3;
    plan.start_y = 10;
    plan.radius = 30;
    plan.aim_x = 26;
    plan.aim_y = 10;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 26 && y == 10; };
    // The whole strait is within reach of towers: the only way leads through it anyway.
    plan.danger = {{12, 0, 16, 20}};
    const sim::SeaCourse course = planner.plan(sea, plan);
    REQUIRE(course.reached_goal);
    const Grid grid = make_grid(sea, plan);
    const Trace t = trace(grid, plan.start_x, plan.start_y, course.path);
    CHECK(t.valid);
    CHECK(t.cost == 18U * 64U + 5U * 504U);  // 18 tiles left on open sea, 5 near the towers
    // Runs: 3..11 in two-tile runs (one left over at 11), 12..16 the same, then 17..25.
    const std::vector<std::uint8_t> expected = {
        sim::path_code(2, 2), sim::path_code(2, 2), sim::path_code(2, 2), sim::path_code(2, 2), sim::path_code(2, 1),
        sim::path_code(2, 2), sim::path_code(2, 2), sim::path_code(2, 1), sim::path_code(2, 2), sim::path_code(2, 2),
        sim::path_code(2, 2), sim::path_code(2, 2), sim::path_code(2, 1)};
    CHECK(course.path == expected);
}

TEST_CASE("sea paths: a long course stops after 99 runs and goes on from its end") {
    const auto buildings = make_buildings();
    sim::World world{300, 10};
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    sim::SeaPlanner planner;
    sim::SeaPlan plan;
    plan.start_x = 5;
    plan.start_y = 5;
    plan.radius = 290;
    plan.aim_x = 290;
    plan.aim_y = 5;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 290 && y == 5; };
    sim::SeaCourse course = planner.plan(sea, plan);
    CHECK(course.path.size() == sim::SeaPlanner::kMaxRuns);
    CHECK(!course.reached_goal);  // truncated: the ship plans again at the end
    CHECK(course.end_x == 5 + 2 * 99);
    CHECK(course.end_y == 5);
    plan.start_x = course.end_x;
    plan.start_y = course.end_y;
    course = planner.plan(sea, plan);
    CHECK(course.reached_goal);
    CHECK(course.path.size() == 44);  // the other 87 tiles
    CHECK(course.end_x == 290);
}

TEST_CASE("sea paths: a goal beyond the window gives a leg to the edge cell closest to the aim") {
    const auto buildings = make_buildings();
    sim::World world{120, 100};
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    sim::SeaPlanner planner;
    sim::SeaPlan plan;
    plan.start_x = 50;
    plan.start_y = 50;
    plan.radius = 10;
    plan.aim_x = 100;
    plan.aim_y = 63;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 100 && y == 63; };
    sim::SeaCourse course = planner.plan(sea, plan);
    REQUIRE(!course.path.empty());
    CHECK(!course.reached_goal);
    CHECK(course.end_x == 60);
    // No other cell of the window's edge is closer to the aim.
    std::int32_t best = 0x7FFFFFFF;
    for (std::int32_t y = 40; y <= 60; ++y) {
        best = std::min(best, sim::sea_distance(plan.aim_x - 60, plan.aim_y - y));
    }
    CHECK(sim::sea_distance(plan.aim_x - course.end_x, plan.aim_y - course.end_y) == best);
    const Grid grid = make_grid(sea, plan);
    const Trace t = trace(grid, plan.start_x, plan.start_y, course.path);
    CHECK(t.valid);
    CHECK(t.x == course.end_x);
    CHECK(t.y == course.end_y);

    // A shifted window has its edge further on; a start outside the window or no window plans nothing.
    plan.shift_x = 5;
    course = planner.plan(sea, plan);
    CHECK(course.end_x == 65);
    plan.shift_x = 11;
    CHECK(planner.plan(sea, plan).path.empty());
    plan.shift_x = 0;
    plan.radius = 0;
    CHECK(planner.plan(sea, plan).path.empty());
}

TEST_CASE("sea paths: cells outside the world are blocked, also for legs to the window edge") {
    const auto buildings = make_buildings();
    sim::World world{40, 40};
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    sim::SeaPlanner planner;
    sim::SeaPlan plan;
    plan.start_x = 1;
    plan.start_y = 1;
    plan.radius = 5;
    // The edge cells closer to the aim than the ship all lie outside the world.
    plan.aim_x = -50;
    plan.aim_y = -50;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == -50 && y == -50; };
    sim::SeaCourse course = planner.plan(sea, plan);
    CHECK(course.path.empty());
    CHECK(!course.reached_goal);
    // Towards an aim beyond the west edge of the world the leg runs along it.
    plan.start_y = 20;
    plan.aim_x = -50;
    plan.aim_y = 30;
    course = planner.plan(sea, plan);
    REQUIRE(!course.path.empty());
    CHECK(course.end_y == 25);  // the south edge of the window
    CHECK(course.end_x >= 0);
    const Grid grid = make_grid(sea, plan);
    CHECK(trace(grid, plan.start_x, plan.start_y, course.path).valid);
}

namespace {

/// Sails leg after leg from the plan's start towards its goal; the ship plans again at the end of
/// each leg, as the fleet does.
struct Voyage {
    bool arrived = false;
    std::size_t legs = 0;
    std::size_t tiles = 0;
};

Voyage sail(const sim::SeaMap& sea, sim::SeaPlan plan, std::size_t max_legs) {
    sim::SeaPlanner planner;
    Voyage v;
    while (v.legs < max_legs) {
        const sim::SeaCourse course = planner.plan(sea, plan);
        ++v.legs;
        if (course.path.empty()) {
            return v;  // no way: the ship would give up or shift its window
        }
        for (const std::uint8_t code : course.path) {
            v.tiles += code & 0x0FU;
        }
        if (course.reached_goal) {
            v.arrived = true;
            return v;
        }
        plan.start_x = course.end_x;
        plan.start_y = course.end_y;
    }
    return v;
}

}  // namespace

TEST_CASE("sea paths: water regions of a block are the parts a ship can sail between inside it") {
    const auto buildings = make_buildings();
    sim::World world{70, 30};
    // A wall of land across the first row of blocks with a gap at x = 20; the second island is a
    // pond of one tile in the middle of land, touching other water only diagonally.
    add_island(world, buildings, 0, 10, 40, 3, {{20, 0}, {20, 1}, {20, 2}});
    add_island(world, buildings, 48, 4, 3, 3, {{1, 1}});
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    CHECK(sea.blocks_x() == 5);  // 70 tiles: four blocks and one of 6 tiles
    CHECK(sea.blocks_y() == 2);
    CHECK(sea.region(5, 5) != sim::SeaMap::kNoRegion);
    CHECK(sea.region(5, 11) == sim::SeaMap::kNoRegion);  // land
    CHECK(sea.region(-1, 0) == sim::SeaMap::kNoRegion);  // outside
    CHECK(sea.region(70, 29) == sim::SeaMap::kNoRegion);
    CHECK(sea.region(5, 5) != sea.region(5, 14));         // first block: the wall splits it
    CHECK(sea.region(20, 5) == sea.region(20, 14));       // second block: joined by the gap
    CHECK(sea.region(49, 5) != sea.region(47, 5));        // the pond is a region of its own
    CHECK(sea.region(69, 29) != sim::SeaMap::kNoRegion);  // the narrow last block has regions too
    CHECK(sea.region_block(sea.region(69, 29)) == 9U);
    const std::uint32_t regions = sea.regions();

    // Closing the gap splits the block; opening it again joins it; numbers stay as a rebuild gives.
    REQUIRE(world.island(0).map.place(*buildings.by_id(kGround), 20, 1, 0, 0).ok());
    sea.update_island(world, buildings, 0, 20, 1, 20, 1);
    CHECK(sea.region(20, 5) != sea.region(20, 14));
    CHECK(sea.regions() == regions + 1);
    sim::SeaMap fresh;
    fresh.rebuild(world, buildings);
    for (std::int32_t y = 0; y < 30; ++y) {
        for (std::int32_t x = 0; x < 70; ++x) {
            CHECK(sea.region(x, y) == fresh.region(x, y));
        }
    }
    REQUIRE(world.island(0).map.place(*buildings.by_id(kWater), 20, 1, 0, 0).ok());
    sea.update_island(world, buildings, 0);
    CHECK(sea.region(20, 5) == sea.region(20, 14));
    CHECK(sea.regions() == regions);
}

TEST_CASE("sea paths: a guided ship sails round an island wider than its window, not into its bay") {
    const auto buildings = make_buildings();
    sim::World world{420, 300};
    // An island of 220 x 220 tiles between ship and goal with a bay of 160 x 60 tiles opening
    // towards the ship, right on the straight line to the goal.
    std::vector<std::pair<std::int32_t, std::int32_t>> bay;
    for (std::int32_t y = 80; y < 140; ++y) {
        for (std::int32_t x = 0; x < 160; ++x) {
            bay.emplace_back(x, y);
        }
    }
    add_island(world, buildings, 120, 40, 220, 220, bay);
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    sim::SeaPlan plan;
    plan.start_x = 40;
    plan.start_y = 150;
    plan.radius = 80;
    plan.aim_x = 380;
    plan.aim_y = 150;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 380 && y == 150; };

    // Heading for the window's edge closest to the goal, the ship ends up in the bay.
    const Voyage unguided = sail(sea, plan, 30);
    CHECK(!unguided.arrived);

    plan.guided = true;
    const Voyage guided = sail(sea, plan, 30);
    CHECK(guided.arrived);
    CHECK(guided.legs <= 8);
    // Round a corner of the island: about 260 tiles there and as many back, never into the bay.
    CHECK(guided.tiles < 560);

    // From the far side as well, the same way round.
    plan.start_x = 380;
    plan.start_y = 150;
    plan.aim_x = 40;
    plan.aim_y = 150;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 40 && y == 150; };
    const Voyage back = sail(sea, plan, 30);
    CHECK(back.arrived);
    CHECK(back.tiles < 560);
}

TEST_CASE("sea paths: a guided ship finds the end of a long thin peninsula, across blocks it splits") {
    const auto buildings = make_buildings();
    sim::World world{400, 300};
    // A wall of land one tile wide at x = 203, in the middle of blocks, from the north edge to
    // y = 280: the target lies 100 tiles east of the ship as the crow flies.
    add_island(world, buildings, 203, 0, 1, 281);
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    CHECK(sea.region(202, 150) != sea.region(204, 150));
    sim::SeaPlan plan;
    plan.start_x = 150;
    plan.start_y = 150;
    plan.radius = 80;
    plan.aim_x = 250;
    plan.aim_y = 150;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 250 && y == 150; };
    CHECK(!sail(sea, plan, 30).arrived);
    plan.guided = true;
    const Voyage v = sail(sea, plan, 30);
    CHECK(v.arrived);
    CHECK(v.tiles < 400);  // round the southern end: about 2 x 135 tiles plus the way across

    // A goal no water leads to: no route, and a guided plan is the same as an unguided one.
    add_island(world, buildings, 300, 100, 30, 30, {{15, 15}});
    sea.rebuild(world, buildings);
    plan.start_x = 250;
    plan.start_y = 200;
    plan.aim_x = 315;
    plan.aim_y = 115;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 315 && y == 115; };
    sim::SeaPlanner planner;
    const sim::SeaCourse guided = planner.plan(sea, plan);
    plan.guided = false;
    const sim::SeaCourse unguided = planner.plan(sea, plan);
    CHECK(guided.path == unguided.path);
    CHECK(!guided.reached_goal);
}

TEST_CASE("sea paths: guided courses are the same on a reused and on a fresh planner") {
    const auto buildings = make_buildings();
    sim::World world{300, 300};
    Lcg rng{7};
    for (std::uint32_t i = 0; i < 6; ++i) {  // one in each cell of 100 x 150 tiles
        add_island(world, buildings, (i % 3) * 100 + 10 + rng.next(20), (i / 3) * 150 + 10 + rng.next(40),
                   10 + rng.next(60), 10 + rng.next(80));
    }
    sim::SeaMap sea;
    sea.rebuild(world, buildings);
    sim::SeaPlanner reused;
    for (int i = 0; i < 40; ++i) {
        sim::SeaPlan plan;
        plan.start_x = static_cast<std::int32_t>(rng.next(300));
        plan.start_y = static_cast<std::int32_t>(rng.next(300));
        plan.radius = 40;
        plan.aim_x = static_cast<std::int32_t>(rng.next(300));
        plan.aim_y = static_cast<std::int32_t>(rng.next(300));
        plan.guided = true;
        const std::int32_t gx = plan.aim_x;
        const std::int32_t gy = plan.aim_y;
        plan.goal = [gx, gy](std::int32_t x, std::int32_t y) { return x == gx && y == gy; };
        sim::SeaPlanner fresh;
        const sim::SeaCourse a = reused.plan(sea, plan);
        const sim::SeaCourse b = fresh.plan(sea, plan);
        CHECK(a.path == b.path);
        CHECK(a.reached_goal == b.reached_goal);
        CHECK(a.end_x == b.end_x);
        CHECK(a.end_y == b.end_y);
        if (!a.path.empty()) {
            CHECK(trace(make_grid(sea, plan), plan.start_x, plan.start_y, a.path).valid);
        }
    }
}
