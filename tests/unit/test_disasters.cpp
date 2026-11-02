// SPDX-License-Identifier: GPL-3.0-or-later
// Fires and plagues, doctors and firemen.

#include <algorithm>

#include "../test_framework.hpp"
#include "golden.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/disasters.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/production_cycle.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kForest = 20201;
constexpr std::uint32_t kPioneerHouse = 20605;
constexpr std::uint32_t kBrickPioneerHouse = 20606;
constexpr std::uint32_t kCitizenHouse = 20640;
constexpr std::uint32_t kBakery = 20503;
constexpr std::uint32_t kKlinik = 20901;
constexpr std::uint32_t kBrunnen = 20902;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}, {"BGRUPPE_WARE", 6}};
    const auto doc = assets::cod::parse(
        "Nahrung: 1.3\n"
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n"
        "  Nummer: 2\n  Prozent: 60\n  Maxwohn: 15\n  Steuer: 1.8\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n  Wegspeed: 100, 100, 100, 100\n"
        "  @Nummer: 2\n  Id: 20201\n  Kind: 10\n  Gfx: 2\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: 3\n  Id: 20605\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n  Ruinenr: 0\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 3\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20640\n  Kind: 14\n  Gfx: 14\n  Size: 2, 2\n  Ruinenr: 0\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 2\n    Radius: 6\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Ziegel: 6\n  EndObj\n"
        "  @Nummer: 5\n  Id: 20503\n  Kind: 14\n  Gfx: 20\n  Size: 2, 2\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 10\n    Rohstoff: 5\n    Rohmenge: 1\n"
        "    Prodmenge: 1\n    Interval: 20\n    Figurnr: 80\n    Radius: 8\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 3\n  EndObj\n"
        "  @Nummer: 6\n  Id: 20901\n  Kind: 14\n  Gfx: 30\n  Size: 2, 2\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 22\n    Radius: 16\n    Figurnr: 84\n  EndObj\n"
        "  @Nummer: 7\n  Id: 20902\n  Kind: 14\n  Gfx: 34\n  Size: 2, 2\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 26\n    Radius: 15\n    Figurnr: 85\n  EndObj\n"
        "  @Nummer: 8\n  Id: 22601\n  Kind: 12\n  Gfx: 50\n  Size: 1, 1\n"
        "  @Nummer: 9\n  Id: 20606\n  Kind: 14\n  Gfx: 54\n  Size: 2, 2\n  Ruinenr: 0\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Ziegel: 6\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// ARZT and LOESCH with the values of figuren.cod.
assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 84\n  Blocknr: 3\n  Speed: 220\n  Rotate: 8\n  Gfx: 0\n  Maxtrag: 6\n  Worktime: 3\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 1\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 85\n"
        "  EndObj\n"
        "  Nummer: 85\n  Blocknr: 3\n  Speed: 300\n  Rotate: 8\n  Gfx: 64\n  Maxtrag: 4\n  Worktime: 3\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 1\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 85\n"
        "  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A 40 x 30 island of open ground with a road along row 10; city 0 of a human player.
struct Town {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world;
    sim::IslandMap map{40, 30};
    std::vector<sim::DisasterEvent> events;

    Town() {
        for (std::int32_t y = 0; y < 30; ++y) {
            for (std::int32_t x = 0; x < 40; ++x) {
                REQUIRE(map.place(*buildings.by_id(y == 10 ? kRoad : kGround), x, y, 0, 0, 0, 0).ok());
            }
        }
    }
    void place(std::uint32_t id, std::int32_t x, std::int32_t y) {
        REQUIRE(map.place(*buildings.by_id(id), x, y, 0, 0, 0, 0).ok());
    }
    void finish(const std::vector<std::pair<std::int32_t, std::int32_t>>& productions) {
        map.capture_ground();
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 0, 0}).ok());
        sim::City city;
        city.owner = 0;
        city.capacity = 30 * 32;
        city.inhabitants = {0, 0, 0};
        city.satisfaction = {128, 128, 128};
        city.tax_rate = {128, 128, 128};
        world.add_city(city);
        std::vector<sim::Player> players(1);
        players[0].role = sim::PlayerRole::Human;
        world.set_players(players);
        for (const auto& [x, y] : productions) {
            sim::Production p;
            p.x = static_cast<std::uint16_t>(x);
            p.y = static_cast<std::uint16_t>(y);
            world.set_production(p);
        }
    }
    sim::House& add_house(std::uint16_t x, std::uint16_t y, std::uint8_t cls, std::uint32_t inhabitants) {
        sim::House h;
        h.x = x;
        h.y = y;
        h.city = 0;
        h.resident_class = cls;
        h.inhabitants64 = inhabitants * 64;
        world.add_house(h);
        world.city(0, 0)->inhabitants[cls] += inhabitants;
        return *world.house(0, x, y);
    }
    sim::City& city() { return *world.city(0, 0); }
    const sim::Disaster* at(std::uint16_t x, std::uint16_t y) const { return sim::disaster_at(world, 0, x, y); }
    std::uint32_t id(std::int32_t x, std::int32_t y) const {
        return buildings
            .by_sprite(world.islands()[0].map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).sprite)
            ->id;
    }
    sim::Production& production(std::int32_t x, std::int32_t y) {
        return world
            .productions()[*world.production_index(0, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y))];
    }
    /// One disaster epoch.
    void epoch(std::vector<sim::ConstructionEvent>* construction = nullptr) {
        sim::advance_disasters(world, buildings, sim::Rules::open1602(), &events, construction);
    }
};

std::size_t count(const std::vector<sim::DisasterEvent>& events, sim::DisasterEvent::Kind kind, bool spread) {
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const sim::DisasterEvent& e) {
        return e.kind == kind && (kind != sim::DisasterEvent::Kind::Started || e.spread == spread);
    }));
}

}  // namespace

TEST_CASE("disasters: a plague marks the house, a fire stops the workshop, the city stops growing") {
    Town town;
    town.place(kCitizenHouse, 4, 8);
    town.place(kBakery, 10, 8);
    town.finish({{10, 8}});
    sim::House& house = town.add_house(4, 8, 2, 15);
    town.production(10, 8).rate = 128;
    town.production(10, 8).countdown = 5;

    REQUIRE(sim::start_disaster(town.world, town.buildings, 0, {5, 9}, sim::Disaster::Kind::Plague,
                                sim::kPlagueDuration, &town.events));
    REQUIRE(town.at(4, 8) != nullptr);  // on the origin, whatever tile was hit
    CHECK((house.services & sim::house_service::kDisasterMask) == 1);
    CHECK(town.city().disasters == 1);
    REQUIRE(town.events.size() == 1);
    CHECK(town.events[0].owner == 0);

    REQUIRE(sim::start_disaster(town.world, town.buildings, 0, {10, 8}, sim::Disaster::Kind::Fire, sim::kFireDuration,
                                &town.events));
    const sim::Production& bakery = town.production(10, 8);
    CHECK(bakery.burning());
    CHECK(bakery.rate == 0);  // the running cycle yields nothing
    CHECK(sim::production_status(bakery, *town.buildings.by_id(kBakery)) == sim::ProductionStatus::Burning);
    CHECK(town.city().disasters == 2);

    // A content house does not grow while the city has a disaster.
    sim::House healthy = house;
    healthy.services = 0;
    healthy.market = true;
    healthy.market_distance = 0;
    healthy.services_complete = true;
    town.city().food_ratio = 128;
    CHECK(sim::growth_rate(town.city(), healthy) == 0);
    // The plagued house itself declines by 256 more.
    CHECK(sim::growth_rate(town.city(), house) < -200);

    CHECK(sim::end_disaster(town.world, 0, {4, 8}, sim::Disaster::Kind::Plague, &town.events));
    CHECK((house.services & sim::house_service::kDisasterMask) == 0);
    CHECK(!sim::end_disaster(town.world, 0, {10, 8}, sim::Disaster::Kind::Plague, nullptr));  // wrong kind
    CHECK(sim::end_disaster(town.world, 0, {10, 8}, sim::Disaster::Kind::Fire, nullptr));
    CHECK(!town.production(10, 8).burning());
    CHECK(town.city().disasters == 0);
    CHECK(sim::growth_rate(town.city(), healthy) > 0);
}

TEST_CASE("disasters: a fire burns the house down after 25 epochs") {
    Town town;
    town.place(kPioneerHouse, 4, 8);
    town.finish({});
    town.add_house(4, 8, 0, 2);
    REQUIRE(sim::start_disaster(town.world, town.buildings, 0, {4, 8}, sim::Disaster::Kind::Fire, sim::kFireDuration,
                                nullptr));
    std::vector<sim::ConstructionEvent> construction;
    for (int i = 0; i < 24; ++i) {
        town.epoch(&construction);
    }
    CHECK(town.at(4, 8) != nullptr);
    CHECK(town.world.house(0, 4, 8) != nullptr);
    town.epoch(&construction);
    CHECK(town.at(4, 8) == nullptr);
    CHECK(town.world.house(0, 4, 8) == nullptr);
    CHECK(town.id(4, 8) == 22601);  // ruins
    CHECK(town.city().inhabitants[0] == 0);
    CHECK(town.city().disasters == 0);
    CHECK(count(town.events, sim::DisasterEvent::Kind::BurnedDown, false) == 1);
}

TEST_CASE("disasters: a plague ends by itself, the house stays") {
    Town town;
    town.place(kCitizenHouse, 4, 8);
    town.finish({});
    town.add_house(4, 8, 2, 4);  // not half full: it cannot infect anybody
    REQUIRE(sim::start_disaster(town.world, town.buildings, 0, {4, 8}, sim::Disaster::Kind::Plague,
                                sim::kPlagueDuration, nullptr));
    for (int i = 0; i < 20; ++i) {
        town.epoch();
    }
    CHECK(town.at(4, 8) == nullptr);
    CHECK(town.world.house(0, 4, 8) != nullptr);
    CHECK(count(town.events, sim::DisasterEvent::Kind::Ended, false) == 1);
}

TEST_CASE("disasters: plague spreads to half full houses of the city, fire over forest") {
    Town town;
    town.place(kCitizenHouse, 4, 8);
    town.place(kCitizenHouse, 7, 8);
    town.place(kPioneerHouse, 20, 4);
    // A strip of forest between two wooden houses: the fire leaps within 2 tiles of the centre.
    town.place(kForest, 22, 4);
    town.place(kForest, 22, 5);
    town.place(kPioneerHouse, 23, 4);
    town.finish({});
    town.add_house(4, 8, 2, 15);
    town.add_house(7, 8, 2, 15);
    town.add_house(20, 4, 0, 2);
    town.add_house(23, 4, 0, 2);

    bool plague = false;
    for (int round = 0; round < 10 && !plague; ++round) {
        sim::start_disaster(town.world, town.buildings, 0, {4, 8}, sim::Disaster::Kind::Plague, sim::kPlagueDuration,
                            nullptr);
        for (int i = 0; i < 20 && !plague; ++i) {
            town.epoch();
            plague = town.at(7, 8) != nullptr;
        }
    }
    CHECK(plague);
    CHECK(town.at(7, 8)->kind == sim::Disaster::Kind::Plague);
    CHECK(town.at(7, 8)->duration == sim::kSpreadDuration);

    bool fire = false;
    for (int round = 0; round < 20 && !fire; ++round) {
        sim::start_disaster(town.world, town.buildings, 0, {20, 4}, sim::Disaster::Kind::Fire, sim::kFireDuration,
                            nullptr);
        for (int i = 0; i < 20 && !fire; ++i) {
            town.epoch();
            const sim::Disaster* d = town.at(23, 4);
            fire = d != nullptr && d->kind == sim::Disaster::Kind::Fire;
        }
    }
    CHECK(fire);
    CHECK(count(town.events, sim::DisasterEvent::Kind::Started, true) >= 2);
}

TEST_CASE("disasters: a package's balance stops plague and fire from spreading (MD-9)") {
    Town town;
    town.place(kCitizenHouse, 4, 8);
    town.place(kCitizenHouse, 7, 8);
    town.place(kPioneerHouse, 20, 4);
    town.place(kForest, 22, 4);
    town.place(kForest, 22, 5);
    town.place(kPioneerHouse, 23, 4);
    town.finish({});
    town.add_house(4, 8, 2, 15);
    town.add_house(7, 8, 2, 15);
    town.add_house(20, 4, 0, 2);
    town.add_house(23, 4, 0, 2);
    sim::Balance balance;
    balance.plague_radius = 0;
    balance.fire_spread = 0;
    town.world.set_balance(balance);

    for (int round = 0; round < 10; ++round) {
        sim::start_disaster(town.world, town.buildings, 0, {4, 8}, sim::Disaster::Kind::Plague, sim::kPlagueDuration,
                            nullptr);
        sim::start_disaster(town.world, town.buildings, 0, {20, 4}, sim::Disaster::Kind::Fire, sim::kFireDuration,
                            nullptr);
        for (int i = 0; i < 20; ++i) {
            town.epoch();
            CHECK(town.at(7, 8) == nullptr);
            CHECK(town.at(23, 4) == nullptr);
        }
    }
}

TEST_CASE("disasters: doctors cure on foot, firemen put out fires along the roads") {
    Town town;
    town.place(kKlinik, 2, 8);
    town.place(kBrunnen, 14, 8);
    town.place(kCitizenHouse, 6, 8);
    town.place(kBakery, 20, 8);
    town.finish({{2, 8}, {14, 8}, {20, 8}});
    town.add_house(6, 8, 2, 15);
    sim::Logistics logistics(town.world, town.buildings, town.figures);
    const auto klinik = *town.world.production_index(0, 2, 8);
    const auto brunnen = *town.world.production_index(0, 14, 8);
    const auto ask = [&](std::uint32_t p) {
        logistics.handle(sim::ProductionEvent{sim::ProductionEvent::Kind::NeedsRawGoods, p, 0, 0});
    };

    // Nothing to do: nobody goes.
    ask(klinik);
    ask(brunnen);
    CHECK(logistics.walkers().size() == 0);

    sim::start_disaster(town.world, town.buildings, 0, {6, 8}, sim::Disaster::Kind::Plague, sim::kPlagueDuration,
                        nullptr);
    sim::start_disaster(town.world, town.buildings, 0, {20, 8}, sim::Disaster::Kind::Fire, sim::kFireDuration, nullptr);
    ask(klinik);
    ask(klinik);  // one figure per building
    ask(brunnen);
    REQUIRE(logistics.walkers().size() == 2);
    // The bakery covers x 20..21, y 8..9; the road runs along y 10.
    const auto on_bakery = [](sim::TilePos t) { return t.x >= 20 && t.x <= 21 && t.y >= 8 && t.y <= 9; };
    bool put_out_from_road = false;
    bool fireman_inside = false;
    for (int t = 0; t < 20 * 60 && logistics.walkers().size() != 0; ++t) {
        logistics.tick();
        logistics.walkers().for_each([&](sim::Handle, const sim::Walker& w) {
            if (w.kind != sim::WalkerKind::Firefighter) {
                return;
            }
            // The fireman stays on the road in front of the fire, turned to it.
            fireman_inside = fireman_inside || on_bakery(w.tile);
            if (w.working()) {
                put_out_from_road = w.tile.y == 10 && w.direction == sim::direction_between(w.tile, {20, 8});
            }
        });
    }
    CHECK(put_out_from_road);
    CHECK(!fireman_inside);
    CHECK(logistics.walkers().size() == 0);  // both back home
    CHECK(town.at(6, 8) == nullptr);
    CHECK(town.at(20, 8) == nullptr);
    CHECK(!town.production(20, 8).burning());
    CHECK(logistics.stats().cured == 1);
    CHECK(logistics.stats().extinguished == 1);
    const auto events = logistics.take_disaster_events();
    CHECK(count(events, sim::DisasterEvent::Kind::Ended, false) == 2);
}

TEST_CASE("disasters: the fire brigade puts out a fire right next to its own building") {
    Town town;
    town.place(kBrunnen, 14, 8);
    town.place(kBakery, 12, 8);  // x 12..13: touches the fire brigade's tile at (14, 8)
    town.finish({{14, 8}, {12, 8}});
    sim::Logistics logistics(town.world, town.buildings, town.figures);
    sim::start_disaster(town.world, town.buildings, 0, {12, 8}, sim::Disaster::Kind::Fire, sim::kFireDuration, nullptr);
    logistics.handle(
        sim::ProductionEvent{sim::ProductionEvent::Kind::NeedsRawGoods, *town.world.production_index(0, 14, 8), 0, 0});
    REQUIRE(logistics.walkers().size() == 1);
    bool worked_at_home = false;
    for (int t = 0; t < 20 * 60 && logistics.walkers().size() != 0; ++t) {
        logistics.tick();
        logistics.walkers().for_each([&](sim::Handle, const sim::Walker& w) {
            if (w.working()) {
                worked_at_home =
                    w.tile == sim::TilePos{14, 8} && w.direction == sim::direction_between(w.tile, {12, 8});
            }
        });
    }
    CHECK(worked_at_home);
    CHECK(logistics.walkers().size() == 0);
    CHECK(town.at(12, 8) == nullptr);
    CHECK(logistics.stats().extinguished == 1);
}

TEST_CASE("disasters: the fire brigade does not reach a building away from the roads") {
    Town town;
    town.place(kBrunnen, 14, 8);
    town.place(kBakery, 14, 20);  // open ground only
    town.finish({{14, 8}, {14, 20}});
    sim::Logistics logistics(town.world, town.buildings, town.figures);
    sim::start_disaster(town.world, town.buildings, 0, {14, 20}, sim::Disaster::Kind::Fire, sim::kFireDuration,
                        nullptr);
    logistics.handle(
        sim::ProductionEvent{sim::ProductionEvent::Kind::NeedsRawGoods, *town.world.production_index(0, 14, 8), 0, 0});
    CHECK(logistics.walkers().size() == 0);
}

TEST_CASE("disasters: cities of many pioneers catch fire, the doctor and the brigade look every cycle") {
    Town town;
    for (std::uint16_t i = 0; i < 10; ++i) {
        town.place(kPioneerHouse, 2 + 3 * i, 4);
    }
    town.place(kKlinik, 2, 14);
    town.finish({{2, 14}});
    for (std::uint16_t i = 0; i < 10; ++i) {
        town.add_house(static_cast<std::uint16_t>(2 + 3 * i), 4, 0, 10);  // 100 pioneers
    }
    sim::advance_city_events(town.world, town.buildings, sim::Rules::open1602(), &town.events,
                             nullptr);  // first check in a minute
    CHECK(town.city().next_event_ms == 60'000);
    bool fire = false;
    for (int i = 0; i < 200 && !fire; ++i) {
        town.world.set_time_ms(town.city().next_event_ms);
        sim::advance_city_events(town.world, town.buildings, sim::Rules::open1602(), &town.events, nullptr);
        CHECK(town.city().next_event_ms >= town.world.time_ms() + 60'000);
        CHECK(town.city().next_event_ms <= town.world.time_ms() + 67'000);
        fire = count(town.events, sim::DisasterEvent::Kind::Started, false) != 0;
    }
    CHECK(fire);
    CHECK(town.events.back().disaster == sim::Disaster::Kind::Fire);

    // The Klinik's cycle asks for work every 11 s.
    std::vector<sim::ProductionEvent> production;
    for (int s = 0; s < 12; ++s) {
        sim::advance_production(town.world, town.buildings, &production);
    }
    CHECK(std::count_if(production.begin(), production.end(), [](const sim::ProductionEvent& e) {
              return e.kind == sim::ProductionEvent::Kind::NeedsRawGoods;
          }) >= 1);
}

TEST_CASE("disasters: wooden houses catch fire more easily than brick ones (S-049)") {
    // The same city twice, once of wooden and once of brick houses, with the same random numbers.
    const auto fires = [](std::uint32_t house, const sim::Rules& rules) {
        Town town;
        for (std::uint16_t i = 0; i < 10; ++i) {
            town.place(house, 2 + 3 * i, 4);
        }
        town.finish({});
        for (std::uint16_t i = 0; i < 10; ++i) {
            town.add_house(static_cast<std::uint16_t>(2 + 3 * i), 4, 0, 10);
        }
        std::size_t n = 0;
        sim::advance_city_events(town.world, town.buildings, rules, nullptr, nullptr);
        for (int i = 0; i < 2000; ++i) {
            town.world.set_time_ms(town.city().next_event_ms);
            std::vector<sim::DisasterEvent> events;
            sim::advance_city_events(town.world, town.buildings, rules, &events, nullptr);
            n += count(events, sim::DisasterEvent::Kind::Started, false);
            town.world.disasters().clear();  // put out at once
            sim::recount_disasters(town.world);
            for (sim::House& h : town.world.houses()) {
                h.services = 0;
            }
        }
        return n;
    };
    const sim::Rules open1602 = sim::Rules::open1602();
    const std::size_t wood = fires(kPioneerHouse, open1602);
    const std::size_t brick = fires(kBrickPioneerHouse, open1602);
    // Per check 15.5 % pass the roll; then three tries of 7/8 (wood, 99.8 %) or 3/8 (brick,
    // 75.6 %): about 310 and 234 of 2000 checks.
    CHECK(wood > brick + brick / 8);
    CHECK(wood > 260);
    CHECK(wood < 360);
    CHECK(brick > 190);
    CHECK(brick < 280);

    // The bug brought back: the other way round, as in the original.
    sim::Rules original = open1602;
    original.bugs.fire_risk = true;
    const std::size_t original_wood = fires(kPioneerHouse, original);
    const std::size_t original_brick = fires(kBrickPioneerHouse, original);
    CHECK(original_brick > original_wood + original_wood / 8);
    CHECK(original_brick > 260);
    CHECK(original_brick < 360);
    CHECK(original_wood > 190);
    CHECK(original_wood < 280);
}

TEST_CASE("disasters: plagues and fires break out as in the original, half as often, or never") {
    // Outbreaks in 2000 checks of the same city, with the same random numbers.
    const auto outbreaks = [](std::uint32_t house, std::uint8_t cls, std::uint32_t inhabitants,
                              const sim::Rules& rules) {
        Town town;
        for (std::uint16_t i = 0; i < 10; ++i) {
            town.place(house, 2 + 3 * i, 4);
        }
        town.finish({});
        for (std::uint16_t i = 0; i < 10; ++i) {
            town.add_house(static_cast<std::uint16_t>(2 + 3 * i), 4, cls, inhabitants);
        }
        std::size_t n = 0;
        sim::advance_city_events(town.world, town.buildings, rules, nullptr, nullptr);
        for (int i = 0; i < 2000; ++i) {
            town.world.set_time_ms(town.city().next_event_ms);
            std::vector<sim::DisasterEvent> events;
            sim::advance_city_events(town.world, town.buildings, rules, &events, nullptr);
            n += count(events, sim::DisasterEvent::Kind::Started, false);
            town.world.disasters().clear();
            sim::recount_disasters(town.world);
            for (sim::House& h : town.world.houses()) {
                h.services = 0;
            }
        }
        return n;
    };
    const auto with = [](sim::Outbreaks plagues, sim::Outbreaks fires) {
        sim::Rules r = sim::Rules::open1602();
        r.plagues = plagues;
        r.fires = fires;
        return r;
    };
    using O = sim::Outbreaks;
    CHECK(sim::Rules::open1602().plagues == O::Original);
    CHECK(sim::Rules::open1602().fires == O::Original);

    // 250 citizens: 13 of 250 checks pass the roll, then up to four tries of 8 of 16: about 98.
    const std::size_t plague = outbreaks(kCitizenHouse, 2, 25, with(O::Original, O::Original));
    const std::size_t rare_plague = outbreaks(kCitizenHouse, 2, 25, with(O::Rare, O::Original));
    CHECK(plague > 70);
    CHECK(plague < 130);
    CHECK(rare_plague * 10 > plague * 3);
    CHECK(rare_plague * 10 < plague * 7);
    CHECK(outbreaks(kCitizenHouse, 2, 25, with(O::Off, O::Original)) == 0);
    // The fires' setting leaves the plague alone.
    CHECK(outbreaks(kCitizenHouse, 2, 25, with(O::Original, O::Off)) == plague);

    // 100 pioneers in wooden houses: about 310 fires, rare about half of them.
    const std::size_t fire = outbreaks(kPioneerHouse, 0, 10, with(O::Original, O::Original));
    const std::size_t rare_fire = outbreaks(kPioneerHouse, 0, 10, with(O::Original, O::Rare));
    CHECK(fire > 260);
    CHECK(fire < 360);
    CHECK(rare_fire * 10 > fire * 3);
    CHECK(rare_fire * 10 < fire * 7);
    CHECK(outbreaks(kPioneerHouse, 0, 10, with(O::Original, O::Off)) == 0);
}

TEST_CASE("disasters: the outbreaks' settings stay with the savegame") {
    Town town;
    town.place(kCitizenHouse, 2, 4);
    town.finish({});
    for (const auto o : {sim::Outbreaks::Original, sim::Outbreaks::Rare, sim::Outbreaks::Off}) {
        for (const auto v : {sim::Volcanoes::Game, sim::Volcanoes::Rare, sim::Volcanoes::On}) {
            sim::Rules rules;
            rules.plagues = o;
            rules.fires = o == sim::Outbreaks::Rare ? sim::Outbreaks::Off : sim::Outbreaks::Rare;
            rules.volcanoes = v;
            town.world.set_saved_rules(rules);
            const auto bytes = sim::write_game(town.world, town.buildings);
            REQUIRE(bytes.ok());
            auto back = sim::read_game(*bytes, town.buildings);
            REQUIRE(back.ok());
            REQUIRE(back->saved_rules().has_value());
            CHECK(*back->saved_rules() == rules);
        }
    }
}

TEST_CASE("disasters: a burning house does not catch fire again (S-050)") {
    Town town;
    for (std::uint16_t i = 0; i < 4; ++i) {
        town.place(kPioneerHouse, 20 + 2 * i, 4);  // a row of wooden houses wall to wall
    }
    town.finish({});
    for (std::uint16_t i = 0; i < 4; ++i) {
        town.add_house(static_cast<std::uint16_t>(20 + 2 * i), 4, 0, 2);
        REQUIRE(sim::start_disaster(town.world, town.buildings, 0, {20 + 2 * i, 4}, sim::Disaster::Kind::Fire,
                                    sim::kFireDuration, nullptr));
    }
    for (int i = 0; i < 24; ++i) {
        town.epoch();
    }
    for (std::uint16_t i = 0; i < 4; ++i) {
        const sim::Disaster* d = town.at(static_cast<std::uint16_t>(20 + 2 * i), 4);
        REQUIRE(d != nullptr);
        CHECK(d->elapsed == 24);  // never started anew by its burning neighbours
    }
    town.epoch();
    CHECK(town.world.disasters().empty());
    CHECK(town.world.houses().empty());
}

TEST_CASE("disasters: a gallows keeps three of four robbers away (S-055)") {
    const auto robbers = [](bool gallows, const sim::Rules& rules) {
        Town town;
        for (std::uint16_t i = 0; i < 10; ++i) {
            town.place(kCitizenHouse, 2 + 3 * i, 4);
            town.place(kCitizenHouse, 2 + 3 * i, 14);
        }
        town.finish({});
        for (std::uint16_t i = 0; i < 10; ++i) {
            town.add_house(static_cast<std::uint16_t>(2 + 3 * i), 4, 2, 15);
            town.add_house(static_cast<std::uint16_t>(2 + 3 * i), 14, 2, 15);  // 300 citizens
        }
        if (gallows) {
            for (sim::House& h : town.world.houses()) {
                h.services = sim::house_service::kGalgen;
            }
        }
        std::size_t n = 0;
        sim::advance_city_events(town.world, town.buildings, rules, nullptr, nullptr);
        for (int i = 0; i < 2000; ++i) {
            town.world.set_time_ms(town.city().next_event_ms);
            std::vector<sim::RobberSpawn> spawns;
            sim::advance_city_events(town.world, town.buildings, rules, nullptr, &spawns);
            n += spawns.size();
            for (const sim::Disaster& d : std::vector<sim::Disaster>(town.world.disasters())) {
                sim::end_disaster(town.world, d.island, {d.x, d.y}, d.kind, nullptr);
            }
        }
        return n;
    };
    const sim::Rules open1602 = sim::Rules::open1602();
    const std::size_t without = robbers(false, open1602);
    const std::size_t with = robbers(true, open1602);
    // 37.75 % of the checks (300..499 citizens): about 755 of 2000; with gallows a quarter of it.
    CHECK(without > 650);
    CHECK(without < 860);
    CHECK(with > without / 6);
    CHECK(with < without / 3);

    // The bug brought back: three tries, so 1 - (3/4)^3 = 58 % of the robbers come all the same.
    sim::Rules original = open1602;
    original.bugs.gallows = true;
    CHECK(robbers(false, original) == without);  // without gallows nothing changes
    const std::size_t original_with = robbers(true, original);
    CHECK(original_with > without / 2);
    CHECK(original_with < without * 2 / 3);
}

TEST_CASE("disasters: the original's bugs a game brings back stay with its savegame") {
    Town town;
    town.place(kCitizenHouse, 2, 4);
    town.finish({});
    CHECK(sim::Rules::open1602().bugs == sim::OriginalBugs{});  // Open1602's fixes by default
    CHECK(sim::Rules::original().bugs == sim::OriginalBugs::all());
    for (const sim::OriginalBugs bugs : {sim::OriginalBugs{true, false}, sim::OriginalBugs{false, true},
                                         sim::OriginalBugs::all(), sim::OriginalBugs{}}) {
        sim::Rules rules;
        rules.bugs = bugs;
        town.world.set_saved_rules(rules);
        const auto bytes = sim::write_game(town.world, town.buildings);
        REQUIRE(bytes.ok());
        auto back = sim::read_game(*bytes, town.buildings);
        REQUIRE(back.ok());
        REQUIRE(back->saved_rules().has_value());
        CHECK(back->saved_rules()->bugs == bugs);
        // The simulation of the loaded game plays by them, whatever it is started with.
        const sim::Simulation sim(*back, town.buildings, town.figures, sim::Rules::open1602());
        CHECK(sim.rules().bugs == bugs);
    }
}

TEST_CASE("disasters: fire and plague in a city develop the same on every platform (golden hash)") {
    Town town;
    for (std::int32_t x = 2; x < 14; x += 3) {
        town.place(kCitizenHouse, x, 8);
    }
    town.place(kPioneerHouse, 20, 4);
    town.place(kForest, 22, 4);
    town.place(kForest, 22, 5);
    town.place(kPioneerHouse, 23, 4);
    town.finish({});
    for (std::uint16_t x = 2; x < 14; x += 3) {
        town.add_house(x, 8, 2, 15);
    }
    town.add_house(20, 4, 0, 2);
    town.add_house(23, 4, 0, 2);
    sim::start_disaster(town.world, town.buildings, 0, {2, 8}, sim::Disaster::Kind::Plague, sim::kPlagueDuration,
                        nullptr);
    sim::start_disaster(town.world, town.buildings, 0, {20, 4}, sim::Disaster::Kind::Fire, sim::kFireDuration, nullptr);
    for (int i = 0; i < 60; ++i) {
        town.epoch();
    }
    test::check_golden("fire and plague after 60 epochs", test::world_hash(town.world, town.buildings),
                       0x5edc304ab021342bULL);
}
