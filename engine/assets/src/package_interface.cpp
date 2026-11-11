// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/package_interface.hpp"

#include <algorithm>
#include <charconv>

#include "o1602/core/data_file.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

}  // namespace

std::optional<InterfaceSprite> parse_interface_sprite(std::string_view name) noexcept {
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const auto sheet = std::find(kInterfaceSheets.begin(), kInterfaceSheets.end(), name.substr(0, colon));
    const std::string_view digits = name.substr(colon + 1);
    std::int32_t number = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (sheet == kInterfaceSheets.end() || digits.empty() || error != std::errc{} ||
        end != digits.data() + digits.size() || number < 0) {
        return std::nullopt;
    }
    return InterfaceSprite{static_cast<std::int32_t>(sheet - kInterfaceSheets.begin()), number, {}, std::nullopt};
}

void use_edition(std::vector<InterfaceSprite>& sprites, bool history_edition) {
    if (!history_edition) {
        return;
    }
    for (InterfaceSprite& s : sprites) {
        if (s.history) {
            s.sprite = *s.history;
        }
    }
}

const InterfaceSprite* find_interface_sprite(std::span<const InterfaceSprite> sprites, std::int32_t sheet,
                                             std::int32_t sprite) noexcept {
    const auto it = std::find_if(sprites.begin(), sprites.end(),
                                 [&](const InterfaceSprite& s) { return s.sheet == sheet && s.sprite == sprite; });
    return it == sprites.end() ? nullptr : &*it;
}

void apply_interface_file(std::vector<InterfaceSprite>& sprites, std::string_view package, std::string_view utf8,
                          std::span<const PackageIcon> icons, std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kInterfaceFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        std::optional<InterfaceSprite> sprite = parse_interface_sprite(t.name);
        if (!sprite) {
            warnings.push_back(at + "[" + t.name +
                               "]: names no sprite of the interface (\"tools:12\"; sheets tools, start, symbol, "
                               "bauhaus, bauship)");
            continue;
        }
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            if (e.key == "icon") {
                const std::string* s = e.value.string();
                if (s == nullptr || find_package_icon(icons, *s) == nullptr) {
                    warnings.push_back(field + "names no icon of a package (icons.toml, <package>:<name>)");
                    ok = false;
                } else {
                    sprite->icon = *s;
                }
            } else if (e.key == "history") {
                const std::int64_t* n = e.value.integer();
                if (n == nullptr || *n < 0 || *n > 0xFFFF) {
                    warnings.push_back(field + "is a sprite number from 0 to 65535");
                    ok = false;
                } else {
                    sprite->history = static_cast<std::int32_t>(*n);
                }
            } else {
                warnings.push_back(field + "is no field of an interface sprite and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (sprite->icon.empty()) {
            warnings.push_back(at + "[" + t.name + "] needs an 'icon'");
            continue;
        }
        std::erase_if(sprites,
                      [&](const InterfaceSprite& s) { return s.sheet == sprite->sheet && s.sprite == sprite->sprite; });
        sprites.push_back(std::move(*sprite));
    }
}

}  // namespace o1602::assets
