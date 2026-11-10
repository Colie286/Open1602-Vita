// SPDX-License-Identifier: GPL-3.0-or-later
//
// The trade cart, "fahrender Händler" (docs/re/ships.md#the-trade-cart): a player's TRADER1 that
// the city panel's button 43026 sends out from the Kontor. The original keeps it in the ship table
// (SHIP4 kind 5) with a ship's name, holds and trade route, and moves it with its own logic
// (Cart_Logic 0x00456410): on land, through the walk grid of the carriers (0x0046EBA0), to a
// clicked tile or within 2 tiles of a market or Kontor (0x00456870). There it trades as a ship
// does at a Kontor: its owner's goods for nothing, a foreign city's with a trade treaty.
#include <algorithm>
#include <cstdlib>
#include <vector>

#include "o1602/sim/ships.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t kCartIdleMs = 1000;   // an idle cart decides again after a second
constexpr std::uint32_t kCartNoWayMs = 2000;  // after a plan without a way
constexpr std::uint8_t kCartMaxFailures = 2;  // the order is given up after the third failed plan
/// A tile takes 6 250 000 * Wegspeed / Speed microseconds straight, sqrt 2 times that diagonally
/// (0x0044A690: 0.16 * Speed / Wegspeed tiles per second).
constexpr std::uint64_t kCartStraightUs = 6'250'000;
constexpr std::uint64_t kCartDiagonalUs = 8'838'835;
constexpr std::uint8_t kFootprintCost = 40;  // a building it stands in (0x004710B0)
constexpr std::size_t kMaxCartRuns = 255;    // one tile per run; a longer way is planned on

/// The island whose rectangle holds the world tile, if any.
std::optional<std::uint16_t> island_of(const World& world, std::int32_t wx, std::int32_t wy) {
    if (wx < 0 || wy < 0 || !world.contains(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy))) {
        return std::nullopt;
    }
    const std::uint16_t island = world.cell(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy)).island;
    if (island == World::kSea || island >= world.islands().size()) {
        return std::nullopt;
    }
    return island;
}

/// The building on an island tile, if any.
const assets::BuildingType* building_on(const IslandMap& map, const assets::BuildingTable& buildings, std::int32_t x,
                                        std::int32_t y) {
    if (!map.contains(x, y)) {
        return nullptr;
    }
    const MapCell& c = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    return c.empty() ? nullptr : buildings.by_sprite(c.sprite);
}

/// Whether a cart can drive on the world tile.
bool drivable(const World& world, const assets::BuildingTable& buildings, std::int32_t wx, std::int32_t wy) {
    const auto island = island_of(world, wx, wy);
    if (!island) {
        return false;
    }
    const WorldIsland& isl = world.islands()[*island];
    const std::int32_t x = wx - static_cast<std::int32_t>(isl.x);
    const std::int32_t y = wy - static_cast<std::int32_t>(isl.y);
    const assets::BuildingType* b = building_on(isl.map, buildings, x, y);
    return b != nullptr &&
           walkable(*b, isl.map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)), WalkClass::Land);
}

bool trades_there(const assets::BuildingType* b) {
    return b != nullptr && (b->prod_kind == prod::kMarket || b->prod_kind == prod::kKontor);
}

}  // namespace

std::vector<std::size_t> cities_near_cart(const World& world, const assets::BuildingTable& buildings, std::int32_t wx,
                                          std::int32_t wy) {
    std::vector<std::size_t> out;
    const auto island = island_of(world, wx, wy);
    if (!island) {
        return out;
    }
    const WorldIsland& isl = world.islands()[*island];
    const std::int32_t lx = wx - static_cast<std::int32_t>(isl.x);
    const std::int32_t ly = wy - static_cast<std::int32_t>(isl.y);
    for (std::int32_t dy = -kCartReach; dy <= kCartReach; ++dy) {
        for (std::int32_t dx = -kCartReach; dx <= kCartReach; ++dx) {
            if (!trades_there(building_on(isl.map, buildings, lx + dx, ly + dy))) {
                continue;
            }
            const MapCell& c = isl.map.top(static_cast<std::uint32_t>(lx + dx), static_cast<std::uint32_t>(ly + dy));
            const City* city = c.city != MapCell::kNoCity ? world.city(*island, c.city) : nullptr;
            if (city == nullptr) {
                continue;
            }
            const auto index = static_cast<std::size_t>(city - world.cities().data());
            if (std::find(out.begin(), out.end(), index) == out.end()) {
                out.push_back(index);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

const Ship* trade_cart_of(const World& world, PlayerId player, std::uint16_t island) {
    for (const Ship& s : world.ships()) {
        if (s.cart() && s.owner == player && !s.sinking && island_of(world, s.x, s.y) == island) {
            return &s;
        }
    }
    return nullptr;
}

const Ship* Fleet::cart_of(PlayerId player, std::uint16_t island) const {
    return trade_cart_of(*m_world, player, island);
}

std::uint32_t Fleet::request_cart(const CartRequest& r) {
    // 0x00440AC0 and CreateCart 0x00449300: at the city's Kontor, for its owner, one per island.
    City* city = m_world->city(r.island, r.city);
    const Player* player = m_world->player(r.player);
    if (city == nullptr || player == nullptr || !player->active() ||
        (player->role != PlayerRole::Human && player->role != PlayerRole::Computer) || city->owner != r.player ||
        !city->warehouse_x || !city->warehouse_y || r.island >= m_world->islands().size() ||
        cart_of(r.player, r.island) != nullptr) {
        return 0;
    }
    const assets::FigureType* type = m_figures->get(figure::kTradeCart);
    if (type == nullptr) {
        return 0;
    }
    const WorldIsland& isl = m_world->islands()[r.island];
    const std::uint32_t sx = isl.x + *city->warehouse_x;
    const std::uint32_t sy = isl.y + *city->warehouse_y;
    if ((r.x == sx && r.y == sy) || island_of(*m_world, r.x, r.y) != r.island) {
        return 0;  // a click on the Kontor itself sends nothing out; only to a tile of its island
    }
    Ship cart = make_ship(next_ship_name(), sx, sy, figure::kTradeCart, r.player, 4);
    cart.cargo.resize(type->ship.cargo_slots != 0 ? type->ship.cargo_slots : 3U);
    cart.health = type->ship.max_health;
    cart.previous = Target::city_of(r.island, r.city);
    cart.target = Target::position(r.x, r.y);
    cart.order = cart.target;
    m_world->add_ship(std::move(cart));
    return m_world->ships().back().id;
}

bool Fleet::cart_target_allowed(const Ship& ship, const Target& target) const {
    const auto island = island_of(*m_world, ship.x, ship.y);
    if (!island || !target_valid(target)) {
        return false;
    }
    switch (target.kind) {
        case Target::Kind::Position:
            return island_of(*m_world, static_cast<std::int32_t>(target.x), static_cast<std::int32_t>(target.y)) ==
                   island;
        case Target::Kind::City:
            return target.island == *island;
        default:
            return false;
    }
}

bool Fleet::cart_at(const Ship& ship, const Target& target) const {
    switch (target.kind) {
        case Target::Kind::Position: {
            // The tile itself, or next to it when a cart cannot drive there (a house, a field).
            const auto tx = static_cast<std::int32_t>(target.x);
            const auto ty = static_cast<std::int32_t>(target.y);
            if (ship.x == tx && ship.y == ty) {
                return true;
            }
            return std::max(std::abs(ship.x - tx), std::abs(ship.y - ty)) <= 1 &&
                   !drivable(*m_world, *m_buildings, tx, ty);
        }
        case Target::Kind::City: {
            const City* city = m_world->city(target.island, target.city);
            if (city == nullptr) {
                return false;
            }
            const auto index = static_cast<std::size_t>(city - m_world->cities().data());
            const std::vector<std::size_t> near = cities_near_cart(*m_world, *m_buildings, ship.x, ship.y);
            return std::find(near.begin(), near.end(), index) != near.end();
        }
        default:
            return false;
    }
}

std::uint32_t Fleet::cart_run_us(const Ship& ship, std::uint8_t direction) const {
    const assets::FigureType* t = type_of(ship);
    const std::uint64_t speed = t != nullptr && t->speed != 0 ? t->speed : 300U;
    const std::uint8_t speed_type = t != nullptr ? std::min<std::uint8_t>(t->speed_type, 3) : 2U;
    // The Wegspeed of the tile it leaves.
    std::uint64_t cost = kFootprintCost;
    if (const auto island = island_of(*m_world, ship.x, ship.y)) {
        const WorldIsland& isl = m_world->islands()[*island];
        const std::int32_t x = ship.x - static_cast<std::int32_t>(isl.x);
        const std::int32_t y = ship.y - static_cast<std::int32_t>(isl.y);
        const assets::BuildingType* b = building_on(isl.map, *m_buildings, x, y);
        if (b != nullptr &&
            walkable(*b, isl.map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)), WalkClass::Land)) {
            cost = b->walk_speed[speed_type] != 0 ? b->walk_speed[speed_type] : 32U;
        }
    }
    const std::uint64_t per_tile = ((direction & 1U) != 0 ? kCartDiagonalUs : kCartStraightUs) * cost / speed;
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(1, per_tile));
}

bool Fleet::plan_cart(Ship& ship) {
    // The A* of 0x0046D4D0 over the walk grid of the carriers (roads, open ground, ruins, squares,
    // bridges, piers, the middle of gates) of the whole island; Open1602's exact search.
    ShipMotion& m = ship.motion;
    m.path.clear();
    m.path_index = 0;
    const auto island = island_of(*m_world, ship.x, ship.y);
    if (!island) {
        return false;
    }
    const WorldIsland& isl = m_world->islands()[*island];
    const IslandMap& map = isl.map;
    const assets::FigureType* type = type_of(ship);
    Catchment area;
    area.centre = {static_cast<std::int32_t>(map.width() / 2), static_cast<std::int32_t>(map.height() / 2)};
    area.radius = static_cast<std::uint8_t>(std::min<std::uint32_t>(255, std::max(map.width(), map.height()) / 2 + 1));
    m_cart_paths.begin(map, *m_buildings, area, WalkClass::Land, type != nullptr ? type->speed_type : 2U, false);
    const auto ox = static_cast<std::int32_t>(isl.x);
    const auto oy = static_cast<std::int32_t>(isl.y);
    const auto goal_near = [&](std::int32_t x, std::int32_t y, std::int32_t reach) {
        for (std::int32_t dy = -reach; dy <= reach; ++dy) {
            for (std::int32_t dx = -reach; dx <= reach; ++dx) {
                const TilePos q{x + dx, y + dy};
                if (m_cart_paths.cell(q) == PathFinder::Cell::Walkable) {
                    m_cart_paths.set(q, PathFinder::Cell::Goal);
                }
            }
        }
    };
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const MapCell& c = map.top(x, y);
            const assets::BuildingType* b = c.empty() ? nullptr : m_buildings->by_sprite(c.sprite);
            if (b == nullptr) {
                continue;
            }
            const TilePos p{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
            if (b->kind == kind::kGate && c.owner != ship.owner) {
                // Open1602: the gates of a player who closed them stop foreign carts too.
                const Player* owner = m_world->player(c.owner);
                if (owner != nullptr && owner->gates_closed) {
                    m_cart_paths.set(p, PathFinder::Cell::Blocked);
                }
            }
            if (ship.target.kind == Target::Kind::City && trades_there(b) && c.city == ship.target.city &&
                *island == ship.target.island) {
                goal_near(p.x, p.y, kCartReach);
            }
        }
    }
    if (ship.target.kind == Target::Kind::Position) {
        const TilePos t{static_cast<std::int32_t>(ship.target.x) - ox, static_cast<std::int32_t>(ship.target.y) - oy};
        if (m_cart_paths.cell(t) == PathFinder::Cell::Walkable) {
            m_cart_paths.set(t, PathFinder::Cell::Goal);
        } else {
            goal_near(t.x, t.y, 1);
        }
    }
    const TilePos start{ship.x - ox, ship.y - oy};
    const auto end = m_cart_paths.search(start, [](TilePos) { return PathFinder::Answer::Accept; });
    std::vector<TilePos> tiles;
    if (!end || !m_cart_paths.path_to(*end, tiles) || tiles.empty()) {
        return false;
    }
    TilePos from = start;
    for (const TilePos& to : tiles) {
        if (m.path.size() >= kMaxCartRuns) {
            break;  // planned on from there
        }
        m.path.push_back(
            static_cast<std::uint8_t>((static_cast<std::uint32_t>(direction_between(from, to)) << 4U) | 1U));
        from = to;
    }
    return true;
}

void Fleet::decide_cart(Ship& ship) {
    // Cart_Logic 0x00456410: at the end of each tile whether it has arrived, the next stop of its
    // route, a way to the target. No sea, no fighting, no other ships in its way.
    ShipMotion& m = ship.motion;
    if (ship.sinking) {
        return;
    }
    if (!ship.target.empty() && !cart_target_allowed(ship, ship.target)) {
        finish_order(ship);
    }
    const bool path_done = m.path_index >= m.path.size();
    std::optional<std::uint32_t> wait;
    bool want_plan = false;
    if (path_done) {
        if (!ship.target.empty() && cart_at(ship, ship.target)) {
            wait = arrive_at_target(ship);
        }
        if (ship.target.empty() && !wait && ship.route.active) {
            route_next_stop(ship);
        }
        want_plan = !ship.target.empty() && !wait && !cart_at(ship, ship.target);
    }
    if (want_plan && m_plans_left == 0) {
        start_wait(ship, std::max<std::uint32_t>(1, m_tick_ms));
        return;
    }
    if (want_plan) {
        --m_plans_left;
        if (!plan_cart(ship)) {
            ++m.failures;
            if (m.failures > kCartMaxFailures) {
                m_events.push_back(ShipEvent{ShipEvent::Kind::GaveUp, ship.id, ship.owner});
                if (ship.route.active) {
                    route_next_stop(ship);
                } else {
                    finish_order(ship);
                }
            }
            start_wait(ship, kCartNoWayMs);
            return;
        }
    }
    if (wait) {
        m.path.clear();
        m.path_index = 0;
        start_wait(ship, *wait);
        return;
    }
    if (!start_next_run(ship)) {
        start_wait(ship, kCartIdleMs);
    }
}

}  // namespace o1602::sim
