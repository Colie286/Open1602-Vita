// SPDX-License-Identifier: GPL-3.0-or-later
// o1602-tool check-mod: reads the packages of a mods folder as the game does and lists every
// problem, without starting the game (docs/modding/README.md).

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "message_table.hpp"
#include "mod_rules.hpp"
#include "tool.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/assets/package_scenarios.hpp"
#include "o1602/core/file.hpp"
#include "o1602/script/game_scripts.hpp"
#include "o1602/sim/package_islands.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/package_icons.hpp"

namespace o1602::tool {

namespace fs = std::filesystem;

namespace {

/// The size of an INSEL5 record.
constexpr std::size_t kInsel5Size = 116;

}  // namespace

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
    (void)sim::load_package_islands(packages, problems);
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

int cmd_export_island(const Args& args) {
    if (args.positional.size() != 3) {
        return usage();
    }
    const std::string number_text(args.positional[1]);
    char* end = nullptr;
    const unsigned long number = std::strtoul(number_text.c_str(), &end, 10);
    if (end == number_text.c_str() || *end != '\0') {
        return fail(core::make_error(core::ErrorCode::OutOfRange, "no island number: " + number_text));
    }
    auto file = assets::ChunkFile::load(fs::path(std::string(args.positional[0])));
    if (!file) {
        return fail(file.error());
    }
    auto islands = assets::load_islands(*file);
    if (!islands) {
        return fail(islands.error());
    }
    const auto it = std::find_if(islands->begin(), islands->end(),
                                 [&](const assets::Island& i) { return i.header.number == number; });
    if (it == islands->end()) {
        return fail(core::make_error(core::ErrorCode::NotFound, "no island " + number_text + " in the file"));
    }
    if (!it->header.stored_complete || it->layers.empty() || it->header.raw.size() != kInsel5Size) {
        return fail(core::make_error(core::ErrorCode::InvalidFormat,
                                     "island " + number_text +
                                         " keeps only its changes to a template of the original; only an island "
                                         "of its own shape with all its tiles (new-world --island-shapes own) "
                                         "can be an island of a package"));
    }
    // The island alone: at the top left, no owner's state, the ground layer with all its tiles.
    std::vector<std::uint8_t> header = it->header.raw;
    header[0] = 0;  // island number
    for (const std::size_t o : {std::size_t{4}, std::size_t{5}, std::size_t{6}, std::size_t{7}}) {
        header[o] = 0;  // position on the world map
    }
    assets::ChunkWriter writer;
    writer.add("INSEL5", header);
    std::vector<std::uint8_t> tiles;
    for (const assets::IslandTile& t : it->layers.front()) {
        const std::array<std::uint8_t, 8> e = {
            static_cast<std::uint8_t>(t.id),         static_cast<std::uint8_t>(t.id >> 8),
            static_cast<std::uint8_t>(t.x),          static_cast<std::uint8_t>(t.y),
            static_cast<std::uint8_t>(t.bits),       static_cast<std::uint8_t>(t.bits >> 8),
            static_cast<std::uint8_t>(t.bits >> 16), static_cast<std::uint8_t>(t.bits >> 24)};
        tiles.insert(tiles.end(), e.begin(), e.end());
    }
    writer.add("INSELHAUS", tiles);
    if (auto status = core::write_file(fs::path(std::string(args.positional[2])), writer.take()); !status) {
        return fail(status.error());
    }
    std::printf("island %s, %u x %u tiles, %zu tiles -> %s\n", number_text.c_str(),
                static_cast<unsigned>(it->header.width), static_cast<unsigned>(it->header.height),
                it->layers.front().size(), std::string(args.positional[2]).c_str());
    return 0;
}

}  // namespace o1602::tool
