// SPDX-License-Identifier: GPL-3.0-or-later
//
// Isometric island rendering following the original renderer (1602.exe, row routine 0x00489C90).
// Specification: docs/re/rendering.md.
//
// Rendering is split into two steps so the same logic can drive the software rasterizer used by
// tools and tests and, later, the GPU renderer:
//   1. build_draw_list(): which sprite goes where, in painter's order
//   2. rasterize():       blit the sprites into an RGBA image
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/production.hpp"

namespace o1602::render {

/// The three graphic sets of the original: GFX (tile 64 px wide), MGFX (32), SGFX (16).
enum class Zoom : std::uint8_t { Large = 0, Medium = 1, Small = 2 };

[[nodiscard]] constexpr std::int32_t tile_width(Zoom zoom) noexcept {
    return 64 >> static_cast<int>(zoom);
}
[[nodiscard]] constexpr std::int32_t tile_height(Zoom zoom) noexcept {
    return tile_width(zoom) / 2;
}

/// Terrain/building sprite sheet of a zoom level, relative to the game directory.
[[nodiscard]] constexpr const char* terrain_sheet_path(Zoom zoom) noexcept {
    constexpr const char* kSheets[] = {"gfx/stadtfld.bsh", "mgfx/stadtfld.bsh", "sgfx/stadtfld.bsh"};
    return kSheets[static_cast<int>(zoom) % 3];
}

/// Ship sheet (figures with Blocknr 2: ships, flags, bow waves) of a zoom level.
[[nodiscard]] constexpr const char* ship_sheet_path(Zoom zoom) noexcept {
    constexpr const char* kSheets[] = {"gfx/ship.bsh", "mgfx/ship.bsh", "sgfx/ship.bsh"};
    return kSheets[static_cast<int>(zoom) % 3];
}

/// Palette of the terrain sheets, relative to the game directory.
inline constexpr const char* kTerrainPalettePath = "toolgfx/stadtfld.col";

struct RenderOptions {
    Zoom zoom = Zoom::Large;
    /// Global animation step added to every animated tile (the original advances a frame counter
    /// per building type every AnimTime; for still images a single global step is sufficient).
    std::uint32_t anim_step = 0;
    /// View rotation 0..3, in the original's order (compared with the running game,
    /// docs/original-comparison.md section 4).
    std::uint8_t view_rotation = 0;
};

/// One sprite to draw. (x, y) is the left/bottom anchor of the sprite, as in the original blit
/// routine (0x0048A140), relative to the island's top tile corner.
struct DrawCommand {
    /// Not a sprite but a translucent tile diamond (tile width x tile height, given by `tint_size`)
    /// in the colour `tint` (alpha = opacity), e.g. for the ownership view. (x, y) is its
    /// left / bottom corner box.
    static constexpr std::uint32_t kOverlayDiamond = 0xFFFFFFFF;
    /// Not a sprite but a dashed outline along the edges `edges` of the same tile
    /// diamond, in the colour `tint`: the border of a catchment circle.
    static constexpr std::uint32_t kOverlayOutline = 0xFFFFFFFE;
    /// Not a sprite but the same tile diamond hatched: every second pixel (a checkerboard on the
    /// world's pixels, see hatch_pixel) in the colour `tint`. The area dragged out for fields or
    /// trees.
    static constexpr std::uint32_t kOverlayHatch = 0xFFFFFFFD;

    /// Edges of a tile diamond (bits of `edges`), named by their side on screen.
    static constexpr std::uint8_t kTopLeft = 1;
    static constexpr std::uint8_t kTopRight = 2;
    static constexpr std::uint8_t kBottomRight = 4;
    static constexpr std::uint8_t kBottomLeft = 8;

    /// How a sprite is drawn.
    enum class Mode : std::uint8_t {
        Normal,
        /// Only the sprite's shape, filled with `tint` (alpha = opacity): figure shadows. The
        /// original dithers them (every second pixel black, 0x00401D10); the classic graphics
        /// draw them so (dither.hpp), otherwise a translucent fill of about the same darkness.
        Shadow,
        /// The sprite with opacity `tint.a`: chimney smoke. The original dithers it like the
        /// shadows (0x004048E0), as the classic graphics do.
        Translucent,
        /// The sprite darkened: its colours times `tint` (r, g, b / 255). The original dims the
        /// tiles the player cannot build on through a shading table of its palette (tables 1 and 2
        /// of 0x005B6360, docs/re/rendering.md#shading-tables).
        Dimmed,
    };

    std::uint32_t sprite = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    assets::Rgba tint{0, 0, 0, 0};  ///< colour of an overlay diamond or shadow
    std::uint8_t tint_size = 0;     ///< tile width of an overlay diamond (64 / 32 / 16)
    Mode mode = Mode::Normal;
    /// Rows at the bottom of the sprite left out: workers and animals standing in crops and young
    /// trees (0x00401B70 with 0x00451250).
    std::uint16_t cut_bottom = 0;
    std::uint8_t edges = 0;  ///< edges of an outline (kTopLeft ...)

    /// Whether this is an untextured overlay, not a sprite.
    [[nodiscard]] bool overlay() const noexcept { return sprite >= kOverlayHatch; }
};

/// The pixels of an outline command: calls `f(x, y)` for the left pixel of each lit two-pixel
/// step, relative to the command's (x, y). The diamond is the one of kOverlayDiamond (rows
/// y - w/2 .. y - 1); every other two rows are left out (the dashes), counted on the world's
/// pixel rows so the dashes of neighbouring tiles line up and stay put while scrolling.
template <class F>
void for_each_outline_step(const DrawCommand& c, F&& f) {
    const std::int32_t w = c.tint_size;
    const std::int32_t h = w / 2;
    for (std::int32_t row = 0; row < h; ++row) {
        const std::int32_t y = row - h;
        if (((c.y + y) / 2) % 2 != 0) {
            continue;
        }
        const bool upper = row < h / 2;
        const std::int32_t d = upper ? row : h - 1 - row;
        const std::int32_t half = (d + 1) * 2;
        const std::int32_t cx = w / 2;
        if ((c.edges & (upper ? DrawCommand::kTopLeft : DrawCommand::kBottomLeft)) != 0) {
            f(cx - half, y);
        }
        if ((c.edges & (upper ? DrawCommand::kTopRight : DrawCommand::kBottomRight)) != 0) {
            f(cx + half - 2, y);
        }
    }
}

/// Whether the pixel (x, y) of the draw list's coordinates is one of the hatched ones of
/// kOverlayHatch. Counted on the world's pixels, so the pattern of neighbouring tiles lines up.
[[nodiscard]] constexpr bool hatch_pixel(std::int32_t x, std::int32_t y) noexcept {
    return ((x + y) & 1) == 0;
}

/// Sprite of a building tile showing animation frame `frame`: base sprite + frame * AnimAdd +
/// rotation step (0x00489C90).
[[nodiscard]] std::uint32_t building_sprite(const sim::MapCell& cell, const assets::BuildingType& building,
                                            std::uint32_t frame, std::uint8_t view_rotation);

/// Animation frame of a tile without production state: (tile phase + global frame) % AnimAnz
/// (default case of 0x00489C90).
[[nodiscard]] std::uint32_t default_frame(const sim::MapCell& cell, const assets::BuildingType& building,
                                          std::uint32_t global_frame);

/// Whether the original chooses the frames of this building from its production state
/// (HAUS_PRODTYP Kind 1..7 with at least two frames).
[[nodiscard]] bool has_production_animation(const assets::BuildingType& building);

/// Animation frame of a production building (0x00489C90 with 0x0047CC80, 0x0047CCD0, 0x0047CD10).
/// `production` is the state of the building, nullptr if it has none (the original then treats
/// the stock and progress as 0 and the phase as the global frame):
/// - LagAniFlg: the frame shows how full the store is, (AnimAnz - 1) * stock / Maxlager, rounded.
/// - Market: the frame follows the progress of the current cycle, (AnimAnz - 1) * progress / 512.
/// - otherwise: a working building runs, (phase + global frame) % AnimAnz; an idle or disabled one
///   stands still at its phase.
/// `phase` replaces the saved phase of the building (the presentation's own, see
/// ProductionPhases); without it the phase stored with the building is taken.
[[nodiscard]] std::uint32_t production_frame(const assets::BuildingType& building, std::uint32_t global_frame,
                                             const sim::Production* production,
                                             std::optional<std::uint8_t> phase = std::nullopt);

/// New animation phase of a production building whose work starts or stops (0x004638C0), so the
/// picture continues smoothly: starting keeps the current frame ((phase + frame) = old phase);
/// stopping freezes the current frame with Anicontflg, otherwise the animation returns to frame 0.
[[nodiscard]] std::uint8_t phase_after_change(const assets::BuildingType& building, std::uint8_t phase,
                                              std::uint32_t global_frame, bool working);

/// Sprite index for a map cell with an explicit global animation frame of its building type
/// (default_frame). `building` may be null (sprite unchanged).
[[nodiscard]] std::uint32_t animated_sprite(const sim::MapCell& cell, const assets::BuildingType* building,
                                            std::uint32_t global_frame, std::uint8_t view_rotation);

/// Sprite index for a map cell, global frame = AnimFrame + options.anim_step.
[[nodiscard]] std::uint32_t sprite_for_cell(const sim::MapCell& cell, const assets::BuildingType* building,
                                            const RenderOptions& options);

/// Painter's order: screen rows of tiles from back to front; per row first all flat tiles,
/// then all tiles with Highflg (the original draws figures between the two passes).
[[nodiscard]] std::vector<DrawCommand> build_draw_list(const sim::IslandMap& map,
                                                       const assets::BuildingTable& buildings,
                                                       const RenderOptions& options);

struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;  ///< RGBA8, row major
};

/// Draws the commands into a tightly fitting image. Sprites that do not exist in the sheet are
/// skipped and counted in `missing_sprites`.
[[nodiscard]] RgbaImage rasterize(const std::vector<DrawCommand>& commands, const assets::SpriteSheet& sheet,
                                  const assets::Palette& palette, assets::Rgba background = {0, 0, 0, 0},
                                  std::size_t* missing_sprites = nullptr);

}  // namespace o1602::render
