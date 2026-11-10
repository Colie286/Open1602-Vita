// SPDX-License-Identifier: GPL-3.0-or-later
//
// The parchment of Open1602's own windows in the original's style (the window "Warenbilanz"): a
// sandstone-coloured ground with soft clouds, made once from a fixed seed, so that it looks the
// same on every computer and in every frame.
#pragma once

#include <cstdint>
#include <vector>

namespace o1602::app {

/// RGBA pixels, `width` x `height`, row by row.
[[nodiscard]] std::vector<std::uint8_t> parchment_pixels(std::int32_t width, std::int32_t height,
                                                         std::uint32_t seed = 3);

}  // namespace o1602::app
