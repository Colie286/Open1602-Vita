// SPDX-License-Identifier: GPL-3.0-or-later
// The tutorial's steps (engine/app/src/tutorial.hpp): which page its window
// shows and which help bubbles point where, chapter by chapter.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../test_framework.hpp"
#include "tutorial.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;
using app::Tutorial;
using app::TutorialFacts;
using app::TutorialView;
namespace bubble = app::bubble;
namespace action = app::ship_action;

namespace {

using Bubbles = std::vector<std::uint8_t>;

TutorialView info(TutorialView::Window window = TutorialView::Window::None) {
    TutorialView v;
    v.mode = TutorialView::Mode::Info;
    v.window = window;
    return v;
}

TutorialView mode(TutorialView::Mode m) {
    TutorialView v;
    v.mode = m;
    return v;
}

/// The ship window open on the human's ship lying still with the given actions.
TutorialFacts ship(std::uint8_t actions, bool loaded = false) {
    TutorialFacts f;
    f.ships[0].exists = true;
    f.ships[0].actions = actions;
    f.ships[0].loaded = loaded;
    return f;
}

/// Chapter 1 with all buildings of the steps before `step` in place.
TutorialFacts::Settlement settled_up_to(std::int32_t step) {
    TutorialFacts::Settlement s;
    s.foresters = step > 9 ? 1 : 0;
    s.fishers = step > 10 ? 1 : 0;
    s.foresters_connected = s.fishers_connected = step > 11;
    s.houses = step > 12 ? 4 : 0;
    s.sheep_farms = step > 14 ? 1 : 0;
    s.weavers = step > 15 ? 1 : 0;
    s.weavers_connected = step > 16;
    s.markets = step > 17 ? 1 : 0;
    return s;
}

}  // namespace

TEST_CASE("tutorial: chapters, pages and the speech") {
    CHECK(Tutorial::first_page(0) == 0);
    CHECK(Tutorial::first_page(2) == 19);
    CHECK(Tutorial::last_page(1) == 18);
    CHECK(Tutorial::last_page(4) == 42);
    CHECK(Tutorial::speech(0) == 1023);
    CHECK(Tutorial::speech(4) == 1098);
    CHECK(Tutorial::next_chapter(0) == std::optional<std::uint8_t>(1));
    CHECK(!Tutorial::next_chapter(4));
    // Only chapter 0 turns a page by hand, once.
    Tutorial t(0);
    CHECK(t.page() == 0);
    CHECK(t.can_page_forward());
    t.page_forward();
    CHECK(t.page() == 1);
    CHECK(!t.can_page_forward());
    t.page_forward();
    CHECK(t.page() == 1);
    Tutorial later(3);
    CHECK(later.page() == 28);
    CHECK(!later.can_page_forward());
}

TEST_CASE("tutorial: chapter 0 explains the frame first") {
    Tutorial t(0);
    CHECK((t.step(info(), {}) ==
           Bubbles{bubble::kWorldMap, bubble::kZoom, bubble::kCompass, bubble::kNext, bubble::kEnd}));
    t.page_forward();
    CHECK(t.step(info(), {}) == Bubbles{bubble::kClickShip});
    CHECK(t.step(mode(TutorialView::Mode::Build), {}) == Bubbles{bubble::kInfoMode});
    CHECK(t.step(mode(TutorialView::Mode::Military), {}) == Bubbles{bubble::kInfoMode});
}

TEST_CASE("tutorial: chapter 0 leads the ship to an island with ore and founds a Kontor") {
    Tutorial t(0);
    t.page_forward();
    const TutorialView window = info(TutorialView::Window::Ship);
    CHECK(t.step(window, ship(0, true)) == Bubbles{bubble::kSailToIsland});
    CHECK(t.page() == 1);
    CHECK(t.step(window, ship(action::kIsland, true)) == Bubbles{bubble::kExplore});
    CHECK(t.page() == 2);
    TutorialFacts barren = ship(action::kIsland | action::kExplored, true);
    CHECK(t.step(window, barren) == Bubbles{bubble::kNoOre});
    CHECK(t.page() == 3);
    TutorialFacts ore = ship(action::kIsland | action::kExplored | action::kFound, true);
    ore.ships[0].island_ore = true;
    CHECK((t.step(window, ore) == Bubbles{bubble::kStatusLine, bubble::kFoundKontor}));
    CHECK(t.page() == 4);
    // Under way nothing changes.
    TutorialFacts sailing = ore;
    sailing.ships[0].moving = true;
    CHECK(t.step(window, sailing).empty());
    CHECK(t.page() == 4);
    // The Kontor stands; the ship is near but not at it, then at it.
    TutorialFacts near = ship(action::kIsland | action::kExplored | action::kOwnCity, true);
    near.ships[0].island_ore = true;
    CHECK(t.step(window, near) == Bubbles{bubble::kToKontor});
    CHECK(t.page() == 5);
    TutorialFacts at = ship(action::kIsland | action::kExplored | action::kOwnCity | action::kTrade, true);
    at.ships[0].island_ore = true;
    CHECK(t.step(window, at) == Bubbles{bubble::kTransferButton});
    CHECK(t.page() == 6);
    TutorialView transfer = window;
    transfer.ship_page = TutorialView::ShipPage::Transfer;
    CHECK(t.step(transfer, at) == Bubbles{bubble::kIntoKontor});
    CHECK(t.page() == 7);
    // Another page of the ship window: back with the right mouse button.
    TutorialView trade = window;
    trade.ship_page = TutorialView::ShipPage::Trade;
    CHECK(t.step(trade, at) == Bubbles{bubble::kRightMouse});
    // Unloaded at the new city: done, for good.
    TutorialFacts unloaded = at;
    unloaded.ships[0].loaded = false;
    unloaded.ships[0].island_city = true;
    CHECK(t.step(transfer, unloaded) == Bubbles{bubble::kEnd});
    CHECK(t.done());
    CHECK(t.page() == 8);
    CHECK(t.step(info(), {}) == Bubbles{bubble::kEnd});
}

TEST_CASE("tutorial: chapter 1 follows the settlement") {
    Tutorial t(1);
    CHECK(t.page() == 9);
    TutorialFacts f;
    TutorialView build = mode(TutorialView::Mode::Build);
    // The forester's lodge: first the category "Farmen", then the building in its pop-up.
    CHECK(t.step(build, f) == Bubbles{bubble::kFirstCategory + 5});
    CHECK(t.page() == 9);
    build.popup_open = true;
    build.popup_category = 31006;
    CHECK(t.step(build, f) == Bubbles{bubble::kForester});
    build.popup_category = 31002;
    CHECK(t.step(build, f).empty());
    // Picked: the bubble goes; holding it, the right mouse button lets go; short of wood.
    build.popup_open = false;
    build.menu_building = 22001;
    build.holding_building = true;
    build.short_of_wood = true;
    CHECK((t.step(build, f) == Bubbles{bubble::kLetGo, bubble::kShortOfWood}));
    // The other modes ask for the build mode.
    CHECK(t.step(info(), f) == Bubbles{bubble::kBuildMode});
    CHECK(t.step(mode(TutorialView::Mode::Military), f) == Bubbles{bubble::kBuildMode});

    for (std::int32_t step = 10; step <= 12; ++step) {
        f.settlement = settled_up_to(step);
        (void)t.step(info(), f);
        CHECK(t.page() == step);
    }
    // A road: the forester or the fisher is not connected yet.
    f.settlement = settled_up_to(11);
    CHECK(t.step(mode(TutorialView::Mode::Build), f) == Bubbles{bubble::kFirstCategory + 1});

    // Four houses: look at one, in the info mode.
    f.settlement = settled_up_to(13);
    CHECK(t.step(mode(TutorialView::Mode::Build), f) == Bubbles{bubble::kInfoMode});
    CHECK(t.page() == 13);
    CHECK(t.step(mode(TutorialView::Mode::Military), f) == Bubbles{bubble::kInfoMode});
    // U-038: the diplomacy window asks for the info mode too (the original: the build mode).
    CHECK(t.step(mode(TutorialView::Mode::Diplomacy), f) == Bubbles{bubble::kInfoMode});
    CHECK(t.step(info(), f) == Bubbles{bubble::kClickHouse});
    CHECK((t.step(info(TutorialView::Window::OwnHouse), f) == Bubbles{bubble::kBuildMode, bubble::kNeedCloth}));
    CHECK(t.page() == 13);
    // Looked at: the sheep farm.
    CHECK((t.step(info(TutorialView::Window::OwnHouse), f) == Bubbles{bubble::kNeedCloth, bubble::kBuildMode}));
    CHECK(t.page() == 14);
    CHECK(t.step(mode(TutorialView::Mode::Diplomacy), f) == Bubbles{bubble::kBuildMode});

    // The weaver's shop in the crafts, the market place in the public buildings.
    f.settlement = settled_up_to(15);
    CHECK(t.step(mode(TutorialView::Mode::Build), f) == Bubbles{bubble::kFirstCategory + 4});
    f.settlement = settled_up_to(17);
    TutorialView market = mode(TutorialView::Mode::Build);
    market.popup_open = true;
    market.popup_category = 31008;
    CHECK(t.step(market, f) == Bubbles{bubble::kMarket});
    CHECK(t.page() == 17);
    // The market place stands: done once the build mode sees it.
    f.settlement = settled_up_to(18);
    (void)t.step(info(), f);
    CHECK(!t.done());
    CHECK(t.step(mode(TutorialView::Mode::Build), f).empty());
    CHECK(t.done());
    CHECK(t.step(info(), f) == Bubbles{bubble::kEnd});
    CHECK(t.page() == 18);
}

TEST_CASE("tutorial: chapter 2 trades cloth for bricks") {
    Tutorial t(2);
    TutorialFacts f;
    f.ships[0].exists = true;
    // The treaty: the diplomacy menu of the player status, then the offer.
    CHECK(t.step(info(), f) == Bubbles{bubble::kDiplomacy});
    CHECK(t.step(info(TutorialView::Window::Ship), f) == Bubbles{bubble::kRightMouse});
    CHECK(t.step(mode(TutorialView::Mode::Diplomacy), f) == Bubbles{bubble::kOfferTreaty});
    CHECK(t.page() == 19);
    f.treaty_offered = true;
    CHECK(t.step(info(), f).empty());
    f.treaty_signed = true;
    CHECK(t.step(mode(TutorialView::Mode::Diplomacy), f) == Bubbles{bubble::kRightMouse});
    CHECK(t.page() == 20);
    // The partner's Kontor, looked at once.
    CHECK(t.step(info(), f) == Bubbles{bubble::kPartnerKontor});
    (void)t.step(info(TutorialView::Window::PartnerKontor), f);
    CHECK(t.step(info(TutorialView::Window::PartnerKontor), f) == Bubbles{bubble::kClickShip});
    // Cloth from the own Kontor: the group of consumer goods, the cloth, onto the ship.
    TutorialView window = info(TutorialView::Window::Ship);
    CHECK(t.step(window, f) == Bubbles{bubble::kToKontor});
    f.ships[0].actions = action::kTrade | action::kOwnCity;
    f.town_cloth = 10 * 32;
    CHECK(t.step(window, f) == Bubbles{bubble::kTransferButton});
    window.ship_page = TutorialView::ShipPage::Transfer;
    CHECK(t.step(window, f) == Bubbles{bubble::kConsumerGoods});
    window.consumer_goods = true;
    CHECK((t.step(window, f) == Bubbles{bubble::kScrollGoods, bubble::kOntoShip}));
    window.kontor_ware = 19;
    CHECK(t.step(window, f) == Bubbles{bubble::kOntoShip});
    CHECK(t.page() == 22);
    f.ships[0].cloth = 10 * 32;
    f.town_cloth = 0;
    CHECK(t.step(window, f) == Bubbles{bubble::kSailToPartner});
    CHECK(t.page() == 23);
    // At the partner's Kontor: sell the cloth, buy bricks.
    f.ships[0].actions = action::kTrade;
    f.partner_buys_cloth = 20 * 32;
    TutorialView trade = info(TutorialView::Window::Ship);
    CHECK(t.step(trade, f) == Bubbles{bubble::kTradeButton});
    trade.ship_page = TutorialView::ShipPage::Trade;
    CHECK(t.step(trade, f) == Bubbles{bubble::kSell});
    CHECK(t.page() == 24);
    f.ships[0].cloth = 0;
    f.partner_sells_bricks = 20 * 32;
    CHECK(t.step(trade, f) == Bubbles{bubble::kBuy});
    CHECK(t.page() == 25);
    f.ships[0].bricks = 16 * 32;
    f.partner_sells_bricks = 4 * 32;
    CHECK(t.step(trade, f).empty());
    CHECK(t.page() == 26);
    // Home, and into the Kontor.
    CHECK(t.step(trade, f) == Bubbles{bubble::kSailHome});
    f.ships[0].actions = action::kTrade | action::kOwnCity;
    CHECK(t.step(info(), f) == Bubbles{bubble::kClickShip});
    TutorialView home = info(TutorialView::Window::Ship);
    home.ship_page = TutorialView::ShipPage::Transfer;
    CHECK(t.step(home, f) == Bubbles{bubble::kIntoKontor});
    f.ships[0].bricks = 0;
    (void)t.step(home, f);
    CHECK(t.done());
    CHECK(t.page() == 27);
}

TEST_CASE("tutorial: chapter 3 fights at sea and sets a patrol") {
    Tutorial t(3);
    sim::World world(64, 64);
    std::vector<sim::Player> players(2);
    world.set_players(players);
    for (std::uint32_t id = 1; id <= 3; ++id) {
        sim::Ship s;
        s.id = id;
        s.owner = 0;
        world.add_ship(s);
    }
    t.start(world, 0);
    REQUIRE(t.ships().size() == 3);

    TutorialFacts f;
    f.partner_ships = 1;
    f.ships[0].exists = f.ships[1].exists = f.ships[2].exists = true;
    // In another mode: the military mode.
    CHECK(t.step(info(), f) == Bubbles{bubble::kMilitaryMode});
    TutorialView fight = mode(TutorialView::Mode::Military);
    CHECK(t.step(fight, f) == Bubbles{bubble::kClickLargeShip});
    CHECK(t.page() == 28);
    fight.units = {ui::UnitRef{ui::UnitRef::Kind::Ship, 1}};
    CHECK(t.step(fight, f) == Bubbles{bubble::kCannons});
    CHECK(t.page() == 29);
    f.ships[0].cannons = 4;
    CHECK(t.step(fight, f) == Bubbles{bubble::kAttackWithLarge});
    CHECK(t.page() == 30);
    // Fighting: the small ships join once it is low.
    f.ships[0].attacking = true;
    CHECK(t.step(fight, f).empty());
    f.ships[0].health = 50;
    CHECK(t.step(fight, f) == Bubbles{bubble::kAttackWithSmall});
    CHECK(t.page() == 31);
    f.ships[1].attacking_partner = f.ships[2].attacking_partner = true;
    CHECK(t.step(fight, f).empty());
    // The enemy sunk: a small ship on patrol.
    f.partner_ships = 0;
    CHECK(t.step(fight, f) == Bubbles{bubble::kSelectSmall});
    fight.units = {ui::UnitRef{ui::UnitRef::Kind::Ship, 2}};
    CHECK(t.step(fight, f) == Bubbles{bubble::kPatrolButton});
    CHECK(t.page() == 33);
    fight.picking_patrol = true;
    CHECK(t.step(fight, f) == Bubbles{bubble::kPatrolPoint});
    CHECK(t.page() == 34);
    f.ships[1].patrolling = true;
    CHECK(t.step(fight, f) == Bubbles{bubble::kEnd});
    CHECK(t.done());
    CHECK(t.page() == 35);
}

TEST_CASE("tutorial: chapter 4 lands the soldiers and attacks with the cavalry first") {
    Tutorial t(4);
    sim::World world(64, 64);
    std::vector<sim::Player> players(2);
    world.set_players(players);
    sim::Ship s;
    s.id = 7;
    s.owner = 0;
    world.add_ship(s);
    t.start(world, 0);

    TutorialFacts f;
    f.ships[0].exists = true;
    f.ships[0].loaded = true;
    TutorialView fight = mode(TutorialView::Mode::Military);
    CHECK(t.step(fight, f) == Bubbles{bubble::kClickShip});
    CHECK(t.page() == 36);
    fight.units = {ui::UnitRef{ui::UnitRef::Kind::Ship, 7}};
    CHECK(t.step(fight, f) == Bubbles{bubble::kSailToCoast});
    f.ships[0].actions = action::kFound;
    CHECK(t.step(fight, f) == Bubbles{bubble::kUnloadSoldiers});
    CHECK(t.page() == 37);
    // Landed: frame them, keep the cavalry, attack.
    f.ships[0].loaded = false;
    f.partner_units = 3;
    f.riders.total = 2;
    f.musketeers.total = 3;
    fight.units.clear();
    CHECK(t.step(fight, f) == Bubbles{bubble::kFrameSoldiers});
    CHECK(t.page() == 38);
    fight.units.assign(5, ui::UnitRef{ui::UnitRef::Kind::Soldier, 1});
    CHECK(t.step(fight, f) == Bubbles{bubble::kOnlyCavalry});
    CHECK(t.page() == 39);
    fight.units.resize(2);
    f.selected_riders = 2;
    CHECK(t.step(fight, f) == Bubbles{bubble::kAttackWithCavalry});
    CHECK(t.page() == 40);
    // The cavalry attacks: now the rest, until it does too.
    f.riders.attacking = 2;
    CHECK(t.step(fight, f) == Bubbles{bubble::kAttackWithRest});
    CHECK(t.page() == 41);
    f.musketeers.attacking = 3;
    CHECK(t.step(fight, f).empty());
    // The cavalry fell back meanwhile: the rest still counts as sent.
    f.riders.attacking = 0;
    CHECK(t.step(fight, f).empty());
    f.partner_units = 0;
    CHECK(t.step(fight, f) == Bubbles{bubble::kEnd});
    CHECK(t.done());
    CHECK(t.page() == 42);
}

namespace {

constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kKontor = 22101;

/// Road and ground; the Kontor, the forester's lodge, the fisher's hut, a house and a market place.
assets::BuildingTable settlement_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20401\n  Kind: 1\n  Gfx: 0\n  Size: 1, 1\n  Wegspeed: 100, 100, 100, 100\n"
        "  @Nummer: +1\n  Id: 20101\n  Kind: 11\n  Gfx: 1\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: +1\n  Id: 22101\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n  Tuerflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Maxlager: 30\n    Radius: 12\n  EndObj\n"
        "  @Nummer: +1\n  Id: 22001\n  Kind: 14\n  Gfx: 20\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n  EndObj\n"
        "  @Nummer: +1\n  Id: 21075\n  Kind: 14\n  Gfx: 30\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 6\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20605\n  Kind: 14\n  Gfx: 40\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20809\n  Kind: 14\n  Gfx: 50\n  Size: 3, 3\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("tutorial: chapter 1 counts the buildings and follows the roads from the Kontor") {
    const assets::BuildingTable buildings = settlement_buildings();
    const assets::FigureTable figures;
    // A 30 x 12 island of player 0 with a road along row 5; the Kontor next to it.
    sim::IslandMap map(30, 12);
    for (std::int32_t y = 0; y < 12; ++y) {
        for (std::int32_t x = 0; x < 30; ++x) {
            REQUIRE(map.place(*buildings.by_id(y == 5 ? kRoad : kGround), x, y, 0, 0, 0, 0).ok());
        }
    }
    auto place = [&](std::uint32_t id, std::int32_t x, std::int32_t y) {
        REQUIRE(map.place(*buildings.by_id(id), x, y, 0, 0, 0, 0).ok());
    };
    place(kKontor, 2, 3);
    place(22001, 10, 6);  // the forester at the road
    place(21075, 20, 1);  // the fisher away from it
    place(20605, 14, 7);
    place(20605, 17, 7);
    sim::World world(64, 64);
    std::vector<sim::Player> players(2);
    world.set_players(players);
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
    sim::City city;
    city.island = 0;
    city.index = 0;
    city.owner = 0;
    city.warehouse_x = 2;
    city.warehouse_y = 3;
    world.add_city(city);
    sim::Simulation simulation(world, buildings, figures);

    Tutorial t(1);
    t.start(simulation.world(), 0);
    const TutorialFacts f = t.facts(info(), simulation, buildings, figures, 0);
    CHECK(f.settlement.foresters == 1);
    CHECK(f.settlement.fishers == 1);
    CHECK(f.settlement.houses == 2);
    CHECK(f.settlement.markets == 0);
    CHECK(f.settlement.foresters_connected);
    CHECK(!f.settlement.fishers_connected);
    (void)t.step(info(), f);
    CHECK(t.page() == 11);
}
