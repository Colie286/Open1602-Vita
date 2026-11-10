// SPDX-License-Identifier: GPL-3.0-or-later
//
// Corrections of the original's data (ADR 0006, the D entries of docs/re/bugs.md). The .cod
// interpreter stays faithful to the original; these are applied to the tables after loading and
// can be switched off (settings.cfg `data_fixes = off`, `--original-data`). Each one only replaces
// the value the original files have, so data a mod has changed stays as it is. They can differ
// between the computers of a game: the picture and the sound are local, and the formations decide
// the places Open1602's interface sends with an order (ui::UnitSelection); the simulation reads them
// only for a command that names a formation, which the interface never sends.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace o1602::assets {

class BuildingTable;
class FigureTable;

namespace fixes {

/// D-001: the bakery's sound when it is built, SAMPLES/Backer.wav, which the original never loads.
/// The first number after the original's 92 sounds (audio::kWavBakery).
inline constexpr std::uint16_t kBakerySound = 92;

/// D-001 (bakery without a building sound), D-002 (pirate Kontor without a flag). Returns the
/// catalogue ids of the fixes it made.
std::vector<std::string_view> fix_buildings(BuildingTable& buildings);
/// D-014 (the formation "Block" names one place twice).
std::vector<std::string_view> fix_figures(FigureTable& figures);

}  // namespace fixes
}  // namespace o1602::assets
