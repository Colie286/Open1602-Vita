// SPDX-License-Identifier: GPL-3.0-or-later
//
// A new endless game as the original starts it (docs/re/newgame.md): the mission screen picks the
// difficulty, the scenario list one of three map layouts; the game loads Endlosspiel<D><V>.szs,
// which carries the world plan, four start ships, the money and the settings of that difficulty,
// makes the islands (world_generator.hpp) and resets what it does not take from the file: the
// players' control, colour and names, the game time and the clocks of droughts and volcanoes.
// A multiplayer endless game loads Endlosspiel<D><V>.szm instead: maps of their own, and every
// player starts with the small trading cog. Those are the German game's names; other languages
// name the files their own way (find_endless_file).
//
// Open1602 builds the scenario as the original would have it after that reset and loads it like
// any other, so the savegame writer has the right records to start from.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/sim/world_generator.hpp"
#include "o1602/sim/world_plan.hpp"

namespace o1602::sim {

struct NewGameOptions {
    static constexpr std::uint8_t kDifficulties = 4;  ///< [ENDLESSKIND]: Einfach .. Schwer
    static constexpr std::uint8_t kLayouts = 3;       ///< map layouts per difficulty
    std::uint8_t difficulty = 0;                      ///< first digit of the file: money, resources, pirates
    std::uint8_t layout = 0;                          ///< second digit: the original rolls rand() % 3
    std::uint8_t colour = 0;                          ///< the human's banner, 0 red, 1 blue, 2 yellow, 3 white
    std::string name;                                 ///< the human's name (Windows-1252, at most 63 bytes)
    /// Identifier linking savegames and high scores (TIMERS +0xAC); new_game_id() makes one from
    /// the clock as the original does.
    std::uint32_t game_id = 0;
    /// A multiplayer game: the level's .szm file, in which every player has the small trading cog
    /// (the single player file gives the computers in slots 2 and 3 the large one).
    bool multiplayer = false;
    /// How the islands are drawn; the original takes the RANDTAB from index rand() % 512.
    WorldStart world;
    /// An own world instead of the endless file's plan (world_plan.hpp): its size, islands and
    /// start areas, drawn from `world.seed`. Players, money and ships come from the level's file;
    /// `layout` does not matter then. Players beyond four are computers in slots 7 on (add_players).
    std::optional<WorldSettings> custom;
};

/// The original's game id (0x00469410): year % 8, month, day, hour, minute and 8 random bits.
[[nodiscard]] std::uint32_t new_game_id(std::uint32_t year, std::uint32_t month, std::uint32_t day, std::uint32_t hour,
                                        std::uint32_t minute, std::uint32_t random) noexcept;

/// The map layout a world number stands for: the same number, the same map, in single player
/// (--seed) as in multiplayer.
[[nodiscard]] constexpr std::uint8_t layout_for_seed(std::uint64_t seed) noexcept {
    return static_cast<std::uint8_t>(seed % NewGameOptions::kLayouts);
}

/// File of a difficulty and layout, relative to the game directory: Szenes/Endlosspiel<D><V>.szs,
/// or .szm for a multiplayer game.
[[nodiscard]] std::string endless_file(std::uint8_t difficulty, std::uint8_t layout, bool multiplayer = false);

/// The German game's name of the endless scenario, Endlosspiel (the files' name without digits).
inline constexpr std::string_view kEndlessName = "Endlosspiel";

/// An endless game's file in an installation and the scenario's name in its language.
struct EndlessFile {
    std::filesystem::path path;
    std::string name;  ///< the file's name without the digits, in Windows-1252: "Endlosspiel"
};

/// The file of a difficulty and layout in `game`: endless_file() as the German game names it, else
/// the file of the same two digits in another language's name. The English game's endless levels
/// are not called Endlosspiel; their name is that of the .szs files in Szenes whose names end in two
/// digits and whose scenario is the endless game (SZENE_MISSNR 1), as the scenario list finds them.
[[nodiscard]] core::Result<EndlessFile> find_endless_file(const assets::GameDirectory& game, std::uint8_t difficulty,
                                                          std::uint8_t layout, bool multiplayer = false);

/// The endless scenario as the original has it after the reset of a new game: control, colour and
/// names of the players, the gated TIMERS fields, no editor leftovers in the ships, NAME
/// `name` (the scenario's name in the installation's language, EndlessFile::name). The world plan
/// stays for load_world.
[[nodiscard]] core::Result<assets::ChunkFile> new_game_scenario(const assets::ChunkFile& endless,
                                                                const assets::TextTable& text,
                                                                const NewGameOptions& options,
                                                                std::string_view name = kEndlessName);

/// Players five to `players` of an own world: computers as the level's second player, with its
/// money and a copy of its ship at their starts, in the slots player_slot() names and with banners
/// from kFirstExtraBanner on. Nothing for four players or fewer.
core::Status add_players(World& world, const WorldPlan& plan, std::uint8_t players);

/// Gives player `who` banner `banner`: the colour screen offers all of kMaxPlayers colours, a new
/// game's file only the original's four (NewGameOptions::colour). The player that had it, if any,
/// takes `who`'s old one, as a computer's in the multiplayer lobby; it is returned, nothing if
/// nobody had it or `who` is no player.
std::optional<PlayerId> take_banner(World& world, PlayerId who, BannerId banner);

/// The scenario of an own world: the players' ships at the plan's starts (none for players beyond
/// `players`), the map size if it is not the original's. The islands are still to be made.
[[nodiscard]] core::Result<assets::ChunkFile> own_world_scenario(const assets::ChunkFile& scenario,
                                                                 const WorldPlan& plan, std::uint8_t players);

/// Starts a new endless game: the scenario of new_game_scenario() with its islands made.
[[nodiscard]] core::Result<World> start_new_game(const assets::GameDirectory& game,
                                                 const assets::BuildingTable& buildings, const assets::TextTable& text,
                                                 const NewGameOptions& options,
                                                 std::vector<std::string>* warnings = nullptr);

}  // namespace o1602::sim
