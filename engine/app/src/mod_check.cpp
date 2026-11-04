// SPDX-License-Identifier: GPL-3.0-or-later
#include "mod_check.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string_view>

namespace o1602::app {

namespace {

bool same_title(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

/// `text` as a JSON string, quotes included.
std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out += c;  // UTF-8 passes as it is
                }
        }
    }
    return out + "\"";
}

/// Where a problem points to: "<package>/<file> line <N>: " or "<package>/<file>: ".
struct Place {
    std::string package;
    std::string file;
    std::optional<std::size_t> line;
};

std::optional<Place> place_of(std::string_view text) {
    const std::size_t slash = text.find('/');
    const std::size_t space = text.find(' ');
    if (slash == std::string_view::npos || slash == 0 || (space != std::string_view::npos && space < slash)) {
        return std::nullopt;
    }
    std::size_t end = space == std::string_view::npos ? text.size() : space;
    std::string_view file = text.substr(slash + 1, end - slash - 1);
    if (file.ends_with(':')) {
        file.remove_suffix(1);
    }
    const bool known = file.ends_with(".toml") || file.ends_with(".lua") || file.ends_with(".o1602") ||
                       file.ends_with(".szs") || file.ends_with(".scp");
    if (!known) {
        return std::nullopt;
    }
    Place place{std::string(text.substr(0, slash)), std::string(file), std::nullopt};
    constexpr std::string_view kLine = " line ";
    if (text.substr(end).starts_with(kLine)) {
        std::size_t n = 0;
        std::size_t at = end + kLine.size();
        bool digits = false;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            n = n * 10 + static_cast<std::size_t>(text[at] - '0');
            digits = true;
            ++at;
        }
        if (digits) {
            place.line = n;
        }
    }
    return place;
}

bool has_banner(const render::BannerTable& banners, std::string_view id) {
    for (std::size_t b = 0; b < banners.size(); ++b) {
        const render::Banner* banner = banners.get(static_cast<sim::BannerId>(b));
        if (banner != nullptr && banner->id == id) {
            return true;
        }
    }
    return false;
}

}  // namespace

void check_music_replacements(std::span<const audio::MusicTrack> music, std::span<const std::string> tracks,
                              std::span<const std::string> fights, std::vector<std::string>& problems) {
    for (const audio::MusicTrack& track : music) {
        if (track.replaces.empty()) {
            continue;
        }
        const auto same = [&](const std::string& t) { return same_title(t, track.replaces); };
        if (std::none_of(tracks.begin(), tracks.end(), same) && std::none_of(fights.begin(), fights.end(), same)) {
            problems.push_back(track.title + ": MUSIC8 has no track \"" + track.replaces +
                               "\" to replace; the track is passed over");
        }
    }
}

void check_arms_banners(std::span<const assets::PackageArms> arms, const render::BannerTable& banners,
                        std::vector<std::string>& problems) {
    for (const assets::PackageArms& a : arms) {
        if (!has_banner(banners, a.banner)) {
            problems.push_back(a.icon + ": no banner is called \"" + a.banner + "\"; its coat of arms is not shown");
        }
    }
}

void check_flag_banners(std::span<const assets::PackageFlag> flags, const render::BannerTable& banners,
                        std::vector<std::string>& problems) {
    for (const assets::PackageFlag& f : flags) {
        if (!has_banner(banners, f.banner)) {
            problems.push_back(f.image.id + ": no banner is called \"" + f.banner + "\"; its flag is not shown");
        }
    }
}

std::string problems_json(std::span<const assets::ModPackage> packages, std::span<const std::string> problems) {
    std::string out = "{\"packages\":[";
    for (std::size_t i = 0; i < packages.size(); ++i) {
        out += (i == 0 ? "" : ",");
        out += "{\"id\":" + json_string(packages[i].id) + ",\"version\":" + json_string(packages[i].version) + "}";
    }
    out += "],\"problems\":[";
    for (std::size_t i = 0; i < problems.size(); ++i) {
        out += (i == 0 ? "" : ",");
        out += "{\"text\":" + json_string(problems[i]);
        if (const auto place = place_of(problems[i])) {
            out += ",\"package\":" + json_string(place->package) + ",\"file\":" + json_string(place->file);
            if (place->line) {
                out += ",\"line\":" + std::to_string(*place->line);
            }
        }
        out += "}";
    }
    return out + "]}";
}

}  // namespace o1602::app
