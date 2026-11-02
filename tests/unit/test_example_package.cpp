// SPDX-License-Identifier: GPL-3.0-or-later
// The example package data/mods/example (docs/modding/README.md): it has every file a package can
// have, and each loads without a warning. Without the original's data the files that need no tables
// of the original are read; with them, also the buildings, figures, classes, levels and the build
// menu, as the game and `o1602-tool check-mod` read them.

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "message_table.hpp"
#include "mod_rules.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/package_scenarios.hpp"
#include "o1602/script/game_scripts.hpp"
#include "o1602/sim/package_islands.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/package_icons.hpp"

using namespace o1602;
namespace fs = std::filesystem;

namespace {

const fs::path kMods = O1602_EXAMPLE_MODS;

/// Fails the test with every warning, so that its output names them.
void require_none(const std::vector<std::string>& problems) {
    for (const std::string& p : problems) {
        std::fprintf(stderr, "problem: %s\n", p.c_str());
    }
    CHECK(problems.empty());
}

/// Sets the icons of the interface for one test, as the game sets them before the messages and the
/// build menu, which may name them, and takes them away again.
struct IconsInUse {
    explicit IconsInUse(std::vector<assets::PackageIcon> icons) { ui::set_package_icons(std::move(icons)); }
    ~IconsInUse() { ui::set_package_icons({}); }
    IconsInUse(const IconsInUse&) = delete;
    IconsInUse& operator=(const IconsInUse&) = delete;
};

}  // namespace

TEST_CASE("the example package has every file a package can have") {
    for (const char* name :
         {"mod.toml",       "messages.toml",  "build_menu.toml", "factions.toml",      "balance.toml",
          "buildings.toml", "figures.toml",   "computers.toml",  "own_buildings.toml", "own_figures.toml",
          "goods.toml",     "classes.toml",   "levels.toml",     "script.lua",         "scenarios.toml",
          "campaigns.toml", "graphics.toml",  "icons.toml",      "figure_images.toml", "music.toml",
          "sounds.toml",    "landscapes.toml"}) {
        if (!fs::is_regular_file(kMods / "example" / name)) {
            std::fprintf(stderr, "missing: %s\n", name);
            CHECK(false);
        }
    }
}

TEST_CASE("the example package loads without a warning, as far as it needs no original data") {
    std::vector<std::string> problems;
    const std::vector<assets::ModPackage> packages = assets::load_mod_packages(kMods, problems);
    REQUIRE(packages.size() == 1);
    CHECK(packages[0].id == "example");
    CHECK(packages[0].version == "1.0");

    const IconsInUse icons(app::load_package_icons(packages, problems));
    CHECK(ui::package_icon_sprite("example:pepper").has_value());
    const std::vector<sim::PackageLandscape> landscapes = app::load_package_landscapes(packages, problems);
    CHECK(landscapes.size() == 1);
    const std::vector<sim::PackageIsland> islands = sim::load_package_islands(packages, problems);
    REQUIRE(islands.size() == 1);
    CHECK(islands[0].id == "example:lagoon");
    app::MessageTable messages;
    messages.load_mods(packages, problems);

    const std::vector<assets::PackageScenario> scenarios = assets::load_package_scenarios(packages, problems);
    CHECK(scenarios.size() == 1);
    CHECK(assets::load_package_campaigns(packages, scenarios, problems).size() == 1);

    std::vector<script::PackageScript> scripts = script::read_package_scripts(packages, problems);
    for (const assets::PackageScenario& scenario : scenarios) {
        CHECK(fs::is_regular_file(scenario.file));
        if (auto own = script::read_scenario_script(scenario, problems)) {
            scripts.push_back(std::move(*own));
        }
    }
    CHECK(scripts.size() == 2);
    const sim::World world;
    script::GameScripts host(std::move(scripts), world);
    for (const script::ScriptNote& note : host.take_notes()) {
        if (note.error) {
            problems.push_back(note.package + ": " + note.text);
        }
    }
    require_none(problems);
}

TEST_CASE("data: the example package loads without a warning") {
    auto dir = assets::GameDirectory::from_environment();
    if (!dir) {
        SKIP("ANNO1602_DIR not set");
    }
    auto buildings = assets::BuildingTable::load(*dir);
    auto figures = assets::FigureTable::load(*dir);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    std::vector<std::string> problems;
    const std::vector<assets::ModPackage> packages = assets::load_mod_packages(kMods, problems);
    REQUIRE(packages.size() == 1);
    const IconsInUse icons(app::load_package_icons(packages, problems));
    const app::ModRules rules = app::load_mod_rules(packages, *buildings, *figures, problems);
    CHECK(rules.own.size() == 2);
    CHECK(rules.goods.own_goods().size() == 1);
    (void)ui::load_build_menu_mods(ui::original_build_menu(), packages, *buildings, problems, rules.own);
    require_none(problems);
}
