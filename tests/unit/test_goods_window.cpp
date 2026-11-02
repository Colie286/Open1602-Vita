// SPDX-License-Identifier: GPL-3.0-or-later
// The window "Warenbilanz" (goods_window.hpp): its figures as text, the rows it shows for the
// local player's cities only, the island and time chosen, and a good's page.

#include <algorithm>
#include <string>

#include "../test_framework.hpp"
#include "goods_window.hpp"
#include "parchment.hpp"

#include "o1602/sim/goods_statistics.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;
using app::GoodsWindow;
using sim::GoodsFlow;

namespace {

constexpr std::uint8_t kFood = 14;
constexpr std::uint8_t kWood = 23;
constexpr std::uint32_t kTon = 32;
constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts{};
const assets::FigureTable kFigures{};

/// A human (0) with cities on islands 0 and 1, and an ally (1) with a city on island 0.
sim::World make_world() {
    sim::World world(200, 200);
    for (std::uint16_t i = 0; i < 2; ++i) {
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, sim::IslandMap(20, 20), i * 50U, 0}).ok());
    }
    std::vector<sim::Player> players(2);
    players[0].role = sim::PlayerRole::Human;
    players[1].role = sim::PlayerRole::Human;
    world.set_players(std::move(players));
    std::vector<sim::City> cities(3);
    const std::array<std::array<std::uint16_t, 3>, 3> at = {{{0, 0, 0}, {0, 1, 1}, {1, 0, 0}}};  // island, index, owner
    const std::array<const char*, 3> names = {"Eastport", "Allytown", "Westport"};
    for (std::size_t i = 0; i < cities.size(); ++i) {
        cities[i].island = at[i][0];
        cities[i].index = static_cast<std::uint8_t>(at[i][1]);
        cities[i].owner = static_cast<sim::PlayerId>(at[i][2]);
        cities[i].name = names[i];
        cities[i].capacity = 30 * kTon;
        cities[i].ensure_ware(kWood);
    }
    world.set_cities(std::move(cities));
    return world;
}

sim::City& city(sim::World& world, std::uint16_t island, std::uint8_t index) {
    sim::City* c = world.city(island, index);
    REQUIRE(c != nullptr);
    return *c;
}

std::vector<std::string> names(std::size_t count) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back("ware" + std::to_string(i));
    }
    return out;
}

bool shows(const GoodsWindow& w, ui::Form::Id id) {
    const auto& list = w.form().widgets();
    return std::ranges::any_of(list, [&](const ui::Form::Widget& x) { return x.id == id; });
}

/// The stock in the table, e.g. "14 t".
std::string tons(int value) {
    return ui::tr("stats.tons", {std::to_string(value)});
}

bool draws_text(const std::vector<ui::DrawItem>& items, const std::string& text) {
    return std::ranges::any_of(
        items, [&](const ui::DrawItem& d) { return d.kind == ui::DrawItem::Kind::Text && d.text == text; });
}

/// A click on the widget's centre (design units at scale 1 are pixels).
void click(GoodsWindow& w, ui::Form::Id id, ui::MouseButton button = ui::MouseButton::Left) {
    for (const ui::Form::Widget& x : w.form().widgets()) {
        if (x.id == id) {
            const std::int32_t cx = x.rect.x + x.rect.w / 2;
            const std::int32_t cy = x.rect.y + x.rect.h / 2;
            (void)w.mouse(cx, cy, true, button);
            (void)w.mouse(cx, cy, false, button);
            return;
        }
    }
    REQUIRE(false);  // no such widget
}

constexpr ui::Form::Id kIsland = 1;
constexpr ui::Form::Id kWindow = 2;
constexpr ui::Form::Id kBack = 5;
constexpr ui::Form::Id kTab = 10;
constexpr ui::Form::Id kRow = 100;

}  // namespace

TEST_CASE("goods window: tons per minute with one decimal, and how long a stock lasts") {
    CHECK(GoodsWindow::rate_text(0, 5) == "0");
    CHECK(GoodsWindow::rate_text(5 * 32, 5) == "1.0");          // 5 t in 5 minutes
    CHECK(GoodsWindow::rate_text(-7 * 16, 5, true) == "-0.7");  // -3.5 t in 5 minutes
    CHECK(GoodsWindow::rate_text(3 * 32, 1, true) == "+3.0");
    CHECK(GoodsWindow::rate_text(1, 60) == "0");  // less than 0.05 t a minute
    CHECK(GoodsWindow::rate_text(32, 0) == "-");
    CHECK(GoodsWindow::lasts_text(std::nullopt).empty());
    CHECK(GoodsWindow::lasts_text(12) == "12 min");
    CHECK(GoodsWindow::lasts_text(0) == "< 1 min");
    CHECK(GoodsWindow::lasts_text(150) == "2 h");
}

TEST_CASE("goods window: only the player's own cities, all islands or one") {
    sim::World world = make_world();
    city(world, 0, 0).goods[kWood] = 10 * kTon;
    city(world, 1, 0).goods[kWood] = 4 * kTon;
    city(world, 0, 1).goods[kWood] = 100 * kTon;  // the ally's: never shown
    city(world, 0, 1).book(kFood, GoodsFlow::Produced, 50 * kTon);
    for (const auto& [island, index] : {std::pair<std::uint16_t, std::uint8_t>{0, 0}, {0, 1}, {1, 0}}) {
        sim::City& c = city(world, island, index);
        c.ledger.close_minute(c.goods);
    }
    GoodsWindow w(app::GoodsStyle{}, names(25), 192);
    w.open();
    auto items = w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(shows(w, kRow + kWood));
    CHECK(!shows(w, kRow + kFood));      // only the ally made food
    CHECK(draws_text(items, tons(14)));  // 10 t + 4 t
    CHECK(!draws_text(items, tons(100)));
    CHECK(!draws_text(items, tons(114)));

    click(w, kIsland);  // the first island: Eastport
    REQUIRE(w.island().has_value());
    CHECK(*w.island() == 0);
    items = w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(draws_text(items, tons(10)));
    CHECK(std::ranges::any_of(w.form().widgets(), [](const ui::Form::Widget& x) { return x.text == "Eastport"; }));
    click(w, kIsland);
    CHECK(*w.island() == 1);
    click(w, kIsland);  // and round to all of them
    CHECK(!w.island().has_value());
    click(w, kIsland, ui::MouseButton::Right);  // backwards: the last island
    CHECK(*w.island() == 1);

    // The ally sees only its own city.
    (void)w.draw(world, assets::BuildingTable{}, kFigures, 1, kNoFonts);
    CHECK(!w.island().has_value());  // its island list has no island 1
    items = w.draw(world, assets::BuildingTable{}, kFigures, 1, kNoFonts);
    CHECK(draws_text(items, tons(100)));
    CHECK(shows(w, kRow + kFood));
}

TEST_CASE("goods window: the time window goes 1, 5, 15, 60 minutes, five at first") {
    GoodsWindow w(app::GoodsStyle{}, names(25), 192);
    sim::World world = make_world();
    (void)w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(w.minutes() == 5);
    click(w, kWindow);
    CHECK(w.minutes() == 15);
    click(w, kWindow);
    CHECK(w.minutes() == 60);
    click(w, kWindow);
    CHECK(w.minutes() == 1);
    click(w, kWindow, ui::MouseButton::Right);
    CHECK(w.minutes() == 60);
}

TEST_CASE("goods window: a click on a good opens its page, Escape goes back to the table") {
    sim::World world = make_world();
    sim::City& c = city(world, 0, 0);
    c.goods[kWood] = 10 * kTon;
    c.book(kWood, GoodsFlow::Produced, 6 * kTon);
    c.book(kWood, GoodsFlow::Construction, 2 * kTon);
    c.ledger.close_minute(c.goods);
    GoodsWindow w(app::GoodsStyle{}, names(25), 192);
    w.open();
    (void)w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    click(w, kRow + kWood);
    REQUIRE(w.detail().has_value());
    CHECK(*w.detail() == kWood);
    const auto items = w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(draws_text(items, "ware23"));
    CHECK(draws_text(items, "+4.0 " + ui::tr("stats.per_minute")));  // 6 t in, 2 t out in the one minute there is
    CHECK(shows(w, kBack));
    CHECK(w.back());
    CHECK(!w.detail().has_value());
    CHECK(!w.back());  // on the table Escape closes the window
}

TEST_CASE("goods window: the parchment is the same on every computer") {
    const std::vector<std::uint8_t> a = app::parchment_pixels(40, 30);
    CHECK(a.size() == 40U * 30U * 4U);
    CHECK(a == app::parchment_pixels(40, 30));
    CHECK(a != app::parchment_pixels(40, 30, 4));
    for (std::size_t i = 3; i < a.size(); i += 4) {
        REQUIRE(a[i] == 255);  // opaque
    }
}

TEST_CASE("goods window: the tabs, and the trade and finances of one's own cities") {
    sim::World world = make_world();
    sim::City& c = city(world, 0, 0);
    c.book(kWood, GoodsFlow::Sold, 4 * kTon);
    c.book(kWood, GoodsFlow::SoldValue, 120);
    c.book(kWood, GoodsFlow::TraderSoldValue, 30);
    c.book(kFood, GoodsFlow::Bought, 2 * kTon);
    c.book(kFood, GoodsFlow::BoughtValue, 50);
    city(world, 0, 1).book(kWood, GoodsFlow::SoldValue, 999);  // the ally's: never shown
    for (const auto& [island, index] : {std::pair<std::uint16_t, std::uint8_t>{0, 0}, {0, 1}}) {
        sim::City& x = city(world, island, index);
        x.ledger.close_minute(x.goods);
    }
    sim::CitySample s;
    s.set(sim::CityFigure::Taxes, 80);
    s.set(sim::CityFigure::RunningCosts, 30);
    s.set(sim::CityFigure::Inhabitants, 150);
    s.set_class(0, 150);
    c.history.close_minute(s);
    c.inhabitants = {150};

    GoodsWindow w(app::GoodsStyle{}, names(25), 192);
    w.open();
    (void)w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(w.tab() == GoodsWindow::Tab::Goods);
    click(w, kTab + static_cast<ui::Form::Id>(GoodsWindow::Tab::Trade));
    CHECK(w.tab() == GoodsWindow::Tab::Trade);
    auto items = w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(draws_text(items, "ware23"));
    CHECK(draws_text(items, "120"));  // earned with wood
    CHECK(draws_text(items, "30"));   // average price per ton, and the free trader's share
    CHECK(draws_text(items, "50"));   // paid for food
    CHECK(draws_text(items, "+70"));  // the balance of all
    CHECK(!draws_text(items, "999"));
    click(w, kRow + kWood);  // a good of the trade opens its page
    CHECK(w.detail() == std::optional<std::uint8_t>(kWood));
    CHECK(w.back());
    CHECK(w.tab() == GoodsWindow::Tab::Trade);

    click(w, kTab + static_cast<ui::Form::Id>(GoodsWindow::Tab::Finances));
    items = w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(draws_text(items, "80"));  // the taxes of the last minute on average
    click(w, kTab + static_cast<ui::Form::Id>(GoodsWindow::Tab::Population));
    items = w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(draws_text(items, "150"));
    click(w, kTab + static_cast<ui::Form::Id>(GoodsWindow::Tab::Producers));
    items = w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(draws_text(items, ui::tr("stats.prod.empty")));
}

TEST_CASE("goods window: a good the original does not know (a mod's) shows under further goods") {
    constexpr std::uint8_t kModWare = 30;
    sim::World world = make_world();
    sim::City& c = city(world, 0, 0);
    c.ensure_ware(kModWare);
    c.goods[kModWare] = 6 * kTon;
    c.book(kModWare, GoodsFlow::Produced, 2 * kTon);
    c.ledger.close_minute(c.goods);
    GoodsWindow w(app::GoodsStyle{}, names(31), 192);
    w.open();
    const auto items = w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(shows(w, kRow + kModWare));
    CHECK(draws_text(items, ui::tr("stats.group.other")));
    CHECK(draws_text(items, "ware30"));
    // Not for the ally, who never booked it.
    (void)w.draw(world, assets::BuildingTable{}, kFigures, 1, kNoFonts);
    CHECK(!shows(w, kRow + kModWare));
}

TEST_CASE("goods window: eight classes of inhabitants, each a row with its tax and a colour of its own") {
    sim::World world = make_world();
    std::vector<assets::ResidentClass> classes(sim::kCityClasses);
    for (std::size_t k = 0; k < classes.size(); ++k) {
        classes[k].id = "mod:class" + std::to_string(k);
        classes[k].names = {{"en", "Class" + std::to_string(k)}};
        classes[k].max_per_house = 10;
    }
    assets::BuildingTable buildings;
    buildings.set_resident_classes(classes);
    sim::City& c = city(world, 0, 0);
    c.inhabitants.assign(sim::kCityClasses, 0);
    c.satisfaction.assign(sim::kCityClasses, 128);
    c.tax_rate.assign(sim::kCityClasses, 128);
    for (std::size_t k = 0; k < sim::kCityClasses; ++k) {
        c.inhabitants[k] = static_cast<std::uint32_t>(10 + k);
        c.tax_rate[k] = static_cast<std::uint8_t>(64 + 16 * k);
    }

    app::GoodsStyle style;
    for (std::size_t i = 0; i < style.series.size(); ++i) {
        style.series[i] = static_cast<std::uint8_t>(200 + i);
    }
    GoodsWindow w(style, names(25), 192);
    w.open();
    (void)w.draw(world, buildings, kFigures, 0, kNoFonts);
    click(w, kTab + static_cast<ui::Form::Id>(GoodsWindow::Tab::Population));
    const auto items = w.draw(world, buildings, kFigures, 0, kNoFonts);
    std::int32_t last_row = 0;
    for (std::size_t k = 0; k < sim::kCityClasses; ++k) {
        CHECK(draws_text(items, "Class" + std::to_string(k)));
        CHECK(draws_text(items, ui::tr("stats.percent", {std::to_string((64 + 16 * k) / 4)})));
        // Its box in the table: a colour no other class has.
        const auto box = std::ranges::find_if(items, [&](const ui::DrawItem& d) {
            return d.kind == ui::DrawItem::Kind::Fill && d.w == 10 && d.h == 10 &&
                   d.colour == static_cast<std::int32_t>(200 + k);
        });
        REQUIRE(box != items.end());
        CHECK(box->y > last_row);
        last_row = box->y;
    }
    CHECK(last_row < GoodsWindow::kHeight - 200);  // the chart keeps its room below the table
}

TEST_CASE("goods window: the tab \"Routes\" lists the player's ships on trade routes, ordered and to go to") {
    sim::World world = make_world();
    const auto ship = [&](std::uint32_t id, const char* name, sim::PlayerId owner, std::uint16_t island,
                          std::uint8_t index, bool problem) {
        sim::Ship s;
        s.id = id;
        s.name = name;
        s.owner = owner;
        s.x = static_cast<std::uint16_t>(10 * id);
        s.y = 5;
        s.route.active = true;
        s.route.stops[0].target = sim::Target::city_of(island, index);
        s.route.stops[0].entries[0] = sim::RouteEntry{kWood, false, 10 * kTon};
        s.route.stops[1].target = sim::Target::city_of(0, 0);
        s.route.stops[1].entries[0] = sim::RouteEntry{kFood, true, 10 * kTon};
        s.route.current = 1;
        s.route.problem = problem;
        world.add_ship(std::move(s));
    };
    ship(1, "Zephyr", 0, 1, 0, false);
    ship(2, "Albatross", 0, 1, 0, true);
    ship(3, "Theirs", 1, 0, 1, false);  // the ally's: never shown
    sim::Ship idle;
    idle.id = 4;
    idle.name = "Idle";
    world.add_ship(std::move(idle));  // no route

    GoodsWindow w(app::GoodsStyle{}, names(25), 192);
    w.open();
    (void)w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    click(w, kTab + static_cast<ui::Form::Id>(GoodsWindow::Tab::Routes));
    auto items = w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(draws_text(items, "Westport - Eastport"));
    CHECK(draws_text(items, "Eastport"));  // sailing to
    CHECK(!draws_text(items, "Theirs"));
    CHECK(!draws_text(items, "Idle"));
    REQUIRE(w.route_ships().size() == 2);
    CHECK(w.route_ships()[0].id == 2);  // by name: Albatross first

    constexpr ui::Form::Id kRouteSort = 60;
    click(w, kRouteSort + static_cast<ui::Form::Id>(GoodsWindow::RouteOrder::Problems));
    CHECK(w.route_order() == GoodsWindow::RouteOrder::Problems);
    (void)w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(w.route_ships()[0].id == 2);  // the one with trouble first

    // A click on a row hands the ship over.
    constexpr ui::Form::Id kRouteRow = 2000;
    for (const ui::Form::Widget& x : w.form().widgets()) {
        if (x.id == kRouteRow + 1) {
            const std::int32_t cx = x.rect.x + x.rect.w / 2;
            const std::int32_t cy = x.rect.y + x.rect.h / 2;
            (void)w.mouse(cx, cy, true);
            CHECK(w.mouse(cx, cy, false) == GoodsWindow::Result::Ship);
        }
    }
    const auto target = w.take_ship();
    REQUIRE(target.has_value());
    CHECK(target->id == 1);
    CHECK(target->x == 10.0);
    CHECK(!w.take_ship().has_value());

    // One island chosen: only the routes calling there (both call at island 0).
    click(w, kIsland);
    (void)w.draw(world, assets::BuildingTable{}, kFigures, 0, kNoFonts);
    CHECK(w.route_ships().size() == 2);
}
