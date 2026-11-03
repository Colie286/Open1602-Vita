// SPDX-License-Identifier: GPL-3.0-or-later
// Interface art in any player colour (render/banner.hpp BannerArt, render/variant_tint.hpp): the
// drawn variant for the original's colours, the white one recoloured for every other banner; the
// banners of the colour screen recoloured within their outline (render/shape_tint.hpp).

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/render/banner.hpp"
#include "o1602/render/flag_tint.hpp"
#include "o1602/render/shape_tint.hpp"
#include "o1602/render/variant_tint.hpp"

using namespace o1602;

namespace {

/// A solid RGBA image.
std::vector<std::uint8_t> solid(std::uint32_t w, std::uint32_t h, assets::Rgba c) {
    std::vector<std::uint8_t> rgba(std::size_t{w} * h * 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = c.r;
        rgba[i + 1] = c.g;
        rgba[i + 2] = c.b;
        rgba[i + 3] = c.a;
    }
    return rgba;
}

void put(std::vector<std::uint8_t>& rgba, std::uint32_t w, std::uint32_t x, std::uint32_t y, assets::Rgba c) {
    const std::size_t i = (std::size_t{y} * w + x) * 4;
    rgba[i] = c.r;
    rgba[i + 1] = c.g;
    rgba[i + 2] = c.b;
    rgba[i + 3] = c.a;
}

assets::Rgba at(const std::vector<std::uint8_t>& rgba, std::uint32_t w, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (std::size_t{y} * w + x) * 4;
    return {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
}

}  // namespace

TEST_CASE("banner art: the original colours show their drawn variant, the others the white one tinted") {
    auto banners = render::BannerTable::original();
    banners.add_more_players();
    render::BannerArt art = render::BannerArt::at(548);
    for (sim::BannerId b = 0; b < 4; ++b) {
        CHECK(render::banner_art(art, banners.get(b)) == (render::BannerArtSprite{548 + b, std::nullopt, 3}));
    }
    // Players five on, and natives and the trader (no art of their own): white, in their colour.
    for (const sim::BannerId b :
         {sim::BannerId{5}, sim::BannerId{6}, sim::kFirstExtraBanner, static_cast<sim::BannerId>(banners.size() - 1)}) {
        const render::Banner* banner = banners.get(b);
        REQUIRE(banner != nullptr);
        CHECK(render::banner_art(art, banner) == (render::BannerArtSprite{551, banner->colour, banner->art_pattern}));
    }
    // Natives and the trader keep the white plain field; the further players get the patterns of
    // the four original coats of arms, picked from their ids: not all the same, and the same on
    // every machine.
    CHECK(banners.get(5)->art_pattern == 3);
    std::array<int, 4> patterns{};
    for (std::size_t b = sim::kFirstExtraBanner; b < banners.size(); ++b) {
        const std::uint8_t p = banners.get(static_cast<sim::BannerId>(b))->art_pattern;
        REQUIRE(p < 4);
        ++patterns[p];
    }
    CHECK(std::count(patterns.begin(), patterns.end(), 0) <= 1);
    CHECK(banners.get(sim::kFirstExtraBanner)->art_pattern == 2);  // orange: FNV-1a of "open1602:orange"
    // No banner: the white art as drawn.
    CHECK(render::banner_art(art, nullptr) == (render::BannerArtSprite{551, std::nullopt, 3}));

    // A sprite of one banner's own: the pirates' skull among the military message icons.
    art.own = {{"anno1602:pirates", 4}};
    CHECK(render::banner_art(art, banners.get(4)) == (render::BannerArtSprite{552, std::nullopt, 3}));
    // Variants further apart.
    render::BannerArt spread = render::BannerArt::at(100);
    spread.stride = 12;
    CHECK(render::banner_art(spread, banners.get(2)).gfx == 124);
    CHECK(render::banner_art(spread, banners.get(sim::kFirstExtraBanner)).gfx == 136);
}

TEST_CASE("banner art: tinting takes the colour where all variants differ and the white one is grey") {
    // 6 x 6: a light grey field in the white variant, solid other colours in the others; a motif
    // (2 x 2 orange at 1,1) the same in all; a dark outline pixel and a lone differing pixel.
    constexpr std::uint32_t w = 6;
    constexpr std::uint32_t h = 6;
    const assets::Rgba grey{200, 200, 200, 255};
    const assets::Rgba motif{230, 90, 20, 255};
    std::array<std::vector<std::uint8_t>, 4> v{solid(w, h, {216, 51, 8, 255}), solid(w, h, {0, 0, 241, 255}),
                                               solid(w, h, {255, 219, 57, 255}), solid(w, h, grey)};
    for (auto& img : v) {
        for (std::uint32_t y = 1; y < 3; ++y) {
            for (std::uint32_t x = 1; x < 3; ++x) {
                put(img, w, x, y, motif);
            }
        }
    }
    put(v[3], w, 5, 0, {30, 30, 30, 255});     // outline: too dark
    put(v[0], w, 5, 5, {200, 200, 200, 255});  // the red variant agrees with the white one here
    const std::array<render::RgbaView, 4> views{
        render::RgbaView{v[0].data(), w, h}, render::RgbaView{v[1].data(), w, h}, render::RgbaView{v[2].data(), w, h},
        render::RgbaView{v[3].data(), w, h}};
    const assets::Rgba green{0, 230, 90, 255};
    std::vector<std::uint8_t> out;
    render::tint_variant(views, 3, 3, green, out);
    REQUIRE(out.size() == std::size_t{w} * h * 4);
    const assets::Rgba cloth = render::tint_shade(101, green);  // 0.48 * 200 + 5
    CHECK(at(out, w, 4, 3) == cloth);
    CHECK(at(out, w, 0, 5) == cloth);
    CHECK(at(out, w, 1, 1) == motif);                            // the same in all variants
    CHECK(at(out, w, 5, 0) == (assets::Rgba{30, 30, 30, 255}));  // outline
    CHECK(at(out, w, 5, 5) == grey);                             // not all variants differ

    // With the pattern of the yellow variant: dark stripes (columns 3 and 4) on its bright field,
    // scaled to the white one's brightness; the cloth stays where the white variant has it.
    for (std::uint32_t y = 0; y < h; ++y) {
        for (const std::uint32_t x : {3U, 4U}) {
            put(v[2], w, x, y, {40, 40, 0, 255});
        }
    }
    render::tint_variant(views, 3, 2, green, out);
    const assets::Rgba field = at(out, w, 0, 4);
    const assets::Rgba stripe = at(out, w, 3, 4);
    CHECK(field.g > cloth.g);  // the field is brighter than the white variant's average
    CHECK(stripe.g < cloth.g / 2);
    CHECK(stripe.r == 0);  // still green: tinted, not the yellow variant's colour
    CHECK(at(out, w, 1, 1) == motif);
    CHECK(at(out, w, 5, 0) == (assets::Rgba{30, 30, 30, 255}));

    // A pixel of cloth alone (fewer than three cloth neighbours) is dithering, not cloth.
    std::array<std::vector<std::uint8_t>, 2> lone{solid(w, h, motif), solid(w, h, motif)};
    put(lone[0], w, 3, 3, {10, 10, 200, 255});
    put(lone[1], w, 3, 3, grey);
    put(lone[0], w, 4, 3, {10, 10, 200, 255});
    put(lone[1], w, 4, 3, grey);
    const std::array<render::RgbaView, 2> lone_views{render::RgbaView{lone[0].data(), w, h},
                                                     render::RgbaView{lone[1].data(), w, h}};
    render::tint_variant(lone_views, 1, 1, green, out);
    CHECK(at(out, w, 3, 3) == grey);

    // Variants of another size are left out; with none left the base is as drawn.
    const std::vector<std::uint8_t> small = solid(2, 2, {0, 0, 0, 255});
    const std::array<render::RgbaView, 2> other_size{render::RgbaView{small.data(), 2, 2}, views[3]};
    render::tint_variant(other_size, 1, 0, green, out);
    CHECK(out == v[3]);
    render::tint_variant(other_size, 5, 5, green, out);
    CHECK(out.empty());
}

TEST_CASE("banner art: a banner of the colour screen takes another colour within its outline") {
    constexpr std::uint32_t w = 6;
    constexpr std::uint32_t h = 5;
    // A red banner on a grey wall, a brighter fold in its middle.
    std::vector<std::uint8_t> banner = solid(w, h, {200, 40, 10, 255});
    for (std::uint32_t y = 0; y < h; ++y) {
        put(banner, w, 0, y, {90, 90, 90, 255});
        put(banner, w, 5, y, {90, 90, 90, 255});
    }
    put(banner, w, 2, 2, {250, 120, 90, 255});
    put(banner, w, 3, 2, {0, 0, 0, 0});  // a hole in the cloth stays a hole
    // Its outline from column 1 to 4, with a gap in row 2 and nothing in row 4.
    std::vector<std::uint8_t> outline = solid(w, h, {0, 0, 0, 0});
    for (const std::uint32_t y : {0U, 1U, 3U}) {
        put(outline, w, 1, y, {255, 200, 0, 255});
        put(outline, w, 4, y, {255, 200, 0, 255});
    }
    const assets::Rgba green{0, 230, 90, 255};
    std::vector<std::uint8_t> out;
    render::tint_shape(render::RgbaView{banner.data(), w, h}, render::RgbaView{outline.data(), w, h}, green, out);
    REQUIRE(out.size() == banner.size());
    for (std::uint32_t y = 0; y < 4; ++y) {
        // The wall stays, the cloth is green (row 2 too: the rows around it close the gap).
        CHECK(at(out, w, 0, y) == (assets::Rgba{90, 90, 90, 255}));
        CHECK(at(out, w, 5, y) == (assets::Rgba{90, 90, 90, 255}));
        const assets::Rgba c = at(out, w, 1, y);
        CHECK(c.g > c.r);
        CHECK(c.g > c.b);
    }
    CHECK(at(out, w, 3, 2).a == 0);
    CHECK(at(out, w, 2, 4) == at(banner, w, 2, 4));  // below the outline
    // The fold stays brighter than the cloth around it; most of the cloth is about the colour.
    CHECK(at(out, w, 2, 2).g > at(out, w, 1, 2).g);
    CHECK(at(out, w, 1, 1) == render::tint_shade(136, green));

    // Without an outline, or without a sprite.
    render::tint_shape(render::RgbaView{banner.data(), w, h}, render::RgbaView{}, green, out);
    CHECK(out == banner);
    render::tint_shape(render::RgbaView{}, render::RgbaView{outline.data(), w, h}, green, out);
    CHECK(out.empty());
}

TEST_CASE("banner art: an outline's gap of two rows is closed too") {
    // The red banner of the colour screen: its outline's left edge misses two rows, which came out
    // as a red line across a banner shown in another colour.
    constexpr std::uint32_t w = 6;
    constexpr std::uint32_t h = 7;
    const std::vector<std::uint8_t> banner = solid(w, h, {200, 40, 10, 255});
    std::vector<std::uint8_t> outline = solid(w, h, {0, 0, 0, 0});
    for (std::uint32_t y = 0; y < h; ++y) {
        if (y != 2 && y != 3) {
            put(outline, w, 0, y, {255, 200, 0, 255});
        }
        put(outline, w, 5, y, {255, 200, 0, 255});
    }
    const assets::Rgba green{0, 230, 90, 255};
    std::vector<std::uint8_t> out;
    render::tint_shape(render::RgbaView{banner.data(), w, h}, render::RgbaView{outline.data(), w, h}, green, out);
    REQUIRE(out.size() == banner.size());
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const assets::Rgba c = at(out, w, x, y);
            CHECK(c.g > c.r);
        }
    }

    // Three rows are no gap: the rows between keep their narrow span.
    for (const std::uint32_t y : {2U, 3U, 4U}) {
        put(outline, w, 0, y, {0, 0, 0, 0});
    }
    render::tint_shape(render::RgbaView{banner.data(), w, h}, render::RgbaView{outline.data(), w, h}, green, out);
    CHECK(at(out, w, 0, 3) == at(banner, w, 0, 3));
}

TEST_CASE("banner art: the colour screen draws the further colours' patterns anew") {
    auto banners = render::BannerTable::original();
    banners.add_more_players();
    const std::size_t more = banners.size() - sim::kFirstExtraBanner;
    constexpr std::size_t kMore = sim::kMaxPlayers - sim::kOriginalPlayers;
    REQUIRE(more == kMore);
    std::array<std::uint8_t, kMore> first{};
    bool changed = false;
    for (std::uint32_t seed = 0; seed < 20; ++seed) {
        banners.shuffle_art_patterns(sim::kFirstExtraBanner, more, seed);
        std::array<std::uint8_t, kMore> now{};
        for (std::size_t n = 0; n < more; ++n) {
            now[n] = banners.get(static_cast<sim::BannerId>(sim::kFirstExtraBanner + n))->art_pattern;
        }
        // Each run of four (a side of the colour screen with twelve players) shows each pattern once.
        for (std::size_t from = 0; from + 4 <= more; from += 4) {
            std::array<std::uint8_t, 4> run{};
            std::copy_n(now.begin() + static_cast<std::ptrdiff_t>(from), 4, run.begin());
            std::sort(run.begin(), run.end());
            CHECK(run == (std::array<std::uint8_t, 4>{0, 1, 2, 3}));
        }
        if (seed == 0) {
            first = now;
        } else {
            changed = changed || now != first;
        }
    }
    CHECK(changed);
    // The original's banners keep theirs.
    CHECK(banners.get(5)->art_pattern == 3);
    // Past the table's end nothing happens.
    banners.shuffle_art_patterns(static_cast<sim::BannerId>(banners.size() - 2), 4, 1);
}
