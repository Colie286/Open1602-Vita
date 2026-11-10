// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fertility view (Open1602): whether a crop bears on each tile of an island, as the original
// decides it when a field is planted (sim::tile_fertile_at: the island's fertility for the crop,
// the Randwachs of the ground and a pattern). The tiles are tinted green where the crop bears and
// red where it does not. It shows by itself while a field or plantation is in the build tool, and
// with its key (Ctrl+B) for the crop of the selected farm. Pure logic, so that the tests have it.
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

/// Tint of a tile where the crop bears, and where it does not.
inline constexpr assets::Rgba kFertileTint{40, 200, 40, 110};
inline constexpr assets::Rgba kInfertileTint{220, 40, 40, 110};

/// The crop the view shows for a building type: a field's or plantation's own crop, or the crop a
/// farm or plantation harvests; 0 for anything else, and for pastures and forests, which grow on
/// every tile.
[[nodiscard]] std::uint8_t fertility_crop(const assets::BuildingTable& buildings, const assets::BuildingType& type);

/// Appends a mark for every tile of `island` where `crop` could be planted or grows already (open
/// ground and fields) and that belongs to `player` or to nobody: green where it bears, red where
/// it does not. A drought is left out: what it withers grows again after it.
void fertility_marks(const sim::World& world, const assets::BuildingTable& buildings, sim::PlayerId player,
                     std::uint16_t island, std::uint8_t crop, std::vector<render::TileMark>& out);

}  // namespace o1602::app
