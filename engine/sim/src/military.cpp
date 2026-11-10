// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/military.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/state_io.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t kRegenEpochMs = 10'000;  // 0x0047A8C0: building damage heals every 10 s
constexpr std::uint32_t kRegenAmount = 32;       // by 1 HP
constexpr std::uint32_t kTowerThinkUs = 1'000'000;
constexpr std::uint32_t kCastleSecondMs = 1000;  // castles count their training in seconds
constexpr std::int32_t kTowerSight8 = 0x60;      // towers score entries within 12 tiles

/// HAUS kinds that take only 51/128 of the damage of a shot that is not a cannon's (0x0047A650).
bool sturdy(std::int32_t kind) noexcept {
    switch (kind) {
        case sim::kind::kGate:
        case sim::kind::kWall:
        case sim::kind::kWallBeach:
        case sim::kind::kTower:
        case sim::kind::kTowerBeach:
        case sim::kind::kBuilding:
        case sim::kind::kBeachHouse:
        case sim::kind::kPier:
        case sim::kind::kMine:
        case sim::kind::kHq:
        case sim::kind::kHarbour:
        case sim::kind::kMill:
            return true;
        default:
            return false;
    }
}

bool acts(const World& world, PlayerId p) noexcept {
    const Player* player = world.player(p);
    return player != nullptr && (player->role == PlayerRole::Human || player->role == PlayerRole::Computer);
}

}  // namespace

Military::Military(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                   const Rules& rules, Combat& combat)
    : m_world(&world), m_buildings(&buildings), m_figures(&figures), m_rules(&rules), m_combat(&combat) {}

void Military::tick(std::uint32_t dt_ms, std::vector<ConstructionEvent>* construction) {
    const std::uint32_t budget = dt_ms * 1000U;
    m_plans_left = kPlansPerTick;
    m_tick_ms = dt_ms;
    // By id, so that a unit added on the way could not break the loop.
    m_ids.clear();
    for (const Soldier& s : m_world->soldiers()) {
        m_ids.push_back(s.id);
    }
    grid_build();
    for (const std::uint32_t id : m_ids) {
        if (Soldier* s = m_world->soldier(id)) {
            advance(*s, budget);
            grid_moved(*s);
        }
    }
    m_grid_data = nullptr;
    // The dead are gone at the end of their animation.
    m_world->erase_soldiers([](const Soldier& s) { return s.owner == kNoPlayer; });
    m_castle_ms += dt_ms;
    while (m_castle_ms >= kCastleSecondMs) {
        m_castle_ms -= kCastleSecondMs;
        castles_tick();
    }
    towers_tick(dt_ms);
    resolve_hits(construction);
    m_regen_ms += dt_ms;
    if (m_regen_ms >= kRegenEpochMs) {
        m_regen_ms -= kRegenEpochMs;
        regenerate();
    }
}

// ---- Orders ------------------------------------------------------------------------------------

bool Military::order(const SoldierOrder& o) {
    // Each unit once, in the order given, with the place of its id in the order (for its offset):
    // an order from the network may name a unit many times.
    std::vector<Soldier*> units;
    std::vector<std::size_t> given_at;
    std::unordered_set<std::uint32_t> seen;
    for (std::size_t i = 0; i < o.soldiers.size(); ++i) {
        Soldier* s = m_world->soldier(o.soldiers[i]);
        if (s != nullptr && !s->away() && (o.player == kNoPlayer || s->owner == o.player) &&
            seen.insert(s->id).second) {
            units.push_back(s);
            given_at.push_back(i);
        }
    }
    if (units.empty() || (!o.target.empty() && !target_valid(o.target))) {
        return false;
    }
    if (o.attack) {
        declare_war_by_attack(units.front()->owner, o.target);
    }
    // Formation (0x0044C6D0): slots one tile apart around the spot; each unit in order takes the
    // nearest free slot.
    std::vector<Target> targets(units.size(), o.target);
    const auto& formations = m_figures->formations();
    if (o.target.kind == Target::Kind::Spot && o.formation < formations.size() && !formations[o.formation].empty()) {
        const auto& slots = formations[o.formation];
        std::vector<bool> taken(slots.size(), false);
        for (std::size_t u = 0; u < units.size(); ++u) {
            std::size_t best = slots.size();
            std::int32_t best_d = 0;
            for (std::size_t k = 0; k < slots.size(); ++k) {
                if (taken[k]) {
                    continue;
                }
                const std::int32_t sx = static_cast<std::int32_t>(o.target.x) + 2 * slots[k].x;
                const std::int32_t sy = static_cast<std::int32_t>(o.target.y) + 2 * slots[k].y;
                const std::int32_t d = sea_distance(sx - units[u]->cell_x(), sy - units[u]->cell_y());
                if (best == slots.size() || d < best_d) {
                    best = k;
                    best_d = d;
                }
            }
            if (best == slots.size()) {
                break;  // more units than slots: the rest go to the spot
            }
            taken[best] = true;
            targets[u] = Target::spot(
                static_cast<std::uint32_t>(std::max(0, static_cast<std::int32_t>(o.target.x) + 2 * slots[best].x)),
                static_cast<std::uint32_t>(std::max(0, static_cast<std::int32_t>(o.target.y) + 2 * slots[best].y)));
        }
    }
    // The shape of the group: each unit to the spot plus its offset (1/8 tile = 1/4 cell).
    if (o.target.kind == Target::Kind::Spot && o.offsets.size() == o.soldiers.size() && o.formation == 0xFF) {
        for (std::size_t u = 0; u < units.size(); ++u) {
            const auto& off = o.offsets[given_at[u]];
            const auto floor_div = [](std::int32_t a, std::int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
            const std::int32_t x = static_cast<std::int32_t>(o.target.x) + floor_div(off.first, 4);
            const std::int32_t y = static_cast<std::int32_t>(o.target.y) + floor_div(off.second, 4);
            const auto max_x = static_cast<std::int32_t>(2 * m_world->width()) - 1;
            const auto max_y = static_cast<std::int32_t>(2 * m_world->height()) - 1;
            targets[u] = Target::spot(static_cast<std::uint32_t>(std::clamp(x, 0, max_x)),
                                      static_cast<std::uint32_t>(std::clamp(y, 0, max_y)));
        }
    }
    for (std::size_t u = 0; u < units.size(); ++u) {
        Soldier& s = *units[u];
        if (o.queue && (!s.target.empty() || !s.queue.empty()) && !s.patrol.active && !targets[u].empty()) {
            if (s.queue.size() < kMaxQueuedOrders) {
                s.queue.push_back(targets[u]);
            }
            continue;
        }
        s.queue.clear();
        s.attack_move = o.attack_move;
        s.hold = false;
        // Soldier_SetTarget 0x004497C0: the running cell ends, then the soldier plans.
        s.target = targets[u];
        s.motion.path.clear();
        s.motion.path_index = 0;
        s.failures = 0;
        s.patrol.active = false;
        if (s.motion.phase == SoldierMotion::Phase::Wait) {
            s.motion.elapsed_us = s.motion.duration_us;
        }
    }
    return true;
}

bool Military::declare_war_by_attack(PlayerId self, const Target& target) {
    const PlayerId other = owner_of(target);
    if (self == kNoPlayer || other == kNoPlayer || other == self) {
        return false;
    }
    const Player* p = m_world->player(other);
    // SetRelation(self, other, 0) (0x004760E0): also an offer of peace on either side ends.
    if (p == nullptr || m_world->behaviour(*p) == FactionBehaviour::Trader || self >= m_world->diplomacy().size() ||
        other >= m_world->diplomacy().size() || m_world->diplomacy().relation(self, other).peace == Treaty::None) {
        return false;
    }
    m_world->diplomacy().set_peace(*m_world, self, other, Treaty::None, m_world->time_ms());
    m_events.push_back(MilitaryEvent{MilitaryEvent::Kind::WarDeclared, {}, target, self, other});
    return true;
}

bool Military::command(const SoldierCommand& c) {
    Soldier* s = m_world->soldier(c.soldier);
    if (s == nullptr || s->away() || (c.player != kNoPlayer && s->owner != c.player)) {
        return false;
    }
    switch (c.op) {
        case SoldierCommand::Op::Stop:
        case SoldierCommand::Op::Hold:
            s->target = {};
            s->queue.clear();
            s->attack_move = false;
            s->hold = c.op == SoldierCommand::Op::Hold;
            s->patrol.active = false;
            s->motion.path.clear();
            s->motion.path_index = 0;
            return true;
        case SoldierCommand::Op::Disband: {
            // The original charges the third value of the Soldat: line (0x0043D559).
            const assets::FigureType* type = type_of(*s);
            if (Player* p = m_world->player(s->owner); p != nullptr && type != nullptr) {
                p->money = money_plus(p->money, -static_cast<std::int64_t>(m_figures->unit_cost(type->kind)));
            }
            kill(*s, kNoPlayer, true);
            return true;
        }
        case SoldierCommand::Op::SetWaypoint:
            if (c.index > 1 || (!c.point.empty() && !target_valid(c.point))) {
                return false;
            }
            s->patrol.points[c.index] = c.point;
            return true;
        case SoldierCommand::Op::Patrol:
            if (!c.on) {
                s->patrol.active = false;
                return true;
            }
            if (s->patrol.points[0].empty()) {
                return false;
            }
            s->patrol.active = true;
            s->patrol.one_way = c.one_way;
            s->patrol.index = 0;
            s->target = s->patrol.points[0];
            s->motion.path.clear();
            s->motion.path_index = 0;
            return true;
    }
    return false;
}

// ---- Towers ------------------------------------------------------------------------------------

void Military::update_towers(std::uint16_t island) {
    if (island >= m_world->islands().size()) {
        return;
    }
    const IslandMap& map = m_world->islands()[island].map;
    // Towers whose building is gone, or no Wachturm any more, go (0x00481450); the others belong to
    // the owner of the city they stand in.
    std::erase_if(m_world->towers(), [&](const Tower& t) {
        if (t.island != island) {
            return false;
        }
        const auto b = building_at(*m_world, *m_buildings, island, {t.x, t.y});
        return !b || b->type->prod_kind != prod::kTower || b->origin.x != t.x || b->origin.y != t.y;
    });
    // The towers of the island by position (y << 16 | x), sorted: whether a building is known.
    std::vector<std::uint32_t> known_at;
    for (Tower& t : m_world->towers()) {
        if (t.island == island) {
            const MapCell& c = map.top(t.x, t.y);
            const City* city = m_world->city(island, c.city);
            if (city != nullptr) {
                t.owner = city->owner;
            }
            known_at.push_back(std::uint32_t{t.y} << 16 | t.x);
        }
    }
    std::sort(known_at.begin(), known_at.end());
    // New towers for Wachturm buildings in a city's territory (0x00482120).
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const MapCell& c = map.top(x, y);
            if (c.empty() || c.city == MapCell::kNoCity) {
                continue;
            }
            const assets::BuildingType* type = m_buildings->by_sprite(c.sprite);
            if (type == nullptr || type->prod_kind != prod::kTower) {
                continue;
            }
            const auto b = building_at(*m_world, *m_buildings, island,
                                       {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
            if (!b || b->origin.x != static_cast<std::int32_t>(x) || b->origin.y != static_cast<std::int32_t>(y)) {
                continue;
            }
            const bool known = std::binary_search(known_at.begin(), known_at.end(), y << 16 | x);
            const City* city = m_world->city(island, c.city);
            if (known || city == nullptr) {
                continue;
            }
            Tower t;
            t.island = island;
            t.x = static_cast<std::uint16_t>(x);
            t.y = static_cast<std::uint16_t>(y);
            t.owner = city->owner;
            t.figure = type->worker_figure;
            t.next_shot_ms = m_world->time_ms();
            m_world->towers().push_back(t);
        }
    }
}

void Military::towers_tick(std::uint32_t dt_ms) {
    const std::uint32_t budget = dt_ms * 1000U;
    for (std::size_t i = 0; i < m_world->towers().size(); ++i) {
        Tower& t = m_world->towers()[i];
        if (t.next_think_us > budget) {
            t.next_think_us -= budget;
            continue;
        }
        t.next_think_us = kTowerThinkUs - (budget - t.next_think_us);
        tower_think(t);
    }
}

void Military::tower_think(Tower& t) {
    const Target self = Target::building(t.island, t.x, t.y);
    const auto me = m_combat->fighter(self);
    if (!me) {
        return;
    }
    const Player* owner = m_world->player(t.owner);
    const bool pirate_tower = owner != nullptr && m_world->behaviour(*owner) == FactionBehaviour::Pirates;
    const std::uint64_t now = m_world->time_ms();
    for (const Candidate& c : m_combat->rank(*me, m_combat->hostiles(self), kTowerSight8)) {
        if (!c.near) {
            continue;
        }
        // Pirate towers spare unarmed trade ships without soldiers on board (0x00458E60).
        if (pirate_tower && c.unit.kind == Target::Kind::Ship) {
            const Ship* ship = m_world->ship(c.unit.ship);
            if (ship != nullptr && ship->cannons == 0 && !ship->trader) {
                bool troops = false;
                for (const CargoSlot& slot : ship->cargo) {
                    troops = troops || is_soldier_ware(slot.ware);
                }
                if (!troops) {
                    continue;
                }
            }
        }
        const auto target = m_combat->fighter(c.unit);
        if (!target) {
            continue;
        }
        // In range from the tower's centre (half tiles), no line of sight needed (0x00458D80).
        const std::int32_t cx = me->x8 / 4;
        const std::int32_t cy = me->y8 / 4;
        if (target->cells.distance(cx, cy) > me->range8 / 4) {
            continue;
        }
        const auto [nx, ny] = target->cells.nearest(cx, cy);
        t.direction = direction_to(nx - cx, ny - cy);
        if (now >= t.next_shot_ms) {
            const assets::FigureType* type = m_figures->get(t.figure);
            m_combat->shoot(*me, *target, type != nullptr ? type->combat.hit_delay_ms : 600);
            t.next_shot_ms = now + (type != nullptr ? type->work_time_ms : 3000);
            m_events.push_back(MilitaryEvent{MilitaryEvent::Kind::Shot, self, c.unit, t.owner, target->owner,
                                             me->x8 * 512, me->y8 * 512, t.figure});
        }
        return;
    }
}

// ---- Hits --------------------------------------------------------------------------------------

void Military::resolve_hits(std::vector<ConstructionEvent>* construction) {
    const std::uint64_t now = m_world->time_ms();
    auto& hits = m_world->pending_hits();
    // One pass: the due hits move out in their order, the others close up behind them.
    m_due.clear();
    std::size_t kept = 0;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        if (hits[i].due_ms <= now) {
            m_due.push_back(hits[i]);
        } else {
            hits[kept++] = hits[i];
        }
    }
    hits.resize(kept);
    for (const PendingHit& h : m_due) {
        hit(h, construction);
    }
}

void Military::hit(const PendingHit& h, std::vector<ConstructionEvent>* construction) {
    // Projectile_Hit 0x00445930: the target is hit if it still exists, wherever it went.
    const auto target = m_combat->fighter(h.target);
    if (!target) {
        return;
    }
    m_world->diplomacy().add_damage(target->owner, h.attacker, h.damage);
    // Impact effect: a third of the way from the target's nearest point to its centre.
    std::uint16_t effect = 0;
    switch (h.shooter.kind) {
        case Target::Kind::Soldier:
        case Target::Kind::Ship: {
            const Soldier* s = h.shooter.kind == Target::Kind::Soldier ? m_world->soldier(h.shooter.ship) : nullptr;
            const Ship* ship = h.shooter.kind == Target::Kind::Ship ? m_world->ship(h.shooter.ship) : nullptr;
            const assets::FigureType* type = s != nullptr      ? m_figures->get(s->figure)
                                             : ship != nullptr ? m_figures->get(ship->figure)
                                                               : nullptr;
            effect = type != nullptr ? type->combat.hit_figure : 0;
            break;
        }
        case Target::Kind::Building:
            for (const Tower& t : m_world->towers()) {
                if (t.island == h.shooter.island && t.x == h.shooter.x && t.y == h.shooter.y) {
                    const assets::FigureType* type = m_figures->get(t.figure);
                    effect = type != nullptr ? type->combat.hit_figure : 0;
                }
            }
            break;
        default:
            break;
    }
    const auto [nx, ny] = target->cells.nearest(h.from_x / kCellUnit, h.from_y / kCellUnit);
    const std::int32_t px =
        (nx * kCellUnit + kCellUnit / 2) + (target->x8 * 512 - (nx * kCellUnit + kCellUnit / 2)) / 3;
    const std::int32_t py =
        (ny * kCellUnit + kCellUnit / 2) + (target->y8 * 512 - (ny * kCellUnit + kCellUnit / 2)) / 3;
    m_events.push_back(
        MilitaryEvent{MilitaryEvent::Kind::Hit, h.shooter, h.target, h.attacker, target->owner, px, py, effect});

    switch (h.target.kind) {
        case Target::Kind::Soldier: {
            Soldier* s = m_world->soldier(h.target.ship);
            const std::uint32_t health = target->health;
            if (health > h.damage) {
                s->health = health - h.damage;
            } else {
                s->health = 0;  // the original keeps the old health until the kill arrives (M-015)
                kill(*s, h.attacker, false);
            }
            break;
        }
        case Target::Kind::Ship:
            if (m_fleet != nullptr) {
                const Player* attacker = m_world->player(h.attacker);
                m_fleet->hit(h.target.ship, h.damage, h.attacker,
                             attacker != nullptr && m_world->behaviour(*attacker) == FactionBehaviour::Pirates &&
                                 h.shooter.kind == Target::Kind::Ship);
            }
            break;
        case Target::Kind::Building:
            if (h.shooter.kind == Target::Kind::Ship) {
                m_events.push_back(MilitaryEvent{MilitaryEvent::Kind::SettlementShelled, h.shooter, h.target,
                                                 h.attacker, target->owner, px, py});
            }
            damage_building(h.target.island, static_cast<std::uint16_t>(target->unit.x),
                            static_cast<std::uint16_t>(target->unit.y), h.damage, h.cannon, h.attacker, construction);
            break;
        default:
            break;
    }
}

std::uint32_t Military::building_damage(std::uint16_t island, std::uint16_t x, std::uint16_t y) const {
    for (const BuildingDamage& d : m_world->building_damage()) {
        if (d.island == island && d.x == x && d.y == y) {
            return d.damage;
        }
    }
    return 0;
}

void Military::damage_building(std::uint16_t island, std::uint16_t x, std::uint16_t y, std::uint32_t damage,
                               bool cannon, PlayerId attacker, std::vector<ConstructionEvent>* construction) {
    const auto b = building_at(*m_world, *m_buildings, island, {x, y});
    if (!b) {
        return;
    }
    if (!cannon && sturdy(b->type->kind)) {
        damage = damage * 51 / 128;
    }
    const auto ox = static_cast<std::uint16_t>(b->origin.x);
    const auto oy = static_cast<std::uint16_t>(b->origin.y);
    auto& list = m_world->building_damage();
    auto it = std::find_if(list.begin(), list.end(),
                           [&](const BuildingDamage& d) { return d.island == island && d.x == ox && d.y == oy; });
    if (it == list.end()) {
        list.push_back(BuildingDamage{island, ox, oy, 0});
        it = list.end() - 1;
    }
    it->damage += damage;
    const IslandMap& map = m_world->islands()[island].map;
    const MapCell& cell = map.top(static_cast<std::uint32_t>(ox), static_cast<std::uint32_t>(oy));
    if (City* city = m_world->city(island, cell.city)) {
        city->last_attacked_ms = m_world->time_ms();
    }
    if (it->damage < b->type->max_health) {
        return;
    }
    list.erase(it);
    const PlayerId victim = cell.owner;
    // A destroyed market or Kontor blames the attacker (op 0x52; the natives' curse looks at it).
    if ((b->type->prod_kind == prod::kMarket || b->type->prod_kind == prod::kKontor) && acts(*m_world, attacker)) {
        m_world->diplomacy().set_blamed(victim, attacker);
    }
    const Target what = Target::building(island, ox, oy);
    const std::uint8_t city = cell.city;
    const Player* owner = m_world->player(victim);
    const bool natives =
        owner != nullptr && m_world->behaviour(*owner) == FactionBehaviour::Natives && city != MapCell::kNoCity;
    // A village cursed before it is gone curses only once (the dissolved city takes its flag along).
    const City* village = natives ? m_world->city(island, city) : nullptr;
    const bool cursed_before = village != nullptr && village->cursed;
    if (destroy_building(*m_world, *m_buildings, island, {ox, oy}, construction)) {
        m_events.push_back(MilitaryEvent{MilitaryEvent::Kind::BuildingDestroyed, {}, what, attacker, victim});
        m_combat->island_changed(island);
        update_towers(island);
        if (natives && !cursed_before) {
            curse(island, city, m_world->city(island, city) == nullptr);
        }
    }
}

void Military::curse(std::uint16_t island, std::uint8_t city, bool dissolved) {
    // Natives_Curse 0x0047EAA0, from 0x00482120: when the village is gone, or defenceless with fewer
    // than 3 dwellings. The blamed player (the last who killed their warriors or destroyed their
    // main building) is cursed; one time in two a drought follows on the island.
    City* c = m_world->city(island, city);
    if (c != nullptr) {
        if (c->cursed || !city_defenceless(*m_world, *m_buildings, island, city)) {
            return;
        }
        std::uint32_t dwellings = 0;
        const IslandMap& map = m_world->islands()[island].map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const MapCell& cell = map.top(x, y);
                if (cell.city != city || cell.empty()) {
                    continue;
                }
                const auto b = building_at(*m_world, *m_buildings, island,
                                           {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
                dwellings += b && b->type->prod_kind == prod::kPirateHouse &&
                                     b->origin.x == static_cast<std::int32_t>(x) &&
                                     b->origin.y == static_cast<std::int32_t>(y)
                                 ? 1U
                                 : 0U;
            }
        }
        if (dwellings >= 3) {
            return;
        }
        c->cursed = true;
    } else if (!dissolved) {
        return;
    }
    PlayerId natives = kNoPlayer;
    for (std::size_t p = 0; p < m_world->players().size(); ++p) {
        if (m_world->behaviour(m_world->players()[p]) == FactionBehaviour::Natives) {
            natives = static_cast<PlayerId>(p);
        }
    }
    const PlayerId blamed = m_world->diplomacy().blamed(natives);
    if (!acts(*m_world, blamed)) {
        return;
    }
    // The original rolls this on the cursed player's computer only (M-016).
    const bool drought = (m_world->random(RandomStream::Military).next() & 1U) != 0;
    if (drought) {
        m_world->island(island).drought.curse_pending = true;
    }
    m_events.push_back(MilitaryEvent{MilitaryEvent::Kind::NativesCurse, {}, {}, blamed, natives, 0, 0, 0, drought});
}

void Military::regenerate() {
    auto& list = m_world->building_damage();
    for (BuildingDamage& d : list) {
        d.damage = d.damage > kRegenAmount ? d.damage - kRegenAmount : 0;
    }
    std::erase_if(list, [](const BuildingDamage& d) { return d.damage == 0; });
}

void Military::save(core::ByteWriter& out) const {
    out.u32(m_regen_ms);
    out.u32(m_castle_ms);
    out.u32(m_native_ticks);
}

core::Status Military::load(core::ByteReader& reader, std::uint16_t version) {
    if (version < 1 || version > kStateVersion) {
        return core::make_error(core::ErrorCode::Unsupported, "MILI: version " + std::to_string(version));
    }
    // Read whole first, then taken over: a failed load leaves the clocks as they were.
    StateIn in(reader);
    const std::uint32_t regen = in.u32();
    const std::uint32_t castle_ms = version >= 2 ? in.u32() % kCastleSecondMs : m_castle_ms;
    const std::uint32_t native_ticks = version >= 3 ? in.u32() : m_native_ticks;
    if (!in.ok()) {
        return core::make_error(core::ErrorCode::InvalidFormat, "MILI: cut off");
    }
    m_regen_ms = regen % kRegenEpochMs;
    m_castle_ms = castle_ms;
    m_native_ticks = native_ticks;
    return core::Ok{};
}

}  // namespace o1602::sim
