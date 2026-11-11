// SPDX-License-Identifier: GPL-3.0-or-later
//
// Classes of inhabitants as data, Open1602's own: `classes.toml` of a content package changes the
// original's five classes (haeuser.cod's BGRUPPE) and adds classes after them
// (docs/modding/classes.md, MD-2). A class's needs may name goods a package added (goods.hpp), and
// a house a package added may belong to a new class (own_buildings.hpp). A game keeps its classes
// (section CLAS of the savegame), so it plays with them whatever the packages installed later say.
#pragma once

#include <array>
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

/// The public buildings a class may need (ResidentClass::public_needs): the bits of the house's
/// buildings in reach as the simulation keeps them (sim::House::services), and the market.
namespace public_building {
inline constexpr ServiceBits kChapel = 1U << 2;
inline constexpr ServiceBits kChurch = 1U << 3;  ///< also the cathedral
inline constexpr ServiceBits kTavern = 1U << 4;
inline constexpr ServiceBits kSchool = 1U << 5;
inline constexpr ServiceBits kBathhouse = 1U << 6;
inline constexpr ServiceBits kTheatre = 1U << 7;
inline constexpr ServiceBits kCollege = 1U << 8;
inline constexpr ServiceBits kDoctor = 1U << 9;
inline constexpr ServiceBits kKontor = 1U << 10;
inline constexpr ServiceBits kGallows = 1U << 11;
inline constexpr ServiceBits kMarket = 1U << 15;  ///< no bit of House::services: the market
/// The services of public buildings of a kind of their own (own_buildings.toml `service`), in the
/// order the packages declare them (own_services): bits 13 and 14, then 16 to 63.
inline constexpr std::size_t kOwnCount = 50;
[[nodiscard]] constexpr ServiceBits own(std::size_t index) noexcept {
    return index < 2 ? ServiceBits{1} << (13 + index) : ServiceBits{1} << (14 + index);
}
/// All the bits of the public buildings of a kind of their own.
inline constexpr ServiceBits kOwnMask = ~ServiceBits{0xFFFF} | own(0) | own(1);
inline constexpr ServiceBits kAll = kChapel | kChurch | kTavern | kSchool | kBathhouse | kTheatre | kCollege | kDoctor |
                                    kKontor | kGallows | kMarket | kOwnMask;
}  // namespace public_building

/// Public buildings of a kind of their own a game may have.
inline constexpr std::size_t kMaxOwnServices = public_building::kOwnCount;
static_assert(public_building::own(kMaxOwnServices - 1) == ServiceBits{1} << 63);

/// Choices of public buildings a class may need at most.
inline constexpr std::size_t kMaxPublicNeeds = 8;

/// The public buildings `text` names (the field `public` of classes.toml): choices separated by
/// commas, the buildings of a choice by '|', as "market, chapel | church, tavern"; "" needs none.
/// Names: market, kontor, chapel, church, tavern, school, college, bathhouse, theatre, doctor,
/// gallows; and `<package>:<name>`, a building of `services` (own_services: its own kind). Nothing,
/// and `error` says why, for an unknown name, an empty choice or more than kMaxPublicNeeds choices.
[[nodiscard]] std::optional<std::vector<ServiceBits>> public_needs_of(std::string_view text, std::string& error,
                                                                      std::span<const std::string> services = {});

/// Changes `classes` as `classes.toml` of `package` says, in the order of the file: a table
/// ["anno1602:<name or number>"] or ["<package>:<name>"] changes a class there is, a table [<name>]
/// adds the class `<package>:<name>` after the last. A table that cannot be read is passed over
/// with a warning; a file that is no data file changes nothing. `goods`: what `need.<good>` may
/// name besides the original's goods; `services`: the buildings of a kind of their own `public` may
/// name (own_services). Returns whether anything changed.
bool apply_classes_file(std::vector<ResidentClass>& classes, std::string_view package, std::string_view utf8,
                        std::vector<std::string>& warnings, const Goods& goods = {},
                        std::span<const std::string> services = {});

/// The number of the class a string id stands for: "anno1602:<name or number>" of the original's
/// (original_names.hpp) or "<package>:<name>" of one a package added; nothing for any other.
[[nodiscard]] std::optional<std::uint8_t> class_of(std::span<const ResidentClass> classes, std::string_view id);

/// The name of `c` in `language`: the package's; empty for a class of the original (Text.cod
/// names those).
[[nodiscard]] std::string class_name(const ResidentClass& c, std::string_view language);

}  // namespace o1602::assets
