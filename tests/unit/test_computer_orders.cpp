// SPDX-License-Identifier: GPL-3.0-or-later
// The computers' build orders as data (MD-4, docs/modding/computer-players.md): the original's
// orders, a package's computers.toml and the section AIOR that keeps them with the game.

#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/goods.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/computer_orders.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;
using Need = sim::ComputerNeed;
using Kind = sim::ComputerNeed::Kind;
using When = sim::ComputerNeed::When;

namespace {

/// Goods with a package's pepper (ware kFirstOwn).
assets::Goods pepper_goods() {
    assets::Goods goods;
    assets::OwnGood pepper;
    pepper.id = "spice:pepper";
    pepper.price = 40;
    pepper.icon = assets::ware::kFirstTrade;
    REQUIRE(goods.add(pepper).ok());
    return goods;
}

std::vector<sim::ComputerOrder> read(std::string_view text, std::vector<std::string>& warnings,
                                     const assets::Goods& goods = {}) {
    std::vector<sim::ComputerOrder> orders;
    sim::apply_computers_file(orders, "mod", text, warnings, goods);
    return orders;
}

}  // namespace

TEST_CASE("computer orders: every list has the original's order, every entry of it is valid") {
    for (std::size_t i = 0; i < sim::kComputerLists; ++i) {
        const auto list = static_cast<sim::ComputerList>(i);
        const auto order = sim::default_order(list);
        CHECK(!order.empty());
        CHECK(order.size() <= sim::kMaxOrderNeeds);
        for (const Need& n : order) {
            CHECK(sim::valid(n));
        }
        CHECK(sim::order_of({}, list).data() == order.data());
    }
    CHECK(sim::kComputerListNames[0] == "founded");
    CHECK(sim::kComputerListNames[sim::kComputerLists - 1] == "first_crops");
    CHECK(sim::order_text(sim::default_order(sim::ComputerList::Founded), {}) ==
          "market 1, fisher 1, woodcutter 1, houses 8");
}

TEST_CASE("computer orders: the original's orders as text read back as themselves, so nothing is kept") {
    for (std::size_t i = 0; i < sim::kComputerLists; ++i) {
        const std::string text = "[" + std::string(sim::kComputerListNames[i]) + "]\norder = \"" +
                                 sim::order_text(sim::default_order(static_cast<sim::ComputerList>(i)), {}) + "\"\n";
        std::vector<std::string> warnings;
        CHECK(read(text, warnings).empty());
        CHECK(warnings.empty());
    }
}

TEST_CASE("computer orders: a package replaces an order or adds to it") {
    std::vector<std::string> warnings;
    const auto orders = read(
        "[settlers]\n"
        "order = \"market 0, houses 30, chapel 0 covered, cattle 4 gold_4000, tavern 1 covered_8000\"\n"
        "[founded]\n"
        "append = \"tower 2, luxury 4 one_crop, colony 2 never\"\n",
        warnings);
    CHECK(warnings.empty());
    REQUIRE(orders.size() == 2);
    // Sorted by list: founded first.
    CHECK(orders[0].list == sim::ComputerList::Founded);
    CHECK(orders[1].list == sim::ComputerList::Settlers);
    const auto founded = sim::order_of(orders, sim::ComputerList::Founded);
    REQUIRE(founded.size() == 7);
    CHECK((founded[4] == Need{Kind::Tower, 0, 2}));
    CHECK((founded[5] == Need{Kind::Luxury, 0, 4, When::Always}));
    CHECK((founded[6] == Need{Kind::Colony, 0, 2, When::Never, When::Never}));
    const auto settlers = sim::order_of(orders, sim::ComputerList::Settlers);
    REQUIRE(settlers.size() == 5);
    CHECK((settlers[0] == Need{Kind::Market, 0, 0}));
    CHECK((settlers[1] == Need{Kind::Houses, 0, 30}));
    CHECK(settlers[2].kind == Kind::Public);
    CHECK(settlers[2].flag == When::Always);
    CHECK(settlers[3].kind == Kind::Building);
    CHECK(settlers[3].n == 4);
    CHECK(settlers[3].when == When::Gold4000);
    CHECK(settlers[4].flag == When::Gold8000);
    CHECK(sim::order_text(settlers, {}) ==
          "market 0, houses 30, chapel 0 covered, cattle 4 gold_4000, tavern 1 covered_8000");
    // The others stay the original's.
    CHECK(sim::order_of(orders, sim::ComputerList::Main).data() == sim::default_order(sim::ComputerList::Main).data());
    // An empty order: the stage builds nothing of its own.
    const auto empty = read("[main]\norder = \"\"\n", warnings);
    REQUIRE(empty.size() == 1);
    CHECK(sim::order_of(empty, sim::ComputerList::Main).empty());
}

TEST_CASE("computer orders: a later package changes what an earlier one gave") {
    std::vector<sim::ComputerOrder> orders;
    std::vector<std::string> warnings;
    sim::apply_computers_file(orders, "a", "[colony]\norder = \"market 0\"\n", warnings, {});
    sim::apply_computers_file(orders, "b", "[colony]\nappend = \"processing\"\n", warnings, {});
    CHECK(warnings.empty());
    // market 0, processing: the original's order again, which a game does not keep.
    CHECK(orders.empty());
}

TEST_CASE("computer orders: a good of a package is built by its id") {
    const assets::Goods goods = pepper_goods();
    std::vector<std::string> warnings;
    const auto orders = read("[pioneers]\norder = \"spice:pepper 2, spice:salt 1\"\n", warnings, goods);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("spice:salt") != std::string::npos);
    REQUIRE(orders.size() == 1);
    REQUIRE(orders[0].needs.size() == 1);
    CHECK((orders[0].needs[0] == Need{Kind::Package, assets::ware::kFirstOwn, 2}));
    CHECK(sim::order_text(orders[0].needs, goods) == "spice:pepper 2");
}

TEST_CASE("computer orders: what cannot be read is passed over with the file and the line") {
    std::vector<std::string> warnings;
    const auto orders = read(
        "[settlers]\n"
        "order = \"houses 30, palace 1, houses, kontor 5, castle 2, fisher 1 covered, sheep 2 gold_100\"\n"
        "[nowhere]\n"
        "order = \"houses 1\"\n"
        "[main]\n"
        "order = 3\n"
        "prepend = \"houses 1\"\n",
        warnings);
    // palace, houses without a count, kontor 5, castle with a count, fisher with a flag, gold_100;
    // the table [nowhere]; order = 3; prepend.
    CHECK(warnings.size() == 9);
    for (const std::string& w : warnings) {
        CHECK(w.starts_with("mod/computers.toml line "));
    }
    REQUIRE(orders.size() == 1);
    CHECK(sim::order_text(orders[0].needs, {}) == "houses 30");
    // At most kMaxOrderNeeds entries.
    std::string many;
    for (std::size_t i = 0; i < sim::kMaxOrderNeeds + 3; ++i) {
        many += (i == 0 ? "" : ", ") + std::string("houses 1");
    }
    warnings.clear();
    const auto capped = read("[main]\norder = \"" + many + "\"\n", warnings);
    CHECK(warnings.size() == 1);
    REQUIRE(capped.size() == 1);
    CHECK(capped[0].needs.size() == sim::kMaxOrderNeeds);
}

TEST_CASE("computer orders: only what the computers can run is valid") {
    CHECK(sim::valid(Need{Kind::Castle}));
    CHECK(!sim::valid(Need{Kind::Castle, 0, 1}));                             // no count
    CHECK(!sim::valid(Need{Kind::Castle, 0, 0, When::Always}));               // no flag
    CHECK(!sim::valid(Need{Kind::Kontor, 0, 0}));                             // levels 1 to 4
    CHECK(!sim::valid(Need{Kind::ColonyLuxury, 0, 0}));                       // tiles per field
    CHECK(!sim::valid(Need{Kind::Building, 0x0A, 1}));                        // the chapel is a public building
    CHECK(!sim::valid(Need{Kind::Public, 0x1F, 1}));                          // the fisher is none
    CHECK(!sim::valid(Need{Kind::Package, assets::ware::kFirstOwn - 1, 1}));  // a ware of the original
    CHECK(!sim::valid(Need{Kind::Houses, 0, sim::kMaxOrderCount + 1}));
    CHECK(!sim::valid(Need{static_cast<Kind>(sim::ComputerNeed::kKinds), 0, 0}));
    CHECK(!sim::valid(Need{Kind::Houses, 0, 1, When::Never, static_cast<When>(sim::ComputerNeed::kWhens)}));
    // Orders: sorted by list, each once.
    const std::vector<sim::ComputerOrder> twice = {{sim::ComputerList::Main, {}}, {sim::ComputerList::Main, {}}};
    CHECK(!sim::valid(twice));
    const std::vector<sim::ComputerOrder> sorted = {{sim::ComputerList::Founded, {}}, {sim::ComputerList::Main, {}}};
    CHECK(sim::valid(sorted));
    sim::Balance balance;
    balance.computer_orders = twice;
    CHECK(!sim::valid(balance));
}

TEST_CASE("computer orders: the crops of the computers' first colonies are a list of their own") {
    // The original's: ore, spices, tobacco, wine by player % 4.
    CHECK(sim::first_crop({}, 0) == assets::ware::kOre);
    CHECK(sim::first_crop({}, 1) == assets::ware::kSpiceField);
    CHECK(sim::first_crop({}, 2) == assets::ware::kTobaccoField);
    CHECK(sim::first_crop({}, 3) == assets::ware::kWineField);
    CHECK(sim::first_crop({}, 4) == assets::ware::kOre);
    CHECK(sim::order_text(sim::default_order(sim::ComputerList::FirstCrops), {}) ==
          "crop_ore, crop_spices, crop_tobacco, crop_wine");

    std::vector<std::string> warnings;
    const auto orders = read("[first_crops]\norder = \"crop_cocoa, crop_wine\"\n", warnings);
    CHECK(warnings.empty());
    CHECK(sim::first_crop(orders, 0) == assets::ware::kCocoaField);
    CHECK(sim::first_crop(orders, 1) == assets::ware::kWineField);
    CHECK(sim::first_crop(orders, 2) == assets::ware::kCocoaField);
    // No crop: any island.
    const auto none = read("[first_crops]\norder = \"\"\n", warnings);
    CHECK(warnings.empty());
    CHECK(sim::first_crop(none, 0) == 0);

    // A crop elsewhere, a building here, a crop with a condition: passed over.
    const auto wrong = read(
        "[first_crops]\norder = \"crop_sugar gold_4000, market 0, crop_cotton\"\n"
        "[main]\nappend = \"crop_ore\"\n",
        warnings);
    CHECK(warnings.size() == 3);
    CHECK(sim::first_crop(wrong, 7) == assets::ware::kCottonField);
    CHECK(sim::order_of(wrong, sim::ComputerList::Main).data() == sim::default_order(sim::ComputerList::Main).data());
    CHECK(!sim::valid(Need{Kind::Crop, assets::ware::kOre, 0, When::Never, When::Never}));
    CHECK(!sim::valid(Need{Kind::Crop, assets::ware::kFirstOwn, 0}));
}

TEST_CASE("computer orders: a game keeps its orders in the savegame (AIOR), a wrong entry loads nothing") {
    const assets::BuildingTable buildings;
    sim::World world(60, 40);
    std::vector<std::string> warnings;
    sim::Balance balance;
    balance.computer_orders = read(
        "[settlers]\norder = \"market 0, houses 30, cattle 4 gold_4000\"\n[aristocrats2]\nappend = \"monument\"\n"
        "[first_crops]\norder = \"crop_sugar\"\n",
        warnings);
    REQUIRE(balance.computer_orders.size() == 3);
    world.set_balance(balance);
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    CHECK(loaded->balance() == balance);

    balance.computer_orders[0].needs[1].n = sim::kMaxOrderCount + 1;
    world.set_balance(balance);
    auto damaged = sim::write_game(world, buildings);
    REQUIRE(damaged.ok());
    CHECK(!sim::read_game(*damaged, buildings).ok());
}
