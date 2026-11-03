// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kinds of island world of a content package's own: `landscapes.toml` names presets of the
// landscape of islands of their own shape (WorldSettings::Landscape, world generator stage 2),
// which the start dialog offers after Open1602's own (landscape_presets();
// docs/modding/landscapes.md). A preset only fills the settings of a new world: the world itself
// comes from them and its number, so a game needs no package for it.
#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/sim/world_plan.hpp"

namespace o1602::sim {

/// The file of a package with its kinds of island world.
inline constexpr std::string_view kLandscapesFile = "landscapes.toml";
/// Presets one package may name.
inline constexpr std::size_t kMaxPackageLandscapes = 32;

struct PackageLandscape {
    std::string id;  ///< "<package>:<name>"
    /// The name the start dialog shows, Windows-1252, by language code ("" for every language).
    std::map<std::string, std::string, std::less<>> titles;
    WorldSettings::Landscape landscape;

    /// The title in `language`, else the one for every language, else English, else the id.
    [[nodiscard]] std::string title(std::string_view language) const;
    friend bool operator==(const PackageLandscape&, const PackageLandscape&) = default;
};

/// Reads `landscapes.toml` of `package` (UTF-8) and adds its presets to `presets`. A table starts
/// from the landscape `like` names (a name of landscape_presets() or an earlier package's preset,
/// the defaults without it) and changes the fields set_landscape() knows (landscape_names::kKeys);
/// `title` and `title.<language>` name it. A field that is wrong is passed over with a warning that
/// names the line; a file that is no data file adds nothing.
void apply_landscapes_file(std::vector<PackageLandscape>& presets, std::string_view package, std::string_view utf8,
                           std::vector<std::string>& warnings);

/// The preset called `id`, nullptr if there is none.
[[nodiscard]] const PackageLandscape* find_package_landscape(std::span<const PackageLandscape> presets,
                                                             std::string_view id) noexcept;

}  // namespace o1602::sim
