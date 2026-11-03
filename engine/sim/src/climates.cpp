// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/climates.hpp"

#include <algorithm>
#include <array>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/original_names.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/game_data.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t bit(std::uint8_t crop) noexcept {
    return 1U << (crop - ware::kCornField);
}

struct CropName {
    std::string_view name;
    std::uint8_t ware;
};

/// The crops a climate may name, the six of the plantations.
constexpr std::array<CropName, 6> kCrops = {{
    {"tobacco", ware::kTobaccoField},
    {"spices", ware::kSpiceField},
    {"sugar", ware::kSugarField},
    {"cotton", ware::kCottonField},
    {"wine", ware::kWineField},
    {"cocoa", ware::kCocoaField},
}};

constexpr std::uint32_t kAllCrops = bit(ware::kTobaccoField) | bit(ware::kSpiceField) | bit(ware::kSugarField) |
                                    bit(ware::kCottonField) | bit(ware::kWineField) | bit(ware::kCocoaField);

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A climate's name in its package: letters, digits and underscores.
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

/// The entries of a list "a, b, c", without the empty ones.
std::vector<std::string_view> entries(std::string_view text) {
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view entry = trim(text.substr(0, comma));
        if (!entry.empty()) {
            out.push_back(entry);
        }
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    return out;
}

/// The building a forest or ground entry names: "anno1602:<name or number>" or a package's own building.
std::optional<std::uint32_t> tree_of(std::string_view id, std::span<const assets::OwnBuilding> own) {
    if (const auto original = assets::original_building_id(id)) {
        return original;
    }
    if (const assets::OwnBuilding* b = assets::find_own_building(own, id)) {
        return b->number;
    }
    return std::nullopt;
}

}  // namespace

std::string Climate::name(std::string_view language) const {
    for (const std::string_view code : {language, std::string_view{}, std::string_view{"en"}}) {
        if (const auto it = names.find(code); it != names.end()) {
            return it->second;
        }
    }
    return names.empty() ? id : names.begin()->second;
}

std::uint32_t original_climate_crops(std::uint8_t like) noexcept {
    if (like == climate::kNorth) {
        return bit(ware::kTobaccoField) | bit(ware::kSugarField) | bit(ware::kWineField);
    }
    if (like == climate::kSouth) {
        return bit(ware::kSpiceField) | bit(ware::kCottonField) | bit(ware::kCocoaField);
    }
    return 0;
}

std::optional<std::uint32_t> crop_bit(std::string_view name) noexcept {
    const auto it = std::ranges::find(kCrops, name, &CropName::name);
    return it == kCrops.end() ? std::nullopt : std::optional<std::uint32_t>(bit(it->ware));
}

void apply_climates_file(std::vector<Climate>& climates, std::string_view package, std::string_view utf8,
                         std::vector<std::string>& warnings, const assets::BuildingTable* buildings,
                         std::span<const assets::OwnBuilding> own) {
    const std::string where = std::string(package) + "/" + std::string(kClimatesFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name + "]: a climate's name holds only lower case letters, digits and '_'");
            continue;
        }
        Climate c;
        c.id = std::string(package) + ":" + t.name;
        if (std::ranges::find(climates, c.id, &Climate::id) != climates.end()) {
            warnings.push_back(at + c.id + " is declared twice; the second is passed over");
            continue;
        }
        if (climates.size() >= kMaxClimates) {
            warnings.push_back(at + "the packages name at most " + std::to_string(kMaxClimates) +
                               " climates; the rest are passed over");
            return;
        }
        // `like` first, wherever it stands: the crops and the forest start from it.
        const core::DataValue* like = t.find("like");
        const std::string* like_text = like != nullptr ? like->string() : nullptr;
        if (like_text != nullptr && *like_text == "north") {
            c.like = climate::kNorth;
        } else if (like_text != nullptr && *like_text == "south") {
            c.like = climate::kSouth;
        } else {
            warnings.push_back((like != nullptr ? line_of(where, like->line) : at) + c.id +
                               ": 'like' must be \"north\" or \"south\"; the climate is passed over");
            continue;
        }
        c.crops = original_climate_crops(c.like);
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            const std::string* s = e.value.string();
            if (e.key == "like") {
                continue;
            }
            if (e.key == "name" || e.key.starts_with("name.")) {
                if (s == nullptr || s->empty() || s->size() > 64) {
                    warnings.push_back(field + "is a text of 1 to 64 bytes");
                } else {
                    c.names.insert_or_assign(e.key == "name" ? std::string() : e.key.substr(5),
                                             core::utf8_to_cp1252(*s));
                }
            } else if (e.key == "crops") {
                if (s == nullptr) {
                    warnings.push_back(field + "is a list of crops, such as \"tobacco, wine\"");
                    continue;
                }
                std::uint32_t crops = 0;
                for (const std::string_view crop : entries(*s)) {
                    if (const auto b = crop_bit(crop)) {
                        crops |= *b;
                    } else {
                        warnings.push_back(field + "names no crop '" + std::string(crop) +
                                           "' (tobacco, spices, sugar, cotton, wine, cocoa); it is passed over");
                    }
                }
                c.crops = crops;
            } else if (e.key == "forest" || e.key == "ground") {
                const bool forest = e.key == "forest";
                const std::int32_t wanted = forest ? kind::kForest : kind::kGround;
                const std::size_t most = forest ? kMaxForestTrees : kMaxGroundTiles;
                const std::string what = forest ? "tree" : "ground";
                if (s == nullptr) {
                    warnings.push_back(field + (forest ? "is a list of trees, such as \"anno1602:mixed_forest\""
                                                       : "is a list of kinds of ground (anno1602:<number>)"));
                    continue;
                }
                std::vector<std::uint32_t> tiles;
                for (const std::string_view tile : entries(*s)) {
                    const auto id = tree_of(tile, own);
                    const assets::BuildingType* b = id && buildings != nullptr ? buildings->by_id(*id) : nullptr;
                    if (!id || (buildings != nullptr && (b == nullptr || b->kind != wanted))) {
                        std::string text = field;
                        text += "'";
                        text += tile;
                        text += "' is no ";
                        text += what;
                        text += "; it is passed over";
                        warnings.push_back(std::move(text));
                    } else if (tiles.size() >= most) {
                        warnings.push_back(field + "names more than " + std::to_string(most) + " " +
                                           (forest ? "trees" : "kinds of ground") + "; the rest are passed over");
                        break;
                    } else if (std::ranges::find(tiles, *id) == tiles.end()) {
                        tiles.push_back(*id);
                    }
                }
                (forest ? c.forest : c.ground) = std::move(tiles);
            } else if (e.key == "share") {
                const std::int64_t* n = e.value.integer();
                if (n == nullptr || *n < 0 || *n > 100) {
                    warnings.push_back(field + "is a percent from 0 to 100");
                } else {
                    c.share = static_cast<std::uint8_t>(*n);
                }
            } else {
                warnings.push_back(field + "is no field of a climate and is passed over");
            }
        }
        climates.push_back(std::move(c));
    }
}

bool valid(std::span<const Climate> climates) noexcept {
    if (climates.size() > kMaxClimates) {
        return false;
    }
    for (std::size_t i = 0; i < climates.size(); ++i) {
        const Climate& c = climates[i];
        if (c.id.empty() || c.like >= climate::kCount || (c.crops & ~kAllCrops) != 0 || c.share > 100 ||
            c.forest.size() > kMaxForestTrees || c.ground.size() > kMaxGroundTiles) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (climates[j].id == c.id) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace o1602::sim
