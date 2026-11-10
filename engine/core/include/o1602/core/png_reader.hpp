// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads PNG files, the images of content packages (docs/modding/graphics.md): every colour type
// and bit depth of the PNG standard, without interlacing. Needs zlib (compress.hpp).
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::core {

/// The largest image read: 16384 pixels wide or high, 64 megapixels in all.
inline constexpr std::uint32_t kMaxPngSide = 16384;
inline constexpr std::uint64_t kMaxPngPixels = std::uint64_t{64} << 20;

struct PngImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;  ///< width * height * 4 bytes, rows from the top
};

/// Width and height from the header (the first 33 bytes of the file are enough), without the pixels.
[[nodiscard]] Result<PngImage> png_size(std::span<const std::uint8_t> file);

/// The pixels as RGBA8. 16-bit samples keep their high byte; transparency of a palette, grey or
/// RGB image (tRNS) becomes alpha.
[[nodiscard]] Result<PngImage> decode_png(std::span<const std::uint8_t> file);

}  // namespace o1602::core
