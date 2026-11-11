// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's map as a picture (ADR 0013): every world tile a square of its kind's colour (sea,
// surf, beach, ground, forest, mountains, villages), the players' starts marked, to look at or to
// share outside the game.
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

struct MapImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;  ///< row by row, 4 bytes a pixel
};

/// The map of `world`, `scale` pixels a tile (1 to 8), the starts (start_areas) as white squares.
[[nodiscard]] MapImage map_image(const sim::World& world, const assets::BuildingTable& buildings,
                                 std::uint32_t scale = 2);

}  // namespace o1602::editor
