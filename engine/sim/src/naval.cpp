// SPDX-License-Identifier: GPL-3.0-or-later
// Ships in a fight (docs/re/military.md, "Naval combat"): target choice, the broadside and firing.
#include <algorithm>

#include "o1602/sim/combat.hpp"
#include "o1602/sim/ships.hpp"

namespace o1602::sim {

namespace {

constexpr std::int32_t kOrderRange = 15;  // tiles: an ordered target this near is fought (0x00445320)
constexpr std::uint32_t kStandMs = 1000;  // a ship fighting stands a second between looks
constexpr std::uint32_t kDefaultTurnMs = 200;

bool computer_controlled(const Player* p) noexcept {
    return p != nullptr && p->role != PlayerRole::Human;
}

}  // namespace

std::optional<std::uint8_t> Fleet::bearing_to(const Ship& ship, const Target& target) const {
    // Ship_FireDirection 0x00453E50: half tiles from the ship's centre to the nearest point of the
    // target, within Shotradius; no line of sight at sea.
    const auto me = m_combat->fighter(Target::other_ship(ship.id));
    const auto other = m_combat->fighter(target);
    if (!me || !other) {
        return std::nullopt;
    }
    const std::int32_t cx = 2 * ship.x + 1;
    const std::int32_t cy = 2 * ship.y + 1;
    if (other->cells.distance(cx, cy) > me->range8 / 4) {
        return std::nullopt;
    }
    const auto [nx, ny] = other->cells.nearest(cx, cy);
    return direction_to(nx - cx, ny - cy);
}

bool Fleet::naval_combat(Ship& ship) {
    // The combat block of Ship_OnStepDone 0x00452370: only armed ships fight.
    if (m_combat == nullptr || ship.cannons == 0 || ship.held || ship.sinking) {
        return false;
    }
    const Target self = Target::other_ship(ship.id);
    const auto me = m_combat->fighter(self);
    if (!me) {
        return false;
    }
    const Player* owner = m_world->player(ship.owner);
    const bool pirate = owner != nullptr && m_world->behaviour(*owner) == FactionBehaviour::Pirates;
    const Diplomacy& dipl = m_world->diplomacy();
    const auto hostile = [&](const Target& t) {
        const auto f = m_combat->fighter(t);
        return f && f->owner != kNoPlayer && f->owner != ship.owner && dipl.hostile(ship.owner, f->owner);
    };
    const auto is_combat_ref = [](const Target& t) {
        return t.kind == Target::Kind::Ship || t.kind == Target::Kind::Soldier || t.kind == Target::Kind::Building;
    };
    // Engage mode: ships on a route or patrol, pirates, and computer players' ships out at sea or
    // sent against something; the player's ships without a route only fight what is in range.
    const bool engage =
        !ship.hold &&
        (ship.route.active || pirate || ship.attack_move ||
         (computer_controlled(owner) && !ship.repairing && (is_combat_ref(ship.target) || ship.near_island == 0xFFFF)));
    // The ordered target (pirates: their order; others: the current target) if hostile, or a tree
    // or field that is not the ship's, and near.
    Target candidate;
    const Target& ordered = pirate ? ship.order : ship.target;
    if (is_combat_ref(ordered) && (hostile(ordered) || m_combat->fellable(ordered, ship.owner))) {
        if (const auto f = m_combat->fighter(ordered);
            f && f->cells.distance(2 * ship.x + 1, 2 * ship.y + 1) <= 2 * kOrderRange) {
            candidate = ordered;
        }
    }
    Target fight;
    bool chase = false;
    const bool unarmed_candidate = candidate.empty() || m_combat->fighter(candidate)->damage == 0;
    if (ship.target.empty() || (engage && unarmed_candidate)) {
        for (const Candidate& c : m_combat->rank(*me, m_combat->hostiles(self), 0x7FFFFFFF)) {
            if (ship.route.active && c.unit.kind == Target::Kind::Building) {
                continue;  // ships on a route leave towers alone
            }
            if (c.near && bearing_to(ship, c.unit)) {
                fight = c.unit;
                break;
            }
            // Chasing: pirates only after trade and war ships of players; ships on a route keep it.
            if (engage && !ship.route.active && fight.empty() && !chase) {
                const Ship* prey = c.unit.kind == Target::Kind::Ship ? m_world->ship(c.unit.ship) : nullptr;
                const Player* prey_owner = prey != nullptr ? m_world->player(prey->owner) : nullptr;
                const bool worth = !pirate || (prey != nullptr && !prey->trader && prey_owner != nullptr &&
                                               m_world->behaviour(*prey_owner) != FactionBehaviour::Pirates);
                if (worth && c.unit.kind != Target::Kind::Soldier) {
                    fight = c.unit;
                    chase = true;
                }
            }
        }
    }
    if (fight.empty()) {
        fight = candidate;
    }
    if (fight.empty()) {
        return false;
    }
    // A pirate meeting a ship with the white flag plunders it instead of shooting (0x00455840).
    if (pirate && fight.kind == Target::Kind::Ship) {
        if (Ship* victim = m_world->ship(fight.ship); victim != nullptr && victim->held) {
            plunder(ship, *victim);
            return false;
        }
    }
    const auto bearing = bearing_to(ship, fight);
    if (!bearing) {
        if (chase && !(ship.target == fight)) {
            // Sail towards it (approach radius 0x0C); the next decision fires once in range. An
            // attack move takes up its way again afterwards.
            if (ship.attack_move && !ship.target.empty() && !is_combat_ref(ship.target) &&
                ship.queue.size() < kMaxQueuedOrders) {
                ship.queue.insert(ship.queue.begin(), ship.target);
            }
            ship.target = fight;
            ship.motion.path.clear();
            ship.motion.path_index = 0;
            ship.motion.failures = 0;
        }
        return false;  // out of range: the movement goes on towards the target
    }
    // Broadside: the heading 90 degrees to the bearing that needs fewer turns (tie: bearing - 2).
    const auto right = static_cast<std::uint8_t>((*bearing + 2U) & 7U);
    const auto left = static_cast<std::uint8_t>((*bearing + 6U) & 7U);
    const auto steps = [&](std::uint8_t heading) {
        const std::uint32_t a = (heading - ship.direction) & 7U;
        return std::min<std::uint32_t>(a, 8U - a);
    };
    const std::uint8_t heading = steps(right) < steps(left) ? right : left;
    ShipMotion& m = ship.motion;
    const std::uint64_t now = m_world->time_ms();
    const bool moving_past = !(fight == ship.target) && m.path_index < m.path.size();
    if (ship.direction != heading) {
        if (moving_past) {
            return false;  // not broadside while passing: sail on (the original fires only when aligned)
        }
        // Stop and turn one step per Drehtime (Ship_PostTurn 0x00454760).
        m.path.clear();
        m.path_index = 0;
        const assets::FigureType* t = type_of(ship);
        const std::uint32_t turn = t != nullptr && t->ship.turn_ms != 0 ? t->ship.turn_ms : kDefaultTurnMs;
        const std::uint32_t r = (heading - ship.direction) & 7U;
        ship.direction = static_cast<std::uint8_t>((ship.direction + (r <= 4U ? 1U : 7U)) & 7U);
        m.phase = ShipMotion::Phase::Turn;
        m.turn_to = heading;
        m.elapsed_us = 0;
        m.duration_us = turn * 1000U;
        return true;
    }
    if (now >= ship.next_shot_ms) {
        const auto target = m_combat->fighter(fight);
        const assets::FigureType* t = type_of(ship);
        if (target && t != nullptr) {
            m_combat->shoot(*me, *target, t->combat.hit_delay_ms);
            // Reload: Worktime * k / cannons (0x00447880).
            const std::uint32_t k = Combat::cannon_factor(ship.cannons);
            ship.next_shot_ms = now + std::uint64_t{t->work_time_ms} * k / ship.cannons;
            m_events.push_back(ShipEvent{ShipEvent::Kind::Fired, ship.id, ship.owner, 0, 0, *bearing});
        }
    }
    if (moving_past) {
        return false;  // fired while passing, without stopping
    }
    m.path.clear();
    m.path_index = 0;
    start_wait(ship, kStandMs);
    return true;
}

}  // namespace o1602::sim
