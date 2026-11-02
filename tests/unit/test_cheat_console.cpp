// SPDX-License-Identifier: GPL-3.0-or-later
// The number line of the cheats (cheat_console.hpp): the keys as in the original and the orders
// they give.

#include "../test_framework.hpp"
#include "cheat_console.hpp"

using namespace o1602;
using app::CheatConsole;
using Key = CheatConsole::Key;
using Op = sim::CheatOrder::Op;

namespace {

void type(CheatConsole& c, const char* digits) {
    for (const char* d = digits; *d != 0; ++d) {
        CHECK(c.key(Key::Digit, *d, false, 0, {}).taken);
    }
}

}  // namespace

TEST_CASE("cheat console: the code is typed in the line and taken with a, as in the original") {
    CheatConsole c;
    const CheatConsole::Target city{std::nullopt, 40, 50};
    CHECK(!c.key(Key::A, 0, false, 0, city).taken);  // nothing typed: "a" is the game's
    CHECK(c.key(Key::Open, 0, false, 0, city).taken);
    CHECK(c.open());
    type(c, "20611");
    CHECK(c.key(Key::Backspace, 0, false, 0, city).taken);
    CHECK(c.line() == "2061");
    CHECK(c.key(Key::A, 0, false, 0, city).taken);  // typed into the line: nothing happens
    CHECK(c.key(Key::Enter, 0, false, 0, city).taken);
    CHECK(!c.open());
    CHECK(c.line() == "2061");  // the number stays
    auto r = c.key(Key::A, 0, false, 2, city);
    REQUIRE(r.order.has_value());
    CHECK(r.order->op == Op::Code);
    CHECK(r.order->code == 2061);
    CHECK(r.order->player == 2);
    // Again: switches them off (the simulation decides).
    CHECK(c.key(Key::A, 0, true, 2, city).order->op == Op::Code);

    // Escape closes the line and forgets the number.
    c.key(Key::Open, 0, false, 0, city);
    type(c, "12");
    c.key(Key::Escape, 0, false, 0, city);
    CHECK(!c.open());
    CHECK(c.line().empty());
}

TEST_CASE("cheat console: with the cheats on, gold, goods, cannons and the volcano") {
    CheatConsole c;
    const CheatConsole::Target city{std::nullopt, 40, 50};
    const CheatConsole::Target ship{7U, 40, 50};
    CHECK(!c.key(Key::ShiftM, 0, false, 0, city).taken);  // off: the keys stay the game's
    CHECK(!c.key(Key::V, 0, false, 0, city).taken);
    auto r = c.key(Key::ShiftM, 0, true, 0, city);
    REQUIRE(r.order.has_value());
    CHECK(r.order->op == Op::Gold);
    CHECK(r.order->x == 40);
    CHECK(r.order->y == 50);
    CHECK(!r.order->ship.has_value());
    CHECK(c.key(Key::ShiftZ, 0, true, 0, city).order->ware == 24);
    CHECK(c.key(Key::ShiftT, 0, true, 0, city).order->ware == 22);
    CHECK(c.key(Key::ShiftH, 0, true, 0, city).order->ware == 23);
    CHECK(c.key(Key::ShiftK, 0, true, 0, ship).order->op == Op::Cannon);
    CHECK(c.key(Key::ShiftK, 0, true, 0, ship).order->ship == std::optional<std::uint32_t>(7U));
    const auto cannons = c.key(Key::ShiftK, 0, true, 0, city).order;
    CHECK(cannons->op == Op::Goods);
    CHECK(cannons->ware == 13);
    CHECK(c.key(Key::V, 0, true, 0, city).order->op == Op::Volcano);

    // A ware's number, then "a": 5 t of it; 0: gold.
    c.key(Key::Open, 0, true, 0, city);
    type(c, "16");
    c.key(Key::Enter, 0, true, 0, city);
    r = c.key(Key::A, 0, true, 0, ship);
    CHECK(r.order->op == Op::Goods);
    CHECK(r.order->ware == 16);
    CHECK(!c.key(Key::A, 0, false, 0, ship).taken);  // off: not a code, so "a" is the game's
    c.key(Key::Open, 0, true, 0, city);
    type(c, "0");
    c.key(Key::Enter, 0, true, 0, city);
    CHECK(c.key(Key::A, 0, true, 0, city).order->op == Op::Gold);
}
