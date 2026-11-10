// SPDX-License-Identifier: GPL-3.0-or-later
//
// High score lists and campaign progress (docs/re/missions.md "Kampagnen").
//
// The original writes both into its installation: the list of a scenario to Szenes/<name>.hss
// (HighScore_Insert 0x0048E540, twelve lines), the progress into Game.dat. Open1602 never writes
// there (T-005): it keeps its own copies in the user's data folder, in the same text format for the
// lists, and starts a list from the original's file the first time it adds a line.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/scenario.hpp"

namespace o1602::assets {

/// Lines of a list in the original's files.
inline constexpr std::size_t kHighScoreLines = 12;
/// Bytes of a name: the original copies it into 32 bytes without a check.
inline constexpr std::size_t kHighScoreNameLength = 31;

/// The text of a .hss file as the original writes it: "Objekt: HISCORE", one "Entry: i, \"name\",
/// points, stars, game id" per line, padded with empty lines to kHighScoreLines, "EndObj;".
[[nodiscard]] std::string write_high_scores(const std::vector<HighScore>& scores);

/// Puts `entry` into the list as HighScore_Insert (0x0048E540) does: above the first line with
/// as many points or fewer. A line of the same game id and name keeps the better result of the
/// two, so a game ended again after loading a savegame holds one line. The list then has
/// kHighScoreLines lines (empty ones at the end). The index of the line that holds the result,
/// or nothing if it did not make the list.
std::optional<std::size_t> insert_high_score(std::vector<HighScore>& scores, HighScore entry);

/// Which missions a player has opened: per campaign the next part, and the next numbered
/// mission of the original scenarios (Game.dat lines "Kampagne: k, n" and the third value of
/// "Volume:" minus 597 in the original).
struct CampaignProgress {
    static constexpr std::uint32_t kFirstNumbered = 2;  ///< what counts without a value
    static constexpr std::uint32_t kLastNumbered = 17;
    std::map<std::uint32_t, std::uint32_t> campaigns;  ///< campaign -> next part
    std::uint32_t numbered = kFirstNumbered;

    /// A single player game won (GameDat_Write 0x00487FB0): a numbered mission n (or 0, 1) that
    /// was the furthest opens n + 1 (at most kLastNumbered); otherwise a campaign opens `part + 1`
    /// if that is further than before. True if something changed.
    bool record_win(std::uint32_t campaign, std::uint32_t part, std::uint32_t mission_number);
    /// Whether that win ends the campaigns, asked before record_win: the last part (3) of campaign 5
    /// won by way of the campaign rule. The original then plays video 2990 after the success video.
    [[nodiscard]] bool ends_campaigns(std::uint32_t campaign, std::uint32_t part,
                                      std::uint32_t mission_number) const noexcept;
};
/// The video after the last campaign (VideoSmk/2990.smk).
inline constexpr std::uint16_t kCampaignsEndVideo = 2990;
/// Lines "campaign k n" and "numbered n"; unknown lines are passed over.
[[nodiscard]] CampaignProgress parse_progress(std::string_view text);
[[nodiscard]] std::string write_progress(const CampaignProgress& progress);

}  // namespace o1602::assets
