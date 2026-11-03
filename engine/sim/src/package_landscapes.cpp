// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/package_landscapes.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "o1602/core/data_file.hpp"
#include "o1602/core/text.hpp"

namespace o1602::sim {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A preset's name in its package: letters, digits and underscores.
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

/// The landscape `like` names: one of Open1602's presets or an earlier package's.
std::optional<WorldSettings::Landscape> landscape_of(std::span<const PackageLandscape> presets, std::string_view like) {
    if (const auto it = std::ranges::find(landscape_presets(), like, &LandscapePreset::name);
        it != landscape_presets().end()) {
        return it->landscape;
    }
    if (const PackageLandscape* p = find_package_landscape(presets, like)) {
        return p->landscape;
    }
    return std::nullopt;
}

}  // namespace

std::string PackageLandscape::title(std::string_view language) const {
    for (const std::string_view code : {language, std::string_view{}, std::string_view{"en"}}) {
        if (const auto it = titles.find(code); it != titles.end()) {
            return it->second;
        }
    }
    return titles.empty() ? id : titles.begin()->second;
}

const PackageLandscape* find_package_landscape(std::span<const PackageLandscape> presets,
                                               std::string_view id) noexcept {
    const auto it = std::ranges::find(presets, id, &PackageLandscape::id);
    return it == presets.end() ? nullptr : &*it;
}

void apply_landscapes_file(std::vector<PackageLandscape>& presets, std::string_view package, std::string_view utf8,
                           std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kLandscapesFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    std::size_t added = 0;
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name + "]: a preset's name holds only lower case letters, digits and '_'");
            continue;
        }
        PackageLandscape preset;
        preset.id = std::string(package) + ":" + t.name;
        if (find_package_landscape(presets, preset.id) != nullptr) {
            warnings.push_back(at + preset.id + " is declared twice; the second is passed over");
            continue;
        }
        if (added >= kMaxPackageLandscapes) {
            warnings.push_back(at + "a package names at most " + std::to_string(kMaxPackageLandscapes) +
                               " presets; the rest are passed over");
            return;
        }
        // `like` first, wherever it stands: the other fields change what it brings.
        if (const core::DataValue* like = t.find("like")) {
            const std::string* s = like->string();
            const auto from = s != nullptr ? landscape_of(presets, *s) : std::nullopt;
            if (from) {
                preset.landscape = *from;
            } else {
                warnings.push_back(line_of(where, like->line) +
                                   "'like' names no preset (varied, archipelago, continents, giants, even or "
                                   "<package>:<name>)");
            }
        }
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            if (e.key == "like") {
                continue;
            }
            if (e.key == "title" || e.key.starts_with("title.")) {
                const std::string* s = e.value.string();
                if (s == nullptr || s->empty() || s->size() > 64) {
                    warnings.push_back(field + "is a text of 1 to 64 bytes");
                } else {
                    const std::string code = e.key == "title" ? std::string() : e.key.substr(6);
                    preset.titles.insert_or_assign(code, core::utf8_to_cp1252(*s));
                }
                continue;
            }
            if (std::ranges::find(landscape_names::kKeys, e.key) == landscape_names::kKeys.end()) {
                warnings.push_back(field + "is no field of a preset and is passed over");
                continue;
            }
            const std::int64_t* n = e.value.integer();
            const std::string* s = e.value.string();
            const std::string text = n != nullptr ? std::to_string(*n) : s != nullptr ? *s : std::string();
            if (text.empty() || !set_landscape(preset.landscape, e.key, text)) {
                warnings.push_back(field + "has a value out of its range or no name of its choices");
            }
        }
        preset.landscape.name = preset.id;  // not the name of `like`
        presets.push_back(std::move(preset));
        ++added;
    }
}

}  // namespace o1602::sim
