// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original's mouse cursors: resources of Language.dll (docs/re/ui.md "Mouse cursors"), 32 x 32
// black-and-white Windows cursors with a hot spot. The reader takes the cursor group of a resource
// id from a PE file and turns the first image into RGBA for the platform.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::assets {

/// Resource ids of the cursors in Language.dll (LoadCursor in 0x004948E1).
inline constexpr std::uint16_t kArrowCursor = 122;
/// The "double triangle" while a window waits for a click on the map.
inline constexpr std::uint16_t kTargetCursor = 125;

struct CursorImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t hot_x = 0;
    std::uint32_t hot_y = 0;
    /// width * height pixels, rows from the top, 4 bytes each (r, g, b, a). Pixels the original
    /// inverts the screen under are black.
    std::vector<std::uint8_t> rgba;
};

/// The cursor `group` (RT_GROUP_CURSOR) of a PE file (32 or 64 bit). Only black-and-white images,
/// as the original has.
[[nodiscard]] core::Result<CursorImage> read_cursor(std::span<const std::uint8_t> pe, std::uint16_t group);

}  // namespace o1602::assets
