// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the program is asked to do at the start (the command line, main.cpp), apart from the
// viewer itself so that the parser and its tests need no window.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "o1602/net/enet_transport.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/world_generator.hpp"

namespace o1602::app {

struct ViewerOptions {
    std::filesystem::path scenario;    ///< scenario (.szs / .szm), savegame (.gam) or Open1602 savegame (.o1602)
    std::uint32_t max_frames = 0;      ///< stop after N frames (0 = run until closed); for smoke tests
    std::filesystem::path screenshot;  ///< write the last frame as PNG before exiting
    std::filesystem::path save;        ///< save the game before exiting (.o1602, or .gam for the original)
    render::Zoom zoom = render::Zoom::Large;
    bool show_owners = false;  ///< start with the ownership view
    /// Show figures at the smallest zoom level from half-size graphics of the middle level (the
    /// original shows none there). Off: only the sheets the original has.
    bool derive_small_figures = true;
    /// The original red/blue/yellow/white flag images instead of the tinted generic flag.
    bool original_flags = false;
    /// The original's data without the corrections settings.cfg `data_fixes` switches on.
    bool original_data = false;
    /// The window's size (--size); 0: the size it had last time (settings.cfg), else 1280 x 800.
    std::int32_t width = 0;
    std::int32_t height = 0;
    sim::Rules rules;  ///< settings of the game (refund on demolishing, ...)
    /// Start a new endless game instead of loading `scenario`.
    std::optional<sim::NewGameOptions> new_game;
    /// How the islands of a scenario that makes them at the start are drawn (also --scenario
    /// Szenes/Endlosspiel00.szs).
    sim::WorldStart world;
    bool frame = true;    ///< the original's panel and status bar around the map
    bool effects = true;  ///< sounds and voice messages (also off in the original's Game.dat)
    bool music = true;
    bool videos = true;                  ///< the videos of messages, victory, defeat and pause
    bool intro = false;                  ///< the logos and the intro before the game (the original always shows them)
    std::optional<std::uint16_t> video;  ///< one video before the game (VideoSmk number)
    /// A message to show when the game starts (its id, "mymod:lighthouse"): to try a mod's messages.
    std::string message;
    /// The original's mouse: the left button gives orders too, the right one only cancels
    /// (ADR 0010: Open1602 orders with the right button).
    bool original_mouse = false;
    /// Ask before scuttling, disbanding and declaring war by an attack (ADR 0010).
    bool confirm = true;
    /// The key settings (ADR 0010); empty: keys.cfg in the user's data folder, written with the
    /// defaults if it is not there yet.
    std::filesystem::path keys_file;
    /// The language of Open1602's own texts ("de", "en", ...); empty: settings.cfg, else the
    /// language of the installation (ui/localization.hpp).
    std::string language;
    /// Start in the main menu instead of a game (no game was named on the command line).
    bool menu = false;
    bool performance_test = false; ///< explicit private flag: fixed saved scene and mouse path, no state writes
    /// The player's name given on the command line (--name, Windows-1252): the multiplayer menu
    /// starts with it for this run, until the person types another; empty: the name in the settings.
    std::string player_name;
    /// Mouse input played at given frames (smoke tests of the interface without a person).
    struct ScriptedInput {
        enum class Kind : std::uint8_t { Move, Down, Up, Key, Wheel, Text };
        Kind kind = Kind::Move;
        std::uint32_t frame = 0;
        float x = 0.0F;
        float y = 0.0F;
        std::int32_t key = 0;   ///< Key: SDL key code; Wheel: notches (up: zoom in)
        std::uint16_t mod = 0;  ///< Key, Down/Up: SDL_KMOD_CTRL, SDL_KMOD_SHIFT
        bool right = false;     ///< Down/Up: the right button
        std::string text;       ///< Text: typed as the platform's text input (UTF-8)
    };
    std::vector<ScriptedInput> input;
    /// Select the building on this island tile at the start (island index, x, y) and look at it.
    std::optional<std::array<std::uint16_t, 3>> select;
    std::optional<std::uint32_t> select_ship;     ///< index into the game's ships
    std::optional<std::uint32_t> select_soldier;  ///< index into the game's land units
    /// A multiplayer game (ADR 0011): host the game named, or join a host and play the game it sends.
    struct Multiplayer {
        enum class Role : std::uint8_t { Host, Join };
        Role role = Role::Host;
        bool public_room = false;
        std::string address;  ///< Join: the host's name or IPv4 address
        std::uint16_t port = net::kDefaultPort;
        std::string name;      ///< the player's name for the others; empty: the menu's
        std::string password;  ///< Join: the game's password, if it has one
    };
    std::optional<Multiplayer> multiplayer;
};

}  // namespace o1602::app
