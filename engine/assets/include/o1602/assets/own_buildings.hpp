// SPDX-License-Identifier: GPL-3.0-or-later
//
// Buildings of a package's own, Open1602's own: `own_buildings.toml` of a package declares
// buildings that the original's `haeuser.cod` does not have (docs/modding/own-buildings.md). Each
// one copies a building of the original (`like`) and gets an Id of its own in the building table.
// Its string id `<package>:<name>` is what packages, the build menu and savegames name it by, so
// that the numbers may differ between installations.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/goods.hpp"
#include "o1602/assets/package_graphics.hpp"

namespace o1602::assets {

/// The file of a package that declares its buildings.
inline constexpr std::string_view kOwnBuildingsFile = "own_buildings.toml";
/// Buildings one package may declare, so that a broken file cannot fill the table.
inline constexpr std::size_t kMaxOwnBuildings = 256;

/// A building a package added, with the Id the building table gave it in this run.
struct OwnBuilding {
    std::string id;            ///< "<package>:<name>", UTF-8
    std::uint32_t number = 0;  ///< its `HAUS` Id in this run's building table
};

/// The entry of `own` called `id`, nullptr if there is none.
[[nodiscard]] const OwnBuilding* find_own_building(std::span<const OwnBuilding> own, std::string_view id) noexcept;

/// Reads `own_buildings.toml` of `package` and adds its buildings to `table` and `own`. Packages
/// are read in their load order, so the Ids are the same on every computer with the same packages.
/// A table that cannot be read is passed over with a warning; a file that is no data file at all
/// adds nothing. `goods`: what `ware`, `raw` and `raw2` may name besides the original's goods;
/// `classes`: what `class` may name (resident_classes.hpp), by default the classes of `table`;
/// `levels`: what `level` may name (unlock_levels.hpp), by default the levels of `table`; `images`:
/// what `gfx` may name besides the original's sprite numbers (package_graphics.hpp).
void apply_own_buildings_file(BuildingTable& table, std::vector<OwnBuilding>& own, std::string_view package,
                              std::string_view utf8, std::vector<std::string>& warnings, const Goods& goods = {},
                              std::span<const ResidentClass> classes = {}, std::span<const InfraLevel> levels = {},
                              std::span<const PackageImage> images = {});

}  // namespace o1602::assets
