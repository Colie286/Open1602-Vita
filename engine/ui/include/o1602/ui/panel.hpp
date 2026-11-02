// SPDX-License-Identifier: GPL-3.0-or-later
//
// Gadget layouts at work: where the regions of the screen are, what a layout
// draws in which state, and how it answers the mouse (press, release, toggles, radio groups,
// select-on-drag, sliders, menu buttons) as the original does. Pure logic:
// a panel makes a list of things to draw, the frontend draws them.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "o1602/assets/gad.hpp"
#include "o1602/ui/font.hpp"

namespace o1602::ui {

struct Rect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;
    [[nodiscard]] bool contains(std::int32_t px, std::int32_t py) const noexcept {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
    friend bool operator==(const Rect&, const Rect&) = default;
};

/// Design size of the original's screens: every layout is in units of a 1024 x 768 screen.
inline constexpr std::int32_t kDesignWidth = 1024;
inline constexpr std::int32_t kDesignHeight = 768;

/// The regions layouts belong to: the panel on the right, the status bar below the
/// game view, the tutorial window above it, and the whole screen (menus).
struct ScreenRegions {
    double scale = 1.0;  ///< screen width / 1024
    Rect side_panel;
    Rect status_bar;
    Rect tutor;
    Rect full;
    Rect game_view;  ///< what is left for the map
};
[[nodiscard]] ScreenRegions screen_regions(std::int32_t width, std::int32_t height) noexcept;

/// The regions at a chosen scale (the original: width / 1024; Open1602 keeps the whole panel on
/// screen with min(width / 1024, height / 768)).
[[nodiscard]] ScreenRegions screen_regions(std::int32_t width, std::int32_t height, double scale) noexcept;

/// Design units to pixels: trunc((v + 0.5) * scale), as the original rounds every element.
[[nodiscard]] std::int32_t to_pixels(std::int32_t v, double scale) noexcept;

/// A sprite of a family the original draws once per player colour (coats of arms, message icons),
/// recoloured for a colour it was not drawn in. The family's variants are `stride` sprites apart;
/// the sprite drawn is variant `base` (white in the original). Pixels that differ between the
/// variants take the colour, shaded like variant `pattern` (its coat of arms: cross, stripes, ...);
/// the rest stays as drawn (render/variant_tint.hpp). With `shape` the sprite has no variants:
/// the cloth is what sprite `gfx + shape` of the same sheet outlines, shaded by its own brightness
/// (render/shape_tint.hpp; the banners of the colour screen).
struct ColourTint {
    assets::Rgba colour{};
    std::int32_t stride = 1;
    std::uint8_t variants = 4;  ///< red, blue, yellow, white in the original
    std::uint8_t base = 3;
    std::uint8_t pattern = 3;
    std::optional<std::int32_t> shape{};
    friend bool operator==(const ColourTint&, const ColourTint&) = default;
};

/// Something a panel draws, in pixels relative to its region.
struct DrawItem {
    enum class Kind : std::uint8_t {
        Sprite,        ///< sprite `gfx` of sheet `block` with its top left at (x, y)
        SpriteMask,    ///< the sprite's shape in palette colour `colour`
        SpriteRaster,  ///< the shape as a checkerboard of `colour`
        Text,          ///< `text` in font `block`, text colour `colour`, aligned at x, cell bottom at y
        Fill,          ///< rectangle (x, y, w, h) in palette colour `colour` (or ui::rgb_ink)
        Raster,        ///< checkerboard over (x, y, w, h) (disabled gadgets), black
    };
    Kind kind = Kind::Sprite;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;
    std::int32_t block = 0;
    std::int32_t gfx = 0;
    std::int32_t colour = 0;
    /// Text: the letters in this palette colour instead of the text colour's (a player's name in
    /// their banner's colour; the shadow stays the text colour's); -1: the text colour's.
    std::int32_t ink = -1;
    Align align = Align::Left;
    bool half = false;   ///< drawn as a 50 % checkerboard (Alphaflg)
    bool faded = false;  ///< sprite, mask or fill on the screen's even pixels only
    std::string text;
    std::optional<ColourTint> tint;  ///< Sprite: recoloured
    /// Sprite: only the art's rows first_row .. first_row + rows - 1 (in the layout's pixels), each
    /// where it would be; rows 0: all of it.
    std::int32_t first_row = 0;
    std::int32_t rows = 0;
};

/// What the mouse did to a panel.
struct PanelEvent {
    enum class Kind : std::uint8_t {
        Pressed,  ///< a menu gadget was pressed (Menuflg: the original's callback)
        Clicked,  ///< released on the gadget it was pressed on
        Slid,     ///< a slider moved (every frame while held)
    };
    Kind kind = Kind::Clicked;
    std::int32_t id = 0;
};

class Panel {
public:
    Panel(assets::GadgetLayout layout, double scale);

    [[nodiscard]] const assets::GadgetLayout& layout() const noexcept { return m_layout; }
    [[nodiscard]] double scale() const noexcept { return m_scale; }
    void set_scale(double scale) noexcept { m_scale = scale; }

    // State the game sets; unknown Ids are ignored.
    void set_text(std::int32_t id, std::string text);
    /// A text gadget's text cut so that it ends `room` design units right of the gadget's position
    /// (by default its width), its end replaced by "..." (a long savegame name ran past the list).
    void set_fit(std::int32_t id, bool on, std::optional<std::int32_t> room = std::nullopt);
    void set_selected(std::int32_t id, bool on);
    [[nodiscard]] bool selected(std::int32_t id) const;
    void set_hidden(std::int32_t id, bool on);
    /// Not clickable.
    void set_no_select(std::int32_t id, bool on);
    /// Hidden and not clickable, or both undone: what the original's windows do with gadgets that
    /// do not apply (hidden ones would still answer the mouse).
    void set_visible(std::int32_t id, bool on);
    /// Greyed (gadget +0x19 bit 3): drawn as usual with black over every second pixel,
    /// of the picture's shape for a graphic, of the rectangle for the others. Still clickable.
    void set_disabled(std::int32_t id, bool on);
    /// Faded (+0x19 bit 4): a graphic, fill or bar drawn in its own colours on every
    /// second pixel only, the background showing through the others; texts unchanged. Still clickable. The original
    /// fades the locked parts of a campaign and the Kontor's trade buttons without a ware.
    void set_faded(std::int32_t id, bool on);
    /// Slider value scaled to `range` ((value * range + size / 2) / size) and back.
    [[nodiscard]] std::int32_t slider(std::int32_t id, std::size_t axis, std::int32_t range) const;
    void set_slider(std::int32_t id, std::size_t axis, std::int32_t value, std::int32_t range);
    /// First line of a flowing text shown (TEXTFL scrolling).
    void set_scroll(std::int32_t id, std::int32_t line);
    /// Last first line that still fills the flowing text's box (0 if the text fits); callers clamp
    /// their scroll to it so "down" past the end does not blank the window (UI-12).
    [[nodiscard]] std::int32_t max_scroll(std::int32_t id, const std::array<const Font*, kFontSlots>& fonts) const;
    /// Lines a flowing text's box shows (Size.y / Posoffs.y), 0 for other gadgets.
    [[nodiscard]] std::int32_t flow_rows(std::int32_t id) const;
    /// Another sprite and pressed step for a gadget: message icons. With
    /// `tint` the sprite is recoloured (a player colour the art was not drawn in).
    void set_graphic(std::int32_t id, std::int32_t gfx, std::int32_t press_offset,
                     std::optional<ColourTint> tint = std::nullopt);
    /// Only a band of a graphic's rows (in the layout's pixels), each drawn where it would be; a
    /// picture is cut so into pieces placed apart (the settings' voice page). `count` 0: all rows.
    void set_rows(std::int32_t id, std::int32_t first, std::int32_t count);
    /// Another draw offset (Posoffs) for a gadget: art centred in its box, groups moved together.
    void set_offset(std::int32_t id, std::int32_t x, std::int32_t y);
    /// Moves a gadget, click area and picture (Open1602 rearranges a few windows, e.g. U-013).
    void set_position(std::int32_t id, std::int32_t x, std::int32_t y);
    /// Another size for a gadget (a flowing text in a window that grew with the screen).
    void set_size(std::int32_t id, std::int32_t w, std::int32_t h);
    /// Top left of a gadget's click area, nothing if the layout has no such gadget.
    [[nodiscard]] std::optional<std::array<std::int32_t, 2>> position(std::int32_t id) const;

    /// The mouse at (x, y) in pixels relative to the region, button down or up.
    std::vector<PanelEvent> mouse(std::int32_t x, std::int32_t y, bool down);
    /// Gadget Id under the mouse at the last call (for the status line's help text).
    [[nodiscard]] std::optional<std::int32_t> hovered() const;
    /// Gadget Id held down by the mouse now (a slider being dragged), nothing if none.
    [[nodiscard]] std::optional<std::int32_t> held() const;

    /// Everything to draw, in paint order. `fonts` are the three font slots (for flowing text).
    [[nodiscard]] std::vector<DrawItem> draw(const std::array<const Font*, kFontSlots>& fonts) const;

private:
    struct State {
        bool selected = false;
        bool hidden = false;
        bool disabled = false;
        bool faded = false;
        bool no_select = false;
        std::array<std::int32_t, 2> slider{};
        std::int32_t scroll = 0;
        std::string text;
        bool fit = false;
        std::optional<std::int32_t> fit_room;
        std::optional<std::pair<std::int32_t, std::int32_t>> graphic;  ///< gfx, press offset
        std::optional<ColourTint> tint;
        std::int32_t first_row = 0;
        std::int32_t rows = 0;
    };
    /// A flowing text's wrapped lines and how many fit its box; nothing if it draws no text.
    [[nodiscard]] std::optional<std::pair<std::vector<std::string>, std::int32_t>> flow_lines(
        std::size_t index, const std::array<const Font*, kFontSlots>& fonts) const;
    [[nodiscard]] std::optional<std::size_t> hit(std::int32_t x, std::int32_t y) const;
    void draw_gadget(std::vector<DrawItem>& out, std::size_t index, std::uint8_t state,
                     const std::array<const Font*, kFontSlots>& fonts) const;
    void slide(std::size_t index, std::int32_t gx, std::int32_t gy);
    [[nodiscard]] std::uint8_t state_of(std::size_t index) const;

    assets::GadgetLayout m_layout;
    std::vector<State> m_state;
    double m_scale;
    std::optional<std::size_t> m_pressed;
    std::optional<std::size_t> m_hovered;
    bool m_down = false;
};

}  // namespace o1602::ui
