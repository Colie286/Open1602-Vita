// SPDX-License-Identifier: GPL-3.0-or-later
//
// Art in any player colour from art drawn in a few. The original draws some interface pictures
// once per player colour (coats of arms, message icons: red, blue, yellow, white); a banner
// without its own drawn variant gets the white one recoloured. What belongs to the colour is
// found by comparing the variants: where they all differ is cloth, where they agree (the lips of
// a message icon, outlines, the selection frame) stays as drawn. Cloth is shaded like the generic
// flag (flag_tint.hpp), so a banner's art and its flag have the same colour; the shading may come
// from another variant, so that a further banner shows the cross, the quarters or the stripes of
// one of the original coats of arms in its own colour.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "o1602/assets/palette.hpp"
#include "o1602/render/unit_tint.hpp"

namespace o1602::render {

/// Recolours variant `base` of a family into `out` (RGBA8, the base's size). A pixel is cloth if
/// - every other variant of the same size is opaque there with another colour,
/// - the base is grey there (channels at most kVariantTintGreyRange apart; the white variant's
///   cloth is grey, its motifs are not) and not darker than kVariantTintMinLuminance (outlines),
/// - and at least kVariantTintNeighbours of its eight neighbours are cloth by the two rules above
///   (the variants' hand-dithered shadows differ in single pixels).
/// Cloth gets the grey 0.48 * luminance + 5 (original_flag_to_template) tinted in `colour`. The
/// luminance is variant `pattern`'s, scaled so that its cloth is as bright on average as the
/// base's (the yellow arms' black stripes stay dark, the field gets the brightness of the white
/// one); a `pattern` that is the base, out of range or of another size takes the base's own.
/// Every other pixel is the base's. Variants of another size are ignored; with none left the base
/// comes out as drawn, with `base` out of range nothing.
void tint_variant(std::span<const RgbaView> variants, std::size_t base, std::size_t pattern, assets::Rgba colour,
                  std::vector<std::uint8_t>& out);

inline constexpr int kVariantTintGreyRange = 47;
inline constexpr double kVariantTintMinLuminance = 64.0;
inline constexpr int kVariantTintNeighbours = 3;

}  // namespace o1602::render
