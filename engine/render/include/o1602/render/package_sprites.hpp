// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sprites of content packages' images (assets/package_graphics.hpp, docs/modding/graphics.md)
// as sources of an atlas, and the terrain sheet with them at their sprite numbers; the pictures of
// their icons for the interface.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "o1602/assets/package_graphics.hpp"
#include "o1602/render/sprite_source.hpp"

namespace o1602::render {

/// The sprites of one image at one zoom level (0 = largest), decoded once: from its PNG for that
/// level, else from the next larger level's at half size. A pixel of less than half alpha is
/// transparent, any other opaque. Transparent rows above a sprite are left out: sprites stand on
/// their bottom row. An image that cannot be read gives empty sprites and an error().
class PackageImageSource final : public SpriteSource {
public:
    PackageImageSource(const assets::PackageImage& image, std::size_t zoom);
    [[nodiscard]] std::uint32_t count() const override { return static_cast<std::uint32_t>(m_sprites.size()); }
    void size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const override;
    bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const override;
    /// Why the image could not be read; empty if it was.
    [[nodiscard]] const std::string& error() const noexcept { return m_error; }

private:
    struct Sprite {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> rgba;
    };
    std::vector<Sprite> m_sprites;
    std::string m_error;
};

/// Sources at sprite numbers of their own: sprite `at + i` is sprite i of the source placed at
/// `at`; numbers no source has are empty. The sources must outlive it and must not overlap.
class PlacedSources final : public SpriteSource {
public:
    void place(std::uint32_t at, const SpriteSource& source);
    [[nodiscard]] std::uint32_t count() const override { return m_count; }
    void size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const override;
    bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const override;

private:
    struct Placed {
        std::uint32_t at = 0;
        const SpriteSource* source = nullptr;
    };
    /// The source sprite `i` belongs to, nullptr for none.
    [[nodiscard]] const Placed* find(std::uint32_t i) const noexcept;

    std::vector<Placed> m_placed;  ///< sorted by `at`
    std::uint32_t m_count = 0;
};

/// The picture of a package's icon, RGBA with its alpha as the PNG has it.
struct IconPixels {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
    std::string error;  ///< why the PNG could not be read; then the picture is empty
};

/// The picture of `icon` at `scale` (1: as the PNG is; the interface's smaller art takes less, at
/// most 1), each pixel the average of those it covers.
[[nodiscard]] IconPixels package_icon_pixels(const assets::PackageIcon& icon, double scale);

}  // namespace o1602::render
