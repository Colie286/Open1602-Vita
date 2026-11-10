// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/combat.hpp"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <tuple>
#include <utility>

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/sea_paths.hpp"

namespace o1602::sim {

namespace {

constexpr std::int32_t kEighth = kPosUnit / 8;  // 1/8 tile in position units

/// The island a trade cart drives on.
std::uint16_t cart_island(const World& world, const Ship& cart) {
    if (!world.contains(cart.x, cart.y)) {
        return 0xFFFF;
    }
    const std::uint16_t island = world.cell(cart.x, cart.y).island;
    return island == World::kSea ? std::uint16_t{0xFFFF} : island;
}

struct Knot {
    std::int32_t x;
    std::int32_t v;
};

/// Byte tables of the scorer (Table_InterpByte 0x00403370, built in 0x00442A90): linear between the
/// knots, the last value beyond them, the first below.
std::int32_t table(std::initializer_list<Knot> knots, std::int32_t x) noexcept {
    const Knot* prev = nullptr;
    for (const Knot& k : knots) {
        if (x <= k.x) {
            return prev == nullptr ? k.v : prev->v + (k.v - prev->v) * (x - prev->x) / (k.x - prev->x);
        }
        prev = &k;
    }
    return prev != nullptr ? prev->v : 0;
}

std::int32_t t1(std::int32_t x) noexcept {  // remaining health after my shot: prefer kills
    return x < 0 ? 30 : table({{0, 30}, {25, 17}, {64, 9}, {96, 5}, {128, 0}}, x);
}
std::int32_t t2(std::int32_t x) noexcept {  // my damage relative to its size
    return table({{0, 0}, {25, 8}, {64, 20}, {96, 50}, {128, 60}, {192, 40}, {256, 20}}, std::min(x, 256));
}
std::int32_t t3(std::int32_t x) noexcept {  // its threat to me
    return table({{0, 0}, {6, 12}, {12, 28}, {38, 45}, {64, 60}, {96, 50}, {128, 40}, {256, 30}}, std::min(x, 256));
}
std::int32_t t5(std::int32_t x) noexcept {  // distance beyond my range, 1/8 tile
    return table({{0, 40}, {24, 20}, {48, 10}, {96, 0}}, x);
}

std::uint64_t key_of(const Target& t) noexcept {
    const auto kind = static_cast<std::uint64_t>(t.kind) << 56U;
    if (t.kind == Target::Kind::Building) {
        return kind | std::uint64_t{t.island} << 32U | std::uint64_t{t.x & 0xFFFFU} << 16U | (t.y & 0xFFFFU);
    }
    return kind | t.ship;
}

}  // namespace

bool destroyable(const assets::BuildingType& building, bool ctrl) noexcept {
    if (building.destroyed) {
        return false;  // a destroyed Kontor or market is no target any more
    }
    switch (building.kind) {
        case kind::kGate:
        case kind::kWall:
        case kind::kWallBeach:
        case kind::kTower:
        case kind::kTowerBeach:
        case kind::kBuilding:
        case kind::kBeachHouse:
        case kind::kMine:
        case kind::kHq:
        case kind::kHarbour:
        case kind::kMill:
            return true;
        case kind::kRoad:
        case kind::kForest:
        case kind::kBridge:
        case kind::kPier:
            return ctrl;
        default:
            return false;
    }
}

std::uint32_t Combat::scaled_damage(std::uint32_t hit, std::uint32_t health, std::uint32_t max_health) noexcept {
    const std::uint32_t h = std::min(health, max_health);
    return ((h * 85 / std::max<std::uint32_t>(1, max_health) + 43) * hit) >> 7U;
}

std::uint32_t Combat::cannon_factor(std::uint8_t cannons) noexcept {
    if (cannons == 0) {
        return 0;
    }
    return cannons <= 2 ? 1U : cannons <= 10 ? 2U : cannons <= 16 ? 4U : 6U;
}

Combat::Combat(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures)
    : m_world(&world), m_buildings(&buildings), m_figures(&figures) {
    m_land.rebuild(world, buildings);
}

void Combat::island_changed(std::uint16_t island) {
    m_land.update_island(*m_world, *m_buildings, island);
}

std::optional<Fighter> Combat::fighter(const Target& unit) const {
    Fighter f;
    f.unit = unit;
    switch (unit.kind) {
        case Target::Kind::Soldier: {
            const Soldier* s = m_world->soldier(unit.ship);
            if (s == nullptr || s->away()) {
                return std::nullopt;
            }
            const assets::FigureType* type = m_figures->get(s->figure);
            if (type == nullptr) {
                return std::nullopt;
            }
            f.owner = s->owner;
            f.island = s->island;
            f.x8 = s->pos_x / kEighth;
            f.y8 = s->pos_y / kEighth;
            f.cells = {s->cell_x(), s->cell_y(), s->cell_x(), s->cell_y()};
            f.max_health = std::max<std::uint32_t>(1, type->combat.max_health);
            f.health = s->health != 0 ? s->health : f.max_health;
            f.range8 = static_cast<std::int32_t>(type->combat.range8);
            f.damage = scaled_damage(type->combat.hit, f.health, f.max_health);
            f.threat = f.damage * 1000 / std::max<std::uint32_t>(1, type->work_time_ms);
            f.cannon = type->kind == figtype::kGunner;
            return f;
        }
        case Target::Kind::Ship: {
            const Ship* s = m_world->ship(unit.ship);
            if (s == nullptr || s->sinking) {
                return std::nullopt;
            }
            const assets::FigureType* type = m_figures->get(s->figure);
            if (type == nullptr) {
                return std::nullopt;
            }
            f.owner = s->owner;
            f.island = s->cart() ? cart_island(*m_world, *s) : s->near_island;
            f.x8 = s->pos_x / kEighth;
            f.y8 = s->pos_y / kEighth;
            f.cells = {2 * s->x, 2 * s->y, 2 * s->x + 1, 2 * s->y + 1};
            f.max_health = std::max<std::uint32_t>(1, type->combat.max_health);
            f.health = s->health != 0 ? std::min(s->health, f.max_health) : f.max_health;
            if (s->cart()) {
                return f;  // unarmed, and a figure on land
            }
            f.range8 = static_cast<std::int32_t>(type->combat.range8);
            f.damage = type->combat.hit * cannon_factor(s->cannons);
            f.threat = f.damage;  // per shot; soldiers and towers count per second (original)
            f.cannon = true;
            f.ship = true;
            return f;
        }
        case Target::Kind::Building: {
            // A tower by its building.
            for (const Tower& t : m_world->towers()) {
                if (t.island == unit.island && t.x == unit.x && t.y == unit.y) {
                    return tower_fighter(t);
                }
            }
            // Any other building: a target, not a shooter.
            if (unit.island >= m_world->islands().size()) {
                return std::nullopt;
            }
            const auto b = building_at(*m_world, *m_buildings, unit.island,
                                       {static_cast<std::int32_t>(unit.x), static_cast<std::int32_t>(unit.y)});
            // Gone, or only a ruin or terrain left: nothing to shoot at any more.
            if (!b || !destroyable(*b->type, true)) {
                return std::nullopt;
            }
            const WorldIsland& island = m_world->islands()[unit.island];
            const auto wx = static_cast<std::int32_t>(island.x) + b->origin.x;
            const auto wy = static_cast<std::int32_t>(island.y) + b->origin.y;
            const auto w = static_cast<std::int32_t>(b->width);
            const auto h = static_cast<std::int32_t>(b->height);
            f.unit = Target::building(unit.island, static_cast<std::uint32_t>(b->origin.x),
                                      static_cast<std::uint32_t>(b->origin.y));
            f.owner =
                island.map.top(static_cast<std::uint32_t>(b->origin.x), static_cast<std::uint32_t>(b->origin.y)).owner;
            f.island = unit.island;
            f.x8 = wx * 8 + w * 4;
            f.y8 = wy * 8 + h * 4;
            f.cells = {2 * wx, 2 * wy, 2 * (wx + w) - 1, 2 * (wy + h) - 1};
            f.max_health = std::max<std::uint32_t>(1, b->type->max_health);
            std::uint32_t damage = 0;
            for (const BuildingDamage& d : m_world->building_damage()) {
                if (d.island == unit.island && d.x == b->origin.x && d.y == b->origin.y) {
                    damage = d.damage;
                }
            }
            f.health = f.max_health > damage ? f.max_health - damage : 0;
            return f;
        }
        default:
            break;
    }
    return std::nullopt;
}

bool Combat::fellable(const Target& unit, PlayerId self) const {
    if (unit.kind != Target::Kind::Building || unit.island >= m_world->islands().size()) {
        return false;
    }
    const auto b = building_at(*m_world, *m_buildings, unit.island,
                               {static_cast<std::int32_t>(unit.x), static_cast<std::int32_t>(unit.y)});
    if (!b || b->type->kind != kind::kForest) {
        return false;
    }
    const IslandMap& map = m_world->islands()[unit.island].map;
    return map.top(static_cast<std::uint32_t>(b->origin.x), static_cast<std::uint32_t>(b->origin.y)).owner != self;
}

std::optional<Fighter> Combat::tower_fighter(const Tower& t) const {
    const assets::FigureType* type = m_figures->get(t.figure);
    if (type == nullptr || t.island >= m_world->islands().size()) {
        return std::nullopt;
    }
    Fighter f;
    f.unit = Target::building(t.island, t.x, t.y);
    const WorldIsland& island = m_world->islands()[t.island];
    const auto wx = static_cast<std::int32_t>(island.x + t.x);
    const auto wy = static_cast<std::int32_t>(island.y + t.y);
    const assets::BuildingType* b = m_land.building(wx, wy);
    const auto w = static_cast<std::int32_t>(b != nullptr ? b->size_x : 1);
    const auto h = static_cast<std::int32_t>(b != nullptr ? b->size_y : 1);
    f.owner = t.owner;
    f.island = t.island;
    f.x8 = wx * 8 + w * 4;
    f.y8 = wy * 8 + h * 4;
    f.cells = {2 * wx, 2 * wy, 2 * (wx + w) - 1, 2 * (wy + h) - 1};
    f.max_health = std::max<std::uint32_t>(1, type->combat.max_health);
    f.health = f.max_health;  // the tower figure never weakens (original)
    f.range8 = static_cast<std::int32_t>(type->combat.range8);
    f.damage = type->combat.hit;
    f.threat = f.damage * 1000 / std::max<std::uint32_t>(1, type->work_time_ms);
    f.cannon = true;
    return f;
}

std::size_t Combat::list_index(const Target& unit) const {
    const std::uint64_t key = key_of(unit);
    const auto it =
        std::lower_bound(m_index.begin(), m_index.end(), key,
                         [](const std::pair<std::uint64_t, std::uint32_t>& e, std::uint64_t k) { return e.first < k; });
    return it != m_index.end() && it->first == key ? it->second : m_entries.size();
}

std::span<const Neighbour> Combat::hostiles(const Target& unit) const {
    const std::size_t i = list_index(unit);
    if (i >= m_entries.size()) {
        return {};
    }
    return std::span<const Neighbour>(m_neighbours).subspan(m_first[i], m_first[i + 1] - m_first[i]);
}

void Combat::update() {
    m_entries.clear();
    for (const Ship& s : m_world->ships()) {
        if (s.sinking || s.owner == kNoPlayer) {
            continue;
        }
        if (s.cart()) {
            // A trade cart is a land figure of its island that soldiers and towers attack.
            m_entries.push_back({Target::other_ship(s.id), s.owner, cart_island(*m_world, s), s.pos_x / kEighth,
                                 s.pos_y / kEighth, false, true});
        } else {
            m_entries.push_back(
                {Target::other_ship(s.id), s.owner, s.near_island, s.pos_x / kEighth, s.pos_y / kEighth, true});
        }
    }
    const std::size_t ships = m_entries.size();
    for (const Soldier& s : m_world->soldiers()) {
        if (!s.away() && s.owner != kNoPlayer) {
            m_entries.push_back(
                {Target::soldier(s.id), s.owner, s.island, s.pos_x / kEighth, s.pos_y / kEighth, false});
        }
    }
    const std::size_t units = m_entries.size() - ships;
    for (const Tower& t : m_world->towers()) {
        if (t.owner == kNoPlayer || t.island >= m_world->islands().size()) {
            continue;
        }
        if (const auto f = tower_fighter(t)) {
            m_entries.push_back({f->unit, t.owner, t.island, f->x8, f->y8, false});
        }
    }
    const std::size_t n = m_entries.size();
    m_index.clear();
    m_neighbours.clear();

    // Who is hostile to whom, once per tick instead of per pair.
    const Diplomacy& d = m_world->diplomacy();
    const std::size_t players = m_world->players().size();
    m_hostile.assign(players * players, 0);
    for (std::size_t a = 0; a < players; ++a) {
        for (std::size_t b = 0; b < players; ++b) {
            m_hostile[a * players + b] = d.hostile(static_cast<PlayerId>(a), static_cast<PlayerId>(b)) ? 1 : 0;
        }
    }
    const auto hostile = [&](PlayerId a, PlayerId b) {
        return a < players && b < players && m_hostile[std::size_t{a} * players + b] != 0;
    };
    // A figure whose owner has no enemy among the figures sees nobody: no search.
    m_present.assign(players, 0);
    for (const Entry& e : m_entries) {
        if (e.owner < players) {
            m_present[e.owner] = 1;
        }
    }
    m_enemies.assign(players, 0);
    bool fight = false;
    for (std::size_t a = 0; a < players; ++a) {
        for (std::size_t b = 0; b < players; ++b) {
            if (m_present[b] != 0 && m_hostile[a * players + b] != 0) {
                m_enemies[a] = 1;
                fight = fight || m_present[a] != 0;
            }
        }
    }
    if (!fight) {
        // Nobody present has an enemy present (peace, TB-5): every list is empty, no sorting.
        m_entries.clear();
        m_first.assign(1, 0);
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        m_index.emplace_back(key_of(m_entries[i].unit), static_cast<std::uint32_t>(i));
    }
    // Ships, units and towers are each in the order of their ids as a rule: with the towers
    // (buildings) before the units, as the keys go, the index is sorted without sorting (TB-5).
    // Equal keys only come from one list, whose order the rotation keeps: the stable sort's result.
    std::rotate(m_index.begin() + static_cast<std::ptrdiff_t>(ships),
                m_index.begin() + static_cast<std::ptrdiff_t>(ships + units), m_index.end());
    const auto by_key = [](const auto& a, const auto& b) { return a.first < b.first; };
    if (!std::is_sorted(m_index.begin(), m_index.end(), by_key)) {
        std::stable_sort(m_index.begin(), m_index.end(), by_key);
    }

    // Buckets of kSight tiles: a figure sees only figures in its own and the eight around it.
    // Within a bucket the figures are grouped by owner, so that a figure looks only at the groups
    // of its enemies (review point 18: friends in a crowd cost nothing).
    constexpr std::int32_t kSight8 = kSight * 8;
    // Farthest distance a figure in sight can have (sea_distance of kSight8 in both axes).
    constexpr std::int32_t kMaxSeenDistance = kSight8 + kSight8 / 4;
    const auto bucket_of = [](std::int32_t v) { return v >= 0 ? v / kSight8 : -((-v + kSight8 - 1) / kSight8); };
    // Sorted by (bucket row, bucket column, owner, entry): as one 64-bit key each when the values
    // fit, which sorts several times faster than the records (TB-5).
    constexpr std::int32_t kBias = 1 << 15;
    constexpr std::size_t kMaxEntries = std::size_t{1} << 24U;
    m_bucket_keys.clear();
    bool packed = n < kMaxEntries;
    for (std::size_t i = 0; i < n && packed; ++i) {
        const Entry& e = m_entries[i];
        const std::int32_t bx = bucket_of(e.x8) + kBias;
        const std::int32_t by = bucket_of(e.y8) + kBias;
        packed = bx >= 0 && bx <= 0xFFFF && by >= 0 && by <= 0xFFFF;
        m_bucket_keys.push_back(std::uint64_t(std::uint32_t(by)) << 48U | std::uint64_t(std::uint32_t(bx)) << 32U |
                                std::uint64_t{e.owner} << 24U | i);
    }
    m_buckets.clear();
    if (packed) {
        std::sort(m_bucket_keys.begin(), m_bucket_keys.end());
        for (const std::uint64_t key : m_bucket_keys) {
            const auto i = static_cast<std::uint32_t>(key & 0xFFFFFFU);
            const Entry& e = m_entries[i];
            m_buckets.push_back({bucket_of(e.x8), bucket_of(e.y8), e.x8, e.y8, i, e.owner});
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            const Entry& e = m_entries[i];
            m_buckets.push_back({bucket_of(e.x8), bucket_of(e.y8), e.x8, e.y8, static_cast<std::uint32_t>(i), e.owner});
        }
        std::sort(m_buckets.begin(), m_buckets.end(), [](const Bucketed& a, const Bucketed& b) {
            return std::tie(a.by, a.bx, a.owner, a.entry) < std::tie(b.by, b.bx, b.owner, b.entry);
        });
    }
    // The buckets in use with their owner groups.
    m_cells.clear();
    m_groups.clear();
    for (std::size_t k = 0; k < m_buckets.size(); ++k) {
        const Bucketed& e = m_buckets[k];
        if (m_cells.empty() || m_cells.back().bx != e.bx || m_cells.back().by != e.by) {
            m_cells.push_back({e.bx, e.by, static_cast<std::uint32_t>(m_groups.size()), 0});
        }
        if (m_groups.empty() || m_cells.back().first_group == m_groups.size() || m_groups.back().owner != e.owner) {
            m_groups.push_back({e.owner, static_cast<std::uint32_t>(k), static_cast<std::uint32_t>(k)});
        }
        m_groups.back().end = static_cast<std::uint32_t>(k + 1);
        m_cells.back().end_group = static_cast<std::uint32_t>(m_groups.size());
    }
    const auto cell_at = [&](std::int32_t bx, std::int32_t by) -> const Cell* {
        const auto it = std::lower_bound(
            m_cells.begin(), m_cells.end(), std::pair{by, bx},
            [](const Cell& c, const std::pair<std::int32_t, std::int32_t>& k) { return std::pair{c.by, c.bx} < k; });
        return it != m_cells.end() && it->by == by && it->bx == bx ? &*it : nullptr;
    };

    // Fix S-114: each list holds the nearest hostile figures (ties by the order of the figures),
    // not the first 32 of a sweep in y order, which left out the nearest in a crowd.
    m_neighbours.clear();
    m_first.assign(n + 1, 0);
    for (std::size_t i = 0; i < n; ++i) {
        m_first[i] = static_cast<std::uint32_t>(m_neighbours.size());
        const Entry& a = m_entries[i];
        if (a.owner >= players || m_enemies[a.owner] == 0 || a.cart) {
            continue;  // a cart does not fight
        }
        m_seen.clear();
        const std::int32_t abx = bucket_of(a.x8);
        const std::int32_t aby = bucket_of(a.y8);
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                const Cell* cell = cell_at(abx + dx, aby + dy);
                if (cell == nullptr) {
                    continue;
                }
                for (std::uint32_t g = cell->first_group; g < cell->end_group; ++g) {
                    const Group& group = m_groups[g];
                    if (!hostile(a.owner, group.owner)) {
                        continue;
                    }
                    for (std::uint32_t k = group.begin; k < group.end; ++k) {
                        const Bucketed& b = m_buckets[k];
                        if (std::abs(b.x8 - a.x8) > kSight8 || std::abs(b.y8 - a.y8) > kSight8) {
                            continue;
                        }
                        // A ship sees land figures only near their island (rectangle + 5 tiles).
                        const Entry& e = m_entries[b.entry];
                        if ((a.ship != e.ship && a.island != e.island) || (a.ship && e.cart)) {
                            continue;  // ships leave trade carts alone
                        }
                        m_seen.push_back({sea_distance(a.x8 - b.x8, a.y8 - b.y8), b.entry});
                    }
                }
            }
        }
        const auto nearer = [](const Seen& x, const Seen& y) {
            return std::tie(x.distance8, x.entry) < std::tie(y.distance8, y.entry);
        };
        if (m_seen.size() > kMaxNeighbours) {
            // Only the nearest kMaxNeighbours stay: count the distances, find the one the list
            // ends at and drop everything farther before sorting (the same list as sorting all).
            m_distances.assign(kMaxSeenDistance + 1, 0);
            for (const Seen& v : m_seen) {
                ++m_distances[static_cast<std::size_t>(std::min(v.distance8, kMaxSeenDistance))];
            }
            std::int32_t last = 0;
            for (std::size_t count = 0; last <= kMaxSeenDistance; ++last) {
                count += m_distances[static_cast<std::size_t>(last)];
                if (count >= kMaxNeighbours) {
                    break;
                }
            }
            std::erase_if(m_seen, [last](const Seen& v) { return v.distance8 > last; });
        }
        const std::size_t keep = std::min(m_seen.size(), kMaxNeighbours);
        std::sort(m_seen.begin(), m_seen.end(), nearer);  // the order is total: no ties
        for (std::size_t k = 0; k < keep; ++k) {
            m_neighbours.push_back({m_entries[m_seen[k].entry].unit, m_seen[k].distance8});
        }
    }
    m_first[n] = static_cast<std::uint32_t>(m_neighbours.size());
}

std::span<const Candidate> Combat::rank(const Fighter& me, std::span<const Neighbour> list,
                                        std::int32_t max_distance8) const {
    std::vector<Candidate>& out = m_ranked;
    out.clear();
    for (const Neighbour& n : list) {
        if (n.distance8 > max_distance8) {
            continue;
        }
        const auto t = fighter(n.unit);
        if (!t) {
            continue;
        }
        const auto my_damage = static_cast<std::int32_t>(me.damage);
        const auto t_max = static_cast<std::int32_t>(t->max_health);
        const std::int32_t over = std::max(0, n.distance8 - me.range8);
        std::int32_t score = t1((static_cast<std::int32_t>(t->health) - my_damage) * 128 / t_max);
        score += t2(my_damage * 128 / t_max);
        if (n.distance8 <= t->range8) {
            score += t3(static_cast<std::int32_t>(t->threat * 128 / std::max<std::uint32_t>(1, me.max_health)));
        }
        if (t->range8 < me.range8) {
            score += 35;
        }
        score += t5(over);
        out.push_back({n.unit, n.distance8, score, over < 4});
    }
    // Stable, best first. Insertion sort: at most kMaxNeighbours entries, and unlike
    // std::stable_sort it needs no buffer.
    for (std::size_t i = 1; i < out.size(); ++i) {
        const Candidate c = out[i];
        std::size_t j = i;
        for (; j > 0 && out[j - 1].score < c.score; --j) {
            out[j] = out[j - 1];
        }
        out[j] = c;
    }
    return out;
}

void Combat::shoot(const Fighter& shooter, const Fighter& target, std::uint32_t delay_ms) {
    PendingHit h;
    h.due_ms = m_world->time_ms() + delay_ms;
    h.shooter = shooter.unit;
    h.attacker = shooter.owner;
    h.cannon = shooter.cannon;
    h.damage = shooter.damage;
    h.target = target.unit;
    h.from_x = shooter.x8 * kEighth;
    h.from_y = shooter.y8 * kEighth;
    m_world->pending_hits().push_back(h);
}

}  // namespace o1602::sim
