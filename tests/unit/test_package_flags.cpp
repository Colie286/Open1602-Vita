// SPDX-License-Identifier: GPL-3.0-or-later
// Flags of packages: flags.toml (engine/assets/include/o1602/assets/package_flags.hpp,
// docs/modding/flags.md) and the check of their banners (o1602-tool check-mod).

#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "mod_check.hpp"

#include "o1602/assets/package_flags.hpp"

using namespace o1602;

namespace {

std::vector<assets::PackageImage> make_images() {
    std::vector<assets::PackageImage> images(2);
    images[0].id = "banners:pennant";
    images[0].first = 40000;
    images[0].count = 8;
    images[1].id = "banners:small";
    images[1].first = 40008;
    images[1].count = 4;
    return images;
}

}  // namespace

TEST_CASE("package flags: flags.toml gives banners an image of eight frames") {
    const std::vector<assets::PackageImage> images = make_images();
    std::vector<assets::PackageFlag> flags;
    std::vector<std::string> warnings;
    assets::apply_flags_file(flags, "banners",
                             "[\"anno1602:red\"]\nimage = \"banners:pennant\"\n"
                             "[\"open1602:orange\"]\nimage = \"banners:pennant\"\ntint = true\n"
                             "[red]\nimage = \"banners:pennant\"\n"              // line 6: no banner id
                             "[\"anno1602:blue\"]\nimage = \"banners:small\"\n"  // line 9: 4 sprites
                             "[\"anno1602:white\"]\ntint = 3\n"                  // line 11: no boolean
                             "[\"anno1602:yellow\"]\nsize = 3\n",                // lines 12, 13
                             images, warnings);
    REQUIRE(flags.size() == 2);
    CHECK(flags[0].banner == "anno1602:red");
    CHECK(flags[0].image.first == 40000);
    CHECK(!flags[0].tint);
    CHECK(flags[1].banner == "open1602:orange");
    CHECK(flags[1].tint);
    REQUIRE(warnings.size() == 5);
    CHECK(warnings[0].find("line 6: [red]: names no banner") != std::string::npos);
    CHECK(warnings[1] ==
          "banners/flags.toml line 9: 'image' names banners:small, which holds 4 sprites; a flag waves in 8");
    CHECK(warnings[2].find("line 11: 'tint' is true or false") != std::string::npos);
    CHECK(warnings[3].find("line 13: 'size' is no field of a flag") != std::string::npos);
    CHECK(warnings[4].find("line 12: [anno1602:yellow] needs an 'image'") != std::string::npos);
    // A later package's flag of the same banner wins.
    assets::apply_flags_file(flags, "more", "[\"anno1602:red\"]\nimage = \"banners:pennant\"\ntint = true\n", images,
                             warnings);
    REQUIRE(flags.size() == 2);
    REQUIRE(assets::find_package_flag(flags, "anno1602:red") != nullptr);
    CHECK(assets::find_package_flag(flags, "anno1602:red")->tint);
    CHECK(assets::find_package_flag(flags, "anno1602:blue") == nullptr);
}

TEST_CASE("package flags: check-mod names flags of banners no game has") {
    const std::vector<assets::PackageImage> images = make_images();
    std::vector<assets::PackageFlag> flags;
    std::vector<std::string> warnings;
    assets::apply_flags_file(flags, "banners",
                             "[\"anno1602:red\"]\nimage = \"banners:pennant\"\n"
                             "[\"banners:purple\"]\nimage = \"banners:pennant\"\n",
                             images, warnings);
    CHECK(warnings.empty());
    auto banners = render::BannerTable::original();
    banners.add_more_players();
    std::vector<std::string> problems;
    app::check_flag_banners(flags, banners, problems);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0] == "banners:pennant: no banner is called \"banners:purple\"; its flag is not shown");
}
