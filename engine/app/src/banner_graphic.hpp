// SPDX-License-Identifier: GPL-3.0-or-later
//
// Interface art in a player's colour for any banner (render::BannerArt): the drawn variant where
// the original has one, else the white one recoloured. Every window that shows a coat of arms or
// a message icon goes through here, so further colours and mods' banners need no new graphics.
#pragma once

#include <cstdint>
#include <optional>

#include "o1602/render/banner.hpp"
#include "o1602/sim/player.hpp"
#include "o1602/ui/panel.hpp"

namespace o1602::app {

/// The banner of a player, nothing without a table or player.
[[nodiscard]] inline const render::Banner* banner_of(const render::BannerTable* banners, const sim::Player* player) {
    return banners != nullptr && player != nullptr ? banners->get(player->banner) : nullptr;
}

/// The colour variant of `art` for `banner` on gadget `id` (Panel::set_graphic).
inline void set_banner_graphic(ui::Panel& panel, std::int32_t id, const render::BannerArt& art,
                               const render::Banner* banner, std::int32_t press_offset = 0) {
    const render::BannerArtSprite s = render::banner_art(art, banner);
    std::optional<ui::ColourTint> tint;
    if (s.tint) {
        tint = ui::ColourTint{*s.tint, art.stride, art.variants, art.base, s.pattern};
    }
    panel.set_graphic(id, s.gfx, press_offset, tint);
}

}  // namespace o1602::app
