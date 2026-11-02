// SPDX-License-Identifier: GPL-3.0-or-later
// Figures a package adds: own_figures.toml, the numbers the figure table gives them, the buildings
// that name them as their workers, their values from figures.toml and the savegame section OWNF
// (engine/assets/include/o1602/assets/own_figures.hpp, docs/modding/own-figures.md).

#include <optional>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/figure_looks.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/assets/own_figures.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

/// Swordsmen in the four colours (1 to 4) and the carrier (80), who walks with eight directions.
assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 1\n  Kind: 1\n  Gfx: 10\n  Blocknr: 1\n"
        "  Nummer: 2\n  Kind: 1\n  Gfx: 11\n  Blocknr: 1\n"
        "  Nummer: 3\n  Kind: 1\n  Gfx: 12\n  Blocknr: 1\n"
        "  Nummer: 4\n  Kind: 1\n  Gfx: 13\n  Blocknr: 1\n"
        "  Nummer: 80\n  Kind: 3\n  Gfx: 100\n  Blocknr: 3\n  Rotate: 2\n  Schatten: 30\n  Speed: 30\n"
        "  Nummer: 30\n  Gfx: 5\n  Blocknr: 8\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A bakery of the original, whose carrier a package's bakery replaces.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20503\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Ware: 14\n    Figurnr: 80\n    Figuranz: 1\n  EndObj\n"
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
    i.width = 32;
    i.height = 32;
    return i;
}

}  // namespace

TEST_CASE("own figures: a package adds figures that copy one of the original") {
    auto figures = make_figures();
    std::vector<assets::OwnFigure> own;
    std::vector<std::string> warnings;
    const std::vector<assets::PackageImage> images = {image("monks:monk", 40000, 15)};
    assets::apply_own_figures_file(figures, own, "monks",
                                   "[monk]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "image = \"monks:monk\"\n"
                                   "shadow = false\n"
                                   "[guard]\n"
                                   "like = \"anno1602:2\"\n"
                                   "[brother]\n"
                                   "like = \"monks:monk\"\n",
                                   warnings, images);
    CHECK(warnings.empty());
    REQUIRE(own.size() == 3);
    CHECK(own[0].id == "monks:monk");
    // The numbers are the highest free ones, in the order the file names them.
    CHECK(own[0].number == assets::kMaxFigures - 1);
    CHECK(own[1].number == assets::kMaxFigures - 2);
    CHECK(own[2].number == assets::kMaxFigures - 3);
    CHECK(assets::find_own_figure(own, "monks:guard") == &own[1]);
    CHECK(assets::find_own_figure(own, "monks:nobody") == nullptr);

    const assets::FigureType* monk = figures.get(own[0].number);
    REQUIRE(monk != nullptr);
    CHECK(monk->number == own[0].number);
    CHECK(monk->speed == figures.get(80)->speed);  // the carrier's values
    CHECK(monk->sheet == assets::FigureSheet::Stadtfld);
    CHECK(monk->gfx == 40000);
    CHECK(assets::draws_package_image(*monk));
    CHECK(monk->shadow == 0);
    CHECK(figures.get(80)->gfx == 100);  // the original's stays as it is
    const assets::FigureType* guard = figures.get(own[1].number);
    REQUIRE(guard != nullptr);
    CHECK(guard->gfx == 11);                          // without an image the picture of the one it copies
    CHECK(figures.get(own[2].number)->gfx == 40000);  // a copy of another package's figure

    // What names one figure: the original's by name or number, a package's by its id.
    CHECK(assets::figure_number(own, figures, "anno1602:carrier") == 80U);
    CHECK(assets::figure_number(own, figures, "anno1602:0x50") == 80U);
    CHECK(assets::figure_number(own, figures, "monks:monk") == own[0].number);
    CHECK(!assets::figure_number(own, figures, "anno1602:swordsman"));  // four colours
    CHECK(!assets::figure_number(own, figures, "anno1602:500"));        // no such figure
    CHECK(!assets::figure_number(own, figures, "monks:abbot"));
}

TEST_CASE("own figures: a table that is wrong is passed over with a warning") {
    auto figures = make_figures();
    std::vector<assets::OwnFigure> own;
    std::vector<std::string> warnings;
    const std::vector<assets::PackageImage> images = {image("monks:small", 40000, 4)};
    assets::apply_own_figures_file(figures, own, "monks",
                                   "[nothing]\n"
                                   "image = \"monks:small\"\n"
                                   "[unit]\n"
                                   "like = \"anno1602:cavalry\"\n"
                                   "[tiny]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "image = \"monks:small\"\n"
                                   "[unknown]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "image = \"monks:none\"\n"
                                   "[Bad-Name]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "[ok]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "colour = 3\n",
                                   warnings, images);
    // Only "ok" is added; its unknown field is passed over.
    REQUIRE(own.size() == 1);
    CHECK(own[0].id == "monks:ok");
    CHECK(warnings.size() == 6);
}

TEST_CASE("own figures: a figure like a unit is a unit family of four colours") {
    auto figures = make_figures();
    std::vector<assets::OwnFigure> own;
    std::vector<assets::OwnUnit> units;
    std::vector<std::string> warnings;
    // Swordsmen draw one sprite each: 4 for one per colour, 1 for the same in every colour.
    const std::vector<assets::PackageImage> images = {image("guards:coloured", 40000, 4),
                                                      image("guards:one", 41000, 1)};
    assets::apply_own_figures_file(figures, own, "guards",
                                   "[pikeman]\n"
                                   "like = \"anno1602:swordsman\"\n"
                                   "image = \"guards:coloured\"\n"
                                   "icon = \"guards:pike\"\n"
                                   "name.en = \"Pikeman\"\n"
                                   "name.de = \"Pikenier\"\n"
                                   "[halberdier]\n"
                                   "like = \"guards:pikeman\"\n"
                                   "image = \"guards:one\"\n"
                                   "[monk]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "icon = \"guards:monk\"\n",
                                   warnings, images, {}, &units);
    REQUIRE(own.size() == 3);
    CHECK(warnings.size() == 1);  // the monk's icon: a unit's field only
    REQUIRE(units.size() == 2);
    // Four numbers in a row from the top, red first; the next family below them.
    CHECK(units[0].id == "guards:pikeman");
    CHECK(units[0].family == assets::kMaxFigures - 4);
    CHECK(own[0].number == units[0].family);
    CHECK(units[0].like == 1U);
    CHECK(units[0].icon == "guards:pike");
    CHECK(units[0].name("de") == "Pikenier");
    CHECK(units[1].family == assets::kMaxFigures - 8);
    CHECK(units[1].like == 1U);  // beside the swordsman, as the unit it copies
    CHECK(own[2].number == assets::kMaxFigures - 9);
    for (std::uint32_t k = 0; k < 4; ++k) {
        const assets::FigureType* f = figures.get(units[0].family + k);
        REQUIRE(f != nullptr);
        CHECK(f->kind == 1);
        CHECK(f->gfx == 40000 + k);  // one sprite per colour
        CHECK(figures.get(units[1].family + k)->gfx == 41000);
        const auto variant = figures.unit_of(units[0].family + k);
        REQUIRE(variant.has_value());
        CHECK(variant->family == units[0].family);
        CHECK(variant->index == k);
    }
    CHECK(!assets::FigureTable::unit_variant(units[0].family));  // not one of the original's
    CHECK(figures.unit_families().size() == 6);
    CHECK(assets::find_own_unit(units, units[1].family) == &units[1]);
    CHECK(assets::find_own_unit(units, 1) == nullptr);
}

TEST_CASE("own figures: a unit with tint = true draws one template in every colour") {
    auto figures = make_figures();
    std::vector<assets::OwnFigure> own;
    std::vector<assets::OwnUnit> units;
    std::vector<std::string> warnings;
    const std::vector<assets::PackageImage> images = {image("guards:grey", 40000, 4)};
    assets::apply_own_figures_file(figures, own, "guards",
                                   "[pikeman]\n"
                                   "like = \"anno1602:swordsman\"\n"
                                   "image = \"guards:grey\"\n"
                                   "tint = true\n"
                                   "[halberdier]\n"
                                   "like = \"guards:pikeman\"\n"
                                   "[archer]\n"
                                   "like = \"anno1602:swordsman\"\n"
                                   "tint = true\n"
                                   "[monk]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "tint = true\n",
                                   warnings, images, {}, &units);
    // The archer's tint needs an image, the monk is no unit.
    CHECK(warnings.size() == 2);
    REQUIRE(units.size() == 3);
    CHECK(units[0].tint);
    CHECK(units[1].tint);  // draws the pikeman's template
    CHECK(!units[2].tint);
    for (std::uint32_t k = 0; k < 4; ++k) {
        CHECK(figures.get(units[0].family + k)->gfx == 40000);  // one image, not one per colour
    }
    const auto sprites = assets::tint_template_sprites(units, figures);
    REQUIRE(sprites.size() == 1);
    CHECK(sprites[0].first == 40000);
    CHECK(sprites[0].second == 1);
}

TEST_CASE("own figures: a figure like a ship of the shipyards is a ship with its wreck") {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 21\n  Gfx: 200\n  Blocknr: 2\n  Rotate: 1\n  Maxenergy: 50\n  Rohmenge: 25\n"
        "  Nummer: 22\n  Gfx: 300\n  Blocknr: 2\n"
        "  Nummer: 29\n  Gfx: 400\n  Blocknr: 2\n  Rotate: 1\n"
        "  Nummer: 30\n  Gfx: 410\n  Blocknr: 2\n"
        "EndObj\n",
        types);
    auto loaded = assets::FigureTable::from_cod(doc);
    REQUIRE(loaded.ok());
    auto figures = std::move(loaded).value();
    std::vector<assets::OwnFigure> own;
    std::vector<assets::OwnUnit> units;
    std::vector<assets::OwnShip> ships;
    std::vector<std::string> warnings;
    const std::vector<assets::PackageImage> images = {image("fleet:cog", 50000, 8)};
    assets::apply_own_figures_file(figures, own, "fleet",
                                   "[cog]\n"
                                   "like = \"anno1602:small_merchant_ship\"\n"
                                   "image = \"fleet:cog\"\n"
                                   "tint = true\n"
                                   "icon = \"fleet:cog\"\n"
                                   "name.en = \"Cog\"\n"
                                   "name.de = \"Kogge\"\n"
                                   "[big_cog]\n"
                                   "like = \"fleet:cog\"\n"
                                   "[trader]\n"
                                   "like = \"anno1602:free_trader\"\n"
                                   "icon = \"fleet:trader\"\n",
                                   warnings, images, {}, &units, &ships);
    CHECK(warnings.size() == 1);  // the trader's icon: no shipyard builds the free trader's ship
    CHECK(units.empty());
    REQUIRE(own.size() == 3);
    REQUIRE(ships.size() == 2);
    // The ship and its wreck: the highest two numbers.
    CHECK(ships[0].id == "fleet:cog");
    CHECK(ships[0].figure == assets::kMaxFigures - 2);
    CHECK(own[0].number == ships[0].figure);
    CHECK(ships[0].type == 0);
    CHECK(ships[0].icon == "fleet:cog");
    CHECK(ships[0].name("de") == "Kogge");
    CHECK(ships[0].tint);
    CHECK(figures.get(ships[0].figure)->gfx == 50000);
    CHECK(figures.get(ships[0].figure)->ship.max_health == 50 * 32);
    CHECK(figures.get(ships[0].figure + 1)->gfx == 300);  // the wreck of the ship it copies
    // A ship like the cog stands beside the same type and draws the cog's template.
    CHECK(ships[1].figure == assets::kMaxFigures - 4);
    CHECK(ships[1].type == 0);
    CHECK(ships[1].tint);
    CHECK(figures.ship_type_of(ships[1].figure) == std::optional<std::uint8_t>{0});
    CHECK(!figures.ship_type_of(own[2].number).has_value());  // the trader is no shipyard's ship
    CHECK(assets::find_own_ship(ships, ships[1].figure) == &ships[1]);
    CHECK(assets::find_own_ship(ships, 21) == nullptr);
    const auto sprites = assets::tint_template_sprites(units, figures, ships);
    REQUIRE(sprites.size() == 1);
    CHECK(sprites[0].first == 50000);
}

TEST_CASE("own figures: a package's building names its workers, figures.toml their values") {
    auto figures = make_figures();
    auto buildings = make_buildings();
    std::vector<assets::OwnFigure> own_figures;
    std::vector<std::string> warnings;
    assets::apply_own_figures_file(figures, own_figures, "monks", "[monk]\nlike = \"anno1602:carrier\"\n", warnings);
    std::vector<assets::OwnBuilding> own;
    assets::apply_own_buildings_file(buildings, own, "monks",
                                     "[abbey]\n"
                                     "like = \"anno1602:20503\"\n"
                                     "figure = \"monks:monk\"\n"
                                     "[mill]\n"
                                     "like = \"anno1602:20503\"\n"
                                     "figure = \"anno1602:cart\"\n",
                                     warnings, {}, {}, {}, {}, &figures, own_figures);
    // The cart is not in this table: the mill is passed over.
    CHECK(warnings.size() == 1);
    REQUIRE(own.size() == 1);
    CHECK(buildings.by_id(own[0].number)->worker_figure == own_figures[0].number);

    sim::Balance balance;
    warnings.clear();
    sim::apply_figures_file(balance, "monks", "[\"monks:monk\"]\nspeed = 60\n", warnings, own_figures);
    CHECK(warnings.empty());
    REQUIRE(balance.figures.size() == 1);
    CHECK(balance.figures[0].number == own_figures[0].number);
    const auto original = figures;
    sim::apply_figure_values(figures, original, balance);
    CHECK(figures.get(own_figures[0].number)->speed == 60);
    CHECK(figures.get(80)->speed == original.get(80)->speed);
}

TEST_CASE("own figures: a game keeps the numbers of the figures its packages added") {
    auto figures = make_figures();
    const auto buildings = make_buildings();
    std::vector<assets::OwnFigure> own;
    std::vector<std::string> warnings;
    assets::apply_own_figures_file(figures, own, "monks", "[monk]\nlike = \"anno1602:carrier\"\n", warnings);
    sim::World world(60, 40);
    world.set_own_figures(own);
    const auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto read = sim::read_game(*bytes, buildings, nullptr, &figures);
    REQUIRE(read.ok());
    REQUIRE(read->own_figures().size() == 1);
    CHECK(read->own_figures()[0] == own[0]);

    // Without that figure the game does not load: its buildings' carriers would name nothing.
    const auto plain = make_figures();
    CHECK(!sim::read_game(*bytes, buildings, nullptr, &plain).ok());
}

TEST_CASE("own figures: a figure and a building of a package sound sounds of a package") {
    assets::FigureTable figures = make_figures();
    assets::BuildingTable buildings = make_buildings();
    std::vector<assets::PackageSound> sounds(2);
    sounds[0].id = "mill:grind";
    sounds[0].number = assets::kFirstPackageSound;
    sounds[1].id = "mill:thud";
    sounds[1].number = assets::kFirstPackageSound + 1;
    std::vector<assets::OwnFigure> own;
    std::vector<std::string> warnings;
    assets::apply_own_figures_file(figures, own, "mill",
                                   "[miller]\nlike = \"anno1602:80\"\nsound = \"mill:grind\"\n"
                                   "shot_sound = \"mill:thud\"\nhit_sound = \"mill:grind\"\n"
                                   "[mute]\nlike = \"anno1602:80\"\nsound = \"mill:none\"\n",
                                   warnings, {}, sounds);
    REQUIRE(own.size() == 1);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("'sound' names no sound of a package") != std::string::npos);
    const assets::FigureType* miller = figures.get(own[0].number);
    REQUIRE(miller != nullptr);
    CHECK(miller->shot_sound.sample == assets::kFirstPackageSound + 1);
    CHECK(miller->shot_sound.variants == 1);
    CHECK(miller->hit_sound.sample == assets::kFirstPackageSound);
    for (const assets::FigureAnim& a : miller->anims) {
        CHECK(a.sound == assets::kFirstPackageSound);
    }

    std::vector<assets::OwnBuilding> own_buildings;
    warnings.clear();
    assets::apply_own_buildings_file(buildings, own_buildings, "mill",
                                     "[mill]\nlike = \"anno1602:20503\"\nbuild_sound = \"mill:thud\"\n"
                                     "[quiet]\nlike = \"anno1602:20503\"\nbuild_sound = \"mill:none\"\n",
                                     warnings, {}, {}, {}, {}, &figures, own, sounds);
    REQUIRE(own_buildings.size() == 1);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("'build_sound' names no sound of a package") != std::string::npos);
    const assets::BuildingType* mill = buildings.by_id(own_buildings[0].number);
    REQUIRE(mill != nullptr);
    CHECK(mill->build_sound == assets::kFirstPackageSound + 1);
}
