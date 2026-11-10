// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/unlock_levels.hpp"

#include <algorithm>
#include <charconv>

#include "o1602/assets/original_names.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/core/data_file.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A name a package may give its level: lower case letters, digits and '_', as the original's.
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

/// Inhabitants a level may ask for, the most a city of 20,000 houses could hold.
constexpr std::int64_t kMaxInhabitants = 10'000'000;

}  // namespace

std::optional<std::uint8_t> level_of(std::span<const InfraLevel> levels, std::string_view id) {
    const std::size_t colon = id.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    if (id.substr(0, colon) == "anno1602") {
        const std::string_view after = id.substr(colon + 1);
        std::uint32_t number = 0;
        if (const OriginalName* n = find_original_name(original_level_names(), after)) {
            number = n->first;
        } else if (const auto [end, error] = std::from_chars(after.data(), after.data() + after.size(), number);
                   error != std::errc{} || end != after.data() + after.size()) {
            return std::nullopt;
        }
        // Only the levels of haeuser.cod (0 is none): a package's levels have ids of their own.
        if (number >= 1 && number < levels.size() && levels[number].id.empty()) {
            return static_cast<std::uint8_t>(number);
        }
        return std::nullopt;
    }
    for (std::size_t n = 1; n < levels.size(); ++n) {
        if (levels[n].id == id) {
            return static_cast<std::uint8_t>(n);
        }
    }
    return std::nullopt;
}

bool apply_levels_file(std::vector<InfraLevel>& levels, std::string_view package, std::string_view utf8,
                       std::vector<std::string>& warnings, std::span<const ResidentClass> classes) {
    const std::string where = std::string(package) + "/" + std::string(kLevelsFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return false;
    }
    if (levels.empty()) {
        levels.resize(1);  // number 0: no level
    }
    bool changed = false;
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        // A level there is, or a new one of this package.
        std::optional<std::uint8_t> target;
        std::string id;
        if (t.name.find(':') != std::string::npos) {
            target = level_of(levels, t.name);
            if (!target) {
                warnings.push_back(at + "[" + t.name +
                                   "]: names none of the original's unlock levels (anno1602:<name or number>) and "
                                   "none a package added");
                continue;
            }
        } else if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name + "]: a level's name holds only lower case letters, digits and '_'");
            continue;
        } else {
            id = std::string(package) + ":" + t.name;
            if (level_of(levels, id)) {
                warnings.push_back(at + id + " is declared twice; the second is passed over");
                continue;
            }
            if (levels.size() > kMaxUnlockLevel) {
                warnings.push_back(at + id + ": a game has at most " + std::to_string(kMaxUnlockLevel) +
                                   " unlock levels");
                continue;
            }
        }

        std::optional<std::uint8_t> resident_class;
        std::optional<std::int64_t> min_inhabitants;
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            if (e.key == "class") {
                const std::string* s = e.value.string();
                if (s != nullptr) {
                    resident_class = class_of(classes, *s);
                }
                if (s == nullptr || !resident_class) {
                    warnings.push_back(field + "names no class (anno1602:<name or number> or <package>:<name>)");
                    ok = false;
                }
            } else if (e.key == "min_inhabitants") {
                const std::int64_t* n = e.value.integer();
                if (n == nullptr || *n < 0 || *n > kMaxInhabitants) {
                    warnings.push_back(field + "is a number from 0 to " + std::to_string(kMaxInhabitants));
                    ok = false;
                } else {
                    min_inhabitants = *n;
                }
            } else {
                warnings.push_back(field + "is no field of an unlock level and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (!target && (!resident_class || !min_inhabitants)) {
            warnings.push_back(at + id + " needs a 'class' and 'min_inhabitants'");
            continue;
        }
        InfraLevel& level = target ? levels[*target] : levels.emplace_back();
        if (!target) {
            level.id = id;
        }
        if (resident_class) {
            level.resident_class = *resident_class;
        }
        if (min_inhabitants) {
            level.min_inhabitants = static_cast<std::uint32_t>(*min_inhabitants);
        }
        changed = true;
    }
    return changed;
}

}  // namespace o1602::assets
