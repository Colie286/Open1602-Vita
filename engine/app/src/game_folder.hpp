// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which Anno 1602 installation the game plays with, in this order: --game-dir, ANNO1602_DIR, the
// folder chosen in the folder dialog before (settings.cfg, "game_folder"), an installation in the
// usual folders (GameDirectory::installation_candidates). A folder named on the command line or in
// ANNO1602_DIR is never replaced: a typo there is an error. Only when all of them come to nothing is
// the player asked (game_folder_dialog.hpp). Without SDL, so that the tests have it.
#pragma once

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "o1602/assets/game_directory.hpp"
#include "o1602/core/result.hpp"

namespace o1602::app {

struct GameFolderSearch {
    /// The installation found; if none, `error` says why and `ask` whether the player may choose one.
    std::optional<assets::GameDirectory> game;
    core::Error error{core::ErrorCode::NotFound, {}};
    /// No folder was named (or only the saved one, which no longer counts): the folder dialog may ask.
    bool ask = false;
    /// The saved folder that no longer counts, for the dialog to say so; empty if there was none.
    std::filesystem::path saved_invalid;
};

/// The search in the order above. `command_line` is --game-dir as typed, `saved` the settings'
/// game_folder (UTF-8), `environment` the value of ANNO1602_DIR if it is set (blanks and quotes
/// around both are passed over).
[[nodiscard]] GameFolderSearch find_game_folder(std::string_view command_line, std::string_view saved,
                                                const std::optional<std::string>& environment);

/// The folder the player chose in the dialog, or the single installation directly inside it (a
/// player who picks "GOG Games" means the "Anno 1602" folder in it).
[[nodiscard]] core::Result<assets::GameDirectory> open_chosen_folder(const std::filesystem::path& folder);

/// Why `folder` is no installation, in the current language (UTF-8, for the system's dialogs): it
/// does not exist, it holds several installations (the player chooses one), or a file of the game
/// is missing. Empty if it is one.
[[nodiscard]] std::string folder_problem(const std::filesystem::path& folder);

/// A text of the current language in UTF-8 with {1}, {2}, ... replaced by `args`, which are UTF-8 as
/// well (paths keep every letter, the game's Windows-1252 has not all of them).
[[nodiscard]] std::string tr_utf8(std::string_view key, std::initializer_list<std::string_view> args = {});

}  // namespace o1602::app
