// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the sprites of an atlas come from: a BSH sheet of the original, or a sheet of the next
// larger zoom level reduced to half size for zoom levels the original has no graphics for
// (the SGFX of some editions lacks animals, carriers and soldiers). Mods will add sources for their own images.
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"

namespace o1602::render {

class SpriteSource {
public:
    virtual ~SpriteSource() = default;
    [[nodiscard]] virtual std::uint32_t count() const = 0;
    /// Size of sprite i; 0 x 0 for missing or empty sprites.
    virtual void size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const = 0;
    /// RGBA8 pixels of sprite i (width * height * 4, alpha 0 or 255). False if it cannot be decoded.
    virtual bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const = 0;
};

/// The sprites of a BSH sheet, coloured with a palette.
class SheetSource final : public SpriteSource {
public:
    SheetSource(const assets::SpriteSheet& sheet, const assets::Palette& palette) noexcept
        : m_sheet(sheet), m_palette(palette) {}
    [[nodiscard]] std::uint32_t count() const override;
    void size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const override;
    bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const override;

private:
    const assets::SpriteSheet& m_sheet;
    const assets::Palette& m_palette;
};

/// The sprites of a BSH sheet reduced to half size: each 2 x 2 block becomes one pixel, opaque if
/// at least two of its pixels are, with the average colour of its opaque pixels. Used for the
/// smallest zoom level, where the original shows no figures (configurable, see WorldGraphics).
class HalfSizeSource final : public SpriteSource {
public:
    HalfSizeSource(const assets::SpriteSheet& sheet, const assets::Palette& palette) noexcept
        : m_sheet(sheet), m_palette(palette) {}
    [[nodiscard]] std::uint32_t count() const override;
    void size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const override;
    bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const override;

private:
    const assets::SpriteSheet& m_sheet;
    const assets::Palette& m_palette;
};

/// Halves an RGBA8 image as HalfSizeSource does (exposed for tests).
void halve_rgba(const std::vector<std::uint8_t>& src, std::uint32_t width, std::uint32_t height,
                std::vector<std::uint8_t>& dst, std::uint32_t& out_width, std::uint32_t& out_height);

}  // namespace o1602::render
