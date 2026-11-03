// SPDX-License-Identifier: GPL-3.0-or-later
// Islands of their own shape (world generator stage 2).

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/sim/island_shapes.hpp"

using namespace o1602;
using sim::shapes::Coast;
namespace ids = sim::shapes::ids;

namespace {

template <std::size_t N>
bool one_of(std::uint32_t id, const std::array<std::uint32_t, N>& list) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

/// What the tile with this id is, as far as the coast goes.
std::optional<Coast> coast_of(std::uint32_t id) {
    if (one_of(id, ids::kOpenSea)) {
        return Coast::OpenSea;
    }
    if (one_of(id, ids::kSurf) || id == ids::kMouthSurf) {
        return Coast::Surf;
    }
    if (one_of(id, ids::kSurfCorner)) {
        return Coast::SurfCorner;
    }
    if (one_of(id, ids::kSurfBay)) {
        return Coast::SurfBay;
    }
    if (one_of(id, ids::kBeach) || id == ids::kRiverMouth) {
        return Coast::Beach;
    }
    if (one_of(id, ids::kOuterCorner)) {
        return Coast::OuterCorner;
    }
    if (one_of(id, ids::kInnerCorner)) {
        return Coast::InnerCorner;
    }
    return std::nullopt;  // ground, forest, mountains
}

int footprint(std::uint32_t id) {
    if (one_of(id, ids::kBigMountain) || id == ids::kVolcanoMountain) {
        return ids::kBigMountainSize;
    }
    if (one_of(id, ids::kSmallMountain)) {
        return ids::kSmallMountainSize;
    }
    if (one_of(id, ids::kRiverBend)) {
        return ids::kRiverBendSize;
    }
    return 1;
}

bool is_sea(Coast c) {
    return c == Coast::OpenSea || c == Coast::Surf || c == Coast::SurfCorner || c == Coast::SurfBay;
}

struct Checked {
    std::size_t land = 0;
    std::size_t big = 0;
    std::size_t small = 0;
    std::size_t volcanoes = 0;
};

/// Every tile is laid once, every coast tile is the one its neighbours ask for.
Checked check_island(const assets::Island& island, const sim::shapes::ShapeRequest& r) {
    const int w = island.header.width;
    const int h = island.header.height;
    CHECK(w == r.width);
    CHECK(h == r.height);
    CHECK(island.header.template_number == assets::IslandHeader::kNoTemplate);
    CHECK(island.header.stored_complete);
    CHECK(island.header.template_path().empty());
    REQUIRE(island.layers.size() == 1);
    std::vector<int> laid(static_cast<std::size_t>(w * h), 0);
    std::vector<std::optional<Coast>> coast(static_cast<std::size_t>(w * h));
    std::vector<std::uint8_t> rotation(static_cast<std::size_t>(w * h), 0);
    Checked out;
    for (const assets::IslandTile& t : island.layers[0]) {
        CHECK(t.island_number() == r.number);
        CHECK(t.city() == assets::IslandTile::kNoCity);
        const int size = footprint(t.cod_id());
        const bool volcano = t.cod_id() == ids::kVolcanoMountain;
        out.volcanoes += volcano ? 1U : 0U;
        out.big += size == ids::kBigMountainSize && !volcano ? 1U : 0U;
        out.small += size == ids::kSmallMountainSize ? 1U : 0U;
        for (int y = t.y; y < t.y + size; ++y) {
            for (int x = t.x; x < t.x + size; ++x) {
                REQUIRE(x < w);
                REQUIRE(y < h);
                const auto i = static_cast<std::size_t>(y * w + x);
                ++laid[i];
                coast[i] = coast_of(t.cod_id());
                // The surf before a river mouth is turned as the mouth's beach, one less than surf.
                rotation[i] =
                    t.cod_id() == ids::kMouthSurf ? static_cast<std::uint8_t>((t.rotation() + 1) % 4) : t.rotation();
            }
        }
    }
    CHECK(std::all_of(laid.begin(), laid.end(), [](int n) { return n == 1; }));
    const auto land = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return false;
        }
        const auto& c = coast[static_cast<std::size_t>(y * w + x)];
        return !c || !is_sea(*c);
    };
    constexpr std::array<int, 8> dx = {0, 1, 1, 1, 0, -1, -1, -1};
    constexpr std::array<int, 8> dy = {-1, -1, 0, 1, 1, 1, 0, -1};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t n = 0;
            for (std::size_t d = 0; d < 8; ++d) {
                n = static_cast<std::uint8_t>(n | (land(x + dx[d], y + dy[d]) ? 1U << d : 0U));
            }
            const auto want = sim::shapes::coast_tile(land(x, y), n);
            REQUIRE(want.has_value());
            const auto& has = coast[static_cast<std::size_t>(y * w + x)];
            if (want->coast == Coast::Inland) {
                CHECK(!has.has_value());
            } else {
                REQUIRE(has.has_value());
                CHECK(*has == want->coast);
                if (want->coast != Coast::OpenSea) {
                    CHECK(rotation[static_cast<std::size_t>(y * w + x)] == want->rotation);
                }
            }
            if (land(x, y)) {
                ++out.land;
                CHECK((x >= 2 && y >= 2 && x < w - 2 && y < h - 2));
            }
        }
    }
    return out;
}

}  // namespace

TEST_CASE("island shapes: the coast tiles of the templates") {
    using sim::shapes::coast_tile;
    // Land: with land all around, sea to the east (rotation 3), sea to the south and west (an
    // outer corner, rotation 0), sea only to the north-east (an inner corner, rotation 0).
    CHECK(coast_tile(true, 255)->coast == Coast::Inland);
    CHECK((coast_tile(true, 241)->coast == Coast::Beach && coast_tile(true, 241)->rotation == 3));
    CHECK((coast_tile(true, 7)->coast == Coast::OuterCorner && coast_tile(true, 7)->rotation == 0));
    CHECK((coast_tile(true, 253)->coast == Coast::InnerCorner && coast_tile(true, 253)->rotation == 0));
    // Sea: land to the west (rotation 0), only to the north-east (21207, rotation 0), to the north
    // and east (rotation 0).
    CHECK(coast_tile(false, 0)->coast == Coast::OpenSea);
    CHECK((coast_tile(false, 224)->coast == Coast::Surf && coast_tile(false, 224)->rotation == 0));
    CHECK((coast_tile(false, 2)->coast == Coast::SurfCorner && coast_tile(false, 2)->rotation == 0));
    CHECK((coast_tile(false, 7)->coast == Coast::SurfBay && coast_tile(false, 7)->rotation == 0));
    // What the templates have no tile for.
    CHECK(!coast_tile(true, 1 | 16));          // land one tile wide
    CHECK(!coast_tile(true, 255 & ~2 & ~32));  // the sea on two diagonals
    CHECK(!coast_tile(false, 1 | 16));         // water one tile wide
    CHECK(!coast_tile(false, 2 | 128));        // water between two corners
    CHECK(!coast_tile(false, 1 | 4 | 16));     // a bay three sides deep
    // Each side of the sea has its own beach rotation.
    std::array<bool, 4> seen{};
    for (unsigned n = 0; n < 256; ++n) {
        if (auto c = coast_tile(true, static_cast<std::uint8_t>(n)); c && c->coast == Coast::Beach) {
            seen[c->rotation] = true;
        }
    }
    CHECK(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }));
}

TEST_CASE("island shapes: every size makes an island whose coast fits, with its mountains") {
    struct Size {
        std::uint16_t width, height;
        std::uint8_t size, big, small;
    };
    constexpr std::array<Size, 5> kSizes = {
        {{30, 30, 0, 0, 1}, {40, 40, 1, 0, 1}, {50, 52, 2, 1, 1}, {70, 60, 3, 1, 2}, {100, 90, 4, 2, 3}}};
    for (const Size& s : kSizes) {
        for (std::uint64_t seed = 0; seed < 40; ++seed) {
            sim::shapes::ShapeRequest r;
            r.width = s.width;
            r.height = s.height;
            r.size = s.size;
            r.climate = static_cast<std::uint8_t>(seed & 1U);
            r.number = static_cast<std::uint8_t>(seed + 3);
            r.big_mountains = s.big;
            r.small_mountains = s.small;
            r.seed = seed * 7919U + s.width;
            const assets::Island island = sim::shapes::make_island(r);
            const Checked c = check_island(island, r);
            const std::size_t area = std::size_t{s.width} * s.height;
            CHECK(c.land * 4 >= area);  // the templates are 40..60 % land
            CHECK(c.land * 4 <= area * 3);
            CHECK(c.big == s.big);
            CHECK(c.small == s.small);
            CHECK(island.header.template_size == s.size);
            CHECK(island.header.climate == r.climate);
        }
    }
}

TEST_CASE("island shapes: the same request gives the same island, another seed another") {
    sim::shapes::ShapeRequest r;
    r.width = 70;
    r.height = 60;
    r.size = 3;
    r.big_mountains = 1;
    r.small_mountains = 2;
    r.number = 5;
    r.seed = 1602;
    const auto a = sim::shapes::make_island(r);
    const auto b = sim::shapes::make_island(r);
    REQUIRE(a.layers.size() == 1);
    REQUIRE(b.layers.size() == 1);
    REQUIRE(a.layers[0].size() == b.layers[0].size());
    CHECK(std::equal(a.layers[0].begin(), a.layers[0].end(), b.layers[0].begin(),
                     [](const assets::IslandTile& x, const assets::IslandTile& y) {
                         return x.id == y.id && x.x == y.x && x.y == y.y && x.bits == y.bits;
                     }));
    r.seed = 1603;
    const auto c = sim::shapes::make_island(r);
    const bool same = c.layers[0].size() == a.layers[0].size() &&
                      std::equal(a.layers[0].begin(), a.layers[0].end(), c.layers[0].begin(),
                                 [](const assets::IslandTile& x, const assets::IslandTile& y) {
                                     return x.id == y.id && x.x == y.x && x.y == y.y;
                                 });
    CHECK(!same);
}

TEST_CASE("island shapes: every coast, outline and share of land, from the smallest to the largest side") {
    using sim::shapes::Form;
    using sim::shapes::Roughness;
    struct Size {
        std::uint16_t width, height;
    };
    constexpr std::array<Size, 4> kSizes = {{{16, 16}, {64, 24}, {180, 120}, {250, 250}}};
    for (const Size& s : kSizes) {
        for (const auto roughness : {Roughness::Smooth, Roughness::Natural, Roughness::Rugged}) {
            for (const auto form : {Form::Compact, Form::Branched}) {
                for (const std::uint16_t land : std::array<std::uint16_t, 2>{0, 740}) {
                    sim::shapes::ShapeRequest r;
                    r.width = s.width;
                    r.height = s.height;
                    r.size = 4;
                    r.number = 9;
                    r.roughness = roughness;
                    r.form = form;
                    r.land = land;
                    r.big_mountains = s.width >= 100 ? 6 : 0;
                    r.small_mountains = s.width >= 100 ? 8 : 0;
                    r.seed = std::uint64_t{s.width} * 31U + static_cast<std::uint64_t>(roughness) * 7U +
                             static_cast<std::uint64_t>(form) + land;
                    const assets::Island island = sim::shapes::make_island(r);
                    const Checked c = check_island(island, r);
                    const std::size_t area = std::size_t{s.width} * s.height;
                    CHECK(c.land * 5 >= area);  // never a speck
                    if (land >= 700 && form == Form::Compact && s.width >= 100) {
                        CHECK(c.land * 100 >= area * 58);  // a world of much land: fuller islands
                    }
                    CHECK(c.big == r.big_mountains);
                    CHECK(c.small == r.small_mountains);
                }
            }
        }
    }
}

TEST_CASE("island shapes: light water past the surf, dark far out, the band edges turned to the light") {
    sim::shapes::ShapeRequest r;
    r.width = 140;
    r.height = 120;
    r.size = 4;
    r.number = 3;
    r.seed = 77;
    const assets::Island island = sim::shapes::make_island(r);
    const int w = r.width;
    const int h = r.height;
    // 0 land and surf, 1 light, 2 middle, 3 dark; the light edge counts as middle, the dark as dark.
    std::vector<int> band(static_cast<std::size_t>(w * h), 0);
    std::vector<std::uint32_t> id(band.size(), 0);
    std::vector<std::uint8_t> rotation(band.size(), 0);
    for (const assets::IslandTile& t : island.layers[0]) {
        const auto i = static_cast<std::size_t>(t.y * w + t.x);
        id[i] = t.cod_id();
        rotation[i] = t.rotation();
        // The open sea is never turned, as the world's around the island.
        CHECK((t.cod_id() != ids::kDarkSea || t.rotation() == 0));
        switch (t.cod_id()) {
            case ids::kLightSea:
                band[i] = 1;
                break;
            case ids::kLightEdge:
            case ids::kMiddleSea:
                band[i] = 2;
                break;
            case ids::kDarkEdge:
            case ids::kDarkSea:
                band[i] = 3;
                break;
            default:
                break;
        }
    }
    const auto band_at = [&](int x, int y) {
        return x < 0 || y < 0 || x >= w || y >= h ? 3 : band[static_cast<std::size_t>(y * w + x)];
    };
    std::array<std::size_t, 4> count{};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = static_cast<std::size_t>(y * w + x);
            const int b = band[i];
            ++count[static_cast<std::size_t>(b)];
            if (b == 0) {
                continue;
            }
            // Next to the surf never dark.
            bool near_surf = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    near_surf = near_surf || (band_at(x + dx, y + dy) == 0 && (dx != 0 || dy != 0));
                }
            }
            CHECK(!(near_surf && b == 3));
            // On the rectangle's edge the open sea, but next to the surf.
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                CHECK((near_surf || b == 3));
            }
            if (id[i] == ids::kLightEdge || id[i] == ids::kDarkEdge) {
                // An edge: the lighter water on the two sides it is turned to.
                const int own = id[i] == ids::kLightEdge ? 2 : 3;
                const bool n = band_at(x, y - 1) < own;
                const bool e = band_at(x + 1, y) < own;
                const bool s = band_at(x, y + 1) < own;
                const bool west = band_at(x - 1, y) < own;
                switch (rotation[i]) {
                    case 0:
                        CHECK((s && west));
                        break;
                    case 1:
                        CHECK((n && west));
                        break;
                    case 2:
                        CHECK((n && e));
                        break;
                    default:
                        CHECK((e && s));
                        break;
                }
            }
        }
    }
    CHECK(count[1] > 0);
    CHECK(count[2] > 0);
    CHECK(count[3] > 0);
}

TEST_CASE("island shapes: an island larger than the original's, with ore mountains for its size") {
    sim::shapes::ShapeRequest r;
    r.width = 300;
    r.height = 280;
    r.size = 4;
    r.number = 4;
    r.big_mountains = 60;
    r.small_mountains = 90;
    r.seed = 3;
    const assets::Island island = sim::shapes::make_island(r);
    const Checked c = check_island(island, r);
    CHECK(c.big == r.big_mountains);
    CHECK(c.small == r.small_mountains);
    CHECK(c.land * 2 >= std::size_t{r.width} * r.height);
}

TEST_CASE("island shapes: a giant does not run straight along its rectangle") {
    for (const std::uint16_t land : std::array<std::uint16_t, 3>{0, 605, 740}) {
        for (std::uint64_t seed = 1; seed < 4; ++seed) {
            sim::shapes::ShapeRequest r;
            r.width = 480;
            r.height = 470;
            r.size = 4;
            r.number = 2;
            r.land = land;
            r.seed = seed;
            const assets::Island island = sim::shapes::make_island(r);
            // The longest run of land along each of the four lines two tiles inside the edge.
            std::vector<std::uint8_t> is_land(std::size_t{r.width} * r.height, 0);
            std::size_t total = 0;
            for (const assets::IslandTile& t : island.layers[0]) {
                const auto c = coast_of(t.cod_id());
                if (!c || !is_sea(*c)) {
                    is_land[std::size_t{t.y} * r.width + t.x] = 1;
                    ++total;
                }
            }
            const auto at = [&](int x, int y) {
                return is_land[static_cast<std::size_t>(y) * r.width + static_cast<std::size_t>(x)] != 0;
            };
            int longest = 0;
            for (int line = 2; line < 5; ++line) {
                int west = 0;
                int east = 0;
                int north = 0;
                int south = 0;
                for (int k = 0; k < r.height; ++k) {
                    west = at(line, k) ? west + 1 : 0;
                    east = at(r.width - 1 - line, k) ? east + 1 : 0;
                    longest = std::max({longest, west, east});
                }
                for (int k = 0; k < r.width; ++k) {
                    north = at(k, line) ? north + 1 : 0;
                    south = at(k, r.height - 1 - line) ? south + 1 : 0;
                    longest = std::max({longest, north, south});
                }
            }
            CHECK(longest < 20);  // without a falloff at the edge: up to 210 tiles
            CHECK(total * 100 >= std::size_t{r.width} * r.height * (land == 0 ? 50 : land / 10 - 5));
        }
    }
}

TEST_CASE("island shapes: mountains in a ring of slopes turned to them, ground of many kinds") {
    sim::shapes::ShapeRequest r;
    r.width = 100;
    r.height = 90;
    r.size = 4;
    r.number = 7;
    r.big_mountains = 2;
    r.small_mountains = 3;
    r.seed = 11;
    const assets::Island island = sim::shapes::make_island(r);
    (void)check_island(island, r);
    const int w = island.header.width;
    std::vector<std::uint32_t> id(static_cast<std::size_t>(w * island.header.height), 0);
    std::vector<std::uint8_t> rotation(id.size(), 0);
    std::vector<const assets::IslandTile*> mountains;
    std::array<std::size_t, 2> grounds{};  // the main ground, the others
    for (const assets::IslandTile& t : island.layers[0]) {
        id[static_cast<std::size_t>(t.y * w + t.x)] = t.cod_id();
        rotation[static_cast<std::size_t>(t.y * w + t.x)] = t.rotation();
        if (footprint(t.cod_id()) > 1 && !one_of(t.cod_id(), ids::kRiverBend)) {
            mountains.push_back(&t);
        }
        const bool ground = std::any_of(ids::kGroundMix.begin(), ids::kGroundMix.end(),
                                        [&](const auto& g) { return g.id == t.cod_id(); });
        if (ground) {
            ++grounds[t.cod_id() == ids::kGround ? 0 : 1];
        }
    }
    REQUIRE(mountains.size() == 5);
    for (const assets::IslandTile* m : mountains) {
        const int size = footprint(m->cod_id());
        const int x0 = m->x - 1;
        const int y0 = m->y - 1;
        const int x1 = m->x + size;
        const int y1 = m->y + size;
        const auto at = [&](int x, int y) { return static_cast<std::size_t>(y * w + x); };
        // Corners: the mountain on their diagonal (rotation 2 north-west, 3 north-east, 0
        // south-east, 1 south-west).
        CHECK(id[at(x0, y0)] == ids::kSlopeCorner);
        CHECK(rotation[at(x0, y0)] == 2);
        CHECK(rotation[at(x1, y0)] == 3);
        CHECK(rotation[at(x1, y1)] == 0);
        CHECK(rotation[at(x0, y1)] == 1);
        for (int i = 0; i < size; ++i) {
            // A spring of a river is a slope too, turned the same.
            CHECK((one_of(id[at(m->x + i, y0)], ids::kSlope) || id[at(m->x + i, y0)] == ids::kSpring));
            CHECK(rotation[at(m->x + i, y0)] == 3);  // the mountain south of it
            CHECK(rotation[at(x1, m->y + i)] == 0);  // west
            CHECK(rotation[at(m->x + i, y1)] == 1);  // north
            CHECK(rotation[at(x0, m->y + i)] == 2);  // east
        }
    }
    // The templates' main ground is about a quarter of their ground; the rest are other kinds.
    CHECK(grounds[0] > 0);
    CHECK(grounds[1] > grounds[0]);
}

TEST_CASE("island shapes: the forest of the climate, or of the trees a climate of a package names") {
    sim::shapes::ShapeRequest r;
    r.width = 60;
    r.height = 50;
    r.size = 3;
    r.seed = 5;
    const auto count = [](const assets::Island& island, auto&& is_tree) {
        std::size_t n = 0;
        for (const assets::IslandTile& t : island.layers[0]) {
            n += is_tree(t.cod_id()) ? 1U : 0U;
        }
        return n;
    };
    const assets::Island north = sim::shapes::make_island(r);
    const std::size_t trees = count(north, [](std::uint32_t id) { return one_of(id, ids::kForestNorth); });
    CHECK(trees > 0);
    // Only the trees asked for, in the same places.
    r.trees = {ids::kForestSouth[0], ids::kForestSouth[3]};
    const assets::Island own = sim::shapes::make_island(r);
    CHECK(count(own, [](std::uint32_t id) { return one_of(id, ids::kForestNorth); }) == 0);
    CHECK(count(own, [&](std::uint32_t id) { return id == r.trees[0] || id == r.trees[1]; }) == trees);
}

TEST_CASE("island shapes: the ground a climate of a package names, in the same places") {
    sim::shapes::ShapeRequest r;
    r.width = 60;
    r.height = 50;
    r.size = 3;
    r.seed = 7;
    const auto mixed = [](std::uint32_t id) {
        return std::any_of(ids::kGroundMix.begin(), ids::kGroundMix.end(), [&](const auto& g) { return g.id == id; });
    };
    const auto count = [](const assets::Island& island, auto&& is) {
        std::size_t n = 0;
        for (const assets::IslandTile& t : island.layers[0]) {
            n += is(t.cod_id()) ? 1U : 0U;
        }
        return n;
    };
    const assets::Island plain = sim::shapes::make_island(r);
    const std::size_t ground = count(plain, mixed);
    CHECK(ground > 0);
    r.ground = {22722, 22724};
    const assets::Island own = sim::shapes::make_island(r);
    CHECK(count(own, [&](std::uint32_t id) { return id == 22722 || id == 22724; }) == ground);
    CHECK(count(own, [&](std::uint32_t id) { return mixed(id) && id != 22722 && id != 22724; }) == 0);
}

TEST_CASE("island shapes: a volcano only where one is asked for, in its ring of slopes") {
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        sim::shapes::ShapeRequest r;
        r.width = 60;
        r.height = 50;
        r.size = 3;
        r.number = 4;
        r.big_mountains = 2;
        r.small_mountains = 1;
        r.seed = seed;
        const Checked without = check_island(sim::shapes::make_island(r), r);
        CHECK(without.volcanoes == 0);  // a big ore mountain is never the volcano 21108
        CHECK(without.big == 2);
        r.volcano = true;
        const assets::Island island = sim::shapes::make_island(r);
        const Checked with = check_island(island, r);
        CHECK(with.volcanoes == 1);
        CHECK(with.big == 2);
        const int w = island.header.width;
        std::vector<std::uint32_t> id(static_cast<std::size_t>(w * island.header.height), 0);
        const assets::IslandTile* volcano = nullptr;
        for (const assets::IslandTile& t : island.layers[0]) {
            id[static_cast<std::size_t>(t.y * w + t.x)] = t.cod_id();
            if (t.cod_id() == ids::kVolcanoMountain) {
                volcano = &t;
            }
        }
        REQUIRE(volcano != nullptr);
        // Slopes all around, no spring: lava, not water, comes from a volcano.
        const int x0 = volcano->x - 1;
        const int y0 = volcano->y - 1;
        const int x1 = volcano->x + ids::kBigMountainSize;
        const int y1 = volcano->y + ids::kBigMountainSize;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                if (x == x0 || x == x1 || y == y0 || y == y1) {
                    const std::uint32_t ring = id[static_cast<std::size_t>(y * w + x)];
                    CHECK((one_of(ring, ids::kSlope) || ring == ids::kSlopeCorner));
                }
            }
        }
    }
}

TEST_CASE("island shapes: rivers from a spring on a mountain's slopes, with bends, to a river mouth") {
    std::size_t rivers = 0;
    std::size_t bends = 0;
    std::size_t straights = 0;
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        sim::shapes::ShapeRequest r;
        r.width = 100;
        r.height = 90;
        r.size = 4;
        r.number = 9;
        r.big_mountains = 2;
        r.small_mountains = 3;
        r.seed = seed;
        const assets::Island island = sim::shapes::make_island(r);
        (void)check_island(island, r);  // the mouth is beach, its surf surf, as the coast asks
        const int w = island.header.width;
        const int h = island.header.height;
        std::vector<std::uint32_t> id(static_cast<std::size_t>(w * h), 0);
        std::vector<std::uint8_t> rotation(id.size(), 0);
        std::vector<int> bend(id.size(), -1);  // the tile's bend, by its index in the layer
        std::vector<std::pair<int, int>> springs;
        const auto at = [&](int x, int y) { return static_cast<std::size_t>(y * w + x); };
        const auto& layer = island.layers[0];
        for (std::size_t i = 0; i < layer.size(); ++i) {
            const assets::IslandTile& t = layer[i];
            const int size = footprint(t.cod_id());
            for (int y = t.y; y < t.y + size; ++y) {
                for (int x = t.x; x < t.x + size; ++x) {
                    id[at(x, y)] = t.cod_id();
                    rotation[at(x, y)] = t.rotation();
                    if (one_of(t.cod_id(), ids::kRiverBend)) {
                        bend[at(x, y)] = static_cast<int>(i);
                    }
                }
            }
            if (t.cod_id() == ids::kSpring) {
                springs.emplace_back(t.x, t.y);
            }
        }
        std::vector<std::uint8_t> seen(id.size(), 0);
        for (const auto& [sx, sy] : springs) {
            // The river leaves the spring on its east side at rotation 0 (north at 3, south at 1,
            // west at 2). A straight piece lies across its way; a bend at rotation r joins its sides
            // r and r + 1 (north 0, east 1, south 2, west 3), the river coming in and going out on
            // its diagonal. The river ends in a mouth turned as the beach, with its surf beyond.
            constexpr std::array<int, 4> kDx = {0, 1, 0, -1};
            constexpr std::array<int, 4> kDy = {-1, 0, 1, 0};
            int way = (rotation[at(sx, sy)] + 1) % 4;
            int x = sx + kDx[static_cast<std::size_t>(way)];
            int y = sy + kDy[static_cast<std::size_t>(way)];
            int length = 0;
            bool after_bend = false;
            while (one_of(id[at(x, y)], ids::kRiver) || one_of(id[at(x, y)], ids::kRiverBend)) {
                if (one_of(id[at(x, y)], ids::kRiver)) {
                    CHECK(rotation[at(x, y)] % 2 == (way % 2 == 0 ? 1 : 0));
                    CHECK(seen[at(x, y)] == 0);
                    seen[at(x, y)] = 1;
                    ++length;
                    ++straights;
                    after_bend = false;
                    x += kDx[static_cast<std::size_t>(way)];
                    y += kDy[static_cast<std::size_t>(way)];
                    continue;
                }
                CHECK(!after_bend);  // a straight piece between two bends
                const assets::IslandTile& b = layer[static_cast<std::size_t>(bend[at(x, y)])];
                const int in = (way + 2) % 4;
                const int turned = b.rotation();
                const int out = turned == in ? (turned + 1) % 4 : (turned + 1) % 4 == in ? turned : -1;
                REQUIRE(out >= 0);  // it joins the side the river comes in by
                const int ex = x + kDx[static_cast<std::size_t>(way)] + kDx[static_cast<std::size_t>(out)];
                const int ey = y + kDy[static_cast<std::size_t>(way)] + kDy[static_cast<std::size_t>(out)];
                CHECK(std::min(x, ex) == b.x);  // the river comes in and goes out on its diagonal
                CHECK(std::min(y, ey) == b.y);
                for (int ty = b.y; ty < b.y + ids::kRiverBendSize; ++ty) {
                    for (int tx = b.x; tx < b.x + ids::kRiverBendSize; ++tx) {
                        CHECK(seen[at(tx, ty)] == 0);
                        seen[at(tx, ty)] = 1;
                    }
                }
                way = out;
                x = ex + kDx[static_cast<std::size_t>(way)];
                y = ey + kDy[static_cast<std::size_t>(way)];
                length += 2;
                after_bend = true;
                ++bends;
            }
            CHECK(length >= 3);
            REQUIRE(id[at(x, y)] == ids::kRiverMouth);
            CHECK((rotation[at(x, y)] + 2) % 4 == way);  // the beach's sea lies the river's way
            const int sea_x = x + kDx[static_cast<std::size_t>(way)];
            const int sea_y = y + kDy[static_cast<std::size_t>(way)];
            CHECK(id[at(sea_x, sea_y)] == ids::kMouthSurf);
            CHECK(rotation[at(sea_x, sea_y)] == rotation[at(x, y)]);
            ++rivers;
        }
        // Every piece of river belongs to a river from a spring.
        for (std::size_t i = 0; i < id.size(); ++i) {
            if (one_of(id[i], ids::kRiver) || one_of(id[i], ids::kRiverBend)) {
                CHECK(seen[i] == 1);
            }
        }
    }
    CHECK(rivers >= 20);         // most islands with mountains get one or more
    CHECK(bends >= rivers / 2);  // as in the templates, about two straight tiles to a bend
    CHECK(straights >= bends);
}
