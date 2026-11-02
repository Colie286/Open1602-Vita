// SPDX-License-Identifier: GPL-3.0-or-later
// The command line of open1602 (engine/app/src/command_line.hpp, BT-2).

#include <array>
#include <initializer_list>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "command_line.hpp"

using namespace o1602;

namespace {

core::Result<app::CommandLine> parse(std::initializer_list<const char*> args) {
    std::vector<const char*> argv{"open1602"};
    argv.insert(argv.end(), args.begin(), args.end());
    return app::parse_command_line(static_cast<int>(argv.size()), argv.data());
}

std::string error_of(std::initializer_list<const char*> args) {
    const auto result = parse(args);
    return result ? std::string() : result.error().message;
}

}  // namespace

TEST_CASE("command line: nothing given starts the main menu") {
    const auto none = parse({});
    REQUIRE(static_cast<bool>(none));
    CHECK(none->options.menu);
    CHECK(none->scenario == "Szenes/Das Atoll.szs");
    CHECK(!none->new_game);
    // A game named, a new game or a multiplayer game: no menu; --menu forces it.
    CHECK(!parse({"--scenario", "Szenes/Atoll.szs"})->options.menu);
    CHECK(!parse({"--new-game", "1"})->options.menu);
    CHECK(!parse({"--join", "example.org"})->options.menu);
    CHECK(parse({"--scenario", "a.szs", "--menu"})->options.menu);
    CHECK(!parse({"--menu", "--no-panel"})->options.menu);  // without the frame there is no menu
}

TEST_CASE("command line: options and their values") {
    const auto c = parse({"--game-dir", "/games/anno", "--load", "x.o1602", "--size",     "800x600",  "--zoom",
                          "2",          "--frames",    "30",     "--video", "58",         "--refund", "partial:25",
                          "--droughts", "rare",        "--ai",   "classic", "--no-sound", "--select", "3,10,20"});
    REQUIRE(static_cast<bool>(c));
    CHECK(c->game_dir == "/games/anno");
    CHECK(c->load == "x.o1602");
    CHECK(c->options.width == 800);
    CHECK(c->options.height == 600);
    CHECK(c->options.zoom == render::Zoom::Small);
    CHECK(c->options.max_frames == 30);
    CHECK(c->options.video == std::optional<std::uint16_t>(58));
    CHECK(c->options.rules.demolish_refund == sim::Refund::Partial);
    CHECK(c->options.rules.refund_percent == 25);
    CHECK(c->options.rules.droughts == sim::Droughts::Rare);
    CHECK(c->options.rules.bugs.classic_ai);
    CHECK(!c->options.effects);
    const std::array<std::uint16_t, 3> tile{3, 10, 20};
    CHECK(c->options.select == tile);
    // A run of N frames comes from a script: no dialog waits for a click (P29).
    CHECK(!c->dialogs);
    CHECK(parse({})->dialogs);
    CHECK(!parse({"--no-dialogs"})->dialogs);
}

TEST_CASE("command line: a run of N frames advances the game by the same time every frame") {
    // Screenshots of two runs with the same game and seed compare pixel by pixel.
    CHECK(app::fixed_frame_ms(parse({"--frames", "300"})->options) == app::kFixedFrameMs);
    CHECK(app::kFixedFrameMs == 16);
    // Without --frames the game goes by real time.
    CHECK(app::fixed_frame_ms(parse({})->options) == 0);
    CHECK(app::fixed_frame_ms(parse({"--frames", "0"})->options) == 0);
    // So does a multiplayer game: the other computers go by real time too.
    CHECK(app::fixed_frame_ms(parse({"--frames", "300", "--host"})->options) == 0);
    CHECK(app::fixed_frame_ms(parse({"--frames", "300", "--join", "example.org"})->options) == 0);
}

TEST_CASE("command line: how often disasters come and what they do to the speed") {
    const auto c = parse({"--volcanoes", "rare", "--plagues", "off", "--fires", "rare", "--event-speed", "attacks"});
    REQUIRE(static_cast<bool>(c));
    CHECK(c->options.rules.volcanoes == sim::Volcanoes::Rare);
    CHECK(c->options.rules.plagues == sim::Outbreaks::Off);
    CHECK(c->options.rules.fires == sim::Outbreaks::Rare);
    CHECK(c->options.rules.event_speed == sim::EventSpeed::Attacks);
    CHECK(parse({"--event-speed", "keep"})->options.rules.event_speed == sim::EventSpeed::Keep);
    CHECK(!parse({"--plagues", "often"}));
    CHECK(!parse({"--event-speed", "fast"}));
}

TEST_CASE("command line: a new game with its own world, and the player's name") {
    const auto c = parse({"--name", "J\xc3\xbcrgen", "--new-game", "2", "--layout", "1", "--seed", "123", "--world",
                          "300x400", "--players", "6", "--host", "--port", "2700"});
    REQUIRE(static_cast<bool>(c));
    REQUIRE(c->new_game.has_value());
    CHECK(c->new_game->difficulty == 2);
    CHECK(c->new_game->name == "J\xfcrgen");  // Windows-1252, as the game keeps names
    REQUIRE(c->new_game->custom.has_value());
    CHECK(c->new_game->custom->width == 300);
    CHECK(c->new_game->custom->height == 400);
    CHECK(c->new_game->custom->players == 6);
    CHECK(c->layout == std::optional<std::uint8_t>(1));
    CHECK(c->seed == std::optional<std::uint64_t>(123));
    REQUIRE(c->options.multiplayer.has_value());
    CHECK(c->options.multiplayer->role == app::ViewerOptions::Multiplayer::Role::Host);
    CHECK(c->options.multiplayer->port == 2700);
    CHECK(c->options.multiplayer->name == "J\xfcrgen");
    CHECK(c->options.player_name == "J\xfcrgen");
    CHECK(c->new_game->custom->shapes == sim::WorldSettings::Shapes::Templates);
    // An own world is a new game.
    CHECK(error_of({"--world", "300x400"}).find("--new-game") != std::string::npos);
    const auto own = parse({"--new-game", "0", "--island-shapes", "own"});
    REQUIRE(static_cast<bool>(own));
    REQUIRE(own->new_game.has_value());
    REQUIRE(own->new_game->custom.has_value());
    CHECK(own->new_game->custom->shapes == sim::WorldSettings::Shapes::Own);
    CHECK(error_of({"--new-game", "0", "--island-shapes", "round"}) == "bad value for --island-shapes: round");
}

TEST_CASE("command line: the player's name alone is the multiplayer menu's for this run") {
    const auto c = parse({"--name", "Merlin"});
    REQUIRE(static_cast<bool>(c));
    CHECK(c->options.menu);
    CHECK(c->options.player_name == "Merlin");
    CHECK(!c->options.multiplayer.has_value());
    CHECK(parse({})->options.player_name.empty());
}

TEST_CASE("command line: the landscape of islands of their own shape, by preset and one by one") {
    const auto c = parse({"--new-game", "0", "--island-preset", "archipelago", "--island-max", "80", "--island-coast",
                          "smooth", "--island-forest", "much"});
    REQUIRE(static_cast<bool>(c));
    REQUIRE(c->new_game.has_value());
    REQUIRE(c->new_game->custom.has_value());
    const sim::WorldSettings& w = *c->new_game->custom;
    CHECK(w.shapes == sim::WorldSettings::Shapes::Own);  // a preset is one of islands of their own shape
    CHECK(w.landscape.land == 20);
    CHECK(w.landscape.min_side == 16);
    CHECK(w.landscape.max_side == 80);
    CHECK(w.landscape.coast == sim::shapes::Roughness::Smooth);
    CHECK(w.landscape.forest == sim::shapes::Share::Much);
    CHECK(error_of({"--new-game", "0", "--island-preset", "atlantis"}) == "bad value for --island-preset: atlantis");
    CHECK(error_of({"--new-game", "0", "--island-land", "60"}) == "bad value for --island-land: 60");
    CHECK(error_of({"--new-game", "0", "--island-gap", "huge"}) == "bad value for --island-gap: huge");
}

TEST_CASE("command line: wrong values are named, not taken in part") {
    // --video used to read "abc" as 0 and wrap 70000 (BT-2).
    CHECK(error_of({"--video", "abc"}) == "bad value for --video: abc");
    CHECK(error_of({"--video", "70000"}) == "bad value for --video: 70000");
    CHECK(error_of({"--frames", "12abc"}) == "bad value for --frames: 12abc");
    CHECK(error_of({"--seed", "1x"}) == "bad value for --seed: 1x");
    CHECK(error_of({"--zoom", "3"}) == "bad value for --zoom: 3");
    CHECK(error_of({"--join", "host:0"}) == "bad value for --join: host:0");
    CHECK(error_of({"--players", "13"}) == "bad value for --players: 13");
    CHECK(error_of({"--bogus"}) == "unknown option or missing value: --bogus");
    CHECK(error_of({"--zoom"}) == "unknown option or missing value: --zoom");
}

TEST_CASE("command line: scripted input") {
    std::vector<app::ViewerOptions::ScriptedInput> in;
    REQUIRE(app::parse_input("move 10,20@1;down 10,20@2;rup 5,6@3;key 27+ctrl@4;wheel 1,2,-3@5;text Ab@6", in));
    REQUIRE(in.size() == 6);
    using Kind = app::ViewerOptions::ScriptedInput::Kind;
    CHECK(in[0].kind == Kind::Move);
    CHECK(in[1].kind == Kind::Down);
    CHECK(in[2].kind == Kind::Up);
    CHECK(in[2].right);
    CHECK(in[3].kind == Kind::Key);
    CHECK(in[3].key == 27);
    CHECK(in[3].mod == app::kModCtrl);
    CHECK(in[4].kind == Kind::Wheel);
    CHECK(in[4].key == -3);
    CHECK(in[5].text == "Ab");
    CHECK(in[5].frame == 6);
    // A click with Shift held.
    std::vector<app::ViewerOptions::ScriptedInput> held;
    REQUIRE(app::parse_input("down+shift 7,8@1;up+shift 7,8@2;down 7,8@3", held));
    REQUIRE(held.size() == 3);
    CHECK(held[0].kind == Kind::Down);
    CHECK(held[0].mod == app::kModShift);
    CHECK(held[1].kind == Kind::Up);
    CHECK(held[1].mod == app::kModShift);
    CHECK(held[2].mod == 0);
    // Several modifiers, in any order (the cheats' Ctrl+Shift+Alt+W).
    std::vector<app::ViewerOptions::ScriptedInput> chord;
    REQUIRE(app::parse_input("key 119+ctrl+shift+alt@1;down+alt+ctrl 1,2@2", chord));
    REQUIRE(chord.size() == 2);
    CHECK(chord[0].key == 119);
    CHECK(chord[0].mod == (app::kModCtrl | app::kModShift | app::kModAlt));
    CHECK(chord[1].kind == Kind::Down);
    CHECK(chord[1].mod == (app::kModCtrl | app::kModAlt));
    std::vector<app::ViewerOptions::ScriptedInput> bad;
    CHECK(!app::parse_input("jump 1,2@3", bad));
    CHECK(!app::parse_input("jump+shift 1,2@3", bad));
    CHECK(!app::parse_input("move 1,2@x", bad));
    CHECK(!error_of({"--input", "move 1@2"}).empty());
}
