// SPDX-License-Identifier: GPL-3.0-or-later
//
// The names the game shows for players, cities and ships. The simulation takes names only from
// the game's texts (Text.cod PLAYCOL, STAEDTE, SHIPS) and leaves a name empty where they name
// none; the windows show such a name in the game's language (MD-10).
#pragma once

#include <string>
#include <vector>

#include "o1602/sim/player.hpp"
#include "o1602/sim/ship.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

/// Players five on of a new world (sim::add_players, which leaves their names empty) get the
/// names of their colours in the game's language; players named otherwise keep their names.
void name_more_players(sim::World& world);

/// The name of a computer player by its banner's colour, in the game's language: the original's
/// four from `colour_names` (Text.cod PLAYCOL) or Open1602's texts, the further ones from
/// Open1602's texts.
[[nodiscard]] std::string colour_player_name(sim::BannerId banner, const std::vector<std::string>& colour_names);

/// The name of a player as the game shows it (Windows-1252): its own, or the one of its colour.
[[nodiscard]] std::string player_name(const sim::Player& player);
/// The name of a city as the game shows it (Windows-1252): its own, or one by its island.
[[nodiscard]] std::string city_name(const sim::City& city);
/// The name of a ship as the game shows it (Windows-1252): its own, or one by its number.
[[nodiscard]] std::string ship_name(const sim::Ship& ship);

}  // namespace o1602::app
