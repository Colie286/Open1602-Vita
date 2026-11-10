// SPDX-License-Identifier: GPL-3.0-or-later
//
// The military: ARMY (soldiers with their paths, towers, building damage, shots in flight, the
// combat state of ships and cities, castle slots) and SOLD, the first version's list of units that
// only files older than ARMY have.
#include <utility>

#include "section_io.hpp"

namespace o1602::sim::save {

namespace {

template <class A, class S>
void io_soldier(A& a, S& s) {
    a.u32(s.id);
    a.i32(s.pos_x);
    a.i32(s.pos_y);
    a.u16(s.figure);
    a.u8(s.owner);
    a.u8_masked(s.direction, 7U);
    a.u8(s.anim);
    a.u16(s.island);
    a.u32(s.health);
    io_target(a, s.home);
    io_target(a, s.target);
    a.u8(s.radius);
    a.u8(s.failures);
    a.u64(s.next_attack_ms);
    a.u64(s.next_follow_ms);
    a.u64(s.last_attack_ms);
    a.boolean(s.patrol.active);
    a.boolean(s.patrol.one_way);
    a.u8_masked(s.patrol.index, 1U);
    io_target(a, s.patrol.points[0]);
    io_target(a, s.patrol.points[1]);
    a.boolean(s.disbanded);
    a.u16(s.source_slot);
    auto& m = s.motion;
    a.enum8(m.phase, SoldierMotion::Phase::Healing);
    a.u8_masked(m.direction, 7U);
    a.u8(m.count);
    a.u32(m.elapsed_us);
    a.u32(m.duration_us);
    a.i32(m.from_x);
    a.i32(m.from_y);
    a.u8s(m.path);
    a.u8(m.path_index);
}

template <class A, class T>
void io_tower(A& a, T& t) {
    a.u16(t.island);
    a.u16(t.x);
    a.u16(t.y);
    a.u8(t.owner);
    a.u16(t.figure);
    a.u8_masked(t.direction, 7U);
    a.u64(t.next_shot_ms);
    a.u32(t.next_think_us);
    a.u16(t.source_slot);
}

template <class A, class D>
void io_damage(A& a, D& d) {
    a.u16(d.island);
    a.u16(d.x);
    a.u16(d.y);
    a.u32(d.damage);
}

template <class A, class H>
void io_hit(A& a, H& h) {
    a.u64(h.due_ms);
    io_target(a, h.shooter);
    a.u8(h.attacker);
    a.boolean(h.cannon);
    a.u32(h.damage);
    io_target(a, h.target);
    a.i32(h.from_x);
    a.i32(h.from_y);
}

/// The combat state of a Ship (writing) or of Decoded::ArmyPart::ShipPart (reading).
template <class A, class S>
void io_ship_combat(A& a, S& s) {
    a.u32(s.id);
    a.boolean(s.sinking);
    a.u64(s.next_shot_ms);
}

/// The combat state of a City (writing) or of Decoded::ArmyPart::CityPart (reading).
template <class A, class C>
void io_city_combat(A& a, C& c) {
    a.u16(c.island);
    a.u8(c.index);
    a.u64(c.last_attacked_ms);
    a.boolean(c.cursed);
}

/// The castle fields of a cost building: weapons in store and the training slots.
template <class A, class B>
void io_castle(A& a, B& b) {
    for (auto& weapon : b.weapons) {
        a.u32(weapon);
    }
    a.list(b.slots, 10, [&](auto& slot) {
        a.u16(slot.figure);
        a.boolean(slot.training);
        a.boolean(slot.armed);
        a.u32(slot.health);
        a.u16(slot.progress);
    });
}

/// ARMY in its order. Ships and cities give their combat state, the cost buildings (in the order
/// of COST) their castle fields.
template <class A, class Id, class Soldiers, class Towers, class Damage, class Hits, class Ships, class Cities,
          class Castles>
void io_army(A& a, Id& next_soldier_id, Soldiers& soldiers, Towers& towers, Damage& damage, Hits& hits, Ships& ships,
             Cities& cities, Castles& castles) {
    a.u32(next_soldier_id);
    a.list(soldiers, 60, [&](auto& s) { io_soldier(a, s); });
    a.list(towers, 24, [&](auto& t) { io_tower(a, t); });
    a.list(damage, 10, [&](auto& d) { io_damage(a, d); });
    a.list(hits, 40, [&](auto& h) { io_hit(a, h); });
    a.list(ships, 13, [&](auto& s) { io_ship_combat(a, s); });
    a.list(cities, 12, [&](auto& c) { io_city_combat(a, c); });
    a.list(castles, 16, [&](auto& b) { io_castle(a, b); });
}

}  // namespace

// SOLD has positions as floating point; Open1602 no longer writes it.
std::vector<Soldier> read_soldiers(Reader& in) {
    std::vector<Soldier> list;
    in.list(list, 21, [&](Soldier& s) {
        s.pos_x = in.in().real_fixed(kPosUnit);
        s.pos_y = in.in().real_fixed(kPosUnit);
        in.u16(s.figure);
        in.u8(s.owner);
        in.u8(s.direction);
        in.u8(s.anim);
    });
    return list;
}

void write_army(Writer& out, const World& w) {
    const std::uint32_t next_soldier_id = w.next_soldier_id();
    io_army(out, next_soldier_id, w.soldiers(), w.towers(), w.building_damage(), w.pending_hits(), w.ships(),
            w.cities(), w.cost_buildings());
}

Decoded::ArmyPart read_army(Reader& in) {
    Decoded::ArmyPart a;
    io_army(in, a.next_soldier_id, a.soldiers, a.towers, a.damage, a.hits, a.ships, a.cities, a.castles);
    return a;
}

}  // namespace o1602::sim::save
