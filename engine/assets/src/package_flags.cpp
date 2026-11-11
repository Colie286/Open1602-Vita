// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/package_flags.hpp"

#include <algorithm>

#include "o1602/assets/package_arms.hpp"
#include "o1602/core/data_file.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

}  // namespace

const PackageFlag* find_package_flag(std::span<const PackageFlag> flags, std::string_view banner) noexcept {
    const auto it = std::find_if(flags.begin(), flags.end(), [&](const PackageFlag& f) { return f.banner == banner; });
    return it == flags.end() ? nullptr : &*it;
}

void apply_flags_file(std::vector<PackageFlag>& flags, std::string_view package, std::string_view utf8,
                      std::span<const PackageImage> images, std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kFlagsFile);
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
        const PackageImage* image = nullptr;
        bool tint = false;
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            if (e.key == "image") {
                const std::string* s = e.value.string();
                image = s != nullptr ? find_package_image(images, *s) : nullptr;
                if (image == nullptr) {
                    warnings.push_back(field + "names no image of a package (graphics.toml, <package>:<name>)");
                    ok = false;
                } else if (image->count < kFlagFrames) {
                    warnings.push_back(field + "names " + image->id + ", which holds " + std::to_string(image->count) +
                                       " sprites; a flag waves in " + std::to_string(kFlagFrames));
                    ok = false;
                }
            } else if (e.key == "tint") {
                if (e.value.boolean() == nullptr) {
                    warnings.push_back(field + "is true or false");
                    ok = false;
                } else {
                    tint = *e.value.boolean();
                }
            } else {
                warnings.push_back(field + "is no field of a flag and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (image == nullptr) {
            warnings.push_back(at + "[" + t.name + "] needs an 'image'");
            continue;
        }
        std::erase_if(flags, [&](const PackageFlag& f) { return f.banner == t.name; });
        flags.push_back(PackageFlag{t.name, *image, tint});
    }
}

}  // namespace o1602::assets
