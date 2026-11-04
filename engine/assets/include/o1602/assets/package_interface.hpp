// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original's interface art drawn with a package's icons: `interface.toml` names a sprite of one
// of the interface's sheets (TOOLS, START, SYMBOL, BAUHAUS, BAUSHIP) and the icon of icons.toml
// drawn in its place, wherever the interface shows it: buttons, panels, the menus' backgrounds
// (docs/modding/graphics.md). Presentation only: a game keeps nothing of it.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/package_graphics.hpp"

namespace o1602::assets {

/// The file of a package that draws the original's interface art anew.
inline constexpr std::string_view kInterfaceFile = "interface.toml";

/// The names of the interface's sheets, by their block number (ui::sheet_file()).
inline constexpr std::array<std::string_view, 5> kInterfaceSheets = {"tools", "start", "symbol", "bauhaus", "bauship"};

/// One sprite of the interface drawn with an icon.
struct InterfaceSprite {
    std::int32_t sheet = 0;   ///< its block: index of kInterfaceSheets
    std::int32_t sprite = 0;  ///< its number in the sheet
    std::string icon;         ///< the icon drawn, "<package>:<name>" of icons.toml
    /// Its number in the History Edition's sheet where that has other sprites before it
    /// (`history`), nothing if the same.
    std::optional<std::int32_t> history;

    friend bool operator==(const InterfaceSprite&, const InterfaceSprite&) = default;
};

/// "<sheet>:<number>" of a table's name ("start:0"), nothing if it names no sheet.
[[nodiscard]] std::optional<InterfaceSprite> parse_interface_sprite(std::string_view name) noexcept;

/// Reads `interface.toml` of `package`: each table ["<sheet>:<number>"] draws that sprite with the
/// icon `icon` (one of `icons`); `history` gives its number in the History Edition's sheet. A later package's icon for
/// the same sprite replaces an earlier one's. A table that cannot be used is passed over with a warning; a file that is
/// no data file adds nothing.
void apply_interface_file(std::vector<InterfaceSprite>& sprites, std::string_view package, std::string_view utf8,
                          std::span<const PackageIcon> icons, std::vector<std::string>& warnings);

/// The sprites with the numbers of the edition in use: for the History Edition each `history`
/// number in place of the original's.
void use_edition(std::vector<InterfaceSprite>& sprites, bool history_edition);

/// The icon drawn in place of sprite `sprite` of sheet `sheet`, nullptr if no package gives one.
[[nodiscard]] const InterfaceSprite* find_interface_sprite(std::span<const InterfaceSprite> sprites, std::int32_t sheet,
                                                           std::int32_t sprite) noexcept;

}  // namespace o1602::assets
