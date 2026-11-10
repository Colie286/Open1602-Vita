// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shipyards (docs/re/ships.md, "Shipyard"): a switched-on yard with an order or a ship to repair runs
// production-like cycles of wood and cloth its pack donkey brings; the points of each cycle build
// the ship or become its health.
#include <algorithm>

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/ships.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t kSecondMs = 1000;
constexpr std::uint16_t kIdleSeconds = 11;        // a cycle that could not start waits this long
constexpr std::uint32_t kFetchThreshold = 0x180;  // material for three cycles or less: fetch more

constexpr std::uint8_t kLaunchesToComplain = 100;  // failed launches before a computer complains
constexpr std::int32_t kYardComplaint = 8;         // reach of the count round the shipyard (0x004287B0)
constexpr std::uint8_t kMaxIdle = 15;
constexpr std::uint8_t kSmallYardSize = 3;
constexpr auto kShipTypes = static_cast<std::uint8_t>(figure::kShips.size());

std::uint32_t scaled(std::uint32_t amount, std::uint8_t rate) noexcept {
    return amount * rate >> 7U;
}

}  // namespace

CostBuilding* Fleet::yard_at(std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    for (CostBuilding& c : m_world->cost_buildings()) {
        if (c.kind == CostBuilding::Kind::Shipyard && c.island == island && c.x == x && c.y == y) {
            return &c;
        }
    }
    return nullptr;
}

void Fleet::yard_delivery(std::uint16_t island, std::uint16_t x, std::uint16_t y, std::uint8_t ware,
                          std::uint32_t amount) {
    CostBuilding* yard = yard_at(island, x, y);
    if (yard == nullptr) {
        return;
    }
    yard->fetching = false;
    const WorldIsland& isl = m_world->islands()[island];
    const MapCell& cell = isl.map.top(x, y);
    const assets::BuildingType* b = cell.empty() ? nullptr : m_buildings->by_sprite(cell.sprite);
    // 0x0047D940: the Workstoff to the cloth, everything else to the wood; a waiting yard starts.
    if (b != nullptr && ware == b->raw2_ware && ware != b->raw_ware) {
        yard->cloth += amount;
    } else {
        yard->wood += amount;
    }
    if (yard->rate == 0) {
        yard->countdown = 0;
    }
}

bool Fleet::shipyard(const ShipyardOrder& o) {
    if (o.op == ShipyardOrder::Op::Repair) {
        Ship* ship = m_world->ship(o.ship);
        if (ship == nullptr || (o.player != kNoPlayer && ship->owner != o.player)) {
            return false;
        }
        CostBuilding* yard = yard_at(o.island, o.x, o.y);
        // Only at a yard of the ship's owner: the yard pays the repair with its own material.
        if (o.on && yard != nullptr &&
            m_world->islands()[yard->island].map.top(yard->x, yard->y).owner != ship->owner) {
            return false;
        }
        if (!o.on) {
            ship->repairing = false;
            if (yard != nullptr && yard->repair_ship == ship->id) {
                yard->repair_ship = 0;
            }
            return true;
        }
        // Ship op 0x24: only a ship lying in reach, only one ship per yard.
        if (yard == nullptr ||
            !building_in_reach(*m_world, *m_buildings, yard->island, yard->x, yard->y, ship->x, ship->y) ||
            (yard->repair_ship != 0 && yard->repair_ship != ship->id && m_world->ship(yard->repair_ship) != nullptr)) {
            return false;
        }
        const assets::FigureType* t = type_of(*ship);
        if (t == nullptr || ship->health >= t->ship.max_health) {
            return false;
        }
        yard->repair_ship = ship->id;
        ship->repairing = true;
        if (!yard->enabled) {
            yard->enabled = true;
            yard->countdown = 0;
        }
        return true;
    }
    CostBuilding* yard = yard_at(o.island, o.x, o.y);
    if (yard == nullptr) {
        return false;
    }
    const WorldIsland& isl = m_world->islands()[yard->island];
    const MapCell& cell = isl.map.top(yard->x, yard->y);
    if (o.player != kNoPlayer && cell.owner != o.player) {
        return false;
    }
    switch (o.op) {
        case ShipyardOrder::Op::Build: {
            if (o.ship_type >= kShipTypes || yard->order != CostBuilding::kNoOrder) {
                return false;
            }
            // The small yard builds only the small ships; the original checks this only in its
            // window (S-005), which OriginalBugs::small_yard_warships brings back.
            const assets::BuildingType* b = cell.empty() ? nullptr : m_buildings->by_sprite(cell.sprite);
            if (b != nullptr && b->size_x <= kSmallYardSize && (o.ship_type & 1U) != 0 &&
                !m_world->original_bugs().small_yard_warships) {
                return false;
            }
            const std::uint32_t limit = m_rules->max_ships_per_player;
            if (limit != 0 && fleet_size(cell.owner) >= limit) {
                m_events.push_back(ShipEvent{ShipEvent::Kind::FleetFull, 0, cell.owner, yard->island});
                return false;
            }
            yard->order = o.ship_type;
            yard->progress = 0;
            if (!yard->enabled) {
                yard->enabled = true;
                if (yard->rate == 0) {
                    yard->countdown = 0;
                    yard->idle_cycles = 0;
                }
            }
            return true;
        }
        case ShipyardOrder::Op::Cancel:
            if (yard->order == CostBuilding::kNoOrder) {
                return false;
            }
            // Op 0x1A 0xFF: the progress is lost, the material in the yard stays.
            yard->order = CostBuilding::kNoOrder;
            yard->progress = 0;
            yard->enabled = yard->repair_ship != 0;
            return true;
        case ShipyardOrder::Op::Enable:
            if (o.on && !yard->enabled && yard->rate == 0) {
                yard->countdown = 0;
                yard->idle_cycles = 0;
            }
            yard->enabled = o.on;
            return true;
        case ShipyardOrder::Op::Repair:
            break;
    }
    return false;
}

void Fleet::yards_tick(std::uint32_t dt_ms) {
    m_yard_ms += dt_ms;
    while (m_yard_ms >= kSecondMs) {
        m_yard_ms -= kSecondMs;
        for (std::size_t i = 0; i < m_world->cost_buildings().size(); ++i) {
            CostBuilding& yard = m_world->cost_buildings()[i];
            if (yard.kind != CostBuilding::Kind::Shipyard || !yard.enabled) {
                continue;
            }
            if (yard.order == CostBuilding::kNoOrder && yard.repair_ship == 0) {
                continue;
            }
            if (yard.countdown > 1) {
                --yard.countdown;
                continue;
            }
            yard.countdown = 0;
            yard_cycle(yard);
        }
    }
}

void Fleet::yard_cycle(CostBuilding& yard) {
    // 0x004791B0.
    if (yard.island >= m_world->islands().size()) {
        return;
    }
    const WorldIsland& isl = m_world->islands()[yard.island];
    if (!isl.map.contains(yard.x, yard.y)) {
        return;
    }
    const MapCell& cell = isl.map.top(yard.x, yard.y);
    const assets::BuildingType* b = cell.empty() ? nullptr : m_buildings->by_sprite(cell.sprite);
    if (b == nullptr) {
        return;
    }
    const bool burning = std::any_of(m_world->disasters().begin(), m_world->disasters().end(), [&](const Disaster& d) {
        return d.kind == Disaster::Kind::Fire && d.island == yard.island && d.x == yard.x && d.y == yard.y;
    });
    // 1. The running cycle ends. In a burning yard it yields nothing and uses no material, as
    // observed in the original.
    if (yard.rate != 0 && !burning) {
        const std::uint32_t wood = std::min(yard.wood, scaled(b->raw_amount, yard.rate));
        const std::uint32_t cloth = std::min(yard.cloth, scaled(b->raw2_amount, yard.rate));
        yard.wood -= wood;
        yard.cloth -= cloth;
        if (City* city = m_world->city(yard.island, cell.city)) {
            if (m_world->goods().is_trade(b->raw_ware)) {
                city->book(b->raw_ware, GoodsFlow::Workshops, wood);
            }
            if (m_world->goods().is_trade(b->raw2_ware)) {
                city->book(b->raw2_ware, GoodsFlow::Workshops, cloth);
            }
        }
        const std::uint32_t points = scaled(b->produce_amount, yard.rate);
        Ship* ship = yard.repair_ship != 0 ? m_world->ship(yard.repair_ship) : nullptr;
        if (ship != nullptr && ship->repairing && !m_world->original_bugs().remote_repair &&
            !building_in_reach(*m_world, *m_buildings, yard.island, yard.x, yard.y, ship->x, ship->y)) {
            ship->repairing = false;  // only at the yard (S-013; OriginalBugs::remote_repair: anywhere)
        }
        if (ship != nullptr && ship->repairing) {
            const assets::FigureType* t = type_of(*ship);
            if (t != nullptr && t->ship.build_amount != 0) {
                // 1.5 * Maxenergy / Rohmenge in tons per full cycle; the original divides first and
                // repairs the trade ship at a third of the rate (S-081).
                const std::uint64_t hp = std::uint64_t{t->ship.max_health} * points * 192 / 128 / t->ship.build_amount;
                ship->health =
                    static_cast<std::uint32_t>(std::min<std::uint64_t>(t->ship.max_health, ship->health + hp));
                if (ship->health >= t->ship.max_health) {
                    ship->repairing = false;
                    m_events.push_back(ShipEvent{ShipEvent::Kind::ShipRepaired, ship->id, ship->owner, yard.island});
                }
            }
        } else {
            yard.progress += points;
        }
    }
    yard.rate = 0;
    // 2. A finished ship is launched.
    if (yard.repair_ship == 0 && yard.order != CostBuilding::kNoOrder) {
        const assets::FigureType* t = m_figures->get(figure::ship_figure(yard.order));
        const std::uint32_t threshold = t != nullptr ? t->ship.build_amount : 0U;
        if (threshold != 0 && yard.progress >= threshold) {
            if (launch(yard)) {
                yard.order = CostBuilding::kNoOrder;
                yard.progress = 0;
                yard.enabled = false;
            } else if (++yard.failed_launches >= kLaunchesToComplain) {
                // 0x00479508: after 100 tries (about 18 minutes) a computer owner looks for the
                // ships in the way (0x00428920 with 2). The original measures from the shipyard
                // its computer has noted, Open1602 from this one; a computer builds only one.
                yard.failed_launches = 0;
                const WorldIsland& home = m_world->islands()[yard.island];
                complain_of_ships(home.map.top(yard.x, yard.y).owner, static_cast<std::int32_t>(home.x + yard.x),
                                  static_cast<std::int32_t>(home.y + yard.y), kYardComplaint);
            }
            yard.countdown = kIdleSeconds;
            return;
        }
    }
    // 3. A repair that ended frees the yard.
    if (yard.repair_ship != 0) {
        const Ship* ship = m_world->ship(yard.repair_ship);
        if (ship == nullptr || !ship->repairing) {
            yard.repair_ship = 0;
            if (yard.order == CostBuilding::kNoOrder) {
                yard.enabled = false;
                return;
            }
        }
    }
    // 4. The next cycle, as far as the material goes; a burning yard waits.
    std::uint16_t wait = kIdleSeconds;
    if (yard.wood == 0 || burning) {
        yard.idle_cycles = static_cast<std::uint8_t>(std::min<std::uint32_t>(kMaxIdle, yard.idle_cycles + 1U));
    } else {
        std::uint32_t r = 128;
        if (b->raw_amount != 0) {
            r = std::min<std::uint32_t>(r, yard.wood * 128 / b->raw_amount);
        }
        if (b->raw2_amount != 0) {
            r = std::min<std::uint32_t>(r, yard.cloth * 128 / b->raw2_amount);
        }
        if (r < 32) {
            yard.idle_cycles = static_cast<std::uint8_t>(std::min<std::uint32_t>(kMaxIdle, yard.idle_cycles + 1U));
        } else {
            yard.rate = static_cast<std::uint8_t>(r);
            wait = static_cast<std::uint16_t>(b->interval * r / 128);
            yard.idle_cycles = 0;
        }
    }
    // 5. Material for three cycles or less: the pack donkey fetches more.
    const std::uint32_t w1 = b->raw_amount != 0 ? yard.wood * 128 / b->raw_amount : kFetchThreshold;
    const std::uint32_t w2 = b->raw2_amount != 0 ? yard.cloth * 128 / b->raw2_amount : kFetchThreshold;
    if (!yard.fetching) {
        std::uint8_t ware = 0;
        if (w1 <= kFetchThreshold && w1 <= w2) {
            ware = b->raw_ware;
        } else if (w2 <= kFetchThreshold) {
            ware = b->raw2_ware;
        }
        if (ware != 0) {
            m_fetch.push_back(FetchRequest{yard.island, yard.x, yard.y, ware});
            yard.fetching = true;  // one pack donkey at a time; yard_delivery clears it
        }
    }
    yard.countdown = wait;
}

bool Fleet::launch(CostBuilding& yard) {
    // 0x004796A0: the nearest free sea tile within the yard's Radius.
    const WorldIsland& isl = m_world->islands()[yard.island];
    const MapCell& cell = isl.map.top(yard.x, yard.y);
    const assets::BuildingType* b = m_buildings->by_sprite(cell.sprite);
    const assets::FigureType* type = m_figures->get(figure::ship_figure(yard.order));
    if (b == nullptr || type == nullptr) {
        return false;
    }
    std::uint32_t w = 1;
    std::uint32_t h = 1;
    rotated_size(*b, cell.rotation, w, h);
    const auto cx = static_cast<std::int32_t>(isl.x + yard.x + (w - 1) / 2);
    const auto cy = static_cast<std::int32_t>(isl.y + yard.y + (h - 1) / 2);
    const std::int32_t radius = std::max<std::int32_t>(1, b->radius);
    SeaPlan r;
    r.start_x = cx;
    r.start_y = cy;
    r.radius = radius;
    r.aim_x = cx;
    r.aim_y = cy;
    for (const Ship& o : m_world->ships()) {
        if (!o.cart() && std::abs(o.x - cx) <= radius + 1 && std::abs(o.y - cy) <= radius + 1) {
            r.ships.push_back(SeaPlan::Obstacle{o.x, o.y, false});
        }
    }
    // Search from the yard over land and water alike: the first free water tile wins.
    std::optional<std::pair<std::int32_t, std::int32_t>> spot;
    std::int32_t best = 0x7FFFFFFF;
    for (std::int32_t dy = -radius; dy <= radius; ++dy) {
        for (std::int32_t dx = -radius; dx <= radius; ++dx) {
            const std::int32_t x = cx + dx;
            const std::int32_t y = cy + dy;
            if (!m_sea.sailable(x, y)) {
                continue;
            }
            bool taken = false;
            for (const SeaPlan::Obstacle& o : r.ships) {
                taken = taken || (std::abs(o.x - x) <= 1 && std::abs(o.y - y) <= 1);
            }
            const std::int32_t d = sea_distance(dx, dy);
            if (!taken && d < best) {
                best = d;
                spot = std::pair{x, y};
            }
        }
    }
    if (!spot) {
        return false;
    }
    Ship s;
    s.name = next_ship_name();
    s.place(static_cast<std::uint32_t>(spot->first), static_cast<std::uint32_t>(spot->second));
    s.figure = figure::ship_figure(yard.order);
    s.owner = cell.owner;
    s.direction = static_cast<std::uint8_t>((cell.rotation * 2U) & 7U);
    s.health = type->ship.max_health;
    s.cargo.resize(std::max<std::size_t>(1, type->ship.cargo_slots));
    m_world->add_ship(std::move(s));
    const Ship& added = m_world->ships().back();
    m_events.push_back(ShipEvent{ShipEvent::Kind::ShipLaunched, added.id, added.owner, yard.island});
    update_island(m_world->ships().back());
    return true;
}

}  // namespace o1602::sim
