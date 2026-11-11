// SPDX-License-Identifier: GPL-3.0-or-later
//
// Coats of arms of a content package: `arms.toml` gives a banner (render::Banner::id, such as
// "anno1602:red") an icon of icons.toml that the interface draws wherever it shows that banner's
// coat of arms (docs/modding/flags.md). Presentation only: a game keeps nothing of it.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/package_graphics.hpp"

namespace o1602::assets {

/// The file of a package that gives banners their coats of arms.
inline constexpr std::string_view kArmsFile = "arms.toml";

/// The coat of arms of one banner.
struct PackageArms {
    std::string banner;  ///< the banner's id, "<origin>:<name>"
    std::string icon;    ///< the icon drawn, "<package>:<name>" of icons.toml

    friend bool operator==(const PackageArms&, const PackageArms&) = default;
};

/// Reads `arms.toml` of `package`: each table ["<banner id>"] gives that banner the icon `icon`
/// (one of `icons`). A later package's arms of the same banner replace an earlier one's. A table
/// that cannot be used is passed over with a warning; a file that is no data file adds nothing.
void apply_arms_file(std::vector<PackageArms>& arms, std::string_view package, std::string_view utf8,
                     std::span<const PackageIcon> icons, std::vector<std::string>& warnings);

/// Whether `id` is written as a banner's id: "<origin>:<name>", each of lower case letters, digits
/// and '_'.
[[nodiscard]] bool is_banner_id(std::string_view id) noexcept;

/// The arms of banner `banner`, nullptr if no package gives it any.
[[nodiscard]] const PackageArms* find_package_arms(std::span<const PackageArms> arms, std::string_view banner) noexcept;

}  // namespace o1602::assets
