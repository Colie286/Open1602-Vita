// SPDX-License-Identifier: GPL-3.0-or-later
//
// Classes of inhabitants as data, Open1602's own: `classes.toml` of a content package changes the
// original's five classes (haeuser.cod's BGRUPPE) and adds classes after them
// (docs/modding/classes.md, MD-2). A class's needs may name goods a package added (goods.hpp), and
// a house a package added may belong to a new class (own_buildings.hpp). A game keeps its classes
// (section CLAS of the savegame), so it plays with them whatever the packages installed later say.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/goods.hpp"

namespace o1602::assets {

/// The file of a package that changes and adds classes.
inline constexpr std::string_view kClassesFile = "classes.toml";
/// Classes a game may have: the goods statistics keep the inhabitants of eight
/// (sim::kCityClasses).
inline constexpr std::size_t kMaxResidentClasses = 8;

/// Changes `classes` as `classes.toml` of `package` says, in the order of the file: a table
/// ["anno1602:<name or number>"] or ["<package>:<name>"] changes a class there is, a table [<name>]
/// adds the class `<package>:<name>` after the last. A table that cannot be read is passed over
/// with a warning; a file that is no data file changes nothing. `goods`: what `need.<good>` may
/// name besides the original's goods. Returns whether anything changed.
bool apply_classes_file(std::vector<ResidentClass>& classes, std::string_view package, std::string_view utf8,
                        std::vector<std::string>& warnings, const Goods& goods = {});

/// The number of the class a string id stands for: "anno1602:<name or number>" of the original's
/// (original_names.hpp) or "<package>:<name>" of one a package added; nothing for any other.
[[nodiscard]] std::optional<std::uint8_t> class_of(std::span<const ResidentClass> classes, std::string_view id);

/// The name of `c` in `language`: the package's; empty for a class of the original (Text.cod
/// names those).
[[nodiscard]] std::string class_name(const ResidentClass& c, std::string_view language);

}  // namespace o1602::assets
