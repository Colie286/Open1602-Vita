// SPDX-License-Identifier: GPL-3.0-or-later
// The checks of o1602-tool check-mod that need more than one file (engine/app/src/mod_check.hpp).

#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "mod_check.hpp"

using namespace o1602;

TEST_CASE("mod check: a track that replaces a title MUSIC8 has not") {
    std::vector<audio::MusicTrack> music(3);
    music[0].title = "calm";
    music[0].replaces = "GREENSLEAVES";  // case ignored
    music[1].title = "battle";
    music[1].replaces = "Fight4";
    music[2].title = "own";  // no replacement: nothing to check
    const std::vector<std::string> tracks = {"Greensleaves", "Daydream"};
    const std::vector<std::string> fights = {"Fight1", "Fight2", "Fight3"};
    std::vector<std::string> problems;
    app::check_music_replacements(music, tracks, fights, problems);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0] == "battle: MUSIC8 has no track \"Fight4\" to replace; the track is passed over");
}

TEST_CASE("mod check: coats of arms of banners no game has") {
    auto banners = render::BannerTable::original();
    banners.add_more_players();
    const std::vector<assets::PackageArms> arms = {
        {"anno1602:red", "h:lion"}, {"open1602:orange", "h:eagle"}, {"anno1602:green", "h:bear"}};
    std::vector<std::string> problems;
    app::check_arms_banners(arms, banners, problems);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0] == "h:bear: no banner is called \"anno1602:green\"; its coat of arms is not shown");
}

TEST_CASE("mod check: the problems as JSON, with package, file and line") {
    std::vector<assets::ModPackage> packages(1);
    packages[0].id = "lights";
    packages[0].version = "1.0";
    const std::vector<std::string> problems = {
        "lights/sounds.toml line 3: 'file' is \"x\\y\"",
        "lights/mod.toml: no version",
        "lights:none: SAMPLES has no sound",
        "tab\there",
    };
    CHECK(app::problems_json(packages, problems) ==
          "{\"packages\":[{\"id\":\"lights\",\"version\":\"1.0\"}],\"problems\":["
          "{\"text\":\"lights/sounds.toml line 3: 'file' is \\\"x\\\\y\\\"\",\"package\":\"lights\","
          "\"file\":\"sounds.toml\",\"line\":3},"
          "{\"text\":\"lights/mod.toml: no version\",\"package\":\"lights\",\"file\":\"mod.toml\"},"
          "{\"text\":\"lights:none: SAMPLES has no sound\"},"
          "{\"text\":\"tab\\there\"}]}");
    CHECK(app::problems_json({}, {}) == "{\"packages\":[],\"problems\":[]}");
}
