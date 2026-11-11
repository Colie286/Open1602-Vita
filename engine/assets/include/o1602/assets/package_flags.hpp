// SPDX-License-Identifier: GPL-3.0-or-later
//
// Flags of a content package: `flags.toml` gives a banner (render::Banner::id, such as
// "anno1602:red") a waving flag of its own, an image of graphics.toml with the flag's eight frames,
// drawn on its ships and buildings instead of the tinted generic flag (docs/modding/flags.md).
// Presentation only: a game keeps nothing of it.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/package_graphics.hpp"

namespace o1602::assets {

/// The file of a package that gives banners their flags.
inline constexpr std::string_view kFlagsFile = "flags.toml";
/// The frames of a waving flag, as the original's (90 ms each).
inline constexpr std::uint32_t kFlagFrames = 8;

/// The flag of one banner.
struct PackageFlag {
    std::string banner;  ///< the banner's id, "<origin>:<name>"
    PackageImage image;  ///< its frames, the first kFlagFrames sprites
    bool tint = false;   ///< a template: its pure grey pixels take on the banner's colour

    friend bool operator==(const PackageFlag&, const PackageFlag&) = default;
};

/// Reads `flags.toml` of `package`: each table ["<banner id>"] gives that banner the image `image`
/// (one of `images`, with at least kFlagFrames sprites) and `tint`. A later package's flag of the
/// same banner replaces an earlier one's. A table that cannot be used is passed over with a
/// warning; a file that is no data file adds nothing.
void apply_flags_file(std::vector<PackageFlag>& flags, std::string_view package, std::string_view utf8,
                      std::span<const PackageImage> images, std::vector<std::string>& warnings);

/// The flag of banner `banner`, nullptr if no package gives it one.
[[nodiscard]] const PackageFlag* find_package_flag(std::span<const PackageFlag> flags,
                                                   std::string_view banner) noexcept;

}  // namespace o1602::assets
