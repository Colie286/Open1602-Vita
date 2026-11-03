// SPDX-License-Identifier: GPL-3.0-or-later
//
// Island templates of a content package's own: `islands.toml` names island files (.scp) of the
// package, which new own worlds draw from with the original's templates of their climate and size
// class (docs/modding/islands.md). A world keeps such an island's tiles, so a game needs no package
// for it once it is made.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/game_content.hpp"
#include "o1602/assets/localized_names.hpp"

namespace o1602::sim {

/// The file of a package with its island templates.
inline constexpr std::string_view kIslandsFile = "islands.toml";
/// Island templates all packages may name together.
inline constexpr std::size_t kMaxPackageIslands = 1024;

struct PackageIsland {
    std::string id;       ///< "<package>:<name>"
    std::string package;  ///< the package's id
    std::filesystem::path file;
    std::uint8_t climate = 0;  ///< 0 north, 1 south
    /// A package's climate the island takes when a world draws it ("<package>:<name>",
    /// climates.toml), empty for none; it is drawn among the templates of `climate`.
    std::string own_climate;
    std::uint8_t size = 0;    ///< size class 0 lit .. 4 lar, the smallest the island fits
    std::uint16_t width = 0;  ///< in tiles
    std::uint16_t height = 0;
    assets::LocalizedNames names;  ///< Windows-1252

    /// The name in `language`, else in English, else the first, else the id.
    [[nodiscard]] std::string name(std::string_view language) const;
};

/// Reads `islands.toml` of `package` (UTF-8) and adds its islands to `islands`. A table names one
/// island: `file`, an island file of the original's format in the package (required), `climate`
/// ("north", "south" or a package's climate "<package>:<name>"; the file's if not given; a
/// package's climate leaves the island among the templates of the file's) and `name.<language>`. The island must fit a
/// size class of the original's templates (templates::kClassWidth, kClassHeight). A table that is wrong or whose file
/// cannot be read is passed over with a warning that names the line.
void apply_islands_file(std::vector<PackageIsland>& islands, const assets::ModPackage& package, std::string_view utf8,
                        std::vector<std::string>& warnings);

/// The island templates of `packages`, in their load order.
[[nodiscard]] std::vector<PackageIsland> load_package_islands(std::span<const assets::ModPackage> packages,
                                                              std::vector<std::string>& warnings);

/// The size class whose templates' box `width` x `height` fits, the smallest; none if it fits none.
[[nodiscard]] std::optional<std::uint8_t> size_class_of(std::uint32_t width, std::uint32_t height) noexcept;

/// The template number an own world gives the `index`-th island of `islands` among those of its
/// climate and size class: after the original's (templates::kTemplateCount).
[[nodiscard]] std::uint16_t package_template_number(std::span<const PackageIsland> islands, std::size_t index) noexcept;

/// The island of `islands` that template `number` of `climate` and `size` names, nullptr for one
/// of the original's.
[[nodiscard]] const PackageIsland* package_template(std::span<const PackageIsland> islands, std::uint8_t climate,
                                                    std::uint8_t size, std::uint16_t number) noexcept;

}  // namespace o1602::sim
