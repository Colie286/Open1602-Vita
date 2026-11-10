// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/world_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>

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
constexpr std::uint64_t kPlanStream = 0x5701'D0A1ULL;        // the islands
constexpr std::uint64_t kStartOrderStream = 0x5701'D0A2ULL;  // which player gets which start area

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
};

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
    void place_starts();
    void place_homes();
    void place_locals(std::uint32_t total);
    void place_rest(std::uint32_t total);
    bool place_near(const Point& start, std::uint8_t size, std::uint32_t min_r, std::uint32_t max_r, bool home);
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
    const auto gap = static_cast<std::int32_t>(kGap);
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

std::uint8_t Planner::band(const Rect& r) const {
    // The original: the north is the top third of the map, the south the bottom third
    // (0x0048C613); in between either.
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

bool Planner::place_near(const Point& start, std::uint8_t size, std::uint32_t min_r, std::uint32_t max_r, bool home) {
    const auto cw = static_cast<std::int32_t>(kClassWidth[size]);
    const auto ch = static_cast<std::int32_t>(kClassHeight[size]);
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
            p.size = size;
            p.home = home;
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
    const std::uint8_t wanted = m_s.sizes == WorldSettings::Sizes::Small ? 2 : 3;
    for (std::size_t i = 0; i < m_starts.size(); ++i) {
        for (std::uint8_t size = wanted;; --size) {
            if (place_near(m_starts[i], size, kShipClear, kShipClear + 14, true)) {
                m_home[i] = static_cast<std::int32_t>(m_islands.size() - 1);
                break;
            }
            if (size == 0) {
                warn("no room for the home island of player " + std::to_string(i));
                break;
            }
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
    std::vector<std::uint8_t> sizes;
    for (std::uint32_t k = 0; k < per_start; ++k) {
        sizes.push_back(std::min<std::uint8_t>(draw_size(), 3));
    }
    std::sort(sizes.rbegin(), sizes.rend());  // the large ones first, while there is room
    for (const std::uint8_t size : sizes) {
        for (const Point& start : m_starts) {
            if (!place_near(start, size, m_reach * 40 / 100, m_reach * 90 / 100, false) && size > 0) {
                place_near(start, static_cast<std::uint8_t>(size - 1), m_reach * 40 / 100, m_reach * 90 / 100, false);
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
    for (std::uint8_t size = 2;; --size) {
        const auto cw = static_cast<std::int32_t>(kClassWidth[size]);
        const auto ch = static_cast<std::int32_t>(kClassHeight[size]);
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
                p.size = size;
                p.climate = climate;
                m_islands.push_back(p);
                return true;
            }
        }
        if (size == 0) {
            return false;
        }
    }
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
    std::uint32_t volcano_limit = 0;
    const std::uint32_t players = m_s.players;
    switch (m_s.volcanoes) {
        case Amount::None:
            volcano_limit = 0;
            break;
        case Amount::Few:
            volcano_limit = std::max<std::uint32_t>(1, players / 2);
            break;
        case Amount::Normal:
            volcano_limit = players;
            break;
        case Amount::Many:
            volcano_limit = 2 * players;
            break;
    }
    volcano_limit = std::min<std::uint32_t>(volcano_limit, 10);  // at most 20 craters in a world
    std::uint32_t volcanoes = 0;
    std::vector<const TemplateInfo*> used;
    // Homes first: they have the strictest wishes.
    std::vector<std::size_t> order(m_islands.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_partition(order.begin(), order.end(), [&](std::size_t i) { return m_islands[i].home; });
    for (const std::size_t i : order) {
        Planned& p = m_islands[i];
        std::vector<const TemplateInfo*> all;
        for (const TemplateInfo& t : m_templates) {
            if (t.climate == p.climate && t.size == p.size && t.number < kTemplateCount[t.climate][t.size]) {
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
        const bool volcano_allowed = volcanoes < volcano_limit;
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
        e.flags = p.home ? kPlanNoVillage : 0;
        e.number = static_cast<std::uint8_t>(k);
        e.template_number = p.template_number;
        e.x = static_cast<std::uint32_t>(p.rect.x);
        e.y = static_cast<std::uint32_t>(p.rect.y);
        plan.islands.push_back(e);
        per_climate[p.climate & 1U] += p.home ? 0U : 1U;
    }

    // Villages: four per 18 islands as the original, shared by the climates' island counts.
    const auto islands = static_cast<std::uint32_t>(m_islands.size());
    std::uint32_t per18 = 0;
    switch (m_s.natives) {
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
    const std::uint32_t villages = (islands * per18 + kPlanIslands / 2) / kPlanIslands;
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
    plan.max_crops = 2;
    plan.gold_min_size = 1;
    plan.gold_max_size = 2;
    plan.ores[0] = ScenarioPlan::Resource{id::kOreMine, scaled(kBigIron[d]), 0};
    plan.ores[1] = ScenarioPlan::Resource{id::kOreMine, scaled(kSmallIron[d]), 1};
    plan.ores[2] = ScenarioPlan::Resource{id::kGoldMine, scaled(kGold[d]), 2};
    if (m_s.gold_islands != 0) {
        plan.ores[2].count = m_s.gold_islands;
        out.gold_islands = m_s.gold_islands;
    }
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
    total = std::clamp<std::uint32_t>(total, std::uint32_t{m_s.players} + 2U, kMaxPlannedIslands);

    place_starts();
    place_homes();
    place_locals(total);
    decide_climates();  // while there is still room for an island a start needs
    place_rest(total);
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

std::vector<TemplateInfo> island_templates(const assets::GameDirectory& game, const assets::BuildingTable& buildings) {
    std::vector<TemplateInfo> out;
    for (std::uint8_t climate = 0; climate < climate::kCount; ++climate) {
        for (std::uint8_t size = 0; size < templates::kSizeClasses; ++size) {
            for (std::uint16_t number = 0; number < kTemplateCount[climate][size]; ++number) {
                const auto path = game.resolve(template_path(climate, size, number, false));
                if (!path) {
                    continue;
                }
                auto file = assets::ChunkFile::load(*path);
                if (!file) {
                    continue;
                }
                auto islands = assets::load_islands(*file);
                if (!islands || islands->empty()) {
                    continue;
                }
                const assets::Island& island = islands->front();
                TemplateInfo t;
                t.climate = climate;
                t.size = size;
                t.number = number;
                t.width = island.header.width;
                t.height = island.header.height;
                // A native version only counts if it is as large (not SuedNat/big15, D-010).
                if (const auto vpath = game.resolve(template_path(climate, size, number, true))) {
                    if (auto vfile = assets::ChunkFile::load(*vpath)) {
                        if (auto v = assets::load_islands(*vfile); v && !v->empty()) {
                            t.village = v->front().header.width == island.header.width &&
                                        v->front().header.height == island.header.height;
                        }
                    }
                }
                const IslandMap map = IslandMap::from_island(island, buildings, nullptr, nullptr);
                for (std::uint32_t y = 0; y < map.height(); ++y) {
                    for (std::uint32_t x = 0; x < map.width(); ++x) {
                        const MapCell& cell = map.top(x, y);
                        const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
                        if (b == nullptr) {
                            continue;
                        }
                        const SubTile sub = map.sub_tile(buildings, x, y);
                        if (sub.origin_x != static_cast<std::int32_t>(x) ||
                            sub.origin_y != static_cast<std::int32_t>(y)) {
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
                out.push_back(t);
            }
        }
    }
    return out;
}

core::Result<WorldPlan> plan_world(const WorldSettings& settings, const std::vector<TemplateInfo>& templates,
                                   std::vector<std::string>* warnings) {
    Planner planner(settings, templates, warnings);
    return planner.run();
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
