// SPDX-License-Identifier: GPL-3.0-or-later
//
// Art in any player colour from art drawn in one: the big banners of the colour screen
// (FARBWAHL.GAD). Each of the four is drawn once, in its own colour and shape, so there are no
// variants to compare (variant_tint.hpp); what belongs to the cloth comes from a second sprite
// that outlines it (the banners' selection outlines). The cloth keeps its own folds, motif and
// shadows, only in another colour.
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/palette.hpp"
#include "o1602/render/unit_tint.hpp"

namespace o1602::render {

/// Recolours `sprite` into `out` (RGBA8, the sprite's size). The cloth is every opaque pixel of
/// the sprite that lies, on its row, between the leftmost and the rightmost opaque pixel of
/// `shape` (both with their top left at the same place); a row of the shape narrower than what
/// the nearest rows above and below a gap of up to two rows have in common takes their common
/// part (the outlines have gaps of a row or two).
/// Cloth gets the grey kShapeTintReference * luminance / reference tinted in `colour`
/// (flag_tint.hpp), where reference is the luminance that a quarter of the cloth exceeds: the lit
/// side of the folds comes out a little brighter than the colour, the motif and the shadows darker
/// (the yellow banner's black stripes stay black). With no cloth the sprite comes out as drawn,
/// with no sprite nothing.
void tint_shape(const RgbaView& sprite, const RgbaView& shape, assets::Rgba colour, std::vector<std::uint8_t>& out);

inline constexpr double kShapeTintReference = 136.0;

}  // namespace o1602::render
