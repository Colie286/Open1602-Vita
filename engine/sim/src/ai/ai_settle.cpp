// SPDX-License-Identifier: GPL-3.0-or-later
//
// Founding cities (docs/re/ai.md, "Settling"): the island search (0x00416370), the free land
// test (task 2), the Kontor site and the free cargo (task 3), the landing (task 4), later colonies
// (0x00426290) and giving a city up (task 0x2E).
#include <algorithm>
#include <array>
#include <limits>

#include "ai_engine.hpp"
#include "ai_tables.hpp"

namespace o1602::sim::ai {

namespace {

constexpr std::uint64_t kColonyCooldownMs = 60'000;  // C+0x30: 60 s between settlement events
constexpr std::int32_t kNeedStep = 50;
constexpr std::uint32_t kPirateRegionPasses = 6;  // passes a further nest keeps to a part without one
constexpr std::uint8_t kLandRetries = 4;
constexpr std::uint32_t kReach = 5;       // a ship within 5 tiles of a Kontor lies at it
constexpr std::uint32_t kLandRange = 12;  // founding: the ship within 12 tiles of the site

bool in_circle(std::int32_t dx, std::int32_t dy, std::uint32_t r) {
    static const std::vector<std::uint8_t> kNear = circle_half_widths(kReach);
    static const std::vector<std::uint8_t> kLand = circle_half_widths(kLandRange);
    const std::vector<std::uint8_t>& circle = r == kLandRange ? kLand : kNear;
    const std::int32_t ax = dx < 0 ? -dx : dx;
    const std::int32_t ay = dy < 0 ? -dy : dy;
    return static_cast<std::uint32_t>(ay) <= r && ax <= circle[static_cast<std::size_t>(ay)];
}

/// Land a settlement may claim (task 2): forest, ground, ruins, slopes.
bool settle_ground(std::int32_t k) {
    return k == kind::kForest || k == kind::kGround || k == kind::kRuin || k == kind::kSlope || k == kind::kSlopeCorner;
}

bool beach_kind(std::int32_t k) {
    return k == kind::kBeach || k == kind::kBeachInner || k == kind::kBeachVariant || k == kind::kBeachRuin ||
           k == kind::kPier;
}

}  // namespace

Ship* Engine::settle_ship(AiPlayer& p) {
    if (p.settle.ship == 0) {
        return nullptr;
    }
    Ship* s = m_world.ship(p.settle.ship);
    if (s == nullptr || s->owner != p.player || s->sinking) {
        p.settle.ship = 0;
        return nullptr;
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// The island search (0x00416370)

bool Engine::island_allowed(const AiPlayer& p, std::uint16_t island) const {
    // 0x0040E570: no own city there, and no city of a human or computer with at most two cities.
    for (const City& c : m_world.cities()) {
        if (c.island != island) {
            continue;
        }
        if (c.owner == p.player) {
            return false;
        }
        if (is_settler(c.owner) && own_city_count(c.owner) <= 2) {
            return false;
        }
    }
    return true;
}

void Engine::pick_island(AiPlayer& p) {
    const auto n = static_cast<std::uint16_t>(m_world.islands().size());
    if (n == 0) {
        return;
    }
    Settlement& s = p.settle;
    if (settle_ship(p) == nullptr && own_ships(p).empty()) {
        return;  // nothing to sail with: no island scans until there is a ship again (TB-13)
    }
    if (p.pirates) {
        // Pirates: the first island whose size class is at most the pass number (smallest first).
        // A further nest (Rules::pirate_nests) goes to a part of the map without one, as long as
        // the passes find such an island, and never to an island with a nest.
        const std::uint32_t nests = pirate_nests(m_world, m_sim.rules());
        std::vector<bool> region_has_nest(nests, false);
        std::vector<bool> island_has_nest(n, false);
        for (const City& c : m_world.cities()) {
            if (c.owner == p.player && c.island < n) {
                island_has_nest[c.island] = true;
                region_has_nest[island_region(c.island, nests)] = true;
            }
        }
        for (std::uint32_t k = 0; k < 2U * n; ++k) {
            s.cursor = static_cast<std::uint16_t>(s.cursor + 1);
            if (s.cursor >= n) {
                s.cursor = 0;
                ++s.pass;
                s.last_ms = now_ms();
            }
            if (island_has_nest[s.cursor] ||
                (s.pass <= kPirateRegionPasses && region_has_nest[island_region(s.cursor, nests)])) {
                continue;
            }
            if (island_size(s.cursor) <= std::max<std::uint32_t>(s.pass, 1)) {
                s.island = s.cursor;
                push(p, kSettleArea, true);
                return;
            }
        }
        return;
    }
    for (std::uint32_t k = 0; k <= n; ++k) {
        s.cursor = static_cast<std::uint16_t>(s.cursor + 1);
        if (s.cursor >= n) {
            // Before island 0, so that the next search tests it first (it was passed over).
            s.cursor = std::numeric_limits<std::uint16_t>::max();
            if (s.need <= s.floor) {
                if (s.crop != 0) {
                    // Drop the crop and search again from 1800 tiles.
                    s.previous_crop = s.crop;
                    s.crop = 0;
                    s.need = 1800;
                    s.floor = 300;
                    return;
                }
                s.failed = true;
                p.colony_failed = true;
                log(p, "no island found for a colony");
                s.previous_crop = 0;
                s.last_ms = now_ms();
                return;
            }
            s.need -= kNeedStep;
            return;
        }
        const std::uint16_t i = s.cursor;
        if (!island_allowed(p, i)) {
            continue;
        }
        if (s.crop != 0 && fertility(i, s.crop) != 128) {
            continue;
        }
        if (island_size(i) < (p.cities <= 1 ? 2U : 1U)) {
            continue;
        }
        // The free land task 2 counts never exceeds the island's rectangle (TB-13).
        const IslandMap& map = island_of(i).map;
        if (std::int64_t{map.width()} * std::int64_t{map.height()} < std::int64_t{s.need}) {
            continue;
        }
        s.island = i;
        push(p, kSettleArea, true);
        return;
    }
}

// ---------------------------------------------------------------------------------------------
// Task 2: free land (0x00416FD0 -> 0x00415AF0)

void Engine::task_settle_area(AiPlayer& p) {
    pop(p);
    Settlement& s = p.settle;
    s.regions.clear();
    if (s.island < 0 || static_cast<std::size_t>(s.island) >= m_world.islands().size()) {
        return;
    }
    const AreaScan& scan = area_scan(static_cast<std::uint16_t>(s.island));
    s.regions = scan.regions;
    if (scan.total >= s.need) {
        push(p, kSettleSite, true);
    }
}

const Engine::AreaScan& Engine::area_scan(std::uint16_t island) {
    // The scan depends on the island's tiles only: kept while none of them changed (TB-13).
    if (m_area_scans.size() < m_world.islands().size()) {
        m_area_scans.resize(m_world.islands().size());
    }
    AreaScan& scan = m_area_scans[island];
    if (const std::uint64_t revision = island_revision(island); scan.revision != revision) {
        scan_area(island, scan);
        scan.revision = revision;
    }
    return scan;
}

void Engine::scan_area(std::uint16_t island, AreaScan& out) const {
    out.regions.clear();
    out.beach.clear();
    const IslandMap& map = island_of(island).map;
    const auto w = static_cast<std::int32_t>(map.width());
    const auto h = static_cast<std::int32_t>(map.height());
    std::vector<std::uint8_t> ok(std::size_t(w) * std::size_t(h), 0);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            const Tile t = tile(island, x, y);
            ok[std::size_t(y) * std::size_t(w) + std::size_t(x)] =
                t.valid() && t.city == MapCell::kNoCity && settle_ground(t.kind) ? 1 : 0;
            if (t.valid() && beach_kind(t.kind) && t.city == MapCell::kNoCity) {
                out.beach.emplace_back(x, y);
            }
        }
    }
    // Vertical runs of at least 5 free tiles, then horizontal runs of 5 among them.
    constexpr std::int32_t kRun = 5;
    std::vector<std::uint8_t> v(ok.size(), 0);
    for (std::int32_t x = 0; x < w; ++x) {
        std::int32_t start = 0;
        for (std::int32_t y = 0; y <= h; ++y) {
            const bool free = y < h && ok[std::size_t(y) * std::size_t(w) + std::size_t(x)] != 0;
            if (!free) {
                if (y - start >= kRun) {
                    for (std::int32_t k = start; k < y; ++k) {
                        v[std::size_t(k) * std::size_t(w) + std::size_t(x)] = 1;
                    }
                }
                start = y + 1;
            }
        }
    }
    std::vector<std::uint8_t> m(ok.size(), 0);
    for (std::int32_t y = 0; y < h; ++y) {
        std::int32_t start = 0;
        for (std::int32_t x = 0; x <= w; ++x) {
            const bool free = x < w && v[std::size_t(y) * std::size_t(w) + std::size_t(x)] != 0;
            if (!free) {
                if (x - start >= kRun) {
                    for (std::int32_t k = start; k < x; ++k) {
                        m[std::size_t(y) * std::size_t(w) + std::size_t(k)] = 1;
                    }
                }
                start = x + 1;
            }
        }
    }
    // Connected regions.
    std::int32_t& total = out.total;
    total = 0;
    std::vector<std::int32_t> todo;
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            if (m[std::size_t(y) * std::size_t(w) + std::size_t(x)] != 1) {
                continue;
            }
            Region r{x, y, x, y, 0};
            todo.assign(1, y * w + x);
            m[std::size_t(y) * std::size_t(w) + std::size_t(x)] = 2;
            while (!todo.empty()) {
                const std::int32_t i = todo.back();
                todo.pop_back();
                const std::int32_t cx = i % w;
                const std::int32_t cy = i / w;
                ++r.area;
                r.x0 = std::min(r.x0, cx);
                r.y0 = std::min(r.y0, cy);
                r.x1 = std::max(r.x1, cx);
                r.y1 = std::max(r.y1, cy);
                constexpr std::array<std::array<std::int32_t, 2>, 4> kN{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
                for (const auto& d : kN) {
                    const std::int32_t nx = cx + d[0];
                    const std::int32_t ny = cy + d[1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                        continue;
                    }
                    std::uint8_t& c = m[std::size_t(ny) * std::size_t(w) + std::size_t(nx)];
                    if (c == 1) {
                        c = 2;
                        todo.push_back(ny * w + nx);
                    }
                }
            }
            total += r.area;
            out.regions.push_back(r);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Task 3: the Kontor site (0x004172E0)

std::uint64_t Engine::island_revision(std::uint16_t island) const {
    const IslandMap& map = island_of(island).map;
    std::uint64_t revision = 0;
    for (std::size_t b = 0; b < std::size_t{map.blocks_x()} * map.blocks_y(); ++b) {
        revision = std::max(revision, map.block_revision(b));
    }
    return revision;
}

const Engine::SiteCounts& Engine::site_counts(std::uint16_t island) {
    // The island's tiles have not changed while no block has a newer revision: the counts of the
    // last test still hold for the next region and the next computer (TB-13).
    const IslandMap& map = island_of(island).map;
    const std::uint64_t revision = island_revision(island);
    if (m_site_counts.size() < m_world.islands().size()) {
        m_site_counts.resize(m_world.islands().size());
    }
    std::optional<SiteCounts>& cached = m_site_counts[island];
    if (cached && cached->revision == revision) {
        return *cached;
    }
    const auto w = static_cast<std::int32_t>(map.width());
    const auto h = static_cast<std::int32_t>(map.height());
    cached.reset();
    cached.emplace(SiteCounts{revision,
                              AreaCount(w, h,
                                        [&](std::int32_t x, std::int32_t y) {
                                            const Tile t = tile(island, x, y);
                                            return t.valid() && t.city == MapCell::kNoCity &&
                                                   (t.ware == ware::kTree || t.ware == ware::kGrass ||
                                                    t.kind == kind::kRuin);
                                        }),
                              AreaCount(w, h, [&](std::int32_t x, std::int32_t y) {
                                  const Tile t = tile(island, x, y);
                                  return t.valid() && t.ware == ware::kTree;
                              })});
    return *cached;
}

void Engine::task_settle_site(AiPlayer& p) {
    pop(p);
    Settlement& s = p.settle;
    if (s.island < 0 || static_cast<std::size_t>(s.island) >= m_world.islands().size() || s.regions.empty()) {
        return;
    }
    const auto island = static_cast<std::uint16_t>(s.island);
    bool experienced = p.cities > 1;
    for (const City& c : m_world.cities()) {
        experienced = experienced || (c.owner == p.player && population(c) >= 1200);
    }
    // The largest region left; too small ends the test of this island.
    auto best_region = std::max_element(s.regions.begin(), s.regions.end(),
                                        [](const Region& a, const Region& b) { return a.area < b.area; });
    if (best_region->area <= s.need / 4) {
        return;
    }
    const Region region = *best_region;
    best_region->area = 1;

    const WorldIsland& isl = island_of(island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    const SiteCounts& counts = site_counts(island);
    const AreaCount& green = counts.green;
    const AreaCount& trees = counts.trees;
    const assets::BuildingType* kontor = by_id(p.pirates ? id::kPirateKontor : id::kKontor1);
    if (kontor == nullptr) {
        return;
    }
    // Candidates: Kontor sites on the beach around the region (the original walks straight beach
    // runs of at least four tiles; Open1602 lets the build rules decide what stands at the coast).
    struct Best {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint8_t rotation = 0;
        std::int32_t green = -1;
    } best;
    std::vector<std::uint8_t> tried(std::size_t(w) * std::size_t(h), 0);
    const std::int32_t min_depth = experienced ? 12 : 21;
    // The free beach tiles around the region, row by row as the island's scan keeps them.
    const std::int32_t bx0 = std::max(region.x0 - 1, 0);
    const std::int32_t by0 = std::max(region.y0 - 1, 0);
    const std::int32_t bx1 = std::min(region.x1 + 1, w - 1);
    const std::int32_t by1 = std::min(region.y1 + 1, h - 1);
    for (const auto& [x, y] : area_scan(island).beach) {
        if (x < bx0 || x > bx1 || y < by0 || y > by1) {
            continue;
        }
        for (std::int32_t oy = y - 2; oy <= y; ++oy) {
            for (std::int32_t ox = x - 2; ox <= x; ++ox) {
                if (!isl.map.contains(ox, oy) || tried[std::size_t(oy) * std::size_t(w) + std::size_t(ox)] != 0) {
                    continue;
                }
                tried[std::size_t(oy) * std::size_t(w) + std::size_t(ox)] = 1;
                BuildOrder order;
                order.player = p.player;
                order.island = island;
                order.items.push_back(BuildItem{kontor->id, ox, oy, 0, 0});
                const BuildPlan plan = plan_build(m_world, m_buildings, order);
                const PlannedBuild& item = plan.items.front();
                switch (item.refusal) {
                    case BuildRefusal::None:
                    case BuildRefusal::Money:
                    case BuildRefusal::Tools:
                    case BuildRefusal::Wood:
                    case BuildRefusal::Bricks:
                        break;
                    default:
                        continue;
                }
                std::uint32_t fw = 0;
                std::uint32_t fh = 0;
                rotated_size(*kontor, item.rotation, fw, fh);
                if (!open_sea_ahead(island, item.x, item.y, fw, fh, item.rotation)) {
                    continue;  // e.g. the shore of a lake inside the island
                }
                // Land behind the Kontor, away from the water side.
                const std::uint8_t inland = static_cast<std::uint8_t>((item.rotation + 2U) & 3U);
                const std::int32_t cx = item.x + static_cast<std::int32_t>(fw) / 2;
                const std::int32_t cy = item.y + static_cast<std::int32_t>(fh) / 2;
                std::int32_t sx = cx;
                std::int32_t sy = cy;
                switch (inland) {
                    case 0:
                        sx = item.x + static_cast<std::int32_t>(fw);
                        break;
                    case 1:
                        sy = item.y + static_cast<std::int32_t>(fh);
                        break;
                    case 2:
                        sx = item.x - 1;
                        break;
                    default:
                        sy = item.y - 1;
                        break;
                }
                const std::int32_t depth = free_run(island, MapCell::kNoCity, sx, sy, inland);
                if (depth <= min_depth) {
                    continue;
                }
                if (!experienced && trees.count(cx - 12, cy - 12, cx + 12, cy + 12) <= 60) {
                    continue;
                }
                const std::int32_t g = green.count(cx - 32, cy - 32, cx + 32, cy + 32);
                if (g > best.green) {
                    best = {item.x, item.y, item.rotation, g};
                }
            }
        }
    }
    if (best.green < 0) {
        push(p, kSettleSite, true);  // the next region
        return;
    }
    s.site_x = best.x;
    s.site_y = best.y;
    s.site_rotation = best.rotation;
    Ship* ship = settle_ship(p);
    if (ship == nullptr) {
        std::vector<Ship*> ships = own_ships(p);
        if (ships.empty()) {
            return;
        }
        ship = ships.front();
        s.ship = ship->id;
    }
    free_colony_cargo(p, *ship, !p.pirates);
    ShipOrder order;
    order.player = p.player;
    order.ship = ship->id;
    order.target = water_near(island, best.x, best.y);
    execute(order);
    s.retries = kLandRetries;
    s.sailing = true;
}

void Engine::free_colony_cargo(AiPlayer& p, Ship& ship, bool computer) {
    // 0x00416A80: the AI's founding goods come from nowhere (the original sends them as raw cargo
    // messages). Computers: wood and tools up to 50 t, food up to 15 t, bricks 20 t if neither
    // food nor bricks are aboard. Pirates: food 15 t and bricks up to 20 t.
    (void)p;
    const auto& m = m_buildings.materials();
    const std::uint8_t food = m_buildings.food_ware();
    std::size_t usable = 0;
    for (const CargoSlot& c : ship.cargo) {
        if (c.ware == 0 || c.ware == m.wood || c.ware == m.tools || c.ware == food || c.ware == m.bricks) {
            ++usable;
        }
    }
    if (usable < 2) {
        return;
    }
    const auto top_up = [&](std::uint8_t w, std::uint32_t target) {
        std::uint32_t have = ship.load(w);
        for (CargoSlot& c : ship.cargo) {
            if (have >= target) {
                return;
            }
            if (c.ware == w || c.ware == 0) {
                const std::uint32_t room = kHoldCapacity - std::min(c.amount, kHoldCapacity);
                const std::uint32_t add = std::min(room, target - have);
                if (add != 0) {
                    c.ware = w;
                    c.amount += add;
                    have += add;
                }
            }
        }
    };
    if (computer) {
        const bool food_or_bricks = ship.load(food) != 0 || ship.load(m.bricks) != 0;
        top_up(m.wood, 50 * kTon);
        top_up(m.tools, 50 * kTon);
        top_up(food, 15 * kTon);
        if (!food_or_bricks) {
            top_up(m.bricks, 20 * kTon);
        }
    } else {
        top_up(food, 15 * kTon);
        top_up(m.bricks, 20 * kTon);
    }
}

bool Engine::open_sea_ahead(std::uint16_t island, std::int32_t x, std::int32_t y, std::uint32_t width,
                            std::uint32_t height, std::uint8_t rotation) const {
    // 0x00417220: from the beach of a candidate, every tile towards the water out to the edge of
    // the island's rectangle must be sea, so that a ship can reach the site from outside. Without
    // it the computer can pick the shore of a lake inside the island, which its ship never
    // reaches. Open1602 starts at the Kontor's water side and allows a few tiles of beach or surf
    // before the water (the original skips two or three tiles).
    constexpr std::int32_t kShore = 3;
    constexpr std::array<std::array<std::int32_t, 2>, 4> kSeaward{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    const WorldIsland& isl = island_of(island);
    const SeaMap& sea = m_sim.fleet().sea();
    const auto& d = kSeaward[rotation & 3U];
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    // The middle of the Kontor's water side, one tile out.
    std::int32_t tx = d[0] > 0   ? x + static_cast<std::int32_t>(width)
                      : d[0] < 0 ? x - 1
                                 : x + static_cast<std::int32_t>(width) / 2;
    std::int32_t ty = d[1] > 0   ? y + static_cast<std::int32_t>(height)
                      : d[1] < 0 ? y - 1
                                 : y + static_cast<std::int32_t>(height) / 2;
    bool water = false;
    std::int32_t shore = 0;
    for (; tx >= 0 && ty >= 0 && tx < w && ty < h; tx += d[0], ty += d[1]) {
        if (sea.sailable(static_cast<std::int32_t>(isl.x) + tx, static_cast<std::int32_t>(isl.y) + ty)) {
            water = true;
        } else if (water || ++shore > kShore) {
            return false;
        }
    }
    return water;
}

Target Engine::water_near(std::uint16_t island, std::int32_t x, std::int32_t y) const {
    // The original sails to the beach tile; Open1602 picks the open water nearest to it, so the
    // course can be planned.
    const WorldIsland& isl = island_of(island);
    const std::int32_t wx = static_cast<std::int32_t>(isl.x) + x;
    const std::int32_t wy = static_cast<std::int32_t>(isl.y) + y;
    const SeaMap& sea = m_sim.fleet().sea();
    for (std::int32_t r = 0; r <= 10; ++r) {
        std::int32_t best = -1;
        std::int32_t bx = 0;
        std::int32_t by = 0;
        for (std::int32_t dy = -r; dy <= r; ++dy) {
            for (std::int32_t dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r || !sea.sailable(wx + dx, wy + dy)) {
                    continue;
                }
                const std::int32_t d = dx * dx + dy * dy;
                if (best < 0 || d < best) {
                    best = d;
                    bx = wx + dx;
                    by = wy + dy;
                }
            }
        }
        if (best >= 0) {
            return Target::position(static_cast<std::uint32_t>(bx), static_cast<std::uint32_t>(by));
        }
    }
    return Target::position(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy));
}

// ---------------------------------------------------------------------------------------------
// Task 4: landing and founding (0x00417690)

void Engine::task_settle_land(AiPlayer& p) {
    Settlement& s = p.settle;
    Ship* ship = settle_ship(p);
    if (ship == nullptr || s.island < 0 || static_cast<std::size_t>(s.island) >= m_world.islands().size()) {
        pop(p);
        end_settlement(p);
        return;
    }
    pop(p);
    if (ship_moving(*ship)) {
        s.sailing = true;  // the root task comes back when the ship has stopped
        return;
    }
    s.sailing = false;
    const auto island = static_cast<std::uint16_t>(s.island);
    const WorldIsland& isl = island_of(island);
    const std::uint32_t free = free_land(island, MapCell::kNoCity, nullptr);
    if (!p.pirates && p.cities == 0 && free <= 600) {
        return;  // the search goes on with the next island
    }
    // Other cities on the island must leave room.
    for (const City& c : m_world.cities()) {
        if (c.island != island || c.owner == p.player) {
            continue;
        }
        const Player* o = m_world.player(c.owner);
        if (o != nullptr && m_world.behaviour(*o) == FactionBehaviour::Natives && island_size(island) >= 4) {
            continue;
        }
        const bool human = o != nullptr && o->role == PlayerRole::Human;
        if ((!human && island_size(island) > 3) || own_city_count(c.owner) < 2 || free < 300 ||
            has_population(c, 0, 50)) {
            return;
        }
    }
    const std::int32_t tx = static_cast<std::int32_t>(isl.x) + s.site_x;
    const std::int32_t ty = static_cast<std::int32_t>(isl.y) + s.site_y;
    if (!in_circle(static_cast<std::int32_t>(ship->x) - tx, static_cast<std::int32_t>(ship->y) - ty, kLandRange)) {
        if (s.retries > 0) {
            --s.retries;
            ShipOrder order;
            order.player = p.player;
            order.ship = ship->id;
            order.target = water_near(island, s.site_x, s.site_y);
            execute(order);
            s.sailing = true;
            return;
        }
        site_unreachable(p);
        return;
    }
    Player& pl = player(p);
    if (pl.money < kCheats.founding_below) {
        pl.money = money_plus(pl.money, std::int64_t{kCheats.founding_money});  // the original's founding money (cheat)
    }
    BuildOrder order;
    order.player = p.player;
    order.island = island;
    order.ship = ship->id;
    order.items.push_back(
        BuildItem{p.pirates ? id::kPirateKontor : id::kKontor1, s.site_x, s.site_y, s.site_rotation, 0});
    const CommandResult result = execute(order);
    log(p, "found a city on island %u at %d,%d: %s", island, s.site_x, s.site_y,
        result.build && result.build->placed != 0
            ? "built"
            : (result.build ? refusal_name(result.build->items.front().refusal) : "?"));
    if (!result.build || result.build->placed == 0) {
        push(p, kSettleSite, true);
        return;
    }
    const PlannedBuild& built = result.build->items.front();
    const Tile t = tile(island, built.x, built.y);
    City* c = t.city != MapCell::kNoCity ? city(island, t.city) : nullptr;
    if (c == nullptr || c->owner != p.player) {
        end_settlement(p);
        return;
    }
    p.has_city = true;
    p.island = island;
    p.city = c->index;
    p.has_plan = true;
    ++p.cities;
    for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
        if (&m_world.cities()[i] == c) {
            p.city_cursor = static_cast<std::uint32_t>(i);
        }
    }
    CityPlan& r = plan_of(p, island, c->index);
    r = CityPlan{};
    r.island = island;
    r.city = c->index;
    r.stage_ms = now_ms();
    r.land = free_land(island, c->index, &r.land_percent);
    unload_at(*c, *ship, false, false);
    add_goods(*c, m_buildings.materials().bricks,
              kCheats.founding_bricks * kStockUnitsPerTon);  // free bricks after founding
    s.failed = false;
    p.budget = 14;
    if (!p.home_valid) {
        p.home_valid = true;
        p.home_island = island;
        p.home_city = c->index;
    }
    push(p, kSurvey, true);
    push(p, kLayout, true);
    end_settlement(p);
}

void Engine::site_unreachable(AiPlayer& p) {
    // The original ends the settlement here and starts the search afresh, which picks the same
    // site again (the search has no randomness): a computer whose site cannot be reached tries it
    // forever. Open1602 goes on with the next island of the same search instead; the ship stays.
    if (classic()) {
        end_settlement(p);
        return;
    }
    Settlement& s = p.settle;
    log(p, "the ship cannot reach the site on island %d, the search goes on", s.island);
    s.retries = 0;
    s.sailing = false;
    s.regions.clear();
    s.island = -1;
}

void Engine::end_settlement(AiPlayer& p) {
    Settlement& s = p.settle;
    s.last_ms = now_ms();
    s.previous_crop = s.crop;
    s.crop = 0;
    s.retries = 0;
    s.ship = 0;
    s.active = false;
    s.sailing = false;
    s.regions.clear();
    s.island = -1;
}

void Engine::unload_at(City& c, Ship& ship, bool keep_basics, bool destroy) {
    // 0x004168E0: every hold into the warehouse as far as there is room; with `destroy` the rest
    // is thrown away (S-097: the original does this when it brings goods home).
    const auto& m = m_buildings.materials();
    for (CargoSlot& slot : ship.cargo) {
        if (!assets::is_good_number(slot.ware)) {
            continue;
        }
        if (keep_basics && (slot.ware == m.wood || slot.ware == m.tools || slot.ware == m.bricks ||
                            slot.ware == m_buildings.food_ware())) {
            continue;
        }
        c.ensure_ware(slot.ware);
        const std::uint32_t room = c.capacity > c.goods[slot.ware] ? c.capacity - c.goods[slot.ware] : 0;
        const std::uint32_t moved = std::min(room, slot.amount);
        c.goods[slot.ware] += moved;
        c.book(slot.ware, GoodsFlow::ShippedIn, moved);
        slot.amount -= moved;
        if (destroy) {
            slot.amount = 0;
        }
        if (slot.amount == 0) {
            slot = CargoSlot{};
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Later colonies (0x00426290, 0x00422380, 0x00416CA0)

bool Engine::crop_available(std::uint8_t crop) const {
    // 0x0040E620: some city of any computer player lies on an island that grows the crop fully
    // (ore: a big or small vein; gold: a big one). Shared by all computers.
    for (const City& c : m_world.cities()) {
        const Player* o = m_world.player(c.owner);
        if (o == nullptr || o->role != PlayerRole::Computer) {
            continue;
        }
        const std::uint32_t f = fertility(c.island, crop);
        if (crop == ware::kOre ? f >= 64 : f == 128) {
            return true;
        }
    }
    return false;
}

std::uint8_t Engine::choose_colony_crop(AiPlayer& p) {
    const std::uint8_t skip = p.settle.previous_crop;
    for (const std::uint8_t crop : {ware::kSpiceField, ware::kTobaccoField, ware::kWineField, ware::kCocoaField,
                                    ware::kCottonField, ware::kSugarField}) {
        if (crop != skip && !crop_available(crop)) {
            return crop;
        }
    }
    struct Pair {
        std::uint8_t slot;
        std::uint8_t crop;
        bool needs_score;
    };
    constexpr std::array<Pair, 6> kPairs{{{kSlotSpices, ware::kSpiceField, false},
                                          {kSlotTobaccoGoods, ware::kTobaccoField, false},
                                          {kSlotWine, ware::kWineField, false},
                                          {kSlotCocoa, ware::kCocoaField, false},
                                          {kSlotCotton, ware::kCottonField, true},
                                          {kSlotSugar, ware::kSugarField, true}}};
    for (const Pair& pr : kPairs) {
        if (pr.needs_score && p.score <= 2) {
            continue;
        }
        if (pr.crop != skip && slot_present(p, pr.slot) == 0) {
            return pr.crop;
        }
    }
    return 0;
}

std::uint32_t Engine::colony_ship_candidate(AiPlayer& p) {
    // 0x00423FF0: an idle or patrolling ship without a mission, the fewest cannons.
    if (Ship* s = settle_ship(p)) {
        return s->id;
    }
    std::uint32_t best = 0;
    std::uint32_t best_cannons = 25;
    for (Ship* s : own_ships(p)) {
        const ShipNote& n = note(p, s->id);
        const bool idle = !ship_moving(*s) && !s->route.active;
        if (s->repairing || s->held || n.mission != 0 || !(idle || n.patrol) || s->cannons >= best_cannons) {
            continue;
        }
        best = s->id;
        best_cannons = s->cannons;
    }
    return best;
}

bool Engine::want_colony(AiPlayer& p, std::uint8_t mode, bool force, bool food) {
    // A scenario can forbid further cities (player +0x70 bit 1, tested by 0x00426290 and its callers).
    if (player(p).scenario.has(scenario_flag::kNoColonies)) {
        return false;
    }
    if (now_ms() - p.settle.last_ms < kColonyCooldownMs) {
        return false;
    }
    const std::uint32_t n = p.cities;
    if (n >= 2 && net_income(p) <= static_cast<std::int32_t>(50 * n) && player(p).money < 15001) {
        return false;
    }
    if (p.settle.active || n > 3) {
        return false;
    }
    std::vector<std::uint8_t> wanted{ware::kWineField};
    if (mode >= 2) {
        wanted.push_back(ware::kTobaccoField);
        wanted.push_back(ware::kSpiceField);
    }
    if (mode >= 3) {
        wanted.push_back(ware::kCocoaField);
        wanted.push_back(ware::kSugarField);
    }
    if (mode >= 4) {
        wanted.push_back(ware::kGold);
    }
    bool all = true;
    for (const std::uint8_t crop : wanted) {
        all = all && crop_available(crop);
    }
    if (all && !force) {
        return false;
    }
    const std::uint32_t ship = colony_ship_candidate(p);
    if (ship == 0) {
        p.settle.ship = 0;
        return false;
    }
    if (p.settle.ship != ship) {
        log(p, "colony (mode %u): ship %u goes home to load", mode, ship);
        // First call: the ship goes to the home city to load.
        p.settle.ship = ship;
        Ship* s = m_world.ship(ship);
        if (const City* home = home_city(p); home != nullptr && s != nullptr) {
            send_to_city(p, *s, *home);
        }
        return true;
    }
    return prepare_colony_ship(p, choose_colony_crop(p), food);
}

bool Engine::prepare_colony_ship(AiPlayer& p, std::uint8_t crop, bool food) {
    Settlement& s = p.settle;
    if (s.active) {
        pick_island(p);
        return true;
    }
    if (s.failed) {
        if (now_ms() - s.last_ms < kColonyCooldownMs) {
            return false;
        }
        s.failed = false;
    }
    Ship* ship = settle_ship(p);
    City* c = current_city(p);
    if (ship == nullptr || c == nullptr) {
        return false;
    }
    const auto& m = m_buildings.materials();
    const std::uint8_t food_ware = m_buildings.food_ware();
    if (behind(p) && !threatened(p)) {
        // The colony subsidy (cheat): one ware per call into the current city.
        bool given = true;
        if (c->stock(m.wood) < kCheats.wood_below * kStockUnitsPerTon) {
            add_goods(*c, m.wood, kCheats.wood * kStockUnitsPerTon);
        } else if (c->stock(m.tools) < kCheats.tools_below * kStockUnitsPerTon) {
            add_goods(*c, m.tools, kCheats.tools * kStockUnitsPerTon);
        } else {
            std::uint32_t inhabited = 0;
            for (const City& o : m_world.cities()) {
                inhabited += o.owner == p.player && population(o) > 0 ? 1U : 0U;
            }
            if (inhabited < 2 && c->stock(food_ware) < kCheats.food_below * kStockUnitsPerTon) {
                add_goods(*c, food_ware, kCheats.food * kStockUnitsPerTon);
            } else if (inhabited >= 2 && c->stock(m.bricks) < kCheats.bricks_below * kStockUnitsPerTon) {
                add_goods(*c, m.bricks, kCheats.bricks * kStockUnitsPerTon);
            } else {
                given = false;
            }
        }
        if (given) {
            return true;
        }
    }
    // Loading from the current city. The original loads wherever the ship is (S-097); Open1602 sends
    // the ship to the city first.
    const City& here = *c;
    if (ship_moving(*ship)) {
        return true;
    }
    if (!ship_at_city(*ship, here)) {
        send_to_city(p, *ship, here);
        return true;
    }
    const auto load = [&](std::uint8_t w, std::uint32_t enough, std::uint32_t min_city, std::uint32_t amount) {
        if (ship->load(w) >= enough) {
            return true;
        }
        if (c->stock(w) >= min_city) {
            CargoTransfer t;
            t.player = p.player;
            t.ship = ship->id;
            t.ware = w;
            t.to_ship = true;
            t.amount = amount;
            execute(t);
        }
        return false;
    };
    bool ready = load(m.wood, 30 * kTon, 15 * kTon, 5 * kTon);
    ready = load(m.tools, 30 * kTon, 18 * kTon, 5 * kTon) && ready;
    if (food) {
        ready = load(food_ware, 15 * kTon, 9 * kTon, 3 * kTon) && ready;
    } else {
        ready = load(m.bricks, 30 * kTon, 15 * kTon, 5 * kTon) && ready;
    }
    if (ready) {
        log(p, "colony ship %u loaded: wood %u tools %u bricks %u food %u", ship->id, ship->load(m.wood) / 32,
            ship->load(m.tools) / 32, ship->load(m.bricks) / 32, ship->load(food_ware) / 32);
        s.need = 1800;
        s.floor = 300;
        s.crop = crop;
        s.active = true;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Task 0x2E: giving a city up (0x0041DF00)

void Engine::task_abandon(AiPlayer& p) {
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        pop(p);
        return;
    }
    const std::uint16_t island = c->island;
    const std::uint8_t index = c->index;
    const IslandMap& map = island_of(island).map;
    // At most 25 removals per step. A refused order is skipped, and a pass that removes nothing
    // gives up: the city stays (GB-3; the original repeats the pass every step until the re-init).
    std::uint32_t done = 0;
    bool others = false;
    for (std::uint32_t y = 0; y < map.height() && done < 25; ++y) {
        for (std::uint32_t x = 0; x < map.width() && done < 25; ++x) {
            const Tile t = tile(island, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
            if (!t.valid() || t.city != index || !t.origin || t.owner != p.player) {
                continue;
            }
            if (!territory_building(m_buildings, *t.type) || t.ware == ware::kTree) {
                continue;
            }
            if (t.prod == prod::kKontor) {
                continue;  // last
            }
            others = true;
            if (demolish_tile(p, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)) != 0) {
                ++done;
            }
        }
    }
    if (others && done == 0) {
        log(p, "abandon: nothing could be demolished, the city stays");
        pop(p);
        r->flags &= ~city_flag::kAbandon;
        return;
    }
    if (others) {
        return;
    }
    // Only the Kontor is left: the stock goes on an idle ship lying there, then the Kontor goes.
    if (c->warehouse_x && c->warehouse_y) {
        demolish_tile(p, *c->warehouse_x, *c->warehouse_y);
    }
    pop(p);
    if (p.cities > 0) {
        --p.cities;
    }
    r->flags &= ~city_flag::kAbandon;
    p.has_city = false;
}

}  // namespace o1602::sim::ai
