// SPDX-License-Identifier: GPL-3.0-or-later
//
// The last check of every load (review point 13): whatever the file, everything in a world that
// names an island, a tile or a player must name one the world has, so that no tick reads outside a
// list. The loaders check their own parts as they read them; this checks the world they built.
#pragma once

#include "o1602/core/result.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// Ok if every reference of the world holds: islands on the map, cities, buildings, units and
/// records on islands the world has and inside their tiles, owners that are players or nobody.
/// Otherwise an error naming the first that does not.
[[nodiscard]] core::Status validate(const World& world);

}  // namespace o1602::sim
