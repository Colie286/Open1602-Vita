// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tinting the generic player flag. Template convention (also for mods, docs/modding/flags.md):
// every pixel with R == G == B is part of the cloth and takes the player colour; its grey value
// is the shade - 128 is exactly the player colour, darker greys darken it towards black, lighter
// ones brighten it towards white. All other pixels (pole, outline, emblem) stay as they are.
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/palette.hpp"
#include "o1602/render/sprite_source.hpp"

namespace o1602::render {

/// Grey value that reproduces the player colour exactly.
inline constexpr std::uint8_t kTintNeutral = 128;

/// Colour of a cloth pixel with grey value `shade`.
[[nodiscard]] assets::Rgba tint_shade(std::uint8_t shade, assets::Rgba colour) noexcept;

/// Applies the tint to an RGBA8 image in place (grey pixels only).
void tint_rgba(std::vector<std::uint8_t>& rgba, assets::Rgba colour);

/// Turns an original flag image (one solid cloth) into a template: every opaque pixel becomes
/// grey with shade 0.48 * luminance + 5. The factors are fitted to the original red, blue and
/// yellow flags with the white flag as source (mean deviation ~15 of 255 per channel; the
/// originals are hand-dithered 256-colour images).
void original_flag_to_template(std::vector<std::uint8_t>& rgba);

/// The generic flag in several colours: `colours.size()` banners x `frames` images, banner-major.
/// Template images are frames [first, first + frames) of `base`.
class TintedFlagSource final : public SpriteSource {
public:
    TintedFlagSource(const SpriteSource& base, std::uint32_t first, std::uint32_t frames,
                     std::vector<assets::Rgba> colours, bool from_original) noexcept
        : m_base(base),
          m_first(first),
          m_frames(frames),
          m_colours(std::move(colours)),
          m_from_original(from_original) {}
    [[nodiscard]] std::uint32_t count() const override;
    void size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const override;
    bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const override;

private:
    const SpriteSource& m_base;
    std::uint32_t m_first;
    std::uint32_t m_frames;
    std::vector<assets::Rgba> m_colours;
    bool m_from_original;
};

}  // namespace o1602::render
