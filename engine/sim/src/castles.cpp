// SPDX-License-Identifier: GPL-3.0-or-later
// Castles: recruiting, training, sending units out; soldiers on board ships (docs/re/military.md).
#include <algorithm>
#include <span>

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/military.hpp"
#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/ships.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint16_t kTrained = 320;               // training points of a finished unit
constexpr std::uint8_t kFirstWeapon = ware::kSwords;  // SCHWERTER, MUSKETEN, KANONEN
constexpr std::uint32_t kFullHold = kHoldCapacity;    // a soldier at full health fills a hold ("50 t")

}  // namespace

CostBuilding* Military::castle_at(std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    for (CostBuilding& c : m_world->cost_buildings()) {
        if (c.kind == CostBuilding::Kind::Castle && c.island == island && c.x == x && c.y == y) {
            return &c;
        }
    }
    return nullptr;
}

std::uint32_t Military::army_size(PlayerId player) const {
    std::uint32_t n = 0;
    for (const Soldier& s : m_world->soldiers()) {
        n += s.owner == player && !s.dying() ? 1U : 0U;
    }
    for (const CostBuilding& b : m_world->cost_buildings()) {
        if (b.kind == CostBuilding::Kind::Castle && b.island < m_world->islands().size() &&
            m_world->islands()[b.island].map.top(b.x, b.y).owner == player) {
            n += static_cast<std::uint32_t>(b.slots.size());
        }
    }
    for (const Ship& ship : m_world->ships()) {
        if (ship.owner == player) {
            for (const CargoSlot& c : ship.cargo) {
                n += is_soldier_ware(c.ware) ? 1U : 0U;
            }
        }
    }
    return n;
}

std::optional<std::pair<std::int32_t, std::int32_t>> Military::exit_cell(std::uint16_t island, std::uint16_t x,
                                                                         std::uint16_t y) const {
    // A cell soldiers can stand on next to the building (0x00463A70), searched from the middle of
    // its lower edge outwards.
    const auto b = building_at(*m_world, *m_buildings, island, {x, y});
    if (!b || island >= m_world->islands().size()) {
        return std::nullopt;
    }
    const WorldIsland& isl = m_world->islands()[island];
    const std::int32_t x0 = 2 * (static_cast<std::int32_t>(isl.x) + b->origin.x);
    const std::int32_t y0 = 2 * (static_cast<std::int32_t>(isl.y) + b->origin.y);
    const std::int32_t x1 = x0 + 2 * static_cast<std::int32_t>(b->width) - 1;
    const std::int32_t y1 = y0 + 2 * static_cast<std::int32_t>(b->height) - 1;
    const std::int32_t mx = (x0 + x1) / 2;
    for (std::int32_t ring = 1; ring <= 8; ++ring) {
        std::optional<std::pair<std::int32_t, std::int32_t>> best;
        std::int32_t best_d = 0;
        for (std::int32_t cy = y0 - ring; cy <= y1 + ring; ++cy) {
            for (std::int32_t cx = x0 - ring; cx <= x1 + ring; ++cx) {
                const bool edge = cx == x0 - ring || cx == x1 + ring || cy == y0 - ring || cy == y1 + ring;
                if (!edge || m_combat->land().cell(cx, cy, 0) != LandMap::Cell::Walk) {
                    continue;
                }
                const std::int32_t d = sea_distance(cx - mx, cy - (y1 + ring));
                if (!best || d < best_d) {
                    best = std::pair{cx, cy};
                    best_d = d;
                }
            }
        }
        if (best) {
            return best;
        }
    }
    return std::nullopt;
}

bool Military::castle(const CastleOrder& o) {
    CostBuilding* c = castle_at(o.island, o.x, o.y);
    if (c == nullptr || o.island >= m_world->islands().size()) {
        return false;
    }
    const PlayerId owner = m_world->islands()[o.island].map.top(o.x, o.y).owner;
    if (o.player != kNoPlayer && owner != o.player) {
        return false;
    }
    City* city = m_world->city(o.island, m_world->islands()[o.island].map.top(o.x, o.y).city);
    const auto b = building_at(*m_world, *m_buildings, o.island, {o.x, o.y});
    if (!b) {
        return false;
    }
    switch (o.op) {
        case CastleOrder::Op::Recruit: {
            // Op 0x10: no gold, no inhabitants; weapons if the castle holds them.
            // Only the soldiers a castle trains (the unit families), not ships, towers or natives.
            const assets::FigureType* type = m_figures->get(o.figure);
            if (type == nullptr || type->combat.max_health == 0 || !assets::FigureTable::unit_variant(o.figure)) {
                return false;
            }
            if (m_rules->max_army != 0 && army_size(owner) >= m_rules->max_army) {
                m_events.push_back(MilitaryEvent{MilitaryEvent::Kind::ArmyFull, {}, {}, owner, owner});
                return false;
            }
            if (c->slots.size() >= b->type->max_wares) {
                return false;
            }
            CastleSlot slot;
            const auto variant = assets::FigureTable::unit_variant(o.figure);
            slot.figure = static_cast<std::uint16_t>(variant ? variant->family : o.figure);
            slot.training = true;
            slot.health = type->combat.max_health;
            const std::uint8_t weapon = type->combat.weapon_ware;
            if (weapon == 0 || type->combat.weapon_amount == 0) {
                slot.armed = true;
            } else if (weapon >= kFirstWeapon && weapon < kFirstWeapon + c->weapons.size() &&
                       c->weapons[weapon - kFirstWeapon] >= type->combat.weapon_amount) {
                c->weapons[weapon - kFirstWeapon] -= type->combat.weapon_amount;
                slot.armed = true;
                if (city != nullptr) {
                    city->book(weapon, GoodsFlow::Military, type->combat.weapon_amount);
                }
            }
            c->slots.push_back(slot);
            return true;
        }
        case CastleOrder::Op::Release: {
            // "Einheit aussenden" (0x004491B0): a trained unit leaves the castle. Units still in
            // training stay (the original's button state is open).
            if (o.slot >= c->slots.size() || c->slots[o.slot].training) {
                return false;
            }
            const auto cell = exit_cell(o.island, o.x, o.y);
            if (!cell) {
                return false;
            }
            release(*c, o.slot, *cell, owner, o.spot);
            return true;
        }
        case CastleOrder::Op::ReleaseAll: {
            // Open1602: all trained units at once, in a block around the spot. The block is fixed
            // here, not taken from the formations of the figure data, which may differ between the
            // computers of a multiplayer game.
            const auto cell = exit_cell(o.island, o.x, o.y);
            if (!cell) {
                return false;
            }
            SoldierOrder group;
            group.player = owner;
            group.target = o.spot;
            for (std::size_t i = 0; i < c->slots.size();) {
                if (c->slots[i].training) {
                    ++i;
                } else {
                    group.soldiers.push_back(release(*c, i, *cell, owner, o.spot));  // the place goes
                }
            }
            if (group.soldiers.empty()) {
                return false;
            }
            if (o.spot.kind == Target::Kind::Spot && group.soldiers.size() > 1) {
                // Rows of `columns` units a tile apart (offsets in 1/8 tile), centred on the spot.
                const auto n = static_cast<std::int32_t>(group.soldiers.size());
                std::int32_t columns = 1;
                while (columns * columns < n) {
                    ++columns;
                }
                const std::int32_t rows = (n + columns - 1) / columns;
                for (std::int32_t i = 0; i < n; ++i) {
                    group.offsets.emplace_back(static_cast<std::int16_t>(8 * (i % columns) - 4 * (columns - 1)),
                                               static_cast<std::int16_t>(8 * (i / columns) - 4 * (rows - 1)));
                }
            }
            if (!o.spot.empty() && target_valid(o.spot)) {
                order(group);
            }
            return true;
        }
        case CastleOrder::Op::Cancel: {
            // Op 0x4C value 0: paid weapons come back as far as the unit is healthy.
            if (o.slot >= c->slots.size()) {
                return false;
            }
            const CastleSlot& slot = c->slots[o.slot];
            const assets::FigureType* type = m_figures->get(slot.figure);
            if (slot.armed && type != nullptr && type->combat.max_health != 0) {
                const std::uint8_t weapon = type->combat.weapon_ware;
                if (weapon >= kFirstWeapon && weapon < kFirstWeapon + c->weapons.size()) {
                    const std::uint32_t back = slot.health * type->combat.weapon_amount / type->combat.max_health;
                    c->weapons[weapon - kFirstWeapon] += back;
                    if (city != nullptr) {
                        city->book(weapon, GoodsFlow::Returned, back);
                    }
                }
            }
            c->slots.erase(c->slots.begin() + o.slot);
            return true;
        }
    }
    return false;
}

std::uint32_t Military::release(CostBuilding& c, std::size_t slot, std::pair<std::int32_t, std::int32_t> cell,
                                PlayerId owner, const Target& spot) {
    const CastleSlot unit = c.slots[slot];
    Soldier s;
    s.place_cell(cell.first, cell.second);
    s.figure = unit.figure;
    s.owner = owner;
    s.island = c.island;
    s.health = unit.health;
    s.anim = 1;
    s.home = Target::building(c.island, c.x, c.y);
    if (!spot.empty() && target_valid(spot)) {
        s.target = spot;
    }
    m_world->add_soldier(s);
    c.slots.erase(c.slots.begin() + static_cast<std::ptrdiff_t>(slot));
    return m_world->soldiers().back().id;
}

void Military::castles_tick() {
    for (CostBuilding& c : m_world->cost_buildings()) {
        if (c.kind == CostBuilding::Kind::Castle) {
            castle_second(c);
        }
    }
}

void Military::castle_second(CostBuilding& c) {
    // Castles_Tick 0x004798E0: a countdown in seconds; at 0 the training points of the castle go to
    // the units in order, the first taking all it needs.
    if (c.island >= m_world->islands().size() || c.slots.empty()) {
        return;
    }
    const auto b = building_at(*m_world, *m_buildings, c.island, {c.x, c.y});
    if (!b) {
        return;
    }
    const PlayerId owner = m_world->islands()[c.island].map.top(c.x, c.y).owner;
    if (c.countdown > 0) {
        --c.countdown;
    }
    if (c.countdown == 0) {
        std::uint32_t budget = b->type->produce_amount;
        const Player* p = m_world->player(owner);
        if (p != nullptr && p->role != PlayerRole::Human) {
            budget = budget * 3 / 2;  // computer players train half again as fast
        }
        bool added = false;
        for (CastleSlot& slot : c.slots) {
            if (!slot.training) {
                continue;
            }
            const std::uint32_t add = std::min<std::uint32_t>(budget, kTrained - std::min(slot.progress, kTrained));
            budget -= add;
            slot.progress = static_cast<std::uint16_t>(slot.progress + add);
            added = added || add != 0;
            if (slot.progress >= kTrained && slot.armed) {
                slot.progress = kTrained;
                slot.training = false;
                m_events.push_back(MilitaryEvent{
                    MilitaryEvent::Kind::UnitTrained, {}, Target::building(c.island, c.x, c.y), owner, owner});
            }
        }
        // Balance::castle_interval_s; a second more when nothing was trained.
        const auto interval = static_cast<std::uint16_t>(m_world->balance().castle_interval_s);
        c.countdown = added ? interval : static_cast<std::uint16_t>(interval + 1);
    }
    // Weapons: from the store, or a pack donkey fetches them from the city.
    for (CastleSlot& slot : c.slots) {
        if (slot.armed) {
            continue;
        }
        const assets::FigureType* type = m_figures->get(slot.figure);
        if (type == nullptr) {
            continue;
        }
        const std::uint8_t weapon = type->combat.weapon_ware;
        if (weapon < kFirstWeapon || weapon >= kFirstWeapon + c.weapons.size()) {
            slot.armed = true;
            continue;
        }
        std::uint32_t& store = c.weapons[weapon - kFirstWeapon];
        if (store >= type->combat.weapon_amount) {
            store -= type->combat.weapon_amount;
            slot.armed = true;
        } else if (!c.fetching) {
            c.fetching = true;
            m_fetch.push_back(FetchRequest{c.island, c.x, c.y, weapon});
        }
    }
    // Trained without weapons: done as soon as they are there (op 0x13 with the next points).
    for (CastleSlot& slot : c.slots) {
        if (slot.training && slot.progress >= kTrained && slot.armed) {
            slot.training = false;
            m_events.push_back(MilitaryEvent{
                MilitaryEvent::Kind::UnitTrained, {}, Target::building(c.island, c.x, c.y), owner, owner});
        }
    }
}

std::optional<Target> Military::warrior_target(std::uint16_t island, TilePos origin, std::uint32_t width,
                                               std::uint32_t height, PlayerId owner) {
    // 0x00467820, called by 0x0047EB30 with a reach of 7: from the hut's origin, with the hut's
    // footprint opened as the start area, a search over the 15 x 15 square around it (no circle)
    // walks on roads, open ground, ruins, squares, bridges, beach ruins, piers and the middle of
    // gates (the Land class, all at the Wegspeed of speed type 3). The goals are the roads and
    // bridges of the natives' owner, never the start; the first goal reached is the target. No
    // random number is drawn; without such a tile the warrior stays in the hut. The original runs
    // its wavefront in rounds of 64 cost units, Open1602 the exact search of PathFinder on the same
    // costs (pathfinding.hpp).
    constexpr std::uint8_t kReach = 7;
    constexpr std::uint8_t kSpeedType = 3;
    const WorldIsland& isl = m_world->islands()[island];
    Catchment area;
    area.centre = origin;
    area.radius = kReach;
    m_finder.begin(isl.map, *m_buildings, area, WalkClass::Land, kSpeedType, false);
    m_finder.open_footprint(origin, width, height);
    const TilePos min = m_finder.window_min();
    for (std::uint32_t j = 0; j < m_finder.window_height(); ++j) {
        for (std::uint32_t i = 0; i < m_finder.window_width(); ++i) {
            const TilePos p{min.x + static_cast<std::int32_t>(i), min.y + static_cast<std::int32_t>(j)};
            if (p == origin || m_finder.cell(p) != PathFinder::Cell::Walkable) {
                continue;
            }
            const MapCell& c = isl.map.top(static_cast<std::uint32_t>(p.x), static_cast<std::uint32_t>(p.y));
            const assets::BuildingType* t = c.empty() ? nullptr : m_buildings->by_sprite(c.sprite);
            if (t != nullptr && c.owner == owner &&
                (t->kind == building_kind::kRoad || t->kind == building_kind::kBridge)) {
                m_finder.set(p, PathFinder::Cell::Goal);
            }
        }
    }
    const auto goal = m_finder.search(origin, [](TilePos) { return PathFinder::Answer::Accept; });
    if (!goal) {
        return std::nullopt;
    }
    return Target::tile_spot(static_cast<std::uint32_t>(static_cast<std::int32_t>(isl.x) + goal->x),
                             static_cast<std::uint32_t>(static_cast<std::int32_t>(isl.y) + goal->y));
}

void Military::natives_tick() {
    constexpr std::uint64_t kAlarmAfterMs = 15 * 60'000;  // not in the first 15 minutes
    constexpr std::uint64_t kAlarmMs = 10 * 60'000;       // ten minutes after an attack
    constexpr std::uint32_t kCalm = 3;
    constexpr std::uint32_t kAlarmed = 7;
    ++m_native_ticks;
    const std::uint64_t now = m_world->time_ms();
    const std::size_t players = m_world->players().size();
    const std::size_t islands = m_world->islands().size();
    std::vector<std::uint32_t> counts;
    for (const City& found : m_world->cities()) {
        const Player* owner = m_world->player(found.owner);
        if (owner == nullptr || m_world->behaviour(*owner) != FactionBehaviour::Natives) {
            continue;
        }
        const bool alarmed =
            found.last_attacked_ms != 0 && now >= kAlarmAfterMs && now - found.last_attacked_ms <= kAlarmMs;
        if (!alarmed && m_native_ticks % 3 != 0) {
            continue;
        }
        const std::uint32_t wanted = alarmed ? kAlarmed : kCalm;
        // Units out by owner and island, counted once in this step instead of once per city
        // (TB-23); the warriors sent below are added to it.
        if (counts.empty()) {
            counts.assign(players * islands, 0);
            for (const Soldier& s : m_world->soldiers()) {
                if (s.owner < players && s.island < islands && !s.dying()) {
                    ++counts[std::size_t{s.owner} * islands + s.island];
                }
            }
        }
        if (found.owner >= players || found.island >= islands) {
            continue;
        }
        std::uint32_t& out = counts[std::size_t{found.owner} * islands + found.island];
        for (CostBuilding& hut : m_world->cost_buildings()) {
            if (out >= wanted) {
                break;
            }
            if (hut.kind != CostBuilding::Kind::Castle || hut.island != found.island ||
                m_world->islands()[hut.island].map.top(hut.x, hut.y).city != found.index) {
                continue;
            }
            const auto b = building_at(*m_world, *m_buildings, hut.island, {hut.x, hut.y});
            if (!b) {
                continue;
            }
            // From the last place: a trained warrior goes out, a new one starts training in its place.
            // The map does not change in this loop, so the hut's target is searched once.
            std::optional<std::optional<Target>> target;
            for (std::size_t k = hut.slots.size(); k-- > 0 && out < wanted;) {
                if (hut.slots[k].figure == 0 || hut.slots[k].training) {
                    continue;
                }
                const auto cell = exit_cell(hut.island, hut.x, hut.y);
                if (!cell) {
                    break;
                }
                if (!target) {
                    target = warrior_target(hut.island, b->origin, b->width, b->height, found.owner);
                }
                if (!*target) {
                    continue;  // no owned road or bridge in reach: this place sends nobody
                }
                const Target spot = **target;
                const CastleSlot slot = hut.slots[k];
                Soldier s;
                s.place_cell(cell->first, cell->second);
                s.figure = slot.figure;
                s.owner = found.owner;
                s.island = hut.island;
                s.health = slot.health;
                s.anim = 1;
                s.home = spot;
                s.target = spot;
                m_world->add_soldier(s);
                ++out;
                const assets::FigureType* type = m_figures->get(b->type->worker_figure);
                hut.slots[k] = CastleSlot{b->type->worker_figure, true, true,
                                          type != nullptr ? type->combat.max_health : slot.health, 0};
            }
        }
    }
}

void Military::castle_delivery(std::uint16_t island, std::uint16_t x, std::uint16_t y, std::uint8_t ware,
                               std::uint32_t amount) {
    CostBuilding* c = castle_at(island, x, y);
    if (c == nullptr) {
        return;
    }
    c->fetching = false;
    if (ware >= kFirstWeapon && ware < kFirstWeapon + c->weapons.size()) {
        c->weapons[ware - kFirstWeapon] += amount;
    }
}

bool Military::board(Soldier& s, Ship& ship) {
    // Soldier_Board 0x00458000: the first empty hold takes the unit, its health as the amount.
    const assets::FigureType* type = type_of(s);
    if (type == nullptr || ship.owner != s.owner || ship.sinking) {
        return false;
    }
    for (CargoSlot& c : std::span<CargoSlot>(ship.cargo.data(), usable_holds(ship, *m_figures))) {
        if (c.ware != 0) {
            continue;
        }
        const std::uint32_t max = std::max<std::uint32_t>(1, type->combat.max_health);
        const std::uint32_t health = s.health != 0 ? s.health : max;
        c.ware = unit_ware(s.figure);
        c.amount = std::max<std::uint32_t>(1, health * kFullHold / max);
        c.price = 0;
        m_events.push_back(MilitaryEvent{MilitaryEvent::Kind::Boarded, Target::soldier(s.id),
                                         Target::other_ship(ship.id), s.owner, s.owner, s.pos_x, s.pos_y});
        s.owner = kNoPlayer;  // gone at the end of the tick
        s.motion.phase = SoldierMotion::Phase::Wait;
        return true;
    }
    return false;
}

bool Military::unload(const UnloadOrder& o) {
    Ship* ship = m_world->ship(o.ship);
    if (ship == nullptr || ship->sinking || (o.player != kNoPlayer && ship->owner != o.player)) {
        return false;
    }
    // Land within 6 tiles of the ship (0x00448D90, Balance::castle_unload_range): the nearest cell
    // soldiers can stand on.
    const auto range = static_cast<std::int32_t>(m_world->balance().castle_unload_range);
    const std::int32_t sx = 2 * ship->x + 1;
    const std::int32_t sy = 2 * ship->y + 1;
    std::optional<std::pair<std::int32_t, std::int32_t>> landing;
    std::int32_t best = 0;
    for (std::int32_t cy = sy - 2 * range; cy <= sy + 2 * range; ++cy) {
        for (std::int32_t cx = sx - 2 * range; cx <= sx + 2 * range; ++cx) {
            if (m_combat->land().cell(cx, cy, 0) != LandMap::Cell::Walk) {
                continue;
            }
            const std::int32_t d = sea_distance(cx - sx, cy - sy);
            if (d <= 2 * range && (!landing || d < best)) {
                landing = std::pair{cx, cy};
                best = d;
            }
        }
    }
    if (!landing) {
        return false;
    }
    const std::uint16_t island = m_combat->land().island_at(landing->first / 2, landing->second / 2);
    bool any = false;
    for (std::size_t h = 0; h < ship->cargo.size(); ++h) {
        CargoSlot& c = ship->cargo[h];
        if (!is_soldier_ware(c.ware) || (o.hold != 0xFF && h != o.hold)) {
            continue;
        }
        const std::uint16_t figure = unit_of_ware(c.ware);
        const assets::FigureType* type = m_figures->get(figure);
        if (type == nullptr) {
            continue;
        }
        Soldier s;
        s.place_cell(landing->first, landing->second);
        s.figure = figure;
        s.owner = ship->owner;
        s.island = island;
        // Rounded up: the original truncates and loses up to 1/32 HP on every trip (S-089).
        const std::uint32_t max = type->combat.max_health;
        s.health = std::min(max, (c.amount * max + kFullHold - 1) / kFullHold);
        s.anim = 1;
        m_world->add_soldier(s);
        c = CargoSlot{};
        any = true;
    }
    if (!any) {
        return false;
    }
    // Owners of other cities on the island hear of it (message 16).
    for (const City& city : m_world->cities()) {
        if (city.island == island && city.owner != ship->owner) {
            m_events.push_back(MilitaryEvent{MilitaryEvent::Kind::TroopsLanded,
                                             Target::other_ship(ship->id),
                                             {},
                                             ship->owner,
                                             city.owner,
                                             landing->first * kCellUnit,
                                             landing->second * kCellUnit});
        }
    }
    return true;
}

}  // namespace o1602::sim
