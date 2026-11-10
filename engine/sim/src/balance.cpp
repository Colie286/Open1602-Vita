// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/balance.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <span>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/assets/original_names.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/embedded_file.hpp"
#include "o1602/sim/rules.hpp"

namespace o1602::sim {

// data/balance/*.toml, compiled in by o1602_embed_files (engine/sim/CMakeLists.txt).
namespace presets {
extern const core::EmbeddedFile kEmbeddedFiles[];
extern const std::size_t kEmbeddedFileCount;
}  // namespace presets

namespace {

/// The package whose buildings a table ["<package>:<Id>"] may change: the original's.
constexpr std::string_view kBasePackage = "anno1602";

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

bool before(const BuildingValue& a, const BuildingValue& b) noexcept {
    return a.id != b.id ? a.id < b.id : a.field < b.field;
}

/// The Id of a table name "anno1602:<Id>" (decimal or 0x hex).
std::optional<std::uint32_t> building_id(std::string_view number) {
    int base = 10;
    if (number.size() > 2 && number[0] == '0' && (number[1] == 'x' || number[1] == 'X')) {
        number.remove_prefix(2);
        base = 16;
    }
    std::uint32_t id = 0;
    const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), id, base);
    if (error != std::errc{} || end != number.data() + number.size() || number.empty()) {
        return std::nullopt;
    }
    return id;
}

bool before_figure(const FigureValue& a, const FigureValue& b) noexcept {
    return a.number != b.number ? a.number < b.number : a.field < b.field;
}

/// The numbers a table name "anno1602:<Id or name>" stands for: one Id, or what `names` gives the
/// name (a figure in all four colours).
std::optional<assets::OriginalName> table_numbers(std::string_view after, std::span<const assets::OriginalName> names) {
    if (const auto id = building_id(after)) {
        return assets::OriginalName{after, *id, 1};
    }
    if (const assets::OriginalName* named = assets::find_original_name(names, after)) {
        return *named;
    }
    return std::nullopt;
}

/// One table of a file of values, naming an entry of the original ["anno1602:<Id or name>"] or a
/// building a package added (`own`): each entry's field looked up in `fields` and handed to `set`
/// with each number the table stands for, if it lies within the field's limits.
template <typename Info, std::size_t N, typename Set>
void apply_value_table(const core::DataTable& table, const std::string& where, std::string_view what,
                       std::span<const assets::OriginalName> names, std::span<const assets::OwnBuilding> own,
                       const std::array<Info, N>& fields, std::vector<std::string>& warnings, const Set& set) {
    const std::size_t colon = table.name.find(':');
    const std::string_view package = std::string_view(table.name).substr(0, colon);
    const std::string_view after =
        colon == std::string::npos ? std::string_view() : std::string_view(table.name).substr(colon + 1);
    std::optional<assets::OriginalName> numbers;
    if (colon != std::string::npos && package == kBasePackage) {
        numbers = table_numbers(after, names);
        if (!numbers) {
            warnings.push_back(line_of(where, table.line) + "[" + table.name + "]: '" + std::string(after) +
                               "' is no number and no name of the original's " + std::string(what));
            return;
        }
    } else if (const assets::OwnBuilding* added = assets::find_own_building(own, table.name); added != nullptr) {
        numbers = assets::OriginalName{table.name, added->number, 1};
    } else {
        warnings.push_back(line_of(where, table.line) + "[" + table.name + "]: names none of the original's " +
                           std::string(what) + " (anno1602:<number or name>) and none a package added");
        return;
    }
    for (const core::DataEntry& e : table.entries) {
        const std::string at = line_of(where, e.value.line);
        const auto field = std::find_if(fields.begin(), fields.end(), [&](const Info& f) { return f.key == e.key; });
        const std::int64_t* n = e.value.integer();
        if (field == fields.end()) {
            warnings.push_back(at + "unknown field '" + e.key + "'");
        } else if (n == nullptr || *n < field->min || *n > field->max) {
            warnings.push_back(at + "'" + e.key + "' must be a number from " + std::to_string(field->min) + " to " +
                               std::to_string(field->max));
        } else {
            for (std::uint32_t i = 0; i < numbers->count; ++i) {
                set(numbers->first + i, field->field, static_cast<std::uint32_t>(*n));
            }
        }
    }
}

void apply_building_table(Balance& balance, const core::DataTable& table, const std::string& where,
                          std::span<const assets::OwnBuilding> own, std::vector<std::string>& warnings) {
    apply_value_table(table, where, "buildings", assets::original_building_names(), own, kBuildingFields, warnings,
                      [&](std::uint32_t id, BuildingField field, std::uint32_t value) {
                          set_building_value(balance, BuildingValue{id, field, value});
                      });
}

/// A file whose every table is one of `apply_table`'s.
template <typename ApplyTable>
void apply_table_file(std::string_view package, std::string_view file, std::string_view utf8,
                      std::vector<std::string>& warnings, const ApplyTable& apply_table) {
    const std::string where = std::string(package) + "/" + std::string(file);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& table : *tables) {
        if (table.name.empty()) {
            warnings.push_back(line_of(where, table.entries.front().value.line) +
                               "fields before the first [table] belong to none and are passed over");
            continue;
        }
        apply_table(table, where);
    }
}

const BuildingFieldInfo* field_info(BuildingField field) noexcept {
    const auto i = static_cast<std::size_t>(field);
    return i < kBuildingFields.size() ? &kBuildingFields[i] : nullptr;
}

const FigureFieldInfo* field_info(FigureField field) noexcept {
    const auto i = static_cast<std::size_t>(field);
    return i < kFigureFields.size() ? &kFigureFields[i] : nullptr;
}

}  // namespace

bool valid(const Balance& balance) noexcept {
    const bool numbers = std::all_of(kBalanceFields.begin(), kBalanceFields.end(), [&](const BalanceField& f) {
        const std::uint32_t v = balance.*f.value;
        return v >= f.min && v <= f.max;
    });
    const bool buildings = std::all_of(balance.buildings.begin(), balance.buildings.end(), [](const BuildingValue& v) {
        const BuildingFieldInfo* f = field_info(v.field);
        return f != nullptr && v.value >= f->min && v.value <= f->max;
    });
    const bool ordered = std::adjacent_find(balance.buildings.begin(), balance.buildings.end(),
                                            [](const BuildingValue& a, const BuildingValue& b) {
                                                return !before(a, b);
                                            }) == balance.buildings.end();
    const bool figures = std::all_of(balance.figures.begin(), balance.figures.end(), [](const FigureValue& v) {
        const FigureFieldInfo* f = field_info(v.field);
        return f != nullptr && v.value >= f->min && v.value <= f->max;
    });
    const bool figures_ordered = std::adjacent_find(balance.figures.begin(), balance.figures.end(),
                                                    [](const FigureValue& a, const FigureValue& b) {
                                                        return !before_figure(a, b);
                                                    }) == balance.figures.end();
    return numbers && buildings && ordered && figures && figures_ordered;
}

bool original_numbers(const Balance& balance) noexcept {
    const Balance original;
    return std::all_of(kBalanceFields.begin(), kBalanceFields.end(),
                       [&](const BalanceField& f) { return balance.*f.value == original.*f.value; });
}

void set_building_value(Balance& balance, BuildingValue value) {
    auto& values = balance.buildings;
    const auto at = std::lower_bound(values.begin(), values.end(), value, before);
    if (at != values.end() && at->id == value.id && at->field == value.field) {
        at->value = value.value;
    } else {
        values.insert(at, value);
    }
}

void add_building_values(Balance& balance, const std::vector<BuildingValue>& under) {
    for (const BuildingValue& v : under) {
        const auto at = std::lower_bound(balance.buildings.begin(), balance.buildings.end(), v, before);
        if (at == balance.buildings.end() || at->id != v.id || at->field != v.field) {
            balance.buildings.insert(at, v);
        }
    }
}

void set_figure_value(Balance& balance, FigureValue value) {
    auto& values = balance.figures;
    const auto at = std::lower_bound(values.begin(), values.end(), value, before_figure);
    if (at != values.end() && at->number == value.number && at->field == value.field) {
        at->value = value.value;
    } else {
        values.insert(at, value);
    }
}

void apply_buildings_file(Balance& balance, std::string_view package, std::string_view utf8,
                          std::vector<std::string>& warnings, std::span<const assets::OwnBuilding> own) {
    apply_table_file(package, kBuildingsFile, utf8, warnings,
                     [&](const core::DataTable& table, const std::string& where) {
                         apply_building_table(balance, table, where, own, warnings);
                     });
}

void apply_figures_file(Balance& balance, std::string_view package, std::string_view utf8,
                        std::vector<std::string>& warnings) {
    apply_table_file(package, kFiguresFile, utf8, warnings,
                     [&](const core::DataTable& table, const std::string& where) {
                         apply_value_table(table, where, "figures", assets::original_figure_names(), {}, kFigureFields,
                                           warnings, [&](std::uint32_t number, FigureField field, std::uint32_t value) {
                                               set_figure_value(balance, FigureValue{number, field, value});
                                           });
                     });
}

void apply_balance_file(Balance& balance, std::string_view package, std::string_view utf8,
                        std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kBalanceFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& table : *tables) {
        if (table.name.empty()) {
            warnings.push_back(line_of(where, table.entries.front().value.line) +
                               "fields before the first [table] belong to none and are passed over");
            continue;
        }
        if (table.name.find(':') != std::string::npos) {
            apply_building_table(balance, table, where, {}, warnings);
            continue;
        }
        if (std::none_of(kBalanceFields.begin(), kBalanceFields.end(),
                         [&](const BalanceField& f) { return f.table == table.name; })) {
            warnings.push_back(line_of(where, table.line) + "there is no table [" + table.name + "]");
            continue;
        }
        for (const core::DataEntry& e : table.entries) {
            const std::string at = line_of(where, e.value.line);
            const auto field = std::find_if(kBalanceFields.begin(), kBalanceFields.end(), [&](const BalanceField& f) {
                return f.table == table.name && f.key == e.key;
            });
            const std::int64_t* n = e.value.integer();
            if (field == kBalanceFields.end()) {
                warnings.push_back(at + "unknown field '" + e.key + "'");
            } else if (n == nullptr || *n < field->min || *n > field->max) {
                warnings.push_back(at + "'" + e.key + "' must be a number from " + std::to_string(field->min) + " to " +
                                   std::to_string(field->max));
            } else {
                balance.*field->value = static_cast<std::uint32_t>(*n);
            }
        }
    }
}

void drop_unknown_buildings(Balance& balance, const assets::BuildingTable& buildings,
                            std::vector<std::string>& warnings) {
    std::erase_if(balance.buildings, [&](const BuildingValue& v) {
        if (buildings.by_id(v.id) != nullptr) {
            return false;
        }
        warnings.push_back(std::string(kBalanceFile) + ": haeuser.cod has no building anno1602:" +
                           std::to_string(v.id) + ", its values are passed over");
        return true;
    });
}

void apply_resident_classes(assets::BuildingTable& table, const assets::BuildingTable& original,
                            std::span<const assets::ResidentClass> classes) {
    table.set_resident_classes(classes.empty() ? original.resident_classes()
                                               : std::vector<assets::ResidentClass>(classes.begin(), classes.end()));
}

void apply_infra_levels(assets::BuildingTable& table, const assets::BuildingTable& original,
                        std::span<const assets::InfraLevel> levels) {
    table.set_infra_levels(levels.empty() ? original.infra_levels()
                                          : std::vector<assets::InfraLevel>(levels.begin(), levels.end()));
}

void apply_building_values(assets::BuildingTable& table, const assets::BuildingTable& original,
                           const Balance& balance) {
    const std::vector<assets::BuildingType>& from = original.types();
    std::span<assets::BuildingType> types = table.changeable_types();
    for (std::size_t i = 0; i < types.size() && i < from.size(); ++i) {
        assets::BuildingType& t = types[i];
        const assets::BuildingType& o = from[i];
        t.produce_amount = o.produce_amount;
        t.raw_amount = o.raw_amount;
        t.raw2_amount = o.raw2_amount;
        t.interval = o.interval;
        t.max_stock = o.max_stock;
        t.running_cost = o.running_cost;
        t.idle_cost = o.idle_cost;
        t.build_cost.money = o.build_cost.money;
        t.build_cost.tools = o.build_cost.tools;
        t.build_cost.wood = o.build_cost.wood;
        t.build_cost.bricks = o.build_cost.bricks;
        t.build_cost.cannons = o.build_cost.cannons;
        t.radius = o.radius;
        t.workers = o.workers;
        t.max_idle_cycles = o.max_idle_cycles;
        t.max_health = o.max_health;
    }
    constexpr std::uint32_t kTon = assets::kStockUnitsPerTon;
    constexpr std::uint32_t kHealthUnits = 32;  // hit points are kept in 1/32 (Maxenergy * 32)
    for (const BuildingValue& v : balance.buildings) {
        assets::BuildingType* t = table.by_id(v.id);
        if (t == nullptr) {
            continue;
        }
        switch (v.field) {
            case BuildingField::Produce:
                t->produce_amount = v.value * kTon;
                break;
            case BuildingField::Raw:
                t->raw_amount = v.value * kTon;
                break;
            case BuildingField::Raw2:
                t->raw2_amount = v.value * kTon;
                break;
            case BuildingField::Interval:
                t->interval = v.value;
                break;
            case BuildingField::MaxStock:
                t->max_stock = v.value * kTon;
                break;
            case BuildingField::RunningCost:
                t->running_cost = v.value;
                break;
            case BuildingField::IdleCost:
                t->idle_cost = v.value;
                break;
            case BuildingField::BuildMoney:
                t->build_cost.money = static_cast<std::int32_t>(v.value);
                break;
            case BuildingField::BuildTools:
                t->build_cost.tools = v.value * kTon;
                break;
            case BuildingField::BuildWood:
                t->build_cost.wood = v.value * kTon;
                break;
            case BuildingField::BuildBricks:
                t->build_cost.bricks = v.value * kTon;
                break;
            case BuildingField::BuildCannons:
                t->build_cost.cannons = v.value * kTon;
                break;
            case BuildingField::Radius:
                t->radius = static_cast<std::uint8_t>(v.value);
                break;
            case BuildingField::Workers:
                t->workers = static_cast<std::uint16_t>(v.value);
                break;
            case BuildingField::MaxIdle:
                t->max_idle_cycles = v.value;
                break;
            case BuildingField::Health:
                t->max_health = v.value * kHealthUnits;
                break;
        }
    }
}

void drop_unknown_figures(Balance& balance, const assets::FigureTable& figures, std::vector<std::string>& warnings) {
    std::erase_if(balance.figures, [&](const FigureValue& v) {
        if (figures.get(v.number) != nullptr) {
            return false;
        }
        warnings.push_back(std::string(kFiguresFile) + ": figuren.cod has no figure anno1602:" +
                           std::to_string(v.number) + ", its values are passed over");
        return true;
    });
}

void apply_figure_values(assets::FigureTable& table, const assets::FigureTable& original, const Balance& balance) {
    const std::span<const assets::FigureType> from = original.types();
    std::span<assets::FigureType> types = table.changeable_types();
    for (std::size_t i = 0; i < types.size() && i < from.size(); ++i) {
        assets::FigureType& t = types[i];
        const assets::FigureType& o = from[i];
        t.speed = o.speed;
        t.max_carry = o.max_carry;
        t.work_time_ms = o.work_time_ms;
        t.ship.max_health = o.ship.max_health;
        t.ship.hit = o.ship.hit;
        t.ship.shot_radius = o.ship.shot_radius;
        t.ship.shot_ms = o.ship.shot_ms;
        t.ship.max_cannons = o.ship.max_cannons;
        t.ship.price = o.ship.price;
        t.ship.build_amount = o.ship.build_amount;
        t.combat.max_health = o.combat.max_health;
        t.combat.hit = o.combat.hit;
        t.combat.range8 = o.combat.range8;
        t.combat.hit_delay_ms = o.combat.hit_delay_ms;
        t.combat.weapon_amount = o.combat.weapon_amount;
    }
    constexpr std::uint32_t kTon = assets::kStockUnitsPerTon;
    constexpr std::uint32_t kHealthUnits = 32;  // Maxenergy and Hitpoint are kept in 1/32
    for (const FigureValue& v : balance.figures) {
        assets::FigureType* t = table.get(v.number);
        if (t == nullptr) {
            continue;
        }
        // figuren.cod gives ships and soldiers the same fields; the loader copies them into both.
        switch (v.field) {
            case FigureField::Speed:
                t->speed = v.value;
                break;
            case FigureField::MaxCarry:
                t->max_carry = v.value * kTon;
                break;
            case FigureField::WorkTime:
                t->work_time_ms = v.value;
                break;
            case FigureField::Health:
                t->ship.max_health = v.value * kHealthUnits;
                t->combat.max_health = t->ship.max_health;
                break;
            case FigureField::Hit:
                t->ship.hit = v.value * kHealthUnits;
                t->combat.hit = t->ship.hit;
                break;
            case FigureField::Range:
                t->ship.shot_radius = v.value;
                t->combat.range8 = v.value * 8;
                break;
            case FigureField::ShotTime:
                t->ship.shot_ms = v.value;
                t->combat.hit_delay_ms = v.value;
                break;
            case FigureField::Cannons:
                t->ship.max_cannons = static_cast<std::uint8_t>(v.value);
                break;
            case FigureField::Price:
                t->ship.price = v.value;
                break;
            case FigureField::Material:
                t->ship.build_amount = v.value * kTon;
                t->combat.weapon_amount = t->ship.build_amount;
                break;
        }
    }
}

std::vector<BuildingValue> rule_building_values(const Rules& rules) {
    Balance balance;
    std::vector<std::string> warnings;
    const auto add = [&](std::string_view name) {
        for (std::size_t i = 0; i < presets::kEmbeddedFileCount; ++i) {
            if (presets::kEmbeddedFiles[i].name == name) {
                apply_balance_file(balance, "open1602", presets::kEmbeddedFiles[i].text(), warnings);
            }
        }
    };
    if (rules.revised_bread) {
        add("revised_bread");
    }
    return balance.buildings;
}

}  // namespace o1602::sim
