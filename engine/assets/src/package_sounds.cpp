// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/package_sounds.hpp"

#include <algorithm>
#include <cstring>

#include "o1602/assets/game_content.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/file.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A name a package may give its sound: lower case letters, digits and '_', as its other entries.
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

/// The name of a sound file of SAMPLES: letters and digits, as the original's ("kanone5", "baumW").
bool is_sample_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::string lower_name(std::string_view name) {
    std::string s(name);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return s;
}

}  // namespace

bool is_sound_file(const std::filesystem::path& path) {
    const auto head = core::read_file_head(path, 4);
    if (!head || head->bytes.size() < 4) {
        return false;
    }
    const auto is = [&](const char* magic) { return std::memcmp(head->bytes.data(), magic, 4) == 0; };
    return is("RIFF") || is("OggS") || is("fLaC");
}

const PackageSound* find_package_sound(std::span<const PackageSound> sounds, std::string_view id) noexcept {
    const auto it = std::find_if(sounds.begin(), sounds.end(), [&](const PackageSound& s) { return s.id == id; });
    return it == sounds.end() ? nullptr : &*it;
}

void apply_sounds_file(std::vector<PackageSound>& sounds, std::string_view package, const std::filesystem::path& folder,
                       std::string_view utf8, std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kSoundsFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name + "]: a sound's name holds only lower case letters, digits and '_'");
            continue;
        }
        const std::string id = std::string(package) + ":" + t.name;
        if (find_package_sound(sounds, id) != nullptr) {
            warnings.push_back(at + id + " is declared twice; the second is passed over");
            continue;
        }
        std::string file;
        std::string replaces;
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            const std::string* s = e.value.string();
            if (e.key == "file") {
                if (s == nullptr || !safe_package_path(*s)) {
                    warnings.push_back(field + "is the path of a sound file in the package, folders split by '/'");
                    ok = false;
                } else {
                    file = *s;
                }
            } else if (e.key == "replaces") {
                if (s == nullptr || !is_sample_name(*s)) {
                    warnings.push_back(field + "is the name of a sound file of SAMPLES without \".wav\"");
                    ok = false;
                } else {
                    replaces = lower_name(*s);
                }
            } else {
                warnings.push_back(field + "is no field of a sound and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (file.empty()) {
            warnings.push_back(at + id + " needs a 'file'");
            continue;
        }
        if (sounds.size() >= kMaxPackageSounds) {
            warnings.push_back(at + "the packages have at most " + std::to_string(kMaxPackageSounds) +
                               " sounds together; the rest are passed over");
            return;
        }
        PackageSound sound;
        sound.id = id;
        sound.number = static_cast<std::uint16_t>(kFirstPackageSound + sounds.size());
        sound.file = folder / core::path_from_utf8(file);
        sound.replaces = std::move(replaces);
        if (!is_sound_file(sound.file)) {
            warnings.push_back(at + file + " is missing or no WAV, Ogg Vorbis or FLAC file");
            continue;
        }
        sounds.push_back(std::move(sound));
    }
}

}  // namespace o1602::assets
