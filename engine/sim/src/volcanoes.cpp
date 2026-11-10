// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/volcanoes.hpp"

#include <algorithm>
#include <array>

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint64_t kTenthMs = 100;  // the original counts game time in 1/10 s
constexpr std::uint64_t kUsPerMs = 1000;
constexpr std::uint64_t kLegUs = 500'000;         // a flight leg: 25 ticks of 20 ms
constexpr std::uint64_t kTenthUs = 100'000;       // the flight formula counts in 1/10 s
constexpr std::uint32_t kSeedRange = 0x8000;      // rand() % 0xFFFF with RAND_MAX 0x7FFF
constexpr std::int64_t kOne = 1 << 16;            // fixed point of the lava: 1/65536
constexpr std::int64_t kLaunchHeight = 258'867;   // 3.95 half tile widths over the mountain centre
constexpr std::int64_t kHalfGravity10 = 321'126;  // 4.9 * 65536: g/2 of the throw
constexpr std::int64_t kGroundPerPixel = 1835;    // 0.028 * 65536: Posoffs to height
constexpr std::uint8_t kLastStep = kLavaThrows - 1;

/// Earliest next eruption by the craters in the world (0x004692F0, time in 1/10 s): linear from
/// 35 minutes at one crater to 12 at four, 8 at eight and 6 at twenty.
constexpr std::array<std::uint32_t, kMaxCraterCount + 1> kInterval = {
    21000, 21000, 16400, 11800, 7200, 6600, 6000, 5400, 4800, 4700, 4600,
    4500,  4400,  4300,  4200,  4100, 4000, 3900, 3800, 3700, 3600,
};

/// Pause after each lava throw (0x0062BD40, 33 values in seconds, here in microseconds): five
/// linear pieces; each piece's first value overwrites the last of the piece before.
constexpr std::array<std::uint32_t, 33> make_steps() {
    struct Piece {
        std::uint32_t first;
        std::uint32_t last;
        std::uint32_t from;
        std::uint32_t to;
    };
    constexpr std::array<Piece, 5> kPieces = {{
        {0, 6, 500'000, 250'000},
        {6, 12, 250'000, 140'000},
        {12, 24, 170'000, 90'000},
        {24, 30, 120'000, 140'000},
        {30, 32, 170'000, 500'000},
    }};
    std::array<std::uint32_t, 33> steps{};
    for (const Piece& p : kPieces) {
        for (std::uint32_t i = p.first; i <= p.last; ++i) {
            const auto from = static_cast<std::int64_t>(p.from);
            const auto to = static_cast<std::int64_t>(p.to);
            steps[i] = static_cast<std::uint32_t>(from + (to - from) * (i - p.first) / (p.last - p.first));
        }
    }
    return steps;
}
constexpr std::array<std::uint32_t, 33> kSteps = make_steps();

std::uint32_t pause_after(std::uint32_t step) noexcept {
    return kSteps[step * 32 / kLastStep];
}

/// Sine for the table below, evaluated by the compiler (the same table on every platform).
constexpr double table_sin(double x) {
    constexpr double kPi = 3.14159265358979323846;
    while (x > kPi) {
        x -= 2 * kPi;
    }
    double term = x;
    double sum = x;
    for (int n = 1; n < 30; ++n) {
        term *= -x * x / ((2.0 * n) * (2.0 * n + 1));
        sum += term;
    }
    return sum;
}

/// Sine of 512 steps per turn in 1/65536 (0x0072B680; cosine = sine 128 steps on).
constexpr std::array<std::int32_t, 512> make_sine() {
    constexpr double kPi = 3.14159265358979323846;
    std::array<std::int32_t, 512> t{};
    for (std::size_t i = 0; i < t.size(); ++i) {
        const double v = table_sin(static_cast<double>(i) * 2 * kPi / 512) * kOne;
        t[i] = static_cast<std::int32_t>(v < 0 ? v - 0.5 : v + 0.5);
    }
    return t;
}
constexpr std::array<std::int32_t, 512> kSine = make_sine();
// The table is part of the game every computer must run the same (the lava's way): pinned, so
// that a compiler that rounds the series above differently fails to build instead (DS-7).
static_assert(kSine[0] == 0 && kSine[1] == 804 && kSine[43] == 33000 && kSine[85] == 56621);
static_assert(kSine[128] == 65536 && kSine[200] == 41576 && kSine[300] == -33692 && kSine[384] == -65536);
static_assert(kSine[511] == -804);

std::int64_t sine(std::uint32_t i) noexcept {
    return kSine[i & 511U];
}
std::int64_t cosine(std::uint32_t i) noexcept {
    return kSine[(i + 128) & 511U];
}

/// Random value 0..32767 of throw value `n` of an eruption. The original reads a table of 512
/// rand() values that every computer fills by itself and no savegame keeps (M-013); Open1602
/// derives the values from the eruption's seed.
std::uint32_t lava_random(std::uint16_t seed, std::uint32_t n) noexcept {
    std::uint64_t z = (std::uint64_t{seed} << 32 | n) + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    return static_cast<std::uint32_t>(z & 0x7FFFU);
}

void emit(std::vector<VolcanoEvent>* events, VolcanoEvent::Kind kind, std::uint16_t island, std::int32_t x,
          std::int32_t y, bool message = false) {
    if (events != nullptr) {
        events->push_back(
            VolcanoEvent{kind, island, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), message});
    }
}

Crater* crater_of(World& world, const CraterRef& ref) {
    if (ref.island >= world.islands().size()) {
        return nullptr;
    }
    for (Crater& c : world.island(ref.island).craters) {
        if (c.x == ref.x && c.y == ref.y) {
            return &c;
        }
    }
    return nullptr;
}

/// Ground under a world point in 1/65536 half tile widths, and the island tile there.
struct Ground {
    std::int64_t height = 0;
    std::uint16_t island = World::kSea;
    TilePos tile;
};

Ground ground_at(const World& world, const assets::BuildingTable& buildings, std::int64_t x, std::int64_t y) {
    Ground g;
    if (x < 0 || y < 0) {
        return g;
    }
    const auto tx = static_cast<std::uint64_t>(x >> 16);
    const auto ty = static_cast<std::uint64_t>(y >> 16);
    if (tx >= world.width() || ty >= world.height()) {
        return g;
    }
    const World::Cell& cell = world.cell(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
    if (cell.island == World::kSea) {
        return g;
    }
    const MapCell& top = world.islands()[cell.island].map.top(cell.local_x, cell.local_y);
    const assets::BuildingType* b = top.empty() ? nullptr : buildings.by_sprite(top.sprite);
    if (b == nullptr) {
        return g;
    }
    g.island = cell.island;
    g.tile = TilePos{cell.local_x, cell.local_y};
    g.height = std::int64_t{b->posoffs} * kGroundPerPixel;
    return g;
}

/// Whether lava sets the building on fire (0x00479FE0): HAUS_PRODTYP Kind 1..7 (workshops to
/// markets), 13 (houses) and 14 (shipyards).
bool catches_fire(const assets::BuildingType& b) noexcept {
    return (b.prod_kind >= prod::kWorkshop && b.prod_kind <= prod::kMarket) || b.prod_kind == prod::kHouse ||
           b.prod_kind == prod::kShipyard;
}

/// Whether lava destroys the building (0x00464A80): by its Kind.
bool destroyed_by_lava(const assets::BuildingType& b) noexcept {
    switch (b.kind) {
        case 1:   // STRASSE
        case 3:   // TOR
        case 4:   // MAUER
        case 5:   // MAUERSTRAND
        case 6:   // TURM
        case 7:   // TURMSTRAND
        case 10:  // WALD: forest, fields, plantations
        case 14:  // GEBAEUDE
        case 18:  // BRUECKE
        case 28:  // STRANDHAUS
        case 30:  // PIER
        case 34:  // MINE
        case 35:  // HQ: the Kontor
        case 36:  // HAFEN
        case 37:  // WMUEHLE
            return true;
        default:
            return false;
    }
}

/// What a lump does where it lands (0x00446ED0). In the original a fire starts on every computer
/// by itself, a destruction is a command of the owner or the host; here both happen on every
/// computer alike.
void land(World& world, const assets::BuildingTable& buildings, const Ground& g, std::vector<VolcanoEvent>* events,
          std::vector<DisasterEvent>* disasters, std::vector<ConstructionEvent>* construction) {
    const auto b = building_at(world, buildings, g.island, g.tile);
    if (!b || b->type->destroyed) {
        return;
    }
    if (catches_fire(*b->type)) {
        // A building that burns already does not catch fire again (like S-050).
        const Disaster* there = disaster_at(world, g.island, static_cast<std::uint16_t>(b->origin.x),
                                            static_cast<std::uint16_t>(b->origin.y));
        if (there != nullptr && there->kind == Disaster::Kind::Fire) {
            return;
        }
        if (start_disaster(world, buildings, g.island, b->origin, Disaster::Kind::Fire, kLavaFireDuration, disasters)) {
            emit(events, VolcanoEvent::Kind::Fire, g.island, b->origin.x, b->origin.y);
        }
    } else if (destroyed_by_lava(*b->type)) {
        if (destroy_building(world, buildings, g.island, b->origin, construction)) {
            emit(events, VolcanoEvent::Kind::Destroyed, g.island, b->origin.x, b->origin.y);
        }
    }
}

/// A lava throw (0x004471D0) from the mountain centre at game time `at_us`.
LavaLump throw_lava(const World& world, const Eruption& e, std::uint32_t speed, std::uint64_t at_us) {
    const WorldIsland& island = world.islands()[e.crater.island];
    const std::uint32_t n = 3U * e.step;
    LavaLump p;
    p.island = e.crater.island;
    // Mountain centre: the 4x4 mountain's origin + (2, 2).
    p.x = (std::int64_t{island.x} + e.crater.x + 2) * kOne;
    p.y = (std::int64_t{island.y} + e.crater.y + 2) * kOne;
    p.z = kLaunchHeight;
    // Launch speed 200 * Speed / 10000 * (0.6 + r / 128) tiles per second of the throw.
    const std::int64_t r = lava_random(e.seed, n) & 63U;
    p.speed = std::int64_t{speed} * (768 + 10 * r) * 128 / 125;
    p.heading = static_cast<std::uint8_t>(lava_random(e.seed, n + 1) & 63U);
    p.tilt = static_cast<std::uint8_t>(lava_random(e.seed, n + 2) & 31U);
    // ANIM 0 of VULKANFIRE1 is RANDOM over 1, 2, 1 (the original draws rand() for it).
    p.anim = lava_random(e.seed, n + kLavaThrows * 3) % 3 == 1 ? 2 : 1;
    p.launch_us = at_us;
    p.leg_start_us = at_us;
    p.leg_end_us = at_us;
    return p;
}

/// End of a leg of a lump (0x00446ED0): aims the next leg, or lands. False when the lump is gone.
bool lava_step(World& world, const assets::BuildingTable& buildings, LavaLump& p, std::vector<VolcanoEvent>* events,
               std::vector<DisasterEvent>* disasters, std::vector<ConstructionEvent>* construction) {
    const std::uint64_t now = p.leg_end_us;
    p.x += p.dx;
    p.y += p.dy;
    p.z += p.dz;
    p.dx = p.dy = p.dz = 0;
    p.leg_start_us = now;
    const Ground g = ground_at(world, buildings, p.x, p.y);
    const std::uint64_t tenths = (now - p.launch_us) / kTenthUs;
    if (p.z > g.height || tenths == 0) {
        // The next half second follows the throw's parabola over one second from the time since
        // the launch: h = v sin(a) t, z = v cos(a) t - 4.9 t^2.
        const std::uint32_t a = (p.tilt + 8U) / 2U;  // 2.8 to 13.4 degrees from the vertical
        const std::int64_t way = p.speed * sine(a) / kOne;
        std::int64_t dx = way * sine(p.heading * 8U) / kOne;
        std::int64_t dy = way * cosine(p.heading * 8U) / kOne;
        std::int64_t dz =
            p.speed * cosine(a) / kOne - kHalfGravity10 * (2 * static_cast<std::int64_t>(tenths) + 10) / 10;
        std::uint64_t duration = kLegUs;
        p.grounded = false;
        if (dz < 0 && p.z + dz < g.height) {
            // Stop exactly at the ground under the start of the leg.
            const std::int64_t drop = p.z - g.height;
            const std::int64_t fall = -dz;
            dx = dx * drop / fall;
            dy = dy * drop / fall;
            dz = -drop;
            duration = std::max<std::uint64_t>(
                1, kLegUs * static_cast<std::uint64_t>(drop) / static_cast<std::uint64_t>(fall));
            p.grounded = true;
        }
        p.dx = dx;
        p.dy = dy;
        p.dz = dz;
        p.leg_end_us = now + duration;
        return true;
    }
    if (g.island != World::kSea) {
        land(world, buildings, g, events, disasters, construction);
    }
    return false;
}

}  // namespace

std::vector<Crater> find_craters(const IslandMap& map, const assets::BuildingTable& buildings) {
    std::vector<Crater> craters;
    for (std::uint32_t y = 0; y < map.height() && craters.size() < kMaxCraters; ++y) {
        for (std::uint32_t x = map.width(); x-- > 0 && craters.size() < kMaxCraters;) {
            const MapCell& cell = map.top(x, y);
            const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
            if (b == nullptr || b->id != kVolcanoRecordId) {
                continue;
            }
            const SubTile sub = map.sub_tile(buildings, x, y);
            if (sub.origin_x == static_cast<std::int32_t>(x) && sub.origin_y == static_cast<std::int32_t>(y)) {
                craters.push_back(Crater{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), 0});
            }
        }
    }
    return craters;
}

std::uint32_t crater_count(const World& world) noexcept {
    std::uint32_t n = 0;
    for (const WorldIsland& island : world.islands()) {
        n += static_cast<std::uint32_t>(island.craters.size());
    }
    return std::min(n, kMaxCraterCount);
}

std::uint64_t eruption_interval_ms(std::uint32_t craters) noexcept {
    return std::uint64_t{kInterval[std::min(craters, kMaxCraterCount)]} * kTenthMs;
}

std::uint64_t first_eruption_ms(Random& random) {
    const std::uint64_t minutes = random.below(16);
    const std::uint64_t seconds = random.below(32);
    return 2 * eruption_interval_ms(1) + (60 * minutes + seconds) * 1000;
}

std::uint64_t eruption_step_us(std::uint32_t step) noexcept {
    std::uint64_t t = 0;
    for (std::uint32_t s = 0; s < std::min<std::uint32_t>(step, kLavaThrows); ++s) {
        t += pause_after(s);
    }
    return t;
}

bool volcanoes_enabled(const World& world, const Rules& rules) noexcept {
    switch (rules.volcanoes) {
        case Volcanoes::Off:
            return false;
        case Volcanoes::On:
            return true;
        case Volcanoes::Game:
            break;
    }
    return (world.game_settings().flags & World::GameSettings::kNoVolcanoes) == 0;
}

void roll_eruptions(World& world, const Rules& rules, std::vector<VolcanoEvent>* events) {
    if (!volcanoes_enabled(world, rules)) {
        return;
    }
    VolcanoState& v = world.volcanoes();
    for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
        const std::vector<Crater>& craters = world.islands()[i].craters;
        if (!v.eruptions.empty() || craters.empty() || world.time_ms() < v.next_ms) {
            continue;
        }
        Random& random = world.random(RandomStream::Islands);
        const Crater& c = craters[random.below(static_cast<std::uint32_t>(craters.size()))];
        const CraterRef ref{i, c.x, c.y};
        if (v.last && *v.last == ref) {
            // The crater that erupted last is passed over once; the time stays, so the next island
            // with craters (or this one 30 s later) tries again.
            v.last.reset();
            continue;
        }
        const auto seed = static_cast<std::uint16_t>(random.below(kSeedRange));
        erupt(world, ref, seed, true, events);
        const std::uint64_t minutes = random.below(16);
        const std::uint64_t seconds = random.below(32);
        v.next_ms = world.time_ms() + eruption_interval_ms(crater_count(world)) + (60 * minutes + seconds) * 1000;
        v.last.reset();
    }
}

bool erupt(World& world, CraterRef crater, std::uint16_t seed, bool message, std::vector<VolcanoEvent>* events) {
    Crater* c = crater_of(world, crater);
    if (c == nullptr) {
        return false;
    }
    WorldIsland& island = world.island(crater.island);
    island.volcano_erupted = true;
    c->steps_left = kLastStep;
    // The ore deposit registered for this very mountain goes (0x0046BDD0; Auf zu neuen Ufern has
    // some, and the endless generator may put a big iron vein into a volcano): the last one takes
    // its place, as in the original.
    auto& deposits = island.header.deposits;
    for (std::size_t k = deposits.size(); k-- > 0;) {
        if (deposits[k].x == crater.x && deposits[k].y == crater.y) {
            deposits[k] = deposits.back();
            deposits.pop_back();
            break;
        }
    }
    VolcanoState& v = world.volcanoes();
    const bool first = v.eruptions.empty();
    v.eruptions.push_back(Eruption{crater, seed, 0, world.time_ms() * kUsPerMs});
    emit(events, VolcanoEvent::Kind::Erupted, crater.island, crater.x, crater.y, message && first);
    return true;
}

void advance_volcanoes(World& world, const assets::BuildingTable& buildings, std::uint32_t lava_speed,
                       std::vector<VolcanoEvent>* events, std::vector<DisasterEvent>* disasters,
                       std::vector<ConstructionEvent>* construction) {
    VolcanoState& v = world.volcanoes();
    if (v.eruptions.empty() && v.lava.empty()) {
        return;
    }
    const std::uint64_t now = world.time_ms() * kUsPerMs;
    // Eruption steps (0x00447350): a throw each, then the end.
    for (std::size_t i = 0; i < v.eruptions.size();) {
        Eruption& e = v.eruptions[i];
        bool over = false;
        while (e.next_us <= now) {
            if (e.step > kLastStep) {
                over = true;
                break;
            }
            v.lava.push_back(throw_lava(world, e, lava_speed, e.next_us));
            e.next_us += pause_after(e.step);
            ++e.step;
            // The crater of this eruption, not of the last one started (S-061); 0 at the end (S-064).
            if (Crater* c = crater_of(world, e.crater)) {
                c->steps_left = static_cast<std::uint16_t>(e.step <= kLastStep ? kLastStep - e.step : 0);
            }
        }
        if (over) {
            const CraterRef done = e.crater;
            v.eruptions.erase(v.eruptions.begin() + static_cast<std::ptrdiff_t>(i));
            v.last = done;
            emit(events, VolcanoEvent::Kind::Ended, done.island, done.x, done.y);
            continue;
        }
        ++i;
    }
    // Lava flights: legs that end by now, in the order of the throws.
    std::vector<LavaLump>& lava = v.lava;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < lava.size(); ++i) {
        LavaLump p = lava[i];
        bool alive = true;
        while (alive && p.leg_end_us <= now) {
            alive = lava_step(world, buildings, p, events, disasters, construction);
        }
        if (alive) {
            lava[kept++] = p;
        }
    }
    lava.resize(kept);
}

LavaPoint lava_position(const LavaLump& p, std::uint64_t now_us) noexcept {
    double f = 0.0;
    if (p.leg_end_us > p.leg_start_us && now_us > p.leg_start_us) {
        f = std::min(1.0,
                     static_cast<double>(now_us - p.leg_start_us) / static_cast<double>(p.leg_end_us - p.leg_start_us));
    }
    constexpr double kScale = 1.0 / kOne;
    return LavaPoint{(static_cast<double>(p.x) + f * static_cast<double>(p.dx)) * kScale,
                     (static_cast<double>(p.y) + f * static_cast<double>(p.dy)) * kScale,
                     (static_cast<double>(p.z) + f * static_cast<double>(p.dz)) * kScale};
}

double lava_ground(const World& world, const assets::BuildingTable& buildings, double x, double y) noexcept {
    const Ground g =
        ground_at(world, buildings, static_cast<std::int64_t>(x * kOne), static_cast<std::int64_t>(y * kOne));
    return static_cast<double>(g.height) / kOne;
}

}  // namespace o1602::sim
