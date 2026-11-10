// SPDX-License-Identifier: GPL-3.0-or-later
//
// The question for the game's folder when the search (game_folder.hpp) found none: a message box
// says what is wanted, then the system's folder dialog (SDL, on every platform) lets the player pick
// the installation. A folder that is no installation is named with the reason and the player picks
// again; the one that is goes into settings.cfg, so the question comes again only when it is moved
// or deleted. Before the installation is known its language is not: the texts follow --language,
// settings.cfg or the system's language.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "game_folder.hpp"

#include "o1602/assets/game_directory.hpp"

namespace o1602::app {

/// The language of the question: `command_line` (--language), else `settings` unless "auto", else
/// the first of the system's languages Open1602 has, else English.
[[nodiscard]] std::string startup_language(std::string_view command_line, std::string_view settings);

/// Asks until the player chooses an installation (saved as game_folder in `settings_file`) or quits.
/// Nothing if the player quit, or if no dialog could be shown; that is reported as a fatal error.
[[nodiscard]] std::optional<assets::GameDirectory> ask_for_game_folder(const GameFolderSearch& search,
                                                                       const std::filesystem::path& settings_file);

}  // namespace o1602::app
