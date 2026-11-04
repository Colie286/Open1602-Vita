// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/package_arms.hpp"

#include <algorithm>

#include "o1602/core/data_file.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A banner's id: "<origin>:<name>", each of lower case letters, digits and '_'.
bool is_banner_id(std::string_view id) noexcept {
    const std::size_t colon = id.find(':');
    const auto is_part = [](std::string_view s) {
        return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](const char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        });
    };
    return colon != std::string_view::npos && is_part(id.substr(0, colon)) && is_part(id.substr(colon + 1));
}

}  // namespace

const PackageArms* find_package_arms(std::span<const PackageArms> arms, std::string_view banner) noexcept {
    const auto it = std::find_if(arms.begin(), arms.end(), [&](const PackageArms& a) { return a.banner == banner; });
    return it == arms.end() ? nullptr : &*it;
}

void apply_arms_file(std::vector<PackageArms>& arms, std::string_view package, std::string_view utf8,
                     std::span<const PackageIcon> icons, std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kArmsFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_banner_id(t.name)) {
            warnings.push_back(at + "[" + t.name + "]: names no banner (\"anno1602:red\", \"open1602:orange\", ...)");
            continue;
        }
        std::string icon;
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            if (e.key == "icon") {
                const std::string* s = e.value.string();
                if (s == nullptr || find_package_icon(icons, *s) == nullptr) {
                    warnings.push_back(field + "names no icon of a package (icons.toml, <package>:<name>)");
                    ok = false;
                } else {
                    icon = *s;
                }
            } else {
                warnings.push_back(field + "is no field of a coat of arms and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (icon.empty()) {
            warnings.push_back(at + "[" + t.name + "] needs an 'icon'");
            continue;
        }
        std::erase_if(arms, [&](const PackageArms& a) { return a.banner == t.name; });
        arms.push_back(PackageArms{t.name, std::move(icon)});
    }
}

}  // namespace o1602::assets
