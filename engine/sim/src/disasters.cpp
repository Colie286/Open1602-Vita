// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/disasters.hpp"

#include <algorithm>
#include <array>

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::sim {

namespace {

namespace hs = house_service;

// HAUS_PRODTYP Kind values.

// The original's tables (0x0049AED8, 0x0049AF08, 0x0049AF18): one bit per random value.
constexpr std::array<std::array<std::uint8_t, 16>, 3> kPlagueTakes = {{
    {0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1},  // unprotected: 8 of 16
    {0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1},  // doctor or bath house: 5 of 16
    {0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0},  // both: 3 of 16
}};
// The original gives the 7-of-8 row to buildings with more bricks than wood and the 3-of-8 row
// to wooden ones (0x0049AF08); Open1602 swaps them, so wood burns more easily (S-049), unless the
// game brings the bug back (OriginalBugs::fire_risk).
constexpr std::array<std::array<std::uint8_t, 8>, 2> kFireTakes = {{
    {0, 0, 1, 0, 1, 0, 0, 1},  // 3 of 8: more bricks than wood (the original: wood)
    {1, 1, 1, 1, 0, 1, 1, 1},  // 7 of 8: wood (the original: more bricks than wood)
}};
constexpr std::array<std::uint8_t, 8> kRobberPastGallows = {0, 0, 1, 0, 0, 0, 0, 1};

constexpr std::uint32_t kSpreadTiles = 20;  // the area search collects at most 20 tiles

/// Chance of 128 that a plague house infects a neighbour, by how full it is (0x005A7758): linear
/// through (0, 0), (25, 25), (38, 51), (128, 64) over inhabitants * 128 / the largest house.
std::uint32_t plague_spread_chance(std::uint32_t index) noexcept {
    struct Knot {
        std::uint32_t x;
        std::uint32_t y;
    };
    static constexpr std::array<Knot, 4> kKnots = {{{0, 0}, {25, 25}, {38, 51}, {128, 64}}};
    index = std::min<std::uint32_t>(index, 128);
    for (std::size_t k = 1; k < kKnots.size(); ++k) {
        const Knot a = kKnots[k - 1];
        const Knot b = kKnots[k];
        if (index < b.x) {
            const std::uint32_t step = ((b.y - a.y) << 8U) / (b.x - a.x);
            return ((a.y << 8U) + (index - a.x) * step) >> 8U;
        }
    }
    return kKnots.back().y;
}

const assets::BuildingType* type_at(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                                    std::uint16_t x, std::uint16_t y) {
    const IslandMap& map = world.islands()[island].map;
    if (!map.contains(x, y)) {
        return nullptr;
    }
    const MapCell& c = map.top(x, y);
    return c.empty() ? nullptr : buildings.by_sprite(c.sprite);
}

std::uint8_t city_at(const World& world, std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    const IslandMap& map = world.islands()[island].map;
    return map.contains(x, y) ? map.top(x, y).city : MapCell::kNoCity;
}

Production* production_at(World& world, std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    const auto index = world.production_index(island, x, y);
    return index ? &world.productions()[*index] : nullptr;
}

/// HANDWERK .. MARKT have a production state.
bool working_kind(std::int32_t p) noexcept {
    return p >= prod::kWorkshop && p <= prod::kMarket;
}

/// Wooden buildings (Holz >= Ziegel) and the others take fire differently.
bool wooden(const assets::BuildingType& b) noexcept {
    return b.build_cost.wood >= b.build_cost.bricks;
}

/// Whether the building catches fire on the roll `r` (0..7).
bool takes_fire(const assets::BuildingType& b, std::uint32_t r, const Rules& rules) noexcept {
    const bool easily = wooden(b) != rules.bugs.fire_risk;
    return kFireTakes[easily ? 1U : 0U][r] != 0;
}

std::size_t protection(const House& h) noexcept {
    return ((h.services & hs::kKlinik) != 0 ? 1U : 0U) + ((h.services & hs::kBadehaus) != 0 ? 1U : 0U);
}

std::uint32_t max_inhabitants64(const assets::BuildingTable& buildings, std::uint8_t resident_class) {
    const auto& classes = buildings.resident_classes();
    return resident_class < classes.size() ? classes[resident_class].max_per_house * 64U : 64U;
}

/// Removes the entry at `origin` and clears the marks it left on the house or workshop.
std::optional<Disaster> take(World& world, std::uint16_t island, TilePos origin) {
    auto& list = world.disasters();
    const auto it = std::find_if(list.begin(), list.end(), [&](const Disaster& d) {
        return d.island == island && d.x == origin.x && d.y == origin.y;
    });
    if (it == list.end()) {
        return std::nullopt;
    }
    const Disaster d = *it;
    list.erase(it);
    const auto x = static_cast<std::uint16_t>(origin.x);
    const auto y = static_cast<std::uint16_t>(origin.y);
    if (House* h = world.house(island, x, y)) {
        h->services = static_cast<std::uint16_t>(h->services & ~hs::kDisasterMask);
    }
    if (Production* p = production_at(world, island, x, y)) {
        p->flags = static_cast<std::uint8_t>(p->flags & ~Production::kBurningFlag);
    }
    return d;
}

void emit(World& world, std::vector<DisasterEvent>* events, DisasterEvent::Kind kind, const Disaster& d,
          bool spread = false) {
    if (events == nullptr) {
        return;
    }
    DisasterEvent e;
    e.kind = kind;
    e.disaster = d.kind;
    e.island = d.island;
    e.x = d.x;
    e.y = d.y;
    e.city = city_at(world, d.island, d.x, d.y);
    if (const City* c = world.city(d.island, e.city)) {
        e.owner = c->owner;
    }
    e.spread = spread;
    events->push_back(e);
}

/// Tiles around a building the disaster can reach (0x004722F0, 0x00471FB0): a square window of
/// `radius` around its centre, searched outwards from the centre; `goal` tiles are collected (at
/// most 20, nearest first) and crossed, `open` tiles only crossed.
template <class Goal, class Open>
std::vector<TilePos> reach(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                           const BuildingAt& source, std::uint8_t radius, Goal&& goal, Open&& open) {
    std::vector<TilePos> tiles;
    const IslandMap& map = world.islands()[island].map;
    Catchment area;
    area.centre = centre_tile(source);
    area.radius = radius;
    area.parity_x = static_cast<std::uint8_t>((source.width - 1) & 1U);
    area.parity_y = static_cast<std::uint8_t>((source.height - 1) & 1U);
    PathFinder finder;
    finder.begin(map, buildings, area, WalkClass::Land, kUniformSpeed, false);
    const TilePos corner = finder.window_min();
    for (std::uint32_t j = 0; j < finder.window_height(); ++j) {
        for (std::uint32_t i = 0; i < finder.window_width(); ++i) {
            const TilePos t{corner.x + static_cast<std::int32_t>(i), corner.y + static_cast<std::int32_t>(j)};
            if (!map.contains(t.x, t.y)) {
                continue;
            }
            const MapCell& c = map.top(static_cast<std::uint32_t>(t.x), static_cast<std::uint32_t>(t.y));
            const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
            if (b != nullptr && goal(*b, c)) {
                finder.set(t, PathFinder::Cell::Goal, 32);
            } else if (b != nullptr && open(*b, c)) {
                finder.set(t, PathFinder::Cell::Walkable, 32);
            } else {
                finder.set(t, PathFinder::Cell::Blocked);
            }
        }
    }
    finder.search(area.centre, [&](TilePos t) {
        tiles.push_back(t);
        return tiles.size() >= kSpreadTiles ? PathFinder::Answer::Accept : PathFinder::Answer::Cross;
    });
    return tiles;
}

void spread_plague(World& world, const assets::BuildingTable& buildings, const Disaster& d,
                   std::vector<DisasterEvent>* events) {
    const House* h = world.house(d.island, d.x, d.y);
    if (h == nullptr) {
        return;
    }
    Random& random = world.random(RandomStream::Events);
    const auto& classes = buildings.resident_classes();
    const std::uint32_t largest =
        classes.empty() ? 64U : max_inhabitants64(buildings, static_cast<std::uint8_t>(classes.size() - 1));
    if (random.below(128) >= plague_spread_chance(h->inhabitants64 * 128U / std::max<std::uint32_t>(largest, 1))) {
        return;
    }
    const auto source = building_at(world, buildings, d.island, {d.x, d.y});
    if (!source) {
        return;
    }
    const std::uint8_t city = h->city;
    const std::vector<TilePos> tiles = reach(
        world, buildings, d.island, *source, static_cast<std::uint8_t>(world.balance().plague_radius),
        [&](const assets::BuildingType& b, const MapCell& c) { return b.prod_kind == prod::kHouse && c.city == city; },
        [&](const assets::BuildingType& b, const MapCell& c) { return walkable(b, c, WalkClass::Land); });
    if (tiles.empty()) {
        return;
    }
    const TilePos t = tiles[random.below(static_cast<std::uint32_t>(tiles.size()))];
    const auto target = building_at(world, buildings, d.island, t);
    if (!target || (target->origin.x == d.x && target->origin.y == d.y)) {
        return;
    }
    const auto tx = static_cast<std::uint16_t>(target->origin.x);
    const auto ty = static_cast<std::uint16_t>(target->origin.y);
    const House* other = world.house(d.island, tx, ty);
    if (other == nullptr || disaster_at(world, d.island, tx, ty) != nullptr ||
        other->inhabitants64 < max_inhabitants64(buildings, other->resident_class) / 2) {
        return;
    }
    if (kPlagueTakes[protection(*other)][random.below(16)] != 0) {
        start_disaster(world, buildings, d.island, target->origin, Disaster::Kind::Plague, kSpreadDuration, events,
                       true);
    }
}

/// Fire leaps over buildings, walls, gates, towers and forest and fields (0x00472930), whoever
/// owns them; roads, open ground and the sea stop it.
bool conducts_fire(const assets::BuildingType& b) noexcept {
    switch (b.kind) {
        case 3:   // TOR
        case 4:   // MAUER
        case 5:   // MAUERSTRAND
        case 6:   // TURM
        case 7:   // TURMSTRAND
        case 10:  // WALD: forest, fields, plantations
        case 14:  // GEBAEUDE
        case 36:  // HAFEN
        case 37:  // WMUEHLE
            return true;
        default:
            return false;
    }
}

void spread_fire(World& world, const assets::BuildingTable& buildings, const Rules& rules, const Disaster& d,
                 std::vector<DisasterEvent>* events) {
    Random& random = world.random(RandomStream::Events);
    if (random.below(128) >= world.balance().fire_spread) {  // of 128 per epoch
        return;
    }
    const auto source = building_at(world, buildings, d.island, {d.x, d.y});
    if (!source) {
        return;
    }
    const auto radius = static_cast<std::uint8_t>((source->type->size_x + source->type->size_y - 1) / 4 + 2);
    const std::vector<TilePos> tiles = reach(
        world, buildings, d.island, *source, radius,
        [](const assets::BuildingType& b, const MapCell&) { return conducts_fire(b); },
        [](const assets::BuildingType&, const MapCell&) { return false; });
    if (tiles.empty()) {
        return;
    }
    const TilePos t = tiles[random.below(static_cast<std::uint32_t>(tiles.size()))];
    const auto target = building_at(world, buildings, d.island, t);
    if (!target || (target->origin.x == d.x && target->origin.y == d.y)) {
        return;
    }
    if (!takes_fire(*target->type, random.below(8), rules)) {
        return;
    }
    const auto tx = static_cast<std::uint16_t>(target->origin.x);
    const auto ty = static_cast<std::uint16_t>(target->origin.y);
    const std::int32_t p = target->type->prod_kind;
    const Disaster* there = disaster_at(world, d.island, tx, ty);
    const bool burning = there != nullptr && there->kind == Disaster::Kind::Fire;
    bool catches = false;
    if (working_kind(p)) {
        const Production* state = production_at(world, d.island, tx, ty);
        catches = state != nullptr && !state->burning();
    } else if (p == prod::kHouse) {
        // A house with the plague catches fire, a burning one does not catch it again (the original
        // starts its count anew, so wooden quarters burn far longer, S-050).
        catches = world.house(d.island, tx, ty) != nullptr && !burning;
    } else if (p == prod::kShipyard) {
        catches = !burning;
    }
    if (catches) {
        start_disaster(world, buildings, d.island, target->origin, Disaster::Kind::Fire, kSpreadDuration, events, true);
    }
}

bool settler_city(const World& world, const City& city) {
    const Player* p = world.player(city.owner);
    return p != nullptr && (p->role == PlayerRole::Human || p->role == PlayerRole::Computer);
}

std::vector<House*> houses_of(World& world, const City& city, bool upper) {
    std::vector<House*> out;
    for (House& h : world.houses()) {
        if (h.island == city.island && h.city == city.index && (h.services & hs::kDisasterMask) == 0 &&
            (h.resident_class >= 2) == upper) {
            out.push_back(&h);
        }
    }
    return out;
}

/// 0x0047B850: up to four tries on houses of citizens and better that are at least half full.
void break_out_plague(World& world, const assets::BuildingTable& buildings, const City& city,
                      std::vector<DisasterEvent>* events) {
    const std::vector<House*> candidates = houses_of(world, city, true);
    if (candidates.empty()) {
        return;
    }
    Random& random = world.random(RandomStream::Events);
    for (int attempt = 0; attempt < 4; ++attempt) {
        const House* h = candidates[random.below(static_cast<std::uint32_t>(candidates.size()))];
        if (h->inhabitants64 < max_inhabitants64(buildings, h->resident_class) / 2) {
            continue;
        }
        if (kPlagueTakes[protection(*h)][random.below(16)] != 0) {
            start_disaster(world, buildings, h->island, {h->x, h->y}, Disaster::Kind::Plague, kPlagueDuration, events);
            return;
        }
    }
}

/// 0x0047B540: up to three tries on houses of pioneers and settlers.
void break_out_fire(World& world, const assets::BuildingTable& buildings, const Rules& rules, const City& city,
                    std::vector<DisasterEvent>* events) {
    const std::vector<House*> candidates = houses_of(world, city, false);
    if (candidates.empty()) {
        return;
    }
    Random& random = world.random(RandomStream::Events);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const House* h = candidates[random.below(static_cast<std::uint32_t>(candidates.size()))];
        const std::uint32_t r = random.below(8);
        const assets::BuildingType* b = type_at(world, buildings, h->island, h->x, h->y);
        if (b != nullptr && takes_fire(*b, r, rules)) {
            start_disaster(world, buildings, h->island, {h->x, h->y}, Disaster::Kind::Fire, kFireDuration, events);
            return;
        }
    }
}

/// 0x0047B710: a house of citizens and better; a gallows nearby lets only two of eight through.
void send_robber(World& world, const Rules& rules, const City& city, std::vector<RobberSpawn>* robbers) {
    const std::vector<House*> candidates = houses_of(world, city, true);
    if (candidates.empty()) {
        return;
    }
    // The original tries up to three houses, each turned away by a gallows nearby with 6 of 8; the
    // retries undo most of the gallows (a city covered everywhere still gets 58 % of the robbers,
    // S-055). In Open1602 a robber a gallows turned away does not try another house: a covered
    // house lets 2 of 8 through. OriginalBugs::gallows brings the retries back.
    Random& random = world.random(RandomStream::Events);
    const int attempts = rules.bugs.gallows ? 3 : 1;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        const House* h = candidates[random.below(static_cast<std::uint32_t>(candidates.size()))];
        const std::uint32_t r = random.below(8);
        if ((h->services & hs::kGalgen) == 0 || kRobberPastGallows[r] != 0) {
            if (robbers != nullptr) {
                robbers->push_back(RobberSpawn{h->island, {h->x, h->y}, h->city});
            }
            return;
        }
    }
}

}  // namespace

const Disaster* disaster_at(const World& world, std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    for (const Disaster& d : world.disasters()) {
        if (d.island == island && d.x == x && d.y == y) {
            return &d;
        }
    }
    return nullptr;
}

bool start_disaster(World& world, const assets::BuildingTable& buildings, std::uint16_t island, TilePos tile,
                    Disaster::Kind kind, std::uint8_t duration, std::vector<DisasterEvent>* events, bool spread) {
    if (island >= world.islands().size()) {
        return false;
    }
    const auto b = building_at(world, buildings, island, tile);
    if (!b || b->type->destroyed) {
        return false;
    }
    (void)take(world, island, b->origin);
    const auto x = static_cast<std::uint16_t>(b->origin.x);
    const auto y = static_cast<std::uint16_t>(b->origin.y);
    const std::int32_t p = b->type->prod_kind;
    if (working_kind(p)) {
        if (Production* state = production_at(world, island, x, y); state != nullptr && !state->burning()) {
            state->rate = 0;  // the running cycle yields nothing
            if (kind == Disaster::Kind::Fire) {
                state->flags = static_cast<std::uint8_t>(state->flags | Production::kBurningFlag);
            }
        }
    } else if (p == prod::kHouse) {
        if (House* h = world.house(island, x, y)) {
            h->services =
                static_cast<std::uint16_t>((h->services & ~hs::kDisasterMask) | static_cast<std::uint16_t>(kind));
        }
    }
    Disaster d;
    d.kind = kind;
    d.island = island;
    d.x = x;
    d.y = y;
    d.duration = duration;
    d.id = world.next_disaster_id();
    world.disasters().push_back(d);
    recount_disasters(world);
    emit(world, events, DisasterEvent::Kind::Started, d, spread);
    return true;
}

bool end_disaster(World& world, std::uint16_t island, TilePos origin, Disaster::Kind kind,
                  std::vector<DisasterEvent>* events) {
    const Disaster* d =
        disaster_at(world, island, static_cast<std::uint16_t>(origin.x), static_cast<std::uint16_t>(origin.y));
    if (d == nullptr || d->kind != kind) {
        return false;
    }
    const Disaster copy = *d;
    (void)take(world, island, origin);
    recount_disasters(world);
    emit(world, events, DisasterEvent::Kind::Ended, copy);
    return true;
}

void forget_disaster(World& world, std::uint16_t island, TilePos origin) {
    if (take(world, island, origin)) {
        recount_disasters(world);
    }
}

void recount_disasters(World& world) {
    for (const City& c : world.cities()) {
        if (City* city = world.city(c.island, c.index)) {
            city->disasters = 0;
        }
    }
    for (const Disaster& d : world.disasters()) {
        if (City* city = world.city(d.island, city_at(world, d.island, d.x, d.y))) {
            ++city->disasters;
        }
    }
}

void advance_disasters(World& world, const assets::BuildingTable& buildings, const Rules& rules,
                       std::vector<DisasterEvent>* events, std::vector<ConstructionEvent>* construction) {
    // The disasters of this epoch, oldest first; those that start now wait for the next one.
    std::vector<std::uint32_t> ids;
    ids.reserve(world.disasters().size());
    for (const Disaster& d : world.disasters()) {
        ids.push_back(d.id);
    }
    // New disasters get the next id and removals keep the order, so a list sorted by id stays so
    // and a binary search finds each one (TB-24); a list in another order (a hand-made save) is
    // searched through.
    const bool sorted =
        std::is_sorted(ids.begin(), ids.end()) && (ids.empty() || world.peek_disaster_id() > ids.back());
    for (const std::uint32_t id : ids) {
        auto& list = world.disasters();
        auto it = sorted ? std::lower_bound(list.begin(), list.end(), id,
                                            [](const Disaster& d, std::uint32_t v) { return d.id < v; })
                         : std::find_if(list.begin(), list.end(), [&](const Disaster& d) { return d.id == id; });
        if (it != list.end() && it->id != id) {
            it = list.end();
        }
        if (it == list.end()) {
            continue;  // burned down or cured meanwhile
        }
        ++it->elapsed;
        const Disaster d = *it;
        if (d.elapsed < d.duration) {
            if (d.kind == Disaster::Kind::Plague) {
                spread_plague(world, buildings, d, events);
            } else {
                spread_fire(world, buildings, rules, d, events);
            }
            continue;
        }
        if (d.kind == Disaster::Kind::Plague) {
            end_disaster(world, d.island, {d.x, d.y}, d.kind, events);
            continue;
        }
        emit(world, events, DisasterEvent::Kind::BurnedDown, d);
        if (!destroy_building(world, buildings, d.island, {d.x, d.y}, construction)) {
            forget_disaster(world, d.island, {d.x, d.y});
        }
    }
}

void advance_city_events(World& world, const assets::BuildingTable& buildings, const Rules& rules,
                         std::vector<DisasterEvent>* events, std::vector<RobberSpawn>* robbers) {
    const std::uint64_t now = world.time_ms();
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        const City& c = world.cities()[i];
        City* city = world.city(c.island, c.index);
        if (city == nullptr || !settler_city(world, *city)) {
            continue;
        }
        if (city->next_event_ms == 0) {
            city->next_event_ms = now + kCityEventMinMs;  // a new or loaded city: in a minute
            continue;
        }
        if (now < city->next_event_ms) {
            continue;
        }
        const auto count = [&](std::size_t from) {
            std::uint64_t n = 0;
            for (std::size_t k = from; k < city->inhabitants.size(); ++k) {
                n += city->inhabitants[k];
            }
            return n;
        };
        const std::uint64_t n0 = city->inhabitants.empty() ? 0 : city->inhabitants[0];
        const std::uint64_t n1 = city->inhabitants.size() > 1 ? city->inhabitants[1] : 0;
        const std::uint64_t cum1 = count(1);
        const std::uint64_t cum2 = count(2);
        Random& random = world.random(RandomStream::Events);
        // Plague: a city of at least 200 settlers and better without a disaster.
        if (cum1 >= 200 && city->disasters == 0) {
            const std::uint32_t n = cum2 < 200 ? 400 : cum2 < 400 ? 250 : 180;
            if (random.below(n) <= 12) {
                break_out_plague(world, buildings, *city, events);
            }
        }
        // Robbers: at least 300 citizens and better.
        if (cum2 >= 300) {
            const std::uint32_t n = cum2 < 500 ? 400 : cum2 < 700 ? 250 : 180;
            if (random.below(n) <= 150) {
                send_robber(world, rules, *city, robbers);
            }
        }
        // Fire: many pioneers, or many pioneers and settlers together.
        bool fire = false;
        if (n0 >= 80) {
            fire = random.below(n0 < 120 ? 200 : n0 < 150 ? 150 : 100) <= 30;
        } else if (n0 + n1 >= 250) {
            const std::uint64_t s = n0 + n1;
            fire = random.below(s < 300 ? 250 : s < 350 ? 180 : 100) <= 15;
        } else if (cum1 >= 250) {
            fire = random.below(256) <= 7;
        }
        if (fire) {
            break_out_fire(world, buildings, rules, *city, events);
        }
        city->next_event_ms = now + kCityEventMinMs + std::uint64_t{random.below(8)} * 1000U;
    }
}

}  // namespace o1602::sim
