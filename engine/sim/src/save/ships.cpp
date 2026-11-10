// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ships: FLEE (ships with their courses and routes, shipyards, the Kontors' trade settings,
// discovered islands) and SHIP, the first version's list that the chunk in a .GAM still carries for
// readers older than FLEE.
#include <algorithm>
#include <iterator>
#include <utility>

#include "section_io.hpp"

namespace o1602::sim::save {

namespace {

template <class A, class S>
void io_ship(A& a, S& s, std::uint16_t version) {
    a.u32(s.id);
    a.string(s.name);
    a.i32(s.pos_x);
    a.i32(s.pos_y);
    if constexpr (A::kReading) {
        // validate() checks the tile against the world; a position beyond 16 bits of tiles is never on it.
        if (s.pos_x < 0 || s.pos_y < 0 || s.pos_x / kShipUnit > 0xFFFF || s.pos_y / kShipUnit > 0xFFFF) {
            a.fail();
        }
        s.x = static_cast<std::uint16_t>(std::max(0, s.pos_x / kShipUnit));
        s.y = static_cast<std::uint16_t>(std::max(0, s.pos_y / kShipUnit));
    }
    a.u16(s.figure);
    a.u8(s.owner);
    a.u8_masked(s.direction, 7U);
    a.u32(s.health);
    a.u8(s.cannons);
    a.list(s.cargo, 7, [&](auto& c) {
        a.u8(c.ware);
        a.u32(c.amount);
        a.u16(c.price);
    });
    const std::size_t stops = version >= kLongRoutes ? TradeRoute::kStops : TradeRoute::kOriginalStops;
    const std::size_t entries = version >= kRouteWares ? RouteStop::kEntries : RouteStop::kOriginalEntries;
    for (std::size_t n = 0; n < stops; ++n) {
        auto& stop = s.route.stops[n];
        io_target(a, stop.target);
        for (std::size_t k = 0; k < entries; ++k) {
            auto& e = stop.entries[k];
            a.u8(e.ware);
            a.boolean(e.unload);
            a.u32(e.amount);
        }
    }
    a.u8(s.route.current);
    if constexpr (A::kReading) {
        s.route.current = static_cast<std::uint8_t>(s.route.current % TradeRoute::kStops);
    }
    a.boolean(s.route.active);
    a.boolean(s.route.one_way);
    a.boolean(s.route.problem);
    a.u8(s.route.problem_rounds);
    io_target(a, s.target);
    io_target(a, s.order);
    io_target(a, s.previous);
    a.boolean(s.held);
    a.boolean(s.repairing);
    a.boolean(s.for_sale);
    a.u16(s.sale_factor);
    a.boolean(s.trader);
    a.list(s.trader_goods, 7, [&](auto& g) {
        a.u16(g.price);
        a.u8(g.age);
        a.u32(g.amount);
    });
    a.u32(s.trader_tools_ms);
    a.u32(s.trader_ore_ms);
    a.u16(s.near_island);
    a.u16(s.source_slot);
    auto& m = s.motion;
    a.enum8(m.phase, ShipMotion::Phase::Sinking);
    a.u8(m.count);
    a.u32(m.elapsed_us);
    a.u32(m.duration_us);
    a.i32(m.from_x);
    a.i32(m.from_y);
    a.u8s(m.path);
    a.u8(m.path_index);
    a.u8(m.radius);
    a.u8(m.failures);
    a.u8(m.turn_to);
    a.u64(m.next_follow_us);
}

/// What a Kontor offers to other players' ships, of a City (writing) or of
/// Decoded::FleetPart::CityTrade (reading), which has the same names.
template <class A, class C>
void io_city_trade(A& a, C& c) {
    a.u16(c.island);
    a.u8(c.index);
    a.list(c.trade, 14, [&](auto& t) {
        a.boolean(t.sell);
        a.boolean(t.buy);
        a.u16(t.sell_price);
        a.u16(t.buy_price);
        a.u32(t.keep);
        a.u32(t.limit);
    });
    a.u32s(c.goodwill);
    a.boolean(c.trader_inbound);
}

/// The shipyard fields of a cost building.
template <class A, class B>
void io_yard(A& a, B& b) {
    a.u8(b.order);
    a.u32(b.progress);
    a.u32(b.wood);
    a.u32(b.cloth);
    a.u8(b.rate);
    a.u16(b.countdown);
    a.u32(b.repair_ship);
    a.u8(b.failed_launches);  // a reserved byte (always 0) before it counted again
    a.u8(b.idle_cycles);
    a.boolean(b.fetching);
}

/// FLEE: the next ship id, the players who discovered each island (island number, flags), the
/// Kontors' trade settings, the shipyard fields of the cost buildings, the ships.
template <class A, class Id, class Discovered, class Cities, class Yards, class Ships>
void io_fleet(A& a, std::uint16_t version, Id& next_ship_id, Discovered& discovered, Cities& cities, Yards& yards,
              Ships& ships) {
    a.u32(next_ship_id);
    a.list(discovered, version >= kWideDiscovered ? 6 : 5, [&](auto& item) {
        if (version >= kWideDiscovered) {
            a.u16(item.first);
        } else {
            a.u8(item.first);
        }
        a.bools(item.second);
    });
    a.list(cities, 12, [&](auto& c) { io_city_trade(a, c); });
    a.list(yards, 23, [&](auto& b) { io_yard(a, b); });
    a.list(ships, 40, [&](auto& s) { io_ship(a, s, version); });
}

}  // namespace

// SHIP keeps the cargo as a list of the filled holds (the format of the first version) and no
// position finer than the tile: the reader puts the ship on the centre of its tile.
void write_ships(Writer& out, const World& w) {
    out.list(w.ships(), 14, [&](const Ship& s) {
        out.string(s.name);
        out.u16(s.x);
        out.u16(s.y);
        out.u16(s.figure);
        out.u8(s.owner);
        out.u8(s.direction);
        std::vector<CargoSlot> filled;
        std::copy_if(s.cargo.begin(), s.cargo.end(), std::back_inserter(filled),
                     [](const CargoSlot& c) { return c.ware != 0; });
        out.list(filled, 5, [&](const CargoSlot& c) {
            out.u8(c.ware);
            out.u32(c.amount);
        });
    });
}

std::vector<Ship> read_ships(Reader& in) {
    std::vector<Ship> list;
    in.list(list, 14, [&](Ship& s) {
        in.string(s.name);
        in.u16(s.x);
        in.u16(s.y);
        in.u16(s.figure);
        in.u8(s.owner);
        in.u8(s.direction);
        in.list(s.cargo, 5, [&](CargoSlot& c) {
            in.u8(c.ware);
            in.u32(c.amount);
        });
        s.place(s.x, s.y);
    });
    return list;
}

std::uint16_t fleet_version(const World& w) {
    // Version 4 has the routes of version 3 and wide island numbers.
    if (std::ranges::any_of(w.islands(), [](const WorldIsland& i) { return i.header.number > 0xFF; })) {
        return kWideDiscovered;
    }
    if (std::ranges::any_of(w.ships(), [](const Ship& s) { return s.route.beyond_original_entries(); })) {
        return kRouteWares;
    }
    return std::ranges::any_of(w.ships(), [](const Ship& s) { return s.route.beyond_original_stops(); })
               ? kLongRoutes
               : std::uint16_t{1};
}

void write_fleet(Writer& out, const World& w, std::uint16_t version) {
    const std::uint32_t next_ship_id = w.next_ship_id();
    std::vector<std::pair<std::uint16_t, std::vector<bool>>> discovered;
    for (const WorldIsland& i : w.islands()) {
        discovered.emplace_back(i.header.number, i.discovered);
    }
    io_fleet(out, version, next_ship_id, std::as_const(discovered), w.cities(), w.cost_buildings(), w.ships());
}

Decoded::FleetPart read_fleet(Reader& in, std::uint16_t version) {
    Decoded::FleetPart f;
    io_fleet(in, version, f.next_ship_id, f.discovered, f.cities, f.yards, f.ships);
    return f;
}

}  // namespace o1602::sim::save
