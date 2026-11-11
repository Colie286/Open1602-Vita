// SPDX-License-Identifier: GPL-3.0-or-later
//
// The icons of the content packages (icons.toml, docs/modding/graphics.md) for the whole
// interface: set once the packages are loaded, then the build menu, the goods and the messages
// find their sprite numbers here, and the interface's renderer their pictures. Presentation only;
// a game keeps the icons' string ids.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "o1602/assets/goods.hpp"
#include "o1602/assets/package_arms.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/assets/package_interface.hpp"

namespace o1602::ui {

/// Open1602's own icons ("open1602:<name>", data/icons/ui/<name>.png, compiled in), numbered from
/// kFirstPackageIcon; the packages' icons follow them.
[[nodiscard]] std::vector<assets::PackageIcon> builtin_icons();

/// Makes `icons` the packages' icons. Call it on the main thread, before the interface is drawn
/// with them; until then there are none.
void set_package_icons(std::vector<assets::PackageIcon> icons);
/// The icons given to set_package_icons().
[[nodiscard]] std::span<const assets::PackageIcon> package_icons() noexcept;
/// The sprite number of the icon `id` ("<package>:<name>"), nothing if no package has it.
[[nodiscard]] std::optional<std::int32_t> package_icon_sprite(std::string_view id) noexcept;

/// Makes `arms` the packages' coats of arms (arms.toml), with set_package_icons().
void set_package_arms(std::vector<assets::PackageArms> arms);
/// The sprite of the icon a package draws as the coat of arms of banner `banner` (its id), nothing
/// if none does or its icon is gone.
[[nodiscard]] std::optional<std::int32_t> package_arms_sprite(std::string_view banner) noexcept;

/// Makes `sprites` the original's interface sprites the packages draw anew (interface.toml), with
/// set_package_icons().
void set_interface_sprites(std::vector<assets::InterfaceSprite> sprites);
/// The icon a package draws in place of sprite `gfx` of the interface's sheet `block` (Blocknr,
/// ui::sheet_file()), nullptr if none does or its icon is gone.
[[nodiscard]] const assets::PackageIcon* interface_icon(std::int32_t block, std::int32_t gfx) noexcept;

/// The sprite of the icon of good `w`: the package's icon it shows, else the original's at
/// `gfx_ware` (GFXWARE of the window's layout).
[[nodiscard]] std::int32_t ware_sprite(std::int32_t gfx_ware, const assets::Goods& goods, std::uint32_t w) noexcept;

}  // namespace o1602::ui
