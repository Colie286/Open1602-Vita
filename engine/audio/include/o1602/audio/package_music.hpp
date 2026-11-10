// SPDX-License-Identifier: GPL-3.0-or-later
//
// Music of content packages: `music.toml` names tracks in WAV, Ogg Vorbis or FLAC files, which join
// the original's MUSIC8 tracks, as normal or as battle music (docs/modding/sounds.md).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace o1602::audio {

/// The file of a package that names its music.
inline constexpr std::string_view kMusicFile = "music.toml";
/// The longest title of a track, in bytes.
inline constexpr std::size_t kMaxTrackTitle = 64;

/// A track of a package. Its title is what the music list shows and what the settings keep of a
/// track the player turned off.
struct MusicTrack {
    std::string title;
    std::filesystem::path file;
    bool battle = false;  ///< played while shots fall, as the original's Fight tracks

    friend bool operator==(const MusicTrack&, const MusicTrack&) = default;
};

/// Reads `music.toml` of `package` (in `folder`) and adds its tracks to `tracks`. Checks that each
/// file is there and is a sound file by its first bytes; it is decoded when the track starts. A
/// table that cannot be read is passed over with a warning.
void apply_music_file(std::vector<MusicTrack>& tracks, std::string_view package, const std::filesystem::path& folder,
                      std::string_view utf8, std::vector<std::string>& warnings);

}  // namespace o1602::audio
