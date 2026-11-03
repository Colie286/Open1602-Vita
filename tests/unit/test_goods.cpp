// SPDX-License-Identifier: GPL-3.0-or-later
// Goods a package adds: goods.toml, the ware numbers a game gives them, the buildings that make
// them and the savegame section GOOD (engine/assets/include/o1602/assets/goods.hpp,
// docs/modding/goods.md).

#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/goods.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::string_view kSpices =
    "[pepper]\n"
    "price = 70\n"
    "icon = \"anno1602:spices\"\n"
    "name.en = \"Pepper\"\n"
    "name.de = \"Pfeffer\"\n"
    "[salt]\n"
    "price = 20\n"
    "icon = \"anno1602:16\"\n"
    "name.en = \"Salt\"\n";

/// A bakery of the original, enough to copy from.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20503\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Ware: 14\n    Rohstoff: 9\n    Rohmenge: 2\n"
        "    Prodmenge: 1\n    Interval: 19\n    Maxlager: 4\n    Radius: 8\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("goods: a package adds goods after the original's ware numbers") {
    assets::Goods goods;
    std::vector<std::string> warnings;
    assets::apply_goods_file(goods, "spice", kSpices, warnings);
    CHECK(warnings.empty());
    REQUIRE(goods.own_goods().size() == 2);
    const assets::OwnGood* pepper = goods.own("spice:pepper");
    REQUIRE(pepper != nullptr);
    CHECK(pepper->number == assets::ware::kFirstOwn);
    CHECK(goods.own(assets::ware::kFirstOwn) == pepper);
    CHECK(pepper->name("de") == "Pfeffer");
    CHECK(pepper->name("fr") == "Pepper");  // English when the language has none
    CHECK(goods.own("spice:salt")->number == assets::ware::kFirstOwn + 1);

    CHECK(goods.is_trade(assets::ware::kFirstOwn));
    CHECK(goods.is_trade(assets::ware::kTools));
    CHECK(!goods.is_trade(assets::ware::kFirstOwn + 2));
    CHECK(goods.price(assets::ware::kFirstOwn) == 70);
    CHECK(goods.price(assets::ware::kTools) == assets::base_price(assets::ware::kTools));
    CHECK(goods.icon(assets::ware::kFirstOwn) == assets::ware::kSpices);
    CHECK(goods.icon(assets::ware::kTools) == assets::ware::kTools);
    CHECK(goods.end() == assets::ware::kFirstOwn + 2u);
    CHECK(goods.trade_wares().back() == assets::ware::kFirstOwn + 1);

    CHECK(assets::ware_of(goods, "spice:salt") == assets::ware::kFirstOwn + 1);
    CHECK(assets::ware_of(goods, "anno1602:tools") == assets::ware::kTools);
    CHECK(!assets::ware_of(goods, "spice:nothing"));
    CHECK(!assets::ware_of(goods, "tools"));
}

TEST_CASE("goods: a table that is wrong is passed over with a warning") {
    assets::Goods goods;
    std::vector<std::string> warnings;
    assets::apply_goods_file(goods, "spice",
                             "[Pepper]\n"  // line 1: upper case
                             "price = 70\n"
                             "[salt]\n"
                             "price = 0\n"  // line 4: below its limit
                             "[nutmeg]\n"
                             "price = 90\n"
                             "icon = \"anno1602:soldier\"\n"  // line 7: no good of the original
                             "[mace]\n"
                             "price = 90\n"  // line 8: no icon and no name
                             "[clove]\n"
                             "price = 90\n"
                             "icon = \"anno1602:spices\"\n"
                             "name.en = \"Clove\"\n"
                             "colour = 3\n",  // line 14: no field of a good
                             warnings);
    REQUIRE(goods.own_goods().size() == 1);
    CHECK(goods.own_goods()[0].id == "spice:clove");
    REQUIRE(warnings.size() == 5);
    CHECK(warnings[0] ==
          "spice/goods.toml line 1: [Pepper]: a good's name holds only lower case letters, digits and '_'");
    CHECK(warnings[1] == "spice/goods.toml line 4: 'price' is a number from 1 to 10000");
    CHECK(warnings[2] == "spice/goods.toml line 7: 'icon' names no good of the original (anno1602:<name or number>)");
    CHECK(warnings[3] == "spice/goods.toml line 8: spice:mace needs a 'price', an 'icon' and a name ('name.en')");
    CHECK(warnings[4] == "spice/goods.toml line 14: 'colour' is no field of a good and is passed over");

    // The same good again.
    warnings.clear();
    assets::apply_goods_file(goods, "spice", "[clove]\nprice = 9\nicon = \"anno1602:spices\"\nname.en = \"C\"\n",
                             warnings);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "spice/goods.toml line 1: spice:clove is declared twice; the second is passed over");
}

TEST_CASE("goods: a building of a package makes and uses goods by their ids") {
    assets::Goods goods;
    std::vector<std::string> warnings;
    assets::apply_goods_file(goods, "spice", kSpices, warnings);
    auto buildings = make_buildings();
    std::vector<assets::OwnBuilding> own;
    assets::apply_own_buildings_file(buildings, own, "spice",
                                     "[mill]\n"
                                     "like = \"anno1602:20503\"\n"
                                     "ware = \"spice:pepper\"\n"
                                     "raw = \"anno1602:grain\"\n"
                                     "raw2 = \"\"\n"
                                     "[press]\n"
                                     "like = \"anno1602:20503\"\n"
                                     "ware = \"spice:nothing\"\n",  // line 8
                                     warnings, goods);
    REQUIRE(own.size() == 1);
    const assets::BuildingType* mill = buildings.by_id(own[0].number);
    REQUIRE(mill != nullptr);
    CHECK(mill->ware == assets::ware::kFirstOwn);
    CHECK(mill->raw_ware == assets::ware::kGrain);
    CHECK(mill->raw2_ware == 0);
    CHECK(mill->raw2_amount == 0);  // "": it uses no second good, and none of it
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] ==
          "spice/own_buildings.toml line 8: 'ware' names no good (anno1602:<name or number> or <package>:<name>, \"\" "
          "for "
          "none)");
}

TEST_CASE("goods: a game keeps the goods its packages added") {
    assets::Goods goods;
    std::vector<std::string> warnings;
    assets::apply_goods_file(goods, "spice", kSpices, warnings);
    const auto buildings = make_buildings();
    sim::World world(60, 40);
    world.set_goods(goods);
    const auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto read = sim::read_game(*bytes, buildings);
    REQUIRE(read.ok());
    const assets::Goods& kept = read->goods();
    REQUIRE(kept.own_goods().size() == 2);
    const assets::OwnGood* pepper = kept.own("spice:pepper");
    REQUIRE(pepper != nullptr);
    CHECK(pepper->number == assets::ware::kFirstOwn);
    CHECK(pepper->price == 70);
    CHECK(pepper->icon == assets::ware::kSpices);
    CHECK(pepper->name("de") == "Pfeffer");
    CHECK(kept.trade_wares() == goods.trade_wares());

    // The original's savegame has no room for them.
    CHECK(!sim::savegame_chunks(world, buildings).ok());
}
