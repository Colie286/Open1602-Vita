// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/ambient.hpp"
#include "o1602/core/profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

#include "o1602/assets/game_data.hpp"
#include "o1602/render/land_figures.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::render {

namespace {

// HAUS Kind values.
constexpr std::int32_t kRoad = assets::kind::kRoad;
constexpr std::int32_t kGate = assets::kind::kGate;
constexpr std::int32_t kSquare = assets::kind::kSquare;
constexpr std::int32_t kBridge = assets::kind::kBridge;
constexpr std::int32_t kPier = assets::kind::kPier;
constexpr std::int32_t kBeachInnerCorner = assets::kind::kBeachInner;  // STRANDECKI

// Townsfolk (figuren.cod).
constexpr std::uint16_t kAdelweibl = assets::figure::kNoblewoman;
constexpr std::uint16_t kAdel = assets::figure::kNobleman;
constexpr std::uint16_t kAlter = assets::figure::kOldMan;
constexpr std::uint16_t kFrau = assets::figure::kWoman;
constexpr std::uint16_t kPassant = assets::figure::kPasserby;
constexpr std::uint16_t kVeteran = assets::figure::kVeteran;
constexpr std::uint16_t kKind = assets::figure::kChild;
constexpr std::uint16_t kPilger = assets::figure::kPilgrim;

constexpr std::uint32_t kWalkRadius = 10;  // the 21 x 21 search window around the house
constexpr std::uint32_t kStraight = 64;    // one step of the original's wave search
constexpr std::uint32_t kDiagonal = 90;
constexpr std::uint32_t kWalkerTries = 10;      // per city round
constexpr std::uint32_t kHouseBucket = 1087;    // records of the original's house list a try picks from
constexpr std::uint32_t kMinSettlers = 30;      // settlers and better for walkers
constexpr std::uint32_t kJugglerCitizens = 59;  // citizens and better for a juggler: more than this
constexpr std::uint32_t kSeaTries = 3;
constexpr std::int32_t kShipNear = 5;      // no creature this close to a ship
constexpr std::int32_t kShipSeen = 10;     // turns away from ships this close
constexpr std::int32_t kIslandSeen = 5;    // turns away from islands this close
constexpr std::uint32_t kBirdTrees = 25;   // a bird emitter on every 25th tree
constexpr std::uint32_t kSurfCorners = 4;  // a surf emitter on every 4th inner beach corner
constexpr std::uint32_t kBirdPeriodMs = 32'000;
constexpr std::uint32_t kSurfPeriodMs = 9'000;
constexpr std::uint32_t kBirdVariants = 6;

constexpr std::uint64_t key3(std::uint32_t island, std::uint32_t x, std::uint32_t y) noexcept {
    return (std::uint64_t{island} << 32) | (std::uint64_t{y} << 16) | x;
}

std::uint32_t tile_hash(std::uint32_t island, std::uint32_t x, std::uint32_t y) noexcept {
    std::uint32_t h = island * 0x9E3779B1U ^ x * 0x85EBCA77U ^ y * 0xC2B2AE3DU;
    h ^= h >> 15;
    h *= 0x2C1B3C6DU;
    h ^= h >> 12;
    return h;
}

const assets::BuildingType* type_at(const sim::IslandMap& map, const assets::BuildingTable& buildings, std::int32_t x,
                                    std::int32_t y) {
    if (!map.contains(x, y)) {
        return nullptr;
    }
    const sim::MapCell& c = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    return c.empty() ? nullptr : buildings.by_sprite(c.sprite);
}

std::uint32_t inhabitants_from(const sim::City& c, std::size_t first) {
    std::uint32_t n = 0;
    for (std::size_t k = first; k < c.inhabitants.size(); ++k) {
        n += c.inhabitants[k];
    }
    return n;
}

/// Direction code 1..8 (N, NE, E, SE, S, SW, W, NW) away from the blocked sides
/// (bit 0 +y, bit 1 -x, bit 2 -y, bit 3 +x); 0 = free choice, 15 = nowhere.
constexpr std::array<std::uint8_t, 16> kAwayDolphin = {0, 1, 3, 2, 5, 3, 4, 3, 7, 8, 1, 1, 6, 7, 5, 0xFF};
constexpr std::array<std::uint8_t, 16> kAwayWhale = {0, 1, 3, 3, 5, 3, 5, 3, 7, 1, 1, 1, 7, 7, 5, 0xFF};
/// Species by rand() & 7: dolphins 5 of 8, whales 2, a kraken 1.
constexpr std::array<std::uint16_t, 8> kSpecies = {kFigureDolphin, kFigureKraken,  kFigureDolphin, kFigureDolphin,
                                                   kFigureWhale,   kFigureDolphin, kFigureWhale,   kFigureDolphin};

// What the generators of a decision are derived from, besides the seed.
constexpr std::uint64_t kTagCity = 1;
constexpr std::uint64_t kTagSea = 2;
constexpr std::uint64_t kTagSound = 3;

/// Mixes `v` into `h` (the splitmix64 finaliser): a different value for every input.
constexpr std::uint64_t mix(std::uint64_t h, std::uint64_t v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6U) + (h >> 2U);
    h ^= h >> 30U;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27U;
    h *= 0x94D049BB133111EBULL;
    h ^= h >> 31U;
    return h;
}

AnimState fresh_anim(std::uint8_t anim, std::uint32_t seed) noexcept {
    AnimState s;
    s.anim = anim;
    s.random = seed | 1U;
    return s;
}

}  // namespace

std::array<std::uint16_t, 4> townsfolk_for(std::int32_t kind) noexcept {
    switch (kind) {
        case 7:  // MARKT
            return {kAlter, kFrau, kKind, kAdelweibl};
        case 17:  // WIRT
            return {kAlter, kAdel, kPilger, kAdelweibl};
        case 18:  // KAPELLE
            return {kAdel, kAlter, kFrau, kPilger};
        case 19:  // KIRCHE
            return {kAdel, kPassant, kPilger, kAdelweibl};
        case 20:  // BADEHAUS
        case 21:  // THEATER
            return {kAdel, kPassant, kFrau, kAdelweibl};
        case 22:  // KLINIK
            return {kAlter, kFrau, kVeteran, kPassant};
        case 23:  // SCHULE
            return {kFrau, kKind, kPassant, kAdelweibl};
        case 24:  // HOCHSCHULE
            return {kPassant, kAdel, kPassant, kFrau};
        default:
            return {kFrau, kFrau, kFrau, kFrau};
    }
}

std::vector<std::int32_t> walker_targets(std::uint32_t roll) {
    switch (roll & 3U) {
        case 0:
            return {17, 22};  // tavern, doctor
        case 1:
            return {7, 24};  // market, university
        case 2:
            return {18, 19, 21};  // chapel, church, theatre
        default:
            return {20, 23, 24};  // bath house, school, university
    }
}

sim::Random Ambient::stream(std::uint64_t tag, std::uint64_t a, std::uint64_t b, std::uint64_t c) const noexcept {
    return sim::Random(mix(mix(mix(mix(m_seed, tag), a), b), c), tag);
}

void Ambient::tick(const sim::World& world, const assets::BuildingTable& buildings,
                   const assets::FigureTable& figures) {
    core::profile::Scope timer(core::profile::AmbientTick);
    const std::uint64_t now = world.time_ms();
    if (!m_started || now < m_tick_ms) {
        // The first call, or another game in the same picture: nothing is decided before it.
        m_started = true;
        m_tick_ms = now;
        m_frame_ms = now;
        m_walking.clear();
        m_busy_houses.clear();
        m_creatures.clear();
        m_jugglers.clear();
        return;
    }
    const std::uint64_t from = m_tick_ms;
    m_tick_ms = now;
    if (now == from) {
        return;
    }
    m_ships_squared = false;
    m_troops_counted = false;
    // What falls into (from, now], in the order of game time.
    enum class Kind : std::uint8_t { City, Sea, Markets };
    struct Event {
        std::uint64_t at = 0;
        Kind kind = Kind::City;
        std::uint64_t round = 0;
        const sim::City* city = nullptr;
        std::uint32_t bx = 0;  ///< Sea: the block
        std::uint32_t by = 0;
    };
    std::vector<Event> events;
    const auto every = [&](std::uint64_t period, std::uint64_t phase, auto&& add) {
        // Times phase + k * period after `from` and up to `now`.
        std::uint64_t k = from < phase ? 0 : (from - phase) / period + 1;
        for (std::uint64_t at = phase + k * period; at <= now; at += period, ++k) {
            add(at, k);
        }
    };
    for (const sim::City& c : world.cities()) {
        // Each city has its own moment in the 10 s (the original: whenever the city first came up).
        const std::uint64_t phase = mix(mix(m_seed, kTagCity), std::uint64_t{c.island} << 8U | c.index) % kCityPeriodMs;
        every(kCityPeriodMs, phase,
              [&](std::uint64_t at, std::uint64_t k) { events.push_back(Event{at, Kind::City, k, &c}); });
    }
    // Each block of the sea has its own moment in the 5 s too, so a big sea is not decided in one
    // tick (TB-27).
    if (std::min(m_config.sea_interval_ms, kSeaIntervalMaxMs) != 0) {
        for (std::uint32_t by = 0; by * kSeaBlock < world.height(); ++by) {
            for (std::uint32_t bx = 0; bx * kSeaBlock < world.width(); ++bx) {
                const std::uint64_t phase = mix(mix(m_seed, kTagSea), std::uint64_t{by} << 16U | bx) % kSeaPeriodMs;
                every(kSeaPeriodMs, phase, [&](std::uint64_t at, std::uint64_t k) {
                    events.push_back(Event{at, Kind::Sea, k, nullptr, bx, by});
                });
            }
        }
    }
    every(kJugglerPeriodMs, 0,
          [&](std::uint64_t at, std::uint64_t k) { events.push_back(Event{at, Kind::Markets, k}); });
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    for (const Event& e : events) {
        retire(e.at);
        switch (e.kind) {
            case Kind::City:
                city_round(world, buildings, figures, *e.city, e.at, e.round);
                break;
            case Kind::Sea:
                sea_round(world, figures, e.at, e.round, e.bx, e.by);
                break;
            case Kind::Markets:
                jugglers(world, buildings, figures, e.at);
                break;
        }
    }
    retire(now);
}

bool Ambient::foreign_troops(const sim::World& world, std::uint16_t island, sim::PlayerId owner) {
    if (!m_troops_counted) {
        // The players with troops per island, once per tick instead of all soldiers per city.
        m_island_troops.assign(world.islands().size(), {});
        for (const sim::Soldier& s : world.soldiers()) {
            if (s.owner == sim::kNoPlayer || s.x() < 0 || s.y() < 0) {
                continue;
            }
            const auto sx = static_cast<std::uint32_t>(s.x());
            const auto sy = static_cast<std::uint32_t>(s.y());
            const sim::Player* p = world.player(s.owner);
            if (!world.contains(sx, sy) || p == nullptr ||
                (p->role != sim::PlayerRole::Human && p->role != sim::PlayerRole::Computer)) {
                continue;
            }
            const std::uint16_t at = world.cell(sx, sy).island;
            if (at >= m_island_troops.size()) {
                continue;
            }
            std::vector<sim::PlayerId>& owners = m_island_troops[at];
            if (std::find(owners.begin(), owners.end(), s.owner) == owners.end()) {
                owners.push_back(s.owner);
            }
        }
        m_troops_counted = true;
    }
    if (island >= m_island_troops.size()) {
        return false;
    }
    const std::vector<sim::PlayerId>& owners = m_island_troops[island];
    return std::any_of(owners.begin(), owners.end(), [&](sim::PlayerId o) { return o != owner; });
}

void Ambient::retire(std::uint64_t at_ms) {
    std::erase_if(m_walking, [&](const Walker& w) {
        const bool done = w.end_ms <= at_ms;
        if (done) {
            m_busy_houses.erase(w.house);
        }
        return done;
    });
    std::erase_if(m_creatures, [&](const Creature& c) { return c.end_ms <= at_ms; });
}

void Ambient::city_round(const sim::World& world, const assets::BuildingTable& buildings,
                         const assets::FigureTable& figures, const sim::City& c, std::uint64_t at_ms,
                         std::uint64_t round) {
    if (c.island >= world.islands().size() || inhabitants_from(c, 1) < kMinSettlers) {
        return;
    }
    // No townsfolk while soldiers of another player stand on the island.
    if (foreign_troops(world, c.island, c.owner)) {
        return;
    }
    std::vector<const sim::House*> houses;
    for (const sim::House& h : world.houses()) {
        if (h.island == c.island && h.city == c.index) {
            houses.push_back(&h);
        }
    }
    // The original picks 10 records of a house list shared by several islands, so a city gets
    // about 10 * houses / 1087 walkers per round; Open1602 keeps that rate but always finds a
    // house of the city.
    sim::Random random = stream(kTagCity, std::uint64_t{c.island} << 8U | c.index, round);
    for (std::uint32_t t = 0; t < kWalkerTries && !houses.empty(); ++t) {
        if (random.below(kHouseBucket) >= houses.size()) {
            continue;
        }
        const sim::House& h = *houses[random.below(static_cast<std::uint32_t>(houses.size()))];
        send_walker(world, buildings, figures, h, random, at_ms);
    }
}

void Ambient::frame(const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                    const AmbientView& view) {
    const std::uint64_t now = world.time_ms();
    // Where the townsfolk and creatures are drawn: up to a tick behind, like the units of the game.
    constexpr double kTickMs = sim::Logistics::kTickMs;
    const double drawn =
        std::max(0.0, static_cast<double>(now) - kTickMs + std::clamp(view.tick_fraction, 0.0, 1.0) * kTickMs);
    const auto catch_up = [&](AnimState& anim, std::uint64_t& anim_ms, std::uint16_t figure) {
        if (now > anim_ms) {
            if (const assets::FigureType* type = figures.get(figure)) {
                advance_animation(anim, *type, static_cast<std::uint32_t>(std::min<std::uint64_t>(now - anim_ms, ~0U)));
            }
            anim_ms = now;
        }
    };

    const auto known = [&](std::uint32_t island) {
        return view.local_player == sim::kNoPlayer || world.islands()[island].discovered_by(view.local_player);
    };

    m_walker_instances.clear();
    for (Walker& w : m_walking) {
        if (w.island >= world.islands().size() || now >= w.end_ms || !known(w.island)) {
            continue;
        }
        // On the straight line between the centres of two tiles of its path.
        const double elapsed = std::max(0.0, drawn - static_cast<double>(w.start_ms));
        const auto next = std::upper_bound(w.arrive.begin(), w.arrive.end(), elapsed,
                                           [](double e, std::uint32_t a) { return e < a; });
        const auto step = static_cast<std::size_t>(std::max<std::ptrdiff_t>(next - w.arrive.begin(), 1) - 1);
        const std::size_t to = std::min(step + 1, w.path.size() - 1);
        const double t =
            to == step ? 0.0 : (elapsed - w.arrive[step]) / static_cast<double>(w.arrive[to] - w.arrive[step]);
        const sim::TilePos a = w.path[step];
        const sim::TilePos b = w.path[to];
        const sim::WorldIsland& island = world.islands()[w.island];
        const sim::TilePos on = t < 0.5 ? a : b;
        const double fx = static_cast<double>(island.x) + a.x + 0.5 + (b.x - a.x) * t;
        const double fy = static_cast<double>(island.y) + a.y + 0.5 + (b.y - a.y) * t;
        if (!view.drawn.contains(fx, fy) || !walker_visible_on(type_at(island.map, buildings, on.x, on.y))) {
            continue;
        }
        catch_up(w.anim, w.anim_ms, w.figure);
        FigureInstance f;
        f.x = fx;
        f.y = fy;
        f.figure = w.figure;
        f.z = w.z;
        f.anim = w.anim.anim;
        f.frame = w.anim.frame;
        f.direction = sim::direction_between(a, b);
        f.sheet = assets::FigureSheet::Traeger;
        m_walker_instances.push_back(f);
    }

    m_effects.clear();
    for (Creature& c : m_creatures) {
        if (now >= c.end_ms) {
            continue;
        }
        const double age = std::max(0.0, drawn - static_cast<double>(c.start_ms));
        const double ex = c.x + c.vx * age;
        const double ey = c.y + c.vy * age;
        if (!view.drawn.contains(ex, ey)) {
            continue;
        }
        catch_up(c.anim, c.anim_ms, c.figure);
        EffectFigure e;
        e.x = ex;
        e.y = ey;
        e.figure = c.figure;
        e.anim = c.anim.anim;
        e.frame = c.anim.frame;
        e.direction = c.direction;
        e.sheet = assets::FigureSheet::Fische;
        m_effects.push_back(e);
    }
    if (const assets::FigureType* fig = figures.get(kFigureJuggler)) {
        for (auto& [key, j] : m_jugglers) {
            const auto island_index = static_cast<std::uint32_t>(key >> 32U);
            const auto x = static_cast<std::uint32_t>(key & 0xFFFFU);
            const auto y = static_cast<std::uint32_t>((key >> 16U) & 0xFFFFU);
            if (island_index >= world.islands().size() || !known(island_index)) {
                continue;
            }
            const sim::WorldIsland& island = world.islands()[island_index];
            if (!view.drawn.contains(static_cast<double>(island.x) + x, static_cast<double>(island.y) + y)) {
                continue;
            }
            if (!island.map.contains(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y))) {
                continue;
            }
            const sim::MapCell& cell = island.map.top(x, y);
            const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
            if (b == nullptr || b->prod_kind != assets::prod::kMarket) {
                continue;  // torn down since the last look
            }
            catch_up(j.anim, j.anim_ms, kFigureJuggler);
            EffectFigure e;
            building_attach_point(island, *b, x, y, cell.rotation, *fig, e.x, e.y);
            e.z = fig->fahnoffs[2];
            // The shadow lies under his feet, not on the ground below the market: kind 0x1E centres
            // it on the lifted point like the body, without the ground height of lava.
            e.ground = e.z;
            e.figure = kFigureJuggler;
            e.anim = j.anim.anim;
            e.frame = j.anim.frame;
            e.direction = static_cast<std::uint8_t>(((cell.rotation + 1U) & 3U) * 2U);
            e.sheet = assets::FigureSheet::Gaukler;
            m_effects.push_back(e);
        }
    }
    sounds(world, buildings, view, now);
}

void Ambient::send_walker(const sim::World& world, const assets::BuildingTable& buildings,
                          const assets::FigureTable& figures, const sim::House& house, sim::Random& random,
                          std::uint64_t at_ms) {
    const std::uint32_t roll = random.below(4);
    const std::uint32_t min_cost = (random.below(8) + 5) * kStraight;  // 5..12 steps on the road
    const std::uint64_t key = key3(house.island, house.x, house.y);
    if (m_busy_houses.contains(key)) {
        return;  // one walker per house
    }
    const sim::IslandMap& map = world.islands()[house.island].map;
    const assets::BuildingType* home = type_at(map, buildings, house.x, house.y);
    if (home == nullptr) {
        return;
    }
    const sim::MapCell& home_cell = map.top(house.x, house.y);
    std::uint32_t hw = 0;
    std::uint32_t hh = 0;
    sim::rotated_size(*home, home_cell.rotation, hw, hh);
    const auto cx = static_cast<std::int32_t>(house.x + (hw - 1) / 2);
    const auto cy = static_cast<std::int32_t>(house.y + (hh - 1) / 2);
    const std::vector<std::int32_t> targets = walker_targets(roll);

    // Wave search on the roads of a 21 x 21 window (grid, search): roads,
    // squares, bridges, piers and the middle of gates are open; buildings of this city of the
    // chosen kinds are goals; the first goal at least `min_cost` away is taken, nearer ones are
    // dead ends.
    constexpr std::int32_t kSide = 2 * kWalkRadius + 1;
    const std::int32_t x0 = cx - static_cast<std::int32_t>(kWalkRadius);
    const std::int32_t y0 = cy - static_cast<std::int32_t>(kWalkRadius);
    enum : std::uint8_t { kClosed, kOpen, kGoal };
    std::array<std::uint8_t, kSide * kSide> grid{};
    for (std::int32_t j = 0; j < kSide; ++j) {
        for (std::int32_t i = 0; i < kSide; ++i) {
            const std::int32_t x = x0 + i;
            const std::int32_t y = y0 + j;
            const assets::BuildingType* b = type_at(map, buildings, x, y);
            if (b == nullptr) {
                continue;
            }
            std::uint8_t& g = grid[static_cast<std::size_t>(j * kSide + i)];
            if (b->kind == kRoad || b->kind == kSquare || b->kind == kBridge || b->kind == kPier) {
                g = kOpen;
            } else if (b->kind == kGate) {
                const sim::SubTile sub =
                    map.sub_tile(buildings, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                std::uint32_t gw = 0;
                std::uint32_t gh = 0;
                sim::rotated_size(*b, map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).rotation,
                                  gw, gh);
                if (x == sub.origin_x + static_cast<std::int32_t>(gw / 2) &&
                    y == sub.origin_y + static_cast<std::int32_t>(gh / 2)) {
                    g = kOpen;
                }
            }
            if (std::find(targets.begin(), targets.end(), b->prod_kind) != targets.end() &&
                map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).city == house.city) {
                g = kGoal;
            }
        }
    }
    constexpr std::uint32_t kInf = std::numeric_limits<std::uint32_t>::max();
    std::array<std::uint32_t, kSide * kSide> cost{};
    std::array<std::int32_t, kSide * kSide> from{};
    cost.fill(kInf);
    from.fill(-1);
    using Item = std::pair<std::uint32_t, std::int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
    for (std::uint32_t j = 0; j < hh; ++j) {
        for (std::uint32_t i = 0; i < hw; ++i) {
            const std::int32_t gx = static_cast<std::int32_t>(house.x + i) - x0;
            const std::int32_t gy = static_cast<std::int32_t>(house.y + j) - y0;
            if (gx >= 0 && gy >= 0 && gx < kSide && gy < kSide) {
                const std::int32_t n = gy * kSide + gx;
                cost[static_cast<std::size_t>(n)] = 0;
                grid[static_cast<std::size_t>(n)] = kOpen;  // the house is the start
                queue.emplace(0, n);
            }
        }
    }
    std::int32_t goal = -1;
    while (!queue.empty()) {
        const auto [d, n] = queue.top();
        queue.pop();
        if (d != cost[static_cast<std::size_t>(n)]) {
            continue;
        }
        if (grid[static_cast<std::size_t>(n)] == kGoal) {
            if (d >= min_cost) {
                goal = n;
                break;
            }
            continue;  // too near: a dead end
        }
        const std::int32_t gx = n % kSide;
        const std::int32_t gy = n / kSide;
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const std::int32_t nx = gx + dx;
                const std::int32_t ny = gy + dy;
                if (nx < 0 || ny < 0 || nx >= kSide || ny >= kSide) {
                    continue;
                }
                const std::int32_t m = ny * kSide + nx;
                if (grid[static_cast<std::size_t>(m)] == kClosed) {
                    continue;
                }
                if (dx != 0 && dy != 0 &&
                    (grid[static_cast<std::size_t>(gy * kSide + nx)] == kClosed ||
                     grid[static_cast<std::size_t>(ny * kSide + gx)] == kClosed)) {
                    continue;  // no cutting corners
                }
                const std::uint32_t nd = d + (dx != 0 && dy != 0 ? kDiagonal : kStraight);
                if (nd < cost[static_cast<std::size_t>(m)]) {
                    cost[static_cast<std::size_t>(m)] = nd;
                    from[static_cast<std::size_t>(m)] = n;
                    queue.emplace(nd, m);
                }
            }
        }
    }
    if (goal < 0) {
        return;  // nowhere to go (the original makes the figure and frees it at once)
    }
    Walker w;
    w.island = house.island;
    w.house = key;
    for (std::int32_t n = goal; n >= 0; n = from[static_cast<std::size_t>(n)]) {
        w.path.push_back(sim::TilePos{x0 + n % kSide, y0 + n / kSide});
    }
    std::reverse(w.path.begin(), w.path.end());
    const assets::BuildingType* target = type_at(map, buildings, w.path.back().x, w.path.back().y);
    w.figure = townsfolk_for(target != nullptr ? target->prod_kind : 0)[random.below(4)];
    const std::uint32_t seed = random.next();
    const assets::FigureType* type = figures.get(w.figure);
    if (type == nullptr || type->speed == 0 || w.path.size() < 2) {
        return;
    }
    // Speed * 0.002 tiles per second on the road (Wegspeed 100 %) from tile centre to tile centre,
    // diagonal steps sqrt(2) times as long; in whole milliseconds so that every
    // computer lets it arrive at the same moment.
    const std::uint32_t ms_per_tile = (500'000U + type->speed - 1) / type->speed;
    w.arrive.assign(w.path.size(), 0);
    for (std::size_t i = 1; i < w.path.size(); ++i) {
        const bool diagonal = w.path[i].x != w.path[i - 1].x && w.path[i].y != w.path[i - 1].y;
        w.arrive[i] = w.arrive[i - 1] + (diagonal ? ms_per_tile * 14'142U / 10'000U : ms_per_tile);
    }
    w.start_ms = at_ms;
    w.end_ms = at_ms + w.arrive.back();
    // The height of the house tile, kept on the way: 0.56 on land.
    w.z = spawn_height(world, buildings, world.islands()[house.island].x + house.x + 0.5,
                       world.islands()[house.island].y + house.y + 0.5);
    w.anim_ms = at_ms;
    w.anim = start_animation(*type, 0, seed);
    m_busy_houses.insert(key);
    m_walking.push_back(std::move(w));
    ++m_stats.walkers;
}

void Ambient::sea_round(const sim::World& world, const assets::FigureTable& figures, std::uint64_t at_ms,
                        std::uint64_t round, std::uint32_t bx, std::uint32_t by) {
    // The original tries 3 random tiles of the screen, so where the player looks
    // decides where creatures come up. Here each block of 28 x 28 tiles, about one screen of
    // 1024 x 768 at the nearest zoom, has the 3 tries: the same sea everywhere.
    // At the original's rate a creature comes up in nearly every block every 5 s, which over the
    // whole sea is far too busy; Config::sea_interval_ms thins the chances out (flavor.md).
    const std::uint32_t interval = std::min(m_config.sea_interval_ms, kSeaIntervalMaxMs);
    if (interval == 0) {
        return;
    }
    sim::Random random = stream(kTagSea, round, bx, by);
    if (interval > kSeaPeriodMs && random.below(interval) >= kSeaPeriodMs) {
        return;  // this block's chance is not taken this time
    }
    if (!m_ships_squared) {
        // The ships by squares of a block, once per tick: a try looks only at the squares around.
        const std::uint32_t cols = (world.width() + kSeaBlock - 1) / kSeaBlock + 1;
        const std::uint32_t rows = (world.height() + kSeaBlock - 1) / kSeaBlock + 1;
        m_ship_cols = cols;
        m_ship_squares.assign(std::size_t{cols} * rows, {});
        for (std::size_t i = 0; i < world.ships().size(); ++i) {
            const sim::Ship& s = world.ships()[i];
            const std::uint32_t c = std::min<std::uint32_t>(s.x / kSeaBlock, cols - 1);
            const std::uint32_t r = std::min<std::uint32_t>(s.y / kSeaBlock, rows - 1);
            m_ship_squares[std::size_t{r} * cols + c].push_back(static_cast<std::uint32_t>(i));
        }
        m_ships_squared = true;
    }
    sea_block(world, figures, random, bx * kSeaBlock, by * kSeaBlock, at_ms);
}

void Ambient::sea_block(const sim::World& world, const assets::FigureTable& figures, sim::Random& random,
                        std::uint32_t x0, std::uint32_t y0, std::uint64_t at_ms) {
    for (std::uint32_t attempt = 0; attempt < kSeaTries; ++attempt) {
        // A random tile of the block, open sea outside every island's rectangle.
        const auto tx = static_cast<std::int32_t>(x0 + random.below(kSeaBlock));
        const auto ty = static_cast<std::int32_t>(y0 + random.below(kSeaBlock));
        if (!world.contains(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty)) ||
            world.cell(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty)).island != sim::World::kSea) {
            continue;
        }
        const std::uint16_t species = kSpecies[random.below(8)];
        // Sides to keep away from: ships and islands near the spot.
        std::uint32_t blocked = 0;
        bool near_ship = false;
        const auto rows = static_cast<std::int32_t>(m_ship_squares.size() / m_ship_cols);
        const auto cols = static_cast<std::int32_t>(m_ship_cols);
        const std::int32_t c0 = std::max(0, (tx - kShipSeen) / static_cast<std::int32_t>(kSeaBlock));
        const std::int32_t c1 = std::min(cols - 1, (tx + kShipSeen) / static_cast<std::int32_t>(kSeaBlock));
        const std::int32_t r0 = std::max(0, (ty - kShipSeen) / static_cast<std::int32_t>(kSeaBlock));
        const std::int32_t r1 = std::min(rows - 1, (ty + kShipSeen) / static_cast<std::int32_t>(kSeaBlock));
        for (std::int32_t r = r0; r <= r1 && !near_ship; ++r) {
            for (std::int32_t c = c0; c <= c1 && !near_ship; ++c) {
                for (const std::uint32_t i :
                     m_ship_squares[static_cast<std::size_t>(r) * m_ship_cols + static_cast<std::size_t>(c)]) {
                    const sim::Ship& s = world.ships()[i];
                    const std::int32_t dx = static_cast<std::int32_t>(s.x) - tx;
                    const std::int32_t dy = static_cast<std::int32_t>(s.y) - ty;
                    if (std::abs(dx) >= kShipSeen || std::abs(dy) >= kShipSeen) {
                        continue;
                    }
                    if (std::abs(dx) <= kShipNear && std::abs(dy) <= kShipNear) {
                        near_ship = true;
                        break;
                    }
                    if (std::abs(dx) > std::abs(dy)) {
                        blocked |= dx > 0 ? 8U : 2U;
                    } else {
                        blocked |= dy > 0 ? 1U : 4U;
                    }
                }
            }
        }
        if (near_ship) {
            continue;
        }
        for (const sim::WorldIsland& island : world.islands()) {
            const auto left = static_cast<std::int32_t>(island.x);
            const auto top = static_cast<std::int32_t>(island.y);
            const std::int32_t right = left + static_cast<std::int32_t>(island.map.width()) - 1;
            const std::int32_t bottom = top + static_cast<std::int32_t>(island.map.height()) - 1;
            if (tx < left - kIslandSeen || tx > right + kIslandSeen || ty < top - kIslandSeen ||
                ty > bottom + kIslandSeen) {
                continue;
            }
            blocked |=
                (tx <= left ? 8U : 0U) | (tx >= right ? 2U : 0U) | (ty <= top ? 1U : 0U) | (ty >= bottom ? 4U : 0U);
        }
        const bool whale = species == kFigureWhale;
        std::uint8_t code = (whale ? kAwayWhale : kAwayDolphin)[blocked & 15U];
        if (code == 0xFF) {
            continue;
        }
        if (code == 0) {
            code = static_cast<std::uint8_t>(whale ? random.below(4) * 2 + 1 : random.below(8) + 1);
        }
        const auto direction = static_cast<std::uint8_t>(code - 1);
        const assets::FigureType* type = figures.get(species);
        if (type == nullptr || type->speed == 0) {
            return;
        }
        // Worktime runs down at Speed per 50 ms.
        const std::uint64_t life = std::uint64_t{type->work_time_ms} * 500 / type->speed;
        const double x = tx + 0.5;
        const double y = ty + 0.5;
        const auto add = [&](double cx, double cy, std::uint8_t anim) {
            Creature c;
            c.figure = species;
            c.x = cx;
            c.y = cy;
            c.direction = direction;
            c.start_ms = at_ms;
            c.end_ms = at_ms + life;
            c.anim_ms = at_ms;
            c.anim = fresh_anim(anim, random.next());
            m_creatures.push_back(c);
        };
        if (species == kFigureDolphin) {
            // A pod: one or two companions with later jumps, then the leader.
            add(x, y, 2);
            if (random.below(2) != 0) {
                add(x + 1.0, y + 0.2, 3);
            }
            add(x - 1.0, y + 0.7, 0);
        } else {
            add(x, y, 0);
            if (whale) {
                // Swims along a world axis: Speed * 0.002 tiles per second.
                static constexpr std::array<std::pair<int, int>, 8> kStep = {
                    {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};
                const double pace = type->speed * 0.000002;
                m_creatures.back().vx = kStep[direction].first * pace;
                m_creatures.back().vy = kStep[direction].second * pace;
            }
        }
        ++m_stats.creatures;
        return;
    }
}

void Ambient::jugglers(const sim::World& world, const assets::BuildingTable& buildings,
                       const assets::FigureTable& figures, std::uint64_t at_ms) {
    const assets::FigureType* fig = figures.get(kFigureJuggler);
    if (fig == nullptr) {
        return;
    }
    for (auto& [key, j] : m_jugglers) {
        j.seen = false;
    }
    for (const sim::Production& p : world.productions()) {
        if (!p.used || p.island >= world.islands().size()) {
            continue;
        }
        const sim::WorldIsland& island = world.islands()[p.island];
        const sim::MapCell& cell = island.map.top(p.x, p.y);
        const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
        if (b == nullptr || b->prod_kind != assets::prod::kMarket || b->attached_figures[1] != kFigureJuggler) {
            continue;
        }
        const sim::City* city = world.city(p.island, cell.city);
        if (city == nullptr || inhabitants_from(*city, 2) <= kJugglerCitizens) {
            continue;
        }
        // One per market; its act (ANIM) is (x + y) % 5 of the market.
        auto [it, fresh] = m_jugglers.try_emplace(key3(p.island, p.x, p.y));
        if (fresh) {
            it->second.anim =
                start_animation(*fig, static_cast<std::uint8_t>((p.x + p.y) % 5), tile_hash(p.island, p.x, p.y));
            it->second.anim_ms = at_ms;
        }
        it->second.seen = true;
    }
    std::erase_if(m_jugglers, [](const auto& kv) { return !kv.second.seen; });
}

void Ambient::sounds(const sim::World& world, const assets::BuildingTable& buildings, const AmbientView& view,
                     std::uint64_t now_ms) {
    // Birds in every 25th tree and surf at every 4th inner beach corner of the islands on screen;
    // the original counts them in the order it looks at the screen, so they move when it scrolls;
    // here each tile decides by itself. Each one sounds in its own cycle; only at normal speed.
    const std::uint64_t before = std::min(m_frame_ms, now_ms);
    m_frame_ms = now_ms;
    if (view.game_speed != 1 || before == now_ms) {
        return;
    }
    m_emitters.resize(world.islands().size());
    for (std::uint16_t i = 0; i < world.islands().size() && i < view.islands.size(); ++i) {
        if (!view.islands[i]) {
            continue;
        }
        const sim::WorldIsland& island = world.islands()[i];
        IslandEmitters& em = m_emitters[i];
        if (!em.built || now_ms - em.built_ms >= kCityPeriodMs) {
            // Trees are felled and planted: look again from time to time.
            em.built = true;
            em.built_ms = now_ms;
            em.list.clear();
            for (std::uint32_t y = 0; y < island.map.height(); ++y) {
                for (std::uint32_t x = 0; x < island.map.width(); ++x) {
                    const sim::MapCell& cell = island.map.top(x, y);
                    const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
                    if (b == nullptr) {
                        continue;
                    }
                    const bool tree = b->prod_kind == assets::prod::kRipe && b->ware == assets::ware::kTree;
                    const bool corner = b->kind == kBeachInnerCorner;
                    const std::uint32_t h = tile_hash(i, x, y);
                    if ((tree && h % kBirdTrees == 0) || (corner && h % kSurfCorners == 0)) {
                        em.list.push_back(
                            Emitter{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), tree, h});
                    }
                }
            }
        }
        for (const Emitter& e : em.list) {
            const std::uint32_t period = e.birds ? kBirdPeriodMs : kSurfPeriodMs;
            const std::uint64_t phase = (e.hash >> 8) % period;
            if ((before + phase) / period == (now_ms + phase) / period) {
                continue;
            }
            // Which bird, or whether the surf sounds at all, follows the cycle.
            const std::uint64_t cycle = (now_ms + phase) / period;
            sim::Random random = stream(kTagSound, e.hash, cycle);
            AmbientSound s;
            s.x = island.x + e.x + 0.5;
            s.y = island.y + e.y + 0.5;
            if (e.birds) {
                s.kind = AmbientSound::Kind::Birds;
                s.variant = static_cast<std::uint8_t>(random.below(kBirdVariants));
            } else if (random.below(2) == 0) {
                s.kind = AmbientSound::Kind::Surf;
            } else {
                continue;
            }
            m_sounds.push_back(s);
        }
    }
}

}  // namespace o1602::render
