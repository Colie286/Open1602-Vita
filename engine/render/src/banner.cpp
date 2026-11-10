// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/banner.hpp"

#include <random>

namespace o1602::render {

// Fitted to the original flag images (red, blue, yellow) for the tint of flag_tint.hpp; white is
// the template itself.
const std::array<assets::Rgba, 12> BannerTable::kDefaultColours = {{
    {216, 51, 8, 255},     // red (original)
    {0, 0, 241, 255},      // blue (original)
    {255, 219, 57, 255},   // yellow (original)
    {255, 255, 255, 255},  // white (original)
    {0, 230, 90, 255},     // green, brighter than the land on the minimap
    {255, 128, 0, 255},    // orange
    {130, 40, 190, 255},   // purple
    {0, 190, 190, 255},    // teal
    {255, 110, 180, 255},  // pink
    {220, 0, 220, 255},    // magenta
    {120, 70, 30, 255},    // brown
    {90, 90, 100, 255},    // dark grey (no black: the pirates fly a black flag)
}};

BannerTable BannerTable::original(const assets::FigureTable* figures, const BannerOptions& options) {
    BannerTable table;
    const char* kPlayers[] = {"anno1602:red", "anno1602:blue", "anno1602:yellow", "anno1602:white"};
    for (std::uint8_t c = 0; c < 4; ++c) {
        Banner b{kPlayers[c], kDefaultColours[c], FlagStyle::Tinted, 0, 8, c};
        const assets::FigureType* flag = figures != nullptr ? figures->flag_for_colour(c) : nullptr;
        if (options.original_player_flags && flag != nullptr && flag->sheet == assets::FigureSheet::Ship) {
            b.flag = FlagStyle::Sprites;
            b.flag_gfx = flag->gfx;
        }
        table.add(std::move(b));
    }
    Banner pirates{"anno1602:pirates", {40, 40, 40, 255}, FlagStyle::None, 0, 8, std::nullopt};
    if (const assets::FigureType* flag = figures != nullptr ? figures->flag_for_colour(4) : nullptr;
        flag != nullptr && flag->sheet == assets::FigureSheet::Ship) {
        pirates.flag = FlagStyle::Sprites;
        pirates.flag_gfx = flag->gfx;
    }
    table.add(std::move(pirates));
    table.add(Banner{"anno1602:natives", {160, 104, 48, 255}, FlagStyle::None, 0, 8, std::nullopt});
    table.add(Banner{"anno1602:trader", {48, 160, 96, 255}, FlagStyle::None, 0, 8, std::nullopt});
    // Generic flag: the white flag FAHNE4.
    if (const assets::FigureType* white = figures != nullptr ? figures->flag_for_colour(3) : nullptr;
        white != nullptr && white->sheet == assets::FigureSheet::Ship) {
        table.set_flag_template(FlagTemplate{white->gfx, 8, true});
    }
    return table;
}

static_assert(BannerTable::kDefaultColours.size() == sim::kMaxPlayers, "a colour of its own for every player");

void BannerTable::add_more_players() {
    if (m_banners.size() != sim::kFirstExtraBanner) {
        return;
    }
    struct More {
        const char* id;
        std::size_t colour;  ///< in kDefaultColours
    };
    static constexpr std::array<More, sim::kMaxPlayers - sim::kOriginalPlayers> kMore = {{
        {"open1602:orange", 5},
        {"open1602:green", 4},
        {"open1602:teal", 7},
        {"open1602:pink", 8},
        {"open1602:magenta", 9},
        {"open1602:grey", 11},
        {"open1602:purple", 6},
        {"open1602:brown", 10},
    }};
    for (const More& m : kMore) {
        add_colour(m.id, kDefaultColours[m.colour]);
    }
}

sim::BannerId BannerTable::add(Banner banner) {
    m_banners.push_back(std::move(banner));
    return static_cast<sim::BannerId>(m_banners.size() - 1);
}

sim::BannerId BannerTable::add_colour(std::string id, assets::Rgba colour) {
    // FNV-1a of the id: a stable "random" horse and coat of arms per banner (not std::hash, which differs between
    // standard libraries and would break multiplayer games across platforms).
    std::uint32_t hash = 2166136261U;
    for (const char c : id) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619U;
    }
    Banner b{std::move(id), colour, FlagStyle::Tinted, 0, 8, std::nullopt};
    b.unit_mount = static_cast<std::uint8_t>(hash % 4U);
    b.art_pattern = static_cast<std::uint8_t>((hash >> 8U) % 4U);
    return add(std::move(b));
}

void BannerTable::shuffle_art_patterns(sim::BannerId first, std::size_t count, std::uint32_t seed) {
    // std::mt19937 gives the same numbers everywhere; std::shuffle and the distributions do not.
    std::mt19937 random(seed);
    for (std::size_t run = 0; run < count; run += 4) {
        std::array<std::uint8_t, 4> patterns = {0, 1, 2, 3};
        for (std::size_t i = patterns.size() - 1; i > 0; --i) {
            std::swap(patterns[i], patterns[random() % (i + 1)]);
        }
        for (std::size_t k = 0; k < 4 && run + k < count; ++k) {
            const std::size_t b = std::size_t{first} + run + k;
            if (b < m_banners.size()) {
                m_banners[b].art_pattern = patterns[k];
            }
        }
    }
}

BannerArtSprite banner_art(const BannerArt& art, const Banner* banner) {
    if (banner != nullptr) {
        for (const auto& [id, offset] : art.own) {
            if (id == banner->id) {
                return {art.first + offset, std::nullopt};
            }
        }
        if (banner->unit_variant && *banner->unit_variant < art.variants) {
            return {art.first + *banner->unit_variant * art.stride, std::nullopt};
        }
    }
    BannerArtSprite s{art.first + art.base * art.stride, std::nullopt, art.base};
    if (banner != nullptr && art.base < art.variants) {
        s.tint = banner->colour;
        s.pattern = banner->art_pattern < art.variants ? banner->art_pattern : art.base;
    }
    return s;
}

}  // namespace o1602::render
