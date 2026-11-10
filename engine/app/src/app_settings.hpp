// SPDX-License-Identifier: GPL-3.0-or-later
//
// Open1602's own settings and savegame names in the user's data folder (never Game.dat, never the
// installation): settings.cfg with the sounds of the panels MUSIK and ANNO, and saves/slots.cfg with
// the names of the savegames of the panel DISK (the original has twelve, Open1602 any number).
// Both files are UTF-8 (a byte order mark is passed over); the names are converted to and from the
// Windows-1252 the game keeps them in. Files of older versions with Windows-1252 bytes still read.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "display_settings.hpp"
#include "endless_setup.hpp"

#include "o1602/audio/game_audio.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/save_game.hpp"

namespace o1602::app {

struct AppSettings {
    audio::AudioSettings audio;
    bool logging = false;  ///< Vita diagnostics; read before runtime startup, effective next launch.
    bool mission_window = false;  ///< "Auftrag ansehen"
    /// The chapter of the tutorial chosen last, or the next one after „Einführungsspiel beenden“
    /// (the original's "Tutornr"): the tutorial runs this chapter, the menu offers it first.
    std::uint8_t tutorial_chapter = 0;
    std::string player_name;  ///< the name typed in the menu (Windows-1252)
    /// The installation chosen in the folder dialog at the start (UTF-8); "" if none was chosen.
    std::string game_folder;
    /// The language of Open1602's own texts ("de", "en", ...); "auto": the installation's.
    std::string language = "auto";
    /// Optional public lobby/relay endpoint, hostname or IPv4 with optional UDP port (16022).
    std::string master_server;
    /// Whales, dolphins and krakens: on average one per screen of sea every this many seconds
    /// (the original: 5); 0 turns them off. Only the picture, the game does not depend on it.
    std::int32_t sea_creatures_every = 60;
    /// The corrections of the original's data (assets/data_fixes.hpp, ui::Font::fix_accents): the
    /// bakery's sound, the pirate Kontor's flag, the block formation, the accents of capitals.
    bool data_fixes = true;
    /// Multiplayer hosts joined by address, the latest first ("name[:port]"); at most kMaxHosts.
    std::vector<std::string> remembered_hosts;
    static constexpr std::size_t kMaxHosts = 8;
    /// Packages of the mods folder turned off in the menu "Mods" (their ids, UTF-8): new games do
    /// not use them (docs/modding/packages.md).
    std::vector<std::string> disabled_mods;
    /// The last settings chosen in the start dialog of the endless game.
    EndlessSetup endless;
    /// The last rules chosen for a scenario (its world is given, only the rules count).
    EndlessSetup scenario;
    /// The last settings a host chose for a multiplayer game.
    EndlessSetup multiplayer = EndlessSetup::defaults(SetupScope::MultiplayerEndless);
    DisplaySettings display;
};

/// "key = value" lines; unknown keys are passed over, missing ones keep `defaults`.
[[nodiscard]] AppSettings parse_settings(std::string_view text, const AppSettings& defaults);
[[nodiscard]] std::string write_settings(const AppSettings& settings);
/// Internal defaults -> optional Vita menu-preferences.cfg -> manual settings.cfg.
/// Reading never creates or migrates either file. Desktop keeps its original single file.
[[nodiscard]] AppSettings load_user_settings(const std::filesystem::path& folder,
    const AppSettings& defaults, bool separate_preferences);
/// Persist only changed menu values; explicit configuration is never copied into preferences.
/// Existing values behind an explicit override are retained, so removing it restores the menu choice.
[[nodiscard]] std::string menu_preferences_text(const AppSettings& settings,
    std::string_view previous, std::string_view overrides);

/// `text` (a settings file) with the line of `key` set to `value`: the first such line is replaced
/// (any others go), or the line is added at the end; every other line stays as it is.
[[nodiscard]] std::string set_setting_line(std::string_view text, std::string_view key, std::string_view value);

/// The savegames of the panel DISK: a name and the number of human players per slot. The original
/// has twelve slots; Open1602 keeps as many as are used plus one free slot at the end, at least
/// twelve, so a new savegame always has a place. Slot kLastGameSlot is the game kept on leaving it.
struct SaveSlots {
    static constexpr std::size_t kMinCount = 12;
    static constexpr std::size_t kMaxCount = 10'000;                       ///< a damaged file cannot ask for more
    std::vector<std::string> names = std::vector<std::string>(kMinCount);  ///< Windows-1252, empty = free
    std::vector<std::uint8_t> humans = std::vector<std::uint8_t>(kMinCount);
    /// What each savegame's section META says (not in slots.cfg; read_slot_meta reads the files).
    std::vector<std::optional<sim::SaveMeta>> meta = std::vector<std::optional<sim::SaveMeta>>(kMinCount);

    [[nodiscard]] std::size_t size() const noexcept { return names.size(); }
    /// Names a slot (beyond the list: the list grows) and keeps one free slot after the last used.
    void set(std::size_t slot, std::string name, std::uint8_t human_players);
};
/// The game kept when a game ends ("Ende"): lastgame.o1602, like the original's and the History
/// Edition's lastgame.gam. The original lists it nowhere (only "Spiel fortsetzen" loads it);
/// Open1602 keeps it in the first slot of every savegame list. It loads like any other savegame
/// but is never named or saved into by hand.
inline constexpr std::size_t kLastGameSlot = 0;
inline constexpr std::string_view kLastGameName = "lastgame";
/// Lines "slot humans name" (slot from 0).
[[nodiscard]] SaveSlots parse_slots(std::string_view text);
[[nodiscard]] std::string write_slots(const SaveSlots& slots);
/// File of a slot in the savegame folder: lastgame.o1602 (kLastGameSlot), spiel01.o1602, ...,
/// spiel100.o1602, ...
[[nodiscard]] std::filesystem::path slot_file(const std::filesystem::path& folder, std::size_t slot);
/// Names kLastGameSlot after its file, or frees it when there is none. An older slots.cfg may hold
/// a savegame of its own there (spiel00.o1602): it gives way and its file stays where it is.
void find_last_game(SaveSlots& slots, const std::filesystem::path& folder);
/// The META sections of the savegames of the named slots (only the head of each file is unpacked);
/// with `only`, of that slot alone.
void read_slot_meta(SaveSlots& slots, const std::filesystem::path& folder,
                    std::optional<std::size_t> only = std::nullopt);
/// A savegame in the lists: the player, the time played and the size of the map.
[[nodiscard]] std::string save_details(const sim::SaveMeta& meta);
/// save_details of every slot ("" where META is unknown).
[[nodiscard]] std::vector<std::string> slot_details(const SaveSlots& slots);
/// Game time as hours and minutes.
[[nodiscard]] std::string save_time(std::uint64_t ms);

using core::utf8_to_cp1252;

}  // namespace o1602::app
