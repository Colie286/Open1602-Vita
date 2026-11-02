// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/ships.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>
#include <vector>

#include "o1602/sim/logistics.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/state_io.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t kReach = 5;            // Kontor and shipyard reach
constexpr std::int32_t kShipNear = 7;          // a ship reaches another ship
constexpr std::int32_t kPlanRadius = 80;       // ordinary plan
constexpr std::int32_t kFollowRadius = 40;     // re-plan towards a moving target (0x28)
constexpr std::int32_t kDetourRadius = 9;      // way round a ship ahead
constexpr std::int32_t kObstacleRange = 5;     // other ships in the planning grid
constexpr std::int32_t kTowerReach = 8;        // half side of the expensive square round a hostile tower
constexpr std::int32_t kLookAhead = 6;         // ships ahead
constexpr std::uint32_t kIdleMs = 1000;        // an idle ship decides again after a second
constexpr std::uint32_t kNoWayMs = 2000;       // after a plan without a way
constexpr std::uint32_t kBlockedMs = 10000;    // after a plan that only other ships blocked (S-121)
constexpr std::uint32_t kAheadWaitMs = 500;    // waits instead of a detour, 1 in 16
constexpr std::uint32_t kFollowMs = 2000;      // re-plan towards a moving target at most every 2 s
constexpr std::uint32_t kKontorStopMs = 3000;  // route stop at a Kontor with something requested
constexpr std::uint32_t kShipStopMs = 2000;    // route stop at a ship
constexpr std::uint8_t kMaxFailures = 2;       // the order is given up after the third failed plan

constexpr std::uint8_t kGiveUpsToComplain = 4;  // ships giving up a Kontor before its computer complains
constexpr std::int32_t kKontorComplaint = 16;   // reach of the count round the Kontor

constexpr std::uint32_t kTon = kStockUnitsPerTon;
constexpr std::size_t kMaxSteps = 64;  // decisions of one ship in one tick (a guard)
// A tile takes 6 250 000 * divisor / Speed microseconds straight, sqrt 2 times that diagonally
// (Speed * 32 / divisor in 1/10000 tile per 20 ms).
constexpr std::uint64_t kStraightUs = 6'250'000;
constexpr std::uint64_t kDiagonalUs = 8'838'835;

/// Piecewise linear table of the original's speed factors.
std::uint32_t interpolate(std::uint32_t i, std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> points) {
    auto prev = *points.begin();
    for (const auto& p : points) {
        if (i <= p.first) {
            if (p.first == prev.first) {
                return p.second;
            }
            const auto span = static_cast<std::int64_t>(p.first - prev.first);
            const auto delta = static_cast<std::int64_t>(p.second) - static_cast<std::int64_t>(prev.second);
            return static_cast<std::uint32_t>(static_cast<std::int64_t>(prev.second) +
                                              delta * static_cast<std::int64_t>(i - prev.first) / span);
        }
        prev = p;
    }
    return prev.second;
}

bool in_circle(std::int32_t dx, std::int32_t dy, std::uint32_t radius) {
    const std::int32_t ax = dx < 0 ? -dx : dx;
    const std::int32_t ay = dy < 0 ? -dy : dy;
    if (ay > static_cast<std::int32_t>(radius)) {
        return false;
    }
    // The shared table: no allocation per call (TB-3). Larger circles are not used in the tick.
    if (radius <= 0xFF) {
        return ax <= circle_table(static_cast<std::uint8_t>(radius))[static_cast<std::size_t>(ay)];
    }
    return ax <= circle_half_widths(radius)[static_cast<std::size_t>(ay)];
}

std::int32_t tile_of(std::int32_t pos) noexcept {
    return pos >= 0 ? pos / kShipUnit : -1;
}

}  // namespace

bool building_in_reach(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                       std::uint32_t bx, std::uint32_t by, std::int32_t wx, std::int32_t wy) {
    if (island >= world.islands().size()) {
        return false;
    }
    const WorldIsland& isl = world.islands()[island];
    if (!isl.map.contains(static_cast<std::int32_t>(bx), static_cast<std::int32_t>(by))) {
        return false;
    }
    const MapCell& cell = isl.map.top(bx, by);
    const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
    std::uint32_t w = 1;
    std::uint32_t h = 1;
    if (b != nullptr) {
        rotated_size(*b, cell.rotation, w, h);
    }
    // Centre of the footprint; buildings of even size count the half tile towards the far side.
    const auto cx = static_cast<std::int32_t>(isl.x + bx + (w - 1) / 2);
    const auto cy = static_cast<std::int32_t>(isl.y + by + (h - 1) / 2);
    std::int32_t dx = wx - cx;
    std::int32_t dy = wy - cy;
    if (dx > 0) {
        dx -= static_cast<std::int32_t>((w - 1) & 1U);
    }
    if (dy > 0) {
        dy -= static_cast<std::int32_t>((h - 1) & 1U);
    }
    return in_circle(dx, dy, kReach);
}

std::optional<std::size_t> kontor_in_reach(const World& world, const assets::BuildingTable& buildings, std::int32_t wx,
                                           std::int32_t wy) {
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        const City& c = world.cities()[i];
        if (c.warehouse_x && c.warehouse_y &&
            building_in_reach(world, buildings, c.island, *c.warehouse_x, *c.warehouse_y, wx, wy)) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<KontorSite> kontor_site_near(const World& world, const assets::BuildingTable& buildings, std::int32_t wx,
                                           std::int32_t wy) {
    constexpr std::int32_t kRadius = 4;
    constexpr std::int32_t kSide = 2 * kRadius + 1;
    if (wx < 0 || wy < 0 || !world.contains(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy))) {
        return std::nullopt;
    }
    // The ship's tile must lie in an island's rectangle, water inside it included.
    const std::uint16_t index = world.cell(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy)).island;
    if (index == World::kSea || index >= world.islands().size()) {
        return std::nullopt;
    }
    const WorldIsland& isl = world.islands()[index];
    const IslandMap& map = isl.map;
    // The circles of the island's markets and Kontors, at least 8 tiles.
    std::vector<std::pair<Catchment, std::uint8_t>> circles;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const MapCell& c = map.top(x, y);
            const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
            if (b == nullptr || (b->prod_kind != prod::kMarket && b->prod_kind != prod::kKontor)) {
                continue;
            }
            const SubTile sub = map.sub_tile(buildings, x, y);
            if (sub.origin_x == static_cast<std::int32_t>(x) && sub.origin_y == static_cast<std::int32_t>(y)) {
                circles.emplace_back(catchment_of(*b, c.rotation, {sub.origin_x, sub.origin_y}),
                                     std::max<std::uint8_t>(b->radius, 8));
            }
        }
    }
    const auto claimed = [&](std::int32_t x, std::int32_t y) {
        return std::any_of(circles.begin(), circles.end(), [&](const auto& circle) {
            const Catchment& a = circle.first;
            std::int32_t dx = x - a.centre.x;
            std::int32_t dy = y - a.centre.y;
            if (dx > 0) {
                dx -= a.parity_x;
            }
            if (dy > 0) {
                dy -= a.parity_y;
            }
            return in_circle(dx, dy, circle.second);
        });
    };
    // The window round the ship cut to the disc: sea, surf and beach
    // can be passed, land claimed by a city cannot, unclaimed land is the goal. Beyond the
    // island's rectangle there is only open sea.
    enum class Cell : std::uint8_t { Blocked, Water, Goal };
    std::array<Cell, kSide * kSide> grid{};
    const std::vector<std::uint8_t>& half = circle_table(kRadius);
    for (std::int32_t dy = -kRadius; dy <= kRadius; ++dy) {
        for (std::int32_t dx = -kRadius; dx <= kRadius; ++dx) {
            Cell& cell = grid[static_cast<std::size_t>((dy + kRadius) * kSide + dx + kRadius)];
            const std::int32_t x = wx + dx;
            const std::int32_t y = wy + dy;
            if (std::abs(dx) > half[static_cast<std::size_t>(std::abs(dy))] || x < 0 || y < 0 ||
                !world.contains(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y))) {
                cell = Cell::Blocked;
                continue;
            }
            const std::int32_t lx = x - static_cast<std::int32_t>(isl.x);
            const std::int32_t ly = y - static_cast<std::int32_t>(isl.y);
            if (!map.contains(lx, ly)) {
                cell = Cell::Water;
                continue;
            }
            const MapCell& c = map.top(static_cast<std::uint32_t>(lx), static_cast<std::uint32_t>(ly));
            const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
            const std::int32_t k = b != nullptr ? b->kind : kind::kSea;
            if ((k >= kind::kSea && k <= kind::kBeachVariant) || k == kind::kBeachRuin) {
                cell = Cell::Water;
            } else {
                cell = claimed(lx, ly) ? Cell::Blocked : Cell::Goal;
            }
        }
    }
    // A search from the ship's tile: eight neighbours, a diagonal step only between
    // two open sides; the first goal reached is the site.
    const auto open = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < kSide && y < kSide &&
               grid[static_cast<std::size_t>(y * kSide + x)] != Cell::Blocked;
    };
    std::array<bool, kSide * kSide> seen{};
    std::array<std::pair<std::int32_t, std::int32_t>, kSide * kSide> queue{};
    std::size_t head = 0;
    std::size_t tail = 0;
    queue[tail++] = {kRadius, kRadius};
    seen[static_cast<std::size_t>(kRadius * kSide + kRadius)] = true;
    constexpr std::array<std::array<std::int32_t, 2>, 8> kSteps{
        {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}}};
    while (head < tail) {
        const auto [x, y] = queue[head++];
        if (grid[static_cast<std::size_t>(y * kSide + x)] == Cell::Goal) {
            return KontorSite{index, static_cast<std::uint32_t>(wx + x - kRadius - static_cast<std::int32_t>(isl.x)),
                              static_cast<std::uint32_t>(wy + y - kRadius - static_cast<std::int32_t>(isl.y))};
        }
        for (const auto& [sx, sy] : kSteps) {
            const std::int32_t nx = x + sx;
            const std::int32_t ny = y + sy;
            if (!open(nx, ny) || seen[static_cast<std::size_t>(ny * kSide + nx)] ||
                (sx != 0 && sy != 0 && (!open(x + sx, y) || !open(x, y + sy)))) {
                continue;
            }
            seen[static_cast<std::size_t>(ny * kSide + nx)] = true;
            queue[tail++] = {nx, ny};
        }
    }
    return std::nullopt;
}

bool kontor_open(const World& world, const assets::BuildingTable& buildings, const City& city) {
    if (!city.warehouse_x || !city.warehouse_y) {
        return false;
    }
    const auto b = building_at(world, buildings, city.island, {*city.warehouse_x, *city.warehouse_y});
    if (!b || !b->type->destroyed) {
        return true;
    }
    const Player* owner = world.player(city.owner);
    return owner != nullptr && owner->active() &&
           (owner->role == PlayerRole::Human || owner->role == PlayerRole::Computer);
}

Fleet::Fleet(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
             const Rules& rules)
    : m_world(&world), m_buildings(&buildings), m_figures(&figures), m_rules(&rules) {
    m_sea.rebuild(world, buildings);
    for (Ship& s : world.ships()) {
        const assets::FigureType* type = type_of(s);
        // Older files have no health; a sinking ship has 0 on purpose and keeps it, or a game loaded
        // during a sea battle would differ from the running one until the ship is gone.
        if (type != nullptr && ((s.health == 0 && !s.sinking) || s.health > type->ship.max_health) &&
            type->ship.max_health != 0) {
            s.health = type->ship.max_health;
        }
        if (s.cargo.empty()) {
            s.cargo.resize(type != nullptr && type->ship.cargo_slots != 0 ? type->ship.cargo_slots : 1U);
        }
        if (world.simulation_state().empty()) {
            // An original savegame keeps no island for its ships; a game saved by Open1602 does.
            s.near_island = 0xFFFF;
            update_island(s);
        }
    }
    // The original starts the names at rand(); a copy of the generator, so that
    // making a Simulation does not change the game.
    Random names = world.random(RandomStream::Ships);
    m_name_index = names.next();
}

// ---- Data of ships -----------------------------------------------------------------------------

const assets::FigureType* Fleet::type_of(const Ship& ship) const {
    return m_figures->get(ship.figure);
}

std::size_t usable_holds(const Ship& ship, const assets::FigureTable& figures) {
    const assets::FigureType* t = figures.get(ship.figure);
    const std::size_t n = t != nullptr && t->ship.cargo_slots != 0 ? t->ship.cargo_slots : ship.cargo.size();
    return std::min(n, ship.cargo.size());
}

std::size_t fullest_hold(const Ship& ship, const assets::FigureTable& figures) {
    std::size_t best = 0;
    std::uint32_t most = 0;
    for (std::size_t h = usable_holds(ship, figures); h-- > 0;) {
        const CargoSlot& slot = ship.cargo[h];
        if (slot.ware != 0 && slot.amount > most) {
            most = slot.amount;
            best = h;
        }
    }
    return best;
}

std::size_t usable_route_entries(const Ship& ship, const assets::FigureTable& figures) {
    return std::clamp(usable_holds(ship, figures), RouteStop::kOriginalEntries, RouteStop::kEntries);
}

std::size_t Fleet::holds(const Ship& ship) const {
    return usable_holds(ship, *m_figures);
}

std::uint32_t Fleet::space_for(const Ship& ship, std::uint8_t ware, std::uint8_t hold) const {
    std::uint32_t space = 0;
    const std::uint32_t capacity = hold_capacity(ship);
    const std::size_t n = holds(ship);
    for (std::size_t i = 0; i < n; ++i) {
        if (hold != 0xFF && i != hold) {
            continue;
        }
        const CargoSlot& c = ship.cargo[i];
        if (c.ware == 0) {
            space += capacity;
        } else if (c.ware == ware && c.amount < capacity) {
            space += capacity - c.amount;
        }
    }
    return space;
}

void Fleet::add_cargo(Ship& ship, std::uint8_t ware, std::uint32_t amount, std::uint16_t price) {
    const std::size_t n = holds(ship);
    const std::uint32_t capacity = hold_capacity(ship);
    // Holds of the same ware first, then empty ones, each in order.
    for (int pass = 0; pass < 2 && amount != 0; ++pass) {
        for (std::size_t i = 0; i < n && amount != 0; ++i) {
            CargoSlot& c = ship.cargo[i];
            const bool same = pass == 0 && c.ware == ware;
            const bool empty = pass == 1 && c.ware == 0;
            if (!same && !empty) {
                continue;
            }
            const std::uint32_t room = capacity > c.amount ? capacity - c.amount : 0U;
            const std::uint32_t put = std::min(room, amount);
            if (put == 0) {
                continue;
            }
            if (empty) {
                c = CargoSlot{ware, put, price};
            } else {
                const std::uint64_t value = std::uint64_t{c.price} * c.amount + std::uint64_t{price} * put;
                c.amount += put;
                c.price = static_cast<std::uint16_t>(value / c.amount);
            }
            amount -= put;
        }
    }
}

std::uint32_t Fleet::cheat_cargo(std::uint32_t id, std::uint8_t ware, std::uint32_t amount) {
    Ship* ship = m_world->ship(id);
    if (ship == nullptr) {
        return 0;
    }
    const std::uint32_t put = std::min(amount, space_for(*ship, ware));
    add_cargo(*ship, ware, put, 0);
    return put;
}

bool Fleet::cheat_cannon(std::uint32_t id) {
    Ship* ship = m_world->ship(id);
    const assets::FigureType* type = ship != nullptr ? type_of(*ship) : nullptr;
    if (type == nullptr || ship->cannons >= type->ship.max_cannons) {
        return false;
    }
    ++ship->cannons;
    return true;
}

std::uint32_t Fleet::remove_cargo(Ship& ship, std::uint8_t ware, std::uint32_t amount, std::uint8_t hold) {
    std::uint32_t taken = 0;
    const std::size_t n = holds(ship);
    for (std::size_t i = 0; i < n && taken < amount; ++i) {
        CargoSlot& c = ship.cargo[i];
        if (c.ware != ware || (hold != 0xFF && i != hold)) {
            continue;
        }
        const std::uint32_t t = std::min(c.amount, amount - taken);
        c.amount -= t;
        taken += t;
        if (c.amount == 0) {
            c.ware = 0;
        }
    }
    return taken;
}

std::uint32_t Fleet::run_us(const Ship& ship, std::uint8_t direction, std::uint8_t tiles) const {
    if (ship.cart()) {
        return cart_run_us(ship, direction) * std::max<std::uint32_t>(1, tiles);
    }
    const assets::FigureType* t = type_of(ship);
    const std::uint32_t speed = t != nullptr && t->speed != 0 ? t->speed : 500U;
    // Speed divisor of damage and load: 32 = full speed, 96 = a third.
    std::uint32_t h = 128;
    if (t != nullptr && t->ship.max_health != 0) {
        h = std::min<std::uint32_t>(128, ship.health * 128 / t->ship.max_health);
    }
    std::uint64_t load = 0;
    const std::size_t n = holds(ship);
    for (std::size_t i = 0; i < n; ++i) {
        const CargoSlot& c = ship.cargo[i];
        if (c.ware != 0) {
            load += is_soldier_ware(c.ware) ? c.amount / 2 : c.amount;  // soldiers weigh half
        }
    }
    const std::uint64_t capacity = std::max<std::uint64_t>(1, n * std::uint64_t{kHoldCapacity});
    const auto l = static_cast<std::uint32_t>(std::min<std::uint64_t>(128, load * 128 / capacity));
    const std::uint32_t hf = interpolate(h, {{0, 384}, {25, 256}, {64, 192}, {96, 160}, {128, 128}});
    const std::uint32_t lf = interpolate(l, {{0, 128}, {38, 140}, {64, 160}, {96, 192}, {128, 256}});
    const std::uint32_t f = std::min<std::uint32_t>(384, hf * lf / 128);
    const std::uint32_t divisor = std::max<std::uint32_t>(1, f * 32 / 128);
    const std::uint64_t per_tile = ((direction & 1U) != 0 ? kDiagonalUs : kStraightUs) * divisor / speed;
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(1, per_tile * tiles));
}

std::uint32_t Fleet::speed_milli(const Ship& ship) const {
    const std::uint32_t us = run_us(ship, 0, 1);
    return us != 0 ? static_cast<std::uint32_t>(1'000'000'000ULL / us) : 0;
}

std::uint32_t Fleet::fleet_size(PlayerId player) const {
    std::uint32_t n = 0;
    for (const Ship& s : m_world->ships()) {
        n += s.owner == player && !s.cart() ? 1U : 0U;  // a cart is no ship of the fleet's limit
    }
    for (const CostBuilding& y : m_world->cost_buildings()) {
        if (y.kind != CostBuilding::Kind::Shipyard || y.order == CostBuilding::kNoOrder ||
            y.island >= m_world->islands().size()) {
            continue;
        }
        const WorldIsland& isl = m_world->islands()[y.island];
        if (isl.map.contains(y.x, y.y) && isl.map.top(y.x, y.y).owner == player) {
            ++n;
        }
    }
    return n;
}

// ---- Targets -----------------------------------------------------------------------------------

bool Fleet::target_valid(const Target& t) const {
    switch (t.kind) {
        case Target::Kind::None:
            return false;
        case Target::Kind::Position:
            return m_world->contains(t.x, t.y);
        case Target::Kind::City: {
            const City* c = m_world->city(t.island, t.city);
            return c != nullptr && c->warehouse_x && c->warehouse_y;
        }
        case Target::Kind::Ship: {
            const Ship* s = m_world->ship(t.ship);
            return s != nullptr && !s->cart();  // ships do not sail to a cart on land
        }
        case Target::Kind::Soldier:
            return m_world->soldier(t.ship) != nullptr;
        case Target::Kind::Spot:
            return m_world->contains(t.x / 2, t.y / 2);
        case Target::Kind::Building:
            return t.island < m_world->islands().size() &&
                   m_world->islands()[t.island].map.contains(static_cast<std::int32_t>(t.x),
                                                             static_cast<std::int32_t>(t.y)) &&
                   !m_world->islands()[t.island].map.top(t.x, t.y).empty();
    }
    return false;
}

std::optional<std::pair<std::int32_t, std::int32_t>> Fleet::target_point(const Target& t) const {
    switch (t.kind) {
        case Target::Kind::Position:
            return std::pair{static_cast<std::int32_t>(t.x), static_cast<std::int32_t>(t.y)};
        case Target::Kind::City: {
            const City* c = m_world->city(t.island, t.city);
            if (c == nullptr || !c->warehouse_x || !c->warehouse_y) {
                return std::nullopt;
            }
            const WorldIsland& isl = m_world->islands()[t.island];
            return std::pair{static_cast<std::int32_t>(isl.x + *c->warehouse_x),
                             static_cast<std::int32_t>(isl.y + *c->warehouse_y)};
        }
        case Target::Kind::Ship:
            if (const Ship* s = m_world->ship(t.ship)) {
                return std::pair{static_cast<std::int32_t>(s->x), static_cast<std::int32_t>(s->y)};
            }
            return std::nullopt;
        case Target::Kind::Soldier:
            if (const Soldier* s = m_world->soldier(t.ship)) {
                return std::pair{s->pos_x / kPosUnit, s->pos_y / kPosUnit};
            }
            return std::nullopt;
        case Target::Kind::Spot:
            return std::pair{static_cast<std::int32_t>(t.x / 2), static_cast<std::int32_t>(t.y / 2)};
        case Target::Kind::Building: {
            if (t.island >= m_world->islands().size()) {
                return std::nullopt;
            }
            const WorldIsland& isl = m_world->islands()[t.island];
            return std::pair{static_cast<std::int32_t>(isl.x + t.x), static_cast<std::int32_t>(isl.y + t.y)};
        }
        case Target::Kind::None:
            break;
    }
    return std::nullopt;
}

namespace {

/// Whether a ship on tile (x, y) has reached the target. The planner aims at the same tiles, so a
/// course ends where this holds. A position on land is reached at the water nearest to it within
/// the Kontor reach, where the original's ship stops too, up to the beach; the original then never
/// arrives (exactly the tile) and gives the order up (S-071). Without water there, anywhere in
/// the reach.
bool reached(const World& world, const assets::BuildingTable& buildings, const SeaMap& sea, const Target& t,
             std::int32_t x, std::int32_t y) {
    switch (t.kind) {
        case Target::Kind::Position: {
            const auto tx = static_cast<std::int32_t>(t.x);
            const auto ty = static_cast<std::int32_t>(t.y);
            if (sea.sailable(tx, ty)) {
                return x == tx && y == ty;
            }
            if (!in_circle(x - tx, y - ty, kReach)) {
                return false;
            }
            std::int32_t nearest = -1;
            constexpr auto r = static_cast<std::int32_t>(kReach);
            for (std::int32_t dy = -r; dy <= r; ++dy) {
                for (std::int32_t dx = -r; dx <= r; ++dx) {
                    const std::int32_t d = dx * dx + dy * dy;
                    if ((nearest < 0 || d < nearest) && in_circle(dx, dy, kReach) && sea.sailable(tx + dx, ty + dy)) {
                        nearest = d;
                    }
                }
            }
            const std::int32_t d = (x - tx) * (x - tx) + (y - ty) * (y - ty);
            return nearest < 0 || d <= nearest;
        }
        case Target::Kind::City: {
            const City* c = world.city(t.island, t.city);
            return c != nullptr && c->warehouse_x && c->warehouse_y &&
                   building_in_reach(world, buildings, t.island, *c->warehouse_x, *c->warehouse_y, x, y);
        }
        case Target::Kind::Ship: {
            const Ship* s = world.ship(t.ship);
            return s != nullptr && sea_distance(s->x - x, s->y - y) < kShipNear;
        }
        case Target::Kind::Soldier: {
            const Soldier* s = world.soldier(t.ship);
            return s != nullptr && sea_distance(s->pos_x / kPosUnit - x, s->pos_y / kPosUnit - y) < kShipNear;
        }
        case Target::Kind::Spot:
            return in_circle(x - static_cast<std::int32_t>(t.x / 2), y - static_cast<std::int32_t>(t.y / 2), kReach);
        case Target::Kind::Building:
            return building_in_reach(world, buildings, t.island, t.x, t.y, x, y);
        case Target::Kind::None:
            break;
    }
    return false;
}

}  // namespace

bool Fleet::at_target(const Ship& ship, const Target& target) const {
    if (ship.cart()) {
        return cart_at(ship, target);
    }
    return reached(*m_world, *m_buildings, m_sea, target, ship.x, ship.y);
}

// ---- Ships nearby (TB-4) ------------------------------------------------------------------------

void Fleet::build_squares() {
    const std::vector<Ship>& ships = m_world->ships();
    m_square_cols = (m_world->width() + kSquareTiles - 1) / kSquareTiles + 1;
    m_square_rows = (m_world->height() + kSquareTiles - 1) / kSquareTiles + 1;
    m_squares.resize(std::size_t{m_square_cols} * m_square_rows);
    for (std::vector<std::uint32_t>& square : m_squares) {
        square.clear();
    }
    for (std::size_t i = 0; i < ships.size(); ++i) {
        const std::uint32_t col = std::min<std::uint32_t>(ships[i].x / kSquareTiles, m_square_cols - 1);
        const std::uint32_t row = std::min<std::uint32_t>(ships[i].y / kSquareTiles, m_square_rows - 1);
        m_squares[std::size_t{row} * m_square_cols + col].push_back(static_cast<std::uint32_t>(i));
    }
    m_squared_ships = ships.size();
    m_squares_valid = true;
}

template <class Visit>
void Fleet::near_ships(std::int32_t x, std::int32_t y, std::int32_t range, Visit&& visit) const {
    const std::vector<Ship>& ships = m_world->ships();
    // Trade carts drive on land: no ship meets them.
    if (!m_squares_valid) {
        for (const Ship& o : ships) {
            if (!o.cart() && visit(o)) {
                return;
            }
        }
        return;
    }
    const auto clamp = [](std::int32_t v, std::uint32_t n) {
        return static_cast<std::uint32_t>(std::clamp<std::int32_t>(v, 0, static_cast<std::int32_t>(n) - 1));
    };
    const std::int32_t reach = range + kSquareSlack;
    const std::uint32_t c0 = clamp((x - reach) / kSquareTiles - ((x - reach) < 0 ? 1 : 0), m_square_cols);
    const std::uint32_t c1 = clamp((x + reach) / kSquareTiles, m_square_cols);
    const std::uint32_t r0 = clamp((y - reach) / kSquareTiles - ((y - reach) < 0 ? 1 : 0), m_square_rows);
    const std::uint32_t r1 = clamp((y + reach) / kSquareTiles, m_square_rows);
    m_near.clear();
    for (std::uint32_t row = r0; row <= r1; ++row) {
        for (std::uint32_t col = c0; col <= c1; ++col) {
            const std::vector<std::uint32_t>& square = m_squares[std::size_t{row} * m_square_cols + col];
            m_near.insert(m_near.end(), square.begin(), square.end());
        }
    }
    // In the order of the ship list, as the scans over all ships were.
    std::sort(m_near.begin(), m_near.end());
    for (const std::uint32_t i : m_near) {
        if (i < ships.size() && !ships[i].cart() && visit(ships[i])) {
            return;
        }
    }
    for (std::size_t i = m_squared_ships; i < ships.size(); ++i) {
        if (!ships[i].cart() && visit(ships[i])) {
            return;
        }
    }
}

// ---- Movement ----------------------------------------------------------------------------------

void Fleet::tick(std::uint32_t dt_ms) {
    const std::uint32_t budget = dt_ms * 1000U;
    m_plans_left = kPlansPerTick;
    m_tick_ms = dt_ms;
    build_squares();
    // By id, so that a ship added on the way could not break the loop.
    m_ids.clear();
    for (const Ship& s : m_world->ships()) {
        m_ids.push_back(s.id);
    }
    for (const std::uint32_t id : m_ids) {
        if (Ship* s = m_world->ship(id)) {
            advance(*s, budget);
        }
    }
    m_squares_valid = false;
    // Sunk ships are gone with their cargo. The wreck the original leaves
    // for about 22 s is only a picture (render::CombatEffects).
    m_world->erase_ships([](const Ship& s) { return s.sinking && s.owner == kNoPlayer; });
    trader_tick(dt_ms);
    yards_tick(dt_ms);
    pirates_tick(dt_ms);
    explore_tick();
}

void Fleet::hit(std::uint32_t id, std::uint32_t damage, PlayerId attacker, bool by_pirates) {
    Ship* ship = m_world->ship(id);
    if (ship == nullptr || ship->sinking) {
        return;
    }
    const assets::FigureType* t = type_of(*ship);
    const std::uint32_t max = t != nullptr && t->ship.max_health != 0 ? t->ship.max_health : 1U;
    const std::uint32_t health = ship->health != 0 ? std::min(ship->health, max) : max;
    if (health <= damage) {
        ship->health = 0;  // the original keeps the old value until the sink command (M-015)
        sink(*ship, attacker);
        return;
    }
    ship->health = health - damage;
    m_events.push_back(ShipEvent{ShipEvent::Kind::Attacked, ship->id, ship->owner, 0, attacker, 0, by_pirates ? 1 : 0});
    const Player* owner = m_world->player(ship->owner);
    if (by_pirates && !ship->held && owner != nullptr && owner->role != PlayerRole::Human &&
        m_world->behaviour(*owner) != FactionBehaviour::Pirates) {
        // A computer's, the trader's or the natives' ship surrenders at once.
        ship->held = true;
        ship->plundered = false;
        ship->motion.path.clear();
        ship->motion.path_index = 0;
        m_events.push_back(ShipEvent{ShipEvent::Kind::WhiteFlag, ship->id, ship->owner});
    }
}

void Fleet::sink(Ship& ship, PlayerId attacker) {
    if (ship.sinking) {
        return;
    }
    // Op 0x0C: the ship stops, turns to the nearest odd heading and goes down.
    ship.sinking = true;
    ship.held = false;
    trader_leave(ship);
    ship.queue.clear();
    ship.route.active = false;
    ship.repairing = false;
    ship.explore_steps = 0;
    for (CostBuilding& yard : m_world->cost_buildings()) {
        if (yard.repair_ship == ship.id) {
            yard.repair_ship = 0;
        }
    }
    ShipMotion& m = ship.motion;
    m.path.clear();
    m.path_index = 0;
    ship.place(ship.x, ship.y);
    const auto heading = static_cast<std::uint8_t>((ship.direction & ~1U) + 1U);
    const assets::FigureType* t = type_of(ship);
    const std::uint32_t turn = t != nullptr ? t->ship.turn_ms : 0U;
    const std::uint32_t sink_ms = t != nullptr && t->ship.sink_ms != 0 ? t->ship.sink_ms : 3000U;
    const std::uint32_t steps = (heading - ship.direction) & 7U;
    ship.direction = static_cast<std::uint8_t>(heading & 7U);
    m.phase = ShipMotion::Phase::Sinking;
    m.elapsed_us = 0;
    m.duration_us = (steps * turn + sink_ms) * 1000U;
    // The score's counters: the soldiers aboard go down with the ship.
    std::uint32_t soldiers = 0;
    for (const CargoSlot& c : ship.cargo) {
        soldiers += is_soldier_ware(c.ware) ? 1U : 0U;
    }
    if (ship.cart()) {
        // A destroyed trade cart is no lost or sunk ship of the score.
        m_events.push_back(ShipEvent{ShipEvent::Kind::Sunk, ship.id, ship.owner, 0, attacker});
        return;
    }
    if (Player* owner = m_world->player(ship.owner)) {
        add_count(owner->ships_lost, 1);
        add_count(owner->soldiers_lost, soldiers);
    }
    Player* by = m_world->player(attacker);
    if (by != nullptr && (by->role == PlayerRole::Human || by->role == PlayerRole::Computer)) {
        add_count(by->ships_sunk, 1);
        add_count(by->soldiers_killed, soldiers);
    }
    m_events.push_back(ShipEvent{ShipEvent::Kind::Sunk, ship.id, ship.owner, 0, attacker});
}

void Fleet::island_changed(std::uint16_t island) {
    m_sea.update_island(*m_world, *m_buildings, island);
}

void Fleet::island_changed(std::uint16_t island, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    m_sea.update_island(*m_world, *m_buildings, island, x0, y0, x1, y1);
}

void Fleet::advance(Ship& ship, std::uint32_t budget_us) {
    for (std::size_t step = 0; step < kMaxSteps; ++step) {
        ShipMotion& m = ship.motion;
        if (m.phase == ShipMotion::Phase::Idle) {
            decide(ship);
            if (m.phase == ShipMotion::Phase::Idle) {
                start_wait(ship, kIdleMs);
            }
            continue;
        }
        const std::uint32_t left = m.duration_us > m.elapsed_us ? m.duration_us - m.elapsed_us : 0U;
        const bool done = budget_us >= left;
        m.elapsed_us = done ? m.duration_us : m.elapsed_us + budget_us;
        budget_us = done ? budget_us - left : 0U;
        if (m.phase == ShipMotion::Phase::Run) {
            const std::int32_t way = static_cast<std::int32_t>(m.count) * kShipUnit;
            const auto part =
                static_cast<std::int32_t>(std::int64_t{way} * m.elapsed_us / std::max<std::uint32_t>(1, m.duration_us));
            ship.pos_x = m.from_x + step_x(ship.direction) * part;
            ship.pos_y = m.from_y + step_y(ship.direction) * part;
            ship.x = static_cast<std::uint16_t>(std::max(0, tile_of(ship.pos_x)));
            ship.y = static_cast<std::uint16_t>(std::max(0, tile_of(ship.pos_y)));
        }
        if (!done) {
            return;
        }
        if (m.phase == ShipMotion::Phase::Sinking) {
            ship.owner = kNoPlayer;  // gone at the end of the tick
            return;
        }
        m.phase = ShipMotion::Phase::Idle;
        decide(ship);
        if (m.phase == ShipMotion::Phase::Idle) {
            start_wait(ship, kIdleMs);
        }
        if (budget_us == 0) {
            return;
        }
    }
}

void Fleet::start_wait(Ship& ship, std::uint32_t ms) {
    ShipMotion& m = ship.motion;
    m.phase = ShipMotion::Phase::Wait;
    m.elapsed_us = 0;
    m.duration_us = std::max<std::uint32_t>(1, ms * 1000U);
}

bool Fleet::start_next_run(Ship& ship) {
    ShipMotion& m = ship.motion;
    if (m.path_index >= m.path.size()) {
        return false;
    }
    const std::uint8_t code = m.path[m.path_index];
    const auto dir = static_cast<std::uint8_t>((code >> 4U) & 7U);
    const auto tiles = static_cast<std::uint8_t>(code & 0x0FU);
    const assets::FigureType* t = type_of(ship);
    if (dir != ship.direction && t != nullptr && t->ship.turn_ms != 0) {
        // Turn in place, one 45 degree step at once and one per Drehtime.
        const std::uint8_t right = static_cast<std::uint8_t>((dir - ship.direction) & 7U);
        const std::uint8_t left = static_cast<std::uint8_t>((ship.direction - dir) & 7U);
        ship.direction = static_cast<std::uint8_t>((ship.direction + (right < left ? 1U : 7U)) & 7U);
        if (ship.direction != dir) {
            m.phase = ShipMotion::Phase::Turn;
            m.turn_to = dir;
            m.elapsed_us = 0;
            m.duration_us = t->ship.turn_ms * 1000U;
            return true;
        }
    }
    ship.direction = dir;
    // A run starts on a tile centre.
    ship.pos_x = static_cast<std::int32_t>(ship.x) * kShipUnit + kShipUnit / 2;
    ship.pos_y = static_cast<std::int32_t>(ship.y) * kShipUnit + kShipUnit / 2;
    m.phase = ShipMotion::Phase::Run;
    m.count = tiles;
    m.from_x = ship.pos_x;
    m.from_y = ship.pos_y;
    m.elapsed_us = 0;
    m.duration_us = run_us(ship, dir, tiles);
    m.turn_to = 0xFF;
    ++m.path_index;
    return true;
}

Fleet::Planned Fleet::plan(Ship& ship, std::int32_t radius) {
    ShipMotion& m = ship.motion;
    m.path.clear();
    m.path_index = 0;
    const auto aim = target_point(ship.target);
    if (!aim) {
        return Planned::NoWay;
    }
    SeaPlan r;
    r.start_x = ship.x;
    r.start_y = ship.y;
    r.radius = radius;
    r.aim_x = aim->first;
    r.aim_y = aim->second;
    r.guided = true;  // around islands wider than the window
    if (m.failures != 0) {
        // After a failure the window moves off-centre.
        const std::uint32_t k = m_world->random(RandomStream::Ships).next() & 3U;
        r.shift_x = (k & 1U) != 0 ? radius / 2 : -radius / 2;
        r.shift_y = (k & 2U) != 0 ? radius / 2 : -radius / 2;
        r.soft_ships = true;
    }
    near_ships(ship.x, ship.y, kObstacleRange, [&](const Ship& o) {
        if (o.id == ship.id || std::abs(o.x - ship.x) >= kObstacleRange || std::abs(o.y - ship.y) >= kObstacleRange) {
            return false;
        }
        if (ship.target.kind == Target::Kind::Ship && o.id == ship.target.ship) {
            return false;  // the ship it sails to is no obstacle
        }
        r.ships.push_back(SeaPlan::Obstacle{o.x, o.y, o.motion.phase == ShipMotion::Phase::Run});
        return false;
    });
    // Water near the cannon towers of players at war with the ship's owner:
    // a square of 16 tiles around each. The original raises the cost around every
    // tower of an island with a hostile city, also the towers of allies; Open1602 only around
    // hostile ones (S-103).
    for (const Tower& t : m_world->towers()) {
        if (t.island >= m_world->islands().size() || !m_world->diplomacy().hostile(t.owner, ship.owner)) {
            continue;
        }
        const WorldIsland& island = m_world->islands()[t.island];
        const std::int32_t tx = static_cast<std::int32_t>(island.x) + t.x;
        const std::int32_t ty = static_cast<std::int32_t>(island.y) + t.y;
        if (std::abs(tx - ship.x) > radius + kTowerReach + 1 || std::abs(ty - ship.y) > radius + kTowerReach + 1) {
            continue;
        }
        r.danger.push_back(
            SeaPlan::Danger{tx - kTowerReach + 1, ty - kTowerReach + 1, tx + kTowerReach, ty + kTowerReach});
    }
    const Target target = ship.target;
    r.goal = [&](std::int32_t x, std::int32_t y) { return reached(*m_world, *m_buildings, m_sea, target, x, y); };
    SeaCourse course = m_planner.plan(m_sea, r);
    if (course.path.empty() && !course.reached_goal && !r.ships.empty()) {
        // No way: whether only the ships nearby close it, e.g. one lying in a narrow inlet (S-121).
        r.ships.clear();
        const SeaCourse free = m_planner.plan(m_sea, r);
        return !free.path.empty() || free.reached_goal ? Planned::ShipsInWay : Planned::NoWay;
    }
    if (course.path.empty()) {
        return course.reached_goal ? Planned::Way : Planned::NoWay;
    }
    m.path = std::move(course.path);
    return Planned::Way;
}

bool Fleet::local_detour(Ship& ship) {
    // Walk the remaining course until 9 tiles away; plan a way to that point around the ship
    // ahead and keep the rest of the course (the original loses the rest, S-072).
    ShipMotion& m = ship.motion;
    std::int32_t x = ship.x;
    std::int32_t y = ship.y;
    std::size_t i = m.path_index;
    while (i < m.path.size()) {
        const auto dir = static_cast<std::uint8_t>((m.path[i] >> 4U) & 7U);
        const auto n = static_cast<std::int32_t>(m.path[i] & 0x0FU);
        x += step_x(dir) * n;
        y += step_y(dir) * n;
        ++i;
        if (std::max(std::abs(x - ship.x), std::abs(y - ship.y)) >= kDetourRadius) {
            break;
        }
    }
    if (std::max(std::abs(x - ship.x), std::abs(y - ship.y)) <= 2) {
        return false;
    }
    SeaPlan r;
    r.start_x = ship.x;
    r.start_y = ship.y;
    r.radius = kDetourRadius;
    r.aim_x = x;
    r.aim_y = y;
    const std::int32_t gx = x;
    const std::int32_t gy = y;
    r.goal = [gx, gy](std::int32_t cx, std::int32_t cy) { return cx == gx && cy == gy; };
    near_ships(ship.x, ship.y, kObstacleRange, [&](const Ship& o) {
        if (o.id != ship.id && std::abs(o.x - ship.x) < kObstacleRange && std::abs(o.y - ship.y) < kObstacleRange) {
            r.ships.push_back(SeaPlan::Obstacle{o.x, o.y, o.motion.phase == ShipMotion::Phase::Run});
        }
        return false;
    });
    SeaCourse course = m_planner.plan(m_sea, r);
    if (!course.reached_goal || course.path.empty()) {
        return false;
    }
    std::vector<std::uint8_t> rest(m.path.begin() + static_cast<std::ptrdiff_t>(i), m.path.end());
    m.path = std::move(course.path);
    for (const std::uint8_t c : rest) {
        if (m.path.size() >= SeaPlanner::kMaxRuns) {
            break;
        }
        m.path.push_back(c);
    }
    m.path_index = 0;
    return true;
}

void Fleet::update_island(Ship& ship) {
    // The island whose rectangle grown by 5 tiles holds the ship.
    std::uint16_t found = 0xFFFF;
    for (std::size_t i = 0; i < m_world->islands().size(); ++i) {
        const WorldIsland& isl = m_world->islands()[i];
        const auto x0 = static_cast<std::int32_t>(isl.x) - 5;
        const auto y0 = static_cast<std::int32_t>(isl.y) - 5;
        const auto x1 = static_cast<std::int32_t>(isl.x + isl.map.width()) + 5;
        const auto y1 = static_cast<std::int32_t>(isl.y + isl.map.height()) + 5;
        if (ship.x >= x0 && ship.x < x1 && ship.y >= y0 && ship.y < y1) {
            found = static_cast<std::uint16_t>(i);
            break;
        }
    }
    if (found == ship.near_island) {
        return;
    }
    ship.near_island = found;
    if (found == 0xFFFF) {
        return;
    }
    const Player* owner = m_world->player(ship.owner);
    if (owner == nullptr || (owner->role != PlayerRole::Human && owner->role != PlayerRole::Computer)) {
        return;
    }
    WorldIsland& isl = m_world->island(found);
    if (isl.discovered_by(ship.owner)) {
        return;
    }
    isl.discover(ship.owner);
    m_events.push_back(ShipEvent{ShipEvent::Kind::IslandDiscovered, ship.id, ship.owner, found});
    for (const City& c : m_world->cities()) {
        if (c.island != found) {
            continue;
        }
        const Player* p = m_world->player(c.owner);
        if (p != nullptr && m_world->behaviour(*p) == FactionBehaviour::Natives) {
            m_events.push_back(ShipEvent{ShipEvent::Kind::NativesFound, ship.id, ship.owner, found, c.index});
        } else if (p != nullptr && m_world->behaviour(*p) == FactionBehaviour::Pirates) {
            m_events.push_back(ShipEvent{ShipEvent::Kind::PiratesFound, ship.id, ship.owner, found, c.index});
        }
    }
}

void Fleet::gave_up_city(const Target& target) {
    // Every ship that gives up its way to a Kontor counts, whoever owns it; the fourth
    // makes a computer owner look for the culprits (with 1).
    City* city = m_world->city(target.island, target.city);
    if (city == nullptr || !city->warehouse_x || !city->warehouse_y || ++city->ships_gave_up < kGiveUpsToComplain) {
        return;
    }
    city->ships_gave_up = 0;
    const WorldIsland& isl = m_world->islands()[city->island];
    complain_of_ships(city->owner, static_cast<std::int32_t>(isl.x + *city->warehouse_x),
                      static_cast<std::int32_t>(isl.y + *city->warehouse_y), kKontorComplaint);
}

void Fleet::complain_of_ships(PlayerId owner, std::int32_t x, std::int32_t y, std::int32_t reach) {
    // Only a computer complains. It counts the other players' ships whose
    // distance max + 0.33 * min (in tiles, from the ship's centre) is at most `reach`, then blames the
    // first human with ships there that it is at peace with: event 10, -100 on its mood.
    const Player* complainer = m_world->player(owner);
    if (complainer == nullptr || complainer->role != PlayerRole::Computer) {
        return;
    }
    const std::int64_t cx = std::int64_t{x} * kShipUnit;
    const std::int64_t cy = std::int64_t{y} * kShipUnit;
    const std::int64_t limit = std::int64_t{reach} * kShipUnit * 100;
    std::vector<bool> near(m_world->players().size(), false);
    for (const Ship& s : m_world->ships()) {
        if (s.owner == owner || s.owner >= near.size() || s.cart()) {
            continue;
        }
        const std::int64_t dx = std::abs(std::int64_t{s.pos_x} - cx);
        const std::int64_t dy = std::abs(std::int64_t{s.pos_y} - cy);
        if (std::max(dx, dy) * 100 + std::min(dx, dy) * 33 <= limit) {
            near[s.owner] = true;
        }
    }
    Diplomacy& d = m_world->diplomacy();
    for (std::size_t p = 0; p < near.size(); ++p) {
        const auto b = static_cast<PlayerId>(p);
        if (near[p] && m_world->players()[p].role == PlayerRole::Human &&
            d.relation(owner, b).peace == Treaty::Signed) {
            d.add_event(owner, b, DiplomacyEventType::ShipsNearby, m_world->time_ms());
            return;
        }
    }
}

void Fleet::finish_order(Ship& ship) {
    trader_leave(ship);
    ship.previous = ship.target;
    ship.target = {};
    ship.motion.failures = 0;
    // The next order waiting (Shift + right click) that still makes sense.
    while (!ship.queue.empty() && ship.target.empty() && !ship.route.active) {
        const Target t = ship.queue.front();
        ship.queue.erase(ship.queue.begin());
        if (target_valid(t)) {
            ship.target = t;
            ship.order = t;
        }
    }
}

std::optional<std::uint32_t> Fleet::arrive_at_target(Ship& ship) {
    // What a ship does where it was going; how long it then stays.
    std::optional<std::uint32_t> wait;
    const Target arrived = ship.target;
    m_events.push_back(ShipEvent{ShipEvent::Kind::Arrived, ship.id, ship.owner});
    if (!ship.route.active && arrived.kind == Target::Kind::City && ship.owner == pirates() &&
        ship.owner != kNoPlayer) {
        // A pirate at its base unloads everything at price 0 and stays 8 s.
        if (City* c = m_world->city(arrived.island, arrived.city); c != nullptr && c->owner == ship.owner) {
            for (CargoSlot& slot : ship.cargo) {
                if (m_world->goods().is_trade(slot.ware)) {
                    c->ensure_ware(slot.ware);
                    const std::uint32_t n = std::min(slot.amount, c->free_space(slot.ware));
                    c->goods[slot.ware] += n;
                    c->book(slot.ware, GoodsFlow::ShippedIn, n);
                    slot.amount -= n;
                    if (slot.amount == 0) {
                        slot = CargoSlot{};
                    }
                }
            }
            wait = 8000;
        }
    } else if (ship.trader && arrived.kind == Target::Kind::City && !ship.route.active) {
        if (City* c = m_world->city(arrived.island, arrived.city)) {
            c->trader_inbound = false;
            if (trader_trade(ship, *c)) {
                wait = 8000;  // the trader stays 8 s after trading
            }
        }
    } else if (ship.route.active) {
        const RouteStop& stop = ship.route.stops[ship.route.current % TradeRoute::kStops];
        if (stop.target == arrived) {
            if (arrived.kind == Target::Kind::City) {
                if (route_city_stop(ship, stop)) {
                    wait = kKontorStopMs;
                }
            } else if (arrived.kind == Target::Kind::Ship) {
                if (route_ship_stop(ship, stop)) {
                    wait = kShipStopMs;
                }
            }
        }
        if (ship.route.one_way) {
            bool later = false;
            for (std::size_t s = ship.route.current + 1U; s < TradeRoute::kStops; ++s) {
                later = later || !ship.route.stops[s].target.empty();
            }
            if (!later) {
                ship.route.active = false;
                ship.order = arrived;
            }
        }
    }
    finish_order(ship);
    return wait;
}

void Fleet::choose_next_target(Ship& ship) {
    // A ship without an order sails on by its route, as the free trader or as a pirate.
    if (ship.route.active) {
        route_next_stop(ship);
    } else if (ship.trader) {
        trader_choose(ship);
    } else if (ship.owner == pirates() && ship.owner != kNoPlayer && !(m_directed && m_directed(ship.id))) {
        pirate_choose(ship);
    }
}

bool Fleet::ship_ahead(const Ship& ship) const {
    // Another ship within kLookAhead in the direction the ship sails.
    bool ahead = false;
    near_ships(ship.x, ship.y, kLookAhead, [&](const Ship& o) {
        if (o.id == ship.id) {
            return false;
        }
        const std::int32_t dx = o.x - ship.x;
        const std::int32_t dy = o.y - ship.y;
        if (std::abs(dx) >= kLookAhead || std::abs(dy) >= kLookAhead || (dx == 0 && dy == 0)) {
            return false;
        }
        const std::int32_t sx = step_x(ship.direction);
        const std::int32_t sy = step_y(ship.direction);
        ahead = (sx != 0 && dx * sx > 0) || (sy != 0 && dy * sy > 0);
        return ahead;
    });
    return ahead;
}

void Fleet::decide(Ship& ship) {
    if (ship.cart()) {
        decide_cart(ship);
        return;
    }
    ShipMotion& m = ship.motion;
    if (ship.sinking) {
        return;
    }
    // A turn in place continues with the run it was for.
    update_island(ship);
    const bool path_done = m.path_index >= m.path.size();
    if (ship.held) {
        if (path_done || !start_next_run(ship)) {
            start_wait(ship, kIdleMs);
        }
        return;
    }
    if (!ship.target.empty() && !target_valid(ship.target)) {
        finish_order(ship);
    }
    if (naval_combat(ship)) {
        return;  // turning to the broadside or standing after a shot
    }
    std::optional<std::uint32_t> wait;
    bool want_plan = false;
    std::int32_t radius = kPlanRadius;
    if (path_done) {
        if (!ship.target.empty() && at_target(ship, ship.target)) {
            wait = arrive_at_target(ship);
        }
        if (ship.target.empty() && !wait) {
            choose_next_target(ship);
        }
        if (!ship.target.empty() && !wait && !at_target(ship, ship.target)) {
            want_plan = true;
        }
    } else if (ship.target.kind == Target::Kind::Ship) {
        // Following a moving ship: a new course at most every 2 s.
        const std::uint64_t now = m_world->time_ms() * 1000ULL;
        if (now >= m.next_follow_us && m_plans_left != 0 && !at_target(ship, ship.target)) {
            m.next_follow_us = now + kFollowMs * 1000ULL;
            want_plan = true;
            radius = kFollowRadius;
        }
    } else if (ship_ahead(ship)) {
        // Another ship ahead: wait a moment (1 in 16) or sail round it.
        if ((m_world->random(RandomStream::Ships).next() & 15U) == 0) {
            wait = kAheadWaitMs;
        } else {
            local_detour(ship);
        }
    }
    if (want_plan && m_plans_left == 0) {
        // No search left in this tick: the ship tries again in the next one.
        start_wait(ship, std::max<std::uint32_t>(1, m_tick_ms));
        return;
    }
    if (want_plan) {
        --m_plans_left;
        const Planned planned = plan(ship, radius);
        if (planned != Planned::Way) {
            ++m.failures;
            if (m.failures > kMaxFailures) {
                m_events.push_back(ShipEvent{ShipEvent::Kind::GaveUp, ship.id, ship.owner});
                if (ship.target.kind == Target::Kind::City) {
                    gave_up_city(ship.target);
                }
                if (ship.route.active) {
                    route_next_stop(ship);  // the next stop instead of trying forever
                } else {
                    finish_order(ship);
                }
            }
            // Ships in the way move on: waiting longer for them keeps a ship from giving up behind
            // one that trades in a narrow inlet; a lasting blockade is still given up (S-121).
            start_wait(ship, planned == Planned::ShipsInWay ? kBlockedMs : kNoWayMs);
            return;
        }
        m.radius = static_cast<std::uint8_t>(radius);
    }
    if (wait) {
        m.path.clear();
        m.path_index = 0;
        start_wait(ship, *wait);
        return;
    }
    if (!start_next_run(ship)) {
        start_wait(ship, kIdleMs);
    }
}

// ---- Orders ------------------------------------------------------------------------------------

bool Fleet::order(const ShipOrder& o) {
    Ship* ship = m_world->ship(o.ship);
    if (ship == nullptr || ship->sinking || (o.player != kNoPlayer && ship->owner != o.player)) {
        return false;
    }
    if (!o.target.empty() && !target_valid(o.target)) {
        return false;
    }
    if (ship->cart() && (o.attack || o.attack_move || (!o.target.empty() && !cart_target_allowed(*ship, o.target)))) {
        return false;  // a cart only drives to places and cities on its island; it does not fight
    }
    if (o.queue && !o.target.empty() && !ship->route.active && !ship->held &&
        (!ship->target.empty() || !ship->queue.empty())) {
        if (ship->queue.size() >= kMaxQueuedOrders) {
            return false;
        }
        ship->queue.push_back(o.target);
        return true;
    }
    ship->queue.clear();
    ship->attack_move = o.attack_move && !o.target.empty();
    ship->hold = false;
    ShipMotion& m = ship->motion;
    // The running run (at most two tiles) ends; then the ship plans.
    m.path.clear();
    m.path_index = 0;
    m.failures = 0;
    if (m.phase == ShipMotion::Phase::Wait) {
        m.elapsed_us = m.duration_us;
    }
    ship->held = false;
    if (ship->repairing) {
        ship->repairing = false;
    }
    stop_exploring(*ship);  // every order ends the exploring
    ship->route.active = false;
    ship->previous = Target::position(ship->x, ship->y);
    ship->target = o.target;
    ship->order = o.target;
    return true;
}

bool Fleet::edit_route(const RouteEdit& e) {
    Ship* ship = m_world->ship(e.ship);
    if (ship == nullptr || (e.player != kNoPlayer && ship->owner != e.player)) {
        return false;
    }
    TradeRoute& route = ship->route;
    switch (e.op) {
        case RouteEdit::Op::SetStop:
            if (e.stop >= TradeRoute::kStops || (!e.target.empty() && !target_valid(e.target))) {
                return false;
            }
            if (ship->cart() &&
                (e.stop >= kCartStops || (!e.target.empty() && (e.target.kind != Target::Kind::City ||
                                                                !cart_target_allowed(*ship, e.target))))) {
                return false;  // a cart's four stops are the cities of its island (0x36 references)
            }
            if (e.player != kNoPlayer && e.target.kind == Target::Kind::Ship) {
                // Goods between two ships move without payment: a player's route reaches only
                // their own ships (the pirates' plunder routes are set by the simulation).
                const Ship* other = m_world->ship(e.target.ship);
                if (other == nullptr || other->owner != e.player) {
                    return false;
                }
            }
            route.stops[e.stop] = RouteStop{e.target, {}};
            route.current = 0;  // the original starts the route anew (UI)
            return true;
        case RouteEdit::Op::ClearStop:
            if (e.stop == 0xFF) {
                route.stops = {};
            } else if (e.stop < TradeRoute::kStops) {
                route.stops[e.stop] = {};
            } else {
                return false;
            }
            return true;
        case RouteEdit::Op::SetEntry:
            if (e.stop >= TradeRoute::kStops || e.entry >= RouteStop::kEntries ||
                (e.value.ware != 0 && !m_world->goods().is_trade(e.value.ware))) {
                return false;  // routes carry goods; soldiers board and land by their own orders
            }
            route.stops[e.stop].entries[e.entry] = e.value;
            return true;
        case RouteEdit::Op::Start: {
            if (!route.has_stops()) {
                return false;
            }
            // Op 0x28: from the current stop, white flag and repair cancelled, a fresh course.
            ship->previous = Target::position(ship->x, ship->y);
            ship->held = false;
            ship->repairing = false;
            stop_exploring(*ship);
            ship->motion.path.clear();
            ship->motion.path_index = 0;
            ship->motion.failures = 0;
            if (ship->motion.phase == ShipMotion::Phase::Wait) {
                ship->motion.elapsed_us = ship->motion.duration_us;
            }
            route.active = true;
            route.problem_rounds = 0;
            ship->queue.clear();
            const RouteStop& stop = route.stops[route.current % TradeRoute::kStops];
            ship->target = target_valid(stop.target) ? stop.target : Target{};
            return true;
        }
        case RouteEdit::Op::Stop:
            route.active = false;
            return true;
        case RouteEdit::Op::OneWay:
            route.one_way = e.on;
            return true;
        case RouteEdit::Op::CopyFrom: {
            // Open1602: the stops of another ship of the same owner, cities only (no patrol points,
            // no ship to meet), from the first stop on.
            const Ship* source = e.target.kind == Target::Kind::Ship ? m_world->ship(e.target.ship) : nullptr;
            if (source == nullptr || source == ship || source->owner != ship->owner || source->cart() != ship->cart()) {
                return false;
            }
            if (ship->cart()) {
                for (const RouteStop& from : source->route.stops) {
                    if (from.target.kind == Target::Kind::City && !cart_target_allowed(*ship, from.target)) {
                        return false;  // the stops of a cart on another island
                    }
                }
            }
            std::array<RouteStop, TradeRoute::kStops> stops{};
            bool any = false;
            for (std::size_t s = 0; s < TradeRoute::kStops; ++s) {
                const RouteStop& from = source->route.stops[s];
                if (from.target.kind == Target::Kind::City && target_valid(from.target)) {
                    stops[s] = from;
                    any = true;
                }
            }
            if (!any) {
                return false;
            }
            route.stops = stops;
            route.current = 0;
            while (route.stops[route.current].target.empty()) {
                ++route.current;
            }
            route.problem = false;
            route.problem_rounds = 0;
            if (route.active) {
                // A running route sails to its new first stop at once.
                ship->target = route.stops[route.current].target;
                ship->motion.path.clear();
                ship->motion.path_index = 0;
                ship->motion.failures = 0;
            }
            return true;
        }
    }
    return false;
}

// ---- Trade at Kontors --------------------------------------------------------------------------

bool Fleet::may_trade(PlayerId ship_owner, PlayerId city_owner) const {
    if (ship_owner == city_owner) {
        return true;
    }
    const Player* city = m_world->player(city_owner);
    const Player* ship = m_world->player(ship_owner);
    if (city == nullptr || ship == nullptr) {
        return false;
    }
    // A trade treaty of the ship's owner with the Kontor's; the free trader trades
    // everywhere.
    return m_world->behaviour(*ship) == FactionBehaviour::Trader ||
           m_world->diplomacy().may_trade(ship_owner, city_owner);
}

std::uint32_t Fleet::move_goods(Ship& ship, City& city, std::uint8_t ware, bool to_ship, std::uint32_t amount,
                                std::uint8_t hold, bool limits, std::uint16_t price) {
    // The city side and the ship side in one step: the ship's room is
    // checked before anything leaves the Kontor, so no goods are lost and nobody pays for what does
    // not fit (S-073).
    city.ensure_ware(ware);
    const City::Trade* trade = city.trade_of(ware);
    const Player* owner = m_world->player(city.owner);
    const bool natives = owner != nullptr && m_world->behaviour(*owner) == FactionBehaviour::Natives;
    std::uint32_t n = 0;
    if (to_ship) {
        std::uint32_t avail = city.stock(ware);
        if (limits) {
            if (trade == nullptr || !trade->sell) {
                return 0;
            }
            const std::uint32_t keep = trade->keep + city.reserved_of(ware);
            avail = avail > keep ? avail - keep : 0;
        }
        n = std::min({avail, amount, space_for(ship, ware, hold)});
        if (natives && limits) {
            // Natives give goods for goodwill: a ton costs about a third of the base price. A
            // player without goodwill there gets nothing.
            const std::uint64_t goodwill = ship.owner < city.goodwill.size() ? city.goodwill[ship.owner] : 0U;
            const std::uint64_t per_ton = std::max<std::uint32_t>(1, m_world->goods().price(ware) * 42U / 128U);
            n = static_cast<std::uint32_t>(std::min<std::uint64_t>(n, goodwill * kTon / per_ton));
        }
        n -= n % kTon;  // whole tons
        if (n == 0) {
            return 0;
        }
        if (price != 0) {
            Player* buyer = m_world->player(ship.owner);
            if (buyer != nullptr && ship.owner != city.owner) {
                // Routes check the money too (the original only in the window, S-074).
                const std::int64_t money = std::max<std::int64_t>(0, buyer->money);
                const auto affordable = static_cast<std::uint32_t>(std::min<std::int64_t>(n, money * kTon / price));
                n = affordable - affordable % kTon;
                if (n == 0) {
                    return 0;
                }
                const std::int64_t value = std::int64_t{price} * n / kTon;
                pay_for_trade(*buyer, value);
                if (Player* seller = m_world->player(city.owner); seller != nullptr && !natives) {
                    earn_from_trade(*seller, value);
                    city.sales += 32 * value;
                    city.book(ware, GoodsFlow::SoldValue, static_cast<std::uint32_t>(value));
                }
            }
        }
        if (natives && limits && ship.owner < city.goodwill.size()) {
            const std::uint64_t per_ton = std::max<std::uint32_t>(1, m_world->goods().price(ware) * 42U / 128U);
            const auto used = static_cast<std::uint32_t>(per_ton * n / kTon);
            city.goodwill[ship.owner] = city.goodwill[ship.owner] > used ? city.goodwill[ship.owner] - used : 0;
        }
        city.goods[ware] -= n;
        city.book(ware, ship.owner == city.owner ? GoodsFlow::ShippedOut : GoodsFlow::Sold, n);
        add_cargo(ship, ware, n, price);
    } else {
        const std::uint32_t have = [&] {
            std::uint32_t h = 0;
            const std::size_t holds_n = holds(ship);
            for (std::size_t i = 0; i < holds_n; ++i) {
                if (ship.cargo[i].ware == ware && (hold == 0xFF || i == hold)) {
                    h += ship.cargo[i].amount;
                }
            }
            return h;
        }();
        std::uint32_t accept = 0;
        if (limits) {
            if (trade == nullptr || !trade->buy) {
                return 0;
            }
            const std::uint32_t stock = city.stock(ware);
            const std::uint32_t limit = trade->limit + city.reserved_of(ware);
            accept = limit > stock ? limit - stock : 0;
            accept = std::min(accept, city.free_space(ware));  // never above the room (S-075)
        } else {
            accept = city.free_space(ware);
        }
        n = std::min({accept, amount, have});
        if (accept < kTon) {
            n = 0;
        }
        n -= n % kTon;
        if (n == 0) {
            return 0;
        }
        if (price != 0 && ship.owner != city.owner) {
            Player* buyer = m_world->player(city.owner);
            if (buyer != nullptr && !natives) {
                const std::int64_t money = std::max<std::int64_t>(0, buyer->money);
                const auto affordable = static_cast<std::uint32_t>(std::min<std::int64_t>(n, money * kTon / price));
                n = affordable - affordable % kTon;
                if (n == 0) {
                    return 0;
                }
                const std::int64_t value = std::int64_t{price} * n / kTon;
                pay_for_trade(*buyer, value);
                city.purchases += 32 * value;
                city.book(ware, GoodsFlow::BoughtValue, static_cast<std::uint32_t>(value));
            }
            if (Player* seller = m_world->player(ship.owner); seller != nullptr && !ship.trader) {
                earn_from_trade(*seller, std::int64_t{price} * n / kTon);
            }
        }
        if (natives && limits && ship.owner < city.goodwill.size()) {
            const std::uint64_t raised =
                city.goodwill[ship.owner] + std::uint64_t{m_world->goods().price(ware)} * n / kTon;
            city.goodwill[ship.owner] = static_cast<std::uint32_t>(std::min<std::uint64_t>(raised, kMaxNativeGoodwill));
        }
        remove_cargo(ship, ware, n, hold);
        city.goods[ware] += n;
        city.book(ware, ship.owner == city.owner ? GoodsFlow::ShippedIn : GoodsFlow::Bought, n);
    }
    if (price != 0 && ship.owner != city.owner && !natives) {
        // Trade makes friends: the Kontor's owner by half the value, the
        // ship's owner by a third.
        const auto value = static_cast<std::uint32_t>(std::uint64_t{price} * n / kTon);
        m_world->diplomacy().add_trade(city.owner, ship.owner, value / 2);
        m_world->diplomacy().add_trade(ship.owner, city.owner, value * 85 / 128 / 2);
    }
    m_events.push_back(ShipEvent{ShipEvent::Kind::Traded, ship.id, ship.owner, city.island, city.index, ware,
                                 to_ship ? std::int64_t{n} : -std::int64_t{n}, std::int64_t{price} * n / kTon});
    return n;
}

std::uint32_t Fleet::transfer(const CargoTransfer& t) {
    Ship* ship = m_world->ship(t.ship);
    if (ship == nullptr || (t.player != kNoPlayer && ship->owner != t.player) || !m_world->goods().is_trade(t.ware)) {
        return 0;
    }
    // The Kontor the ship lies at: the player's own first, else one it may trade with.
    std::optional<std::size_t> found;
    if (ship->cart()) {
        // A cart at a market or Kontor of its own city exchanges goods, at a foreign one it trades
        // with a trade treaty (TRADER.GAD 59004 and 59003).
        for (const std::size_t i : cities_near_cart(*m_world, *m_buildings, ship->x, ship->y)) {
            const City& c = m_world->cities()[i];
            if (c.owner == ship->owner) {
                found = i;
                break;
            }
            if (!found && may_trade(ship->owner, c.owner)) {
                found = i;
            }
        }
    }
    for (std::size_t i = 0; i < m_world->cities().size() && !ship->cart(); ++i) {
        const City& c = m_world->cities()[i];
        if (!c.warehouse_x || !c.warehouse_y ||
            !building_in_reach(*m_world, *m_buildings, c.island, *c.warehouse_x, *c.warehouse_y, ship->x, ship->y) ||
            !kontor_open(*m_world, *m_buildings, c)) {
            continue;
        }
        if (c.owner == ship->owner) {
            found = i;
            break;
        }
        if (!found && may_trade(ship->owner, c.owner)) {
            found = i;
        }
    }
    // S-012 brought back (OriginalBugs::remote_trade): the Kontor of the window, wherever the ship is.
    if (!found && t.kontor_island && m_world->original_bugs().remote_trade && !ship->cart()) {
        for (std::size_t i = 0; i < m_world->cities().size(); ++i) {
            const City& c = m_world->cities()[i];
            if (c.island == *t.kontor_island && c.index == t.kontor_city && c.warehouse_x && c.warehouse_y &&
                (c.owner == ship->owner || may_trade(ship->owner, c.owner))) {
                found = i;
                break;
            }
        }
    }
    if (!found) {
        return 0;
    }
    const City& found_city = m_world->cities()[*found];
    City& city = *m_world->city(found_city.island, found_city.index);
    const bool own = city.owner == ship->owner;
    std::uint16_t price = 0;
    if (!own) {
        const City::Trade* trade = city.trade_of(t.ware);
        price = trade == nullptr ? 0 : (t.to_ship ? trade->sell_price : trade->buy_price);
    }
    const std::uint32_t n = move_goods(*ship, city, t.ware, t.to_ship, t.amount, t.hold, !own, price);
    if (n == 0) {
        m_events.push_back(ShipEvent{ShipEvent::Kind::Refused, ship->id, ship->owner, city.island, city.index, t.ware});
    }
    return n;
}

bool Fleet::set_trade(const TradeSetting& s) {
    City* city = m_world->city(s.island, s.city);
    if (city == nullptr || (s.player != kNoPlayer && city->owner != s.player) || !m_world->goods().is_trade(s.ware)) {
        return false;
    }
    if (city->trade.size() <= s.ware) {
        city->trade.resize(std::size_t{s.ware} + 1);
    }
    City::Trade& t = city->trade[s.ware];
    const Player* owner = m_world->player(city->owner);
    const bool natives = owner != nullptr && m_world->behaviour(*owner) == FactionBehaviour::Natives;
    // A Kontor trades in at most Maxware wares to sell and its second value to buy (HAUS +0x45,
    // +0x46): the original's window allows no more, Open1602's command checks it.
    if (owner != nullptr && (owner->role == PlayerRole::Human || owner->role == PlayerRole::Computer)) {
        if (const assets::BuildingType* kontor = kontor_of(*m_world, *m_buildings, *city);
            kontor != nullptr && (kontor->max_wares != 0 || kontor->max_wares2 != 0)) {
            std::size_t sells = 0;
            std::size_t buys = 0;
            for (const City::Trade& o : city->trade) {
                sells += o.sell ? 1U : 0U;
                buys += o.buy ? 1U : 0U;
            }
            if ((s.mode == TradeSetting::Mode::Sell && !t.sell && sells >= kontor->max_wares) ||
                (s.mode == TradeSetting::Mode::Buy && !t.buy && buys >= kontor->max_wares2)) {
                return false;
            }
        }
    }
    const std::uint16_t base = m_world->goods().price(s.ware);
    // Prices range from 300/1024 of the base price to the base price (UI 0x84E6-0x84E8).
    const auto clamp_price = [&](std::uint16_t p) {
        return static_cast<std::uint16_t>(std::clamp<std::uint32_t>(p, base * 300U / 1024U, base));
    };
    switch (s.mode) {
        case TradeSetting::Mode::None:
            t.sell = false;
            t.buy = false;
            return true;
        case TradeSetting::Mode::Sell:
            if (!t.sell) {  // switching on: keep nothing, half the base price (sub 0x20)
                t.keep = 0;
                t.sell_price = natives ? 0 : static_cast<std::uint16_t>(base * 64U / 128U);
            }
            t.sell = true;
            t.buy = false;
            if (s.price && !natives) {
                t.sell_price = clamp_price(*s.price);
            }
            if (s.amount) {
                t.keep = *s.amount;
            }
            return true;
        case TradeSetting::Mode::Buy:
            if (!t.buy) {  // up to the room, three quarters of the base price (sub 0x1D)
                t.limit = city->capacity;
                t.buy_price = natives ? 0 : static_cast<std::uint16_t>(base * 96U / 128U);
            }
            t.buy = true;
            t.sell = false;
            if (s.price && !natives) {
                t.buy_price = clamp_price(*s.price);
            }
            if (s.amount) {
                t.limit = *s.amount;
            }
            return true;
    }
    return false;
}

// ---- Trade routes ------------------------------------------------------------------------------

void Fleet::route_next_stop(Ship& ship) {
    // The next stop with a target, wrapping round; three rounds with refused transfers
    // warn the player.
    TradeRoute& route = ship.route;
    std::size_t next = route.current;
    for (std::size_t attempt = 0; attempt < TradeRoute::kStops; ++attempt) {
        ++next;
        if (next >= TradeRoute::kStops) {
            if (route.problem) {
                ++route.problem_rounds;
                if (route.problem_rounds >= 3) {
                    m_events.push_back(ShipEvent{ShipEvent::Kind::RouteWarning, ship.id, ship.owner});
                    route.problem_rounds = 1;
                }
                route.problem = false;
            } else {
                route.problem_rounds = 0;
            }
            if (route.one_way) {
                route.active = false;
                return;
            }
            next = 0;
        }
        if (!route.stops[next].target.empty()) {
            break;
        }
    }
    route.current = static_cast<std::uint8_t>(next % TradeRoute::kStops);
    const RouteStop& stop = route.stops[route.current];
    ship.target = target_valid(stop.target) ? stop.target : Target{};
    ship.held = false;
    ship.motion.path.clear();
    ship.motion.path_index = 0;
    ship.motion.failures = 0;
}

bool Fleet::route_city_stop(Ship& ship, const RouteStop& stop) {
    City* city = m_world->city(stop.target.island, stop.target.city);
    if (city == nullptr || !may_trade(ship.owner, city->owner)) {
        return false;  // no treaty: skipped without a warning, like the original
    }
    city->ships_gave_up = 0;  // a ship got through
    const bool foreign = city->owner != ship.owner;
    bool requested = false;
    const std::size_t entries = usable_route_entries(ship, *m_figures);
    // Unloads first, then loads; entries of the same ware add up.
    for (int pass = 0; pass < 2; ++pass) {
        const bool unload = pass == 0;
        std::array<bool, RouteStop::kEntries> done{};
        for (std::size_t e = 0; e < entries; ++e) {
            const RouteEntry& entry = stop.entries[e];
            if (done[e] || entry.ware == 0 || entry.unload != unload) {
                continue;
            }
            std::uint32_t amount = 0;
            for (std::size_t k = e; k < entries; ++k) {
                if (stop.entries[k].ware == entry.ware && stop.entries[k].unload == unload) {
                    amount += stop.entries[k].amount;
                    done[k] = true;
                }
            }
            const std::uint32_t have = ship.load(entry.ware);
            std::uint32_t request = 0;
            if (unload) {
                request = std::min(amount, have);  // deliver up to the amount
            } else {
                request = amount > have ? std::min(amount - have, space_for(ship, entry.ware)) : 0U;  // fill up
            }
            if (request < kTon) {
                // Already full or empty enough: not a problem of the route (the original warns, S-076).
                continue;
            }
            requested = true;
            const City::Trade* trade = city->trade_of(entry.ware);
            std::uint16_t price = 0;
            if (foreign) {
                price = trade == nullptr ? 0 : (unload ? trade->buy_price : trade->sell_price);
            }
            const std::uint32_t moved = move_goods(ship, *city, entry.ware, !unload, request, 0xFF, foreign, price);
            if (moved == 0) {
                ship.route.problem = true;
                m_events.push_back(
                    ShipEvent{ShipEvent::Kind::Refused, ship.id, ship.owner, city->island, city->index, entry.ware});
            }
        }
    }
    return requested;
}

bool Fleet::route_ship_stop(Ship& ship, const RouteStop& stop) {
    Ship* other = m_world->ship(stop.target.ship);
    // Only between ships of one owner, or the pirates' plunder (Fleet::plunder).
    const bool pirate = ship.owner == pirates() && ship.owner != kNoPlayer;
    if (other == nullptr || other == &ship || (other->owner != ship.owner && !pirate)) {
        return false;
    }
    // Goods between the two ships, no money (pirates plunder this way).
    bool moved_any = false;
    const std::size_t entries = usable_route_entries(ship, *m_figures);
    for (std::size_t k = 0; k < entries; ++k) {
        const RouteEntry& e = stop.entries[k];
        if (e.ware == 0) {
            continue;
        }
        Ship& from = e.unload ? ship : *other;
        Ship& to = e.unload ? *other : ship;
        const std::uint32_t n = std::min({e.amount, from.load(e.ware), space_for(to, e.ware)});
        if (n == 0) {
            continue;
        }
        remove_cargo(from, e.ware, n, 0xFF);
        add_cargo(to, e.ware, n, 0);
        moved_any = true;
        if (ship.owner == pirates() && ship.owner != kNoPlayer) {
            m_world->diplomacy().add_trade(ship.owner, other->owner, n / 2);  // plunder pleases the pirates
        }
    }
    return moved_any;
}

}  // namespace o1602::sim

namespace o1602::sim {

std::uint16_t Fleet::state_version() const noexcept {
    return m_trader_choices != 0 || !m_trader_chosen.empty() ? 5 : 4;
}

void Fleet::save(core::ByteWriter& out) const {
    const bool trader_choices = state_version() == 5;
    out.u32(m_trader_ms);
    out.u32(m_yard_ms);
    out.u32(m_tools_given);
    out.u32(m_tools_quota);
    out.u32(m_name_index);
    out.u32(m_pirate_ms);
    out.u32(m_pirate_spawn_ms);
    out.boolean(m_pirate_armed);
    // Version 3: running explorations (the original loses them when saving, Open1602 keeps them).
    std::uint32_t exploring = 0;
    for (const Ship& s : m_world->ships()) {
        exploring += s.explore_steps != 0 ? 1U : 0U;
    }
    out.u32(exploring);
    for (const Ship& s : m_world->ships()) {
        if (s.explore_steps != 0) {
            out.u32(s.id);
            out.u8(s.explore_steps);
            out.u64(s.explore_next_ms);
        }
    }
    // Version 4: the market statistics of the last pass, which set the free trader's prices until
    // the next one (a loaded game must trade as the running one did).
    out.u32(static_cast<std::uint32_t>(m_market.size()));
    for (const Market& m : m_market) {
        out.u64(m.demand);
        out.u64(m.demand_value);
        out.u64(m.offer);
        out.u64(m.offer_value);
    }
    // Version 5: the free traders' choices under Rules::trader_reach.
    if (trader_choices) {
        out.u32(m_trader_choices);
        out.u32(static_cast<std::uint32_t>(m_trader_chosen.size()));
        for (const auto& [kontor, choice] : m_trader_chosen) {
            out.u32(kontor);
            out.u32(choice);
        }
    }
}

core::Status Fleet::load(core::ByteReader& reader, std::uint16_t version) {
    if (version < 1 || version > kMaxStateVersion) {
        return core::make_error(core::ErrorCode::Unsupported, "FLET: version " + std::to_string(version));
    }
    StateIn in(reader);
    const std::uint32_t trader = in.u32();
    const std::uint32_t yard = in.u32();
    const std::uint32_t tools = in.u32();
    const std::uint32_t quota = in.u32();
    const std::uint32_t name = in.u32();
    // Read whole first, then taken over: a failed load changes neither the fleet nor the ships.
    std::uint32_t pirate_ms = m_pirate_ms;
    std::uint32_t pirate_spawn_ms = m_pirate_spawn_ms;
    bool pirate_armed = m_pirate_armed;
    if (version >= 2) {
        pirate_ms = in.u32() % 10'000U;
        pirate_spawn_ms = in.u32();
        pirate_armed = in.boolean();
    }
    struct Exploring {
        std::uint32_t id;
        std::uint8_t steps;
        std::uint64_t next;
    };
    std::vector<Exploring> exploring;
    if (version >= 3) {
        in.list(exploring, 13, [&](Exploring& e) {
            e.id = in.u32();
            e.steps = in.u8();
            e.next = in.u64();
        });
    }
    std::vector<Market> market;
    if (version >= 4) {
        in.list(market, 32, [&](Market& m) {
            m.demand = in.u64();
            m.demand_value = in.u64();
            m.offer = in.u64();
            m.offer_value = in.u64();
        });
    }
    std::uint32_t trader_choices = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> trader_chosen;
    if (version >= 5) {
        trader_choices = in.u32();
        in.list(trader_chosen, 8, [&](std::pair<std::uint32_t, std::uint32_t>& c) {
            c.first = in.u32();
            c.second = in.u32();
        });
    }
    if (!in.ok()) {
        return core::make_error(core::ErrorCode::InvalidFormat, "FLET: cut off");
    }
    m_pirate_ms = pirate_ms;
    m_pirate_spawn_ms = pirate_spawn_ms;
    m_pirate_armed = pirate_armed;
    for (const Exploring& e : exploring) {
        if (Ship* s = m_world->ship(e.id)) {
            s->explore_steps = e.steps;
            s->explore_next_ms = e.next;
        }
    }
    m_market = std::move(market);
    m_trader_choices = trader_choices;
    m_trader_chosen.clear();
    for (const auto& [kontor, choice] : trader_chosen) {
        m_trader_chosen[kontor] = choice;
    }
    // Within their periods (trader check 10 s, shipyard second), like the pirates' clock: a larger
    // value would make the first tick catch up millions of shipyard seconds.
    m_trader_ms = trader % 10'000U;
    m_yard_ms = yard % 1000U;
    m_tools_given = tools;
    m_tools_quota = quota;
    m_name_index = name;
    return core::Ok{};
}

}  // namespace o1602::sim
