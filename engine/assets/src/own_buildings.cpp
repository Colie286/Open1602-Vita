// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/own_buildings.hpp"

#include <algorithm>
#include <limits>
#include <optional>

#include "o1602/assets/original_names.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/assets/unlock_levels.hpp"
#include "o1602/core/data_file.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A name a package may give its building: letters, digits and underscores, as the ids of the
/// original's buildings have them (original_names.hpp).
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

/// The building `like` names: "anno1602:<name or number>" of the original's.
const BuildingType* building_of(const BuildingTable& table, std::string_view like) {
    const auto id = original_building_id(like);
    return id ? table.by_id(*id) : nullptr;
}

}  // namespace

const OwnBuilding* find_own_building(std::span<const OwnBuilding> own, std::string_view id) noexcept {
    const auto it = std::find_if(own.begin(), own.end(), [&](const OwnBuilding& o) { return o.id == id; });
    return it == own.end() ? nullptr : &*it;
}

void apply_own_buildings_file(BuildingTable& table, std::vector<OwnBuilding>& own, std::string_view package,
                              std::string_view utf8, std::vector<std::string>& warnings, const Goods& goods,
                              std::span<const ResidentClass> classes, std::span<const InfraLevel> levels,
                              std::span<const PackageImage> images) {
    const std::string where = std::string(package) + "/" + std::string(kOwnBuildingsFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    std::size_t added = 0;
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name +
                               "]: a building's name holds only lower case letters, digits and '_'");
            continue;
        }
        const std::string id = std::string(package) + ":" + t.name;
        if (find_own_building(own, id) != nullptr) {
            warnings.push_back(at + id + " is declared twice; the second is passed over");
            continue;
        }
        if (added >= kMaxOwnBuildings) {
            warnings.push_back(at + "a package declares at most " + std::to_string(kMaxOwnBuildings) +
                               " buildings; the rest are passed over");
            return;
        }
        // Every field of the table, before anything is added to the table.
        const BuildingType* like = nullptr;
        std::optional<std::int64_t> gfx;
        std::optional<std::int64_t> size_x;
        std::optional<std::int64_t> size_y;
        std::optional<std::uint8_t> ware;
        std::optional<std::uint8_t> raw;
        std::optional<std::uint8_t> raw2;
        std::optional<std::uint8_t> resident_class;
        std::optional<std::uint8_t> level;
        const PackageImage* image = nullptr;
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            if (e.key == "like") {
                if (e.value.string() == nullptr) {
                    warnings.push_back(field + "is a string, a building of the original");
                    ok = false;
                } else if (like = building_of(table, *e.value.string()); like == nullptr) {
                    warnings.push_back(field + "names no building of the original (anno1602:<number or name>)");
                    ok = false;
                }
            } else if (e.key == "gfx" && e.value.string() != nullptr) {
                // The first sprite of an image of a package (package_graphics.hpp).
                image = find_package_image(images, *e.value.string());
                if (image == nullptr) {
                    warnings.push_back(field + "names no image of a package (<package>:<name>, graphics.toml)");
                    ok = false;
                } else {
                    gfx = image->first;
                }
            } else if (e.key == "gfx" || e.key == "size_x" || e.key == "size_y") {
                const std::int64_t* n = e.value.integer();
                const std::int64_t max = e.key == "gfx" ? 65534 : 255;
                if (n == nullptr || *n < (e.key == "gfx" ? 0 : 1) || *n > max) {
                    warnings.push_back(field + "is a number from " + (e.key == "gfx" ? "0" : "1") + " to " +
                                       std::to_string(max));
                    ok = false;
                } else {
                    (e.key == "gfx" ? gfx : e.key == "size_x" ? size_x : size_y) = *n;
                }
            } else if (e.key == "ware" || e.key == "raw" || e.key == "raw2") {
                // What it makes and uses: a good by its id, or "" for none.
                const std::string* s = e.value.string();
                const auto w = s == nullptr ? std::nullopt
                               : s->empty() ? std::optional<std::uint8_t>(std::uint8_t{0})
                                            : ware_of(goods, *s);
                if (!w) {
                    warnings.push_back(field +
                                       "names no good (anno1602:<name or number> or <package>:<name>, \"\" for none)");
                    ok = false;
                } else {
                    (e.key == "ware" ? ware : e.key == "raw" ? raw : raw2) = w;
                }
            } else if (e.key == "class") {
                // Whose house it is: a class of the original's or one a package added.
                const std::string* s = e.value.string();
                const std::span<const ResidentClass> known = classes.empty() ? table.resident_classes() : classes;
                if (s != nullptr) {
                    resident_class = class_of(known, *s);
                }
                if (s == nullptr || !resident_class) {
                    warnings.push_back(field + "names no class (anno1602:<name or number> or <package>:<name>)");
                    ok = false;
                }
            } else if (e.key == "level") {
                // When it may be built: an unlock level by its id, or "" for always.
                const std::string* s = e.value.string();
                const std::span<const InfraLevel> known = levels.empty() ? table.infra_levels() : levels;
                if (s != nullptr) {
                    level = s->empty() ? std::optional<std::uint8_t>(std::uint8_t{0}) : level_of(known, *s);
                }
                if (s == nullptr || !level) {
                    warnings.push_back(field +
                                       "names no unlock level (anno1602:<name or number> or <package>:<name>, \"\" "
                                       "for always)");
                    ok = false;
                }
            } else {
                warnings.push_back(field + "is no field of a building of a package and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (like == nullptr) {
            warnings.push_back(at + id + " names no building to copy ('like')");
            continue;
        }
        if (resident_class && like->prod_kind != prod::kHouse) {
            warnings.push_back(at + id + ": 'class' is for a house only");
            continue;
        }
        BuildingType b = *like;
        if (resident_class) {
            b.resident_class = *resident_class;
        }
        if (level) {
            b.infra = *level;
        }
        if (gfx) {
            b.gfx = static_cast<std::uint32_t>(*gfx);
        }
        if (size_x) {
            b.size_x = static_cast<std::uint32_t>(*size_x);
        }
        if (size_y) {
            b.size_y = static_cast<std::uint32_t>(*size_y);
        }
        if (ware) {
            b.ware = *ware;
        }
        if (raw) {
            b.raw_ware = *raw;
        }
        if (raw2) {
            b.raw2_ware = *raw2;
        }
        // The image holds every sprite the building draws: its tiles, turned four ways, in every
        // frame of its animation (render::building_sprite).
        if (image != nullptr) {
            const std::uint64_t needed = std::uint64_t{b.tile_count()} + std::uint64_t{b.rotate} * 3 +
                                         (b.anim_count > 1 ? std::uint64_t{b.anim_add} * (b.anim_count - 1) : 0);
            if (needed > image->count) {
                warnings.push_back(at + id + ": its image " + image->id + " holds " + std::to_string(image->count) +
                                   " sprites, the building draws " + std::to_string(needed));
                continue;
            }
        }
        const auto number = table.add(b);
        if (!number) {
            warnings.push_back(at + id + ": " + number.error().message);
            continue;
        }
        own.push_back(OwnBuilding{id, *number});
        ++added;
    }
}

}  // namespace o1602::assets
