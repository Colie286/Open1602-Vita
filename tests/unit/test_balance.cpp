// SPDX-License-Identifier: GPL-3.0-or-later
// Values of the balance as data (MD-9, docs/modding/balance.md): the original's defaults, a
// package's balance.toml and the sections BALA and BLDV that keep them with the game; the values of
// buildings and the rule of the revised bread chain.

#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/assets/original_names.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        // A bakery and a grain farm with the original's values.
        "  @Nummer: 1\n  Id: 20503\n  Kind: 14\n  Gfx: 1\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Ware: 14\n    Rohstoff: 9\n    Rohmenge: 2\n    Prodmenge: 1\n"
        "    Interval: 19\n    Maxlager: 4\n    Kosten: 5, 0\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 150\n    Werkzeug: 2\n    Holz: 6\n  EndObj\n"
        "  @Nummer: 2\n  Id: 21503\n  Kind: 14\n  Gfx: 5\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 8\n    Rohstoff: 45\n    Rohmenge: 1\n    Prodmenge: 1\n"
        "    Interval: 8\n    Maxlager: 6\n    Kosten: 5, 0\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 100\n    Werkzeug: 2\n    Holz: 5\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        // A swordsman and a ship with the original's kind of values.
        "  Nummer: 1\n  Kind: 1\n  Gfx: 0\n  Blocknr: 1\n  Speed: 260\n  Shotradius: 0.75\n  Maxstepcnt: 1\n"
        "  Rohstoff: 11\n  Rohmenge: 3\n  Worktime: 0.8\n  Shottime: 0.6\n  Maxenergy: 20\n  Hitpoint: 1.0\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
        "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
        "  Maxtrag: 10\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("balance: the defaults are the original's and within their limits") {
    const sim::Balance balance;
    CHECK(balance.trader_max_held == 50);
    CHECK(balance.trader_max_buy == 10);
    CHECK(balance.trader_restock_s == 60);
    CHECK(balance.castle_interval_s == 10);
    CHECK(balance.castle_unload_range == 6);
    CHECK(balance.fire_spread == 19);
    CHECK(balance.plague_radius == 4);
    CHECK(sim::valid(balance));
    CHECK(sim::World(10, 10).balance() == balance);
    sim::Balance zero = balance;
    zero.trader_max_buy = 0;
    CHECK(!sim::valid(zero));
}

TEST_CASE("balance: a package changes the values one by one") {
    sim::Balance balance;
    std::vector<std::string> warnings;
    sim::apply_balance_file(balance, "harsh",
                            "# A harder game.\n"
                            "[trader]\n"
                            "max_buy = 5\n"
                            "restock_seconds = 120\n"
                            "max_held = 0\n"
                            "\n"
                            "[disasters]\n"
                            "fire_spread = 40\n"
                            "plague = 3\n"
                            "[weather]\n"
                            "rain = 1\n",
                            warnings);
    CHECK(balance.trader_max_buy == 5);
    CHECK(balance.trader_restock_s == 120);
    CHECK(balance.trader_max_held == 50);  // out of its limits: passed over
    CHECK(balance.fire_spread == 40);
    CHECK(balance.castle_interval_s == 10);
    REQUIRE(warnings.size() == 3);
    CHECK(warnings[0] == "harsh/balance.toml line 5: 'max_held' must be a number from 1 to 200");
    CHECK(warnings[1] == "harsh/balance.toml line 9: unknown field 'plague'");
    CHECK(warnings[2] == "harsh/balance.toml line 10: there is no table [weather]");

    // A file that is no data file changes nothing.
    warnings.clear();
    const sim::Balance before = balance;
    sim::apply_balance_file(balance, "broken", "[trader]\nmax_buy = \"3\n", warnings);
    CHECK(balance == before);
    CHECK(warnings.size() == 1);
}

TEST_CASE("balance: a game keeps its values in the savegame, a damaged value loads nothing") {
    const auto buildings = make_buildings();
    sim::World world(60, 40);
    // The original's values are not written (docs/formats/o1602-save.md, BALA).
    auto plain = sim::write_game(world, buildings);
    REQUIRE(plain.ok());
    sim::Balance balance;
    balance.castle_unload_range = 9;
    balance.plague_radius = 0;
    world.set_balance(balance);
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    CHECK(loaded->balance() == balance);
    auto from_plain = sim::read_game(*plain, buildings);
    REQUIRE(from_plain.ok());
    CHECK(from_plain->balance() == sim::Balance{});

    balance.castle_unload_range = 100;
    world.set_balance(balance);
    auto damaged = sim::write_game(world, buildings);
    REQUIRE(damaged.ok());
    CHECK(!sim::read_game(*damaged, buildings).ok());
}

TEST_CASE("balance: the computers' cheats are values of [computers], saved in BALA version 2 when changed") {
    sim::Balance balance;
    CHECK(balance.ai_steady_below == 3791);
    CHECK(balance.ai_steady_money == 379);
    CHECK(balance.ai_founding_bricks == 8);
    CHECK(balance.ai_colony_bricks == 9);
    CHECK(sim::balance_version(balance) == 1);
    std::vector<std::string> warnings;
    sim::apply_balance_file(balance, "fair",
                            "# Computers without gifts.\n"
                            "[computers]\n"
                            "steady_money = 0\n"
                            "founding_bricks = 0\n"
                            "subsidy_money = 2000000\n",
                            warnings);
    CHECK(balance.ai_steady_money == 0);
    CHECK(balance.ai_founding_bricks == 0);
    CHECK(balance.ai_subsidy_money == 1793);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "fair/balance.toml line 5: 'subsidy_money' must be a number from 0 to 100000");
    CHECK(sim::balance_version(balance) == sim::kBalanceWithComputers);

    // Saved and loaded again, as are values of version 1 alone (written as version 1, which an
    // Open1602 before the computers' values reads).
    const auto buildings = make_buildings();
    sim::World world(60, 40);
    world.set_balance(balance);
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    CHECK(loaded->balance() == balance);
    sim::Balance old;
    old.fire_spread = 3;
    world.set_balance(old);
    auto old_bytes = sim::write_game(world, buildings);
    REQUIRE(old_bytes.ok());
    auto old_loaded = sim::read_game(*old_bytes, buildings);
    REQUIRE(old_loaded.ok());
    CHECK(old_loaded->balance() == old);
}

TEST_CASE("balance: when the computers' cities move on is [computer_stages], saved in BALA version 3") {
    sim::Balance balance;
    // The original's numbers.
    CHECK(balance.ai_pioneers_inhabitants == 2);
    CHECK(balance.ai_settlers_inhabitants == 22);
    CHECK(balance.ai_grown_houses == 24);
    CHECK(balance.ai_merchants_land == 951);
    CHECK(balance.ai_aristocrats_money == 15'001);
    std::vector<std::string> warnings;
    sim::apply_balance_file(balance, "eager",
                            "[computer_stages]\n"
                            "settlers_inhabitants = 10\n"
                            "merchants_rich_money = 12000\n"
                            "grown_seconds = -1\n",
                            warnings);
    CHECK(balance.ai_settlers_inhabitants == 10);
    CHECK(balance.ai_merchants_rich_money == 12'000);
    CHECK(balance.ai_grown_seconds == 3000);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].starts_with("eager/balance.toml line 4: 'grown_seconds' must be a number from 0 to"));
    CHECK(sim::balance_version(balance) == sim::kBalanceWithStages);

    const auto buildings = make_buildings();
    sim::World world(60, 40);
    world.set_balance(balance);
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    CHECK(loaded->balance() == balance);
    // A cheat changed alone still needs only version 2.
    sim::Balance cheats;
    cheats.ai_steady_money = 0;
    CHECK(sim::balance_version(cheats) == sim::kBalanceWithComputers);
}

TEST_CASE("balance: a package changes values of the original's buildings") {
    sim::Balance balance;
    std::vector<std::string> warnings;
    sim::apply_balance_file(balance, "bread",
                            "[\"anno1602:20503\"]\n"  // line 1
                            "produce = 2\n"
                            "running_cost = 15\n"
                            "smell = 3\n"              // line 4
                            "interval = 0\n"           // line 5
                            "[\"anno1602:0x5401\"]\n"  // 21505 in hex
                            "cost_wood = 4\n"
                            "[\"anno1602:farm\"]\n"  // line 8
                            "produce = 2\n"
                            "[\"brewery:mash_tun\"]\n"  // line 10
                            "produce = 2\n"
                            "[trader]\n"
                            "max_buy = 5\n"
                            "[\"anno1602:20503\"]\n",  // a table given twice: the file is no data file
                            warnings);
    CHECK(balance == sim::Balance{});
    REQUIRE(warnings.size() == 1);

    warnings.clear();
    sim::apply_balance_file(balance, "bread",
                            "[\"anno1602:20503\"]\n"
                            "produce = 2\n"
                            "running_cost = 15\n"
                            "smell = 3\n"
                            "interval = 0\n"
                            "[\"anno1602:0x5401\"]\n"
                            "cost_wood = 4\n"
                            "[\"anno1602:farm\"]\n"
                            "produce = 2\n"
                            "[\"brewery:mash_tun\"]\n"
                            "produce = 2\n"
                            "[trader]\n"
                            "max_buy = 5\n",
                            warnings);
    CHECK(balance.trader_max_buy == 5);
    REQUIRE(balance.buildings.size() == 3);
    CHECK((balance.buildings[0] == sim::BuildingValue{20503, sim::BuildingField::Produce, 2}));
    CHECK((balance.buildings[1] == sim::BuildingValue{20503, sim::BuildingField::RunningCost, 15}));
    CHECK((balance.buildings[2] == sim::BuildingValue{21505, sim::BuildingField::BuildWood, 4}));
    CHECK(sim::valid(balance));
    REQUIRE(warnings.size() == 4);
    CHECK(warnings[0] == "bread/balance.toml line 4: unknown field 'smell'");
    CHECK(warnings[1] == "bread/balance.toml line 5: 'interval' must be a number from 1 to 3600");
    CHECK(warnings[2] ==
          "bread/balance.toml line 8: [anno1602:farm]: 'farm' is no number and no name of the original's "
          "buildings");
    CHECK(warnings[3] ==
          "bread/balance.toml line 10: [brewery:mash_tun]: names none of the original's buildings "
          "(anno1602:<number or name>) and none a package added");

    // A later package changes a value again; the order stays by building and field.
    warnings.clear();
    sim::apply_balance_file(balance, "later", "[\"anno1602:20503\"]\nproduce = 3\ninterval = 12\n", warnings);
    CHECK(warnings.empty());
    REQUIRE(balance.buildings.size() == 4);
    CHECK((balance.buildings[0] == sim::BuildingValue{20503, sim::BuildingField::Produce, 3}));
    CHECK((balance.buildings[1] == sim::BuildingValue{20503, sim::BuildingField::Interval, 12}));
    CHECK(sim::valid(balance));
    std::swap(balance.buildings[0], balance.buildings[1]);
    CHECK(!sim::valid(balance));  // out of order
    std::swap(balance.buildings[0], balance.buildings[1]);

    // Buildings the data does not have are dropped with a warning.
    const auto buildings = make_buildings();
    warnings.clear();
    sim::drop_unknown_buildings(balance, buildings, warnings);
    CHECK(balance.buildings.size() == 3);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "balance.toml: haeuser.cod has no building anno1602:21505, its values are passed over");
}

TEST_CASE("balance: a game's building table has its values, the next game the original's again") {
    const auto original = make_buildings();
    auto table = original;
    const assets::BuildingType* bakery = table.by_id(20503);
    REQUIRE(bakery != nullptr);
    constexpr std::uint32_t kTon = assets::kStockUnitsPerTon;
    CHECK(bakery->produce_amount == kTon);
    CHECK(bakery->raw_amount == 2 * kTon);
    CHECK(bakery->running_cost == 5);

    sim::Balance balance;
    for (const auto& [field, value] :
         std::vector<std::pair<sim::BuildingField, std::uint32_t>>{{sim::BuildingField::Produce, 2},
                                                                   {sim::BuildingField::Raw, 3},
                                                                   {sim::BuildingField::Raw2, 1},
                                                                   {sim::BuildingField::Interval, 12},
                                                                   {sim::BuildingField::MaxStock, 8},
                                                                   {sim::BuildingField::RunningCost, 15},
                                                                   {sim::BuildingField::IdleCost, 2},
                                                                   {sim::BuildingField::BuildMoney, 220},
                                                                   {sim::BuildingField::BuildTools, 3},
                                                                   {sim::BuildingField::BuildWood, 7},
                                                                   {sim::BuildingField::BuildBricks, 4}}) {
        sim::set_building_value(balance, sim::BuildingValue{20503, field, value});
    }
    sim::set_building_value(balance, sim::BuildingValue{29999, sim::BuildingField::Produce, 9});  // no such building
    sim::apply_building_values(table, original, balance);
    CHECK(table.by_id(20503) == bakery);  // the same place
    CHECK(bakery->produce_amount == 2 * kTon);
    CHECK(bakery->raw_amount == 3 * kTon);
    CHECK(bakery->raw2_amount == kTon);
    CHECK(bakery->interval == 12);
    CHECK(bakery->max_stock == 8 * kTon);
    CHECK(bakery->running_cost == 15);
    CHECK(bakery->idle_cost == 2);
    CHECK(bakery->build_cost.money == 220);
    CHECK(bakery->build_cost.tools == 3 * kTon);
    CHECK(bakery->build_cost.wood == 7 * kTon);
    CHECK(bakery->build_cost.bricks == 4 * kTon);
    CHECK(table.by_id(21503)->produce_amount == kTon);  // untouched

    // The next game without values plays with the original's.
    sim::apply_building_values(table, original, sim::Balance{});
    CHECK(bakery->produce_amount == kTon);
    CHECK(bakery->raw_amount == 2 * kTon);
    CHECK(bakery->raw2_amount == 0);
    CHECK(bakery->interval == 19);
    CHECK(bakery->max_stock == 4 * kTon);
    CHECK(bakery->running_cost == 5);
    CHECK(bakery->idle_cost == 0);
    CHECK(bakery->build_cost.money == 150);
    CHECK(bakery->build_cost.tools == 2 * kTon);
    CHECK(bakery->build_cost.wood == 6 * kTon);
    CHECK(bakery->build_cost.bricks == 0);
}

TEST_CASE("balance: the revised bread chain goes under the packages' values") {
    // Open1602 plays the revised chain by default, the original's settings the original's.
    CHECK(sim::rule_building_values(sim::Rules::original()).empty());
    CHECK(!sim::Rules::original().revised_bread);
    CHECK(sim::Rules::open1602().revised_bread);
    sim::Rules rules = sim::Rules::open1602();
    rules.wider_hunting = false;  // only the bread chain here
    const std::vector<sim::BuildingValue> bread = sim::rule_building_values(rules);
    REQUIRE(bread.size() == 3);
    CHECK((bread[0] == sim::BuildingValue{20503, sim::BuildingField::Produce, 2}));  // bakery
    CHECK((bread[1] == sim::BuildingValue{20503, sim::BuildingField::RunningCost, 15}));
    CHECK((bread[2] == sim::BuildingValue{21503, sim::BuildingField::Produce, 2}));  // grain farm

    // A package that sets the bakery's running cost itself keeps it.
    sim::Balance balance;
    sim::set_building_value(balance, sim::BuildingValue{20503, sim::BuildingField::RunningCost, 10});
    sim::add_building_values(balance, bread);
    REQUIRE(balance.buildings.size() == 3);
    CHECK((balance.buildings[1] == sim::BuildingValue{20503, sim::BuildingField::RunningCost, 10}));
    CHECK(sim::valid(balance));
}

TEST_CASE("balance: the hunter's hut reaches 12 tiles in Open1602, 8 with the original's settings") {
    CHECK(sim::Rules::open1602().wider_hunting);
    CHECK(!sim::Rules::original().wider_hunting);
    sim::Rules rules = sim::Rules::original();
    rules.wider_hunting = true;
    const std::vector<sim::BuildingValue> hunting = sim::rule_building_values(rules);
    REQUIRE(hunting.size() == 1);
    CHECK((hunting[0] == sim::BuildingValue{22003, sim::BuildingField::Radius, 12}));
}

TEST_CASE("balance: a game keeps its building values and the rule in the savegame") {
    const auto buildings = make_buildings();
    sim::World world(60, 40);
    sim::Balance balance;
    sim::set_building_value(balance, sim::BuildingValue{21503, sim::BuildingField::Produce, 2});
    sim::set_building_value(balance, sim::BuildingValue{20503, sim::BuildingField::RunningCost, 15});
    world.set_balance(balance);
    sim::Rules rules;
    rules.revised_bread = true;
    world.set_saved_rules(rules);
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    CHECK(loaded->balance() == balance);
    REQUIRE(loaded->saved_rules().has_value());
    CHECK(loaded->saved_rules()->revised_bread);

    // The original's chain writes no key, and a game without the key (also one of the versions
    // before the revised chain became the default) plays the original's chain.
    rules.revised_bread = false;
    world.set_saved_rules(rules);
    auto without_key = sim::write_game(world, buildings);
    REQUIRE(without_key.ok());
    auto loaded_without_key = sim::read_game(*without_key, buildings);
    REQUIRE(loaded_without_key.ok());
    REQUIRE(loaded_without_key->saved_rules().has_value());
    CHECK(!loaded_without_key->saved_rules()->revised_bread);

    // Only the building values: BALA stays out, BLDV alone brings them back.
    CHECK(sim::original_numbers(loaded->balance()));

    // A building the data does not know, or a value out of its limits, loads nothing.
    sim::Balance unknown = balance;
    sim::set_building_value(unknown, sim::BuildingValue{29999, sim::BuildingField::Produce, 2});
    world.set_balance(unknown);
    auto with_unknown = sim::write_game(world, buildings);
    REQUIRE(with_unknown.ok());
    CHECK(!sim::read_game(*with_unknown, buildings).ok());
    sim::Balance too_much = balance;
    too_much.buildings[0].value = 5000;  // running cost above 1000
    world.set_balance(too_much);
    auto damaged = sim::write_game(world, buildings);
    REQUIRE(damaged.ok());
    CHECK(!sim::read_game(*damaged, buildings).ok());
}

TEST_CASE("balance: buildings.toml changes the buildings' further values") {
    const auto original = make_buildings();
    auto table = original;
    sim::Balance balance;
    std::vector<std::string> warnings;
    sim::apply_buildings_file(balance, "fort",
                              "[\"anno1602:20503\"]\n"
                              "radius = 12\n"
                              "workers = 9\n"
                              "max_idle = 4\n"
                              "health = 40\n"
                              "cost_cannons = 2\n"
                              "cost_wood = 999\n"  // line 7: above its limit
                              "[trader]\n"         // line 8: no building
                              "max_buy = 5\n",
                              warnings);
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0] == "fort/buildings.toml line 7: 'cost_wood' must be a number from 0 to 255");
    CHECK(warnings[1] ==
          "fort/buildings.toml line 8: [trader]: names none of the original's buildings "
          "(anno1602:<number or name>) and none a package added");
    CHECK(balance.trader_max_buy == sim::Balance{}.trader_max_buy);
    REQUIRE(balance.buildings.size() == 5);
    CHECK(sim::valid(balance));

    sim::apply_building_values(table, original, balance);
    const assets::BuildingType* bakery = table.by_id(20503);
    REQUIRE(bakery != nullptr);
    CHECK(bakery->radius == 12);
    CHECK(bakery->workers == 9);
    CHECK(bakery->max_idle_cycles == 4);
    CHECK(bakery->max_health == 40 * 32);
    CHECK(bakery->build_cost.cannons == 2 * assets::kStockUnitsPerTon);
    sim::apply_building_values(table, original, sim::Balance{});
    CHECK(bakery->radius == original.by_id(20503)->radius);
    CHECK(bakery->max_health == original.by_id(20503)->max_health);
}

TEST_CASE("balance: figures.toml changes the figures' values, the next game plays the original's") {
    const auto original = make_figures();
    auto table = original;
    sim::Balance balance;
    std::vector<std::string> warnings;
    sim::apply_figures_file(balance, "army",
                            "[\"anno1602:1\"]\n"
                            "speed = 300\n"
                            "health = 30\n"
                            "hit = 2\n"
                            "range = 2\n"
                            "shot_time = 900\n"
                            "material = 4\n"
                            "[\"anno1602:21\"]\n"
                            "cannons = 10\n"
                            "price = 1500\n"
                            "max_carry = 12\n"
                            "work_time = 500\n"
                            "sails = 3\n"  // line 13
                            "[\"anno1602:999\"]\n"
                            "speed = 10\n",
                            warnings);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "army/figures.toml line 13: unknown field 'sails'");
    REQUIRE(balance.figures.size() == 11);
    CHECK(sim::valid(balance));
    CHECK((balance.figures[0] == sim::FigureValue{1, sim::FigureField::Speed, 300}));

    warnings.clear();
    sim::drop_unknown_figures(balance, original, warnings);
    CHECK(balance.figures.size() == 10);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "figures.toml: figuren.cod has no figure anno1602:999, its values are passed over");

    sim::apply_figure_values(table, original, balance);
    const assets::FigureType* soldier = table.get(1);
    const assets::FigureType* ship = table.get(21);
    REQUIRE(soldier != nullptr);
    REQUIRE(ship != nullptr);
    CHECK(soldier->speed == 300);
    CHECK(soldier->combat.max_health == 30 * 32);
    CHECK(soldier->combat.hit == 2 * 32);
    CHECK(soldier->combat.range8 == 16);
    CHECK(soldier->combat.hit_delay_ms == 900);
    CHECK(soldier->combat.weapon_amount == 4 * assets::kStockUnitsPerTon);
    CHECK(ship->ship.max_cannons == 10);
    CHECK(ship->ship.price == 1500);
    CHECK(ship->max_carry == 12 * assets::kStockUnitsPerTon);
    CHECK(ship->work_time_ms == 500);
    CHECK(ship->ship.max_health == original.get(21)->ship.max_health);  // untouched

    sim::apply_figure_values(table, original, sim::Balance{});
    CHECK(soldier->speed == 260);
    CHECK(soldier->combat.max_health == original.get(1)->combat.max_health);
    CHECK(soldier->combat.range8 == original.get(1)->combat.range8);
    CHECK(ship->ship.max_cannons == 6);
    CHECK(ship->ship.price == 1000);
}

TEST_CASE("balance: tables name the original's buildings and figures by name as well") {
    sim::Balance balance;
    std::vector<std::string> warnings;
    sim::apply_buildings_file(balance, "named",
                              "[\"anno1602:bakery\"]\n"
                              "produce = 2\n"
                              "[\"anno1602:Bakery\"]\n"  // line 3: names are lower case
                              "produce = 3\n",
                              warnings);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] ==
          "named/buildings.toml line 3: [anno1602:Bakery]: 'Bakery' is no number and no name of the original's "
          "buildings");
    REQUIRE(balance.buildings.size() == 1);
    CHECK((balance.buildings[0] == sim::BuildingValue{assets::id::kBakery, sim::BuildingField::Produce, 2}));

    // A unit's name stands for its figures in all four colours.
    warnings.clear();
    sim::apply_figures_file(balance, "named", "[\"anno1602:swordsman\"]\nhealth = 30\n", warnings);
    CHECK(warnings.empty());
    REQUIRE(balance.figures.size() == 4);
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK((balance.figures[i] == sim::FigureValue{1 + i, sim::FigureField::Health, 30}));
    }
    CHECK(sim::valid(balance));
}

TEST_CASE("balance: the names of the original's buildings and figures are unique") {
    for (const auto names : {assets::original_building_names(), assets::original_figure_names()}) {
        for (const assets::OriginalName& n : names) {
            CHECK(assets::find_original_name(names, n.name) == &n);
            CHECK(n.count >= 1);
        }
    }
    // The build menu's buildings, each named once.
    CHECK(assets::find_original_name(assets::original_building_names(), "bakery")->first == assets::id::kBakery);
    CHECK(assets::find_original_name(assets::original_building_names(), "quarry")->first == assets::id::kQuarry);
    CHECK(assets::find_original_name(assets::original_figure_names(), "small_merchant_ship")->first ==
          assets::figure::ship_figure(0));
}

TEST_CASE("balance: a game keeps its figure values in the savegame") {
    const auto buildings = make_buildings();
    sim::World world(60, 40);
    sim::Balance balance;
    sim::set_figure_value(balance, sim::FigureValue{21, sim::FigureField::Cannons, 10});
    sim::set_figure_value(balance, sim::FigureValue{1, sim::FigureField::Health, 30});
    world.set_balance(balance);
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    CHECK(loaded->balance() == balance);

    sim::Balance too_much = balance;
    too_much.figures[0].value = 0;  // health below 1
    world.set_balance(too_much);
    auto damaged = sim::write_game(world, buildings);
    REQUIRE(damaged.ok());
    CHECK(!sim::read_game(*damaged, buildings).ok());
}
