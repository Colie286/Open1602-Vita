// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unlock levels as data, Open1602's own: `levels.toml` of a content package changes when the
// original's levels (haeuser.cod's BAUINFRA) are reached and adds levels after the original's 32
// (docs/modding/levels.md, MD-8). A level a package added may ask for inhabitants of a class a
// package added (resident_classes.hpp), and a building a package added may need it
// (own_buildings.hpp). A game keeps its levels (section LEVL of the savegame).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"

namespace o1602::assets {

/// The file of a package that changes and adds unlock levels.
inline constexpr std::string_view kLevelsFile = "levels.toml";
/// The highest level a game may have: buildings keep their level in a byte (BuildingType::infra).
inline constexpr std::size_t kMaxUnlockLevel = 255;

/// Changes `levels` (by INFRA number, 0 = none) as `levels.toml` of `package` says, in the order
/// of the file: a table ["anno1602:<name or number>"] or ["<package>:<name>"] changes a level there
/// is, a table [<name>] adds the level `<package>:<name>` with the next number. A table that cannot
/// be read is passed over with a warning; a file that is no data file changes nothing. `classes`:
/// what `class` may name. Returns whether anything changed.
bool apply_levels_file(std::vector<InfraLevel>& levels, std::string_view package, std::string_view utf8,
                       std::vector<std::string>& warnings, std::span<const ResidentClass> classes);

/// The INFRA number a string id stands for: "anno1602:<name or number>" of the original's
/// (original_names.hpp) or "<package>:<name>" of one a package added; nothing for any other.
[[nodiscard]] std::optional<std::uint8_t> level_of(std::span<const InfraLevel> levels, std::string_view id);

}  // namespace o1602::assets
