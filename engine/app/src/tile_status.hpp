// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the status line says about the map under the mouse (0x0040A3D0): the kind of a ship or a
// land unit, the name of the building, the field or the fishing ground, and over grass and trees
// of an explored island the crops it is suited for. Without SDL, so that the tests have it.
#pragma once

#include <cstdint>
#include <string>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/unit_selection.hpp"

namespace o1602::app {

/// The name of a building as the build menu calls it (Haus_GetMenuName 0x0042D230): its place in
/// one of the build menu's lists or in [LANDSCHAFT] (natives, pirates, the forests). Empty for
/// anything in none of them. Windows-1252, as Text.cod has it.
[[nodiscard]] std::string menu_name(std::uint32_t id, const assets::TextTable& texts);

/// The status line over tile (x, y) of `island` for player `local`, empty for nothing to say.
/// On an island the player has not discovered it names the ground layer, not what stands on it.
[[nodiscard]] std::string tile_status(const sim::WorldIsland& island, std::int32_t x, std::int32_t y,
                                      sim::PlayerId local, const assets::BuildingTable& buildings,
                                      const assets::TextTable& texts);

/// The status line over a ship or a land unit: "Schiff", "Piratenschiff" or "Freier Händler" by
/// its owner, a unit by its kind ([FIGKIND] "Infanterist" ...), the natives' warriors
/// "Eingeborenenkrieger". Empty for a unit that is gone.
[[nodiscard]] std::string unit_status(const sim::World& world, const ui::UnitRef& unit,
                                      const assets::FigureTable& figures, const assets::TextTable& texts);

}  // namespace o1602::app
