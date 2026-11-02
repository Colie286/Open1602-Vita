// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool scenario: Open1602's scenarios (ADR 0013) without the editor's window.
//
//   scenario info    <file.o1602>                    what the head of the file says (no game data needed)
//   scenario convert <in> <out.o1602> [--name TEXT] [--language CODE] [--stars N] [--author TEXT]
//                    [--original-limits]             any file the game opens as an Open1602 scenario
//   scenario check   <file>                          whether it holds together and keeps to its limits

#include <cstdio>
#include <string>

#include "tool.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/core/file.hpp"
#include "o1602/editor/document.hpp"
#include "o1602/editor/operations.hpp"
#include "o1602/editor/original_check.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/scenario_data.hpp"
#include "o1602/sim/validate.hpp"

namespace o1602::tool {

namespace {

int scenario_usage() {
    std::fputs(
        "usage:\n"
        "  o1602-tool scenario info    <file.o1602>\n"
        "  o1602-tool scenario convert <in.szs|.szm|.gam|.o1602> <out.o1602> [--name TEXT] [--language CODE]\n"
        "                              [--stars 0-5] [--author TEXT] [--original-limits]  (needs ANNO1602_DIR)\n"
        "  o1602-tool scenario check   <file>                                          (needs ANNO1602_DIR)\n",
        stderr);
    return 2;
}

int info(const Args& args) {
    if (args.positional.size() != 2) {
        return scenario_usage();
    }
    auto bytes = core::read_file(fs::path(args.positional[1]));
    if (!bytes) {
        return fail(bytes.error());
    }
    auto head = sim::save_info(*bytes);
    if (!head) {
        return fail(head.error());
    }
    std::printf("format version %u, %s, sections %llu bytes\n", unsigned{head->version},
                head->compressed ? "packed" : "unpacked", static_cast<unsigned long long>(head->size));
    if (!head->meta) {
        std::printf("no META\n");
        return 0;
    }
    std::printf("map %u x %u, game time %llu ms\n", head->meta->width, head->meta->height,
                static_cast<unsigned long long>(head->meta->time_ms));
    const auto& scenario = head->meta->scenario;
    if (!scenario) {
        std::printf("a savegame, no scenario\n");
        return 0;
    }
    std::printf("scenario, %u stars%s\n", unsigned{scenario->stars},
                scenario->original_limits ? ", keeps to the original's limits" : "");
    for (const sim::ScenarioText& name : scenario->names) {
        std::printf("name %s: %s\n", name.language.c_str(), name.text.c_str());
    }
    return 0;
}

/// The game data a document needs: the installation and its buildings.
struct GameData {
    assets::GameDirectory game;
    assets::BuildingTable buildings;
};

core::Result<GameData> game_data() {
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        return game.error();
    }
    auto buildings = assets::BuildingTable::load(*game);
    if (!buildings) {
        return buildings.error();
    }
    return GameData{std::move(game).value(), std::move(buildings).value()};
}

core::Result<editor::Document> open(const GameData& data, std::string_view path) {
    auto bytes = core::read_file(fs::path(path));
    if (!bytes) {
        return bytes.error();
    }
    std::vector<std::string> warnings;
    auto document = editor::Document::open(*bytes, data.buildings, &data.game, &warnings);
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    return document;
}

int convert(const Args& args) {
    if (args.positional.size() != 3) {
        return scenario_usage();
    }
    const auto stars = args.number("--stars");
    if (stars && *stars > sim::ScenarioInfo::kMaxStars) {
        std::fprintf(stderr, "error: --stars: 0 to %u\n", unsigned{sim::ScenarioInfo::kMaxStars});
        return 2;
    }
    auto data = game_data();
    if (!data) {
        return fail(data.error());
    }
    auto document = open(*data, args.positional[1]);
    if (!document) {
        return fail(document.error());
    }
    const auto& before = document->world().scenario_info();
    sim::ScenarioInfo info = before.value_or(sim::ScenarioInfo{});
    const auto option = [&](std::string_view name) -> std::optional<std::string> {
        const auto it = args.options.find(name);
        return it == args.options.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
    if (const auto name = option("--name")) {
        const std::string language = option("--language").value_or("en");
        std::erase_if(info.names, [&](const sim::ScenarioText& t) { return t.language == language; });
        info.names.push_back(sim::ScenarioText{language, *name});
    }
    if (const auto author = option("--author")) {
        info.author = *author;
    }
    if (stars) {
        info.stars = static_cast<std::uint8_t>(*stars);
    }
    info.original_limits = info.original_limits || args.flag("--original-limits");
    if (const auto problems = sim::check_scenario_info(info); !problems.empty()) {
        std::fprintf(stderr, "error: %s\n", problems.front().c_str());
        return 2;
    }
    if (!before || *before != info) {
        document->perform(std::make_unique<editor::ScenarioInfoEdit>(before, info));
    }
    auto bytes = document->save(data->buildings);
    if (!bytes) {
        return fail(bytes.error());
    }
    if (auto status = core::write_file_atomic(fs::path(args.positional[2]), *bytes); !status) {
        return fail(status.error());
    }
    std::printf("-> %s (%zu bytes)\n", std::string(args.positional[2]).c_str(), bytes->size());
    return 0;
}

int check(const Args& args) {
    if (args.positional.size() != 2) {
        return scenario_usage();
    }
    auto data = game_data();
    if (!data) {
        return fail(data.error());
    }
    auto document = open(*data, args.positional[1]);
    if (!document) {
        return fail(document.error());
    }
    int result = 0;
    if (auto valid = sim::validate(document->world()); !valid) {
        std::printf("does not hold together: %s\n", valid.error().message.c_str());
        result = 1;
    }
    for (const std::string& p : document->check_scenario()) {
        std::printf("scenario: %s\n", p.c_str());
        result = 1;
    }
    const auto& scenario = document->world().scenario_info();
    const bool keep = scenario && scenario->original_limits;
    for (const std::string& p : editor::check_original_limits(document->world())) {
        std::printf("%s: %s\n", keep ? "beyond the original's limits" : "(beyond the original's limits)", p.c_str());
        result = keep ? 1 : result;
    }
    std::printf(result == 0 ? "ok\n" : "problems found\n");
    return result;
}

}  // namespace

int cmd_scenario(const Args& args) {
    if (args.positional.empty()) {
        return scenario_usage();
    }
    const std::string_view what = args.positional[0];
    if (what == "info") {
        return info(args);
    }
    if (what == "convert") {
        return convert(args);
    }
    if (what == "check") {
        return check(args);
    }
    return scenario_usage();
}

}  // namespace o1602::tool
