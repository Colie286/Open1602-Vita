// SPDX-License-Identifier: GPL-3.0-or-later
//
// Readable names of the original's buildings, goods, classes, unlock levels and figures, Open1602's own: a content
// package may write ["anno1602:bakery"] instead of ["anno1602:20503"] (docs/modding/values.md). The buildings are those
// of the build menu, named after Text.cod; the figures are named after figuren.cod's symbols.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace o1602::assets {

/// A name for `count` numbers from `first`: one building's Id, or a figure in each of the four
/// player colours (SOLDAT1 to SOLDAT4 are "swordsman").
struct OriginalName {
    std::string_view name;
    std::uint32_t first = 0;
    std::uint32_t count = 1;
};

/// The names of haeuser.cod's buildings, by HAUS Id.
[[nodiscard]] std::span<const OriginalName> original_building_names() noexcept;
/// The names of the original's trade goods, by ware number (EISENERZ to ZIEGEL).
[[nodiscard]] std::span<const OriginalName> original_ware_names() noexcept;
/// The names of haeuser.cod's unlock levels, by INFRA number.
[[nodiscard]] std::span<const OriginalName> original_level_names() noexcept;
/// The names of haeuser.cod's classes of inhabitants, by BGRUPPE number.
[[nodiscard]] std::span<const OriginalName> original_class_names() noexcept;
/// The names of figuren.cod's figures, by FIGUR number.
[[nodiscard]] std::span<const OriginalName> original_figure_names() noexcept;
/// The Id a string id "anno1602:<name or number>" of a building stands for; nothing for another
/// package's id or a name the original has not.
[[nodiscard]] std::optional<std::uint32_t> original_building_id(std::string_view id) noexcept;

/// The entry of `names` called `name`, nullptr if none is.
[[nodiscard]] const OriginalName* find_original_name(std::span<const OriginalName> names,
                                                     std::string_view name) noexcept;

}  // namespace o1602::assets
