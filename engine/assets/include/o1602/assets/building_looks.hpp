// SPDX-License-Identifier: GPL-3.0-or-later
//
// Buildings of the original drawn with a content package's pictures: `building_images.toml` names a
// building of haeuser.cod and an image of graphics.toml (package_graphics.hpp) that holds its
// sprites in the original's order (docs/modding/graphics.md). The islands keep the original's
// sprite numbers: the terrain sheet draws them with the image's sprites (render::RedirectedSprites).
// Presentation only: a game keeps nothing of it.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/package_graphics.hpp"

namespace o1602::assets {

/// The file of a package that gives buildings its images.
inline constexpr std::string_view kBuildingImagesFile = "building_images.toml";

/// Sprites `from` .. `from + count - 1` of the terrain sheet drawn as `to` .. `to + count - 1`.
struct SpriteRedirect {
    std::uint32_t from = 0;
    std::uint32_t count = 0;
    std::uint32_t to = 0;

    friend bool operator==(const SpriteRedirect&, const SpriteRedirect&) = default;
};

/// How many sprites from its Gfx on `type` draws: its tiles, turned four ways, in every frame of
/// its animation (render::building_sprite).
[[nodiscard]] std::uint64_t building_sprite_count(const BuildingType& type) noexcept;

/// Reads `building_images.toml` of `package`: each table ["anno1602:<name or number>"] gives the
/// building the image `image` ("<package>:<name>" of `images`). A later image of the same building
/// replaces an earlier one. A table that cannot be used is passed over with a warning; a file that
/// is no data file adds nothing.
void apply_building_images_file(std::vector<SpriteRedirect>& redirects, std::string_view package, std::string_view utf8,
                                std::span<const PackageImage> images, const BuildingTable& buildings,
                                std::vector<std::string>& warnings);

}  // namespace o1602::assets
