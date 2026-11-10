// SPDX-License-Identifier: GPL-3.0-or-later
//
// The build menu (docs/re/ui.md#baumenü): eight category buttons in BAU.GAD, and per category a
// pop-up of building icons in rows that the code builds itself (BuildMenu_OnCategoryPress
// 0x00432A70). Which buildings belong to which category is a table in the original's code, not
// data; here it is data (BuildMenuDefinition), with the original's lists as the default, and the
// packages' build_menu.toml add, move and remove buildings (ADR 0007, docs/modding/build-menu.md).
// The eight categories are the buttons of BAU.GAD.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/gad.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/core/result.hpp"

namespace o1602::ui {

/// One building of a category's pop-up.
struct BuildButton {
    std::uint32_t id = 0;    ///< HAUS Id
    std::int32_t icon = 0;   ///< TOOLS sprite of its icon, as an offset from GFXBAUHAUS
    std::int32_t text = -1;  ///< its line of the category's Text.cod section; -1: `name`
    std::string name;        ///< Windows-1252: the name a package gave it, when `text` is -1
    std::string picture;     ///< a package's icon it shows instead of `icon` ("<package>:<name>"), or empty

    /// The sprite of its icon: its package's icon if a package has it, else GFXBAUHAUS + `icon`.
    [[nodiscard]] std::int32_t sprite(std::int32_t gfx_bauhaus) const;
};

/// One category: its button in BAU.GAD, the Text.cod section that names the original's buildings
/// and its buildings in the pop-up's rows.
struct BuildCategory {
    std::int32_t button = 0;
    bool demolish = false;  ///< no list: the demolish tool
    std::string names;
    std::vector<std::vector<BuildButton>> rows;

    /// Number of buildings, and the building at a position of the whole list.
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> at(std::size_t index) const noexcept;
    [[nodiscard]] const BuildButton* button_at(std::size_t index) const noexcept;
    /// Position of a building in the list.
    [[nodiscard]] std::optional<std::size_t> index_of(std::uint32_t id) const noexcept;
    /// The name of the building at `index`: its line of `section` (the Text.cod section `names`),
    /// or the name its package gave it; empty if neither.
    [[nodiscard]] std::string name(std::size_t index, const std::vector<std::string>& section) const;
};

struct BuildMenuDefinition {
    std::vector<BuildCategory> categories;
    std::int32_t default_category = 31006;  ///< selected at the start of a game (Farmen, 0x0042BAD0)
    std::int32_t locked_icon_offset = 43;   ///< the grey "?" of a building not yet available

    [[nodiscard]] const BuildCategory* category(std::int32_t button) const noexcept;
    /// The category a building is listed in, nothing if none.
    [[nodiscard]] const BuildCategory* category_of(std::uint32_t id) const noexcept;
};

/// The original's categories (lists 0x00498440.., row sizes 0x004985A0..).
[[nodiscard]] const BuildMenuDefinition& original_build_menu();

/// The build menu of the game: original_build_menu() with the packages' changes once
/// set_build_menu() gave them. Both on the main thread only.
[[nodiscard]] const BuildMenuDefinition& build_menu();
void set_build_menu(BuildMenuDefinition menu);

/// What a pop-up holds: six rows of icons over the category's button (the original's most are
/// five, the farms) and four icons a row in its width; build_popup() leaves out what is beyond.
inline constexpr std::size_t kMaxPopupRows = 6;
inline constexpr std::size_t kMaxRowIcons = 4;

/// Applies a package's build_menu.toml to `menu`: each table puts one building (`id`, a record of
/// `buildings`, or `building`, a string id of the original's or of one a package added, `own`)
/// into a category's row, moving it if it is listed already, or takes it out of the
/// menu (`remove = true`). What is wrong in a table is passed over with a warning that names the
/// file and the line; a file that cannot be read is an error and changes nothing.
core::Status apply_build_menu_package(BuildMenuDefinition& menu, std::string_view package, std::string_view utf8,
                                      const assets::BuildingTable& buildings, std::vector<std::string>& warnings,
                                      std::span<const assets::OwnBuilding> own = {});
/// The build_menu.toml of every package that has one (<package folder>/build_menu.toml), in the
/// order given (their load order), applied to a copy of `menu`.
[[nodiscard]] BuildMenuDefinition load_build_menu_mods(const BuildMenuDefinition& menu,
                                                       std::span<const assets::ModPackage> packages,
                                                       const assets::BuildingTable& buildings,
                                                       std::vector<std::string>& warnings,
                                                       std::span<const assets::OwnBuilding> own = {});

// Gadget Ids of the pop-up (the original's numbers).
inline constexpr std::int32_t kPopupSlot = 32001;           ///< + position in the list: the frame to click
inline constexpr std::int32_t kPopupIcon = 32101;           ///< + position: the icon
inline constexpr std::int32_t kPopupBackdrop = 1000000000;  ///< the rest of the panel: closes the pop-up
/// Distance of two rows and of two icons in a row, in design units.
inline constexpr std::int32_t kPopupRowStep = 59;
inline constexpr std::int32_t kPopupIconStep = 57;

/// The pop-up of a category as a layout of the side panel. Only rows with an available building
/// are shown (if there is none, the last row, all "?"), the first row next to the category button
/// and the others above it. `available[i]`: whether the building at position i may be picked.
/// `gfx_bauhaus` is TOOLS.INC's GFXBAUHAUS (the frame pieces are its first five sprites).
[[nodiscard]] assets::GadgetLayout build_popup(const BuildCategory& category, const std::vector<bool>& available,
                                               const assets::Gadget& button, std::int32_t gfx_bauhaus,
                                               std::int32_t locked_icon_offset);

}  // namespace o1602::ui
