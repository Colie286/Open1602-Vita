// SPDX-License-Identifier: GPL-3.0-or-later
// A fresh selection against a repeated click (engine/app/src/selection.hpp): a window opens on its
// first page, the same Kontor or market place clicked again shows its stock.

#include <cstdint>

#include "../test_framework.hpp"
#include "selection.hpp"

using namespace o1602;
using Change = app::SelectionPages::Change;

namespace {

app::Selection building(std::uint16_t x, std::uint16_t y) {
    app::Selection s;
    s.kind = app::Selection::Kind::Building;
    s.island = 2;
    s.x = x;
    s.y = y;
    return s;
}

/// The side panel and one window with sub-pages, as MapInput and Hud drive them.
struct Panel {
    app::Selection selection;
    std::uint32_t serial = 0;
    app::SelectionPages pages;

    /// A left click on the map that hits `picked` (or nothing).
    Change click(const app::Selection& picked) {
        set(app::clicked_selection(selection, picked));
        return pages.update(selection);
    }
    /// A right click: back to the player status.
    void deselect() { set(app::Selection{}); }
    void set(const app::Selection& next) { selection = app::stamped_selection(selection, next, serial); }
};

}  // namespace

TEST_CASE("selection: a double click on a Kontor shows its stock") {
    Panel p;
    const app::Selection kontor = building(10, 12);
    CHECK(p.click(kontor) == Change::Opened);
    CHECK(p.selection.again == 0);
    CHECK(p.click(kontor) == Change::Again);
    CHECK(p.selection.again == 1);
    // The window shows the stock; a further frame changes nothing.
    CHECK(p.pages.update(p.selection) == Change::None);
    // Clicked once more (after going back to the city page in the window): the stock again.
    CHECK(p.click(kontor) == Change::Again);
}

TEST_CASE("selection: after a deselection the same building opens on its first page") {
    Panel p;
    const app::Selection kontor = building(10, 12);
    CHECK(p.click(kontor) == Change::Opened);
    CHECK(p.click(kontor) == Change::Again);  // the stock
    p.deselect();
    // The window is not shown while nothing is selected, so it sees no update.
    CHECK(p.click(kontor) == Change::Opened);
    CHECK(p.selection.again == 0);
}

TEST_CASE("selection: another building in between starts anew") {
    Panel p;
    const app::Selection kontor = building(10, 12);
    const app::Selection market = building(20, 22);
    CHECK(p.click(kontor) == Change::Opened);
    CHECK(p.click(market) == Change::Opened);
    CHECK(p.click(kontor) == Change::Opened);
    CHECK(p.click(kontor) == Change::Again);
}

TEST_CASE("selection: a click beside the buildings deselects") {
    Panel p;
    const app::Selection kontor = building(10, 12);
    CHECK(p.click(kontor) == Change::Opened);
    (void)p.click(app::Selection{});
    CHECK(p.selection.kind == app::Selection::Kind::None);
    CHECK(p.click(kontor) == Change::Opened);
}

TEST_CASE("selection: the same object keeps its serial, a new one gets another") {
    std::uint32_t counter = 0;
    app::Selection ship;
    ship.kind = app::Selection::Kind::Ship;
    ship.ship = 7;
    const app::Selection first = app::stamped_selection(app::Selection{}, ship, counter);
    const app::Selection same = app::stamped_selection(first, ship, counter);
    CHECK(same.serial == first.serial);
    ship.ship = 8;
    const app::Selection other = app::stamped_selection(same, ship, counter);
    CHECK(other.serial != same.serial);
    // Only buildings count repeated clicks.
    CHECK(app::clicked_selection(other, other).again == 0);
}

TEST_CASE("selection: a building or unit selected from the settings shows in the info mode") {
    namespace m = app::side_mode;
    // The settings handle the click like the info mode and switch to it.
    CHECK(app::mode_after_selecting(m::kOptions, true) == m::kInfo);
    // A click on nothing keeps the settings open.
    CHECK(app::mode_after_selecting(m::kOptions, false) == m::kOptions);
    // The other modes stay: the military mode shows the selection itself, the build mode builds.
    CHECK(app::mode_after_selecting(m::kInfo, true) == m::kInfo);
    CHECK(app::mode_after_selecting(m::kMilitary, true) == m::kMilitary);
    CHECK(app::mode_after_selecting(m::kBuild, true) == m::kBuild);
}

TEST_CASE("selection: a switch shows the state clicked for until the game has it") {
    app::PendingSwitch lock;
    CHECK(!lock.shown(7, false, 1000));
    // Clicked on: shown on while the order is on its way, the game still saying off.
    CHECK(lock.next(7, false, 1000));
    lock.clicked(7, true, 1000);
    CHECK(lock.shown(7, false, 1010));
    CHECK(lock.shown(7, false, 1500));
    // The game has it: the game's state from now on, also when it changes again by itself.
    CHECK(lock.shown(7, true, 1600));
    CHECK(!lock.shown(7, false, 1700));
    // Clicked off: shown off at once.
    lock.clicked(7, false, 2000);
    CHECK(!lock.shown(7, true, 2010));
    // Another object shows its own state.
    CHECK(lock.shown(8, true, 2020));
    CHECK(lock.shown(7, true, 2030));
    // An order that never went through: the game's state again after the timeout.
    lock.clicked(7, true, 3000);
    CHECK(lock.shown(7, false, 3000 + app::PendingSwitch::kTimeoutMs - 1));
    CHECK(!lock.shown(7, false, 3000 + app::PendingSwitch::kTimeoutMs));
}
