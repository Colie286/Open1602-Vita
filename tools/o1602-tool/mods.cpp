// SPDX-License-Identifier: GPL-3.0-or-later
// o1602-tool check-mod: reads the packages of a mods folder as the game does and lists every
// problem, without starting the game (docs/modding/README.md).

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "message_table.hpp"
#include "mod_rules.hpp"
#include "tool.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/package_scenarios.hpp"
#include "o1602/script/game_scripts.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/package_icons.hpp"

namespace o1602::tool {

namespace fs = std::filesystem;

int cmd_check_mod(const Args& args) {
    if (args.positional.size() != 1) {
        return usage();
    }
    const fs::path mods(std::string(args.positional[0]));
    std::error_code ec;
    if (!fs::is_directory(mods, ec)) {
        return fail(
            core::make_error(core::ErrorCode::NotFound, std::string(args.positional[0]) + ": no folder of packages"));
    }
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        return fail(game.error());
    }
    auto buildings = assets::BuildingTable::load(*game);
    if (!buildings) {
        return fail(buildings.error());
    }
    auto figures = assets::FigureTable::load(*game);
    if (!figures) {
        return fail(figures.error());
    }
    // The packages, then their files in the order the game reads them.
    std::vector<std::string> problems;
    const std::vector<assets::ModPackage> packages = assets::load_mod_packages(mods, problems);
    for (const assets::ModPackage& p : packages) {
        std::printf("package %s\n", assets::package_label(p.content()).c_str());
    }
    // The icons first, as the game sets them: the messages and the build menu may name them.
    ui::set_package_icons(app::load_package_icons(packages, problems));
    (void)app::load_package_landscapes(packages, problems);
    const app::ModRules rules = app::load_mod_rules(packages, *buildings, *figures, problems);
    app::MessageTable messages;
    messages.load_mods(packages, problems);
    (void)ui::load_build_menu_mods(ui::original_build_menu(), packages, *buildings, problems, rules.own);
    const std::vector<assets::PackageScenario> scenarios = assets::load_package_scenarios(packages, problems);
    (void)assets::load_package_campaigns(packages, scenarios, problems);
    // The scripts' top level, run for an empty world: what fails there fails in every game.
    std::vector<script::PackageScript> scripts = script::read_package_scripts(packages, problems);
    for (const assets::PackageScenario& scenario : scenarios) {
        if (auto own = script::read_scenario_script(scenario, problems)) {
            scripts.push_back(std::move(*own));
        }
    }
    const sim::World world;
    script::GameScripts host(std::move(scripts), world);
    for (const script::ScriptNote& note : host.take_notes()) {
        if (note.error) {
            problems.push_back(note.package + ": " + note.text);
        }
    }
    for (const std::string& p : problems) {
        std::printf("problem: %s\n", p.c_str());
    }
    std::printf("%zu packages, %zu problems\n", packages.size(), problems.size());
    return problems.empty() ? 0 : 1;
}

}  // namespace o1602::tool
