// SPDX-License-Identifier: GPL-3.0-or-later
//
// The command line of open1602 (main.cpp prints the usage): parsed apart from the program's start,
// without a window, so that the tests have it (BT-2).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "viewer_options.hpp"

#include "o1602/core/result.hpp"

namespace o1602::app {

/// The modifier bits of "key K+ctrl@F" and "key K+shift@F" (SDL_KMOD_LCTRL, SDL_KMOD_LSHIFT; main.cpp
/// checks that they are SDL's).
inline constexpr std::uint16_t kModCtrl = 0x0040;
inline constexpr std::uint16_t kModShift = 0x0001;

struct CommandLine {
    /// The options as given, with the multiplayer game and `menu` set. What needs the installation
    /// or chance (scenario, world.seed, new_game) the program fills in.
    ViewerOptions options;
    std::string game_dir;                           ///< --game-dir as typed; empty: not given
    std::string scenario = "Szenes/Das Atoll.szs";  ///< --scenario, as typed
    std::string savegame;                           ///< --savegame: a name of Game.dat or a file name
    std::string load;                               ///< --load: a file by its path
    /// --new-game: difficulty, name, colour and an own world; layout and game id are still to roll.
    std::optional<sim::NewGameOptions> new_game;
    std::optional<std::uint8_t> layout;  ///< --layout
    std::optional<std::uint64_t> seed;   ///< --seed
    /// false with --no-dialogs or --frames N: a run from a script, where a message box or the
    /// folder question would wait for a click nobody gives (the desktop's dialog helper may not
    /// even end on SIGTERM). The message goes to stderr only.
    bool dialogs = true;
};

/// "move X,Y@F;down X,Y@F;up X,Y@F;key K@F;text T@F;wheel X,Y,N@F" (K: SDL key code, "+ctrl" /
/// "+shift" after it; T: text typed, as the platform's text input gives it). False if an item is
/// not of these forms.
[[nodiscard]] bool parse_input(std::string_view text, std::vector<ViewerOptions::ScriptedInput>& out);

/// The arguments after the program's name (argv[1] ...). An unknown option, a missing value or one
/// out of range is an error that names it.
[[nodiscard]] core::Result<CommandLine> parse_command_line(int argc, const char* const* argv);

}  // namespace o1602::app
