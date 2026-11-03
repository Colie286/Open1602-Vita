// SPDX-License-Identifier: GPL-3.0-or-later
#include "mod_rules.hpp"

#include <string_view>

#include "o1602/assets/unlock_levels.hpp"
#include "o1602/core/file.hpp"
#include "o1602/sim/climates.hpp"
#include "o1602/sim/computer_orders.hpp"
#include "o1602/ui/package_icons.hpp"

namespace o1602::app {

std::vector<assets::PackageIcon> load_package_icons(std::span<const assets::ModPackage> packages,
                                                    std::vector<std::string>& problems) {
    std::vector<assets::PackageIcon> icons = ui::builtin_icons();  // Open1602's own first
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kIconsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_icons_file(icons, package.id, package.folder, text, problems);
        }
    }
    return icons;
}

std::vector<sim::PackageLandscape> load_package_landscapes(std::span<const assets::ModPackage> packages,
                                                           std::vector<std::string>& problems) {
    std::vector<sim::PackageLandscape> presets;
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / sim::kLandscapesFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_landscapes_file(presets, package.id, text, problems);
        }
    }
    return presets;
}

ModRules load_mod_rules(std::span<const assets::ModPackage> packages, assets::BuildingTable& buildings,
                        assets::FigureTable& figures, std::vector<std::string>& problems) {
    ModRules rules;
    // Goods first (with the icons they may show), then the classes and buildings that use them,
    // then the values that may name both. The icons' warnings come when the game takes them
    // (MapViewer::use_game_packages).
    std::vector<std::string> ignored;
    const std::vector<assets::PackageIcon> icons = load_package_icons(packages, ignored);
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kGoodsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_goods_file(rules.goods, package.id, text, problems, icons);
        }
    }
    // Then the classes that need them, then the buildings, among them houses of those classes.
    std::vector<assets::ResidentClass> classes = buildings.resident_classes();
    bool classes_changed = false;
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kClassesFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            classes_changed |= assets::apply_classes_file(classes, package.id, text, problems, rules.goods);
        }
    }
    if (classes_changed) {
        rules.classes = classes;
    }
    // Then the levels that count inhabitants of those classes, which the buildings may need.
    std::vector<assets::InfraLevel> levels = buildings.infra_levels();
    bool levels_changed = false;
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kLevelsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            levels_changed |= assets::apply_levels_file(levels, package.id, text, problems, classes);
        }
    }
    if (levels_changed) {
        rules.levels = levels;
    }
    // Then the images the buildings draw.
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kGraphicsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_graphics_file(rules.images, package.id, package.folder, text, problems);
        }
        if (const auto bytes = core::read_file(package.folder / assets::kFigureImagesFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_figure_images_file(rules.looks, package.id, text, rules.images, figures, problems);
        }
        if (const auto bytes = core::read_file(package.folder / audio::kMusicFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            audio::apply_music_file(rules.music, package.id, package.folder, text, problems);
        }
        if (const auto bytes = core::read_file(package.folder / assets::kSoundsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_sounds_file(rules.sounds, package.id, package.folder, text, problems);
        }
    }
    // Then the figures, with the images they draw, which the buildings may name as their workers.
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kOwnFiguresFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_own_figures_file(figures, rules.own_figures, package.id, text, problems, rules.images,
                                           rules.sounds, &rules.own_units, &rules.own_ships);
        }
    }
    for (const assets::OwnUnit& unit : rules.own_units) {
        if (!unit.icon.empty() && assets::find_package_icon(icons, unit.icon) == nullptr) {
            problems.push_back(unit.id + ": its icon " + unit.icon +
                               " is no icon of a package (icons.toml); the castle shows the unit it copies");
        }
    }
    for (const assets::OwnShip& ship : rules.own_ships) {
        if (!ship.icon.empty() && assets::find_package_icon(icons, ship.icon) == nullptr) {
            problems.push_back(ship.id + ": its icon " + ship.icon +
                               " is no icon of a package (icons.toml); the shipyard shows the ship it copies");
        }
    }
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / assets::kOwnBuildingsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            assets::apply_own_buildings_file(buildings, rules.own, package.id, text, problems, rules.goods, classes,
                                             levels, rules.images, &figures, rules.own_figures, rules.sounds);
        }
    }
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / sim::kFactionFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_faction_file(rules.factions, package.id, text, problems, &rules.faction_replacements);
        }
        if (const auto bytes = core::read_file(package.folder / sim::kBalanceFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_balance_file(rules.balance, package.id, text, problems);
        }
        if (const auto bytes = core::read_file(package.folder / sim::kBuildingsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_buildings_file(rules.balance, package.id, text, problems, rules.own);
        }
        if (const auto bytes = core::read_file(package.folder / sim::kFiguresFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_figures_file(rules.balance, package.id, text, problems, rules.own_figures);
        }
        if (const auto bytes = core::read_file(package.folder / sim::kComputersFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_computers_file(rules.balance.computer_orders, package.id, text, problems, rules.goods,
                                      &rules.balance.computer_producers, rules.own);
        }
        if (const auto bytes = core::read_file(package.folder / sim::kSlotsFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_slots_file(rules.balance.computer_slots, package.id, text, problems, rules.goods);
        }
    }
    // Climates after the buildings, whose trees their forest may name.
    for (const assets::ModPackage& package : packages) {
        if (const auto bytes = core::read_file(package.folder / sim::kClimatesFile)) {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            sim::apply_climates_file(rules.balance.climates, package.id, text, problems, &buildings, rules.own);
        }
    }
    sim::drop_unknown_buildings(rules.balance, buildings, problems);
    sim::drop_unknown_figures(rules.balance, figures, problems);
    return rules;
}

}  // namespace o1602::app
