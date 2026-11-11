// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/building_looks.hpp"

#include <algorithm>

#include "o1602/assets/original_names.hpp"
#include "o1602/core/data_file.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

}  // namespace

std::uint64_t building_sprite_count(const BuildingType& type) noexcept {
    return std::uint64_t{type.tile_count()} + std::uint64_t{type.rotate} * 3 +
           (type.anim_count > 1 ? std::uint64_t{type.anim_add} * (type.anim_count - 1) : 0);
}

void apply_building_images_file(std::vector<SpriteRedirect>& redirects, std::string_view package, std::string_view utf8,
                                std::span<const PackageImage> images, const BuildingTable& buildings,
                                std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kBuildingImagesFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        const auto id = original_building_id(t.name);
        const BuildingType* building = id ? buildings.by_id(*id) : nullptr;
        if (building == nullptr) {
            warnings.push_back(at + "[" + t.name + "]: names no building of the original (anno1602:<name or number>)");
            continue;
        }
        const PackageImage* image = nullptr;
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            if (e.key == "image") {
                const std::string* s = e.value.string();
                image = s != nullptr ? find_package_image(images, *s) : nullptr;
                if (image == nullptr) {
                    warnings.push_back(field + "names no image of a package (graphics.toml, <package>:<name>)");
                    ok = false;
                }
            } else {
                warnings.push_back(field + "is no field of a building's image and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (image == nullptr) {
            warnings.push_back(at + "[" + t.name + "] needs an 'image'");
            continue;
        }
        const std::uint64_t needed = building_sprite_count(*building);
        if (needed > image->count) {
            warnings.push_back(at + "[" + t.name + "]: its image " + image->id + " holds " +
                               std::to_string(image->count) + " sprites, the building draws " + std::to_string(needed));
            continue;
        }
        if (building->gfx >= kFirstPackageSprite) {
            warnings.push_back(at + "[" + t.name + "]: the building draws a package's image already");
            continue;
        }
        const SpriteRedirect redirect{building->gfx, static_cast<std::uint32_t>(needed), image->first};
        std::erase_if(redirects, [&](const SpriteRedirect& r) { return r.from == redirect.from; });
        redirects.push_back(redirect);
    }
}

}  // namespace o1602::assets
