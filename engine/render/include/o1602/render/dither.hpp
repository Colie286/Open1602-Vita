// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original's look of shadows and chimney smoke. The original cannot blend: it draws a figure's
// shadow as every second pixel black (0x00401D10) and smoke as every second pixel of its picture
// (0x00401D50 -> 0x004048E0), a checkerboard on the screen's pixels. Open1602 blends them by
// default (DrawCommand::Mode::Shadow, Mode::Translucent); with the classic graphics it draws them
// from copies of their sheets that keep only every second pixel, one copy for each of the two
// ways the checkerboard can lie on a sprite, so the pattern stays fixed to the world's pixels as
// a figure moves (docs/re/figures.md#drawing).
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/render/island_renderer.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/sprite_source.hpp"

namespace o1602::render {

/// The sprites of a source twice: block p (0 or 1) keeps pixel (x, y) only where (x + y + p) is
/// even, the rest is transparent.
class DitheredSource final : public SpriteSource {
public:
    explicit DitheredSource(const SpriteSource& source) noexcept : m_source(source) {}
    [[nodiscard]] std::uint32_t count() const override { return m_source.count() * 2; }
    void size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const override;
    bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const override;

private:
    const SpriteSource& m_source;
};

/// A sheet of the atlas and where its dithered copies are.
struct DitheredSheet {
    std::uint32_t base = 0;      ///< atlas index of the sheet's first sprite
    std::uint32_t count = 0;     ///< sprites of the sheet
    std::uint32_t dithered = 0;  ///< atlas index of block 0 of its DitheredSource
};

/// The dithered sheets of a zoom level (WorldGraphics::dither).
struct Dither {
    std::vector<DitheredSheet> sheets;

    /// A shadow or a translucent sprite of a dithered sheet in the original's look: a shadow
    /// fills every second pixel of its shape in its full tint colour, a translucent sprite shows
    /// every second pixel of its picture, both on the pixels of the draw list where hatch_pixel
    /// does not hold (the original's even pixels, one row up). Anything else stays as it is.
    void apply(DrawCommand& c, const SpriteAtlas& atlas) const noexcept;
};

}  // namespace o1602::render
