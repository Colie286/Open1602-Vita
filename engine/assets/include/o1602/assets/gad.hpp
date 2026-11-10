// SPDX-License-Identifier: GPL-3.0-or-later
//
// Layouts of the user interface: GADDATA/*.GAD (docs/formats/gad.md). Plain text in the language of
// the .cod files (not encoded), one "Objekt: GADGET" block of numbered gadgets; the loader of the
// original is 0x00405480. Coordinates are design units of a 1024 x 768 screen, relative to the
// region the layout belongs to.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/game_directory.hpp"
#include "o1602/core/result.hpp"

namespace o1602::assets {

/// GAD Kind (numeric values as the loader predefines them).
enum class GadgetKind : std::uint8_t {
    Unused = 0,         ///< GAD_UNUSED: not drawn, not clickable
    Area = 1,           ///< GAD_NIX: an invisible click area (minimap, parts of odd shaped buttons)
    Graphic = 2,        ///< GAD_GFX: a sprite
    GraphicRaster = 3,  ///< GAD_GFXRAST: the sprite's shape as a checkerboard of one colour
    GraphicFill = 4,    ///< GAD_GFXFILL: the sprite's shape in one colour
    TextLeft = 5,       ///< GAD_TEXTL
    TextCentre = 6,     ///< GAD_TEXTZ
    TextRight = 7,      ///< GAD_TEXTR
    Fill = 8,           ///< GAD_FILL: a rectangle of a palette colour
    Bar = 9,            ///< GAD_BALKEN: a bar following a slider
    TextFlow = 10,      ///< GAD_TEXTFL: wrapped text in lines
};

/// One gadget (record of 0x4C bytes in the original).
struct Gadget {
    std::int32_t id = 0;
    GadgetKind kind = GadgetKind::Unused;
    std::int32_t base = 0;                        ///< Basenr: index of the group's first or background gadget
    std::array<std::int32_t, 2> colour{};         ///< Color: text colour (or palette index) for state 0 / 1
    bool radio = false;                           ///< Reiheflg: one of a group (the gadgets with the same base)
    bool toggle = false;                          ///< Flipflg: a press inverts the selected state
    bool no_select = false;                       ///< Noselflg: not clickable
    bool select_on_move = false;                  ///< Selmoveflg: pressing or dragging onto it selects it
    bool menu = false;                            ///< Menuflg: the layout's callback hears the press
    bool draw_base = false;                       ///< Baseflg: draw the base gadget first
    bool base_when_selected = false;              ///< Baseselflg: ... only when not in state 0
    bool clip = false;                            ///< Clipflg: read by nobody in the original
    bool hidden = false;                          ///< Nodrawflg: not drawn (still clickable)
    bool disabled = false;                        ///< Rasterflg: a checkerboard over it
    bool half = false;                            ///< Alphaflg: drawn as a 50 % checkerboard
    bool slider = false;                          ///< Sliderflg (also set by the two below)
    bool slider_x = false;                        ///< Slidhorflg
    bool slider_y = false;                        ///< Slidverflg
    std::array<std::int16_t, 2> slider_offset{};  ///< Slidoffs
    std::array<std::int16_t, 2> slider_size{};    ///< Slidsize (0: the gadget's size)
    std::array<std::int16_t, 2> offset{};         ///< Posoffs: draw offset (text anchor; TEXTFL line height)
    std::int32_t press_offset = 0;                ///< Pressoff: sprite step for state 1
    std::array<std::int32_t, 2> pos{};            ///< Pos: top left of the click area
    std::array<std::int32_t, 2> size{};           ///< Size: the click area (not the sprite's size)
    std::int32_t block = 0;  ///< Blocknr: sprite sheet (0 TOOLS, 1 START, 2 SYMBOL, 3 BAUHAUS, 4 BAUSHIP) or font
    std::int32_t gfx = 0;    ///< Gfxnr: sprite in the sheet

    [[nodiscard]] bool text() const noexcept {
        return kind == GadgetKind::TextLeft || kind == GadgetKind::TextCentre || kind == GadgetKind::TextRight ||
               kind == GadgetKind::TextFlow;
    }
};

/// The gadgets of one file, by Nummer (paint order; the last gadget under the mouse is on top).
struct GadgetLayout {
    std::vector<Gadget> gadgets;
    std::vector<std::string> warnings;
    /// The constants the file and its includes define (TOOLS.INC: GFXBAUHAUS, GFXWARE, ...); the code
    /// of the original uses the same numbers for what it draws itself.
    std::map<std::string, std::int32_t, std::less<>> constants;

    /// First gadget with this Id, nullptr if none (Ids repeat between files, not within one).
    [[nodiscard]] const Gadget* find(std::int32_t id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> index_of(std::int32_t id) const noexcept;
    /// A constant of the file, nothing if it does not define it.
    [[nodiscard]] std::optional<std::int32_t> constant(std::string_view name) const;
};

/// Most gadgets of one file in the original (MAXGADGET); Open1602 takes more (L-010).
inline constexpr std::size_t kOriginalMaxGadgets = 300;
/// Most gadgets Open1602 reads from one file: gadgets with a higher Nummer are left out with a
/// warning, so a stray "Nummer: 2000000000" cannot ask for gigabytes (HI-2).
inline constexpr std::int32_t kMaxGadgets = 4096;

/// Reads a layout from its text. `include` returns the text of an included file ("TOOLS.INC")
/// relative to the layout, nothing if it is missing (then the constants stay 0, with a warning).
[[nodiscard]] GadgetLayout parse_gad(std::string_view text,
                                     const std::function<std::optional<std::string>(std::string_view)>& include = {});

/// Loads GADDATA/<name> of the game directory (case does not matter).
[[nodiscard]] core::Result<GadgetLayout> load_gad(const GameDirectory& game, std::string_view name);

}  // namespace o1602::assets
