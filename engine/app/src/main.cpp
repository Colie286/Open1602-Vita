// SPDX-License-Identifier: GPL-3.0-or-later
//
// open1602 - the game: finds the installation, then starts in the main menu or directly in a game.
// The options are parsed in command_line.cpp; an unknown one prints them all (usage() below).
//
// The game directory defaults to $ANNO1602_DIR, else to the folder chosen before, else to an
// installation in the usual folders; without any the player chooses it (game_folder.hpp). Without a
// game the program starts in the main menu (--menu forces it); with --no-panel the scenario defaults to "Szenes/Das
// Atoll.szs". A relative scenario path is looked up in the game directory first. --savegame NAME opens a savegame by
// the name shown in the game (looked up in Game.dat, e.g. "Merlin01" -> SAVEGAME/game00.gam) or by file name. Scenarios
// and savegames share one format. --load FILE opens a savegame of Open1602 (.o1602, docs/formats/o1602-save.md) or any
// file of the original by its path.

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_main.h>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "app_settings.hpp"
#include "command_line.hpp"
#include "diagnostics.hpp"
#include "game_folder.hpp"
#include "game_folder_dialog.hpp"
#include "map_viewer.hpp"

#include "o1602/assets/scenario.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;

static_assert(app::kModCtrl == SDL_KMOD_LCTRL && app::kModShift == SDL_KMOD_LSHIFT && app::kModAlt == SDL_KMOD_LALT);

namespace {

int usage() {
    std::fputs(
        "usage: open1602 [--game-dir DIR] [--scenario FILE | --savegame NAME | --load FILE] [--size WxH]\n"
        "                [--zoom 0|1|2] [--frames N]\n"
        "                [--owners] [--original-sheets] [--original-flags] [--original-data] [--refund MODE]\n"
        "                [--droughts MODE] [--volcanoes MODE] [--plagues MODE] [--fires MODE]\n"
        "                [--pirates MODE] [--pirate-nests MODE] [--reveal MODE]\n"
        "                [--ore-veins MODE] [--bread MODE] [--computers MODE] [--ai MODE]\n"
        "                [--new-game DIFFICULTY [--layout 0-2] [--name NAME] [--colour 0-3]] [--seed N]\n"
        "                [--islands original|seed]\n"
        "                [--world WxH] [--players 1-12] [--island-count C] [--island-sizes S] [--villages C]\n"
        "                [--volcano-islands C] [--gold-islands auto|N] [--starts spread|together]\n"
        "                [--island-shapes templates|mixed|own] [--island-preset P] [--island-land PERCENT]\n"
        "                [--island-min N] [--island-max N] [--island-spread S] [--island-gap G] [--island-coast C]\n"
        "                [--island-outline O] [--island-forest F] [--island-mountains M]\n"
        "                [--screenshot out.png] [--no-sound] [--no-music] [--no-panel] [--no-videos] [--intro]\n"
        "                [--video N] [--message ID] [--input EVENTS] [--select ISLAND,X,Y|ship:N]\n"
        "                [--host [--port N] | --join HOST[:PORT]] [--name NAME] [--editor [FILE]]\n"
        "  --new-game D       new endless game, difficulty 0 (easy) .. 3 (hard); layout: one of the three maps,\n"
        "                     chosen by the seed\n"
        "  --seed N           the world of an endless game: its map and islands (default: random)\n"
        "  --islands MODE     original: as the original (512 worlds per file), seed: from the whole seed\n"
        "  --world WxH ...    an own world for the new game (any of these options): size in tiles (200-4000),\n"
        "                     players with a start, island count, villages and volcano islands none|few|normal|\n"
        "                     many, island sizes small|mixed|large, islands with gold auto (default) or a\n"
        "                     number, starts spread (default) or together, island shapes of the original's\n"
        "                     templates (default), their own or mixed\n"
        "  --island-preset P  islands of their own shape as varied|archipelago|continents|giants|even\n"
        "  --island-land ...  islands of their own shape: land 5-45 percent of the map, the smallest and the\n"
        "                     largest island's side 16-1000 tiles, spread even|mixed|giants, gap narrow|normal|\n"
        "                     wide, coast smooth|natural|rugged, outline round|long|branched, forest and\n"
        "                     mountains little|normal|much (with --island-shapes own or mixed)\n"
        "  --original-sheets  no figures at the smallest zoom level, as in the original\n"
        "  --original-flags   original flag images instead of the tinted generic flag\n"
        "  --original-data    the original's data without its corrections (D-001 ...; settings.cfg data_fixes)\n"
        "  --refund MODE      what demolishing gives back: none (original), partial[:PERCENT] (default 50), full\n"
        "  --droughts MODE    game (as the game's difficulty says, original), off, rare, normal, frequent\n"
        "  --volcanoes MODE   game (as the game's settings say, original), off, on, rare\n"
        "  --plagues MODE     original, rare (half as often), off: plagues breaking out in towns\n"
        "  --fires MODE       original, rare (half as often), off: fires breaking out in towns\n"
        "  --pirates MODE     game (as the game's settings say, original: none on the two easy levels), off, on\n"
        "  --pirate-nests MODE  one (original), per4 (one for every four players begun)\n"
        "  --reveal MODE      towns discovered without a ship: none (every island by ship), original (a town\n"
        "                     of a human or computer player once it is founded), all (also native villages\n"
        "                     and pirate nests, and the towns there from the start)\n"
        "  --ore-veins MODE   game (as the game's settings say, original: endless in every endless game),\n"
        "                     finite (large iron veins run out), endless (the deep mine digs on for ever)\n"
        "  --bread MODE       original (the original's bread chain), revised (twice the grain per field and\n"
        "                     the bread per flour, at a higher running cost of the bakery)\n"
        "  --computers MODE   game (as the game's settings say), off (computer opponents do nothing), on\n"
        "  --ai MODE          open1602 (computer players without the original's bugs), classic (the original's)\n"
        "  --no-panel         no panel and status bar around the map (the original's frame)\n"
        "  --no-sound         no effects and voice messages; --no-music: no music (the original's Game.dat\n"
        "                     is read for the volumes, never written)\n"
        "  --no-videos        no videos of messages, victory, defeat and pause\n"
        "  --original-mouse   the original's mouse: orders with the left button, the right one cancels\n"
        "  --no-confirm       no questions before scuttling, disbanding and declaring war\n"
        "  --no-dialogs       no message boxes and no question for the game's folder: errors on stderr only\n"
        "                     (also with --frames N, for scripts)\n"
        "  --keys FILE        key settings (default: keys.cfg in the user's data folder; F1 shows the keys)\n"
        "  --language CODE    language of Open1602's own texts, e.g. en, de (default: the installation's)\n"
        "  --intro            the two logos and the intro first, as the original's start (Escape skips them)\n"
        "  --video N          the video VideoSmk/N.smk first (e.g. 58 the intro, 10 the credits)\n"
        "  --message ID       show a message at the start, e.g. a mod's mymod:lighthouse (docs/modding/messages.md)\n"
        "  --input EVENTS     mouse and keys at frames, for tests: \"move X,Y@F;down X,Y@F;up X,Y@F;key K@F\";\n"
        "                     rdown/rup: the right button; \"wheel X,Y,N@F\": N notches (up zooms in)\n"
        "                     \"down+shift X,Y@F\" (also +ctrl, up, rdown, rup): with the key held\n"
        "                     \"text T@F\": T typed (a name)\n"
        "  --select I,X,Y     select the building at tile X,Y of island I (index) and look at it\n"
        "  --select ship:N    select the N-th ship of the game (index) and look at it; unit:N a land unit\n"
        "  --load FILE        open a savegame of Open1602 (.o1602) or a file of the original by its path\n"
        "  --menu             start in the main menu (the default when no game is named)\n"
        "  --editor [FILE]    start in the editor: with FILE (Open1602's own, or a scenario or savegame of the\n"
        "                     original), or with a new map\n"
        "  --host             host a multiplayer game of the game named (UDP port 16020, --port N); others join\n"
        "                     into the free human slots, or watch\n"
        "  --join HOST[:PORT] join the multiplayer game of a host (name or IPv4 address); the game comes from it\n"
        "  --password TEXT    the password of the game to join (--join)\n"
        "  --name NAME        the player's name (multiplayer, new endless game)\n"
        "  --save FILE        save the game when the viewer ends (never into the installation): Open1602's own\n"
        "                     savegame, or with the ending .gam a savegame the original loads (if the game fits)\n"
        "\n"
        "Keys (docs/architecture/0010-controls-and-interface.md):\n"
        "  WASD, arrows   scroll (Shift: faster)      right/middle mouse drag  scroll\n"
        "  Q / E          rotate view                 wheel, PageUp/PageDn     zoom\n"
        "  Space          to the last message         Home                     to the selection\n"
        "  F3 / Pause     pause / with the pause video + / -, Num+ / Num-      game speed\n"
        "  F5 / F9        quick save / quick load     F12, Print               screenshot\n"
        "  F1             these keys over the map     F11, Alt+Enter           window / full screen\n"
        "  Ctrl + / Ctrl - the map twice / half as large\n"
        "  B K I O        build, military, info, options   F2, Tab  goods balance\n"
        "  Enter          chat (multiplayer)\n"
        "  left mouse     select (drag: rectangle, Shift: add, Ctrl/double: all alike on screen)\n"
        "  right mouse    order (also on the minimap; Shift: queued)\n"
        "  Ctrl+1..0      store a group  Shift+1..0 add to it  1..0 select it (twice: go there)\n"
        "  . / ,          next idle ship / unit\n"
        "  units:         F attack, H stop, G hold, P patrol, C formation, Shift+C scatter, Tab one kind,\n"
        "                 Y white flag, X explore, T trade, R route, Del disband/scuttle\n"
        "  building:      R rotate, C pick the building under the mouse, Del demolish\n"
        "  Esc            back, out of a tool, deselect   Ctrl+Q quit   Ctrl+O ownership view\n",
        stderr);
    return 2;
}

}  // namespace

namespace {

int run_program(int argc, char** argv) {
    auto parsed = app::parse_command_line(argc, argv);
    if (!parsed) {
        std::fprintf(stderr, "open1602: %s\n", parsed.error().message.c_str());
        return usage();
    }
    app::CommandLine& command = *parsed;
    app::ViewerOptions& options = command.options;
    std::string scenario = command.scenario;
    app::set_dialogs(command.dialogs);

    // --game-dir, ANNO1602_DIR, the folder chosen before, the usual folders; else the player is asked.
    const std::filesystem::path settings_file = app::MapViewer::data_folder() / "settings.cfg";
    app::AppSettings settings;
    if (auto bytes = core::read_file(settings_file)) {
        settings = app::parse_settings(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
                                       settings);
    }
    app::GameFolderSearch search = app::find_game_folder(command.game_dir, settings.game_folder,
                                                         core::environment_variable(assets::GameDirectory::kEnvVar));
    if (!search.game && search.ask && command.dialogs) {
        ui::set_language(app::startup_language(options.language, settings.language),
                         app::MapViewer::data_folder() / "locale");
        search.game = app::ask_for_game_folder(search, settings_file);
        if (!search.game) {
            return 1;
        }
    }
    if (!search.game) {
        app::report_fatal(search.error.message + "\n(set ANNO1602_DIR or pass --game-dir)");
        return 1;
    }
    // The History Edition has a folder per language: the one asked for, else the system's.
    if (!options.language.empty()) {
        search.game->choose_language(options.language);
    } else if (!settings.language.empty() && settings.language != "auto") {
        search.game->choose_language(settings.language);
    }
    const assets::GameDirectory* game = &*search.game;
    std::string savegame_path;
    if (const std::string& savegame = command.savegame; !savegame.empty()) {
        // The name shown in the game (Game.dat, "SPIELNAME") or a file name in SAVEGAME/.
        if (const auto dat = game->resolve("Game.dat")) {
            if (auto bytes = core::read_file(*dat)) {
                const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
                for (const auto& slot : assets::parse_savegame_slots(text)) {
                    if (core::equals_ignore_case(slot.name, savegame)) {
                        savegame_path = slot.file;
                    }
                }
            }
        }
        if (savegame_path.empty()) {
            savegame_path = "Savegame/" + std::string(savegame);
            if (!savegame_path.ends_with(".gam") && !savegame_path.ends_with(".GAM")) {
                savegame_path += ".gam";
            }
        }
        scenario = savegame_path;
    }
    if (!command.editor_file.empty()) {
        // As --scenario: a relative path in the game directory first.
        const auto resolved = game->resolve(command.editor_file);
        options.editor_file = resolved ? *resolved : core::path_from_utf8(command.editor_file);
    }
    if (!command.load.empty()) {
        options.scenario = core::path_from_utf8(command.load);
    } else if (const auto resolved = game->resolve(scenario)) {
        options.scenario = *resolved;
    } else {
        options.scenario = core::path_from_utf8(scenario);
    }

    // What the original rolls with rand(): the map layout and where the island table starts; the
    // game id from the clock.
    std::random_device device;
    options.world.seed = command.seed.value_or((std::uint64_t{device()} << 32) | device());
    options.world.guarantee_resources = options.rules.guarantee_resources;
    options.world.climate_crops = options.rules.climate_crops;
    options.world.big_ore_veins = options.rules.big_ore_veins;
    if (command.new_game) {
        sim::NewGameOptions& new_game_options = *command.new_game;
        new_game_options.layout = command.layout.value_or(sim::layout_for_seed(options.world.seed));
        const std::time_t now = std::time(nullptr);
        const std::tm* local = std::localtime(&now);
        new_game_options.game_id = local != nullptr
                                       ? sim::new_game_id(static_cast<std::uint32_t>(local->tm_year + 1900),
                                                          static_cast<std::uint32_t>(local->tm_mon + 1),
                                                          static_cast<std::uint32_t>(local->tm_mday),
                                                          static_cast<std::uint32_t>(local->tm_hour),
                                                          static_cast<std::uint32_t>(local->tm_min), device())
                                       : device();
        new_game_options.world = options.world;
        if (const auto& custom = new_game_options.custom) {
            std::fprintf(stderr, "new endless game: own world %ux%u, %u players, level %u, seed %llu\n", custom->width,
                         custom->height, static_cast<unsigned>(custom->players),
                         static_cast<unsigned>(new_game_options.difficulty),
                         static_cast<unsigned long long>(options.world.seed));
        } else {
            std::fprintf(stderr, "new endless game: %s, islands from seed %llu\n",
                         sim::endless_file(new_game_options.difficulty, new_game_options.layout).c_str(),
                         static_cast<unsigned long long>(options.world.seed));
        }
        options.new_game = new_game_options;
    }
    // Before the game is made: what it notes while loading already goes by the clock of the frames.
    platform::use_fixed_clock(app::fixed_frame_ms(options));
    auto viewer = app::MapViewer::create(*game, options);
    if (!viewer) {
        app::report_fatal(viewer.error().message);
        return 1;
    }
    return (*viewer)->run();
}

}  // namespace

int main(int argc, char** argv) {
    app::setup_diagnostics();
    // What nothing else caught (a length_error, a filesystem_error) ends the program with a
    // message, on Windows in a message box, instead of std::terminate.
    int code = 1;
    try {
        code = run_program(argc, argv);
    } catch (const std::exception& e) {
        app::report_fatal(std::string("unexpected error: ") + e.what());
    } catch (...) {
        app::report_fatal("unexpected error");
    }
    app::report_exit(code);
    return code;
}
