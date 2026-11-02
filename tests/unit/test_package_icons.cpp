// SPDX-License-Identifier: GPL-3.0-or-later
// Icons of a package's own: icons.toml (engine/assets/include/o1602/assets/package_graphics.hpp),
// the icons of goods, of the build menu and of messages, the savegame section GPIC and the
// pictures the interface draws (engine/ui/include/o1602/ui/package_icons.hpp,
// engine/render/include/o1602/render/package_sprites.hpp; docs/modding/graphics.md).

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "message_table.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/goods.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/core/compress.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/png_writer.hpp"
#include "o1602/render/package_sprites.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/package_icons.hpp"

using namespace o1602;

namespace {

void need_zlib() {
    if (!core::has_zlib()) {
        SKIP("this build has no zlib");
    }
}

/// An RGBA image of one colour.
std::vector<std::uint8_t> plain(std::uint32_t width, std::uint32_t height, std::uint8_t red) {
    std::vector<std::uint8_t> rgba(std::size_t{width} * height * 4, 0);
    for (std::size_t p = 0; p < rgba.size(); p += 4) {
        rgba[p] = red;
        rgba[p + 3] = 255;
    }
    return rgba;
}

/// A folder of a package with the PNG files given (name, width, height).
std::filesystem::path package_folder(
    const std::string& name,
    const std::vector<std::pair<std::string, std::pair<std::uint32_t, std::uint32_t>>>& files) {
    const std::filesystem::path folder = test::temp_path(name);
    std::filesystem::remove_all(folder);
    for (const auto& [file, size] : files) {
        const std::filesystem::path path = folder / file;
        std::filesystem::create_directories(path.parent_path());
        REQUIRE(core::write_png_rgba(path, size.first, size.second, plain(size.first, size.second, 200)).ok());
    }
    return folder;
}

/// The icons of a package "spice": pepper and salt.
std::vector<assets::PackageIcon> spice_icons() {
    const auto folder = package_folder("o1602-test-icons-spice", {{"pepper.png", {30, 28}}, {"salt.png", {30, 28}}});
    std::vector<assets::PackageIcon> icons;
    std::vector<std::string> warnings;
    assets::apply_icons_file(icons, "spice", folder, "[pepper]\nimage = \"pepper.png\"\n[salt]\nimage = \"salt.png\"\n",
                             warnings);
    REQUIRE(warnings.empty());
    REQUIRE(icons.size() == 2);
    return icons;
}

/// Sets the icons of the interface for one test and takes them away again.
struct IconsInUse {
    explicit IconsInUse(std::vector<assets::PackageIcon> icons) { ui::set_package_icons(std::move(icons)); }
    ~IconsInUse() { ui::set_package_icons({}); }
    IconsInUse(const IconsInUse&) = delete;
    IconsInUse& operator=(const IconsInUse&) = delete;
};

/// A bakery of the original, for a savegame.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20503\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n"
        "  @Nummer: 1\n  Id: 22201\n  Kind: 10\n  Gfx: 2\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("icons: an icon with tint = true is a template in player colour") {
    need_zlib();
    const auto folder = package_folder("o1602-test-icons-tint", {{"arms.png", {30, 28}}});
    std::vector<assets::PackageIcon> icons;
    std::vector<std::string> warnings;
    assets::apply_icons_file(icons, "heraldry", folder,
                             "[arms]\nimage = \"arms.png\"\ntint = true\n"
                             "[plain]\nimage = \"arms.png\"\n"
                             "[bad]\nimage = \"arms.png\"\ntint = 1\n",
                             warnings);
    REQUIRE(icons.size() == 2);
    CHECK(icons[0].tint);
    CHECK(!icons[1].tint);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "heraldry/icons.toml line 8: 'tint' is true or false");
}

TEST_CASE("icons: icons.toml numbers the icons of the packages") {
    need_zlib();
    const auto folder =
        package_folder("o1602-test-icons", {{"icons/pepper.png", {30, 28}}, {"big.png", {300, 20}}, {"x.png", {8, 8}}});
    std::vector<assets::PackageIcon> icons;
    std::vector<std::string> warnings;
    assets::apply_icons_file(icons, "spice", folder,
                             "[pepper]\nimage = \"icons/pepper.png\"\n"  // lines 1-2
                             "[Salt]\n"                                  // line 3: upper case
                             "[a]\nimage = \"../x.png\"\n"               // line 5: outside the package
                             "[b]\nsize = 3\n"                           // line 7: no field; line 6: no image
                             "[c]\nimage = \"none.png\"\n"               // line 8: no file
                             "[d]\nimage = \"big.png\"\n",               // line 10: too wide
                             warnings);
    REQUIRE(icons.size() == 1);
    CHECK(icons[0].id == "spice:pepper");
    CHECK(icons[0].sprite == assets::kFirstPackageIcon);
    CHECK(icons[0].file == folder / "icons" / "pepper.png");
    CHECK(icons[0].width == 30);
    CHECK(icons[0].height == 28);
    REQUIRE(warnings.size() == 6);
    CHECK(warnings[0] ==
          "spice/icons.toml line 3: [Salt]: an icon's name holds only lower case letters, digits and '_'");
    CHECK(warnings[1] ==
          "spice/icons.toml line 5: 'image' is the path of a PNG file in the package, folders split by '/'");
    CHECK(warnings[2] == "spice/icons.toml line 7: 'size' is no field of an icon and is passed over");
    CHECK(warnings[3] == "spice/icons.toml line 6: spice:b needs an 'image'");
    CHECK(warnings[4].rfind("spice/icons.toml line 8: spice:c: none.png: ", 0) == 0);
    CHECK(warnings[5] == "spice/icons.toml line 10: spice:d: big.png: 300 x 20 pixels, an icon has at most 256 a side");

    // A package's icon the same package declared before is passed over; the next package's follow.
    warnings.clear();
    assets::apply_icons_file(icons, "spice", folder, "[pepper]\nimage = \"x.png\"\n", warnings);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "spice/icons.toml line 1: spice:pepper is declared twice; the second is passed over");
    warnings.clear();
    assets::apply_icons_file(icons, "more", folder, "[x]\nimage = \"x.png\"\n", warnings);
    CHECK(warnings.empty());
    REQUIRE(icons.size() == 2);
    CHECK(icons[1].sprite == assets::kFirstPackageIcon + 1);
    CHECK(assets::find_package_icon(icons, "more:x") == &icons[1]);
    CHECK(assets::package_icon_at(icons, assets::kFirstPackageIcon + 1) == &icons[1]);
    CHECK(assets::package_icon_at(icons, assets::kFirstPackageIcon + 2) == nullptr);
    CHECK(assets::package_icon_at(icons, 192) == nullptr);
}

TEST_CASE("icons: the picture of an icon at the interface's smaller art") {
    need_zlib();
    // 4 x 2: the left half red and opaque, the right half transparent but for one blue pixel.
    std::vector<std::uint8_t> rgba(4 * 2 * 4, 0);
    for (std::size_t y = 0; y < 2; ++y) {
        for (std::size_t x = 0; x < 2; ++x) {
            rgba[(y * 4 + x) * 4] = 200;
            rgba[(y * 4 + x) * 4 + 3] = 255;
        }
    }
    rgba[(1 * 4 + 3) * 4 + 2] = 255;
    rgba[(1 * 4 + 3) * 4 + 3] = 255;
    const std::filesystem::path folder = test::temp_path("o1602-test-icon-pixels");
    std::filesystem::create_directories(folder);
    REQUIRE(core::write_png_rgba(folder / "i.png", 4, 2, rgba).ok());
    assets::PackageIcon icon;
    icon.file = folder / "i.png";

    const render::IconPixels full = render::package_icon_pixels(icon, 1.0);
    CHECK(full.error.empty());
    CHECK(full.width == 4);
    CHECK(full.rgba == rgba);

    const render::IconPixels half = render::package_icon_pixels(icon, 0.5);
    REQUIRE(half.width == 2);
    REQUIRE(half.height == 1);
    // Left: four red pixels. Right: one blue pixel of four, its colour kept, a quarter opaque.
    CHECK(half.rgba == std::vector<std::uint8_t>({200, 0, 0, 255, 0, 0, 255, 63}));

    icon.file = folder / "none.png";
    const render::IconPixels none = render::package_icon_pixels(icon, 1.0);
    CHECK(!none.error.empty());
    CHECK(none.rgba.empty());
}

TEST_CASE("icons: a good shows an icon of a package, and a game keeps it") {
    need_zlib();
    std::vector<assets::PackageIcon> icons = spice_icons();
    assets::Goods goods;
    std::vector<std::string> warnings;
    assets::apply_goods_file(
        goods, "spice",
        "[pepper]\nprice = 70\nicon = \"anno1602:spices\"\npicture = \"spice:pepper\"\n"
        "name.en = \"Pepper\"\n"
        "[salt]\nprice = 20\nicon = \"anno1602:16\"\npicture = \"spice:none\"\nname.en = \"Salt\"\n",
        warnings, icons);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] ==
          "spice/goods.toml line 9: 'picture' names no icon of a package (icons.toml, <package>:<name>)");
    REQUIRE(goods.own_goods().size() == 1);
    const std::uint8_t pepper = goods.own_goods()[0].number;
    CHECK(goods.picture(pepper) == "spice:pepper");
    CHECK(goods.picture(assets::ware::kSpices).empty());

    {
        // The interface draws the package's icon while a package has it, else the original's.
        const IconsInUse in_use(icons);
        CHECK(ui::ware_sprite(192, goods, pepper) == assets::kFirstPackageIcon);
        CHECK(ui::ware_sprite(192, goods, assets::ware::kSpices) == 192 + assets::ware::kSpices);
    }
    CHECK(ui::ware_sprite(192, goods, pepper) == 192 + assets::ware::kSpices);

    // A savegame keeps the icon's id (GPIC).
    const auto buildings = make_buildings();
    sim::World world(60, 40);
    world.set_goods(goods);
    const auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto read = sim::read_game(*bytes, buildings);
    REQUIRE(read.ok());
    CHECK(read->goods().picture(pepper) == "spice:pepper");
    CHECK(read->goods() == goods);
}

TEST_CASE("icons: a building of the build menu shows an icon of a package") {
    need_zlib();
    const IconsInUse in_use(spice_icons());
    const assets::BuildingTable buildings = make_buildings();
    ui::BuildMenuDefinition menu = ui::original_build_menu();
    std::vector<std::string> warnings;
    REQUIRE(ui::apply_build_menu_package(menu, "spice",
                                         "[mill]\nid = 22201\ncategory = 31007\nicon = \"spice:salt\"\n"
                                         "name = \"Salzmühle\"\n"
                                         "[bakery]\nid = 20503\ncategory = 31007\nicon = \"spice:none\"\n",
                                         buildings, warnings)
                .ok());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] ==
          "spice/build_menu.toml line 9: 'icon' names no icon of a package (icons.toml, <package>:<name>); "
          "[bakery] is passed over");
    const ui::BuildCategory* harbour = menu.category(31007);
    REQUIRE(harbour != nullptr);
    const ui::BuildButton* mill = harbour->button_at(*harbour->index_of(22201));
    REQUIRE(mill != nullptr);
    CHECK(mill->picture == "spice:salt");
    CHECK(mill->sprite(100) == assets::kFirstPackageIcon + 1);
    ui::BuildButton original = *mill;
    original.picture.clear();
    original.icon = 7;
    CHECK(original.sprite(100) == 107);
}

TEST_CASE("icons: a message shows an icon of a package") {
    need_zlib();
    const IconsInUse in_use(spice_icons());
    app::MessageTable t;
    std::vector<std::string> warnings;
    REQUIRE(t.load_package("spice",
                           "[harvest]\ntext = \"Pepper!\"\nicon = \"spice:pepper\"\n"
                           "[storm]\ntext = \"Storm!\"\nicon = \"spice:none\"\n",
                           {}, warnings)
                .ok());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].rfind("spice/messages.toml line 6: 'icon' must be one of notice", 0) == 0);
    const auto harvest = t.find("spice:harvest");
    REQUIRE(harvest);
    CHECK(t.get(*harvest)->picture == "spice:pepper");
    // A message like it takes the icon too; a kind of icon puts the package's away.
    REQUIRE(t.load_package("more", "[a]\nlike = \"spice:harvest\"\n[b]\nlike = \"spice:harvest\"\nicon = \"fire\"\n",
                           {}, warnings)
                .ok());
    CHECK(t.get(*t.find("more:a"))->picture == "spice:pepper");
    CHECK(t.get(*t.find("more:b"))->picture.empty());
    CHECK(t.get(*t.find("more:b"))->icon == app::MessageIcon::Fire);
}

TEST_CASE("package icons: Open1602's own icons are compiled in and come first") {
    const std::vector<assets::PackageIcon> icons = ui::builtin_icons();
    for (const char* id : {"open1602:demolish", "open1602:copy"}) {
        const assets::PackageIcon* icon = assets::find_package_icon(icons, id);
        REQUIRE(icon != nullptr);
        CHECK(icon->data != nullptr);
        CHECK(icon->width > 0);
        CHECK(icon->width <= 53);  // the build menu's pop-up slot
        CHECK(icon->height <= 50);
        CHECK(icon->sprite >= assets::kFirstPackageIcon);
        CHECK(icon->sprite < assets::kFirstPackageIcon + static_cast<std::int32_t>(icons.size()));
    }
}
