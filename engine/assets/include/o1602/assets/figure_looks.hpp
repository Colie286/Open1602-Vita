// SPDX-License-Identifier: GPL-3.0-or-later
//
// Figures and ships of the original drawn with a content package's pictures: `figure_images.toml`
// names a figure of figuren.cod and an image of graphics.toml (package_graphics.hpp) that holds its
// sprites in the original's order (docs/modding/graphics.md). Presentation only: the simulation
// keeps the figure's values, a game keeps nothing of it.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/package_graphics.hpp"

namespace o1602::assets {

/// The file of a package that gives figures its images.
inline constexpr std::string_view kFigureImagesFile = "figure_images.toml";

/// A figure drawn with an image of a package.
struct FigureLook {
    std::uint32_t figure = 0;  ///< FIGUR number
    std::uint32_t gfx = 0;     ///< sprite number of the image's first sprite (PackageImage::first)
    bool shadow = true;        ///< the figure keeps its shadow (SCHATTEN.BSH)

    friend bool operator==(const FigureLook&, const FigureLook&) = default;
};

/// Whether `type` draws a package's image (apply_figure_looks): from the terrain sheet, whatever
/// sheet the original picks for its kind of figure.
[[nodiscard]] constexpr bool draws_package_image(const FigureType& type) noexcept {
    return type.sheet == FigureSheet::Stadtfld && type.gfx >= kFirstPackageSprite;
}

/// How many sprites from its Gfx on `type` draws: every direction of every animation, or of its
/// picture without one.
[[nodiscard]] std::uint32_t figure_sprite_count(const FigureType& type) noexcept;

/// Reads `figure_images.toml` of `package`: each table ["anno1602:<name or number>"] gives the
/// figure (a unit's name: all four colours) the image `image` ("<package>:<name>" of `images`).
/// A later look of the same figure replaces an earlier one. A table that cannot be used is passed
/// over with a warning; a file that is no data file adds nothing.
void apply_figure_images_file(std::vector<FigureLook>& looks, std::string_view package, std::string_view utf8,
                              std::span<const PackageImage> images, const FigureTable& figures,
                              std::vector<std::string>& warnings);

/// Gives the figures of `table` the pictures of `original` (sheet, Gfx, shadow), then `looks`:
/// those figures draw from the terrain sheet, where the packages' images are.
void apply_figure_looks(FigureTable& table, const FigureTable& original, std::span<const FigureLook> looks);

}  // namespace o1602::assets
