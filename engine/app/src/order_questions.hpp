// SPDX-License-Identifier: GPL-3.0-or-later
//
// The questions before orders that cannot be taken back (scuttling a ship, disbanding a unit,
// breaking a peace treaty, an attack that means war, demolishing land other buildings stand on). Without SDL, so that
// the tests have them.
#pragma once

#include <optional>
#include <string>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/target.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

/// The player an order's target belongs to (kNoPlayer: nobody's, or the target is gone).
[[nodiscard]] sim::PlayerId target_owner(const sim::World& world, const sim::Target& target);

/// Whether a ship's right click on the Kontor of `owner` is an attack rather than a visit: with F
/// first always, otherwise when `local` may not trade there. A trade partner's Kontor (the pirates'
/// too, whose trade stays open at war) is sailed to for trading. A visit to one's own Kontor is
/// never an attack. The original attacks any Kontor in its military mode (Haus_IsDestroyable 0x00464A80).
[[nodiscard]] bool ship_attacks_kontor(const sim::World& world, sim::PlayerId local, sim::PlayerId owner,
                                       bool attack_pick);

/// The question `local` is asked before `command` is given, in the current language
/// (Windows-1252, as the fonts of the game have it), or none.
[[nodiscard]] std::optional<std::string> order_question(const sim::World& world, const assets::BuildingTable& buildings,
                                                        const assets::FigureTable& figures, sim::PlayerId local,
                                                        const sim::Command& command);

}  // namespace o1602::app
