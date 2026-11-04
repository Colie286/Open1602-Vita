// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool - inspect and convert original Anno 1602 data files, make own worlds, measure the
// simulation. usage() below lists every command with its options (o1602-tool without arguments
// prints it). The commands for the original's files:
//
//   o1602-tool chunks     <file>                         list chunks of any chunk container
//   o1602-tool cod        <file.cod> [out.txt]           decode a .cod script to UTF-8 text
//   o1602-tool cod-dump   <file.cod> [--predefined haeuser|figuren|none] [--type NAME]
//                                                        interpret a .cod script and print its records
//   o1602-tool bsh-info   <file.bsh|.zei>                list sprites
//   o1602-tool bsh-export <file.bsh|.zei> <outdir> [--palette FILE] [--sheet] [--first N] [--count N]
//   o1602-tool islands    <file.scp|.szs|.szm|.gam|.o1602> list islands and tile statistics
//   o1602-tool savegames                                 list savegames (names from Game.dat)
//   o1602-tool render-island <file.scp|.szs|.gam> <out.png> [--island N] [--zoom 0|1|2]
//                            [--rotation 0-3] [--anim N] [--no-template] [--base FILE.scp]
//                                                        render an island (needs ANNO1602_DIR)
//   o1602-tool render-world <file.szs|.gam> <out.png> [--zoom 0|1|2] [--rotation 0-3] [--time MS]
//                           [--tile X,Y] [--size WxH] [--owners]
//                                                        render the world map or a part of it
//   o1602-tool export, new-world, bench, scenario, smk, bk2, webm, state, check-mod,
//   export-island see usage()
//
// The palette defaults to $ANNO1602_DIR/TOOLGFX/STADTFLD.COL.
//
// The commands live in formats.cpp, mods.cpp, savegames.cpp, render.cpp, scenario.cpp, templates.cpp
// and video.cpp (declared in tool.hpp).

#include <cstdio>
#include <string>
#include <string_view>

#include "tool.hpp"

#include "o1602/assets/game_directory.hpp"

namespace o1602::tool {

int fail(const core::Error& error) {
    std::fprintf(stderr, "error: %s\n", error.message.c_str());
    return 1;
}

int usage() {
    std::fputs(
        "usage:\n"
        "  o1602-tool chunks     <file>\n"
        "  o1602-tool cod        <file.cod> [out.txt]\n"
        "  o1602-tool cod-dump   <file.cod> [--predefined haeuser|figuren|none] [--type NAME]\n"
        "  o1602-tool bsh-info   <file.bsh|.zei>\n"
        "  o1602-tool bsh-export <file.bsh|.zei> <outdir> [--palette FILE] [--sheet] [--first N] [--count N]\n"
        "  o1602-tool islands    <file.scp|.szs|.szm|.gam|.o1602>  (.o1602 needs ANNO1602_DIR)\n"
        "  o1602-tool template-stats [--count N]           (the island templates' tiles, rivers and rocks;\n"
        "                                                   N maps of templates with rivers, needs ANNO1602_DIR)\n"
        "  o1602-tool savegames                               (names from Game.dat, needs ANNO1602_DIR)\n"
        "  o1602-tool export    <in.gam|in.o1602> <out.gam> [--via-own]  (needs ANNO1602_DIR)\n"
        "  o1602-tool new-world <out.gam|out.o1602> [--level 0-3] [--seed N] [--world WxH] [--players N]\n"
        "                       [--island-count C] [--villages C] [--volcano-islands C] [--island-sizes S]\n"
        "                       [--gold-islands N] [--together] [--island-shapes templates|mixed|own]\n"
        "                       [--island-preset P] [--island-land PERCENT] [--island-min N] [--island-max N]\n"
        "                       [--island-spread S] [--island-gap G] [--island-coast C] [--island-outline O]\n"
        "                       [--island-forest F] [--island-mountains M] [--without-original]\n"
        "                       [--mods FOLDER]\n"
        "                       (an own world as a new game, 1-4 players in a .gam, more in a .o1602; C "
        "none|few|normal|many, S small|mixed|large,\n"
        "                        N islands with gold, default by the level; shapes: the original's templates,\n"
        "                        islands of their own shape or both; the landscape of islands of their own\n"
        "                        shape as in open1602 --help; written as a savegame; --without-original: a .o1602\n"
        "                        without the original's file it was made from, for a map to pass on; --mods:\n"
        "                        the island templates of the packages of that folder too)\n"
        "  o1602-tool bench     [<file.gam|.o1602>] [--players N] [--world WxH] [--seed N] [--level 0-3]\n"
        "                       [--minutes M] [--period M] [--repeat N] [--wall] [--out file.o1602]\n"
        "                       [--mods FOLDER]\n"
        "                       (runs a game headless, every player a computer; tick times per period and step,\n"
        "                        CPU time of the game thread unless --wall, the fastest of N runs per tick;\n"
        "                        state hash, savegame, peak memory; default: new own world, 8 players, 600 min;\n"
        "                        without --world as large as a multiplayer game of that many players; --mods: with\n"
        "                        the rules of the packages of that folder, a savegame keeps its own)\n"
        "  o1602-tool scenario  info|convert|check ...  (Open1602's scenarios; o1602-tool scenario lists the options)\n"
        "  o1602-tool smk        <file.smk> [--png DIR] [--every N] [--wav TRACK OUT.wav]\n"
        "                        (Smacker video: header, decode all frames; frames as PNG, a track as WAVE)\n"
        "  o1602-tool bk2        <file.bk2> [--png DIR] [--every N] [--wav TRACK OUT.wav]\n"
        "  o1602-tool webm       <file.webm> [--png DIR] [--every N] [--wav TRACK OUT.wav]\n"
        "                        (Bink 2 video: header, decode all frames and their time; frames as PNG, a\n"
        "                         track as WAVE)\n"
        "  o1602-tool state      <file.gam|.szs|.o1602> [--wares] [--production] [--buildings] [--mods FOLDER]\n"
        "                        (players, cities, warehouses, production, ships, deer; .o1602 needs\n"
        "                        ANNO1602_DIR; --buildings: a .o1602's buildings of each player; --mods:\n"
        "                        with the buildings of the packages of that folder)\n"
        "  o1602-tool render-island <file.scp|.szs|.gam> <out.png> [--island N] [--zoom 0|1|2] [--rotation 0-3]\n"
        "                           [--anim N] [--no-template] [--base FILE.scp]\n"
        "  o1602-tool render-world <file.szs|.gam> <out.png> [--zoom 0|1|2] [--rotation 0-3] [--time MS]\n"
        "                          [--tile X,Y] [--size WxH] [--owners] [--original-sheets] [--original-flags]\n"
        "                          [--original-data] [--banner P:RRGGBB] [--simulate SECONDS] [--walkers]\n"
        "                          [--reveal none|original|all] [--mods FOLDER]\n"
        "                          [--build \"ID@X,Y[,R];...\"]  (world tiles, for the first human player)\n"
        "                          (default: whole world at zoom 2; --simulate: run the economy first; "
        "--original-sheets: no figures where the\n"
        "                          original has no graphics for them; --original-flags: the original flag\n"
        "                          images instead of the tinted generic flag; --original-data: without the\n"
        "                          corrections of the original's data, D-001 ...; --mods: with the pictures of\n"
        "                          the packages of that folder)\n"
        "  o1602-tool check-mod <mods folder> [--json]  (reads its packages as the game does and lists\n"
        "                       every problem; exit code 1 if there is one; needs ANNO1602_DIR; --json:\n"
        "                       packages and problems as one JSON object, with file and line)\n"
        "  o1602-tool export-island <in.gam|.szs> <island number> <out.scp>  (an island of its own shape\n"
        "                       with all its tiles as an island file of a package, islands.toml)\n",
        stderr);
    return 2;
}

core::Result<assets::Palette> load_palette(const Args& args) {
    if (const auto it = args.options.find("--palette"); it != args.options.end()) {
        return assets::Palette::load(fs::path(it->second));
    }
    auto dir = assets::GameDirectory::from_environment();
    if (!dir) {
        return core::make_error(core::ErrorCode::NotFound, "no --palette given and " + dir.error().message);
    }
    const auto path = dir->resolve("toolgfx/stadtfld.col");
    if (!path) {
        return core::make_error(core::ErrorCode::NotFound, "TOOLGFX/STADTFLD.COL not found");
    }
    return assets::Palette::load(*path);
}

}  // namespace o1602::tool

int main(int argc, char** argv) {
    using namespace o1602::tool;
    if (argc < 2) {
        return usage();
    }
    const std::string_view command = argv[1];
    const Args args(argc, argv);
    if (!args.problems.empty()) {
        for (const std::string& p : args.problems) {
            std::fprintf(stderr, "error: %s\n", p.c_str());
        }
        return 2;
    }
    if (command == "chunks") {
        return cmd_chunks(args);
    }
    if (command == "cod") {
        return cmd_cod(args);
    }
    if (command == "cod-dump") {
        return cmd_cod_dump(args);
    }
    if (command == "bsh-info") {
        return cmd_bsh_info(args);
    }
    if (command == "bsh-export") {
        return cmd_bsh_export(args);
    }
    if (command == "render-island") {
        return cmd_render_island(args);
    }
    if (command == "render-world") {
        return cmd_render_world(args);
    }
    if (command == "savegames") {
        return cmd_savegames(args);
    }
    if (command == "islands") {
        return cmd_islands(args);
    }
    if (command == "smk") {
        return cmd_smk(args);
    }
    if (command == "bk2") {
        return cmd_bk2(args);
    }
    if (command == "webm") {
        return cmd_webm(args);
    }
    if (command == "state") {
        return cmd_state(args);
    }
    if (command == "new-world") {
        return cmd_new_world(args);
    }
    if (command == "bench") {
        return cmd_bench(args);
    }
    if (command == "template-stats") {
        return cmd_template_stats(args);
    }
    if (command == "export") {
        return cmd_export(args);
    }
    if (command == "scenario") {
        return cmd_scenario(args);
    }
    if (command == "check-mod") {
        return cmd_check_mod(args);
    }
    if (command == "export-island") {
        return cmd_export_island(args);
    }
    return usage();
}
