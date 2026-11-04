// SPDX-License-Identifier: GPL-3.0-or-later
// The original's buildings drawn with a package's images: building_images.toml
// (engine/assets/include/o1602/assets/building_looks.hpp, docs/modding/graphics.md) and the terrain
// sheet that draws their sprites with the image's (render::RedirectedSprites).

#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_looks.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/render/package_sprites.hpp"

using namespace o1602;

namespace {

/// A grain mill (2 x 2, turned, 5 frames 16 apart) and a well (1 x 1).
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"HAUS_PRODTYP", 2}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20501\n  Gfx: 100\n  Size: 2, 2\n  Rotate: 4\n  AnimAnz: 5\n  AnimAdd: 16\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 14\n  EndObj\n"
        "  @Nummer: 1\n  Id: 20502\n  Gfx: 200\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 14\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::PackageImage image(std::string id, std::uint32_t first, std::uint32_t count) {
    assets::PackageImage i;
    i.id = std::move(id);
    i.first = first;
    i.count = count;
    i.width = 64;
    i.height = 64;
    return i;
}

/// Sprite i is i pixels wide and one high.
class Numbered final : public render::SpriteSource {
public:
    explicit Numbered(std::uint32_t n) : m_count(n) {}
    [[nodiscard]] std::uint32_t count() const override { return m_count; }
    void size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const override {
        width = i;
        height = 1;
    }
    bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const override {
        rgba.assign(std::size_t{i} * 4, 0xFF);
        return true;
    }

private:
    std::uint32_t m_count;
};

}  // namespace

TEST_CASE("building images: a package draws the original's buildings with its images") {
    const assets::BuildingTable buildings = make_buildings();
    const assets::BuildingType* mill = buildings.by_id(20501);
    REQUIRE(mill != nullptr);
    CHECK(assets::building_sprite_count(*mill) == 4 + 4 * 3 + 16 * 4);
    const std::vector<assets::PackageImage> images = {image("mills:mill", 40000, 80), image("mills:well", 40080, 1),
                                                      image("mills:small", 40081, 79)};
    std::vector<assets::SpriteRedirect> redirects;
    std::vector<std::string> warnings;
    assets::apply_building_images_file(redirects, "mills",
                                       "[\"anno1602:grain_mill\"]\n"  // lines 1-2
                                       "image = \"mills:mill\"\n"
                                       "[\"anno1602:20502\"]\n"  // line 3
                                       "image = \"mills:well\"\n"
                                       "[\"anno1602:20501\"]\n"  // line 5: too few sprites
                                       "image = \"mills:small\"\n"
                                       "[\"anno1602:castle_of_dreams\"]\n"  // line 7
                                       "image = \"mills:well\"\n",
                                       images, buildings, warnings);
    assets::apply_building_images_file(redirects, "bad",
                                       "[\"anno1602:20502\"]\n"
                                       "image = \"mills:none\"\n"  // line 2
                                       "[\"anno1602:20501\"]\n"    // line 3
                                       "picture = \"mills:well\"\n",
                                       images, buildings, warnings);
    CHECK((redirects == std::vector<assets::SpriteRedirect>{{100, 80, 40000}, {200, 1, 40080}}));
    REQUIRE(warnings.size() == 5);
    CHECK(warnings[0] ==
          "mills/building_images.toml line 5: [anno1602:20501]: its image mills:small holds 79 sprites, the "
          "building draws 80");
    CHECK(warnings[1].find("line 7: [anno1602:castle_of_dreams]: names no building of the original") !=
          std::string::npos);
    CHECK(warnings[2] ==
          "bad/building_images.toml line 2: 'image' names no image of a package (graphics.toml, "
          "<package>:<name>)");
    CHECK(warnings[3].find("line 4: 'picture' is no field of a building's image") != std::string::npos);
    CHECK(warnings[4].find("line 3: [anno1602:20501] needs an 'image'") != std::string::npos);

    // A later package's image of the same building wins.
    assets::apply_building_images_file(redirects, "wells", "[\"anno1602:20502\"]\nimage = \"mills:mill\"\n", images,
                                       buildings, warnings);
    CHECK((redirects == std::vector<assets::SpriteRedirect>{{100, 80, 40000}, {200, 1, 40000}}));
}

TEST_CASE("building images: the terrain sheet draws the redirected sprites with the image's") {
    const Numbered base(50);
    const render::RedirectedSprites sheet(base, {{20, 3, 40}, {5, 2, 30}, {6, 4, 10}, {48, 5, 0}, {0, 1, 49}});
    CHECK(sheet.count() == 50);
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    sheet.size(5, w, h);
    CHECK(w == 30);
    sheet.size(6, w, h);
    CHECK(w == 31);
    sheet.size(7, w, h);  // after the redirect at 5; the one at 6 overlaps it and is left out
    CHECK(w == 7);
    sheet.size(22, w, h);
    CHECK(w == 42);
    sheet.size(23, w, h);
    CHECK(w == 23);
    sheet.size(48, w, h);  // a redirect beyond the sheet is left out
    CHECK(w == 48);
    sheet.size(0, w, h);
    CHECK(w == 49);
    std::vector<std::uint8_t> rgba;
    CHECK(sheet.pixels(21, rgba));
    CHECK(rgba.size() == 41 * 4);
}
