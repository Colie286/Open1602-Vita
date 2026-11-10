// SPDX-License-Identifier: GPL-3.0-or-later
//
// Military, diplomacy and the pirates' nest of the computer players (docs/re/ai.md, "Military and diplomacy"):
// recruiting and guards every 6 s, ship missions with the logistics every 10 s, a look at one
// foreign city after every step, the soldiers on one island per tick, the treaties every 30 s.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <span>

#include "ai_engine.hpp"

#include "o1602/sim/ships.hpp"

namespace o1602::sim::ai {

namespace {

/// Guard posts kept per city (the original's list at R+0x188).
constexpr std::size_t kMaxPosts = 64;

/// The pirate nest's offer comes again after more than 42 s (0x1A4 tenths, task 5 at 0x00429DF0).
constexpr std::uint64_t kNestOfferMs = 42'000;

std::uint32_t family_index(std::uint32_t figure) {
    const auto v = assets::FigureTable::unit_variant(figure);
    const std::uint32_t family = v ? v->family : figure;
    for (std::uint32_t i = 0; i < figure::kUnitFamilies.size(); ++i) {
        if (figure::kUnitFamilies[i] == family) {
            return i;
        }
    }
    return 0;
}

std::uint32_t city_param(const City& c) {
    return std::uint32_t{c.island} << 8 | c.index;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Armies (0x0040EAE0, 0x0040ED50)

const Engine::Census* Engine::census() const {
    if (!m_in_tick) {
        return nullptr;
    }
    Census& c = m_census;
    if (c.valid) {
        return &c;
    }
    c.players = m_world.players().size();
    c.islands = m_world.islands().size();
    c.on_island.assign(c.players * c.islands, 0);
    c.army.assign(c.players, {});
    c.aboard.assign(c.players, {});
    c.cannons.assign(c.players, 0);
    c.own_ships.resize(c.players);
    for (auto& list : c.own_ships) {
        list.clear();
    }
    for (const Soldier& s : m_world.soldiers()) {
        if (s.owner >= c.players) {
            continue;
        }
        if (!s.away() && s.island < c.islands) {
            ++c.on_island[std::size_t{s.owner} * c.islands + s.island];
        }
        if (!s.dying()) {
            ++c.army[s.owner][family_index(s.figure)];
        }
    }
    const auto& ships = m_world.ships();
    for (std::size_t i = 0; i < ships.size(); ++i) {
        const Ship& s = ships[i];
        if (s.owner >= c.players) {
            continue;
        }
        for (const CargoSlot& slot : s.cargo) {
            if (is_soldier_ware(slot.ware)) {
                ++c.aboard[s.owner][family_index(unit_of_ware(slot.ware))];
            }
        }
        if (!s.sinking) {
            c.cannons[s.owner] += s.cannons;
            if (!s.trader && !s.cart()) {
                c.own_ships[s.owner].push_back(static_cast<std::uint32_t>(i));
            }
        }
    }
    c.valid = true;
    return &c;
}

std::vector<Ship*> Engine::own_ships(const AiPlayer& p) {
    std::vector<Ship*> out;
    if (const Census* c = census(); c != nullptr && p.player < c->players) {
        out.reserve(c->own_ships[p.player].size());
        for (const std::uint32_t i : c->own_ships[p.player]) {
            out.push_back(&m_world.ships()[i]);
        }
        return out;
    }
    for (Ship& s : m_world.ships()) {
        if (s.owner == p.player && !s.sinking && !s.trader && !s.cart()) {
            out.push_back(&s);
        }
    }
    return out;
}

std::array<std::uint32_t, 4> Engine::army_counts(PlayerId owner) const {
    const Census* census = this->census();
    std::array<std::uint32_t, 4> n{};
    if (census != nullptr && owner < census->players) {
        n = census->army[owner];
    } else {
        for (const Soldier& s : m_world.soldiers()) {
            if (s.owner == owner && !s.dying()) {
                ++n[family_index(s.figure)];
            }
        }
    }
    for (const CostBuilding& b : m_world.cost_buildings()) {
        if (b.kind != CostBuilding::Kind::Castle || b.island >= m_world.islands().size() ||
            m_world.islands()[b.island].map.top(b.x, b.y).owner != owner) {
            continue;
        }
        for (const CastleSlot& slot : b.slots) {
            if (slot.figure != 0) {
                ++n[family_index(slot.figure)];
            }
        }
    }
    if (census != nullptr && owner < census->players) {
        for (std::size_t i = 0; i < 4; ++i) {
            n[i] += census->aboard[owner][i];
        }
        return n;
    }
    for (const Ship& s : m_world.ships()) {
        if (s.owner != owner) {
            continue;
        }
        for (const CargoSlot& c : s.cargo) {
            if (is_soldier_ware(c.ware)) {
                ++n[family_index(unit_of_ware(c.ware))];
            }
        }
    }
    return n;
}

std::array<std::uint32_t, 4> Engine::unit_weights() const {
    // Damage per second of each unit: Hitpoint / Worktime (sword 40, cavalry 51, musket 38, cannon 49).
    std::array<std::uint32_t, 4> w{};
    for (std::size_t i = 0; i < figure::kUnitFamilies.size(); ++i) {
        const assets::FigureType* t = m_sim.m_figures->get(figure::kUnitFamilies[i]);
        if (t != nullptr && t->work_time_ms != 0) {
            w[i] = static_cast<std::uint32_t>(std::uint64_t{t->combat.hit} * 1000U / (32U * t->work_time_ms));
        }
    }
    return w;
}

std::uint32_t Engine::weighted_army(PlayerId owner) const {
    const auto n = army_counts(owner);
    const auto w = unit_weights();
    std::uint32_t s = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        s += n[i] * w[i];
    }
    return s;
}

std::uint32_t Engine::army_target(const AiPlayer& p) const {
    // A scenario can ask for a fixed strength instead: a hundred cannons' worth (player +0x70
    // bit 5, 0x004257BF), on every level.
    if (player(p).scenario.has(scenario_flag::kFixedArmy)) {
        return unit_weights()[3] * 100;
    }
    // 1.5 times the strongest human army, more on harder levels.
    std::uint32_t best = 0;
    for (std::size_t h = 0; h < m_world.players().size(); ++h) {
        if (m_world.players()[h].role != PlayerRole::Human) {
            continue;
        }
        std::uint32_t s = weighted_army(static_cast<PlayerId>(h));
        s += s / 2;
        best = std::max(best, s);
    }
    switch (difficulty()) {
        case 0:
            break;
        case 1:
            best += best / 4;
            break;
        case 2:
            best += best / 2;
            break;
        default:
            best *= 2;
            break;
    }
    return best;
}

bool Engine::stronger(PlayerId a, PlayerId b) const {
    // 0x0040ED50 compares the armies and the cannons of the fleets. The original takes b's fleet
    // on both sides, so only the armies count (S-102).
    const auto w = unit_weights();
    const auto fleet = [&](PlayerId q) {
        if (const Census* c = census(); c != nullptr && q < c->players) {
            return c->cannons[q];
        }
        std::uint32_t n = 0;
        for (const Ship& s : m_world.ships()) {
            n += s.owner == q && !s.sinking ? s.cannons : 0U;
        }
        return n;
    };
    return weighted_army(a) + fleet(a) * w[3] >= weighted_army(b) + fleet(b) * w[3];
}

std::uint32_t Engine::soldiers_on_island(PlayerId owner, std::uint16_t island) const {
    if (const Census* c = census(); c != nullptr && owner < c->players && island < c->islands) {
        return c->on_island[std::size_t{owner} * c->islands + island];
    }
    std::uint32_t n = 0;
    for (const Soldier& s : m_world.soldiers()) {
        n += s.owner == owner && s.island == island && !s.away() ? 1U : 0U;
    }
    return n;
}

std::vector<Soldier*> Engine::soldiers_near(PlayerId owner, const City& c, std::int32_t range, std::size_t max) {
    std::vector<Soldier*> out;
    if (!c.warehouse_x || !c.warehouse_y) {
        return out;
    }
    const WorldIsland& isl = island_of(c.island);
    const std::int32_t kx = static_cast<std::int32_t>(isl.x) + *c.warehouse_x;
    const std::int32_t ky = static_cast<std::int32_t>(isl.y) + *c.warehouse_y;
    for (Soldier& s : m_world.soldiers()) {
        if (out.size() >= max) {
            break;
        }
        if (s.owner != owner || s.island != c.island || s.away()) {
            continue;
        }
        if (sea_distance(s.pos_x / kPosUnit - kx, s.pos_y / kPosUnit - ky) <= range) {
            out.push_back(&s);
        }
    }
    return out;
}

std::uint32_t Engine::soldiers_aboard(const Ship& s) const {
    std::uint32_t n = 0;
    for (const CargoSlot& c : s.cargo) {
        n += is_soldier_ware(c.ware) ? 1U : 0U;
    }
    return n;
}

std::uint32_t Engine::free_holds(const Ship& s) const {
    std::uint32_t n = 0;
    for (const CargoSlot& c : std::span<const CargoSlot>(s.cargo.data(), usable_holds(s, *m_sim.m_figures))) {
        n += c.ware == 0 ? 1U : 0U;
    }
    return n;
}

// ---------------------------------------------------------------------------------------------
// Recruiting and guards (0x00425580, every 6 s)

void Engine::queue_unit(AiPlayer& p, const CastleEntry& castle, std::uint32_t family) {
    // 0x00424E20: swordsmen and cavalry one at a time per castle, the others may stack.
    const CostBuilding* b = nullptr;
    const std::uint16_t island = p.castle_island.value_or(0);
    for (const CostBuilding& c : m_world.cost_buildings()) {
        if (c.kind == CostBuilding::Kind::Castle && c.island == island && c.x == castle.x && c.y == castle.y) {
            b = &c;
        }
    }
    if (b == nullptr) {
        return;
    }
    const std::uint32_t t = family_index(family);
    for (const CastleSlot& slot : b->slots) {
        if (slot.figure != 0 && family_index(slot.figure) == t && t < 2) {
            return;
        }
    }
    CastleOrder order;
    order.player = p.player;
    order.island = island;
    order.x = castle.x;
    order.y = castle.y;
    order.op = CastleOrder::Op::Recruit;
    order.figure = static_cast<std::uint16_t>(family);
    execute(order);
}

bool Engine::guard_post(AiPlayer& p, City& c, CityPlan& r, std::int32_t& x, std::int32_t& y, bool& at_yard) {
    // 0x00425460: the markets of the city in turn, then the own shipyard.
    const auto market_at = [&](std::int32_t mx, std::int32_t my) {
        const Tile t = tile(c.island, mx, my);
        return t.valid() && t.city == c.index && t.prod == prod::kMarket && !t.type->destroyed;
    };
    // The original fills the list once per plan (until the hourly re-init). Open1602 fills it again
    // when the survey counts more markets than it holds or the next post is no market any more
    // (GB-4), so new markets get guards and soldiers stop going to a burnt one. The original's
    // computers keep their list.
    const std::size_t markets = std::min<std::size_t>(r.slots[kSlotMarket].count, kMaxPosts);
    const bool stale =
        !classic() && (r.posts.size() < markets || (r.post_cursor < r.posts.size() &&
                                                    !market_at(r.posts[r.post_cursor].x, r.posts[r.post_cursor].y)));
    if (r.posts.empty() || stale) {
        r.posts.clear();
        const IslandMap& map = island_of(c.island).map;
        for (const Production& pr : m_world.productions()) {
            if (!pr.used || pr.island != c.island || !map.contains(pr.x, pr.y)) {
                continue;
            }
            if (market_at(pr.x, pr.y) && r.posts.size() < kMaxPosts) {
                r.posts.push_back(GuardPost{pr.x, pr.y});
            }
        }
        r.post_cursor = 0;
    }
    at_yard = false;
    if (r.post_cursor < r.posts.size()) {
        x = r.posts[r.post_cursor].x;
        y = r.posts[r.post_cursor].y;
        ++r.post_cursor;
        return true;
    }
    r.post_cursor = 0;
    if (p.yard_valid && p.yard_island == c.island && r.slots[kSlotShipyard].count != 0) {
        x = p.yard_x;
        y = p.yard_y;
        at_yard = true;
        return true;
    }
    if (!r.posts.empty()) {
        x = r.posts[0].x;
        y = r.posts[0].y;
        r.post_cursor = 1;
        return true;
    }
    return false;
}

void Engine::guard(AiPlayer& p, City& c, std::int32_t x, std::int32_t y, std::uint32_t n, bool castle_island) {
    // 0x004251A0: soldiers beyond the reserve at the Kontor go to the post.
    std::vector<Soldier*> near = soldiers_near(p.player, c, 8, 16);
    const std::size_t reserve = castle_island ? 8 : 0;
    if (near.size() <= reserve) {
        return;
    }
    const WorldIsland& isl = island_of(c.island);
    const Target post =
        Target::spot(2 * (isl.x + static_cast<std::uint32_t>(x)) + 1, 2 * (isl.y + static_cast<std::uint32_t>(y)) + 1);
    std::uint32_t here = 0;
    for (const Soldier& s : m_world.soldiers()) {
        here += s.owner == p.player && s.target == post ? 1U : 0U;
    }
    const auto spare = static_cast<std::uint32_t>(near.size() - reserve);
    const std::uint32_t wanted = n < 2 ? spare + here : soldiers_on_island(p.player, c.island) / n + 1;
    if (here >= wanted) {
        return;
    }
    SoldierOrder order;
    order.player = p.player;
    order.target = post;
    for (std::size_t i = reserve; i < near.size(); ++i) {
        if (near[i]->target.kind != Target::Kind::Building && !(near[i]->target == post)) {
            order.soldiers.push_back(near[i]->id);
        }
    }
    if (!order.soldiers.empty()) {
        execute(order);
    }
}

bool Engine::market_tower(AiPlayer& p, City& c, std::int32_t mx, std::int32_t my) {
    // 0x00425030: one stone tower beside each market, on a wall or the beach if there is one.
    const Tile m = tile(c.island, mx, my);
    const assets::BuildingType* tower = by_id(id::kStoneTower);
    if (!m.valid() || tower == nullptr) {
        return false;
    }
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    rotated_size(*m.type,
                 island_of(c.island).map.top(static_cast<std::uint32_t>(mx), static_cast<std::uint32_t>(my)).rotation,
                 w, h);
    std::int32_t best = 0;
    std::int32_t bx = 0;
    std::int32_t by = 0;
    bool towers = false;
    rings(p, RingFilter::Own, mx, my, mx + static_cast<std::int32_t>(w) - 1, my + static_cast<std::int32_t>(h) - 1, 2,
          [&](const Tile& t, std::int32_t x, std::int32_t y, std::int32_t d) {
              if (t.prod == prod::kTower) {
                  towers = true;
              }
              std::int32_t score = 0;
              if (t.kind == kind::kWall || t.kind == kind::kWallBeach || t.kind == kind::kBeach ||
                  t.kind == kind::kBeachOuter || t.kind == kind::kBeachInner || t.kind == kind::kBeachVariant ||
                  t.kind == kind::kBeachRuin) {
                  score = 500;
              } else if (t.kind == kind::kRuin || t.ware == ware::kGrass || t.ware == ware::kTree) {
                  score = (2 - d) * 50;
              }
              if (t.prod == prod::kShipyard && d == 0) {
                  best = 0;
              }
              if (score > best && can_place(p, *tower, x, y, 1, 1)) {
                  best = score;
                  bx = x;
                  by = y;
              }
          });
    if (towers || best <= 0) {
        return false;
    }
    return build(p, {BuildItem{tower->id, bx, by, 0, 0}}, false) != 0;
}

void Engine::recruit(AiPlayer& p) {
    // 0x00425580: one own city per call.
    const auto& cities = m_world.cities();
    if (cities.empty()) {
        return;
    }
    City* c = nullptr;
    for (std::size_t n = 0; n < cities.size(); ++n) {
        p.recruit_cursor = static_cast<std::uint32_t>((p.recruit_cursor + 1) % cities.size());
        if (cities[p.recruit_cursor].owner == p.player) {
            c = city(cities[p.recruit_cursor].island, cities[p.recruit_cursor].index);
            break;
        }
    }
    if (c == nullptr) {
        return;
    }
    CityPlan& r = plan_of(p, c->island, c->index);
    std::int32_t px = 0;
    std::int32_t py = 0;
    bool at_yard = false;
    if (!guard_post(p, *c, r, px, py, at_yard)) {
        return;
    }
    const std::uint32_t army = m_sim.military().army_size(p.player);
    const std::uint32_t pop = total_population(p.player);
    const std::uint32_t cap = m_sim.rules().max_army;
    // Castles that still stand.
    std::erase_if(p.castles, [&](const CastleEntry& e) {
        const Tile t = tile(p.castle_island.value_or(0), e.x, e.y);
        return !t.valid() || t.prod != prod::kMilitary || t.owner != p.player;
    });
    if (p.castles.empty()) {
        p.castle_island.reset();
    }
    if ((cap != 0 && army >= cap) || army >= pop / 16) {
        // Room for better units: a swordsman goes when muskets are there (op 5 value 7).
        if (r.slots[kSlotMuskets].count != 0 && c->stock(ware::kMuskets) != 0) {
            for (const Soldier& s : m_world.soldiers()) {
                if (s.owner == p.player && s.island == c->island && family_index(s.figure) == 0 && !s.away()) {
                    SoldierCommand cmd;
                    cmd.player = p.player;
                    cmd.soldier = s.id;
                    cmd.op = SoldierCommand::Op::Disband;
                    execute(cmd);
                    break;
                }
            }
        }
    } else {
        // Trained units leave the castles for the Kontor.
        for (const CastleEntry& e : p.castles) {
            const std::uint16_t island = p.castle_island.value_or(0);
            for (const CostBuilding& b : m_world.cost_buildings()) {
                if (b.kind != CostBuilding::Kind::Castle || b.island != island || b.x != e.x || b.y != e.y) {
                    continue;
                }
                for (std::size_t i = 0; i < b.slots.size(); ++i) {
                    if (b.slots[i].figure != 0 && !b.slots[i].training) {
                        const WorldIsland& isl = island_of(island);
                        CastleOrder order;
                        order.player = p.player;
                        order.island = island;
                        order.x = e.x;
                        order.y = e.y;
                        order.op = CastleOrder::Op::Release;
                        order.slot = static_cast<std::uint8_t>(i);
                        order.spot = Target::spot(2 * (isl.x + e.rally_x) + 1, 2 * (isl.y + e.rally_y) + 1);
                        execute(order);
                        break;
                    }
                }
                break;
            }
        }
    }
    // A tower beside the market (cannons from the city: 2 t in the home city, 1 t elsewhere).
    const bool home = p.home_valid && p.home_island == c->island && p.home_city == c->index;
    const std::uint32_t cannons_needed = home ? 0x41U : 0x20U;
    const assets::BuildingType* tower = by_id(id::kStoneTower);
    if (!at_yard && tower != nullptr && c->stock(m_buildings.materials().cannons) >= cannons_needed &&
        unlocked(p, *tower) && affordable(p, *c, *tower)) {
        const bool own_city = p.has_city && p.island == c->island && p.city == c->index;
        const bool saved = p.has_city;
        const std::uint16_t si = p.island;
        const std::uint8_t sc = p.city;
        p.has_city = true;
        p.island = c->island;
        p.city = c->index;
        const bool built = market_tower(p, *c, px, py);
        if (!own_city) {
            p.has_city = saved;
            p.island = si;
            p.city = sc;
        }
        if (built) {
            ++r.slots[kSlotStoneTower].count;
            return;
        }
    }
    const std::uint32_t target = army_target(p);
    if (target == 0) {
        return;
    }
    p.castle_cursor = static_cast<std::uint8_t>((p.castle_cursor + 1) % 3);
    if (p.castle_island && *p.castle_island == c->island) {
        if (p.castle_cursor < p.castles.size() && weighted_army(p.player) <= target) {
            const CastleEntry& e = p.castles[p.castle_cursor];
            if (r.slots[kSlotMuskets].count != 0 && c->stock(ware::kMuskets) > 31) {
                queue_unit(p, e, figure::kUnitFamilies[2]);
            }
            if (r.slots[kSlotCannons].count != 0 && c->stock(ware::kCannons) > 127) {
                queue_unit(p, e, figure::kUnitFamilies[3]);
            }
            if (r.slots[kSlotSwords].count != 0) {
                queue_unit(p, e, figure::kUnitFamilies[1]);
                if (r.slots[kSlotMuskets].count == 0 && r.slots[kSlotCannons].count == 0) {
                    queue_unit(p, e, figure::kUnitFamilies[0]);
                    queue_unit(p, e, figure::kUnitFamilies[1]);
                }
            }
        }
        if (!at_yard) {
            guard(p, *c, px, py, r.slots[kSlotMarket].count, true);
        }
        return;
    }
    // A colony: guards, and troops from home by ship.
    const std::uint32_t k = soldiers_on_island(p.player, c->island);
    if (k != 0 && !at_yard) {
        guard(p, *c, px, py, static_cast<std::uint32_t>(r.posts.size()), false);
    }
    City* home_c = home_city(p);
    if (home_c != nullptr && army >= 8) {
        // k < (army - 8) * 0.006 * (40 or 15), in integers (ADR 0004: no floating point in the game's
        // decisions). Equal to the floating point form for every army below 433 units.
        const std::uint64_t f = std::uint64_t{army - 8} * 6U * (has_population(*c, 0, 100) ? 40U : 15U);
        if (std::uint64_t{k} * 1000U < f && soldiers_near(p.player, *home_c, 8, 16).size() >= 8) {
            if (Ship* ship = best_armed_ship(p, Target::city_of(home_c->island, home_c->index), 1)) {
                ship_send(p, *ship, Target::city_of(home_c->island, home_c->index), 1, city_param(*c));
            }
        }
    }
}

void Engine::buy_pirate_peace(AiPlayer& p) {
    // 0x00428AF0: with more than 10,000 gold and a price above 1000.
    PlayerId pirates = kNoPlayer;
    for (std::size_t i = 0; i < m_world.players().size(); ++i) {
        if (m_world.behaviour(m_world.players()[i]) == FactionBehaviour::Pirates) {
            pirates = static_cast<PlayerId>(i);
        }
    }
    if (pirates == kNoPlayer) {
        return;
    }
    const std::uint32_t price = pirate_peace_price(m_world, pirates, p.player);
    Player& pl = player(p);
    if (price > 1000 && static_cast<std::int64_t>(price) < std::int64_t{pl.money} - 9000) {
        m_world.diplomacy().add_event(pirates, p.player, DiplomacyEventType::PeaceBought, now_ms());
        pl.money = money_plus(pl.money, -std::int64_t{price});
    }
}

// ---------------------------------------------------------------------------------------------
// Ships on missions (0x004238F0, with the logistics every 10 s)

void Engine::ship_send(AiPlayer& p, Ship& s, const Target& t, std::uint8_t mission, std::uint32_t param) {
    // 0x0040F070
    ShipOrder order;
    order.player = p.player;
    order.ship = s.id;
    order.target = t;
    execute(order);
    ShipNote& n = note(p, s.id);
    n.mission = mission;
    n.param = param;
    n.counter = 10;
    n.patrol = false;
    n.supply = false;
}

void Engine::mission_end(AiPlayer& p, Ship& s, ShipNote& n) {
    // 0x0040F250: home with the troops, or stop.
    if (n.mission == 3 || n.mission == 4) {
        ShipOrder stop;
        stop.player = p.player;
        stop.ship = s.id;
        execute(stop);
        n.mission = 0;
        n.param = 0;
        n.counter = 0;
        return;
    }
    if (City* home = home_city(p)) {
        ship_send(p, s, Target::city_of(home->island, home->index), 2, 0);
        return;
    }
    n.mission = 0;
    n.param = 0;
    n.counter = 0;
}

void Engine::board(AiPlayer& p, Ship& s, const City& at, std::uint32_t max) {
    std::vector<Soldier*> near = soldiers_near(p.player, at, 8, 16);
    SoldierOrder order;
    order.player = p.player;
    order.target = Target::other_ship(s.id);
    for (Soldier* soldier : near) {
        if (order.soldiers.size() >= max) {
            break;
        }
        if (soldier->target.kind != Target::Kind::Building) {
            order.soldiers.push_back(soldier->id);
        }
    }
    if (!order.soldiers.empty()) {
        execute(order);
    }
}

void Engine::missions(AiPlayer& p) {
    for (ShipNote& n : p.ships) {
        if (n.mission == 0) {
            continue;
        }
        Ship* s = m_world.ship(n.id);
        if (s == nullptr || s->held || s->repairing) {
            continue;
        }
        if (ship_moving(*s)) {
            continue;
        }
        const Target target = s->order;
        bool arrived = !target.empty();
        if (target.kind == Target::Kind::City) {
            const City* c = city(target.island, target.city);
            arrived = c != nullptr && ship_at_city(*s, *c);
        }
        if (!arrived) {
            if (n.counter == 0 || target.empty()) {
                mission_end(p, *s, n);
                continue;
            }
            --n.counter;
            ShipOrder again;
            again.player = p.player;
            again.ship = s->id;
            again.target = target;
            execute(again);
            continue;
        }
        switch (n.mission) {
            case 1: {
                // Troops at home for a colony.
                const City* home = home_city(p);
                if (home != nullptr && soldiers_aboard(*s) < 2 && n.counter != 0 && free_holds(*s) != 0) {
                    board(p, *s, *home, free_holds(*s));
                    --n.counter;
                    break;
                }
                const City* colony =
                    city(static_cast<std::uint16_t>(n.param >> 8), static_cast<std::uint8_t>(n.param & 0xFF));
                if (soldiers_aboard(*s) != 0 && colony != nullptr && colony->owner == p.player) {
                    ship_send(p, *s, Target::city_of(colony->island, colony->index), 2, 0);
                } else {
                    n.mission = 0;
                }
                break;
            }
            case 2: {
                UnloadOrder order;
                order.player = p.player;
                order.ship = s->id;
                execute(order);
                n.mission = 0;
                n.param = 0;
                n.counter = 0;
                break;
            }
            case 5: {
                // Troops for an attack.
                const City* home = home_city(p);
                const std::uint32_t aboard = soldiers_aboard(*s);
                if (home != nullptr && n.counter != 0 && free_holds(*s) != 0) {
                    const std::size_t before = soldiers_near(p.player, *home, 8, 16).size();
                    board(p, *s, *home, free_holds(*s));
                    --n.counter;
                    if (before != 0) {
                        break;
                    }
                }
                const City* t =
                    city(static_cast<std::uint16_t>(n.param >> 8), static_cast<std::uint8_t>(n.param & 0xFF));
                if (aboard > 3 && t != nullptr) {
                    n.mission = 6;
                    plan_landing(p, *s, n);
                } else if (aboard != 0 && home != nullptr) {
                    ship_send(p, *s, Target::city_of(home->island, home->index), 2, 0);  // S-101: home, not a stray ref
                } else {
                    mission_end(p, *s, n);
                }
                break;
            }
            case 6:
                plan_landing(p, *s, n);
                break;
            case 7:
            case 8:
            case 9: {
                const PlayerId owner = static_cast<PlayerId>(n.param & 0xFF);
                if (m_world.diplomacy().relation(p.player, owner).peace != Treaty::Signed) {
                    UnloadOrder order;
                    order.player = p.player;
                    order.ship = s->id;
                    execute(order);
                    n.mission = 0;
                    n.param = 0;
                } else {
                    mission_end(p, *s, n);
                }
                break;
            }
            default:
                break;
        }
    }
}

void Engine::plan_landing(AiPlayer& p, Ship& s, ShipNote& n) {
    // 0x00422EC0: a beach near the target's markets, away from its towers and soldiers.
    const City* t = city(static_cast<std::uint16_t>(n.param >> 8), static_cast<std::uint8_t>(n.param & 0xFF));
    if (t == nullptr || t->owner == p.player || building_count(*t) <= 3 ||
        m_world.diplomacy().relation(p.player, t->owner).peace == Treaty::Signed ||
        soldiers_on_island(p.player, t->island) >= 8) {
        mission_end(p, s, n);
        return;
    }
    const std::uint16_t island = t->island;
    const WorldIsland& isl = island_of(island);
    // Targets: the markets, or the Kontor of a small city.
    std::vector<std::pair<std::int32_t, std::int32_t>> targets;
    const IslandMap& map = isl.map;
    for (const Production& pr : m_world.productions()) {
        if (!pr.used || pr.island != island || !map.contains(pr.x, pr.y)) {
            continue;
        }
        const Tile tt = tile(island, pr.x, pr.y);
        if (tt.valid() && tt.city == t->index && tt.prod == prod::kMarket) {
            targets.emplace_back(pr.x, pr.y);
        }
    }
    if (targets.empty() && t->warehouse_x && t->warehouse_y) {
        targets.emplace_back(*t->warehouse_x, *t->warehouse_y);
    }
    if (targets.empty()) {
        mission_end(p, s, n);
        return;
    }
    // The danger, gathered once (TB-10): hostile soldiers on the island and hostile towers on it
    // (tile positions), hostile ships near it (world positions); a ship farther than 16 tiles
    // from the island's rectangle reaches no beach tile.
    std::vector<std::pair<std::int32_t, std::int32_t>> soldiers;
    for (const Soldier& o : m_world.soldiers()) {
        if (o.island == island && m_world.diplomacy().hostile(p.player, o.owner)) {
            soldiers.emplace_back(o.pos_x / kPosUnit, o.pos_y / kPosUnit);
        }
    }
    std::vector<std::pair<std::int32_t, std::int32_t>> towers;
    for (const Tower& tw : m_world.towers()) {
        if (tw.island == island && m_world.diplomacy().hostile(p.player, tw.owner)) {
            towers.emplace_back(tw.x, tw.y);
        }
    }
    std::vector<std::pair<std::int32_t, std::int32_t>> ships;
    const auto ix0 = static_cast<std::int32_t>(isl.x);
    const auto iy0 = static_cast<std::int32_t>(isl.y);
    const auto ix1 = ix0 + static_cast<std::int32_t>(map.width()) - 1;
    const auto iy1 = iy0 + static_cast<std::int32_t>(map.height()) - 1;
    for (const Ship& o : m_world.ships()) {
        if (!o.cart() && o.x >= ix0 - 16 && o.x <= ix1 + 16 && o.y >= iy0 - 16 && o.y <= iy1 + 16 &&
            m_world.diplomacy().hostile(p.player, o.owner)) {
            ships.emplace_back(o.x, o.y);
        }
    }
    // Beach tiles within 24 tiles of a target, scored by the danger around them.
    std::int32_t best = 10000;
    std::int32_t bx = 0;
    std::int32_t by = 0;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const Tile b = tile(island, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
            if (!b.valid() || !(b.kind == kind::kBeach || b.kind == kind::kBeachVariant)) {
                continue;
            }
            std::int32_t near = 1000;
            for (const auto& [tx, ty] : targets) {
                near =
                    std::min(near, sea_distance(static_cast<std::int32_t>(x) - tx, static_cast<std::int32_t>(y) - ty));
            }
            if (near > 24) {
                continue;
            }
            std::int32_t score = 1000 + near * 4;
            const std::int32_t wx = static_cast<std::int32_t>(isl.x + x);
            const std::int32_t wy = static_cast<std::int32_t>(isl.y + y);
            for (const auto& [ox, oy] : soldiers) {
                score += sea_distance(ox - wx, oy - wy) <= 16 ? 10 : 0;
            }
            for (const auto& [tx, ty] : towers) {
                score +=
                    sea_distance(tx - static_cast<std::int32_t>(x), ty - static_cast<std::int32_t>(y)) <= 10 ? 100 : 0;
            }
            for (const auto& [ox, oy] : ships) {
                score += sea_distance(ox - wx, oy - wy) <= 16 ? 30 : 0;
            }
            if (score < best) {
                best = score;
                bx = static_cast<std::int32_t>(x);
                by = static_cast<std::int32_t>(y);
            }
        }
    }
    if (best >= 10000) {
        mission_end(p, s, n);
        return;
    }
    ship_send(p, s, water_near(island, bx, by), 7, t->owner);
}

Ship* Engine::best_armed_ship(AiPlayer& p, const Target& ref, std::uint32_t max_same) {
    // 0x00424260: an armed ship with room, healthy and well armed.
    (void)max_same;
    for (const ShipNote& n : p.ships) {
        const Ship* s = m_world.ship(n.id);
        if (s != nullptr && (n.mission == 1 || n.mission == 2 || n.mission == 5) && s->order == ref) {
            return nullptr;
        }
    }
    Ship* best = nullptr;
    std::uint32_t best_score = 8;
    for (Ship* s : own_ships(p)) {
        const ShipNote& n = note(p, s->id);
        if (s->id == p.settle.ship || s->held || n.mission != 0 || n.supply || s->cannons == 0) {
            continue;
        }
        if (free_holds(*s) + soldiers_aboard(*s) < 3) {
            continue;
        }
        const std::uint32_t score = health128(*s) / 16 + 2U * s->cannons;
        if (score > best_score) {
            best = s;
            best_score = score;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------
// Looking for a target (0x004298F0, after every step)

bool Engine::defenceless(std::uint16_t island, std::uint8_t city) {
    // The same as city_defenceless (0x0047F1B0), whose scan of the island answers for all its cities
    // as long as its tiles stay the same.
    if (island >= m_world.islands().size()) {
        return false;
    }
    if (m_defences.size() < m_world.islands().size()) {
        m_defences.resize(m_world.islands().size());
    }
    Defences& d = m_defences[island];
    const std::uint64_t revision = island_revision(island);
    if (!d.valid || d.revision != revision) {
        d.valid = true;
        d.revision = revision;
        d.defended.fill(false);
        const IslandMap& map = island_of(island).map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const MapCell& c = map.top(x, y);
                if (c.empty() || d.defended[c.city]) {
                    continue;
                }
                const assets::BuildingType* b = m_buildings.by_sprite(c.sprite);
                if (b != nullptr &&
                    (((b->prod_kind == prod::kMarket || b->prod_kind == prod::kKontor) && !b->destroyed) ||
                     b->prod_kind == prod::kTower)) {
                    d.defended[c.city] = true;
                }
            }
        }
    }
    return !d.defended[city];
}

bool Engine::attack_ok(AiPlayer& p, const City& t) {
    // 0x00429810. The original's conditions, the cheap ones first: none has a side effect, so the
    // answer is the same, but most targets fail before the island or the world is walked (TB-9).
    for (const ShipNote& n : p.ships) {
        if (n.mission >= 5 && n.mission <= 9) {
            const City* c = city(static_cast<std::uint16_t>(n.param >> 8), static_cast<std::uint8_t>(n.param & 0xFF));
            if ((n.mission <= 6 && c != nullptr && c->owner == t.owner) ||
                (n.mission >= 7 && (n.param & 0xFF) == t.owner)) {
                return false;
            }
        }
    }
    const City* home = home_city(p);
    if (home == nullptr || soldiers_on_island(p.player, t.island) >= 10) {
        return false;
    }
    if (building_count(t) <= 4 || defenceless(t.island, t.index)) {
        return false;
    }
    return soldiers_near(p.player, *home, 8, 4).size() >= 4;
}

void Engine::start_attack(AiPlayer& p, const City& t) {
    // 0x00429770
    if (m_world.diplomacy().relation(p.player, t.owner).peace == Treaty::Signed) {
        return;
    }
    City* home = home_city(p);
    if (home == nullptr) {
        return;
    }
    const Target ref = Target::city_of(home->island, home->index);
    Ship* ship = best_armed_ship(p, ref, 1);
    if (ship == nullptr) {
        return;
    }
    log(p, "attack on city %u/%u of player %u with ship %u", t.island, t.index, t.owner, ship->id);
    ship_send(p, *ship, ref, 5, city_param(t));
    note(p, ship->id).counter = 80;
}

void Engine::raid_outpost(AiPlayer& p, const City& t) {
    // 0x004296B0 / 0x00429640: a warship shells the Kontor of a tiny outpost.
    if (m_world.diplomacy().relation(p.player, t.owner).peace == Treaty::Signed || !t.warehouse_x || !t.warehouse_y) {
        return;
    }
    for (const ShipNote& n : p.ships) {
        if (n.mission == 3 && n.param == city_param(t)) {
            return;
        }
    }
    Ship* best = nullptr;
    std::uint32_t best_score = 8;
    for (Ship* s : own_ships(p)) {
        const ShipNote& n = note(p, s->id);
        if (s->id == p.settle.ship || s->held || n.mission != 0 || s->cannons == 0) {
            continue;
        }
        const std::uint32_t score = health128(*s) / 16 + 2U * s->cannons;
        if (score > best_score) {
            best = s;
            best_score = score;
        }
    }
    if (best != nullptr) {
        ship_send(p, *best, Target::building(t.island, *t.warehouse_x, *t.warehouse_y), 3, city_param(t));
    }
}

void Engine::target_scan(AiPlayer& p) {
    const auto& cities = m_world.cities();
    if (cities.empty()) {
        return;
    }
    p.target_cursor = static_cast<std::uint32_t>((p.target_cursor + 1) % cities.size());
    const City& t = cities[p.target_cursor];
    const Player* o = m_world.player(t.owner);
    if (o == nullptr || t.owner == p.player) {
        return;
    }
    const bool at_peace = m_world.diplomacy().relation(p.player, t.owner).peace == Treaty::Signed;
    bool ok = false;
    if (o->role == PlayerRole::Human) {
        const City* own = nullptr;
        for (const City& c : cities) {
            if (c.owner == p.player && c.island == t.island) {
                own = &c;
            }
        }
        if (own != nullptr && island_size(t.island) < 4) {
            if (p.cities > 2 && own->inhabitants.size() > 0 && own->inhabitants[0] < 4) {
                return;
            }
            if (market_count(t) < 2 && building_count(t) < 3 && soldiers_on_island(t.owner, t.island) == 0) {
                raid_outpost(p, t);
                return;
            }
        } else if (at_peace) {
            return;
        }
        ok = attack_ok(p, t);
    } else if (m_world.behaviour(*o) == FactionBehaviour::Natives && !at_peace) {
        ok = attack_ok(p, t);
    }
    if (ok) {
        start_attack(p, t);
    }
}

// ---------------------------------------------------------------------------------------------
// The soldiers on one island per tick (0x0042B430)

std::vector<PlayerId> Engine::island_enemies(const AiPlayer& a, std::uint16_t island) const {
    // Hostile humans or natives here: soldiers of theirs or a city.
    std::vector<PlayerId> enemies;
    for (std::size_t b = 0; b < m_world.players().size(); ++b) {
        const Player& pl = m_world.players()[b];
        const auto pb = static_cast<PlayerId>(b);
        if ((pl.role != PlayerRole::Human && m_world.behaviour(pl) != FactionBehaviour::Natives) ||
            m_world.diplomacy().relation(a.player, pb).peace == Treaty::Signed) {
            continue;
        }
        bool present = soldiers_on_island(pb, island) != 0;
        for (const City& c : m_world.cities()) {
            present = present || (c.owner == pb && c.island == island);
        }
        if (present) {
            enemies.push_back(pb);
        }
    }
    return enemies;
}

void Engine::peacetime_orders(AiPlayer& a, std::uint16_t island) {
    // Peacetime: soldiers without a city here are disbanded, the others gather at the
    // Kontor or go to the doctor.
    const City* own = nullptr;
    for (const City& c : m_world.cities()) {
        if (c.owner == a.player && c.island == island) {
            own = &c;
        }
    }
    if (own == nullptr) {
        for (const Soldier& s : m_world.soldiers()) {
            if (s.owner == a.player && s.island == island && !s.away()) {
                SoldierCommand cmd;
                cmd.player = a.player;
                cmd.soldier = s.id;
                cmd.op = SoldierCommand::Op::Disband;
                execute(cmd);
                break;
            }
        }
        return;
    }
    if (!own->warehouse_x || !own->warehouse_y) {
        return;
    }
    const CityPlan& r = plan_of(a, own->island, own->index);
    const WorldIsland& isl = island_of(island);
    const Target kontor = Target::spot(2 * (isl.x + *own->warehouse_x) + 1, 2 * (isl.y + *own->warehouse_y) + 1);
    SoldierOrder to_kontor;
    to_kontor.player = a.player;
    to_kontor.target = kontor;
    for (const Soldier& s : m_world.soldiers()) {
        if (s.owner != a.player || s.island != island || s.away() || s.motion.phase == SoldierMotion::Phase::Run) {
            continue;
        }
        const assets::FigureType* ft = m_sim.m_figures->get(s.figure);
        const std::uint32_t max = ft != nullptr && ft->combat.max_health != 0 ? ft->combat.max_health : 1;
        if (r.doctor && s.health != 0 && s.health * 128U / max < 96U) {
            SoldierOrder heal;
            heal.player = a.player;
            heal.soldiers.push_back(s.id);
            heal.target = Target::building(island, r.doctor->first, r.doctor->second);
            execute(heal);
            continue;
        }
        if (s.target.empty() || s.target.kind == Target::Kind::Position) {
            to_kontor.soldiers.push_back(s.id);
        }
    }
    if (!to_kontor.soldiers.empty()) {
        execute(to_kontor);
    }
}

bool Engine::attack_orders(AiPlayer& a, std::uint16_t island, const std::vector<PlayerId>& enemies) {
    bool ordered = false;
    // A hostile city: the group with the most soldiers goes for its markets, its Kontor,
    // its houses. Without one: the soldiers meet the largest enemy group with a quarter more.
    std::vector<std::uint32_t> mine;
    for (const Soldier& s : m_world.soldiers()) {
        if (s.owner == a.player && s.island == island && !s.away() &&
            (s.target.empty() || s.target.kind == Target::Kind::Position || s.target.kind == Target::Kind::Spot)) {
            mine.push_back(s.id);
        }
    }
    if (mine.empty()) {
        return false;
    }
    const City* hostile = nullptr;
    std::vector<std::pair<std::int32_t, std::int32_t>> goals;
    for (const City& c : m_world.cities()) {
        if (c.island != island || std::find(enemies.begin(), enemies.end(), c.owner) == enemies.end()) {
            continue;
        }
        goals.clear();
        const IslandMap& map = island_of(island).map;
        for (const Production& pr : m_world.productions()) {
            if (!pr.used || pr.island != island || !map.contains(pr.x, pr.y)) {
                continue;
            }
            const Tile t = tile(island, pr.x, pr.y);
            if (t.valid() && t.city == c.index && (t.prod == prod::kMarket || t.prod == prod::kKontor) &&
                !t.type->destroyed) {
                goals.emplace_back(pr.x, pr.y);
            }
        }
        for (const House& h : m_world.houses()) {
            if (h.island == island && h.city == c.index && goals.size() < 16) {
                goals.emplace_back(h.x, h.y);
            }
        }
        if (classic()) {
            // The original takes the first hostile city on the island, even one with nothing left
            // to attack (a destroyed Kontor), and its soldiers stand there for good.
            hostile = &c;
            break;
        }
        // The original sends them to the markets even behind a wall, where they give up and get
        // the same order again: they stand in front of the wall for good.
        reachable_goals(a, island, c, mine, goals);
        if (!goals.empty()) {
            hostile = &c;  // Open1602: the first hostile city with something to attack
            break;
        }
    }
    if (hostile != nullptr) {
        std::size_t next = 0;
        for (std::size_t g = 0; g < goals.size() && next < mine.size(); ++g) {
            const std::size_t count = g == 0 ? 4 : 2;
            SoldierOrder order;
            order.player = a.player;
            order.attack = true;
            order.target = Target::building(island, static_cast<std::uint32_t>(goals[g].first),
                                            static_cast<std::uint32_t>(goals[g].second));
            for (std::size_t k = 0; k < count && next < mine.size(); ++k) {
                order.soldiers.push_back(mine[next++]);
            }
            if (execute(order).accepted) {
                ordered = true;
            }
        }
    } else {
        // The enemy soldier nearest to the middle of ours.
        const Soldier* enemy = nullptr;
        std::int64_t cx = 0;
        std::int64_t cy = 0;
        for (const std::uint32_t id : mine) {
            const Soldier* s = m_world.soldier(id);
            cx += s->pos_x;
            cy += s->pos_y;
        }
        cx /= static_cast<std::int64_t>(mine.size());
        cy /= static_cast<std::int64_t>(mine.size());
        std::int64_t best = -1;
        std::uint32_t enemy_count = 0;
        for (const Soldier& o : m_world.soldiers()) {
            if (o.island != island || o.away() || std::find(enemies.begin(), enemies.end(), o.owner) == enemies.end()) {
                continue;
            }
            ++enemy_count;
            const std::int64_t d = std::abs(o.pos_x - cx) + std::abs(o.pos_y - cy);
            if (best < 0 || d < best) {
                best = d;
                enemy = &o;
            }
        }
        if (enemy != nullptr) {
            SoldierOrder order;
            order.player = a.player;
            order.attack = true;
            order.target = Target::soldier(enemy->id);
            const std::uint32_t need = enemy_count + enemy_count / 4;
            for (std::size_t k = 0; k < mine.size() && k < need; ++k) {
                order.soldiers.push_back(mine[k]);
            }
            if (execute(order).accepted) {
                ordered = true;
            }
        }
    }
    return ordered;
}

std::vector<std::uint8_t> Engine::land_reach(PlayerId owner, std::uint16_t island,
                                             const std::vector<std::uint32_t>& soldiers) const {
    const WorldIsland& isl = island_of(island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    std::vector<std::uint8_t> reach(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    const LandMap& land = m_sim.m_combat.land();
    const auto at = [w](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    std::vector<std::pair<std::int32_t, std::int32_t>> open;
    for (const std::uint32_t id : soldiers) {
        const Soldier* s = m_world.soldier(id);
        if (s == nullptr) {
            continue;
        }
        const std::int32_t x = s->cell_x() / 2 - static_cast<std::int32_t>(isl.x);
        const std::int32_t y = s->cell_y() / 2 - static_cast<std::int32_t>(isl.y);
        if (x >= 0 && y >= 0 && x < w && y < h && reach[at(x, y)] == 0) {
            reach[at(x, y)] = 1;  // where a soldier stands, whatever lies there
            open.emplace_back(x, y);
        }
    }
    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> kSteps = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    while (!open.empty()) {
        const auto [x, y] = open.back();
        open.pop_back();
        for (const auto& [dx, dy] : kSteps) {
            const std::int32_t nx = x + dx;
            const std::int32_t ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || reach[at(nx, ny)] != 0 ||
                land.tile(static_cast<std::int32_t>(isl.x) + nx, static_cast<std::int32_t>(isl.y) + ny, 0, nullptr,
                          owner) != LandMap::Cell::Walk) {
                continue;
            }
            reach[at(nx, ny)] = 1;
            open.emplace_back(nx, ny);
        }
    }
    return reach;
}

void Engine::reachable_goals(const AiPlayer& a, std::uint16_t island, const City& hostile,
                             const std::vector<std::uint32_t>& soldiers,
                             std::vector<std::pair<std::int32_t, std::int32_t>>& goals) const {
    if (goals.empty()) {
        return;
    }
    const std::vector<std::uint8_t> reach = land_reach(a.player, island, soldiers);
    const IslandMap& map = island_of(island).map;
    const auto w = static_cast<std::int32_t>(map.width());
    const auto h = static_cast<std::int32_t>(map.height());
    // The buildings with a tile next to one the soldiers reach, by their origin.
    struct Near {
        std::int32_t x = 0;
        std::int32_t y = 0;
        bool defence = false;  ///< a wall, gate or tower
    };
    std::vector<Near> near;
    const auto at = [w](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    std::vector<std::uint8_t> listed(reach.size(), 0);  // by the building's origin
    const auto known = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < w && y < h && listed[at(x, y)] != 0;
    };
    const auto reached = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < w && y < h && reach[at(x, y)] != 0;
    };
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            const Tile t = tile(island, x, y);
            if (!t.valid() || t.city != hostile.index || t.owner != hostile.owner || !destroyable(*t.type, false) ||
                known(t.ox, t.oy) ||
                !(reached(x - 1, y) || reached(x + 1, y) || reached(x, y - 1) || reached(x, y + 1))) {
                continue;
            }
            const bool defence = t.kind == kind::kWall || t.kind == kind::kWallBeach || t.kind == kind::kGate ||
                                 t.kind == kind::kTower || t.kind == kind::kTowerBeach;
            near.push_back({t.ox, t.oy, defence});
            if (t.ox >= 0 && t.oy >= 0 && t.ox < w && t.oy < h) {
                listed[at(t.ox, t.oy)] = 1;
            }
        }
    }
    const auto [fx, fy] = goals.front();
    std::vector<std::pair<std::int32_t, std::int32_t>> kept;
    for (const auto& g : goals) {
        if (known(g.first, g.second)) {
            kept.push_back(g);
        }
    }
    if (kept.empty()) {
        // A way in: what stands between them and the city, nearest to its first goal first.
        std::stable_sort(near.begin(), near.end(), [fx, fy](const Near& l, const Near& r) {
            if (l.defence != r.defence) {
                return l.defence;
            }
            return std::abs(l.x - fx) + std::abs(l.y - fy) < std::abs(r.x - fx) + std::abs(r.y - fy);
        });
        for (std::size_t k = 0; k < near.size() && k < 4; ++k) {
            kept.emplace_back(near[k].x, near[k].y);
        }
    }
    if (!kept.empty()) {
        goals = std::move(kept);
    }
}

void Engine::island_controller() {
    const auto n = static_cast<std::uint32_t>(m_world.islands().size());
    if (n == 0 || m_state.islands.size() != n) {
        return;
    }
    m_state.island_cursor = (m_state.island_cursor + 1) % n;
    const auto island = static_cast<std::uint16_t>(m_state.island_cursor);
    IslandWatch& w = m_state.islands[island];
    if (now_ms() < w.next_ms) {
        return;
    }
    bool ordered = false;
    bool conflict = false;
    for (AiPlayer& a : m_state.players) {
        if (a.pirates || soldiers_on_island(a.player, island) == 0) {
            continue;
        }
        const Player* pa = m_world.player(a.player);
        if (pa == nullptr || pa->role != PlayerRole::Computer) {
            continue;
        }
        const std::vector<PlayerId> enemies = island_enemies(a, island);
        if (enemies.empty()) {
            peacetime_orders(a, island);
            continue;
        }
        conflict = true;
        ordered = attack_orders(a, island, enemies) || ordered;
    }
    w.next_ms = now_ms() + (conflict && ordered ? 0 : conflict ? 3'000 : 8'000);
}

// ---------------------------------------------------------------------------------------------
// Diplomacy (0x00428B50, every 30 s or after a change)

void Engine::diplomacy(AiPlayer& p) {
    if (p.cities < 1) {
        return;
    }
    Diplomacy& d = m_world.diplomacy();
    if (p.previous_mood.size() < m_world.players().size()) {
        p.previous_mood.resize(m_world.players().size(), 0);
    }
    for (std::size_t bi = 0; bi < m_world.players().size(); ++bi) {
        const auto b = static_cast<PlayerId>(bi);
        if (b == p.player) {
            continue;
        }
        const PlayerRole role = m_world.players()[bi].role;
        const Relation& rel = d.relation(p.player, b);
        const auto order = [&](DiplomacyOrder::Kind kind, Treaty value) {
            DiplomacyOrder o;
            o.player = p.player;
            o.partner = b;
            o.kind = kind;
            o.value = value;
            execute(o);
        };
        if (role == PlayerRole::Computer) {
            // Computers always trade with each other; their peace is never touched.
            if (rel.trade == Treaty::None) {
                order(DiplomacyOrder::Kind::Trade, Treaty::Offered);
            } else if (rel.trade == Treaty::Received) {
                order(DiplomacyOrder::Kind::Trade, Treaty::Signed);
            }
            continue;
        }
        if (role != PlayerRole::Human) {
            continue;
        }
        const std::int32_t m = d.mood(m_world, p.player, b, 0, now_ms());
        const std::int32_t prev = p.previous_mood[bi];
        switch (rel.peace) {
            case Treaty::None:
                if (m >= prev && m > 20) {
                    order(DiplomacyOrder::Kind::Peace, Treaty::Offered);
                }
                break;
            case Treaty::Offered:
            case Treaty::Signed:
                if (m < -90 || prev - m > 60) {
                    if (stronger(p.player, b)) {
                        log(p, "declares war on player %u (mood %d)", b, m);
                        order(DiplomacyOrder::Kind::Peace, Treaty::None);
                        subsidy(p, 3);
                    } else {
                        PlayerId pirates = kNoPlayer;
                        for (std::size_t i = 0; i < m_world.players().size(); ++i) {
                            if (m_world.behaviour(m_world.players()[i]) == FactionBehaviour::Pirates) {
                                pirates = static_cast<PlayerId>(i);
                            }
                        }
                        Player& pl = player(p);
                        if (pirates != kNoPlayer) {
                            const std::uint32_t price = pirate_bribe_price(m_world, pirates, p.player, b);
                            if (static_cast<std::int64_t>(price) < std::int64_t{pl.money} - 9000) {
                                d.add_event(pirates, p.player, DiplomacyEventType::BribedBy, now_ms());
                                d.add_event(pirates, b, DiplomacyEventType::BribedAgainst, now_ms());
                                pl.money = money_plus(pl.money, -std::int64_t{price});
                            }
                        }
                    }
                }
                break;
            case Treaty::Received:
                if (m > -20) {
                    order(DiplomacyOrder::Kind::Peace, Treaty::Signed);
                    // Ships on missions against b come home (0x00423830).
                    for (ShipNote& n : p.ships) {
                        if (n.mission == 0) {
                            continue;
                        }
                        const City* c =
                            city(static_cast<std::uint16_t>(n.param >> 8), static_cast<std::uint8_t>(n.param & 0xFF));
                        const bool against = (n.mission >= 7 && (n.param & 0xFF) == b) ||
                                             (n.mission != 2 && n.mission < 7 && c != nullptr && c->owner == b);
                        if (Ship* s = m_world.ship(n.id); against && s != nullptr) {
                            mission_end(p, *s, n);
                        }
                    }
                }
                break;
        }
        if (d.relation(p.player, b).peace == Treaty::Signed) {
            const Treaty trade = d.relation(p.player, b).trade;
            if (trade == Treaty::None) {
                std::uint32_t largest = 0;
                for (const City& c : m_world.cities()) {
                    if (c.owner == b) {
                        largest = std::max(largest, population(c));
                    }
                }
                const std::uint32_t roll = (rng().next() & 63U) + 40U;
                if (m >= prev && m > -10 && largest > roll) {
                    order(DiplomacyOrder::Kind::Trade, Treaty::Offered);
                }
            } else if (trade == Treaty::Offered || trade == Treaty::Signed) {
                if (m <= -56) {
                    order(DiplomacyOrder::Kind::Trade, Treaty::None);
                    continue;  // the mood is not remembered after this
                }
            } else if (trade == Treaty::Received && m > -21) {
                order(DiplomacyOrder::Kind::Trade, Treaty::Signed);
                continue;
            }
        }
        p.previous_mood[bi] = m;
    }
}

// ---------------------------------------------------------------------------------------------
// The pirates' nest (0x00429FD0, every 10 s; task 5 = 0x00429DF0)

void Engine::pirate_step(AiPlayer& p) {
    if (!pirates_enabled(m_world, m_sim.rules())) {
        return;  // no pirates
    }
    // Only once some player has grown.
    if (pirate_max_ships(m_world) <= 1) {
        return;
    }
    const std::vector<Ship*> ships = own_ships(p);
    if (ships.empty()) {
        return;
    }
    // A nest, and with Rules::pirate_nests one for every four players (the original founds one).
    if (own_city_count(p.player) < pirate_nests(m_world, m_sim.rules())) {
        if (!p.settle.active && p.stack.empty() && now_ms() - p.settle.last_ms > 36'000) {
            free_colony_cargo(p, *ships.front(), false);
            p.settle.ship = ships.front()->id;
            p.settle.need = 150;
            p.settle.floor = 0;
            p.settle.active = true;
            p.settle.pass = 1;
            pick_island(p);
        }
    }
    run_task(p);
}

void Engine::pirate_develop(AiPlayer& p) {
    // Pirate task 5 (0x00429DF0): every 42 s the nest offers its most valuable goods, and only then
    // the pirates build: houses, a hunter, a fisher and four towers.
    pop(p);
    if (now_ms() - p.refresh_ms <= kNestOfferMs) {
        return;
    }
    City* c = current_city(p);
    if (c == nullptr) {
        return;
    }
    pirate_nest_offer(m_world, m_buildings, *c);
    p.refresh_ms = now_ms();
    const CityPlan* r = current_plan(p);
    if (r == nullptr || r->slots[kSlotPirateTower].count >= 4) {
        return;
    }
    const auto house = [&](std::uint32_t building, std::uint32_t n) {
        if (r->slots[kSlotPirateHouses].count >= n || r->slots[kSlotPirateHouses].attempts >= 2) {
            return false;
        }
        const assets::BuildingType* b = by_id(building);
        if (b == nullptr) {
            return false;
        }
        if (push(p, kPirateHouses, false)) {
            start_search(p, *b, kSlotPirateHouses, false);
        }
        return true;
    };
    const std::uint32_t first = (rng().next() & 1U) != 0 ? id::kPirateHouse2 : id::kPirateHouse1;
    if (house(first, 5) || house(id::kPirateHouse3, 6) || need(p, kSlotHunter, 1) || need(p, kSlotFisher, 1)) {
        return;
    }
    need_tower(p, 2, 4);
}

}  // namespace o1602::sim::ai
