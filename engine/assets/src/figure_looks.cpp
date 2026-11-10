// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/figure_looks.hpp"

#include <algorithm>
#include <charconv>
#include <optional>

#include "o1602/assets/original_names.hpp"
#include "o1602/core/data_file.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// The figures "anno1602:<name or number>" stands for: one number, or a unit in its four colours.
std::optional<OriginalName> figures_of(std::string_view id) {
    constexpr std::string_view kOriginal = "anno1602:";
    if (!id.starts_with(kOriginal)) {
        return std::nullopt;
    }
    const std::string_view after = id.substr(kOriginal.size());
    if (const OriginalName* named = find_original_name(original_figure_names(), after)) {
        return *named;
    }
    std::uint32_t number = 0;
    const bool hex = after.starts_with("0x");
    const std::string_view digits = hex ? after.substr(2) : after;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number, hex ? 16 : 10);
    if (error != std::errc{} || end != digits.data() + digits.size() || digits.empty()) {
        return std::nullopt;
    }
    return OriginalName{after, number, 1};
}

}  // namespace

std::uint32_t figure_sprite_count(const FigureType& type) noexcept {
    constexpr std::uint32_t kLastDirection = 7;
    std::uint32_t count = kLastDirection * type.rotate + 1;
    for (const FigureAnim& a : type.anims) {
        if (a.defined) {
            const std::uint32_t frames = std::max(a.count, 1U) - 1;
            count = std::max(count, a.offset + a.add * frames + kLastDirection * a.rotate + 1);
        }
    }
    return count;
}

void apply_figure_images_file(std::vector<FigureLook>& looks, std::string_view package, std::string_view utf8,
                              std::span<const PackageImage> images, const FigureTable& figures,
                              std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kFigureImagesFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        const std::optional<OriginalName> numbers = figures_of(t.name);
        if (!numbers) {
            warnings.push_back(at + "[" + t.name + "]: names no figure of the original (anno1602:<name or number>)");
            continue;
        }
        const PackageImage* image = nullptr;
        std::optional<bool> shadow;
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
            } else if (e.key == "shadow") {
                if (e.value.boolean() == nullptr) {
                    warnings.push_back(field + "is true or false");
                    ok = false;
                } else {
                    shadow = *e.value.boolean();
                }
            } else {
                warnings.push_back(field + "is no field of a figure's image and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (image == nullptr) {
            warnings.push_back(at + "[" + t.name + "] needs an 'image'");
            continue;
        }
        // Every figure the table stands for, checked before any of them changes.
        std::vector<FigureLook> adding;
        for (std::uint32_t i = 0; i < numbers->count && ok; ++i) {
            const FigureType* type = figures.get(numbers->first + i);
            if (type == nullptr) {
                warnings.push_back(at + "[" + t.name + "]: figuren.cod has no figure " +
                                   std::to_string(numbers->first + i));
                ok = false;
            } else if (const std::uint32_t needed = figure_sprite_count(*type); needed > image->count) {
                warnings.push_back(at + "[" + t.name + "]: its image " + image->id + " holds " +
                                   std::to_string(image->count) + " sprites, the figure draws " +
                                   std::to_string(needed));
                ok = false;
            } else {
                adding.push_back(FigureLook{type->number, image->first, shadow.value_or(true)});
            }
        }
        if (!ok) {
            continue;
        }
        for (const FigureLook& look : adding) {
            std::erase_if(looks, [&](const FigureLook& l) { return l.figure == look.figure; });
            looks.push_back(look);
        }
    }
}

void apply_figure_looks(FigureTable& table, const FigureTable& original, std::span<const FigureLook> looks) {
    const std::span<const FigureType> from = original.types();
    std::span<FigureType> types = table.changeable_types();
    for (std::size_t i = 0; i < types.size() && i < from.size(); ++i) {
        types[i].sheet = from[i].sheet;
        types[i].gfx = from[i].gfx;
        types[i].shadow = from[i].shadow;
    }
    for (const FigureLook& look : looks) {
        if (FigureType* t = table.get(look.figure)) {
            t->sheet = FigureSheet::Stadtfld;
            t->gfx = look.gfx;
            if (!look.shadow) {
                t->shadow = 0;
            }
        }
    }
}

}  // namespace o1602::assets
