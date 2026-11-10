// SPDX-License-Identifier: GPL-3.0-or-later
//
// Orders of the ship panel beyond sailing and trade (docs/re/ships.md#ship-window): mounting
// and taking off cannons, goods overboard, the name, and selling ships between players.
#include <algorithm>
#include <span>

#include "o1602/sim/ships.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t kTon = kStockUnitsPerTon;

}  // namespace

std::int32_t Fleet::sale_price(const Ship& ship) const {
    const assets::FigureType* t = type_of(ship);
    if (t == nullptr) {
        return 0;
    }
    return static_cast<std::int32_t>(std::uint64_t{t->ship.price} * ship.sale_factor / 128U);
}

void Fleet::change_owner(Ship& ship, PlayerId owner) {
    // The old owner's orders, route and sale go with it; the new owner starts with a ship at rest.
    ship.owner = owner;
    ship.for_sale = false;
    ship.sale_factor = 128;
    ship.held = false;
    ship.queue.clear();
    ship.repairing = false;
    ship.route = TradeRoute{};
    ship.attack_move = false;
    ship.hold = false;
    stop_exploring(ship);
    ship.target = Target{};
    ship.order = Target{};
    ship.motion.path.clear();
    ship.motion.path_index = 0;
}

bool Fleet::command(const ShipCommand& c) {
    Ship* ship = m_world->ship(c.ship);
    if (ship == nullptr || ship->sinking || ship->trader) {
        return false;
    }
    if (ship->cart() && c.op != ShipCommand::Op::Dump && c.op != ShipCommand::Op::Rename) {
        return false;  // a cart has no cannons, flag or price (TRADER.GAD)
    }
    const bool own = c.player == kNoPlayer || ship->owner == c.player;
    const assets::FigureType* type = type_of(*ship);
    const std::uint8_t cannons = m_buildings->materials().cannons;
    switch (c.op) {
        case ShipCommand::Op::MountCannon: {
            // A hold with at least 1 t of cannons, while the ship has room for another (Maxkanon);
            // `count` times, or until the cannons on board or the places run out.
            if (!own || type == nullptr) {
                return false;
            }
            std::uint8_t mounted = 0;
            for (std::size_t h = 0; h < holds(*ship) && mounted < c.count; ++h) {
                CargoSlot& slot = ship->cargo[h];
                if ((c.hold != 0xFF && h != c.hold) || slot.ware != cannons) {
                    continue;
                }
                while (mounted < c.count && slot.amount >= kTon && ship->cannons < type->ship.max_cannons) {
                    slot.amount -= kTon;
                    ++ship->cannons;
                    ++mounted;
                }
                if (slot.amount == 0) {
                    slot = CargoSlot{};
                }
            }
            return mounted > 0;
        }
        case ShipCommand::Op::UnmountCannon: {
            // Needs room for 1 t of cannons: a hold with cannons and space, or an empty hold.
            if (!own || ship->cannons == 0) {
                return false;
            }
            CargoSlot* target = nullptr;
            const std::span<CargoSlot> usable(ship->cargo.data(), holds(*ship));
            for (CargoSlot& slot : usable) {
                if (slot.ware == cannons && slot.amount + kTon <= kHoldCapacity) {
                    target = &slot;
                    break;
                }
            }
            if (target == nullptr) {
                for (CargoSlot& slot : usable) {
                    if (slot.ware == 0) {
                        target = &slot;
                        break;
                    }
                }
            }
            if (target == nullptr) {
                return false;
            }
            target->ware = cannons;
            target->amount += kTon;
            --ship->cannons;
            return true;
        }
        case ShipCommand::Op::Dump: {
            // Every hold (or the one named) but soldiers on board; the goods are gone.
            if (!own) {
                return false;
            }
            bool dumped = false;
            for (std::size_t h = 0; h < ship->cargo.size(); ++h) {
                CargoSlot& slot = ship->cargo[h];
                if ((c.hold != 0xFF && h != c.hold) || slot.ware == 0 || is_soldier_ware(slot.ware)) {
                    continue;
                }
                slot = CargoSlot{};
                dumped = true;
            }
            return dumped;
        }
        case ShipCommand::Op::Rename:
            if (!own || ship->sinking || !valid_name(c.name, ShipCommand::kMaxNameLength)) {
                return false;
            }
            ship->name = c.name;
            return true;
        case ShipCommand::Op::OfferForSale:
            if (!own || c.factor > ShipCommand::kMaxSaleFactor) {
                return false;
            }
            ship->for_sale = true;
            ship->sale_factor = c.factor;
            m_events.push_back(
                ShipEvent{ShipEvent::Kind::OfferedForSale, ship->id, ship->owner, 0, 0, 0, 0, sale_price(*ship)});
            return true;
        case ShipCommand::Op::WithdrawSale:
            if (!own || !ship->for_sale) {
                return false;
            }
            ship->for_sale = false;
            return true;
        case ShipCommand::Op::Buy: {
            // 0x0044DF80: the seller earns the price and the ship changes hands. The original takes
            // the money without looking whether the buyer has it; Open1602 checks it, the fleet
            // limit of the buyer, and that no soldiers are on board (they would change sides).
            Player* buyer = m_world->player(c.player);
            Player* seller = m_world->player(ship->owner);
            if (!ship->for_sale || buyer == nullptr || seller == nullptr || c.player == ship->owner ||
                (buyer->role != PlayerRole::Human && buyer->role != PlayerRole::Computer)) {
                return false;
            }
            const std::int32_t price = sale_price(*ship);
            if (buyer->money < price) {
                return false;
            }
            if (m_rules->max_ships_per_player != 0 && fleet_size(c.player) >= m_rules->max_ships_per_player) {
                m_events.push_back(ShipEvent{ShipEvent::Kind::FleetFull, ship->id, c.player});
                return false;
            }
            if (std::any_of(ship->cargo.begin(), ship->cargo.end(),
                            [](const CargoSlot& slot) { return is_soldier_ware(slot.ware); })) {
                return false;
            }
            const PlayerId old_owner = ship->owner;
            buyer->money = money_plus(buyer->money, -std::int64_t{price});
            seller->money = money_plus(seller->money, std::int64_t{price});
            change_owner(*ship, c.player);
            m_events.push_back(ShipEvent{ShipEvent::Kind::Sold, ship->id, old_owner, 0, c.player, 0, 0, price});
            return true;
        }
        case ShipCommand::Op::WhiteFlag:
            if (!own) {
                return false;
            }
            if (c.on) {
                // Raising it stops the ship like the stop button (0x0044EBA0), then holds it.
                if (!ship->held) {
                    order(ShipOrder{c.player, ship->id, Target{}});
                    ship->held = true;
                    m_events.push_back(ShipEvent{ShipEvent::Kind::WhiteFlag, ship->id, ship->owner});
                }
            } else {
                ship->held = false;
            }
            return true;
        case ShipCommand::Op::Hold:
            if (!own) {
                return false;
            }
            order(ShipOrder{c.player, ship->id, Target{}});
            ship->hold = true;
            return true;
        case ShipCommand::Op::ToPirates: {
            const Player* owner = m_world->player(ship->owner);
            const PlayerId pirates = m_world->first_of(FactionBehaviour::Pirates);
            if (!own || owner == nullptr || owner->role != PlayerRole::Computer || pirates == kNoPlayer ||
                ship->sinking) {
                return false;
            }
            change_owner(*ship, pirates);
            return true;
        }
        case ShipCommand::Op::Scuttle:
            if (!own) {
                return false;
            }
            sink(*ship, kNoPlayer);
            return true;
        case ShipCommand::Op::Patrol: {
            const Target point = Target::position(c.x, c.y);
            if (!own || !target_valid(point)) {
                return false;
            }
            order(ShipOrder{c.player, ship->id, Target{}});
            TradeRoute& route = ship->route;
            route.stops = {};
            route.stops[0].target = Target::position(ship->x, ship->y);
            route.stops[1].target = point;
            route.current = 0;
            route.one_way = false;
            RouteEdit start;
            start.player = c.player;
            start.ship = ship->id;
            start.op = RouteEdit::Op::Start;
            return edit_route(start);
        }
    }
    return false;
}

}  // namespace o1602::sim
