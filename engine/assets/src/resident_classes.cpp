// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/resident_classes.hpp"

#include <algorithm>
#include <charconv>

#include "o1602/assets/original_names.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/text.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A name a package may give its class: lower case letters, digits and '_', as the original's.
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

/// Limits of the fields, in the units of classes.toml.
constexpr std::int64_t kMaxPerHouse = 255;
constexpr std::int64_t kMaxTax = 10000;    // Steuer * 100
constexpr std::int64_t kMaxNeed = 100000;  // kg per 100 inhabitants and minute
constexpr std::int64_t kMaxHiscore = 10000;

/// What `need` gives a class per inhabitant and 10 s, from kg per 100 inhabitants and minute: as
/// haeuser.cod's `Ware: W, t` (BuildingTable::from_cod, 0x00460750: trunc(t * 8192) / 600).
std::uint32_t need_amount(std::int64_t kg) noexcept {
    return static_cast<std::uint32_t>(kg * 8192 / 1000 / 600);
}

/// A field of a table, read before anything changes.
struct Change {
    std::optional<std::uint8_t> like;
    std::optional<std::int64_t> max_per_house;
    std::optional<std::int64_t> tax;
    std::optional<std::int64_t> share;
    std::optional<std::int64_t> hiscore;
    std::vector<std::pair<std::uint8_t, std::int64_t>> needs;  ///< ware, kg
    LocalizedNames names;
};

void apply(ResidentClass& c, const Change& change) {
    if (change.max_per_house) {
        c.max_per_house = static_cast<std::uint32_t>(*change.max_per_house);
    }
    if (change.tax) {
        c.tax = static_cast<std::uint32_t>(*change.tax * 16 / 300);  // trunc(Steuer * 16 / 3)
    }
    if (change.share) {
        c.share = static_cast<std::uint32_t>(*change.share * 128 / 100);
    }
    if (change.hiscore) {
        c.hiscore = static_cast<std::uint32_t>(*change.hiscore);
    }
    // No structured bindings in the lambdas: Apple's clang 15 cannot capture them.
    for (const auto& need : change.needs) {
        const std::uint8_t ware = need.first;
        const auto it =
            std::find_if(c.needs.begin(), c.needs.end(), [&](const ResidentClass::Need& n) { return n.ware == ware; });
        const std::uint32_t amount = need_amount(need.second);
        if (amount == 0) {
            if (it != c.needs.end()) {
                c.needs.erase(it);
            }
        } else if (it != c.needs.end()) {
            it->amount = amount;
        } else {
            c.needs.push_back(ResidentClass::Need{ware, amount});
        }
    }
    for (const auto& named : change.names) {
        const auto it =
            std::find_if(c.names.begin(), c.names.end(), [&](const auto& n) { return n.first == named.first; });
        if (it != c.names.end()) {
            it->second = named.second;
        } else {
            c.names.push_back(named);
        }
    }
}

}  // namespace

std::optional<std::uint8_t> class_of(std::span<const ResidentClass> classes, std::string_view id) {
    const std::size_t colon = id.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    if (id.substr(0, colon) == "anno1602") {
        const std::string_view after = id.substr(colon + 1);
        std::uint32_t number = 0;
        if (const OriginalName* n = find_original_name(original_class_names(), after)) {
            number = n->first;
        } else if (const auto [end, error] = std::from_chars(after.data(), after.data() + after.size(), number);
                   error != std::errc{} || end != after.data() + after.size()) {
            return std::nullopt;
        }
        // Only the classes of haeuser.cod: a package's classes have ids of their own.
        if (number < classes.size() && classes[number].id.empty()) {
            return static_cast<std::uint8_t>(number);
        }
        return std::nullopt;
    }
    const auto it = std::find_if(classes.begin(), classes.end(), [&](const ResidentClass& c) { return c.id == id; });
    if (it == classes.end()) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(it - classes.begin());
}

std::string class_name(const ResidentClass& c, std::string_view language) {
    return c.names.empty() ? std::string() : name_in(c.names, language, c.id);
}

bool apply_classes_file(std::vector<ResidentClass>& classes, std::string_view package, std::string_view utf8,
                        std::vector<std::string>& warnings, const Goods& goods) {
    const std::string where = std::string(package) + "/" + std::string(kClassesFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return false;
    }
    bool changed = false;
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        // A class there is, or a new one of this package.
        std::optional<std::uint8_t> target;
        std::string id;
        if (t.name.find(':') != std::string::npos) {
            target = class_of(classes, t.name);
            if (!target) {
                warnings.push_back(at + "[" + t.name +
                                   "]: names none of the original's classes (anno1602:<name or number>) and none a "
                                   "package added");
                continue;
            }
        } else if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name + "]: a class's name holds only lower case letters, digits and '_'");
            continue;
        } else {
            id = std::string(package) + ":" + t.name;
            if (class_of(classes, id)) {
                warnings.push_back(at + id + " is declared twice; the second is passed over");
                continue;
            }
            if (classes.size() >= kMaxResidentClasses || classes.empty()) {
                warnings.push_back(at + id + ": a game has at most " + std::to_string(kMaxResidentClasses) +
                                   " classes of inhabitants");
                continue;
            }
        }

        Change change;
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            const auto number = [&](std::int64_t min, std::int64_t max, std::optional<std::int64_t>& out) {
                const std::int64_t* n = e.value.integer();
                if (n == nullptr || *n < min || *n > max) {
                    warnings.push_back(field + "is a number from " + std::to_string(min) + " to " +
                                       std::to_string(max));
                    ok = false;
                } else {
                    out = *n;
                }
            };
            if (e.key == "like") {
                const std::string* s = e.value.string();
                const auto c = s != nullptr ? class_of(classes, *s) : std::nullopt;
                if (target) {
                    warnings.push_back(field + "is for a new class only and is passed over");
                } else if (!c) {
                    warnings.push_back(field + "names no class (anno1602:<name or number> or <package>:<name>)");
                    ok = false;
                } else {
                    change.like = c;
                }
            } else if (e.key == "max_per_house") {
                number(1, kMaxPerHouse, change.max_per_house);
            } else if (e.key == "tax") {
                number(0, kMaxTax, change.tax);
            } else if (e.key == "share") {
                number(0, 100, change.share);
            } else if (e.key == "hiscore") {
                number(0, kMaxHiscore, change.hiscore);
            } else if (e.key.starts_with("need.")) {
                const auto w = ware_of(goods, std::string_view(e.key).substr(5));
                std::optional<std::int64_t> kg;
                if (!w) {
                    warnings.push_back(field + "names no good (need.\"anno1602:<name>\" or need.\"<package>:<name>\")");
                    ok = false;
                } else if (*w == ware::kFood) {
                    warnings.push_back(field + "names food, which every class eats as haeuser.cod says");
                    ok = false;
                } else {
                    number(0, kMaxNeed, kg);
                    if (kg) {
                        change.needs.emplace_back(*w, *kg);
                    }
                }
            } else if (const std::string_view language = name_key_language(e.key); !language.empty()) {
                if (e.value.string() == nullptr || e.value.string()->empty()) {
                    warnings.push_back(field + "is a text");
                    ok = false;
                } else {
                    change.names.emplace_back(language, core::utf8_to_cp1252(*e.value.string()));
                }
            } else {
                warnings.push_back(field + "is no field of a class and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (target) {
            apply(classes[*target], change);
        } else {
            if (change.names.empty()) {
                warnings.push_back(at + id + " needs a name ('name.en')");
                continue;
            }
            // A copy of the class it is like, by default the last: its values, needs and looks.
            ResidentClass c = classes[change.like.value_or(static_cast<std::uint8_t>(classes.size() - 1))];
            c.id = id;
            c.names.clear();
            apply(c, change);
            classes.push_back(std::move(c));
        }
        changed = true;
    }
    return changed;
}

}  // namespace o1602::assets
