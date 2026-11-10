// SPDX-License-Identifier: GPL-3.0-or-later
//
// Banners: how a player is recognised on screen - display colour (ownership view, minimap, UI)
// and the waving flag on ships and buildings. Players refer to a banner by sim::BannerId; the
// table is open-ended (ADR 0007, section 4).
//
// Player flags are one generic flag tinted in the banner colour (flag_tint.hpp), so any colour
// works without new graphics. Banners with their own motif (the pirates' skull) keep sprites.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/sim/player.hpp"

namespace o1602::render {

enum class FlagStyle : std::uint8_t {
    None,     ///< no flag (natives, free trader)
    Tinted,   ///< the generic flag in `colour`
    Sprites,  ///< own images (`flag_gfx` in the ship sheet), e.g. the pirates' skull
};

struct Banner {
    std::string id;       ///< stable name, e.g. "anno1602:red"
    assets::Rgba colour;  ///< display colour and flag tint
    FlagStyle flag = FlagStyle::Tinted;
    std::uint32_t flag_gfx = 0;  ///< FlagStyle::Sprites: first image in the ship sheet
    /// FlagStyle::Sprites: how many; the tinted flag has its template's (FlagTemplate::frames).
    std::uint32_t flag_frames = 8;
    /// Units and interface art drawn per colour (soldiers, riders, coats of arms, message icons) use
    /// the original images of this colour (0 red, 1 blue, 2 yellow, 3 white); without it they are
    /// tinted in `colour` (unit_tint.hpp, variant_tint.hpp).
    std::optional<std::uint8_t> unit_variant;
    /// Without unit_variant: which of four horses (0 brown, 1 grey, 2 dark brown, 3 white,
    /// UnitTintColour::mount) the riders get. add_colour picks it from the banner's id, so it stays the same in
    /// every session and on every computer of a multiplayer game.
    std::uint8_t unit_mount = 3;
    /// Without unit_variant: which of the four original coats of arms (0 red's cross, 1 blue's
    /// quarters, 2 yellow's stripes, 3 white's plain field) its interface art takes the pattern
    /// from (BannerArt). add_colour picks it from the banner's id like unit_mount.
    std::uint8_t art_pattern = 3;
};

/// Interface art the original draws once per player colour (coats of arms, message icons, the
/// cities' list icons): variant v is sprite `first + v * stride`. A banner with a drawn variant
/// (Banner::unit_variant, the same red, blue, yellow, white order) or a sprite of its own shows
/// that; every other banner shows variant `base` recoloured in its colour, with the pattern of
/// variant Banner::art_pattern (variant_tint.hpp).
struct BannerArt {
    std::int32_t first = 0;
    std::int32_t stride = 1;
    std::uint8_t variants = 4;
    std::uint8_t base = 3;  ///< white
    /// Sprites drawn for one banner (Banner::id), as an offset from `first`: the pirates' skull among
    /// the military message icons.
    std::vector<std::pair<std::string, std::int32_t>> own;

    /// The four colours of the original one after the other from `first`.
    [[nodiscard]] static BannerArt at(std::int32_t first) {
        BannerArt art;
        art.first = first;
        return art;
    }
};

/// What to draw for a banner: a sprite, recoloured if `tint` is set.
struct BannerArtSprite {
    std::int32_t gfx = 0;
    std::optional<assets::Rgba> tint;
    std::uint8_t pattern = 3;  ///< with `tint`: the variant whose pattern the cloth gets
    friend bool operator==(const BannerArtSprite&, const BannerArtSprite&) = default;
};

/// The sprite of `art` for `banner`; no banner: the base variant as drawn.
[[nodiscard]] BannerArtSprite banner_art(const BannerArt& art, const Banner* banner);

/// Opacity of the ownership overlay diamonds.
inline constexpr std::uint8_t kOverlayAlpha = 110;

/// The generic flag: `frames` images in the ship sheet starting at `gfx`. The base game takes the
/// white flag (FAHNE4) and turns it into a tint template (flag_tint.hpp).
struct FlagTemplate {
    std::uint32_t gfx = 0;
    std::uint32_t frames = 8;
    bool from_original = true;  ///< convert an original coloured flag (false: already a template)
};

struct BannerOptions {
    /// Use the original red, blue, yellow and white flag images instead of the tinted generic
    /// flag (pixel-faithful to the original, including its 256-colour dithering).
    bool original_player_flags = false;
};

class BannerTable {
public:
    /// The seven banners of the original. Red, blue, yellow and white use the tinted generic flag
    /// in the colours measured from the original flags; pirates keep FAHNEPIRAT; natives and the
    /// free trader have none. `figures` supplies the flag images (table 0x00498F2C).
    [[nodiscard]] static BannerTable original(const assets::FigureTable* figures = nullptr,
                                              const BannerOptions& options = {});

    /// Colours offered for new banners: the four of the original and eight more that stay
    /// distinguishable on land, water and the minimap (the menu also allows any RGB colour).
    static const std::array<assets::Rgba, 12> kDefaultColours;
    /// The former name (LC-20).
    [[deprecated("use kDefaultColours")]] static inline const std::array<assets::Rgba, 12>& kDefaultColors =
        kDefaultColours;

    [[nodiscard]] const Banner* get(sim::BannerId id) const noexcept {
        return id < m_banners.size() ? &m_banners[id] : nullptr;
    }
    [[nodiscard]] std::size_t size() const noexcept { return m_banners.size(); }
    [[nodiscard]] const std::vector<Banner>& banners() const noexcept { return m_banners; }
    sim::BannerId add(Banner banner);
    /// The banners of the fifth player on (sim::kFirstExtraBanner), after the original's seven:
    /// the eight colours after the original's four, those that stand out best on the minimap's
    /// land and are furthest from the natives' brown first.
    void add_more_players();
    /// A banner with the generic flag in `colour`; its riders get a horse and its interface art a coat
    /// of arms pattern picked from `id`.
    sim::BannerId add_colour(std::string id, assets::Rgba colour);
    [[deprecated("use add_colour")]] sim::BannerId add_color(std::string id, assets::Rgba colour) {
        return add_colour(std::move(id), colour);
    }

    /// New coats of arms patterns (Banner::art_pattern) for banners [first, first + count): each
    /// run of four takes the four patterns in an order drawn from `seed`, the same on every
    /// platform. For menus that show off the colours (the colour screen); a game's banners keep
    /// the patterns add_colour gave them.
    void shuffle_art_patterns(sim::BannerId first, std::size_t count, std::uint32_t seed);

    [[nodiscard]] const std::optional<FlagTemplate>& flag_template() const noexcept { return m_template; }
    void set_flag_template(FlagTemplate t) { m_template = t; }

private:
    std::vector<Banner> m_banners;
    std::optional<FlagTemplate> m_template;
};

}  // namespace o1602::render
