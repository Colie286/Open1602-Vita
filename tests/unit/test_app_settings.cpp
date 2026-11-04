// SPDX-License-Identifier: GPL-3.0-or-later
// Open1602's own settings files (engine/app/src/app_settings.hpp): settings.cfg and slots.cfg are
// UTF-8, the names in the game Windows-1252.

#include <filesystem>
#include <fstream>
#include <string>

#include "../test_framework.hpp"
#include "app_settings.hpp"
#include "high_score_file.hpp"

using namespace o1602;

TEST_CASE("app settings: names are written as UTF-8 and read back") {
    app::AppSettings s;
    s.player_name = "J\xfcrgen";  // Windows-1252, as typed in the menu
    s.remembered_hosts = {"example.org:2601"};
    const std::string text = app::write_settings(s);
    CHECK(text.find("player_name = J\xc3\xbcrgen\n") != std::string::npos);
    CHECK(text.find("UTF-8") != std::string::npos);
    const app::AppSettings back = app::parse_settings(text, app::AppSettings{});
    CHECK(back.player_name == "J\xfcrgen");
    CHECK(back.remembered_hosts.size() == 1);
    CHECK(back.remembered_hosts[0] == "example.org:2601");
}

TEST_CASE("app settings: the player ID is kept as it was drawn") {
    app::AppSettings s;
    CHECK(app::write_settings(s).find("player_id") == std::string::npos);  // not drawn yet
    s.player_id = 0x00C0FFEE12345678ULL;
    const std::string text = app::write_settings(s);
    CHECK(text.find("player_id = 00c0ffee12345678\n") != std::string::npos);
    CHECK(app::parse_settings(text, {}).player_id == s.player_id);
    // Upper case reads too; nonsense and 0 keep what there was.
    CHECK(app::parse_settings("player_id = 00C0FFEE12345678\n", {}).player_id == s.player_id);
    app::AppSettings kept;
    kept.player_id = 7;
    CHECK(app::parse_settings("player_id = xyz\n", kept).player_id == 7);
    CHECK(app::parse_settings("player_id = 0\n", kept).player_id == 7);
    CHECK(app::parse_settings("player_id = 1234567890abcdef0\n", kept).player_id == 7);  // 17 digits
    // Drawn at random, never 0; set_setting_line adds it to a file without changing the rest.
    const std::uint64_t a = app::new_player_id();
    CHECK(a != 0);
    CHECK(a != app::new_player_id());
    const std::string file = app::set_setting_line("music = off\n", "player_id", app::player_id_text(a));
    CHECK(file.starts_with("music = off\n"));
    CHECK(app::parse_settings(file, {}).player_id == a);
}

TEST_CASE("app settings: the mods turned off, each once") {
    app::AppSettings s;
    s.disabled_mods = {"leuchtturm", "wetter"};
    const app::AppSettings back = app::parse_settings(app::write_settings(s) + "disabled_mod = wetter\n", {});
    REQUIRE(back.disabled_mods.size() == 2);
    CHECK(back.disabled_mods[0] == "leuchtturm");
    CHECK(back.disabled_mods[1] == "wetter");
    CHECK(app::write_settings(app::AppSettings{}).find("disabled_mod") == std::string::npos);
}

TEST_CASE("app settings: a byte order mark and files of older versions") {
    // Saved by an editor with a byte order mark: the first key still counts.
    const app::AppSettings bom =
        app::parse_settings("\xef\xbb\xbfmusic = off\nplayer_name = \xc3\x84gir\n", app::AppSettings{});
    CHECK(!bom.audio.music);
    CHECK(bom.player_name == "\xc4gir");
    // Older versions wrote the Windows-1252 bytes as they were: not UTF-8, so taken as they are.
    const app::AppSettings old = app::parse_settings("player_name = J\xfcrgen\n", app::AppSettings{});
    CHECK(old.player_name == "J\xfcrgen");
    // What Windows-1252 cannot show is left out.
    const app::AppSettings wide = app::parse_settings("player_name = A\xe2\x86\x92Z\n", app::AppSettings{});
    CHECK(wide.player_name == "AZ");
}

TEST_CASE("app settings: the trade routes' voice switch is kept; older files leave it on") {
    app::AppSettings s;
    s.audio.speech[audio::kSpeechRoutes] = false;
    const app::AppSettings back = app::parse_settings(app::write_settings(s), app::AppSettings{});
    CHECK(!back.audio.speech[audio::kSpeechRoutes]);
    CHECK(back.audio.speech[7]);
    // Written before the switch: eight kinds.
    const app::AppSettings old = app::parse_settings("speech = 1 1 1 1 1 1 1 0\n", app::AppSettings{});
    CHECK(!old.audio.speech[7]);
    CHECK(old.audio.speech[audio::kSpeechRoutes]);
}

TEST_CASE("app settings: savegame names are written as UTF-8 and read back") {
    app::SaveSlots slots;
    slots.set(0, "Fu\xdf", 1);  // sharp s
    slots.set(2, "plain", 2);
    const std::string text = app::write_slots(slots);
    CHECK(text.find("0 1 Fu\xc3\x9f\n") != std::string::npos);
    const app::SaveSlots back = app::parse_slots("\xef\xbb\xbf" + text);
    CHECK(back.names[0] == "Fu\xdf");
    CHECK(back.humans[0] == 1);
    CHECK(back.names[2] == "plain");
    // A file of an older version.
    CHECK(app::parse_slots("1 1 M\xfchle\n").names[1] == "M\xfchle");
}

TEST_CASE("app settings: a savegame name with control or undefined bytes reads back as written") {
    // Found by the settings fuzzer: a name that ended in an undefined byte was written with a
    // trailing space, which the next read trimmed away.
    const std::string input("8 [ \x01\x90\0A\x90 \x90", 11);
    const app::SaveSlots slots = app::parse_slots(input);
    CHECK(slots.names[8] == "A");
    const std::string text = app::write_slots(slots);
    CHECK(app::write_slots(app::parse_slots(text)) == text);
    CHECK(app::parse_slots("3 1  A\tB\x81 \n").names[3] == "AB");
}

TEST_CASE("app settings: names typed in the game are written as they read back (FB-36)") {
    // A trailing space, a carriage return or an undefined byte used to take up to three rounds
    // of writing and reading before the name stayed the same.
    app::AppSettings s;
    s.player_name = std::string("Spi \x81\r\x8f", 7);
    s.remembered_hosts = {" host:2601 ", "\x90"};
    const std::string text = app::write_settings(s);
    const app::AppSettings back = app::parse_settings(text, app::AppSettings{});
    CHECK(back.player_name == "Spi");
    REQUIRE(back.remembered_hosts.size() == 1);
    CHECK(back.remembered_hosts[0] == "host:2601");
    CHECK(app::write_settings(back) == text);

    app::SaveSlots slots;
    slots.set(1, " Fu\xdf\x9d ", 1);
    slots.set(2, "\x01", 1);
    const std::string list = app::write_slots(slots);
    CHECK(list.find("1 1 Fu\xc3\x9f\n") != std::string::npos);
    CHECK(list.find("\n2 ") == std::string::npos);
    CHECK(app::write_slots(app::parse_slots(list)) == list);
}

TEST_CASE("app settings: what a savegame's META says, as a line of the lists") {
    app::SaveSlots slots;
    slots.set(20, "late", 1);
    CHECK(slots.meta.size() == slots.names.size());
    CHECK(app::slot_details(slots)[20].empty());  // no META read yet
    slots.meta[20] = sim::SaveMeta{500, 350, (2 * 60 + 5) * 60'000ULL + 59'999, "Merlin", std::nullopt};
    CHECK(app::save_time(slots.meta[20]->time_ms) == "2:05 h");
    CHECK(app::slot_details(slots)[20] == "Merlin, played 2:05 h, map 500 x 350");
    slots.meta[20]->player.clear();
    CHECK(app::save_details(*slots.meta[20]).starts_with("-, "));
    // A game changed in the editor says so.
    slots.meta[20]->edited = true;
    CHECK(app::save_details(*slots.meta[20]).ends_with("map 500 x 350, edited"));
}

TEST_CASE("app settings: a game changed in the editor has no high score list") {
    sim::GameIdentity game;
    game.name = "Zwei K\xfcsten";
    CHECK(app::high_score_file(game, {}, false) == std::filesystem::path(u8"Zwei K\u00fcsten.hss"));
    game.edited = true;
    CHECK(app::high_score_file(game, {}, false).empty());
    CHECK(app::high_score_file(game, {}, true).empty());
}

TEST_CASE("app settings: the first slot is the last game, kept on leaving a game") {
    const std::filesystem::path folder = test::temp_path("o1602-test-lastgame");
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    CHECK(app::slot_file(folder, app::kLastGameSlot) == folder / "lastgame.o1602");
    CHECK(app::slot_file(folder, 1) == folder / "spiel01.o1602");

    // An older slots.cfg had a savegame of its own in the first slot: without lastgame it is free.
    app::SaveSlots slots = app::parse_slots("0 1 Old\n1 1 Kept\n");
    app::find_last_game(slots, folder);
    CHECK(slots.names[app::kLastGameSlot].empty());
    CHECK(slots.names[1] == "Kept");

    std::ofstream(folder / "lastgame.o1602") << "x";
    slots = app::parse_slots("0 1 Old\n1 1 Kept\n");
    app::find_last_game(slots, folder);
    CHECK(slots.names[app::kLastGameSlot] == "lastgame");
    CHECK(slots.humans[app::kLastGameSlot] == 0);
    // As written after "Ende": the humans stay.
    slots = app::parse_slots("0 2 lastgame\n");
    app::find_last_game(slots, folder);
    CHECK(slots.humans[app::kLastGameSlot] == 2);
    std::filesystem::remove_all(folder);
}
