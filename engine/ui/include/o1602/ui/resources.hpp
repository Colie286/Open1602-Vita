// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface's sprite sheets and fonts per screen size (Gfx_LoadResolutionAssets 0x00492960):
// the original has art for 640, 800 and 1024 pixels wide screens, the same indices in each.
#pragma once

#include <cstdint>
#include <string_view>

namespace o1602::ui {

enum class ArtSize : std::uint8_t { Small, Medium, Large };  ///< 640, 800, 1024 pixels wide

/// The original's choice: 640 and 1024 by name, every other width 800 (0x00492979).
[[nodiscard]] constexpr ArtSize original_art_size(std::int32_t width) noexcept {
    return width == 640 ? ArtSize::Small : width == 1024 ? ArtSize::Large : ArtSize::Medium;
}
/// Open1602's choice for any screen: the largest art that is not wider than the screen.
[[nodiscard]] constexpr ArtSize art_size(std::int32_t width) noexcept {
    return width >= 1024 ? ArtSize::Large : width >= 800 ? ArtSize::Medium : ArtSize::Small;
}

/// Pixels of the art (and fonts) of a size per pixel of the 1024 x 768 design.
[[nodiscard]] constexpr double art_scale(ArtSize size) noexcept {
    return size == ArtSize::Small ? 0.625 : size == ArtSize::Medium ? 0.78125 : 1.0;
}
/// The art size whose fonts text drawn at `scale` (screen pixels per pixel of the 1024 x 768
/// design) takes: the largest that is not shrunk there, so that no glyph loses rows or columns
/// (as in the original, a 800 or 640 pixels wide screen shows that screen's fonts at their size).
/// Below the small art's scale the small fonts.
[[nodiscard]] constexpr ArtSize text_art_size(double scale) noexcept {
    constexpr double kSlack = 1e-6;  // 600 / 768 is 0.78125 too, but a scale may come from a division
    return scale + kSlack >= art_scale(ArtSize::Large)    ? ArtSize::Large
           : scale + kSlack >= art_scale(ArtSize::Medium) ? ArtSize::Medium
                                                          : ArtSize::Small;
}

/// Sprite sheet of a Blocknr: 0 TOOLS, 1 START, 2 SYMBOL (one size), 3 BAUHAUS, 4 BAUSHIP; empty for others.
[[nodiscard]] constexpr std::string_view sheet_file(std::int32_t block, ArtSize size) noexcept {
    constexpr std::string_view kFiles[5][3] = {
        {"TOOLGFX/TOOLS6.BSH", "TOOLGFX/TOOLS8.BSH", "TOOLGFX/TOOLS.BSH"},
        {"TOOLGFX/START6.BSH", "TOOLGFX/START8.BSH", "TOOLGFX/START.BSH"},
        {"TOOLGFX/SYMBOL.BSH", "TOOLGFX/SYMBOL.BSH", "TOOLGFX/SYMBOL.BSH"},
        {"TOOLGFX/BAUHAUS6.BSH", "TOOLGFX/BAUHAUS8.BSH", "TOOLGFX/BAUHAUS.BSH"},
        {"TOOLGFX/BAUSHIP6.BSH", "TOOLGFX/BAUSHIP8.BSH", "TOOLGFX/BAUSHIP.BSH"},
    };
    return block >= 0 && block < 5 ? kFiles[block][static_cast<std::size_t>(size)] : std::string_view{};
}

/// Open1602's own font slot (font_file()).
inline constexpr std::int32_t kOwnFontSlot = 3;

/// Font of a slot: 0 normal (ZEINR), 1 small (ZEILITTLE), 2 large, digits only (ZEILARGE). The small
/// screen asks for ZEI20VL.ZEI, which no edition has; the original then takes ZEI20V (as here).
/// Slot 3 is Open1602's own: a medium font with a shadow for the rows of its parchment windows.
/// The History Edition has only the large screen's fonts and lacks ZEI14V; slot 1 stands in for it.
[[nodiscard]] constexpr std::string_view font_file(std::int32_t slot, ArtSize size) noexcept {
    constexpr std::string_view kFiles[4][3] = {
        {"TOOLGFX/ZEI14V.ZEI", "TOOLGFX/ZEI16V.ZEI", "TOOLGFX/ZEI20V.ZEI"},
        {"TOOLGFX/ZEI9A.ZEI", "TOOLGFX/ZEI11A.ZEI", "TOOLGFX/ZEI14A.ZEI"},
        {"TOOLGFX/ZEI20V.ZEI", "TOOLGFX/ZEI24V.ZEI", "TOOLGFX/ZEI28V.ZEI"},
        {"TOOLGFX/ZEI9A.ZEI", "TOOLGFX/ZEI11A.ZEI", "TOOLGFX/ZEI14V.ZEI"},
    };
    return slot >= 0 && slot < 4 ? kFiles[slot][static_cast<std::size_t>(size)] : std::string_view{};
}

}  // namespace o1602::ui
