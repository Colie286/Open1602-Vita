// SPDX-License-Identifier: GPL-3.0-or-later
// The original's interface art drawn with a package's icons: interface.toml
// (engine/assets/include/o1602/assets/package_interface.hpp, docs/modding/graphics.md) and the
// lookup the interface's renderer makes (ui::interface_icon).

#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/package_interface.hpp"
#include "o1602/ui/package_icons.hpp"

using namespace o1602;

namespace {

std::vector<assets::PackageIcon> make_icons() {
    std::vector<assets::PackageIcon> icons(2);
    icons[0].id = "panels:back";
    icons[0].sprite = assets::kFirstPackageIcon + 10;
    icons[1].id = "panels:ok";
    icons[1].sprite = assets::kFirstPackageIcon + 11;
    return icons;
}

}  // namespace

TEST_CASE("interface sprites: a table's name is a sheet and a sprite number") {
    const auto start = assets::parse_interface_sprite("start:0");
    REQUIRE(start.has_value());
    CHECK(start->sheet == 1);
    CHECK(start->sprite == 0);
    const auto ship = assets::parse_interface_sprite("bauship:117");
    REQUIRE(ship.has_value());
    CHECK(ship->sheet == 4);
    CHECK(ship->sprite == 117);
    CHECK(!assets::parse_interface_sprite("start").has_value());
    CHECK(!assets::parse_interface_sprite("start:").has_value());
    CHECK(!assets::parse_interface_sprite("start:-1").has_value());
    CHECK(!assets::parse_interface_sprite("start:1x").has_value());
    CHECK(!assets::parse_interface_sprite("menu:1").has_value());
    CHECK(!assets::parse_interface_sprite("START:1").has_value());
}

TEST_CASE("interface sprites: interface.toml draws sprites of the original with icons") {
    const std::vector<assets::PackageIcon> icons = make_icons();
    std::vector<assets::InterfaceSprite> sprites;
    std::vector<std::string> warnings;
    assets::apply_interface_file(sprites, "panels",
                                 "[\"start:0\"]\nicon = \"panels:back\"\n"
                                 "[\"tools:12\"]\nicon = \"panels:ok\"\nhistory = 14\n"
                                 "[\"menu:3\"]\nicon = \"panels:ok\"\n"    // line 6: no sheet
                                 "[\"tools:13\"]\nicon = \"panels:no\"\n"  // line 9: no such icon
                                 "[\"tools:14\"]\nsize = 3\n",             // lines 10, 11
                                 icons, warnings);
    REQUIRE(sprites.size() == 2);
    CHECK((sprites[0] == assets::InterfaceSprite{1, 0, "panels:back", std::nullopt}));
    CHECK((sprites[1] == assets::InterfaceSprite{0, 12, "panels:ok", 14}));
    REQUIRE(warnings.size() == 4);
    CHECK(warnings[0].find("line 6: [menu:3]: names no sprite of the interface") != std::string::npos);
    CHECK(warnings[1] ==
          "panels/interface.toml line 9: 'icon' names no icon of a package (icons.toml, <package>:<name>)");
    CHECK(warnings[2].find("line 11: 'size' is no field of an interface sprite") != std::string::npos);
    CHECK(warnings[3].find("line 10: [tools:14] needs an 'icon'") != std::string::npos);
    // A later package's icon for the same sprite wins.
    assets::apply_interface_file(sprites, "more", "[\"start:0\"]\nicon = \"panels:ok\"\n", icons, warnings);
    REQUIRE(sprites.size() == 2);
    REQUIRE(assets::find_interface_sprite(sprites, 1, 0) != nullptr);
    CHECK(assets::find_interface_sprite(sprites, 1, 0)->icon == "panels:ok");
    CHECK(assets::find_interface_sprite(sprites, 1, 1) == nullptr);

    // The History Edition's sheet has the sprite under its `history` number.
    std::vector<assets::InterfaceSprite> history = sprites;
    assets::use_edition(history, true);
    CHECK(assets::find_interface_sprite(history, 1, 0) != nullptr);   // no other number
    CHECK(assets::find_interface_sprite(history, 0, 14) != nullptr);  // tools:12
    CHECK(assets::find_interface_sprite(history, 0, 12) == nullptr);
    std::vector<assets::InterfaceSprite> original = sprites;
    assets::use_edition(original, false);
    CHECK(original == sprites);

    // The interface finds the icon by the sheet and the sprite, not in another sheet.
    ui::set_package_icons(icons);
    ui::set_interface_sprites(sprites);
    const assets::PackageIcon* icon = ui::interface_icon(0, 12);
    REQUIRE(icon != nullptr);
    CHECK(icon->sprite == assets::kFirstPackageIcon + 11);
    CHECK(ui::interface_icon(1, 12) == nullptr);
    CHECK(ui::interface_icon(0, 0) == nullptr);
    // An icon that is gone (its package off) draws nothing in its place.
    ui::set_package_icons({});
    CHECK(ui::interface_icon(0, 12) == nullptr);
    ui::set_interface_sprites({});
}
