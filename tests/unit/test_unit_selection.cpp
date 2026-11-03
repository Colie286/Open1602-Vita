// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <variant>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/command_io.hpp"
#include "o1602/ui/unit_selection.hpp"

using namespace o1602;
using ui::UnitRef;

namespace {

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}, {"FORMATION", 2}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 1\n  Kind: 1\n  Gfx: 0\n  Maxenergy: 20\n"
        "  Nummer: 9\n  Kind: 3\n  Gfx: 0\n  Maxenergy: 15\n"
        "  Nummer: 21\n  Gfx: 0\n  Maxenergy: 40\n"
        "EndObj\n"
        "Objekt: FORMATION\n"
        "  Nummer: 0\n  Offs: 0, 0, 0\n  Offs: 1, -1, 0\n  Offs: 2, 1, 0\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

struct Field {
    assets::FigureTable figures = make_figures();
    sim::World world{80, 60};
    Field() {
        std::vector<sim::Player> players(2);
        players[1].role = sim::PlayerRole::Computer;
        world.set_players(players);
    }
    UnitRef ship(std::uint32_t x, std::uint32_t y, sim::PlayerId owner = 0) {
        world.add_ship(sim::make_ship("S", x, y, 21, owner, 0));
        return {UnitRef::Kind::Ship, world.ships().back().id};
    }
    UnitRef cart(std::uint32_t x, std::uint32_t y, sim::PlayerId owner = 0) {
        world.add_ship(sim::make_ship("C", x, y, assets::figure::kTradeCart, owner, 0));
        return {UnitRef::Kind::Ship, world.ships().back().id};
    }
    UnitRef soldier(std::uint32_t x, std::uint32_t y, std::uint16_t figure = 1, sim::PlayerId owner = 0) {
        world.add_soldier(sim::make_soldier(x, y, figure, owner));
        return {UnitRef::Kind::Soldier, world.soldiers().back().id};
    }
};

}  // namespace

TEST_CASE("unit selection: the rectangle takes own units of the kind with the largest part inside") {
    Field f;
    const UnitRef s1 = f.soldier(10, 10);
    const UnitRef s2 = f.soldier(12, 10);
    const UnitRef ship = f.ship(30, 30);
    const UnitRef enemy = f.soldier(11, 11, 1, 1);
    ui::UnitSelection sel;
    sel.box(f.world, {{s1, 100}, {s2, 50}, {ship, 120}, {enemy, 500}}, 0, false);
    // Own units only; the ship has the largest part: only ships.
    REQUIRE(sel.size() == 1);
    CHECK(sel.units()[0] == ship);
    sel.box(f.world, {{s1, 100}, {s2, 50}, {ship, 20}}, 0, false);
    CHECK(sel.size() == 2);
    CHECK(sel.kind() == UnitRef::Kind::Soldier);
    // Only somebody else's: one of them, to look at.
    sel.box(f.world, {{enemy, 10}}, 0, false);
    CHECK(sel.size() == 1);
    CHECK(sel.owner(f.world) == 1);
    // An empty rectangle changes nothing; Shift adds and toggles.
    sel.box(f.world, {{s1, 10}}, 0, false);
    sel.box(f.world, {}, 0, false);
    CHECK(sel.size() == 1);
    sel.toggle(f.world, s2);
    CHECK(sel.size() == 2);
    sel.toggle(f.world, s1);
    CHECK(sel.size() == 1);
    CHECK(sel.units()[0] == s2);
    sel.toggle(f.world, ship);  // another kind: alone
    CHECK(sel.units() == std::vector<UnitRef>{ship});
}

TEST_CASE("unit selection: the rectangle takes a trade cart alone, never with ships or soldiers") {
    Field f;
    const UnitRef cart = f.cart(10, 10);
    const UnitRef ship = f.ship(30, 30);
    const UnitRef s1 = f.soldier(12, 10);
    ui::UnitSelection sel;
    // Only the cart in the rectangle: it is selected.
    sel.box(f.world, {{cart, 40}}, 0, false);
    CHECK(sel.units() == std::vector<UnitRef>{cart});
    // The cart has the largest part: the cart alone, not the ship beside it.
    sel.box(f.world, {{cart, 100}, {ship, 50}, {s1, 20}}, 0, false);
    CHECK(sel.units() == std::vector<UnitRef>{cart});
    // A ship has the largest part: the ships without the cart.
    sel.box(f.world, {{cart, 100}, {ship, 150}}, 0, false);
    CHECK(sel.units() == std::vector<UnitRef>{ship});
    // Shift adds to the ships, but not the cart: a rectangle over the cart alone changes nothing.
    sel.box(f.world, {{cart, 100}}, 0, true);
    CHECK(sel.units() == std::vector<UnitRef>{ship});
    // Shift + click: the cart does not join the ships, it is selected alone, and a ship not with it.
    sel.toggle(f.world, cart);
    CHECK(sel.units() == std::vector<UnitRef>{cart});
    sel.toggle(f.world, ship);
    CHECK(sel.units() == std::vector<UnitRef>{ship});
}

TEST_CASE("unit selection: any number, each once; units that are gone leave it") {
    Field f;
    std::vector<ui::UnitCandidate> many;
    for (std::uint32_t k = 0; k < 30; ++k) {
        many.push_back({f.soldier(5 + k, 20), 10});
    }
    many.push_back(many.front());
    ui::UnitSelection sel;
    sel.box(f.world, many, 0, false);
    CHECK(sel.size() == 30);  // more than the original's 21 (LC-3)
    f.world.soldier(sel.units()[0].id)->motion.phase = sim::SoldierMotion::Phase::Dying;
    CHECK(sel.prune(f.world));
    CHECK(sel.size() == 29);
}

TEST_CASE("unit selection: a box over thousands takes them all, formations add rows, orders come in parts (LC-3)") {
    Field f;
    sim::World big{400, 400};
    std::vector<sim::Player> players(1);
    big.set_players(players);
    std::vector<ui::UnitCandidate> army;
    for (std::uint32_t k = 0; k < 5000; ++k) {
        big.add_soldier(sim::make_soldier(20 + k % 100, 20 + k / 100, 1, 0));
        army.push_back({{UnitRef::Kind::Soldier, big.soldiers().back().id}, 10});
    }
    ui::UnitSelection sel;
    sel.box(big, army, 0, false);
    REQUIRE(sel.size() == 5000);
    // Formation 0 has three places: copies of it fill rows behind, every unit gets a place of its own.
    sel.formation(big, f.figures, 0);
    std::vector<std::pair<std::int16_t, std::int16_t>> places(sel.offsets().begin(), sel.offsets().end());
    std::sort(places.begin(), places.end());
    CHECK(std::adjacent_find(places.begin(), places.end()) == places.end());
    const auto [lo, hi] = std::minmax_element(places.begin(), places.end(),
                                              [](const auto& a, const auto& b) { return a.second < b.second; });
    CHECK(lo->second < 0);  // centred: rows before and behind the middle
    CHECK(hi->second > 0);
    // A move is two orders of at most kMaxCommandItems units, with their offsets.
    const auto orders = sel.move_orders(big, 0, 200.0, 200.0, false);
    REQUIRE(orders.size() == 2);
    std::size_t ordered = 0;
    for (const sim::Command& c : orders) {
        const auto* o = std::get_if<sim::SoldierOrder>(&c);
        REQUIRE(o != nullptr);
        CHECK(o->soldiers.size() <= sim::kMaxCommandItems);
        CHECK(o->offsets.size() == o->soldiers.size());
        ordered += o->soldiers.size();
    }
    CHECK(ordered == 5000);
    // A group keeps them all.
    sel.store_group(1);
    sel.clear();
    CHECK(sel.recall_group(big, 1));
    CHECK(sel.size() == 5000);
}

TEST_CASE("unit selection: a move keeps the shape of the group, only own units are ordered") {
    Field f;
    const UnitRef a = f.soldier(10, 10);
    const UnitRef b = f.soldier(12, 10);
    const UnitRef s1 = f.ship(40, 40);
    const UnitRef s2 = f.ship(44, 40);
    ui::UnitSelection sel;
    sel.box(f.world, {{a, 1}, {b, 1}}, 0, false);
    // Units stand on the upper left cell of their tile: centre (11.25, 10.25), offsets -8 and 8.
    CHECK((sel.offsets()[0] == ui::UnitSelection::Offset{-8, 0}));
    auto orders = sel.move_orders(f.world, 0, 20.5, 30.5, true);
    REQUIRE(orders.size() == 1);
    const auto* o = std::get_if<sim::SoldierOrder>(&orders[0]);
    REQUIRE(o != nullptr);
    CHECK(o->target == sim::Target::spot(41, 61));
    CHECK(o->offsets.size() == 2);
    CHECK(o->queue);
    sel.box(f.world, {{s1, 1}, {s2, 1}}, 0, false);
    orders = sel.move_orders(f.world, 0, 20.5, 30.5, false);
    REQUIRE(orders.size() == 2);
    CHECK(std::get<sim::ShipOrder>(orders[0]).target == sim::Target::position(18, 30));
    CHECK(std::get<sim::ShipOrder>(orders[1]).target == sim::Target::position(22, 30));
    // Somebody else's selection gives no orders.
    sel.select(f.world, f.ship(50, 50, 1));
    CHECK(sel.move_orders(f.world, 0, 1, 1, false).empty());
}

TEST_CASE("unit selection: formation, filters and groups") {
    Field f;
    const UnitRef a = f.soldier(10, 10);
    const UnitRef b = f.soldier(10, 14, 9);
    const UnitRef c = f.soldier(10, 18);
    ui::UnitSelection sel;
    sel.box(f.world, {{a, 1}, {b, 1}, {c, 1}}, 0, false);
    // Kinds: infantry (1) and musketeers (3).
    CHECK(sel.kind_mask(f.world, f.figures) == ((1U << 1) | (1U << 3)));
    // A line around the centre: each to the nearest free place.
    sel.formation(f.world, f.figures, 0);
    std::vector<ui::UnitSelection::Offset> offsets = sel.offsets();
    std::sort(offsets.begin(), offsets.end());
    CHECK((offsets == std::vector<ui::UnitSelection::Offset>{{-8, 0}, {0, 0}, {8, 0}}));
    sel.store_group(1);
    sel.filter_kinds(f.world, f.figures, 1U << 3, true);  // only musketeers
    CHECK(sel.units() == std::vector<UnitRef>{b});
    f.world.soldier(a.id)->health = 8 * 32;  // 8 of 20
    CHECK(sel.recall_group(f.world, 1));
    CHECK(sel.size() == 3);
    CHECK(sel.group_of(c) == 1U);
    sel.filter_wounded(f.world, f.figures, true);
    CHECK(sel.units() == std::vector<UnitRef>{a});
    // Shift + number adds without changing the selection.
    sel.select(f.world, f.soldier(30, 30));
    sel.add_to_group(f.world, 1);
    CHECK(sel.size() == 1);
    CHECK(sel.recall_group(f.world, 1));
    CHECK(sel.size() == 4);
    CHECK(!sel.recall_group(f.world, 5));
    // Saved with the game: the same groups with their shape after loading.
    const sim::UnitGroups saved = sel.saved_groups();
    CHECK(saved[1].size() == 4);
    CHECK(saved[0].empty());
    // Another game loaded: the groups go with the old one.
    sel.reset();
    CHECK(sel.size() == 0);
    CHECK(!sel.recall_group(f.world, 1));
    CHECK(!sel.group_of(c));
    // This game loaded again: they come back from the savegame.
    sel.restore_groups(saved);
    CHECK(sel.group_of(c) == 1U);
    CHECK(sel.recall_group(f.world, 1));
    CHECK(sel.size() == 4);
    CHECK(sel.saved_groups() == saved);
}

TEST_CASE("unit selection: Tab narrows to one type after the other, then back to all") {
    Field f;
    const UnitRef a = f.soldier(10, 10, 1);
    const UnitRef b = f.soldier(11, 10, 9);
    const UnitRef c = f.soldier(12, 10, 1);
    ui::UnitSelection sel;
    sel.box(f.world, {{a, 10}, {b, 10}, {c, 10}}, 0, false);
    REQUIRE(sel.size() == 3);
    CHECK(sel.cycle_types(f.world));
    CHECK(sel.size() == 2);  // figure 1
    CHECK(sel.contains(a));
    CHECK(sel.contains(c));
    CHECK(sel.cycle_types(f.world));
    CHECK(sel.size() == 1);  // figure 9
    CHECK(sel.contains(b));
    CHECK(sel.cycle_types(f.world));
    CHECK(sel.size() == 3);  // all again
    CHECK(sel.cycle_types(f.world));
    CHECK(sel.size() == 2);
    // Another selection starts anew; one type alone does not cycle.
    sel.select(f.world, b);
    CHECK(!sel.cycle_types(f.world));
    CHECK(sel.size() == 1);
}

TEST_CASE("unit selection: all own units of one kind on screen (History Edition, Alt+1 .. Alt+4)") {
    Field f;
    const UnitRef a = f.soldier(10, 10);        // infantry
    const UnitRef b = f.soldier(10, 14, 9);     // musketeer
    const UnitRef c = f.soldier(10, 18);        // infantry
    const UnitRef d = f.soldier(12, 18, 1, 1);  // another player's infantry
    const UnitRef ship = f.ship(30, 30);
    const std::vector<ui::UnitCandidate> screen = {{a, 1}, {b, 1}, {c, 1}, {d, 1}, {ship, 1}};
    ui::UnitSelection sel;
    sel.select(f.world, b);
    CHECK(sel.select_kind(f.world, f.figures, screen, 0, 1));
    CHECK((sel.units() == std::vector<UnitRef>{a, c}));
    CHECK(sel.select_kind(f.world, f.figures, screen, 0, 3));
    CHECK(sel.units() == std::vector<UnitRef>{b});
    // No cannoneer on screen: the selection stays.
    CHECK(!sel.select_kind(f.world, f.figures, screen, 0, 4));
    CHECK(sel.units() == std::vector<UnitRef>{b});
}
