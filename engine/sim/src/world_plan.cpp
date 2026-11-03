// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/world_plan.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <utility>

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/island_templates.hpp"
#include "o1602/sim/player.hpp"
#include "o1602/sim/random.hpp"
#include "o1602/sim/volcanoes.hpp"

namespace o1602::sim {

namespace {

using assets::ScenarioPlan;
using Amount = WorldSettings::Amount;

using templates::kClassHeight;
using templates::kClassWidth;
using templates::kTemplateCount;
using templates::template_path;
// A band of the map that allows both climates; an island whose climate is not decided yet.
constexpr std::uint8_t kEither = climate::kCount;

constexpr std::uint32_t kEdge = 20;              // the original's plans keep 20 tiles to the map edge
constexpr std::uint32_t kGap = 8;                // water between two island rectangles (templates add a ring)
constexpr std::uint32_t kShipClear = 5;          // from a start to the nearest island rectangle
constexpr std::uint32_t kTilesPerIsland = 9500;  // the original: 17-20 islands on 500 x 350
constexpr std::uint32_t kPlanIslands = 18;       // islands the original's resource counts are made for
constexpr std::uint32_t kPlanPlayers = 4;
constexpr std::uint64_t kPlanLand = 500ULL * 350 * 13 / 100;  // land of the original's plans: about 13 %
constexpr std::uint64_t kMaxOreScale = 20000;                 // percent: veins of the level at most 200 times
constexpr std::uint64_t kPlanStream = 0x5701'D0A1ULL;         // the islands
constexpr std::uint64_t kStartOrderStream = 0x5701'D0A2ULL;   // which player gets which start area

// Ore, crop and treasure entries of the original's plans per level (Endlosspiel<D><V>.szs).
constexpr auto kCropBuildings = id::kPlanCrops;
constexpr std::array<std::array<std::uint16_t, 6>, 4> kCropCounts = {
    {{5, 5, 3, 5, 4, 5}, {4, 4, 3, 4, 3, 4}, {3, 3, 2, 3, 2, 3}, {3, 2, 2, 3, 2, 2}}};
constexpr std::array<std::uint16_t, 4> kBigIron = {6, 5, 4, 2};
constexpr std::array<std::uint16_t, 4> kSmallIron = {8, 8, 7, 9};
constexpr std::array<std::uint16_t, 4> kGold = {3, 2, 2, 2};
constexpr std::array<std::uint16_t, 4> kTreasures = {10, 8, 6, 4};

// sin(k * 360/64 degrees) * 10000 for k = 0..16: integer trigonometry, the same on every computer.
constexpr std::array<std::int32_t, 17> kSine = {0,    980,  1951, 2903, 3827, 4714, 5556, 6344, 7071,
                                                7730, 8315, 8819, 9239, 9569, 9808, 9952, 10000};
constexpr std::int32_t kAngles = 64;

std::int32_t sine(std::int32_t k) {
    k = ((k % kAngles) + kAngles) % kAngles;
    if (k <= 16) {
        return kSine[static_cast<std::size_t>(k)];
    }
    if (k <= 32) {
        return kSine[static_cast<std::size_t>(32 - k)];
    }
    if (k <= 48) {
        return -kSine[static_cast<std::size_t>(k - 32)];
    }
    return -kSine[static_cast<std::size_t>(64 - k)];
}
std::int32_t cosine(std::int32_t k) {
    return sine(k + 16);
}

struct Rect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;
    [[nodiscard]] std::int32_t cx() const { return x + w / 2; }
    [[nodiscard]] std::int32_t cy() const { return y + h / 2; }
};

/// An island while the plan is made.
struct Planned {
    Rect rect;
    std::uint8_t size = 0;
    std::uint8_t climate = kEither;  ///< climate::kNorth, kSouth, or kEither: not yet decided
    bool home = false;
    std::uint16_t template_number = ScenarioPlan::kRandomTemplate;
    bool own = false;      ///< Shapes::Own: an island of its own shape, `rect` its size
    bool village = false;  ///< Shapes::Own: an island of its own shape with a native village
    bool volcano = false;  ///< Shapes::Own: an island of its own shape with a volcano
};

/// The sides of an island to place and its size class.
struct Dims {
    std::int32_t w = 0;
    std::int32_t h = 0;
    std::uint8_t size = 0;
};

/// The size class of an island of its own shape: the narrowest template width of each class
/// (the loader takes the class of an INSEL5 from its width), by the longer side.
std::uint8_t class_of(std::int32_t w, std::int32_t h) {
    constexpr std::array<std::int32_t, 4> kFrom = {33, 43, 56, 76};  // mit, med, big, lar
    const std::int32_t side = std::max(w, h);
    std::uint8_t size = 0;
    for (const std::int32_t f : kFrom) {
        size = static_cast<std::uint8_t>(size + (side >= f ? 1 : 0));
    }
    return size;
}

Dims class_dims(std::uint8_t size) {
    return Dims{static_cast<std::int32_t>(kClassWidth[size]), static_cast<std::int32_t>(kClassHeight[size]), size};
}

struct Point {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

std::int64_t distance2(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by) {
    const std::int64_t dx = ax - bx;
    const std::int64_t dy = ay - by;
    return dx * dx + dy * dy;
}

std::int32_t isqrt(std::int64_t v) {
    std::int64_t r = 0;
    while ((r + 1) * (r + 1) <= v) {
        ++r;
    }
    return static_cast<std::int32_t>(r);
}

/// Native villages of a world of `islands`: four per 18 islands as the original.
std::uint32_t village_count(const WorldSettings& s, std::uint32_t islands) {
    std::uint32_t per18 = 0;
    switch (s.natives) {
        case Amount::None:
            per18 = 0;
            break;
        case Amount::Few:
            per18 = 2;
            break;
        case Amount::Normal:
            per18 = 4;
            break;
        case Amount::Many:
            per18 = 7;
            break;
    }
    return (islands * per18 + kPlanIslands / 2) / kPlanIslands;
}

/// The shortest side of an island of its own shape with a volcano: the mountain, its ring of slopes
/// and a tile of room around it, a beach and the surf on both sides.
constexpr std::int32_t kVolcanoSide = 24;
/// The shorter side of an island of its own shape with a native village: room for the villages of
/// the templates (up to 30 tiles across in the south) inland. On an island larger than the
/// original's 255 tiles the generator puts the village within its first 255.
constexpr std::int32_t kVillageSide = 40;

/// Islands with a volcano at most: one per player (none/few/many: 0, half, double), at most 10
/// (20 craters in a world, the original's limit) up to twelve players and as many more per player
/// beyond them (a higher kMaxPlayers, world_generator.cpp max_craters).
std::uint32_t volcano_limit(const WorldSettings& s) {
    std::uint32_t limit = 0;
    const std::uint32_t players = s.players;
    switch (s.volcanoes) {
        case Amount::None:
            limit = 0;
            break;
        case Amount::Few:
            limit = std::max<std::uint32_t>(1, players / 2);
            break;
        case Amount::Normal:
            limit = players;
            break;
        case Amount::Many:
            limit = 2 * players;
            break;
    }
    return std::min<std::uint32_t>(limit, std::max<std::uint32_t>(10, 10 * players / 12));
}

class Planner {
public:
    Planner(const WorldSettings& s, const std::vector<TemplateInfo>& templates, std::vector<std::string>* warnings)
        : m_s(s), m_templates(templates), m_warnings(warnings), m_random(s.seed, kPlanStream) {}

    core::Result<WorldPlan> run();

private:
    void warn(std::string text) {
        if (m_warnings != nullptr) {
            m_warnings->push_back(std::move(text));
        }
    }
    [[nodiscard]] bool fits(const Rect& r) const;
    [[nodiscard]] std::uint8_t band(const Rect& r) const;
    [[nodiscard]] std::uint8_t draw_size();
    /// Shapes::Own: whether islands get their own sizes; their sides as the landscape says.
    [[nodiscard]] bool free_sizes() const noexcept { return m_s.shapes == WorldSettings::Shapes::Own; }
    [[nodiscard]] Dims draw_dims();
    [[nodiscard]] Dims own_dims(std::int32_t side);
    /// The same island a quarter smaller, nothing below the smallest side.
    [[nodiscard]] std::optional<Dims> smaller(const Dims& d) const;
    [[nodiscard]] std::int32_t gap() const noexcept;
    void place_starts();
    void place_homes();
    void place_locals(std::uint32_t total);
    void place_rest(std::uint32_t total);
    void place_land(bool warn_full);
    void choose_villages();
    void choose_volcanoes();
    bool place_near(const Point& start, Dims dims, std::uint32_t min_r, std::uint32_t max_r, bool home);
    void decide_climates();
    bool place_for_climate(std::size_t start, std::uint8_t climate);
    void choose_templates();
    [[nodiscard]] std::vector<std::size_t> in_reach(std::size_t start) const;
    WorldPlan finish();

    const WorldSettings& m_s;
    const std::vector<TemplateInfo>& m_templates;
    std::vector<std::string>* m_warnings;
    Random m_random;
    std::vector<Point> m_starts;
    std::vector<std::int32_t> m_home;  ///< index into m_islands per start, -1 none
    std::uint32_t m_reach = 0;
    std::vector<Planned> m_islands;
};

bool Planner::fits(const Rect& r) const {
    const auto w = static_cast<std::int32_t>(m_s.width);
    const auto h = static_cast<std::int32_t>(m_s.height);
    const auto edge = static_cast<std::int32_t>(kEdge);
    if (r.x < edge || r.y < edge || r.x + r.w > w - edge || r.y + r.h > h - edge) {
        return false;
    }
    const std::int32_t gap = this->gap();
    for (const Planned& p : m_islands) {
        const Rect& o = p.rect;
        if (!(r.x + r.w + gap <= o.x || o.x + o.w + gap <= r.x || r.y + r.h + gap <= o.y || o.y + o.h + gap <= r.y)) {
            return false;
        }
    }
    const auto clear = static_cast<std::int32_t>(kShipClear);
    for (const Point& s : m_starts) {
        const std::int32_t dx = std::max({r.x - s.x, 0, s.x - (r.x + r.w - 1)});
        const std::int32_t dy = std::max({r.y - s.y, 0, s.y - (r.y + r.h - 1)});
        if (std::max(dx, dy) < clear) {
            return false;
        }
    }
    return true;
}

std::int32_t Planner::gap() const noexcept {
    if (!free_sizes()) {
        return static_cast<std::int32_t>(kGap);
    }
    switch (m_s.landscape.gap) {
        case WorldSettings::Landscape::Gap::Narrow:
            return 4;
        case WorldSettings::Landscape::Gap::Normal:
            break;
        case WorldSettings::Landscape::Gap::Wide:
            return 20;
    }
    return static_cast<std::int32_t>(kGap);
}

Dims Planner::own_dims(std::int32_t side) {
    // The other side by the outline: round islands up to a sixth shorter or longer, long ones
    // a half to a third as wide, turned either way.
    std::int32_t other = side;
    if (m_s.landscape.outline == WorldSettings::Landscape::Outline::Long) {
        other = side * static_cast<std::int32_t>(34 + m_random.below(20)) / 100;
    } else {
        other = side * static_cast<std::int32_t>(84 + m_random.below(33)) / 100;
    }
    // Never longer than the largest side asked for.
    const std::int32_t longest =
        std::max<std::int32_t>(side, std::min<std::int32_t>(m_s.landscape.max_side, shapes::kMaxSide));
    other = std::clamp<std::int32_t>(other, shapes::kMinSide, longest);
    const bool turned = m_random.below(2) == 1;
    const std::int32_t w = turned ? other : side;
    const std::int32_t h = turned ? side : other;
    return Dims{w, h, class_of(w, h)};
}

Dims Planner::draw_dims() {
    if (!free_sizes()) {
        return class_dims(draw_size());
    }
    const WorldSettings::Landscape& l = m_s.landscape;
    // No larger than the map holds within its edges.
    const auto room =
        std::max<std::int32_t>(shapes::kMinSide, static_cast<std::int32_t>(std::min(m_s.width, m_s.height)) -
                                                     2 * static_cast<std::int32_t>(kEdge));
    const auto lo = std::min(
        room, static_cast<std::int32_t>(std::clamp<std::uint16_t>(l.min_side, shapes::kMinSide, shapes::kMaxSide)));
    const auto hi = std::clamp(static_cast<std::int32_t>(std::min(l.max_side, shapes::kMaxSide)), lo, room);
    const std::int32_t span = hi - lo;
    // A draw u in 0..1000 shaped by the spread: even, small ones more often (u squared), or one in
    // seven from the largest fifth and the rest from the smallest third.
    const auto u = static_cast<std::int32_t>(m_random.below(1001));
    using Spread = WorldSettings::Landscape::Spread;
    if (l.spread == Spread::Mixed) {
        return own_dims(lo + static_cast<std::int32_t>(std::int64_t{span} * u * u / 1000000));
    }
    if (l.spread == Spread::FewGiants) {
        return own_dims(m_random.below(7) == 0 ? hi - span / 5 * u / 1000 : lo + span / 3 * u / 1000);
    }
    return own_dims(lo + span * u / 1000);
}

std::optional<Dims> Planner::smaller(const Dims& d) const {
    const std::int32_t least = free_sizes() ? std::max<std::int32_t>(shapes::kMinSide, m_s.landscape.min_side * 2 / 3)
                                            : static_cast<std::int32_t>(kClassWidth[0]);
    if (!free_sizes()) {
        if (d.size == 0) {
            return std::nullopt;
        }
        return class_dims(static_cast<std::uint8_t>(d.size - 1));
    }
    const std::int32_t w = d.w * 3 / 4;
    const std::int32_t h = d.h * 3 / 4;
    if (std::min(w, h) < least) {
        return std::nullopt;
    }
    return Dims{w, h, class_of(w, h)};
}

std::uint8_t Planner::band(const Rect& r) const {
    // The original: the north is the top third of the map, the south the bottom third;
    // in between either.
    const auto cy = static_cast<std::uint32_t>(r.cy());
    if (cy <= m_s.height / 3) {
        return 0;
    }
    if (cy >= 2 * m_s.height / 3) {
        return 1;
    }
    return 2;
}

std::uint8_t Planner::draw_size() {
    // Weights per class lit, mit, med, big, lar; Mixed is the original's mix (5/4/4/5 of 18).
    std::array<std::uint32_t, 5> weights{};
    switch (m_s.sizes) {
        case WorldSettings::Sizes::Small:
            weights = {35, 30, 20, 15, 0};
            break;
        case WorldSettings::Sizes::Mixed:
            weights = {26, 23, 25, 26, std::uint64_t{m_s.width} * m_s.height >= 2ULL * 500 * 350 ? 3U : 0U};
            break;
        case WorldSettings::Sizes::Large:
            weights = {10, 15, 25, 40, 10};
            break;
    }
    std::uint32_t total = 0;
    for (const std::uint32_t v : weights) {
        total += v;
    }
    std::uint32_t r = m_random.below(total);
    for (std::uint8_t c = 0; c < 5; ++c) {
        if (r < weights[c]) {
            return c;
        }
        r -= weights[c];
    }
    return 0;
}

void Planner::place_starts() {
    const auto w = static_cast<std::int32_t>(m_s.width);
    const auto h = static_cast<std::int32_t>(m_s.height);
    const std::int32_t n = m_s.players;
    if (m_s.starts == WorldSettings::Starts::Together || n == 1) {
        m_starts.push_back(Point{w / 2, h / 2});
        m_reach = 90;
        return;
    }
    const auto min_distance2 = [](const std::vector<Point>& points) {
        std::int64_t nearest = std::numeric_limits<std::int64_t>::max();
        for (std::size_t i = 0; i < points.size(); ++i) {
            for (std::size_t j = i + 1; j < points.size(); ++j) {
                nearest = std::min(nearest, distance2(points[i].x, points[i].y, points[j].x, points[j].y));
            }
        }
        return nearest;
    };
    if (n <= 4) {
        // Up to four: on a flat ellipse around the centre, all in the middle band (both climates
        // near), turned so that the nearest two are as far apart as possible.
        const std::int32_t rx = w * 36 / 100;
        const std::int32_t ry = h * 16 / 100;  // all starts in the middle band, both climates near
        std::vector<Point> best;
        std::int64_t best_distance = -1;
        for (std::int32_t offset = 0; offset < kAngles / n; ++offset) {
            std::vector<Point> points;
            for (std::int32_t k = 0; k < n; ++k) {
                const std::int32_t a = offset + k * kAngles / n;
                points.push_back(Point{w / 2 + rx * cosine(a) / 10000, h / 2 + ry * sine(a) / 10000});
            }
            if (const std::int64_t d = min_distance2(points); d > best_distance) {
                best_distance = d;
                best = std::move(points);
            }
        }
        m_starts = std::move(best);
    } else {
        // More: rows and columns of equal cells, every second row shifted by half a cell.
        // round(sqrt(n * h / w)) rows, so that the cells are about square.
        auto rows = static_cast<std::int32_t>((isqrt(std::int64_t{n} * h * 100 / w) + 5) / 10);
        rows = std::clamp(rows, 1, n);
        const std::int32_t columns = (n + rows - 1) / rows;
        std::int32_t placed = 0;
        for (std::int32_t r = 0; r < rows && placed < n; ++r) {
            const std::int32_t in_row = std::min(columns, n - placed);
            for (std::int32_t c = 0; c < in_row; ++c, ++placed) {
                const std::int32_t shift = (r % 2 == 1 && in_row == columns) ? w / (4 * columns) : 0;
                const std::int32_t x = w * (2 * c + 1) / (2 * in_row) + (r % 2 == 1 ? shift : -shift);
                m_starts.push_back(Point{x, h * (2 * r + 1) / (2 * rows)});
            }
        }
    }
    m_reach = static_cast<std::uint32_t>(std::clamp(isqrt(min_distance2(m_starts)) * 60 / 100, 70, 140));
}

bool Planner::place_near(const Point& start, Dims dims, std::uint32_t min_r, std::uint32_t max_r, bool home) {
    const std::int32_t cw = dims.w;
    const std::int32_t ch = dims.h;
    // Homes first face the map centre (most room), then the other directions.
    const std::int32_t toward = [&] {
        const std::int32_t dx = static_cast<std::int32_t>(m_s.width / 2) - start.x;
        const std::int32_t dy = static_cast<std::int32_t>(m_s.height / 2) - start.y;
        std::int32_t best = 0;
        std::int64_t best_dot = std::numeric_limits<std::int64_t>::min();
        for (std::int32_t k = 0; k < kAngles; k += 4) {
            const std::int64_t dot = std::int64_t{dx} * cosine(k) + std::int64_t{dy} * sine(k);
            if (dot > best_dot) {
                best_dot = dot;
                best = k;
            }
        }
        return best;
    }();
    constexpr std::uint32_t kTries = 240;
    for (std::uint32_t t = 0; t < kTries; ++t) {
        std::int32_t a = 0;
        std::int32_t r = 0;
        if (home) {
            // Rings of directions around "toward", nearer first.
            const auto step = static_cast<std::int32_t>(t % 16);
            a = toward + ((step % 2 == 0) ? step / 2 : -(step + 1) / 2) * 4;
            r = static_cast<std::int32_t>(min_r + (t / 16) * 2);
        } else {
            a = static_cast<std::int32_t>(m_random.below(static_cast<std::uint32_t>(kAngles)));
            r = static_cast<std::int32_t>(min_r + m_random.below(max_r - min_r + 1));
        }
        // A home: its edge at distance r (the centre r plus the half extent in that direction);
        // other islands: the centre at distance r, so that they count as within reach.
        const std::int32_t c = cosine(a);
        const std::int32_t s = sine(a);
        const std::int32_t half = home ? (std::abs(c) * cw + std::abs(s) * ch) / 20000 : 0;
        const std::int32_t cx = start.x + (r + half) * c / 10000;
        const std::int32_t cy = start.y + (r + half) * s / 10000;
        const Rect rect{cx - cw / 2, cy - ch / 2, cw, ch};
        if (fits(rect)) {
            Planned p;
            p.rect = rect;
            p.size = dims.size;
            p.home = home;
            p.own = free_sizes();
            m_islands.push_back(p);
            return true;
        }
        if (home && static_cast<std::uint32_t>(r) > max_r) {
            break;
        }
    }
    return false;
}

void Planner::place_homes() {
    m_home.assign(m_starts.size(), -1);
    if (m_s.starts == WorldSettings::Starts::Together) {
        return;
    }
    // Of its own shape: as large as a big template, if the landscape allows, the same for all.
    const Dims wanted =
        free_sizes()
            ? own_dims(std::clamp<std::int32_t>(70, std::max<std::int32_t>(shapes::kMinSide, m_s.landscape.min_side),
                                                std::max<std::int32_t>(shapes::kMinSide, m_s.landscape.max_side)))
            : class_dims(m_s.sizes == WorldSettings::Sizes::Small ? 2 : 3);
    for (std::size_t i = 0; i < m_starts.size(); ++i) {
        for (std::optional<Dims> dims = wanted; dims; dims = smaller(*dims)) {
            if (place_near(m_starts[i], *dims, kShipClear, kShipClear + 14, true)) {
                m_home[i] = static_cast<std::int32_t>(m_islands.size() - 1);
                break;
            }
        }
        if (m_home[i] < 0) {
            warn("no room for the home island of player " + std::to_string(i));
        }
    }
}

void Planner::place_locals(std::uint32_t total) {
    // The same sizes near every start, so that every player has as much land within reach.
    if (m_s.starts == WorldSettings::Starts::Together || m_starts.size() < 2) {
        return;
    }
    const auto per_start = std::clamp<std::uint32_t>(
        (total > m_islands.size() ? total - static_cast<std::uint32_t>(m_islands.size()) : 0U) /
            static_cast<std::uint32_t>(m_starts.size()) / 2U,
        2U, 4U);  // with the home three islands: two of the north and one of the south
    std::vector<Dims> sizes;
    for (std::uint32_t k = 0; k < per_start; ++k) {
        sizes.push_back(free_sizes() ? draw_dims() : class_dims(std::min<std::uint8_t>(draw_size(), 3)));
    }
    // The large ones first, while there is room.
    std::stable_sort(sizes.begin(), sizes.end(),
                     [](const Dims& a, const Dims& b) { return std::int64_t{a.w} * a.h > std::int64_t{b.w} * b.h; });
    for (const Dims& dims : sizes) {
        for (const Point& start : m_starts) {
            if (!place_near(start, dims, m_reach * 40 / 100, m_reach * 90 / 100, false)) {
                // One step smaller; islands of their own shape down to the smallest side.
                for (std::optional<Dims> d = smaller(dims); d; d = free_sizes() ? smaller(*d) : std::nullopt) {
                    if (place_near(start, *d, m_reach * 40 / 100, m_reach * 90 / 100, false)) {
                        break;
                    }
                }
            }
        }
    }
}

void Planner::place_rest(std::uint32_t total) {
    constexpr std::uint32_t kTries = 400;
    const auto w = m_s.width;
    const auto h = m_s.height;
    while (m_islands.size() < total) {
        bool placed = false;
        for (std::uint8_t size = draw_size();; --size) {
            const std::uint32_t cw = kClassWidth[size];
            const std::uint32_t ch = kClassHeight[size];
            if (w > 2 * kEdge + cw && h > 2 * kEdge + ch) {
                for (std::uint32_t t = 0; t < kTries && !placed; ++t) {
                    const Rect rect{static_cast<std::int32_t>(kEdge + m_random.below(w - 2 * kEdge - cw + 1)),
                                    static_cast<std::int32_t>(kEdge + m_random.below(h - 2 * kEdge - ch + 1)),
                                    static_cast<std::int32_t>(cw), static_cast<std::int32_t>(ch)};
                    if (fits(rect)) {
                        Planned p;
                        p.rect = rect;
                        p.size = size;
                        m_islands.push_back(p);
                        placed = true;
                    }
                }
            }
            if (placed || size == 0) {
                break;
            }
        }
        if (!placed) {
            warn("the map is full after " + std::to_string(m_islands.size()) + " of " + std::to_string(total) +
                 " islands");
            return;
        }
    }
}

std::uint64_t own_target(const WorldSettings& s) {
    // The rectangles of the islands: the land over the share of a rectangle that is land.
    const WorldSettings::Landscape& l = s.landscape;
    const std::uint64_t land =
        std::clamp(l.land, WorldSettings::Landscape::kMinLand, WorldSettings::Landscape::kMaxLand);
    return std::uint64_t{s.width} * s.height * land / 100 * 1000 / l.fill();
}

void Planner::place_land(bool warn_full) {
    // The sizes are drawn first, until they hold the land, and placed from the largest: a giant
    // finds room only while the map is still empty. An island that finds none is tried smaller;
    // what is still missing then is filled with new draws until the map is full.
    constexpr std::uint32_t kTries = 300;
    constexpr std::uint32_t kMisses = 40;    // islands in a row that found no room: the map is full
    constexpr std::int32_t kScanSide = 100;  // from this side on a missed island is searched for
    const std::uint64_t target = own_target(m_s);
    std::uint64_t placed = 0;
    for (const Planned& p : m_islands) {
        placed += std::uint64_t(p.rect.w) * std::uint64_t(p.rect.h);
    }
    const auto w = static_cast<std::int32_t>(m_s.width);
    const auto h = static_cast<std::int32_t>(m_s.height);
    const auto edge = static_cast<std::int32_t>(kEdge);
    const auto place = [&](Dims first, bool search) {
        for (std::optional<Dims> d = first; d; d = smaller(*d)) {
            if (w <= 2 * edge + d->w || h <= 2 * edge + d->h) {
                continue;
            }
            std::optional<Rect> found;
            for (std::uint32_t t = 0; t < kTries && !found; ++t) {
                const Rect rect{edge + static_cast<std::int32_t>(
                                           m_random.below(static_cast<std::uint32_t>(w - 2 * edge - d->w + 1))),
                                edge + static_cast<std::int32_t>(
                                           m_random.below(static_cast<std::uint32_t>(h - 2 * edge - d->h + 1))),
                                d->w, d->h};
                if (fits(rect)) {
                    found = rect;
                }
            }
            if (!found && search && std::max(d->w, d->h) >= kScanSide) {
                // A large island of the first draws that random places missed: every place on a
                // lattice of a sixth of its side, one of those with room at random, before it is
                // tried smaller.
                const std::int32_t step = std::max<std::int32_t>(4, std::min(d->w, d->h) / 6);
                std::vector<Rect> room;
                for (std::int32_t y = edge; y + d->h <= h - edge; y += step) {
                    for (std::int32_t x = edge; x + d->w <= w - edge; x += step) {
                        const Rect rect{x, y, d->w, d->h};
                        if (fits(rect)) {
                            room.push_back(rect);
                        }
                    }
                }
                if (!room.empty()) {
                    found = room[m_random.below(static_cast<std::uint32_t>(room.size()))];
                }
            }
            if (found) {
                Planned p;
                p.rect = *found;
                p.size = d->size;
                p.own = true;
                m_islands.push_back(p);
                placed += std::uint64_t(found->w) * std::uint64_t(found->h);
                return true;
            }
        }
        return false;
    };

    std::vector<Dims> wanted;
    std::uint64_t drawn = placed;
    while (drawn < target && m_islands.size() + wanted.size() < kMaxPlannedIslands) {
        wanted.push_back(draw_dims());
        drawn += std::uint64_t(wanted.back().w) * std::uint64_t(wanted.back().h);
    }
    std::stable_sort(wanted.begin(), wanted.end(), [](const Dims& a, const Dims& b) { return a.w * a.h > b.w * b.h; });
    for (const Dims& d : wanted) {
        if (placed >= target || m_islands.size() >= kMaxPlannedIslands) {
            break;
        }
        place(d, true);
    }
    std::uint32_t misses = 0;
    while (placed < target && m_islands.size() < kMaxPlannedIslands && misses < kMisses) {
        misses = place(draw_dims(), false) ? 0 : misses + 1;
    }
    if (warn_full && placed * 10 < target * 9) {
        const std::string share = std::to_string(placed * 100 / std::max<std::uint64_t>(target, 1)) + " % of the land";
        if (m_islands.size() >= kMaxPlannedIslands) {
            // An island's number is a byte in its tiles: a large map of small islands runs out first.
            warn("a world has at most " + std::to_string(kMaxPlannedIslands) + " islands, " + share +
                 "; larger islands fill more");
        } else {
            warn("the map is full after " + std::to_string(m_islands.size()) + " islands, " + share);
        }
    }
}

void Planner::choose_villages() {
    // Native villages on islands of their own shape, as many as the original's four per 18
    // islands: not on a home, with room for a village of the templates (the generator moves the
    // village part of a native template onto the island).
    const std::uint32_t villages = village_count(m_s, static_cast<std::uint32_t>(m_islands.size()));
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < m_islands.size(); ++i) {
        const Planned& p = m_islands[i];
        if (p.own && !p.home && std::min(p.rect.w, p.rect.h) >= kVillageSide) {
            candidates.push_back(i);
        }
    }
    for (std::uint32_t k = 0; k < villages && !candidates.empty(); ++k) {
        const std::size_t pick = m_random.below(static_cast<std::uint32_t>(candidates.size()));
        m_islands[candidates[pick]].village = true;
        candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(pick));
    }
}

void Planner::choose_volcanoes() {
    // Volcanoes are laid on islands of their own shape, as many as the setting allows: not on a
    // home, and only where there is room for the mountain in its ring of slopes.
    const std::uint32_t volcanoes = volcano_limit(m_s);
    std::vector<std::size_t> volcano_islands;
    for (std::size_t i = 0; i < m_islands.size(); ++i) {
        const Rect& r = m_islands[i].rect;
        if (m_islands[i].own && !m_islands[i].home && !m_islands[i].village && std::min(r.w, r.h) >= kVolcanoSide) {
            volcano_islands.push_back(i);
        }
    }
    for (std::uint32_t k = 0; k < volcanoes && !volcano_islands.empty(); ++k) {
        const std::size_t pick = m_random.below(static_cast<std::uint32_t>(volcano_islands.size()));
        m_islands[volcano_islands[pick]].volcano = true;
        volcano_islands.erase(volcano_islands.begin() + static_cast<std::ptrdiff_t>(pick));
    }
}

std::vector<std::size_t> Planner::in_reach(std::size_t start) const {
    std::vector<std::size_t> out;
    const std::int64_t r2 = std::int64_t{m_reach} * m_reach;
    for (std::size_t i = 0; i < m_islands.size(); ++i) {
        const Rect& r = m_islands[i].rect;
        if (distance2(r.cx(), r.cy(), m_starts[start].x, m_starts[start].y) <= r2) {
            out.push_back(i);
        }
    }
    return out;
}

bool Planner::place_for_climate(std::size_t start, std::uint8_t climate) {
    // An island near the start whose centre lies in a band that allows the climate: directions
    // towards that band first (north up), from far to near, middle islands before small ones.
    const Point& st = m_starts[start];
    const std::int32_t toward = climate == climate::kNorth ? 48 : 16;  // 1/64 turns: up or down
    const Dims first =
        free_sizes()
            ? own_dims(std::clamp<std::int32_t>(50, std::max<std::int32_t>(shapes::kMinSide, m_s.landscape.min_side),
                                                std::max<std::int32_t>(shapes::kMinSide, m_s.landscape.max_side)))
            : class_dims(2);
    for (std::optional<Dims> dims = first; dims; dims = smaller(*dims)) {
        const std::int32_t cw = dims->w;
        const std::int32_t ch = dims->h;
        for (auto r = static_cast<std::int32_t>(m_reach * 95 / 100); r >= static_cast<std::int32_t>(m_reach * 25 / 100);
             r -= 3) {
            for (std::int32_t step = 0; step <= 32; ++step) {
                const std::int32_t a = toward + ((step % 2 == 0) ? step / 2 : -(step + 1) / 2);
                const std::int32_t cx = st.x + r * cosine(a) / 10000;
                const std::int32_t cy = st.y + r * sine(a) / 10000;
                const Rect rect{cx - cw / 2, cy - ch / 2, cw, ch};
                const std::uint8_t b = band(rect);
                if ((b != kEither && b != climate) || !fits(rect)) {
                    continue;
                }
                Planned p;
                p.rect = rect;
                p.size = dims->size;
                p.climate = climate;
                p.own = free_sizes();
                m_islands.push_back(p);
                return true;
            }
        }
    }
    return false;
}

void Planner::decide_climates() {
    std::vector<bool> free(m_islands.size());
    for (std::size_t i = 0; i < m_islands.size(); ++i) {
        const std::uint8_t b = band(m_islands[i].rect);
        free[i] = b == kEither;
        m_islands[i].climate = b == kEither ? static_cast<std::uint8_t>(m_random.below(climate::kCount)) : b;
    }
    if (m_s.starts == WorldSettings::Starts::Together) {
        return;
    }
    // Every start needs both climates within reach: the six crops grow three in each. A free
    // island near the start changes its climate if no other start then lacks it (the home island
    // last); failing that, one more island goes where its band allows the climate. A second
    // northern island is wished for (sugar and wine never share an island in the original's
    // passes), but only by changing climates.
    std::array<std::uint32_t, 2> kNeeded = {1, 1};
    const auto count = [&](std::size_t s, std::uint8_t climate, std::optional<std::size_t> without) {
        std::uint32_t n = 0;
        for (const std::size_t i : in_reach(s)) {
            n += m_islands[i].climate == climate && i != without ? 1U : 0U;
        }
        return n;
    };
    const auto may_flip = [&](std::size_t i) {
        const std::uint8_t old = m_islands[i].climate;
        for (std::size_t other = 0; other < m_starts.size(); ++other) {
            const std::vector<std::size_t> near = in_reach(other);
            if (std::find(near.begin(), near.end(), i) != near.end() && count(other, old, i) < kNeeded[old]) {
                return false;
            }
        }
        return true;
    };
    for (std::size_t s = 0; s < m_starts.size(); ++s) {
        for (std::uint8_t climate = 0; climate < climate::kCount; ++climate) {
            while (count(s, climate, std::nullopt) < kNeeded[climate]) {
                std::optional<std::size_t> flip;
                for (const std::size_t i : in_reach(s)) {
                    if (!free[i] || m_islands[i].climate == climate || !may_flip(i)) {
                        continue;
                    }
                    if (!flip || (m_islands[*flip].home && !m_islands[i].home) ||
                        (m_islands[*flip].home == m_islands[i].home && m_islands[i].size > m_islands[*flip].size)) {
                        flip = i;
                    }
                }
                if (flip) {
                    m_islands[*flip].climate = climate;
                    continue;
                }
                if (!place_for_climate(s, climate)) {
                    // No room either: an island of the other band takes the climate after all.
                    std::optional<std::size_t> any;
                    for (const std::size_t i : in_reach(s)) {
                        if (m_islands[i].climate != climate && may_flip(i) && (!any || m_islands[*any].home)) {
                            any = i;
                        }
                    }
                    if (any) {
                        m_islands[*any].climate = climate;
                        continue;
                    }
                    warn("start " + std::to_string(s) + " has no island of the " +
                         (climate == climate::kNorth ? "north" : "south") + " within reach");
                    break;
                }
                free.push_back(band(m_islands.back().rect) == kEither);
            }
        }
    }
    kNeeded = {2, 1};
    for (std::size_t s = 0; s < m_starts.size(); ++s) {
        if (count(s, climate::kNorth, std::nullopt) >= 2) {
            continue;
        }
        for (const std::size_t i : in_reach(s)) {
            if (free[i] && m_islands[i].climate == climate::kSouth && may_flip(i)) {
                m_islands[i].climate = climate::kNorth;
                break;
            }
        }
    }
}

void Planner::choose_templates() {
    // A template of the island's climate and class that no other island has; homes take one
    // without a volcano and with ore mountains; volcano islands up to the setting.
    const std::uint32_t limit = volcano_limit(m_s);
    auto volcanoes = static_cast<std::uint32_t>(
        std::count_if(m_islands.begin(), m_islands.end(), [](const Planned& p) { return p.own && p.volcano; }));
    std::vector<const TemplateInfo*> used;
    // Homes first: they have the strictest wishes.
    std::vector<std::size_t> order(m_islands.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_partition(order.begin(), order.end(), [&](std::size_t i) { return m_islands[i].home; });
    for (const std::size_t i : order) {
        Planned& p = m_islands[i];
        if (p.own) {
            continue;
        }
        std::vector<const TemplateInfo*> all;
        for (const TemplateInfo& t : m_templates) {
            if (t.climate == p.climate && t.size == p.size &&
                (t.package || t.number < kTemplateCount[t.climate][t.size])) {
                all.push_back(&t);
            }
        }
        if (all.empty()) {
            continue;  // the generator reports the missing file
        }
        const auto is_used = [&](const TemplateInfo* t) {
            return std::find(used.begin(), used.end(), t) != used.end();
        };
        // Wishes from strict to none.
        const auto pick = [&](auto&& ok) -> const TemplateInfo* {
            std::vector<const TemplateInfo*> c;
            for (const TemplateInfo* t : all) {
                if (ok(t)) {
                    c.push_back(t);
                }
            }
            return c.empty() ? nullptr : c[m_random.below(static_cast<std::uint32_t>(c.size()))];
        };
        const bool volcano_allowed = volcanoes < limit;
        const TemplateInfo* chosen = nullptr;
        if (p.home) {
            chosen = pick([&](const TemplateInfo* t) {
                return !is_used(t) && !t->volcano && t->big_mountains >= 1 &&
                       t->big_mountains + t->small_mountains >= 2;
            });
            if (chosen == nullptr) {
                chosen = pick([&](const TemplateInfo* t) {
                    return !is_used(t) && !t->volcano && t->big_mountains + t->small_mountains >= 1;
                });
            }
        }
        if (chosen == nullptr) {
            chosen = pick([&](const TemplateInfo* t) { return !is_used(t) && (volcano_allowed || !t->volcano); });
        }
        if (chosen == nullptr) {
            chosen = pick([&](const TemplateInfo* t) { return volcano_allowed || !t->volcano; });
        }
        if (chosen == nullptr) {
            // Any of them (the same draw as a pick that takes every template; `all` is not empty).
            chosen = all[m_random.below(static_cast<std::uint32_t>(all.size()))];
        }
        p.template_number = chosen->number;
        used.push_back(chosen);
        volcanoes += chosen->volcano ? 1U : 0U;
    }
}

WorldPlan Planner::finish() {
    // Island numbers as in the original's plans: small classes first, then from the top left.
    std::vector<std::size_t> order(m_islands.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const Planned& pa = m_islands[a];
        const Planned& pb = m_islands[b];
        if (pa.size != pb.size) {
            return pa.size < pb.size;
        }
        if (pa.rect.y != pb.rect.y) {
            return pa.rect.y < pb.rect.y;
        }
        return pa.rect.x < pb.rect.x;
    });
    std::vector<std::uint8_t> number(m_islands.size());
    WorldPlan out;
    out.width = m_s.width;
    out.height = m_s.height;
    out.shared_start = m_s.starts == WorldSettings::Starts::Together && m_s.players > 1;
    ScenarioPlan& plan = out.plan;
    std::array<std::uint32_t, climate::kCount> per_climate{};
    for (std::size_t k = 0; k < order.size(); ++k) {
        const Planned& p = m_islands[order[k]];
        number[order[k]] = static_cast<std::uint8_t>(k);
        ScenarioPlan::Island e;
        e.climate = p.climate;
        e.size = p.size;
        e.flags = static_cast<std::uint8_t>((p.home ? kPlanNoVillage : 0) | (p.own ? kPlanOwnShape : 0));
        e.number = static_cast<std::uint8_t>(k);
        e.template_number = p.template_number;
        e.x = static_cast<std::uint32_t>(p.rect.x);
        e.y = static_cast<std::uint32_t>(p.rect.y);
        plan.islands.push_back(e);
        per_climate[p.climate & 1U] += p.home || p.own ? 0U : 1U;
        if (p.own) {
            out.own_islands.push_back(WorldPlan::OwnIsland{e.number, static_cast<std::uint16_t>(p.rect.w),
                                                           static_cast<std::uint16_t>(p.rect.h), p.volcano, p.village});
        }
    }

    // Villages: four per 18 islands as the original, shared by the climates' island counts.
    const auto islands = static_cast<std::uint32_t>(m_islands.size());
    const std::uint32_t villages = village_count(m_s, islands);
    const std::uint32_t free_islands = per_climate[0] + per_climate[1];
    plan.natives[0] = free_islands != 0 ? (villages * per_climate[0] + free_islands / 2) / free_islands : 0;
    plan.natives[1] = villages > plan.natives[0] ? villages - plan.natives[0] : 0;

    // Ore, crops and treasures of the level, for as many islands and players as there are.
    const std::uint8_t d = std::min<std::uint8_t>(m_s.difficulty, 3);
    const std::uint32_t scale =
        std::max(islands * 100 / kPlanIslands, std::uint32_t{m_s.players} * 100 / kPlanPlayers);  // percent
    const auto scaled = [&](std::uint32_t base) {
        return static_cast<std::uint16_t>(std::max<std::uint32_t>(1, (base * scale + 50) / 100));
    };
    // Islands of their own size: the veins also by the land, so that a world of few giants has as
    // many per tile of land as the original's (whose large islands take more veins each).
    std::uint32_t ore_scale = scale;
    if (free_sizes()) {
        std::uint64_t land = 0;
        for (const Planned& p : m_islands) {
            land += std::uint64_t(p.rect.w) * std::uint64_t(p.rect.h) * m_s.landscape.fill() / 1000;
        }
        ore_scale = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(std::max<std::uint64_t>(scale, land * 100 / kPlanLand), kMaxOreScale));
    }
    const auto ores_scaled = [&](std::uint32_t base) {
        return static_cast<std::uint16_t>(std::max<std::uint32_t>(1, (base * ore_scale + 50) / 100));
    };
    plan.max_crops = 2;
    plan.gold_min_size = 1;
    plan.gold_max_size = 2;
    plan.ores[0] = ScenarioPlan::Resource{id::kOreMine, ores_scaled(kBigIron[d]), 0};
    plan.ores[1] = ScenarioPlan::Resource{id::kOreMine, ores_scaled(kSmallIron[d]), 1};
    plan.ores[2] = ScenarioPlan::Resource{id::kGoldMine, ores_scaled(kGold[d]), 2};
    if (m_s.gold_islands != 0) {
        plan.ores[2].count = m_s.gold_islands;
        out.gold_islands = m_s.gold_islands;
    }
    out.shapes = m_s.shapes;
    out.seed = m_s.seed;
    out.landscape = m_s.landscape;
    for (std::size_t c = 0; c < kCropBuildings.size(); ++c) {
        plan.crops[c] = ScenarioPlan::Resource{kCropBuildings[c], scaled(kCropCounts[d][c]), 0};
    }
    plan.treasures[0] = ScenarioPlan::Resource{id::kGoldsmith, scaled(kTreasures[d]), 0};

    // Starts: all at one place with the original's offsets, or one area per player. Separate
    // areas go to the players in a random order (Fisher-Yates), else the first player, the human
    // in a single-player game, always got the same side of the map. The order has a stream of its
    // own, so a seed still gives the same islands, and every computer of a multiplayer game the
    // same order.
    std::vector<std::size_t> area_of(m_starts.size());
    for (std::size_t i = 0; i < area_of.size(); ++i) {
        area_of[i] = i;
    }
    Random draw_area(m_s.seed, kStartOrderStream);
    for (std::size_t i = area_of.size(); i > 1; --i) {
        std::swap(area_of[i - 1], area_of[draw_area.below(static_cast<std::uint32_t>(i))]);
    }
    static constexpr std::array<Point, 8> kTogether = {
        {{0, 0}, {0, -7}, {-5, 5}, {-4, -1}, {5, 4}, {6, -5}, {-7, -6}, {2, 8}}};
    for (std::size_t p = 0; p < m_s.players; ++p) {
        StartArea a;
        if (m_s.starts == WorldSettings::Starts::Together) {
            const Point& o = kTogether[p % kTogether.size()];
            const auto ring = static_cast<std::int32_t>(p / kTogether.size()) * 3;
            a.x = static_cast<std::uint32_t>(m_starts[0].x + o.x + (o.x >= 0 ? ring : -ring));
            a.y = static_cast<std::uint32_t>(m_starts[0].y + o.y);
            a.home_island = 0xFF;
        } else {
            const std::size_t i = area_of[p];
            a.x = static_cast<std::uint32_t>(m_starts[i].x);
            a.y = static_cast<std::uint32_t>(m_starts[i].y);
            a.home_island = m_home[i] >= 0 ? number[static_cast<std::size_t>(m_home[i])] : std::uint8_t{0xFF};
        }
        a.reach = m_reach;
        out.starts.push_back(a);
    }
    return out;
}

core::Result<WorldPlan> Planner::run() {
    if (m_s.width < WorldSettings::kMinSide || m_s.height < WorldSettings::kMinSide ||
        m_s.width > WorldSettings::kMaxSide || m_s.height > WorldSettings::kMaxSide) {
        return core::make_error(core::ErrorCode::LimitExceeded, "a world side of " +
                                                                    std::to_string(WorldSettings::kMinSide) + " to " +
                                                                    std::to_string(WorldSettings::kMaxSide) + " tiles");
    }
    // The players add_players places, not more (LC-1).
    if (m_s.players == 0 || m_s.players > kMaxPlayers) {
        return core::make_error(core::ErrorCode::LimitExceeded, "1 to " + std::to_string(kMaxPlayers) + " players");
    }
    if (m_templates.empty()) {
        return core::make_error(core::ErrorCode::NotFound, "no island templates in the game directory");
    }
    std::uint32_t density = 100;
    switch (m_s.islands) {
        case Amount::None:
        case Amount::Few:
            density = 70;
            break;
        case Amount::Normal:
            density = 100;
            break;
        case Amount::Many:
            density = 135;
            break;
    }
    const std::uint64_t area = std::uint64_t{m_s.width} * m_s.height;
    auto total = static_cast<std::uint32_t>((area * density / 100 + kTilesPerIsland / 2) / kTilesPerIsland);
    if (free_sizes()) {
        // As many as the land takes at the middle size (for the islands near the starts).
        const std::uint64_t side = (std::uint64_t{m_s.landscape.min_side} + m_s.landscape.max_side) / 2;
        total = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(own_target(m_s) / std::max<std::uint64_t>(side * side, 1), kMaxPlannedIslands));
    }
    total = std::clamp<std::uint32_t>(total, std::uint32_t{m_s.players} + 2U, kMaxPlannedIslands);

    place_starts();
    place_homes();
    place_locals(total);
    decide_climates();  // while there is still room for an island a start needs
    if (free_sizes()) {
        place_land(true);
        choose_villages();
        choose_volcanoes();
    } else {
        place_rest(total);
    }
    for (Planned& island : m_islands) {
        if (island.climate == kEither) {
            const std::uint8_t b = band(island.rect);
            island.climate = b == kEither ? static_cast<std::uint8_t>(m_random.below(climate::kCount)) : b;
        }
    }
    choose_templates();
    return finish();
}

}  // namespace

std::uint16_t WorldSettings::Landscape::fill() const {
    // 560 per mille (the templates' middle) up to 25 % land, then 9 more for each percent: at 45 %
    // the rectangles take about 61 % of the map. Branched islands have 120 less.
    const int l = std::clamp<int>(land, kMinLand, kMaxLand);
    const int compact = 560 + 9 * std::max(0, l - 25);
    return static_cast<std::uint16_t>(outline == Outline::Branched ? compact - 120 : compact);
}

namespace {

using L = WorldSettings::Landscape;

L make_landscape(std::string_view name, std::uint8_t land, std::uint16_t min_side, std::uint16_t max_side,
                 L::Spread spread, L::Gap gap, shapes::Roughness coast, L::Outline outline) {
    L l;
    l.name = name;
    l.land = land;
    l.min_side = min_side;
    l.max_side = max_side;
    l.spread = spread;
    l.gap = gap;
    l.coast = coast;
    l.outline = outline;
    return l;
}

}  // namespace

const std::array<LandscapePreset, 5>& landscape_presets() {
    static const std::array<LandscapePreset, 5> presets = {{
        {"varied",
         make_landscape("varied", L{}.land, L{}.min_side, L{}.max_side, L{}.spread, L{}.gap, L{}.coast, L{}.outline)},
        {"archipelago", make_landscape("archipelago", 20, 16, 60, L::Spread::Even, L::Gap::Narrow,
                                       shapes::Roughness::Rugged, L::Outline::Round)},
        {"continents", make_landscape("continents", 35, 150, 400, L::Spread::Even, L::Gap::Normal,
                                      shapes::Roughness::Natural, L::Outline::Round)},
        {"giants", make_landscape("giants", 30, 25, 500, L::Spread::FewGiants, L::Gap::Wide, shapes::Roughness::Natural,
                                  L::Outline::Round)},
        {"even", make_landscape("even", 20, 50, 60, L::Spread::Even, L::Gap::Normal, shapes::Roughness::Smooth,
                                L::Outline::Round)},
    }};
    return presets;
}

bool is_landscape_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 192 && std::ranges::none_of(name, [](const char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7F;
    });
}

bool same_islands(const WorldSettings::Landscape& a, const WorldSettings::Landscape& b) {
    WorldSettings::Landscape named = b;
    named.name = a.name;
    return a == named;
}

std::string_view landscape_name(const WorldSettings::Landscape& landscape) {
    if (!landscape.name.empty()) {
        return landscape.name;
    }
    const auto it = std::ranges::find_if(
        landscape_presets(), [&](const LandscapePreset& p) { return same_islands(p.landscape, landscape); });
    return it != landscape_presets().end() ? it->name : std::string_view{};
}

bool set_landscape(WorldSettings::Landscape& landscape, std::string_view key, std::string_view value) {
    namespace names = landscape_names;
    const auto choice = [value](const std::array<std::string_view, 3>& list) -> std::optional<std::uint8_t> {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i] == value) {
                return static_cast<std::uint8_t>(i);
            }
        }
        return std::nullopt;
    };
    std::uint32_t n = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), n);
    const bool number = error == std::errc{} && end == value.data() + value.size() && !value.empty();
    if (key == "land") {
        if (!number || n < L::kMinLand || n > L::kMaxLand) {
            return false;
        }
        landscape.land = static_cast<std::uint8_t>(n);
    } else if (key == "min" || key == "max") {
        if (!number || n < shapes::kMinSide || n > shapes::kMaxSide) {
            return false;
        }
        const bool min = key == "min";
        (min ? landscape.min_side : landscape.max_side) = static_cast<std::uint16_t>(n);
        if (landscape.min_side > landscape.max_side) {
            (min ? landscape.max_side : landscape.min_side) = static_cast<std::uint16_t>(n);
        }
    } else {
        const std::array<std::string_view, 3>* list = key == "spread"                         ? &names::kSpread
                                                      : key == "gap"                          ? &names::kGap
                                                      : key == "coast"                        ? &names::kCoast
                                                      : key == "outline"                      ? &names::kOutline
                                                      : key == "forest" || key == "mountains" ? &names::kShare
                                                                                              : nullptr;
        const auto i = list != nullptr ? choice(*list) : std::nullopt;
        if (!i) {
            return false;
        }
        if (key == "spread") {
            landscape.spread = static_cast<L::Spread>(*i);
        } else if (key == "gap") {
            landscape.gap = static_cast<L::Gap>(*i);
        } else if (key == "coast") {
            landscape.coast = static_cast<shapes::Roughness>(*i);
        } else if (key == "outline") {
            landscape.outline = static_cast<L::Outline>(*i);
        } else {
            (key == "forest" ? landscape.forest : landscape.mountains) = static_cast<shapes::Share>(*i);
        }
    }
    return true;
}

namespace {

/// Its volcano and ore mountains, which the planner's wishes ask for.
void count_template_features(TemplateInfo& t, const assets::Island& island, const assets::BuildingTable& buildings) {
    const IslandMap map = IslandMap::from_island(island, buildings, nullptr, nullptr);
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const MapCell& cell = map.top(x, y);
            const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
            if (b == nullptr) {
                continue;
            }
            const SubTile sub = map.sub_tile(buildings, x, y);
            if (sub.origin_x != static_cast<std::int32_t>(x) || sub.origin_y != static_cast<std::int32_t>(y)) {
                continue;
            }
            if (b->id == kVolcanoRecordId) {
                t.volcano = true;
            }
            if (b->prod_kind == prod::kOreMountain && b->ware == ware::kOreMountain) {
                if (b->ore_mountain == 0) {
                    ++t.big_mountains;
                } else if (b->ore_mountain == 1) {
                    ++t.small_mountains;
                }
            }
        }
    }
}

/// The first island of the file at `path`, none if it cannot be read.
std::optional<assets::Island> first_island(const std::filesystem::path& path) {
    auto file = assets::ChunkFile::load(path);
    if (!file) {
        return std::nullopt;
    }
    auto islands = assets::load_islands(*file);
    if (!islands || islands->empty()) {
        return std::nullopt;
    }
    return std::move(islands->front());
}

}  // namespace

std::vector<TemplateInfo> island_templates(const assets::GameDirectory& game, const assets::BuildingTable& buildings,
                                           std::span<const PackageIsland> package_islands) {
    std::vector<TemplateInfo> out;
    for (std::uint8_t climate = 0; climate < climate::kCount; ++climate) {
        for (std::uint8_t size = 0; size < templates::kSizeClasses; ++size) {
            for (std::uint16_t number = 0; number < kTemplateCount[climate][size]; ++number) {
                const auto path = game.resolve(template_path(climate, size, number, false));
                if (!path) {
                    continue;
                }
                const auto island = first_island(*path);
                if (!island) {
                    continue;
                }
                TemplateInfo t;
                t.climate = climate;
                t.size = size;
                t.number = number;
                t.width = island->header.width;
                t.height = island->header.height;
                // A native version only counts if it is as large (not SuedNat/big15, D-010).
                if (const auto vpath = game.resolve(template_path(climate, size, number, true))) {
                    if (const auto v = first_island(*vpath)) {
                        t.village =
                            v->header.width == island->header.width && v->header.height == island->header.height;
                    }
                }
                count_template_features(t, *island, buildings);
                out.push_back(t);
            }
        }
    }
    // The packages' islands, numbered after the original's of their climate and class; no villages.
    for (std::size_t i = 0; i < package_islands.size(); ++i) {
        const PackageIsland& p = package_islands[i];
        const auto island = first_island(p.file);
        if (!island) {
            continue;
        }
        TemplateInfo t;
        t.climate = p.climate;
        t.size = p.size;
        t.number = package_template_number(package_islands, i);
        t.width = p.width;
        t.height = p.height;
        t.package = true;
        count_template_features(t, *island, buildings);
        out.push_back(t);
    }
    return out;
}

core::Result<WorldPlan> plan_world(const WorldSettings& settings, const std::vector<TemplateInfo>& templates,
                                   std::vector<std::string>* warnings) {
    Planner planner(settings, templates, warnings);
    return planner.run();
}

WorldSettings fit_landscape(WorldSettings settings) {
    if (settings.shapes != WorldSettings::Shapes::Own || settings.width == 0 || settings.height == 0) {
        return settings;
    }
    const std::uint64_t largest = std::min(settings.landscape.max_side, shapes::kMaxSide);
    const std::uint64_t w = settings.width;
    const std::uint64_t h = settings.height;
    // The factor in per mille: for the shorter side, then for the area (its square root).
    const std::uint64_t by_side = (largest * 1500 + 2 * kEdge * 1000) / std::min(w, h);
    const std::uint64_t area = largest * largest * 16 * 1000 * 1000 / (w * h);
    std::uint64_t by_area = 0;
    while ((by_area + 1) * (by_area + 1) <= area) {
        ++by_area;
    }
    const std::uint64_t factor = std::max<std::uint64_t>({1000, by_side, by_area});
    settings.width = static_cast<std::uint32_t>(std::min<std::uint64_t>(w * factor / 1000, WorldSettings::kMaxSide));
    settings.height = static_cast<std::uint32_t>(std::min<std::uint64_t>(h * factor / 1000, WorldSettings::kMaxSide));
    return settings;
}

WorldSettings settings_for_players(std::uint8_t players) {
    WorldSettings settings;
    settings.players = players;
    if (players > kOriginalPlayers) {
        const double scale = std::sqrt(static_cast<double>(players) / kOriginalPlayers);
        settings.width = static_cast<std::uint32_t>(std::lround(settings.width * scale));
        settings.height = static_cast<std::uint32_t>(std::lround(settings.height * scale));
    }
    return settings;
}

}  // namespace o1602::sim
