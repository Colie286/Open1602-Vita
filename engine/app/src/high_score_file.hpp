// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where a game's high score list lives. Without a platform, so that the main menu's lists and the
// tests have it as well as the score sheets.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/sim/score.hpp"

namespace o1602::app {

/// Where a game's high score list lives, relative to the scores folder: the scenario's name (a
/// campaign part's label, [KAMPAGNE] line 5 * campaign + part), in "Eigene Szenarien" for the
/// player's own scenarios, .hss for one player and .hsm for several. Empty without a name.
[[nodiscard]] std::filesystem::path high_score_file(const sim::GameIdentity& game,
                                                    const std::vector<std::string>& campaign_labels, bool multiplayer);
/// A name of Windows-1252 as a file name (UTF-8), without characters a file system refuses.
[[nodiscard]] std::filesystem::path file_name_of(std::string_view cp1252);

}  // namespace o1602::app
