// SPDX-License-Identifier: GPL-3.0-or-later
// The data files of content packages (core/data_file.hpp) and the messages a mod defines with them
// (app/message_table.hpp, docs/modding/messages.md).

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"
#include "message_table.hpp"

#include "o1602/assets/game_content.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/file.hpp"

using namespace o1602;

namespace {

/// A package folder under the system's temporary folder, removed again at the end.
struct TempPackage {
    std::filesystem::path path;
    explicit TempPackage(std::string_view name) : path(test::temp_path("o1602_mods_test") / std::string(name)) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempPackage() {
        std::error_code ec;
        std::filesystem::remove_all(path.parent_path(), ec);
    }
    TempPackage(const TempPackage&) = delete;
    TempPackage& operator=(const TempPackage&) = delete;

    void write(std::string_view name, std::string_view text) const {
        std::ofstream out(path / name, std::ios::binary);
        out << text;
    }
    /// A short 8-bit mono WAVE file.
    void write_wave(std::string_view name) const {
        std::vector<std::uint8_t> v;
        const auto put = [&](std::uint32_t x, int bytes) {
            for (int i = 0; i < bytes; ++i) {
                v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
            }
        };
        const auto tag = [&](const char* t) { v.insert(v.end(), t, t + 4); };
        const std::uint32_t frames = 441;
        tag("RIFF");
        put(4 + 8 + 16 + 8 + frames, 4);
        tag("WAVE");
        tag("fmt ");
        put(16, 4);
        put(1, 2);
        put(1, 2);
        put(22050, 4);
        put(22050, 4);
        put(1, 2);
        put(8, 2);
        tag("data");
        put(frames, 4);
        v.insert(v.end(), frames, 128);
        REQUIRE(core::write_file(path / name, v).ok());
    }
};

}  // namespace

TEST_CASE("data file: tables, keys and the three kinds of value") {
    const auto tables = core::parse_data_file(
        "\xEF\xBB\xBF# A mod's messages\n"
        "[lighthouse]\n"
        "text = \"The lighthouse shines. # not a comment\"\n"
        "text.de = 'Der Leuchtturm strahlt \\o/'  # a comment\n"
        "like = 0x2A\n"
        "count = -1_000\n"
        "video_always = true\r\n"
        "\n"
        "[\"anno1602:42\"]\n"
        "\"quoted key\" = \"\\u00e4\\n\\\"\"\n");
    REQUIRE(tables.ok());
    REQUIRE(tables->size() == 2);
    const core::DataTable& t = (*tables)[0];
    CHECK(t.name == "lighthouse");
    CHECK(t.line == 2);
    REQUIRE(t.find("text") != nullptr);
    CHECK(*t.find("text")->string() == "The lighthouse shines. # not a comment");
    CHECK(*t.find("text.de")->string() == "Der Leuchtturm strahlt \\o/");
    CHECK(*t.find("like")->integer() == 0x2A);
    CHECK(*t.find("count")->integer() == -1000);
    CHECK(*t.find("video_always")->boolean());
    CHECK(t.find("video_always")->line == 7);
    const core::DataTable& o = (*tables)[1];
    CHECK(o.name == "anno1602:42");
    CHECK(*o.find("quoted key")->string() == "\xC3\xA4\n\"");
}

TEST_CASE("data file: what is not supported is an error with its line, never a guess") {
    const auto error_line = [](std::string_view text) {
        const auto r = core::parse_data_file(text);
        return r.ok() ? std::string("ok") : r.error().message.substr(0, r.error().message.find(':'));
    };
    CHECK(error_line("[a]\nx = unquoted\n") == "line 2");
    CHECK(error_line("[a]\nx = 1.5\n") == "line 2");
    CHECK(error_line("[a]\nx = [1, 2]\n") == "line 2");
    CHECK(error_line("[a]\nx = { y = 1 }\n") == "line 2");
    CHECK(error_line("[a]\nx = \"open\n") == "line 2");
    CHECK(error_line("[a]\nx = \"\"\"long\"\"\"\n") == "line 2");
    CHECK(error_line("[a]\nx = 1\nx = 2\n") == "line 3");
    CHECK(error_line("[a]\n[a]\n") == "line 2");
    CHECK(error_line("[[a]]\n") == "line 1");
    CHECK(error_line("[a]\nx = 1 2\n") == "line 2");
    CHECK(error_line("[a]\nx = 99999999999999999999\n") == "line 2");
    CHECK(error_line("[a]\nx\n") == "line 2");
    CHECK(!core::parse_data_file("x = \"\xFF\"\n").ok());  // not UTF-8
    // Fields before the first table belong to the nameless one.
    const auto top = core::parse_data_file("x = 1\n[a]\n");
    REQUIRE(top.ok());
    CHECK(top->size() == 2);
    CHECK((*top)[0].name.empty());
}

TEST_CASE("messages: the original's messages at their MELDUNG numbers") {
    const app::MessageTable& t = app::MessageTable::original();
    CHECK(t.size() == 0x70);
    CHECK(t.find("anno1602:42") == std::optional<app::MessageId>(42));
    CHECK(t.find("anno1602:0x2A") == std::optional<app::MessageId>(42));
    CHECK(!t.find("anno1602:0x70"));
    CHECK(!t.find("mymod:lighthouse"));
    // Plague: its own icon, voices and video from Speech8 282 and VideoSmk 282, normal speed.
    const app::MessageDefinition* plague = t.get(1);
    REQUIRE(plague != nullptr);
    CHECK(plague->icon == app::MessageIcon::Plague);
    CHECK(plague->meldung == std::optional<std::uint16_t>(1));
    CHECK(plague->playback.voice_base == 282);
    CHECK(plague->playback.video_base == 282);
    CHECK((plague->playback.flags & audio::kMessageNormalSpeed) != 0);
    CHECK(t.get(5)->icon == app::MessageIcon::Combat);  // pirates: kind 5
    // The icons as in the original: a rival's new city and ship the exclamation mark, treaties the
    // scroll, a computer's answer to a founding the mouth, a landing the exclamation mark.
    CHECK(t.get(0x0E)->icon == app::MessageIcon::Notice);
    CHECK(t.get(0x0B)->icon == app::MessageIcon::Notice);
    CHECK(t.get(0x10)->icon == app::MessageIcon::Notice);
    CHECK(t.get(0x0F)->icon == app::MessageIcon::Combat);
    CHECK(t.get(0x4D)->icon == app::MessageIcon::Scroll);
    CHECK(t.get(0x49)->icon == app::MessageIcon::Player);
    CHECK(t.text(1, "de").empty());  // the status bar shows MELDUNG 1
}

TEST_CASE("messages: a mod adds a message with text, icon, sound and video") {
    TempPackage pkg("lighthouse_mod");
    pkg.write_wave("horn.wav");
    app::MessageTable t;
    std::vector<std::string> warnings;
    const auto status = t.load_package("lighthouse_mod",
                                       "[lit]\n"
                                       "text = \"{player}'s lighthouse shines over {island}.\"\n"
                                       "text.de = \"Der Leuchtturm von {player} strahlt über {island}.\"\n"
                                       "icon = \"scroll\"\n"
                                       "kind = \"events\"\n"
                                       "sound = \"horn.wav\"\n"
                                       "video = 23\n"
                                       "normal_speed = true\n",
                                       pkg.path, warnings);
    REQUIRE(status.ok());
    CHECK(warnings.empty());
    const auto id = t.find("lighthouse_mod:lit");
    REQUIRE(id.has_value());
    CHECK(*id == 0x70);  // after the original's
    const app::MessageDefinition* m = t.get(*id);
    CHECK(m->icon == app::MessageIcon::Scroll);
    CHECK(!m->meldung);
    CHECK(m->playback.kind == 1);
    REQUIRE(m->playback.sound != nullptr);
    CHECK(m->playback.sound->frames() == 441);
    CHECK(m->playback.video_base == 23);
    CHECK(m->playback.videos == 1);
    CHECK((m->playback.flags & audio::kMessageNormalSpeed) != 0);
    const app::MessageArgs args{{"player", "Merlin"}, {"island", "Nordinsel"}};
    // German in Windows-1252, as the game's fonts draw; a language without its own text: the
    // text for every language; an unknown placeholder stays.
    CHECK(t.text(*id, "de", args) ==
          "Der Leuchtturm von Merlin strahlt \xFC"
          "ber Nordinsel.");
    CHECK(t.text(*id, "fr", args) == "Merlin's lighthouse shines over Nordinsel.");
    CHECK(t.text(*id, "en", {{"player", "A"}}) == "A's lighthouse shines over {island}.");
}

TEST_CASE("messages: a package's message plays a video of the package's own") {
    TempPackage pkg("films");
    std::filesystem::create_directories(pkg.path / "videos");
    pkg.write("videos/beacon.smk", std::string("SMK2") + std::string(60, '\0'));
    pkg.write("videos/storm.BK2", std::string("KB2j") + std::string(60, '\0'));
    pkg.write("videos/fake.smk", "not a video");
    pkg.write("videos/clip.mp4", std::string("SMK2") + std::string(60, '\0'));
    app::MessageTable t;
    std::vector<std::string> warnings;
    REQUIRE(t.load_package("films",
                           "[beacon]\n"
                           "text = \"The beacon is lit.\"\n"
                           "video = \"videos/beacon.smk\"\n"
                           "[storm]\n"
                           "text = \"A storm.\"\n"
                           "video = \"videos/storm.BK2\"\n"
                           "[again]\n"
                           "text = \"Lit again.\"\n"
                           "video = \"videos/../videos/beacon.smk\"\n"
                           "[fake]\n"
                           "text = \"x\"\n"
                           "video = \"videos/fake.smk\"\n"
                           "[mp4]\n"
                           "text = \"x\"\n"
                           "video = \"videos/clip.mp4\"\n"
                           "[missing]\n"
                           "text = \"x\"\n"
                           "video = \"videos/none.smk\"\n"
                           "[outside]\n"
                           "text = \"x\"\n"
                           "video = \"../other/beacon.smk\"\n",
                           pkg.path, warnings)
                .ok());
    // The files get numbers from kFirstPackageVideo on; the same file keeps its number.
    const auto playback = [&t](std::string_view id) { return t.get(*t.find(id))->playback; };
    CHECK(playback("films:beacon").video_base == app::kFirstPackageVideo);
    CHECK(playback("films:beacon").videos == 1);
    CHECK(playback("films:storm").video_base == app::kFirstPackageVideo + 1);
    CHECK(playback("films:again").video_base == app::kFirstPackageVideo);
    REQUIRE(t.package_videos().size() == 2);
    CHECK(t.package_videos()[0] == (pkg.path / "videos" / "beacon.smk").lexically_normal());
    CHECK(t.package_videos()[1].filename() == "storm.BK2");
    // No Smacker or Bink 2 file, missing, or outside the package: no video, with a warning.
    for (const char* id : {"films:fake", "films:mp4", "films:missing", "films:outside"}) {
        CHECK(playback(id).videos == 0);
    }
    REQUIRE(warnings.size() == 4);
    CHECK(warnings[0].starts_with("films/messages.toml line 12: videos/fake.smk: missing or no Smacker"));
    CHECK(warnings[1].starts_with("films/messages.toml line 15: videos/clip.mp4"));
    CHECK(warnings[2].starts_with("films/messages.toml line 18: videos/none.smk"));
    CHECK(warnings[3].starts_with("films/messages.toml line 21: 'video' must be"));
}

TEST_CASE("messages: 'like' borrows an original's icon, text, voice and video; fields change it") {
    TempPackage pkg("m");
    app::MessageTable t;
    std::vector<std::string> warnings;
    REQUIRE(t.load_package("m",
                           "[gold]\n"
                           "video = false\n"  // before 'like', still applied after it
                           "like = 0x2C\n"
                           "[quiet]\n"
                           "like = \"anno1602:1\"\n"
                           "sound = false\n"
                           "text = \"Quiet plague\"\n",
                           pkg.path, warnings)
                .ok());
    CHECK(warnings.empty());
    const app::MessageDefinition* gold = t.get(*t.find("m:gold"));
    const app::MessageDefinition* original = t.get(0x2C);
    CHECK(gold->icon == original->icon);
    CHECK(gold->meldung == std::optional<std::uint16_t>(0x2C));
    CHECK(gold->playback.voice_base == original->playback.voice_base);
    CHECK(gold->playback.voices == original->playback.voices);
    CHECK(gold->playback.videos == 0);
    const app::MessageDefinition* quiet = t.get(*t.find("m:quiet"));
    CHECK(quiet->icon == app::MessageIcon::Plague);
    CHECK(quiet->playback.voices == 0);
    CHECK(quiet->playback.videos == 1);
    CHECK(t.text(*t.find("m:quiet"), "de") == "Quiet plague");
}

TEST_CASE("messages: a mod changes an original message field by field") {
    TempPackage pkg("voices");
    pkg.write_wave("plague.wav");
    app::MessageTable t;
    std::vector<std::string> warnings;
    REQUIRE(t.load_package("voices",
                           "[\"anno1602:0x01\"]\n"
                           "sound = \"plague.wav\"\n"
                           "text.de = \"Die Pest!\"\n",
                           pkg.path, warnings)
                .ok());
    CHECK(warnings.empty());
    const app::MessageDefinition* plague = t.get(1);
    CHECK(plague->id == "anno1602:1");
    CHECK(plague->playback.sound != nullptr);
    CHECK(plague->playback.video_base == 282);  // the rest as it was
    CHECK(plague->icon == app::MessageIcon::Plague);
    CHECK(t.text(1, "de") == "Die Pest!");
    CHECK(t.text(1, "en").empty());  // English keeps the original's text
    CHECK(app::MessageTable::original().get(1)->playback.sound == nullptr);
}

TEST_CASE("messages: what is wrong in a field is passed over with a warning that names the line") {
    TempPackage pkg("bad");
    pkg.write("noise.txt", "not a wave");
    app::MessageTable t;
    std::vector<std::string> warnings;
    REQUIRE(t.load_package("bad",
                           "[a]\n"
                           "text = \"A\"\n"
                           "icon = \"star\"\n"
                           "kind = 3\n"
                           "sound = \"../../escape.wav\"\n"
                           "sound = \"noise.txt\"\n",
                           pkg.path, warnings)
                .ok() == false);  // a key given twice: the file is not read at all
    CHECK(t.size() == 0x70);
    warnings.clear();
    REQUIRE(t.load_package("bad",
                           "[a]\n"
                           "text = \"A\"\n"
                           "icon = \"star\"\n"
                           "kind = 3\n"
                           "sound = \"../../escape.wav\"\n"
                           "video = \"intro.mp4\"\n"
                           "colour = \"red\"\n"
                           "[b]\n"
                           "sound = \"noise.txt\"\n"
                           "[\"anno1602:200\"]\n"
                           "text = \"x\"\n"
                           "[\"other:x\"]\n"
                           "text = \"x\"\n"
                           "[c]\n"
                           "like = 999\n",
                           pkg.path, warnings)
                .ok());
    REQUIRE(warnings.size() == 11);
    CHECK(warnings[0].starts_with("bad/messages.toml line 3: 'icon'"));
    CHECK(warnings[1].starts_with("bad/messages.toml line 4: 'kind'"));
    CHECK(warnings[2].starts_with("bad/messages.toml line 5: 'sound'"));
    CHECK(warnings[3].starts_with("bad/messages.toml line 6: intro.mp4: missing or no Smacker"));
    CHECK(warnings[4].starts_with("bad/messages.toml line 7: unknown field 'colour'"));
    CHECK(warnings[5].starts_with("bad/messages.toml line 9: noise.txt"));
    CHECK(warnings[6].starts_with("bad/messages.toml line 8: bad:b has no text"));
    CHECK(warnings[7].starts_with("bad/messages.toml line 10: there is no message anno1602:200"));
    CHECK(warnings[8].starts_with("bad/messages.toml line 12: there is no message other:x"));
    CHECK(warnings[9].starts_with("bad/messages.toml line 15: 'like'"));
    CHECK(warnings[10].starts_with("bad/messages.toml line 14: bad:c has no text"));
    // The messages themselves are there, without what was wrong.
    CHECK(t.find("bad:a").has_value());
    CHECK(t.get(*t.find("bad:a"))->icon == app::MessageIcon::Notice);
    CHECK(t.find("bad:b").has_value());
    // A package's id is its folder's name, lower case, and never the original's.
    CHECK(!t.load_package("Bad Mod", "", pkg.path, warnings).ok());
    CHECK(!t.load_package("anno1602", "", pkg.path, warnings).ok());
}

TEST_CASE("messages: the packages' messages, package by package in their load order") {
    TempPackage b("b_second");
    TempPackage a("a_first");
    a.write("messages.toml", "[hello]\ntext = \"from a\"\n");
    b.write("messages.toml", "[\"a_first:hello\"]\ntext = \"changed by b\"\n[hello]\ntext = \"from b\"\n");
    std::filesystem::create_directories(a.path.parent_path() / "no_messages");
    app::MessageTable t;
    std::vector<std::string> warnings;
    t.load_mods(assets::mod_packages(a.path.parent_path()), warnings);
    CHECK(warnings.empty());
    CHECK(t.size() == 0x72);
    CHECK(t.text(*t.find("a_first:hello"), "en") == "changed by b");
    CHECK(t.text(*t.find("b_second:hello"), "en") == "from b");
    // In the other load order b's change comes before a's message: it changes nothing.
    std::vector<assets::ModPackage> reversed = assets::mod_packages(a.path.parent_path());
    std::reverse(reversed.begin(), reversed.end());
    app::MessageTable other;
    other.load_mods(reversed, warnings);
    REQUIRE(other.find("a_first:hello").has_value());
    CHECK(other.text(*other.find("a_first:hello"), "en") == "from a");
    // No packages at all: nothing to load, nothing to say.
    warnings.clear();
    app::MessageTable none;
    none.load_mods({}, warnings);
    CHECK(warnings.empty());
    CHECK(none.size() == app::MessageTable().size());
}
