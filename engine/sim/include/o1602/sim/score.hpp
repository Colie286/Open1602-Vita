// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scores and the end of a game (docs/re/missions.md): the points of a player
// (Player_ComputeScore 0x00474F90), what identifies a game for its high score list, and the
// figures the end screen shows.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/player.hpp"

namespace o1602::sim {

class World;

/// A player's score sheet as the original computes it (S[0..21] of 0x00474F90) and the end screen
/// shows it: a figure per line of the status column and the points it gives. `total` goes into the
/// high score list.
struct ScoreSheet {
    PlayerId player = kNoPlayer;
    std::int64_t money = 0;               ///< S1
    std::int64_t money_points = 0;        ///< S2: steps from 50,000 to 10,000,000 gold
    std::int64_t inhabitants = 0;         ///< S3: Einwohner of all own cities
    std::int64_t subjects_points = 0;     ///< S4: inhabitants per class times its Hiscore / 50
    std::int64_t cities = 0;              ///< S5
    std::int64_t city_points = 0;         ///< S6: grows faster than the size of each city
    std::int64_t islands = 0;             ///< S7: islands with an own city
    std::int64_t land_points = 0;         ///< S8: the own share of all island land, 10,000 = all
    std::int64_t monuments = 0;           ///< S9: monuments the people wanted ("Zufriedenheit")
    std::int64_t monument_points = 0;     ///< S10: per hour of game time
    std::int64_t opponents_defeated = 0;  ///< S11
    std::int64_t defeat_points = 0;       ///< S12: 1500 each
    std::int64_t soldiers_killed = 0;     ///< S13
    std::int64_t soldiers_lost = 0;       ///< S14
    std::int64_t soldier_points = 0;      ///< S15: by kills and the share of won fights
    std::int64_t ships_sunk = 0;          ///< S16
    std::int64_t ships_lost = 0;          ///< S17
    std::int64_t ship_points = 0;         ///< S18
    std::int64_t bonus_percent = 0;       ///< S19: 100 for a winner
    std::int64_t bonus = 0;               ///< S20: the winner's points count twice
    std::int64_t total = 0;               ///< S21
    bool won = false;

    /// The points before the bonus.
    [[nodiscard]] std::int64_t subtotal() const noexcept { return total - bonus; }
};
[[nodiscard]] ScoreSheet score_sheet(const World& world, const assets::BuildingTable& buildings, PlayerId player);
[[nodiscard]] inline std::int64_t player_score(const World& world, const assets::BuildingTable& buildings,
                                               PlayerId player) {
    return score_sheet(world, buildings, player).total;
}

/// The curves of the score (0x00474F90's tables): city points per Einwohner / 20 (0..1000), and
/// points of soldiers and ships by their number of kills (0..200).
[[nodiscard]] std::uint8_t city_curve(std::size_t index) noexcept;
[[nodiscard]] std::int32_t fight_curve(std::size_t index) noexcept;

/// What names a game's high score list: the scenario it comes from (the NAME chunk, which
/// savegames keep, and CUSTOM for the player's own scenarios), its stars or part (SZENE_RANKING,
/// else TIMERS +0xBC), its numbers (SZENE_*, else TIMERS +0xA7) and the game id (TIMERS +0xAC,
/// else SZENE_GAMEID). Empty `name` when the world has no source (a test world).
struct GameIdentity {
    static constexpr std::uint32_t kNone = 0xFF;
    std::string name;     ///< Windows-1252, as the file names of Szenes/
    bool custom = false;  ///< a scenario of the player's own (its list is in "Eigene Szenarien")
    std::uint32_t game_id = 0;
    /// Stars of a scenario, the part of a campaign, the level of the endless game.
    std::uint32_t ranking = 0;
    std::uint32_t mission_number = kNone;  ///< SZENE_MISSNR, else TIMERS +0xA7
    std::uint32_t campaign = kNone;        ///< SZENE_KAMPAGNE
};
[[nodiscard]] GameIdentity game_identity(const World& world);

/// How the game ended for the player the screen is for.
enum class GameOutcome : std::uint8_t {
    Won,   ///< accomplished the mission (or met it when another did)
    Lost,  ///< defeated, or another player accomplished the mission
    Left,  ///< the player quit a game nobody had decided ("Ende" in the options)
};

/// One player's sheet on the end screen.
struct EndScreenRow {
    PlayerId player = kNoPlayer;
    std::string name;
    BannerId banner = 0;
    PlayerRole role = PlayerRole::Human;
    PlayerStatus status = PlayerStatus::Playing;
    ScoreSheet sheet;
};

/// Everything the end screen shows, gathered once when it opens.
struct EndScreenSummary {
    GameOutcome outcome = GameOutcome::Left;
    PlayerId local = kNoPlayer;
    std::uint64_t time_ms = 0;  ///< game time when the game ended
    GameIdentity game;
    /// Humans and computers that took part (not absent, defeated ones included), the local player
    /// first, then in the order of their slots.
    std::vector<EndScreenRow> rows;
};
[[nodiscard]] EndScreenSummary end_screen_summary(const World& world, const assets::BuildingTable& buildings,
                                                  PlayerId local, GameOutcome outcome);

}  // namespace o1602::sim
