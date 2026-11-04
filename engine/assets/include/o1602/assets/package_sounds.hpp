// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sounds of a content package's own: `sounds.toml` names WAV, Ogg Vorbis and FLAC files and gives
// them sound numbers after the original's WAV_* numbers (docs/modding/sounds.md). A building a
// package added sounds one when it is built, a figure a package added at the end of its
// animation's cycle (own_buildings.hpp, own_figures.hpp). The numbers follow the packages' load
// order, so they are the same on every computer with the same packages.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace o1602::assets {

/// The file of a package that names its sounds.
inline constexpr std::string_view kSoundsFile = "sounds.toml";
/// The sound number of the packages' first sound: far after the original's 92 (audio::SampleId).
inline constexpr std::uint16_t kFirstPackageSound = 1000;
/// The sounds of all packages together.
inline constexpr std::size_t kMaxPackageSounds = 1024;

/// A sound of a package.
struct PackageSound {
    std::string id;            ///< "<package>:<name>"
    std::uint16_t number = 0;  ///< its sound number, from kFirstPackageSound
    std::filesystem::path file;
    /// The original's sound this one plays in place of: the name of its file in SAMPLES, without
    /// ".wav", in lower case ("kanone5"); empty for a sound of its own. Which names there are, the
    /// game's audio knows (audio::samples_named).
    std::string replaces;

    friend bool operator==(const PackageSound&, const PackageSound&) = default;
};

/// Reads `sounds.toml` of `package` (in `folder`) and adds its sounds to `sounds` with the next
/// free numbers. Checks that each file is there and is a WAV, Ogg or FLAC file by its first bytes;
/// the game decodes it once when the packages are loaded. A table that cannot be read is passed
/// over with a warning; a file that is no data file adds nothing.
void apply_sounds_file(std::vector<PackageSound>& sounds, std::string_view package, const std::filesystem::path& folder,
                       std::string_view utf8, std::vector<std::string>& warnings);

/// The sound called `id` ("<package>:<name>"), nullptr if there is none.
[[nodiscard]] const PackageSound* find_package_sound(std::span<const PackageSound> sounds,
                                                     std::string_view id) noexcept;

/// Whether the file starts as a WAVE, Ogg or FLAC file does.
[[nodiscard]] bool is_sound_file(const std::filesystem::path& path);

}  // namespace o1602::assets
