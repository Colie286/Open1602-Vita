// SPDX-License-Identifier: GPL-3.0-or-later
//
// Checks of packages that need more than one file to see (o1602-tool check-mod,
// docs/modding/README.md): what a package names that the original's data or the game's banners do
// not have, and the problems as JSON for a script or a CI job.
#pragma once

#include <span>
#include <string>
#include <vector>

#include "o1602/assets/game_content.hpp"
#include "o1602/assets/package_arms.hpp"
#include "o1602/assets/package_flags.hpp"
#include "o1602/audio/package_music.hpp"
#include "o1602/render/banner.hpp"

namespace o1602::app {

/// A problem for each track that replaces a title of MUSIC8 (`tracks` and `fights`, as
/// audio::Music::scan finds them) that is not there, case ignored. The game passes it over.
void check_music_replacements(std::span<const audio::MusicTrack> music, std::span<const std::string> tracks,
                              std::span<const std::string> fights, std::vector<std::string>& problems);

/// A problem for each coat of arms of a banner that `banners` has not. The game passes it over.
void check_arms_banners(std::span<const assets::PackageArms> arms, const render::BannerTable& banners,
                        std::vector<std::string>& problems);

/// A problem for each flag of a banner that `banners` has not. The game passes it over.
void check_flag_banners(std::span<const assets::PackageFlag> flags, const render::BannerTable& banners,
                        std::vector<std::string>& problems);

/// The packages and problems as one JSON object:
/// {"packages":[{"id":..,"version":..}],"problems":[{"text":..,"package":..,"file":..,"line":N}]}.
/// `package`, `file` and `line` are there when the problem starts "<package>/<file> line <N>: "
/// (`file` and `package` only, without " line <N>"), as the readers write them.
[[nodiscard]] std::string problems_json(std::span<const assets::ModPackage> packages,
                                        std::span<const std::string> problems);

}  // namespace o1602::app
