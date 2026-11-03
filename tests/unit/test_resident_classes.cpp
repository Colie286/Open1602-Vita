// SPDX-License-Identifier: GPL-3.0-or-later
// Classes of inhabitants as data: classes.toml changes the original's and adds new ones, houses of
// a package belong to them, and a game keeps them (engine/assets/include/o1602/assets/
// resident_classes.hpp, docs/modding/classes.md).

#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/goods.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint8_t kCloth = 19;
constexpr std::uint8_t kAlcohol = 18;
constexpr std::uint8_t kJewellery = 21;

/// The five classes of haeuser.cod with fewer needs, a house of the aristocrats and a farm.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"HAUS_PRODTYP", 2}, {"BGRUPPE", 5}, {"BGRUPPE_WARE", 6}};
    const auto doc = assets::cod::parse(
        "Nahrung: 1.3\n"
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 19, 0.6\n    Ware: 18, 0.5\n  EndObj\n"
        "  Nummer: 2\n  Prozent: 71\n  Maxwohn: 15\n  Steuer: 2.1\n"
        "  Nummer: 3\n  Prozent: 78\n  Maxwohn: 25\n  Steuer: 2.4\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 19, 0.8\n    Ware: 18, 0.7\n  EndObj\n"
        "  Nummer: 4\n  Prozent: 84\n  Maxwohn: 40\n  Steuer: 2.6\n  Hiscore: 100\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 18, 0.8\n    Ware: 21, 0.2\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20509\n  Gfx: 10\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 4\n  EndObj\n"
        "  @Nummer: 1\n  Id: 20503\n  Gfx: 20\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Ware: 14\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::Goods make_goods() {
    assets::Goods goods;
    std::vector<std::string> warnings;
    assets::apply_goods_file(goods, "spice", "[pepper]\nprice = 70\nicon = \"anno1602:spices\"\nname.en = \"Pepper\"\n",
                             warnings);
    REQUIRE(warnings.empty());
    return goods;
}

}  // namespace

TEST_CASE("classes: a package changes the original's classes and adds one after them") {
    const auto buildings = make_buildings();
    const auto goods = make_goods();
    std::vector<assets::ResidentClass> classes = buildings.resident_classes();
    REQUIRE(classes.size() == 5);
    CHECK(classes[4].look == 4);
    std::vector<std::string> warnings;
    const bool changed = assets::apply_classes_file(classes, "spice",
                                                    "[\"anno1602:merchants\"]\n"
                                                    "tax = 300\n"
                                                    "share = 50\n"
                                                    "need.\"spice:pepper\" = 400\n"
                                                    "need.\"anno1602:alcohol\" = 0\n"
                                                    "[governors]\n"
                                                    "max_per_house = 60\n"
                                                    "need.\"spice:pepper\" = 600\n"
                                                    "name.en = \"Governors\"\n"
                                                    "name.de = \"Statthalter\"\n"
                                                    "[\"anno1602:1\"]\n"
                                                    "need.\"anno1602:cloth\" = 1000\n",
                                                    warnings, goods);
    CHECK(changed);
    CHECK(warnings.empty());
    const std::uint8_t pepper = assets::ware::kFirstOwn;
    REQUIRE(classes.size() == 6);

    const assets::ResidentClass& merchants = classes[3];
    CHECK(merchants.tax == 16);    // trunc(3.0 * 16 / 3)
    CHECK(merchants.share == 64);  // 50 * 128 / 100
    REQUIRE(merchants.needs.size() == 2);
    CHECK(merchants.needs[0].ware == kCloth);  // alcohol gone, pepper after cloth
    CHECK(merchants.needs[1].ware == pepper);
    CHECK(merchants.needs[1].amount == 5);    // trunc(0.4 * 8192) / 600
    CHECK(classes[1].needs[0].amount == 13);  // trunc(1.0 * 8192) / 600

    // The new class copies the last one, then takes its own values.
    const assets::ResidentClass& governors = classes[5];
    CHECK(governors.id == "spice:governors");
    CHECK(governors.look == 4);
    CHECK(governors.max_per_house == 60);
    CHECK(governors.tax == classes[4].tax);
    CHECK(governors.hiscore == 100);
    REQUIRE(governors.needs.size() == 3);
    CHECK(governors.needs[0].ware == kAlcohol);
    CHECK(governors.needs[1].ware == kJewellery);
    CHECK(governors.needs[2].ware == pepper);
    CHECK(assets::class_name(governors, "de") == "Statthalter");
    CHECK(assets::class_name(governors, "fr") == "Governors");
    CHECK(assets::class_name(classes[0], "de").empty());  // Text.cod names the original's

    CHECK(assets::class_of(classes, "spice:governors") == 5);
    CHECK(assets::class_of(classes, "anno1602:aristocrats") == 4);
    CHECK(assets::class_of(classes, "anno1602:0") == 0);
    CHECK(!assets::class_of(classes, "anno1602:5"));  // a package's class has its own id
    CHECK(!assets::class_of(classes, "spice:nobody"));

    // A package's class is changed by its id, as the original's are.
    warnings.clear();
    assets::apply_classes_file(classes, "harder", "[\"spice:governors\"]\nmax_per_house = 50\n", warnings, goods);
    CHECK(warnings.empty());
    CHECK(classes[5].max_per_house == 50);
}

TEST_CASE("classes: a table that is wrong is passed over with a warning") {
    const auto buildings = make_buildings();
    const auto goods = make_goods();
    std::vector<assets::ResidentClass> classes = buildings.resident_classes();
    std::vector<std::string> warnings;
    const bool changed = assets::apply_classes_file(classes, "spice",
                                                    "[\"anno1602:kings\"]\n"  // line 1: no such class
                                                    "tax = 1\n"
                                                    "[Kings]\n"  // line 3: upper case
                                                    "[kings]\n"  // line 4: no name
                                                    "tax = 1\n"
                                                    "[\"anno1602:citizens\"]\n"
                                                    "like = \"anno1602:merchants\"\n"  // line 7: for a new class
                                                    "need.\"anno1602:food\" = 3\n"     // line 8: food
                                                    "[\"anno1602:settlers\"]\n"
                                                    "need.\"spice:salt\" = 3\n"  // line 10: no such good
                                                    "max_per_house = 0\n"        // line 11: below its limit
                                                    "colour = 3\n",              // line 12: no field of a class
                                                    warnings, goods);
    CHECK(!changed);
    CHECK(classes == buildings.resident_classes());
    REQUIRE(warnings.size() == 8);
    CHECK(warnings[0] ==
          "spice/classes.toml line 1: [anno1602:kings]: names none of the original's classes "
          "(anno1602:<name or number>) and none a package added");
    CHECK(warnings[1] ==
          "spice/classes.toml line 3: [Kings]: a class's name holds only lower case letters, digits and '_'");
    CHECK(warnings[2] == "spice/classes.toml line 4: spice:kings needs a name ('name.en')");
    CHECK(warnings[3] == "spice/classes.toml line 7: 'like' is for a new class only and is passed over");
    CHECK(warnings[4] ==
          "spice/classes.toml line 8: 'need.anno1602:food' names food, which every class eats as haeuser.cod says");
    CHECK(warnings[5] ==
          "spice/classes.toml line 10: 'need.spice:salt' names no good (need.\"anno1602:<name>\" or "
          "need.\"<package>:<name>\")");
    CHECK(warnings[6] == "spice/classes.toml line 11: 'max_per_house' is a number from 1 to 255");
    CHECK(warnings[7] == "spice/classes.toml line 12: 'colour' is no field of a class and is passed over");

    // At most eight classes.
    warnings.clear();
    std::string many;
    for (int k = 0; k < 4; ++k) {
        many += "[c" + std::to_string(k) + "]\nname.en = \"C\"\n";
    }
    assets::apply_classes_file(classes, "spice", many, warnings, goods);
    CHECK(classes.size() == assets::kMaxResidentClasses);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "spice/classes.toml line 7: spice:c3: a game has at most 8 classes of inhabitants");
}

TEST_CASE("classes: a house of a package belongs to a class a package added") {
    auto buildings = make_buildings();
    std::vector<assets::ResidentClass> classes = buildings.resident_classes();
    std::vector<std::string> warnings;
    assets::apply_classes_file(classes, "spice", "[governors]\nname.en = \"Governors\"\n", warnings);
    std::vector<assets::OwnBuilding> own;
    assets::apply_own_buildings_file(buildings, own, "spice",
                                     "[palace]\n"
                                     "like = \"anno1602:20509\"\n"
                                     "class = \"spice:governors\"\n"
                                     "[farm]\n"
                                     "like = \"anno1602:20503\"\n"
                                     "class = \"spice:governors\"\n",  // line 6: no house
                                     warnings, {}, classes);
    REQUIRE(own.size() == 1);
    const assets::BuildingType* palace = buildings.by_id(own[0].number);
    REQUIRE(palace != nullptr);
    CHECK(palace->resident_class == 5);
    CHECK(palace->rand_count == 0);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "spice/own_buildings.toml line 4: spice:farm: 'class' is for a house only");

    // The game's table takes the classes; the new one's house is the one the houses rise into, and
    // it needs the public buildings of the aristocrats.
    const auto original = buildings;
    sim::apply_resident_classes(buildings, original, classes);
    REQUIRE(buildings.resident_classes().size() == 6);
    CHECK(buildings.first_house(5) == palace);
    CHECK(sim::service_class(buildings, 5) == 4);
    sim::apply_resident_classes(buildings, original, {});
    CHECK(buildings.resident_classes().size() == 5);
}

TEST_CASE("classes: the inhabitants of a new class ask for a good of a package") {
    auto buildings = make_buildings();
    const auto goods = make_goods();
    std::vector<assets::ResidentClass> classes = buildings.resident_classes();
    std::vector<std::string> warnings;
    assets::apply_classes_file(classes, "spice", "[governors]\nname.en = \"Governors\"\nneed.\"spice:pepper\" = 600\n",
                               warnings, goods);
    const auto original = buildings;
    sim::apply_resident_classes(buildings, original, classes);
    sim::City city;
    city.capacity = 30 * 32;
    city.inhabitants = {0, 0, 0, 0, 0, 100};
    sim::consume(city, buildings, nullptr);
    const std::uint8_t pepper = assets::ware::kFirstOwn;
    REQUIRE(pepper < city.supply.size());
    CHECK(city.supply[pepper].demand == std::uint64_t{8} * 100 * 15 / 16);  // trunc(0.6 * 8192) / 600 per inhabitant
}

TEST_CASE("classes: a game keeps its classes") {
    const auto buildings = make_buildings();
    std::vector<assets::ResidentClass> classes = buildings.resident_classes();
    std::vector<std::string> warnings;
    assets::apply_classes_file(classes, "spice",
                               "[governors]\nname.en = \"Governors\"\nname.de = \"Statthalter\"\n"
                               "public = \"market, doctor\"\n"
                               "[\"anno1602:merchants\"]\ntax = 300\n",
                               warnings);
    REQUIRE(classes[5].public_needs.has_value());
    sim::World world(60, 40);
    world.set_resident_classes(classes);
    const auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto read = sim::read_game(*bytes, buildings);
    REQUIRE(read.ok());
    CHECK(read->resident_classes() == classes);

    // Public buildings of a kind of their own beyond the 16 bits (CPUB version 2).
    namespace pb = assets::public_building;
    classes[5].public_needs = std::vector<assets::ServiceBits>{pb::kMarket, pb::own(2) | pb::own(pb::kOwnCount - 1)};
    world.set_resident_classes(classes);
    const auto wide = sim::write_game(world, buildings);
    REQUIRE(wide.ok());
    auto read_wide = sim::read_game(*wide, buildings);
    REQUIRE(read_wide.ok());
    CHECK(read_wide->resident_classes() == classes);

    // A world without classes of its own writes none.
    const auto plain = sim::write_game(sim::World(60, 40), buildings);
    REQUIRE(plain.ok());
    auto read_plain = sim::read_game(*plain, buildings);
    REQUIRE(read_plain.ok());
    CHECK(read_plain->resident_classes().empty());

    // The original's savegame has no room for a sixth class.
    CHECK(!sim::savegame_chunks(world, buildings).ok());
}

TEST_CASE("classes: a class names the public buildings it needs") {
    namespace pb = assets::public_building;
    std::string error;
    const auto needs = assets::public_needs_of(" market , chapel|church,tavern ,school | college", error);
    REQUIRE(needs.has_value());
    const std::vector<assets::ServiceBits> expected{pb::kMarket, pb::kChapel | pb::kChurch, pb::kTavern,
                                                    pb::kSchool | pb::kCollege};
    CHECK(*needs == expected);
    CHECK(assets::public_needs_of("", error)->empty());
    CHECK(!assets::public_needs_of("market, castle", error));
    CHECK(error == "names no public building: \"castle\"");
    CHECK(!assets::public_needs_of("market,,chapel", error));
    CHECK(!assets::public_needs_of("market | ", error));
    CHECK(
        !assets::public_needs_of("market, chapel, church, tavern, school, college, bathhouse, theatre, doctor", error));

    const auto buildings_cod = make_buildings();
    std::vector<assets::ResidentClass> classes = buildings_cod.resident_classes();
    std::vector<std::string> warnings;
    assets::apply_classes_file(classes, "spice",
                               "[governors]\nname.en = \"Governors\"\npublic = \"market, doctor | gallows\"\n"
                               "[\"anno1602:settlers\"]\npublic = \"\"\n"
                               "[\"anno1602:citizens\"]\npublic = \"market, castle\"\n"
                               "[\"anno1602:merchants\"]\npublic = 3\n",
                               warnings);
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].find("'public' names no public building") != std::string::npos);
    CHECK(warnings[1].find("'public' is a text") != std::string::npos);
    REQUIRE(classes.size() == 6);
    CHECK(!classes[2].public_needs);  // the table with the wrong field changes nothing
    REQUIRE(classes[1].public_needs.has_value());
    CHECK(classes[1].public_needs->empty());

    assets::BuildingTable buildings = buildings_cod;
    buildings.set_resident_classes(classes);
    sim::House house;
    house.market = true;
    // The governors need a market and a doctor or gallows, not the aristocrats' college and theatre.
    CHECK(!sim::has_services(house, buildings, 5));
    house.services = sim::house_service::kGalgen;
    CHECK(sim::has_services(house, buildings, 5));
    CHECK(!sim::has_services(house, buildings, 4));
    CHECK(sim::required_panel_services(buildings, 5) == (sim::panel_service::kMarkt | sim::panel_service::kKlinik));
    // Settlers need nothing now, pioneers still a market or Kontor.
    sim::House empty;
    CHECK(sim::has_services(empty, buildings, 1));
    CHECK(!sim::has_services(empty, buildings, 0));
    CHECK(sim::required_panel_services(buildings, 1) == 0);
    CHECK(sim::required_panel_services(buildings, 3) == sim::required_panel_services(3));
}
