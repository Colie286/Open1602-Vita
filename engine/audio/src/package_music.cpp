// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/audio/package_music.hpp"

#include <algorithm>
#include <cstring>

#include "o1602/assets/game_content.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/file.hpp"

namespace o1602::audio {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// Whether the file starts as a WAVE, Ogg or FLAC file does (sound_file.hpp).
bool sound_file(const std::filesystem::path& path) {
    const auto head = core::read_file_head(path, 4);
    if (!head || head->bytes.size() < 4) {
        return false;
    }
    const auto is = [&](const char* magic) { return std::memcmp(head->bytes.data(), magic, 4) == 0; };
    return is("RIFF") || is("OggS") || is("fLaC");
}

}  // namespace

void apply_music_file(std::vector<MusicTrack>& tracks, std::string_view package, const std::filesystem::path& folder,
                      std::string_view utf8, std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kMusicFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        MusicTrack track;
        track.title = t.name;
        std::string file;
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            const std::string* s = e.value.string();
            if (e.key == "file") {
                if (s == nullptr || !assets::safe_package_path(*s)) {
                    warnings.push_back(field + "is the path of a sound file in the package, folders split by '/'");
                    ok = false;
                } else {
                    file = *s;
                }
            } else if (e.key == "title") {
                if (s == nullptr || s->empty() || s->size() > kMaxTrackTitle) {
                    warnings.push_back(field + "is a text of 1 to " + std::to_string(kMaxTrackTitle) + " bytes");
                    ok = false;
                } else {
                    track.title = *s;
                }
            } else if (e.key == "battle") {
                if (e.value.boolean() == nullptr) {
                    warnings.push_back(field + "is true or false");
                    ok = false;
                } else {
                    track.battle = *e.value.boolean();
                }
            } else {
                warnings.push_back(field + "is no field of a track and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (file.empty()) {
            warnings.push_back(at + "[" + t.name + "] needs a 'file'");
            continue;
        }
        track.file = folder / core::path_from_utf8(file);
        if (!sound_file(track.file)) {
            warnings.push_back(at + file + " is missing or no WAV, Ogg Vorbis or FLAC file");
            continue;
        }
        if (std::any_of(tracks.begin(), tracks.end(), [&](const MusicTrack& m) { return m.title == track.title; })) {
            warnings.push_back(at + "a track is called \"" + track.title + "\" already; this one is passed over");
            continue;
        }
        tracks.push_back(std::move(track));
    }
}

}  // namespace o1602::audio
