// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/island_shapes.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "o1602/sim/random.hpp"

namespace o1602::sim::shapes {

namespace {

// Directions 0..7: N, NE, E, SE, S, SW, W, NW; north is the top (smaller y).
constexpr std::array<int, 8> kDirX = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> kDirY = {-1, -1, 0, 1, 1, 1, 0, -1};

constexpr bool has(std::uint8_t neighbours, int dir) noexcept {
    return ((neighbours >> (dir & 7)) & 1U) != 0;
}

// Land never comes closer to the edge than this, so the surf and a tile of open sea fit around it.
constexpr int kMargin = 2;
// Attempts at a shape before the plain fallback.
constexpr std::uint32_t kAttempts = 12;
constexpr std::uint32_t kSmoothRounds = 64;

/// A grid of land (1) and sea (0); outside is sea.
class LandMap {
public:
    LandMap(int width, int height) : m_width(width), m_height(height), m_cells(cell_count(width, height), 0) {}

    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] bool inside(int x, int y) const noexcept { return x >= 0 && y >= 0 && x < m_width && y < m_height; }
    [[nodiscard]] bool land(int x, int y) const noexcept { return inside(x, y) && m_cells[index(x, y)] != 0; }
    void set(int x, int y, bool land) noexcept {
        if (inside(x, y)) {
            m_cells[index(x, y)] = land ? 1 : 0;
        }
    }
    [[nodiscard]] std::uint8_t neighbours(int x, int y) const noexcept {
        std::uint8_t mask = 0;
        for (int d = 0; d < 8; ++d) {
            if (land(x + kDirX[static_cast<std::size_t>(d)], y + kDirY[static_cast<std::size_t>(d)])) {
                mask = static_cast<std::uint8_t>(mask | (1U << d));
            }
        }
        return mask;
    }
    [[nodiscard]] std::size_t land_count() const noexcept {
        return static_cast<std::size_t>(std::count(m_cells.begin(), m_cells.end(), std::uint8_t{1}));
    }

private:
    static std::size_t cell_count(int width, int height) noexcept {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
    [[nodiscard]] std::size_t index(int x, int y) const noexcept {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x);
    }

    int m_width;
    int m_height;
    std::vector<std::uint8_t> m_cells;
};

/// Value noise: random values on a coarse lattice, blended in between; 0..1023.
class Noise {
public:
    Noise(Random& random, int width, int height, int spacing)
        : m_spacing(spacing), m_columns(width / spacing + 2), m_rows(height / spacing + 2) {
        m_values.resize(static_cast<std::size_t>(m_columns) * static_cast<std::size_t>(m_rows));
        for (int& v : m_values) {
            v = static_cast<int>(random.below(1024));
        }
    }

    [[nodiscard]] int at(int x, int y) const noexcept {
        const int cx = x / m_spacing;
        const int cy = y / m_spacing;
        const int fx = x % m_spacing;
        const int fy = y % m_spacing;
        const int top = lerp(value(cx, cy), value(cx + 1, cy), fx);
        const int bottom = lerp(value(cx, cy + 1), value(cx + 1, cy + 1), fx);
        return lerp(top, bottom, fy);
    }

private:
    [[nodiscard]] int value(int cx, int cy) const noexcept {
        return m_values[static_cast<std::size_t>(cy) * static_cast<std::size_t>(m_columns) +
                        static_cast<std::size_t>(cx)];
    }
    [[nodiscard]] int lerp(int a, int b, int f) const noexcept { return a + (b - a) * f / m_spacing; }

    int m_spacing;
    int m_columns;
    int m_rows;
    std::vector<int> m_values;
};

// The sea's depth bands: land and surf, light, middle, dark.
constexpr int kShore = 0;
constexpr int kLight = 1;
constexpr int kMiddle = 2;
constexpr int kDark = 3;

/// The depth band of every tile: by its distance to the land (Chebyshev, as the templates' bands
/// run): 1 the surf, 2 light, 3 and 4 middle, from 5 dark, each border moved by a tile here and
/// there by coarse noise; never dark next to the surf.
std::vector<std::uint8_t> sea_depth(const LandMap& map, Random& random) {
    const int w = map.width();
    const int h = map.height();
    constexpr int kFar = 8;
    std::vector<std::uint8_t> distance(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), kFar);
    std::vector<std::pair<int, int>> wave;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (map.land(x, y)) {
                distance[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = 0;
                wave.emplace_back(x, y);
            }
        }
    }
    for (int d = 1; d < kFar && !wave.empty(); ++d) {
        std::vector<std::pair<int, int>> next;
        for (const auto& [x, y] : wave) {
            for (std::size_t k = 0; k < kDirX.size(); ++k) {
                const int nx = x + kDirX[k];
                const int ny = y + kDirY[k];
                if (!map.inside(nx, ny)) {
                    continue;
                }
                auto& v =
                    distance[static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) + static_cast<std::size_t>(nx)];
                if (v > d) {
                    v = static_cast<std::uint8_t>(d);
                    next.emplace_back(nx, ny);
                }
            }
        }
        wave = std::move(next);
    }
    const Noise wobble(random, w, h, 5);
    std::vector<std::uint8_t> band(distance.size(), kShore);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const int d = distance[i];
            if (d <= 1) {
                continue;
            }
            const int v = wobble.at(x, y);
            // Towards the rectangle's edge the sea deepens, so that the bands end in the dark open
            // sea around the island and not in a straight line along the edge.
            const int rim = std::min({x, y, w - 1 - x, h - 1 - y});
            const int level = std::max(d + (v < 340 ? -1 : v > 683 ? 1 : 0), 5 - 2 * rim);
            int b = level <= 2 ? kLight : level <= 4 ? kMiddle : kDark;
            if (d == 2) {
                b = std::min(b, kMiddle);  // never dark next to the surf
            }
            band[i] = static_cast<std::uint8_t>(b);
        }
    }
    return band;
}

template <std::size_t N>
std::uint32_t pick(Random& random, const std::array<std::uint32_t, N>& ids) {
    return ids[random.below(static_cast<std::uint32_t>(N))];
}

/// Land blocks of 2 x 2 tiles: noise plus a falloff from the centre, as much land as `share`
/// (per mille) of the blocks. `strength`: the noise against the falloff, percent; `spread`: the
/// lattice of the noise in parts of the shorter side (more parts, smaller features).
LandMap block_land(Random& random, int blocks_x, int blocks_y, std::uint32_t share, int strength, int spread) {
    // Bays and capes of at most about a big template's size, so that a giant is not one round body.
    constexpr int kWidestLattice = 24;
    const int spacing = std::clamp(std::min(blocks_x, blocks_y) / spread, 3, kWidestLattice);
    const Noise coarse(random, blocks_x, blocks_y, spacing);
    const Noise fine(random, blocks_x, blocks_y, std::max(2, spacing / 2));
    // Large islands: finer octaves down to a few blocks, each of half the weight, so that their
    // coasts do not run straight between the lattice points of the coarse noise.
    std::vector<Noise> detail;
    for (int s = spacing / 4; s >= 3; s /= 2) {
        detail.emplace_back(random, blocks_x, blocks_y, s);
    }
    // Half-block units, so that the centres of a block and of the island are whole numbers; the
    // ellipse reaches the blocks next to the edge.
    const bool large = std::min(blocks_x, blocks_y) >= 30;  // 60 tiles and more
    constexpr int kSteepFrom = 800;
    const int rx = std::max(1, blocks_x - 2);
    const int ry = std::max(1, blocks_y - 2);
    std::vector<int> scores;
    std::vector<int> grid(static_cast<std::size_t>(blocks_x) * static_cast<std::size_t>(blocks_y),
                          std::numeric_limits<int>::min());
    for (int by = 1; by + 1 < blocks_y; ++by) {
        for (int bx = 1; bx + 1 < blocks_x; ++bx) {
            const int dx = 2 * bx + 1 - blocks_x;
            const int dy = 2 * by + 1 - blocks_y;
            // 0 in the centre, 1024 on the ellipse touching the margin.
            const int falloff = static_cast<int>((std::int64_t{dx} * dx * 1024) / (std::int64_t{rx} * rx) +
                                                 (std::int64_t{dy} * dy * 1024) / (std::int64_t{ry} * ry));
            int sum = 8 * coarse.at(bx, by) + 4 * fine.at(bx, by);
            int weight = 12;
            int part = 2;
            for (const Noise& n : detail) {
                sum += n.at(bx, by) * part;
                weight += part;
                part = std::max(1, part / 2);
            }
            const int noise = sum / weight;
            int score = 1024 - falloff + (noise - 512) * strength / 100;
            if (large) {
                // Large islands: the land falls off steeply towards the ellipse, its radius
                // wandering with the coarse noise, so that a full island neither fills its
                // rectangle nor runs straight along it; the last two blocks stay sea.
                const int wandering = falloff * (1024 + (coarse.at(bx, by) - 512) / 2) / 1024;
                if (wandering > kSteepFrom) {
                    score -= (wandering - kSteepFrom) * (wandering - kSteepFrom) / 128;
                }
                if (std::min({bx - 1, by - 1, blocks_x - 2 - bx, blocks_y - 2 - by}) < 2) {
                    score -= 4096;
                }
            }
            grid[static_cast<std::size_t>(by) * static_cast<std::size_t>(blocks_x) + static_cast<std::size_t>(bx)] =
                score;
            scores.push_back(score);
        }
    }
    LandMap blocks(blocks_x, blocks_y);
    if (scores.empty()) {
        return blocks;
    }
    const std::size_t wanted = std::min(scores.size() - 1, scores.size() * (1000 - share) / 1000);
    std::nth_element(scores.begin(), scores.begin() + static_cast<std::ptrdiff_t>(wanted), scores.end());
    const int threshold = scores[wanted];
    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            blocks.set(bx, by,
                       grid[static_cast<std::size_t>(by) * static_cast<std::size_t>(blocks_x) +
                            static_cast<std::size_t>(bx)] >= threshold);
        }
    }
    return blocks;
}

/// Keeps the largest piece of land (connected through the four sides) and fills the lakes.
void one_island(LandMap& map) {
    const int w = map.width();
    const int h = map.height();
    std::vector<int> piece(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), -1);
    const auto at = [w](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    // Flood fill of everything of one kind from a tile; returns the tiles.
    const auto flood = [&](int sx, int sy, bool land, int label) {
        std::vector<std::pair<int, int>> stack{{sx, sy}};
        std::size_t count = 0;
        piece[at(sx, sy)] = label;
        while (!stack.empty()) {
            const auto [x, y] = stack.back();
            stack.pop_back();
            ++count;
            for (int d = 0; d < 8; d += 2) {
                const int nx = x + kDirX[static_cast<std::size_t>(d)];
                const int ny = y + kDirY[static_cast<std::size_t>(d)];
                if (map.inside(nx, ny) && piece[at(nx, ny)] == -1 && map.land(nx, ny) == land) {
                    piece[at(nx, ny)] = label;
                    stack.emplace_back(nx, ny);
                }
            }
        }
        return count;
    };
    int best = -1;
    std::size_t best_count = 0;
    int label = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (map.land(x, y) && piece[at(x, y)] == -1) {
                const std::size_t count = flood(x, y, true, label);
                if (count > best_count) {
                    best_count = count;
                    best = label;
                }
                ++label;
            }
        }
    }
    // The sea reaching the edge stays sea, every other water and land but the best piece flips.
    const int sea = label;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if ((x == 0 || y == 0 || x == w - 1 || y == h - 1) && !map.land(x, y) && piece[at(x, y)] == -1) {
                flood(x, y, false, sea);
            }
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            map.set(x, y, piece[at(x, y)] != sea && (piece[at(x, y)] == best || !map.land(x, y)));
        }
    }
}

/// Blocks of land sticking out alone become sea, notches of one block in the coast land.
void round_off(LandMap& blocks) {
    for (std::uint32_t round = 0; round < 8; ++round) {
        std::vector<std::pair<int, int>> flip;
        for (int y = 0; y < blocks.height(); ++y) {
            for (int x = 0; x < blocks.width(); ++x) {
                int sides = 0;
                for (int d = 0; d < 8; d += 2) {
                    sides += blocks.land(x + kDirX[static_cast<std::size_t>(d)], y + kDirY[static_cast<std::size_t>(d)])
                                 ? 1
                                 : 0;
                }
                if (blocks.land(x, y) ? sides <= 1 : sides >= 3) {
                    flip.emplace_back(x, y);
                }
            }
        }
        if (flip.empty()) {
            return;
        }
        for (const auto& [x, y] : flip) {
            blocks.set(x, y, !blocks.land(x, y));
        }
    }
}

/// Land touching other land only at a corner of a block, filled in.
void close_corners(LandMap& blocks) {
    for (bool changed = true; changed;) {
        changed = false;
        for (int y = 0; y + 1 < blocks.height(); ++y) {
            for (int x = 0; x + 1 < blocks.width(); ++x) {
                const bool a = blocks.land(x, y);
                const bool b = blocks.land(x + 1, y);
                const bool c = blocks.land(x, y + 1);
                const bool d = blocks.land(x + 1, y + 1);
                if (a == d && b == c && a != b) {
                    blocks.set(x, y, true);
                    blocks.set(x + 1, y, true);
                    blocks.set(x, y + 1, true);
                    blocks.set(x + 1, y + 1, true);
                    changed = true;
                }
            }
        }
    }
}

/// Changes land and sea until every tile has a coast tile: water the templates have no tile for is
/// filled, land too thin for the beach becomes sea, land touching land only at a corner is joined.
bool smooth(LandMap& map) {
    for (std::uint32_t round = 0; round < kSmoothRounds; ++round) {
        std::vector<std::pair<int, int>> to_land;
        std::vector<std::pair<int, int>> to_sea;
        for (int y = 0; y < map.height(); ++y) {
            for (int x = 0; x < map.width(); ++x) {
                const bool land = map.land(x, y);
                const bool edge =
                    x < kMargin || y < kMargin || x >= map.width() - kMargin || y >= map.height() - kMargin;
                if (land && edge) {
                    to_sea.emplace_back(x, y);
                    continue;
                }
                const std::uint8_t n = map.neighbours(x, y);
                if (coast_tile(land, n)) {
                    continue;
                }
                if (!land) {
                    to_land.emplace_back(x, y);
                } else if (has(n, 0) && has(n, 2) && has(n, 4) && has(n, 6)) {
                    for (int d = 1; d < 8; d += 2) {
                        if (!has(n, d)) {
                            to_land.emplace_back(x + kDirX[static_cast<std::size_t>(d)],
                                                 y + kDirY[static_cast<std::size_t>(d)]);
                        }
                    }
                } else {
                    to_sea.emplace_back(x, y);
                }
            }
        }
        if (to_land.empty() && to_sea.empty()) {
            return true;
        }
        for (const auto& [x, y] : to_sea) {
            map.set(x, y, false);
        }
        for (const auto& [x, y] : to_land) {
            map.set(x, y, true);
        }
    }
    return false;
}

/// A land mask that only needs coast tiles of the templates, or nothing.
std::optional<LandMap> land_mask(Random& random, int width, int height, Roughness roughness, Form form,
                                 std::uint16_t land) {
    const int blocks_x = width / 2;
    const int blocks_y = height / 2;
    // The templates are 40..60 % land; branched islands less, their arms reach as far.
    const bool branched = form == Form::Branched;
    const std::uint32_t low = land == 0 ? (branched ? 380U : 480U) : std::clamp<std::uint32_t>(land, 380, 800) - 80U;
    const std::uint32_t share = low + random.below(160);
    constexpr std::array<int, 3> kStrength = {80, 150, 230};  // Smooth, Natural, Rugged
    const int strength = kStrength[static_cast<std::size_t>(roughness)] + (branched ? 60 : 0);
    LandMap blocks = block_land(random, blocks_x, blocks_y, share, strength, branched ? 7 : 4);
    round_off(blocks);
    close_corners(blocks);
    one_island(blocks);
    close_corners(blocks);
    LandMap map(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            map.set(x, y, blocks.land(x / 2, y / 2));
        }
    }
    for (std::uint32_t pass = 0; pass < 4; ++pass) {
        if (!smooth(map)) {
            return std::nullopt;
        }
        const std::size_t before = map.land_count();
        one_island(map);
        if (map.land_count() == before) {
            break;
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!coast_tile(map.land(x, y), map.neighbours(x, y))) {
                return std::nullopt;
            }
        }
    }
    // Land on at least a quarter of the island.
    if (map.land_count() * 4 < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return std::nullopt;
    }
    return map;
}

/// A lake: where it lies, for the rivers.
struct Lake {
    int x = 0;  ///< its centre
    int y = 0;
};

/// Lakes inland: water ringed by beach and surf as the sea's coast, far from the sea, so that
/// they never join it. Each is a blob of 2 x 2 blocks; one that the coast's rules would not keep
/// is left out.
std::vector<Lake> carve_lakes(LandMap& map, Random& random, std::uint8_t count) {
    std::vector<Lake> lakes;
    const int w = map.width();
    const int h = map.height();
    if (count == 0) {
        return lakes;
    }
    constexpr int kFar = 255;
    constexpr std::uint32_t kTries = 40;  // places tried per lake
    const auto at = [w](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    for (std::uint8_t n = 0; n < count; ++n) {
        // How far each land tile is from water (sea and the lakes so far).
        std::vector<std::uint8_t> dry(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        std::vector<std::pair<int, int>> wave;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (map.land(x, y)) {
                    dry[at(x, y)] = kFar;
                } else {
                    wave.emplace_back(x, y);
                }
            }
        }
        for (int d = 1; d < kFar && !wave.empty(); ++d) {
            std::vector<std::pair<int, int>> next;
            for (const auto& [x, y] : wave) {
                for (std::size_t k = 0; k < kDirX.size(); ++k) {
                    const int nx = x + kDirX[k];
                    const int ny = y + kDirY[k];
                    if (map.inside(nx, ny) && dry[at(nx, ny)] > d) {
                        dry[at(nx, ny)] = static_cast<std::uint8_t>(d);
                        next.emplace_back(nx, ny);
                    }
                }
            }
            wave = std::move(next);
        }
        for (std::uint32_t t = 0; t < kTries; ++t) {
            // Half sides of 3..7 tiles, in blocks; at least 10 tiles of land to other water.
            const int rx = 3 + static_cast<int>(random.below(5));
            const int ry = 3 + static_cast<int>(random.below(5));
            const int cx = 2 * static_cast<int>(random.below(static_cast<std::uint32_t>(w / 2)));
            const int cy = 2 * static_cast<int>(random.below(static_cast<std::uint32_t>(h / 2)));
            if (!map.inside(cx, cy) || dry[at(cx, cy)] < std::max(rx, ry) + 10) {
                continue;
            }
            LandMap before = map;
            for (int by = -ry / 2 - 1; by <= ry / 2 + 1; ++by) {
                for (int bx = -rx / 2 - 1; bx <= rx / 2 + 1; ++bx) {
                    // The blocks within the ellipse, its rim frayed by chance.
                    const int ex = 2 * bx + 1;
                    const int ey = 2 * by + 1;
                    const int inside = ex * ex * 1024 / (rx * rx) + ey * ey * 1024 / (ry * ry);
                    if (inside > 1024 || (inside > 600 && random.below(3) == 0)) {
                        continue;
                    }
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            map.set(cx + 2 * bx + dx, cy + 2 * by + dy, false);
                        }
                    }
                }
            }
            bool fits = smooth(map);
            std::size_t water = 0;
            for (int y = 0; y < h && fits; ++y) {
                for (int x = 0; x < w; ++x) {
                    if (!coast_tile(map.land(x, y), map.neighbours(x, y))) {
                        fits = false;
                        break;
                    }
                    const bool near = std::abs(x - cx) <= rx + 2 && std::abs(y - cy) <= ry + 2;
                    water += near && !map.land(x, y) ? 1U : 0U;
                }
            }
            if (!fits || water < 12) {
                map = std::move(before);
                continue;
            }
            lakes.push_back(Lake{cx, cy});
            break;
        }
    }
    return lakes;
}

/// The fallback: a rectangle of land inside the margin.
LandMap rectangle(int width, int height) {
    LandMap map(width, height);
    for (int y = kMargin; y < height - kMargin; ++y) {
        for (int x = kMargin; x < width - kMargin; ++x) {
            map.set(x, y, true);
        }
    }
    return map;
}

std::uint32_t tile_bits(std::uint8_t rotation, std::uint8_t anim_phase, std::uint8_t number, std::uint8_t random) {
    constexpr std::uint32_t kNoOne = assets::IslandTile::kNoCity;  // no city, no owner
    return (std::uint32_t{rotation} & 3U) | ((std::uint32_t{anim_phase} & 0xFU) << 2) | (std::uint32_t{number} << 6) |
           (kNoOne << 14) | ((std::uint32_t{random} & 0x1FU) << 17) | (kNoOne << 22);
}

}  // namespace

std::optional<CoastTile> coast_tile(bool land, std::uint8_t n) {
    int orthogonal = 0;  // land (or, on land, sea) on the four sides
    int first = -1;      // the side of it; of two neighbouring sides the one before the other clockwise
    for (int d = 0; d < 8; d += 2) {
        if (has(n, d) == land) {
            continue;
        }
        ++orthogonal;
    }
    const auto other = [&](int d) { return has(n, d) != land; };  // the other kind than the tile
    if (orthogonal == 1 || orthogonal == 2) {
        for (int d = 0; d < 8; d += 2) {
            if (other(d) && (orthogonal == 1 || other(d + 2))) {
                first = d;
                break;
            }
        }
        if (first < 0) {
            return std::nullopt;  // two opposite sides
        }
    }
    const auto rot = [](int r) { return static_cast<std::uint8_t>(r & 3); };
    if (land) {
        if (orthogonal == 0) {
            int sea = -1;
            for (int d = 1; d < 8; d += 2) {
                if (!has(n, d)) {
                    if (sea >= 0) {
                        return std::nullopt;  // the sea on two diagonals
                    }
                    sea = d;
                }
            }
            return sea < 0 ? CoastTile{Coast::Inland, 0} : CoastTile{Coast::InnerCorner, rot((sea - 1) / 2)};
        }
        if (orthogonal == 1) {
            // The land behind the beach is land; of the two corners towards the sea one may be land.
            if (!has(n, first + 3) || !has(n, first + 5) || (has(n, first + 1) && has(n, first + 7))) {
                return std::nullopt;
            }
            return CoastTile{Coast::Beach, rot(first / 2 + 2)};
        }
        if (orthogonal == 2) {
            // Sea on `first` and `first + 2`: the corner between them is sea, the one opposite land,
            // of the two others at most one land.
            if (has(n, first + 1) || !has(n, first + 5) || (has(n, first + 3) && has(n, first + 7))) {
                return std::nullopt;
            }
            return CoastTile{Coast::OuterCorner, rot(first / 2 + 2)};
        }
        return std::nullopt;
    }
    if (orthogonal == 0) {
        int shore = -1;
        for (int d = 1; d < 8; d += 2) {
            if (has(n, d)) {
                if (shore >= 0) {
                    return std::nullopt;  // water between two corners of land
                }
                shore = d;
            }
        }
        return shore < 0 ? CoastTile{Coast::OpenSea, 0} : CoastTile{Coast::SurfCorner, rot((shore - 1) / 2)};
    }
    if (orthogonal == 1) {
        // Land on one side: the corners on the far side are water.
        if (has(n, first + 3) || has(n, first + 5)) {
            return std::nullopt;
        }
        return CoastTile{Coast::Surf, rot(first / 2 + 1)};
    }
    if (orthogonal == 2) {
        // Land on `first` and `first + 2`: the corner between them is land, the one opposite water.
        if (!has(n, first + 1) || has(n, first + 5)) {
            return std::nullopt;
        }
        return CoastTile{Coast::SurfBay, rot(first / 2)};
    }
    return std::nullopt;
}

assets::Island make_island(const ShapeRequest& request) {
    const int width = std::clamp<int>(request.width, kMinSide, kMaxSide);
    const int height = std::clamp<int>(request.height, kMinSide, kMaxSide);
    Random random(request.seed, 0x1602'15'1AULL + request.number);

    std::optional<LandMap> mask;
    for (std::uint32_t attempt = 0; attempt < kAttempts && !mask; ++attempt) {
        mask = land_mask(random, width, height, request.roughness, request.form, request.land);
    }
    LandMap map = mask ? std::move(*mask) : rectangle(width, height);
    const std::vector<Lake> lakes = mask ? carve_lakes(map, random, request.lakes) : std::vector<Lake>{};

    assets::Island island;
    assets::IslandHeader& h = island.header;
    h.chunk_name = "INSEL5";
    h.number = request.number;
    h.width = static_cast<std::uint16_t>(width);
    h.height = static_cast<std::uint16_t>(height);
    h.template_number = assets::IslandHeader::kNoTemplate;
    h.template_size = std::min<std::uint8_t>(request.size, 4);
    h.climate = request.climate & 1U;
    h.stored_complete = true;

    // What lies where: 0 free inland, 1 taken (coast, mountain).
    std::vector<std::uint8_t> taken(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    const auto at = [width](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    };
    std::vector<assets::IslandTile> tiles;
    tiles.reserve(taken.size());
    std::vector<std::int32_t> tile_index(taken.size(), -1);  // the tile laid on a tile, for rivers
    const auto lay = [&](std::uint32_t id, int x, int y, std::uint8_t rotation, std::uint8_t anim) {
        tile_index[at(x, y)] = static_cast<std::int32_t>(tiles.size());
        assets::IslandTile t;
        t.id = static_cast<std::uint16_t>(id - assets::IslandTile::kCodIdOffset);
        t.x = static_cast<std::uint16_t>(x);
        t.y = static_cast<std::uint16_t>(y);
        t.bits = tile_bits(rotation, anim, request.number, static_cast<std::uint8_t>(random.below(32)));
        tiles.push_back(t);
    };

    // The depth of the sea, as the templates have it: past the surf light water, then middle, then
    // dark (the open sea) from about five tiles out; the bands' edges wander by a tile.
    const std::vector<std::uint8_t> depth = sea_depth(map, random);
    const auto depth_at = [&](int x, int y) -> int {
        return x < 0 || y < 0 || x >= width || y >= height ? kDark : depth[at(x, y)];
    };
    // The edge of a band: the lighter water on two sides next to each other, turned towards them
    // (0 south and west, 1 north and west, 2 north and east, 3 east and south); else none.
    const auto edge = [&](int x, int y, int band) -> std::optional<std::uint8_t> {
        const bool n = depth_at(x, y - 1) < band;
        const bool e = depth_at(x + 1, y) < band;
        const bool s = depth_at(x, y + 1) < band;
        const bool w = depth_at(x - 1, y) < band;
        if (s && w && !n && !e) {
            return std::uint8_t{0};
        }
        if (n && w && !s && !e) {
            return std::uint8_t{1};
        }
        if (n && e && !s && !w) {
            return std::uint8_t{2};
        }
        if (e && s && !n && !w) {
            return std::uint8_t{3};
        }
        return std::nullopt;
    };

    // The coast and the sea.
    constexpr std::uint32_t kSeaPhases = 6;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool land = map.land(x, y);
            const CoastTile c = coast_tile(land, map.neighbours(x, y)).value_or(CoastTile{});
            const auto phase = static_cast<std::uint8_t>(random.below(kSeaPhases));
            switch (c.coast) {
                case Coast::Inland:
                    continue;
                case Coast::Beach:
                    lay(pick(random, ids::kBeach), x, y, c.rotation, 0);
                    break;
                case Coast::OuterCorner:
                    lay(pick(random, ids::kOuterCorner), x, y, c.rotation, 0);
                    break;
                case Coast::InnerCorner:
                    lay(pick(random, ids::kInnerCorner), x, y, c.rotation, 0);
                    break;
                case Coast::OpenSea: {
                    const int band = depth_at(x, y);
                    const auto turn = static_cast<std::uint8_t>(random.below(4));
                    if (band == kLight) {
                        lay(ids::kLightSea, x, y, 0, phase);
                    } else if (const auto r = edge(x, y, band)) {
                        lay(band == kMiddle ? ids::kLightEdge : ids::kDarkEdge, x, y, *r, phase);
                    } else if (band == kMiddle) {
                        lay(ids::kMiddleSea, x, y, turn, phase);
                    } else {
                        // The open sea as the world's around the island: never turned, so that its
                        // waves run on across the island's edge (its phase is the world's,
                        // set where the island is placed).
                        lay(ids::kDarkSea, x, y, 0, phase);
                    }
                    break;
                }
                case Coast::Surf:
                    lay(pick(random, ids::kSurf), x, y, c.rotation, phase);
                    break;
                case Coast::SurfCorner:
                    lay(pick(random, ids::kSurfCorner), x, y, c.rotation, phase);
                    break;
                case Coast::SurfBay:
                    lay(pick(random, ids::kSurfBay), x, y, c.rotation, phase);
                    break;
            }
            taken[at(x, y)] = 1;
        }
    }

    // A river from the ring of the mountain at (x0, y0): its spring on one side of the ring
    // (rotation 3 north, 0 east, 1 south, 2 west, as the slope it replaces), straight pieces and
    // bends out to a beach whose sea lies the river's last way, the beach a river mouth and its surf
    // the mouth's surf (both turned as the beach). As in the templates a river runs 3 tiles and
    // more, bends now and then (a bend right after the spring, else after a straight piece) and
    // never touches itself. The way is planned first, a few times over; only a way that reaches a
    // fitting beach is laid.
    // `prefer`: the way out (north 0, east 1, south 2, west 3) tried first, towards a lake; -1 none.
    const auto river = [&](int x0, int y0, int size, int prefer) {
        constexpr std::array<int, 4> kDx = {0, 1, 0, -1};  // the way out: north, east, south, west
        constexpr std::array<int, 4> kDy = {-1, 0, 1, 0};
        constexpr int kMinLength = 3;
        constexpr int kRiverAttempts = 10;
        const auto dir = [](int d) { return static_cast<std::size_t>(d); };
        const auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < width && y < height; };
        const auto free = [&](int x, int y) { return map.land(x, y) && taken[at(x, y)] == 0; };
        struct Piece {
            int x = 0;  // the straight tile, or the bend's north-west tile
            int y = 0;
            std::uint8_t rotation = 0;
            bool bend = false;
        };
        // The tiles of the planned way, by the number of their piece (from 1); 0 elsewhere.
        std::vector<std::int32_t> owner(taken.size(), 0);
        std::vector<std::size_t> marked;
        for (int attempt = 0; attempt < kRiverAttempts; ++attempt) {
            for (const std::size_t i : marked) {
                owner[i] = 0;
            }
            marked.clear();
            const auto way = prefer >= 0 && attempt < kRiverAttempts / 2 ? prefer : static_cast<int>(random.below(4));
            const int along = 1 + static_cast<int>(random.below(static_cast<std::uint32_t>(std::max(1, size - 2))));
            const int sx = way == 1 ? x0 + size : way == 3 ? x0 - 1 : x0 + along;
            const int sy = way == 0 ? y0 - 1 : way == 2 ? y0 + size : y0 + along;
            std::vector<Piece> pieces;
            // A new piece may touch only the piece before it (pieces are numbered from 1).
            const auto apart = [&](int x, int y) {
                for (int side = 0; side < 4; ++side) {
                    const int nx = x + kDx[dir(side)];
                    const int ny = y + kDy[dir(side)];
                    if (inside(nx, ny) && owner[at(nx, ny)] != 0 &&
                        owner[at(nx, ny)] < static_cast<std::int32_t>(pieces.size())) {
                        return false;
                    }
                }
                return true;
            };
            const auto mark = [&](int x, int y) {
                owner[at(x, y)] = static_cast<std::int32_t>(pieces.size());
                marked.push_back(at(x, y));
            };
            int d = way;
            int x = sx + kDx[dir(way)];
            int y = sy + kDy[dir(way)];
            int length = 0;
            const int longest = width + height;
            bool bend_allowed = true;  // right after the spring, else after a straight piece
            while (free(x, y) && length < longest) {
                if (bend_allowed && random.below(3) == 0) {
                    const int turn = (d + (random.below(2) == 0 ? 1 : 3)) % 4;
                    const int ex = x + kDx[dir(d)] + kDx[dir(turn)];  // the tile the river leaves by
                    const int ey = y + kDy[dir(d)] + kDy[dir(turn)];
                    const int bx = std::min(x, ex);
                    const int by = std::min(y, ey);
                    bool fits = true;
                    for (int ty = by; ty <= by + 1 && fits; ++ty) {
                        for (int tx = bx; tx <= bx + 1 && fits; ++tx) {
                            fits = free(tx, ty) && owner[at(tx, ty)] == 0 && apart(tx, ty);
                        }
                    }
                    if (fits) {
                        // A bend at rotation r joins its sides r and r + 1 (north 0, east 1, south 2,
                        // west 3): here the side the river comes in by and the side it leaves by.
                        const int in = (d + 2) % 4;
                        pieces.push_back({bx, by, static_cast<std::uint8_t>((in + 1) % 4 == turn ? in : turn), true});
                        for (int ty = by; ty <= by + 1; ++ty) {
                            for (int tx = bx; tx <= bx + 1; ++tx) {
                                mark(tx, ty);
                            }
                        }
                        d = turn;
                        x = ex + kDx[dir(d)];
                        y = ey + kDy[dir(d)];
                        length += 2;
                        bend_allowed = false;
                        continue;
                    }
                }
                if (owner[at(x, y)] != 0 || !apart(x, y)) {
                    pieces.clear();
                    break;
                }
                pieces.push_back({x, y, 0, false});
                pieces.back().rotation =
                    static_cast<std::uint8_t>(d % 2 == 0 ? 1 + 2 * random.below(2) : 2 * random.below(2));
                mark(x, y);
                x += kDx[dir(d)];
                y += kDy[dir(d)];
                ++length;
                bend_allowed = true;
            }
            if (pieces.empty() || length < kMinLength || !inside(x, y) || !map.land(x, y) || !apart(x, y)) {
                continue;
            }
            // The beach must face the river's way (its rotation is the side of its sea: 0 south,
            // 1 west, 2 north, 3 east) and the sea before it be plain surf.
            const auto beach = coast_tile(true, map.neighbours(x, y));
            const int sea_side = beach ? (beach->rotation + 2) % 4 : -1;  // north 0, east 1, south 2, west 3
            const int sea_x = x + kDx[dir(d)];
            const int sea_y = y + kDy[dir(d)];
            const auto surf = coast_tile(false, map.neighbours(sea_x, sea_y));
            if (!beach || beach->coast != Coast::Beach || sea_side != d || !surf || surf->coast != Coast::Surf ||
                tile_index[at(x, y)] < 0 || !inside(sea_x, sea_y) || tile_index[at(sea_x, sea_y)] < 0) {
                continue;
            }
            const auto relay = [&](int tx, int ty, std::uint32_t id, std::uint8_t rotation) {
                assets::IslandTile& t = tiles[static_cast<std::size_t>(tile_index[at(tx, ty)])];
                t.id = static_cast<std::uint16_t>(id - assets::IslandTile::kCodIdOffset);
                t.bits =
                    tile_bits(rotation, t.anim_phase(), request.number, static_cast<std::uint8_t>(random.below(32)));
            };
            relay(sx, sy, ids::kSpring, static_cast<std::uint8_t>((way + 3) % 4));
            for (const Piece& p : pieces) {
                lay(pick(random, p.bend ? ids::kRiverBend : ids::kRiver), p.x, p.y, p.rotation, 0);
                const int span = p.bend ? int{ids::kRiverBendSize} : 1;
                for (int ty = p.y; ty < p.y + span; ++ty) {
                    for (int tx = p.x; tx < p.x + span; ++tx) {
                        taken[at(tx, ty)] = 1;
                    }
                }
            }
            relay(x, y, ids::kRiverMouth, beach->rotation);
            relay(sea_x, sea_y, ids::kMouthSurf, beach->rotation);
            return;
        }
    };

    // Ore mountains on inland, in rock ringed by slopes as in the templates (the mines stand on
    // the slopes), with a tile of room around the ring so that roads reach them.
    // The way from a mountain at (x0, y0) to the nearest lake within `reach` tiles; -1 none.
    const auto towards_lake = [&](int x0, int y0, int size, int reach) {
        int best = -1;
        int best_distance = reach + 1;
        for (const Lake& l : lakes) {
            const int dx = l.x - (x0 + size / 2);
            const int dy = l.y - (y0 + size / 2);
            const int distance = std::max(std::abs(dx), std::abs(dy));
            if (distance < best_distance) {
                best_distance = distance;
                best = std::abs(dx) > std::abs(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0);
            }
        }
        return best;
    };
    // `lake`: the mountains beside it (a river into it from each), else anywhere.
    const auto place_mountains = [&](std::uint8_t count, std::uint8_t size, std::span<const std::uint32_t> kinds,
                                     bool rivers, const Lake* lake) {
        const std::uint32_t tries = 400 + 40 * std::uint32_t{count};  // many on a large island
        const int outer = size + 4;                                   // room, slope, mountain, slope, room
        constexpr int kLakeNear = 9;                                  // from a lake's centre: past its shore
        constexpr int kLakeFar = 22;
        std::uint8_t placed = 0;
        for (std::uint32_t t = 0; t < tries && placed < count && width > outer && height > outer; ++t) {
            int x0 = 2 + static_cast<int>(random.below(static_cast<std::uint32_t>(width - outer)));
            int y0 = 2 + static_cast<int>(random.below(static_cast<std::uint32_t>(height - outer)));
            if (lake != nullptr) {
                x0 = lake->x - kLakeFar + static_cast<int>(random.below(2 * kLakeFar + 1));
                y0 = lake->y - kLakeFar + static_cast<int>(random.below(2 * kLakeFar + 1));
                if (std::max(std::abs(x0 + size / 2 - lake->x), std::abs(y0 + size / 2 - lake->y)) < kLakeNear ||
                    x0 < 2 || y0 < 2 || x0 + outer - 2 >= width || y0 + outer - 2 >= height) {
                    continue;
                }
            }
            bool free = true;
            for (int y = y0 - 2; y <= y0 + size + 1 && free; ++y) {
                for (int x = x0 - 2; x <= x0 + size + 1 && free; ++x) {
                    free = map.land(x, y) && taken[at(x, y)] == 0;
                }
            }
            if (!free) {
                continue;
            }
            const std::uint32_t id = kinds[random.below(static_cast<std::uint32_t>(kinds.size()))];
            lay(id, x0, y0, static_cast<std::uint8_t>(random.below(4)), 0);
            // The ring: slopes turned to the mountain (rotation 3 north of it, 0 east, 1 south,
            // 2 west), corners with the mountain on their diagonal.
            const int x1 = x0 + size;  // the ring's east and south column and row
            const int y1 = y0 + size;
            for (int y = y0 - 1; y <= y1; ++y) {
                for (int x = x0 - 1; x <= x1; ++x) {
                    const bool west = x == x0 - 1;
                    const bool east = x == x1;
                    const bool north = y == y0 - 1;
                    const bool south = y == y1;
                    if (west || east || north || south) {
                        if ((west || east) && (north || south)) {
                            const std::uint8_t r = north ? (west ? 2 : 3) : (east ? 0 : 1);
                            lay(ids::kSlopeCorner, x, y, r, 0);
                        } else {
                            const std::uint8_t r = north ? 3 : east ? 0 : south ? 1 : 2;
                            lay(pick(random, ids::kSlope), x, y, r, 0);
                        }
                    }
                    taken[at(x, y)] = 1;
                }
            }
            // A river from a spring on the ring out to the beach, as most templates have one;
            // none where no way meets a beach facing it.
            // Mountains beside a lake always try one into it; others run to a lake nearby first.
            if (lake != nullptr) {
                river(x0, y0, size, towards_lake(x0, y0, size, 2 * kLakeFar));
            } else if (rivers && random.below(3) != 0) {
                river(x0, y0, size, towards_lake(x0, y0, size, 40));
            }
            ++placed;
        }
    };
    // The volcano first, so that it finds room: a big ore mountain in its ring of slopes, without
    // a river.
    if (request.volcano) {
        const std::array<std::uint32_t, 1> volcano{ids::kVolcanoMountain};
        place_mountains(1, ids::kBigMountainSize, volcano, false, nullptr);
    }
    // A small mountain beside each lake, its river running into the lake.
    for (const Lake& l : lakes) {
        place_mountains(1, ids::kSmallMountainSize, ids::kSmallMountain, true, &l);
    }
    place_mountains(request.big_mountains, ids::kBigMountainSize, ids::kBigMountain, true, nullptr);
    place_mountains(request.small_mountains, ids::kSmallMountainSize, ids::kSmallMountain, true, nullptr);

    // Forest in patches (the north has more of it, as its templates), ground elsewhere.
    // Patches of about a template's (a fifth of a big template's side, at most 24 tiles across on
    // large islands), their edges frayed by a finer noise.
    const int patch = std::clamp(std::min(width, height) / 5, 4, 24);
    const Noise forest(random, width, height, patch);
    const Noise fray(random, width, height, std::max(2, patch / 3));
    constexpr std::array<int, 3> kForestShift = {130, 0, -130};  // Little, Normal, Much
    const int forest_line = (request.climate == 0 ? 470 : 580) + kForestShift[static_cast<std::size_t>(request.forest)];
    const auto& trees = request.climate == 0 ? ids::kForestNorth : ids::kForestSouth;
    // The ground of many kinds, as the templates mix it tile by tile.
    const std::uint32_t ground_total = std::accumulate(ids::kGroundMix.begin(), ids::kGroundMix.end(), std::uint32_t{0},
                                                       [](std::uint32_t sum, const auto& g) { return sum + g.weight; });
    const auto ground = [&]() {
        std::uint32_t roll = random.below(ground_total);
        for (const auto& g : ids::kGroundMix) {
            if (roll < g.weight) {
                return g.id;
            }
            roll -= g.weight;
        }
        return ids::kGround;
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (taken[at(x, y)] != 0 || !map.land(x, y)) {
                continue;
            }
            if ((2 * forest.at(x, y) + fray.at(x, y)) / 3 >= forest_line) {
                lay(request.trees.empty()
                        ? pick(random, trees)
                        : request.trees[random.below(static_cast<std::uint32_t>(request.trees.size()))],
                    x, y, 0, 0);
            } else {
                lay(ground(), x, y, static_cast<std::uint8_t>(random.below(4)), 0);
            }
        }
    }
    island.layers.push_back(std::move(tiles));
    return island;
}

}  // namespace o1602::sim::shapes
