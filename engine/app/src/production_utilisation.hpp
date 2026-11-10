// SPDX-License-Identifier: GPL-3.0-or-later
//
// How busy a production building is, as the original's production window shows it: for the
// building window and the tab "Betriebe" of the window "Warenbilanz". Only for the interface; the
// game never reads it.
#pragma once

#include <cstdint>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

/// Productivity in percent (Prod_Utilisation 0x0047D290): goods made in the statistics window
/// against what the building makes at full speed, for farms, hunters and fishers with the walk of
/// their worker; 100 for kinds that have no such figure.
[[nodiscard]] std::int64_t production_utilisation(const sim::Production& p, const assets::BuildingType& b,
                                                  const assets::FigureTable& figures);

}  // namespace o1602::app
