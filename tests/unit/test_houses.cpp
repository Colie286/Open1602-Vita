// SPDX-License-Identifier: GPL-3.0-or-later
// Houses: coverage by public buildings, growth, upgrade, downgrade, ruins.

#include <array>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kPioneerHouse = 20605;
constexpr std::uint32_t kSettlerHouse = 20621;
constexpr std::uint32_t kMarket = 20809;
constexpr std::uint32_t kChapel = 20803;
constexpr std::uint32_t kRuin = 22601;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}, {"BGRUPPE_WARE", 6}};
    const auto doc = assets::cod::parse(
        "Nahrung: 1.3\n"
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 19, 0.6\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20605\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n  Ruinenr: 0\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 3\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20621\n  Kind: 14\n  Gfx: 14\n  Size: 2, 2\n  Ruinenr: 0\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 1\n    Radius: 6\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 1\n    Holz: 3\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20809\n  Kind: 14\n  Gfx: 20\n  Size: 4, 3\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 16\n    Kosten: 10, 5\n    Arbeiter: 4\n  EndObj\n"
        "  @Nummer: 5\n  Id: 20803\n  Kind: 14\n  Gfx: 40\n  Size: 2, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 18\n    Radius: 8\n    Kosten: 5\n    Arbeiter: 2\n  EndObj\n"
        "  @Nummer: 6\n  Id: 22601\n  Kind: 12\n  Gfx: 50\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

struct Town {
    assets::BuildingTable buildings = make_buildings();
    sim::World world;
    sim::IslandMap map{40, 30};

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
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 0, 0}).ok());
        sim::City city;
        city.owner = 0;
        city.capacity = 30 * 32;
        city.inhabitants = {0, 0};
        city.satisfaction = {128, 128};
        city.tax_rate = {128, 128};
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
    sim::House& add_house(std::uint16_t x, std::uint16_t y, std::uint8_t cls, std::uint32_t inhabitants64) {
        sim::House h;
        h.x = x;
        h.y = y;
        h.city = 0;
        h.resident_class = cls;
        h.inhabitants64 = inhabitants64;
        world.add_house(h);
        world.city(0, 0)->inhabitants[cls] += inhabitants64 >> 6;
        return *world.house(0, x, y);
    }
    std::uint32_t id(std::int32_t x, std::int32_t y) const {
        return buildings
            .by_sprite(world.islands()[0].map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).sprite)
            ->id;
    }
    /// Runs the house clocks for `seconds` of game time.
    std::vector<sim::HouseEvent> run(std::uint32_t seconds) {
        std::vector<sim::HouseEvent> events;
        for (std::uint32_t t = 0; t < seconds * 20; ++t) {
            sim::advance_houses(world, buildings, 50, &events);
            if (t % 200 == 199) {
                world.city(0, 0)->epoch = static_cast<std::uint8_t>((world.city(0, 0)->epoch + 1) & 7U);
            }
        }
        return events;
    }
};

}  // namespace

TEST_CASE("houses: coverage by market, chapel and their radius") {
    Town town;
    town.place(kMarket, 10, 11);  // 4 x 3: centre columns 11..12, row 12
    town.place(kChapel, 20, 11);  // 2 x 1, radius 8
    town.place(kPioneerHouse, 14, 13);
    town.place(kPioneerHouse, 30, 20);  // 18 columns from the market: outside
    town.finish({{10, 11}, {20, 11}});
    town.add_house(14, 13, 0, 64);
    town.add_house(30, 20, 0, 64);
    sim::update_services(town.world, town.buildings);
    const sim::House& near = *town.world.house(0, 14, 13);
    const sim::House& far = *town.world.house(0, 30, 20);
    CHECK(near.market);
    // Gaps 2 (columns 12 -> 14) and 1 (row 12 -> 13): round(0.375 * sqrt 5) = 1.
    CHECK(near.market_distance == 1);
    CHECK((near.services & sim::house_service::kKapelle) != 0);
    CHECK(near.services_complete);  // pioneers: a market is enough
    CHECK(!far.market);
    CHECK(far.market_distance == 6);
    CHECK((far.services & sim::house_service::kKapelle) == 0);
    CHECK(!far.services_complete);
    CHECK(sim::has_services(near, 1));  // market and chapel: settlers can live here
    CHECK(!sim::has_services(near, 2));
    // The house panel shows neither the market nor the chapel as missing.
    CHECK(sim::panel_services(near) == (sim::panel_service::kMarkt | sim::panel_service::kKapelle));
}

TEST_CASE("houses: the panel's icons translate the coverage bits") {
    namespace ps = sim::panel_service;
    namespace hs = sim::house_service;
    sim::House h;
    CHECK(sim::panel_services(h) == 0);
    // The settler house of the report: market and chapel in reach, nothing else.
    h.market = true;
    h.services = hs::kKapelle;
    CHECK(sim::panel_services(h) == (ps::kMarkt | ps::kKapelle));
    // A church stands in for a chapel, a college for a school.
    h.market = false;
    h.services = hs::kKirche | hs::kHochschule;
    CHECK(sim::panel_services(h) == (ps::kKirche | ps::kKapelle | ps::kHochschule | ps::kSchule));
    h.services = hs::kWirtshaus | hs::kSchule | hs::kBadehaus | hs::kTheater;
    CHECK(sim::panel_services(h) == (ps::kWirtshaus | ps::kSchule | ps::kBadehaus | ps::kTheater));
    // Plague, fire and the Kontor have no icon; a Kontor is no market.
    h.services = hs::kDisasterMask | hs::kKontor;
    CHECK(sim::panel_services(h) == 0);
    h.services = hs::kKlinik | hs::kGalgen | hs::kBrunnen;
    CHECK(sim::panel_services(h) == (ps::kKlinik | ps::kGalgen | ps::kBrunnen));
}

TEST_CASE("houses: growth rates like the original's tables") {
    sim::City city;
    city.satisfaction = {128, 128};
    city.food_ratio = 128;
    sim::House h;
    h.market = true;
    h.services_complete = true;
    h.market_distance = 0;
    CHECK(sim::growth_rate(city, h) == 160);  // T2[0]
    h.market_distance = 6;
    CHECK(sim::growth_rate(city, h) == 115);
    city.food_ratio = 100;  // nearly fed: stays
    CHECK(sim::growth_rate(city, h) == 0);
    // No market: settlers see satisfaction 0 and services missing.
    city.food_ratio = 128;
    h.resident_class = 1;
    h.market = false;
    h.services_complete = false;
    h.services = sim::house_service::kKontor;  // the Kontor still brings food
    CHECK(sim::growth_rate(city, h) == -226);  // 11 % per visit
    h.services = 0;                            // nothing at all
    CHECK(sim::growth_rate(city, h) == -514);
}

TEST_CASE("houses: services recomputed for some cities leave the others alone") {
    Town town;
    town.place(kMarket, 10, 11);
    town.place(kPioneerHouse, 14, 13);
    town.place(kPioneerHouse, 18, 13);
    town.finish({{10, 11}});
    town.add_house(14, 13, 0, 64);
    town.add_house(18, 13, 0, 64);
    town.world.house(0, 18, 13)->city = 1;  // a house the cells call city 0, kept apart
    sim::update_services(town.world, town.buildings, 0, std::set<std::uint8_t>{1});
    CHECK(!town.world.house(0, 14, 13)->market);  // city 0 not asked for: untouched
    CHECK(!town.world.house(0, 18, 13)->market);  // city 1 has no market
    sim::update_services(town.world, town.buildings, 0, std::set<std::uint8_t>{0});
    CHECK(town.world.house(0, 14, 13)->market);
    CHECK(town.world.house(0, 14, 13)->services_complete);
    CHECK(!town.world.house(0, 18, 13)->market);
    // The same as the whole island for the city asked for.
    sim::update_services(town.world, town.buildings, 0);
    CHECK(town.world.house(0, 14, 13)->market_distance == 1);
}

TEST_CASE("houses: a full house rises to the next class with building material") {
    Town town;
    town.place(kMarket, 10, 11);
    town.place(kChapel, 16, 11);
    town.place(kPioneerHouse, 14, 13);
    town.finish({{10, 11}, {16, 11}});
    sim::City& city = *town.world.city(0, 0);
    city.ensure_ware(24);
    city.goods[22] = 32;      // tools
    city.goods[23] = 3 * 32;  // wood
    town.add_house(14, 13, 0, 64);
    sim::update_services(town.world, town.buildings);
    const auto events = town.run(600);
    const sim::House& h = *town.world.house(0, 14, 13);
    CHECK(h.resident_class == 1);
    CHECK(town.id(14, 13) == kSettlerHouse);
    CHECK(h.inhabitants() >= 3);
    CHECK(city.inhabitants[0] == 0);
    CHECK(city.inhabitants[1] == h.inhabitants());
    CHECK(city.goods[22] == 0);  // paid
    CHECK(city.goods[23] == 0);
    REQUIRE(!events.empty());
    CHECK(events.front().kind == sim::HouseEvent::Kind::Upgraded);
}

TEST_CASE("houses: without material the house stays, without food it is abandoned") {
    Town town;
    town.place(kMarket, 10, 11);
    town.place(kChapel, 16, 11);
    town.place(kPioneerHouse, 14, 13);
    town.finish({{10, 11}, {16, 11}});
    town.add_house(14, 13, 0, 64);
    sim::update_services(town.world, town.buildings);
    town.run(600);
    CHECK(town.world.house(0, 14, 13)->resident_class == 0);
    CHECK(town.world.house(0, 14, 13)->inhabitants() == 2);  // full, clamped
    // Hunger: the house shrinks to nothing and turns into ruins.
    town.world.city(0, 0)->food_ratio = 0;
    const auto events = town.run(900);
    CHECK(town.world.house(0, 14, 13) == nullptr);
    CHECK(town.id(14, 13) == kRuin);
    CHECK(town.id(15, 14) == kRuin);
    CHECK(town.world.city(0, 0)->inhabitants[0] == 0);
    REQUIRE(!events.empty());
    CHECK(events.back().kind == sim::HouseEvent::Kind::Abandoned);
}

TEST_CASE("houses: settlers without a chapel fall back to pioneers") {
    Town town;
    town.place(kMarket, 10, 11);
    town.place(kSettlerHouse, 14, 13);
    town.finish({{10, 11}});
    town.add_house(14, 13, 1, 6 * 64);
    sim::update_services(town.world, town.buildings);
    const auto events = town.run(900);
    const sim::House* h = town.world.house(0, 14, 13);
    REQUIRE(h != nullptr);
    CHECK(h->resident_class == 0);
    CHECK(town.id(14, 13) == kPioneerHouse);
    bool downgraded = false;
    for (const auto& e : events) {
        downgraded |= e.kind == sim::HouseEvent::Kind::Downgraded;
    }
    CHECK(downgraded);
}

TEST_CASE("einwohner: residents plus the workers of the city's buildings, for one city as for all") {
    // The top bar counted the residents only and showed 150 where the player status, the city
    // window and the population message counted 210 (calls the same).
    Town town;
    town.place(kMarket, 10, 11);
    town.place(kChapel, 16, 11);
    town.finish({{10, 11}, {16, 11}});
    sim::City other;
    other.index = 1;
    other.owner = 0;
    town.world.add_city(other);
    sim::City& city = *town.world.city(0, 0);
    city.inhabitants = {100, 50};
    const std::vector<std::uint32_t> all = sim::city_workers(town.world, town.buildings);
    REQUIRE(all.size() == 2);
    CHECK(all[0] == 6);
    CHECK(all[1] == 0);
    CHECK(sim::city_workers(town.world, town.buildings, city) == 6);
    CHECK(sim::city_workers(town.world, town.buildings, *town.world.city(0, 1)) == 0);
    CHECK(sim::city_einwohner(city, sim::city_workers(town.world, town.buildings, city)) == 156);
    // A switched-off building keeps its workers.
    town.world.productions()[0].enabled = false;
    CHECK(sim::city_workers(town.world, town.buildings, city) == 6);
}

TEST_CASE("money: taxes and running costs") {
    Town town;
    town.place(kMarket, 10, 11);
    town.place(kChapel, 16, 11);
    town.finish({{10, 11}, {16, 11}});
    sim::City& city = *town.world.city(0, 0);
    city.inhabitants = {10, 20};
    // K = trunc(1.4 * 16 / 3) = 7, trunc(1.6 * 16 / 3) = 8: 7 * 10 * 6 / 32 + 8 * 20 * 6 / 32 = 13 + 30.
    CHECK(sim::tax_income(city, town.buildings) == 43);
    city.tax_rate = {64, 192};
    CHECK(sim::tax_income(city, town.buildings) == 13 * 64 / 128 + 30 * 192 / 128);
    // Market 10 and chapel 5 while switched on; the market switched off costs 5.
    CHECK(sim::running_costs(town.world, town.buildings)[0] == 15);
    town.world.productions()[0].enabled = false;
    CHECK(sim::running_costs(town.world, town.buildings)[0] == 10);
    // One city alone, as the Kontor page asks (FB-4), gives the same figure.
    CHECK(sim::running_cost(town.world, town.buildings, city) == 10);
    const sim::City stranger = city;
    CHECK(sim::running_cost(town.world, town.buildings, stranger) == 0);
}

TEST_CASE("world: cities are found by island and number after adding, removing and replacing (TB-21)") {
    sim::World world;
    for (const auto& [island, index] : {std::pair{3, 1}, std::pair{0, 2}, std::pair{3, 0}, std::pair{1, 0}}) {
        sim::City c;
        c.island = static_cast<std::uint16_t>(island);
        c.index = static_cast<std::uint8_t>(index);
        c.owner = static_cast<sim::PlayerId>(island);
        world.add_city(c);
    }
    const auto found = [&](std::uint16_t island, std::uint8_t index) {
        const sim::City* c = std::as_const(world).city(island, index);
        return c != nullptr && c->island == island && c->index == index;
    };
    CHECK(found(3, 1));
    CHECK(found(0, 2));
    CHECK(found(3, 0));
    CHECK(found(1, 0));
    CHECK(!found(3, 2));
    CHECK(!found(2, 0));
    world.remove_city(3, 1);
    CHECK(!found(3, 1));
    CHECK(found(3, 0));
    CHECK(found(1, 0));
    CHECK(world.city(1, 0) == &world.cities()[2]);
    std::vector<sim::City> loaded(1);
    loaded[0].island = 7;
    loaded[0].index = 4;
    world.set_cities(loaded);
    CHECK(found(7, 4));
    CHECK(!found(0, 2));
}

TEST_CASE("houses: the panel's required buildings come from the needs of each class (MD-2)") {
    // The original of the original.
    constexpr std::array<std::uint32_t, 5> kOriginal{0x01, 0x03, 0x0F, 0x3D, 0xF9};
    static_assert(kOriginal.size() == sim::kResidentClasses);
    for (std::uint8_t c = 0; c < kOriginal.size(); ++c) {
        CHECK(sim::required_panel_services(c) == kOriginal[c]);
    }
    CHECK(sim::required_panel_services(5) == 0);
}

TEST_CASE("game data: ware groups and unlock levels are tables (MD-3, MD-8)") {
    CHECK(sim::ware_group(sim::ware::kNone) == sim::WareGroup::None);
    CHECK(sim::ware_group(sim::ware::kAll) == sim::WareGroup::None);
    CHECK(sim::ware_group(sim::ware::kOre) == sim::WareGroup::Trade);
    CHECK(sim::ware_group(sim::ware::kBricks) == sim::WareGroup::Trade);
    CHECK(sim::ware_group(sim::ware::kFirstSoldier) == sim::WareGroup::Soldier);
    CHECK(sim::ware_group(sim::ware::kLastSoldier) == sim::WareGroup::Soldier);
    CHECK(sim::ware_group(sim::ware::kCornField) == sim::WareGroup::Field);
    CHECK(sim::ware_group(sim::ware::kTreasure) == sim::WareGroup::Field);
    CHECK(sim::ware_group(59) == sim::WareGroup::None);
    CHECK(sim::ware_group(1000) == sim::WareGroup::None);

    // Levels by bit n - 1; the fleets grow at 2D, 3C, 4B and 5A as in the original.
    sim::Player p;
    CHECK(p.unlocked(sim::infra::kNone));
    CHECK(!p.unlocked(sim::infra::kLevel2A));
    CHECK(p.ship_step() == 0);
    p.infra_mask = 1U << (sim::infra::kLevel2D - 1);
    CHECK(p.ship_step() == 1);
    p.infra_mask |= 1U << (sim::infra::kLevel4B - 1);
    CHECK(p.ship_step() == 3);
    p.infra_mask = 1U << (sim::infra::kLevel5B - 1);
    CHECK(p.unlocked(sim::infra::kLevel5B));
    CHECK(!p.unlocked(sim::infra::kMaxLevel + 1));
    CHECK(sim::infra::ware_level(sim::ware::kJewellery) == sim::infra::kLevel4A);
    CHECK(sim::infra::ware_level(sim::ware::kCloth) == sim::infra::kNone);
}
