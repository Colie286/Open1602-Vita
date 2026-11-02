// SPDX-License-Identifier: GPL-3.0-or-later
// Consumption of the inhabitants.

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint8_t kFood = 14;
constexpr std::uint8_t kTobacco = 15;
constexpr std::uint8_t kSpices = 16;
constexpr std::uint8_t kAlcohol = 18;
constexpr std::uint8_t kCloth = 19;

/// The BGRUPPE block of haeuser.cod (classes 0..2) with the global Nahrung.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}, {"BGRUPPE_WARE", 6}};
    const auto doc = assets::cod::parse(
        "Nahrung: 1.3\n"
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 19, 0.6\n    Ware: 18, 0.5\n  EndObj\n"
        "  Nummer: 2\n  Prozent: 71\n  Maxwohn: 15\n  Steuer: 2.1\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 19, 0.7\n    Ware: 18, 0.6\n    Ware: 15, 0.5\n    Ware: 16, 0.5\n"
        "  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n  Nummer: 0\n  Id: 20101\n  Gfx: 0\nEndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

sim::City make_city(std::uint32_t pioneers, std::uint32_t settlers) {
    sim::City c;
    c.capacity = 30 * 32;
    c.inhabitants = {pioneers, settlers, 0};
    c.ensure_ware(kCloth);
    return c;
}

}  // namespace

TEST_CASE("consumption: needs converted like the original") {
    const auto b = make_buildings();
    CHECK(b.food_need() == 17);  // trunc(1.3 * 8192) / 600
    REQUIRE(b.resident_classes().size() == 3);
    const auto& settlers = b.resident_classes()[1];
    CHECK(settlers.share == 60);  // 47 * 128 / 100
    CHECK(settlers.tax == 8);     // trunc(1.6 * 16 / 3)
    REQUIRE(settlers.needs.size() == 2);
    CHECK(settlers.needs[0].ware == kCloth);
    CHECK(settlers.needs[0].amount == 8);  // trunc(0.6 * 8192) / 600
    CHECK(settlers.needs[1].amount == 6);
    CHECK((sim::consumer_goods(b) == std::vector<std::uint8_t>{kFood, kTobacco, kSpices, kAlcohol, kCloth}));
}

TEST_CASE("consumption: the city eats from the warehouse, about 15/16 of the nominal need") {
    const auto b = make_buildings();
    sim::City city = make_city(0, 100);
    city.goods[kFood] = 900;
    city.goods[kCloth] = 900;
    city.goods[kAlcohol] = 900;
    std::vector<sim::CityEvent> events;
    // First tick: no demand yet, nothing taken.
    sim::consume(city, b, &events);
    CHECK(city.goods[kFood] == 900);
    CHECK(city.supply[kFood].demand == 1700 * 15 / 16);
    // Steady state: d = 1700 per tick -> about 15/16 * d / 256 = 6.2 units (1/32 t) per tick.
    for (int i = 0; i < 100; ++i) {
        sim::consume(city, b, &events);
    }
    const std::uint32_t before = city.goods[kFood];
    for (int i = 0; i < 10; ++i) {
        sim::consume(city, b, &events);
    }
    const std::uint32_t used = before - city.goods[kFood];
    CHECK(used >= 60);
    CHECK(used <= 70);
    CHECK(city.food_ratio == 128);
    CHECK(city.satisfaction[1] == 128);  // cloth and alcohol fully supplied
    CHECK(events.empty());
    // The food runs out: the supply falls, the city reports a shortage, then hunger.
    city.goods[kFood] = 0;
    for (int i = 0; i < 12; ++i) {
        sim::consume(city, b, &events);
    }
    CHECK(city.food_ratio < 77);
    REQUIRE(!events.empty());
    CHECK(events.front().kind == sim::CityEvent::Kind::Shortage);
    CHECK(events.front().ware == kFood);
    bool hunger = false;
    for (const auto& e : events) {
        hunger |= e.kind == sim::CityEvent::Kind::Hunger;
    }
    CHECK(hunger);
}

TEST_CASE("consumption: satisfaction against Prozent and the tax rate") {
    const auto b = make_buildings();
    sim::City city = make_city(14, 76);
    city.supply.resize(city.goods.size());
    city.supply[kCloth].ratio = 128;
    city.supply[kAlcohol].ratio = 128;
    city.supply[kTobacco].ratio = 0;
    city.supply[kSpices].ratio = 0;
    // Nova Semlja in the savegame: settlers content, citizens 256 * 128 / 360 = 91.
    CHECK(sim::satisfaction_of(city, b.resident_classes()[0], 128) == 128);
    CHECK(sim::satisfaction_of(city, b.resident_classes()[1], 128) == 128);
    CHECK(sim::satisfaction_of(city, b.resident_classes()[2], 128) == 91);
    // Lower taxes ask for less (curve[64] = 86), higher ones for more.
    CHECK(sim::satisfaction_of(city, b.resident_classes()[2], 64) == 128);
    CHECK(sim::satisfaction_of(city, b.resident_classes()[2], 192) == 256 * 128 / (352 * 360 / 128));
}

TEST_CASE("consumption: a castle or cathedral lowers the taxes satisfaction is measured against") {
    const auto b = make_buildings();
    sim::City city = make_city(14, 76);
    city.supply.resize(city.goods.size());
    city.supply[kCloth].ratio = 128;
    city.supply[kAlcohol].ratio = 128;
    city.supply[kTobacco].ratio = 0;
    city.supply[kSpices].ratio = 0;
    // Bonus 800: tax (128 - 25) * 128 / 128 = 103, curve[103] = 102, 256 * 128 / (102 * 360 / 128).
    city.satisfaction_bonus = 800;
    CHECK(sim::satisfaction_of(city, b.resident_classes()[2], 128) == 256 * 128 / (102 * 360 / 128));
    // At 192: (103 * 192) >> 7 = 154, curve[154] = 182.
    CHECK(sim::satisfaction_of(city, b.resident_classes()[2], 192) == 256 * 128 / (182 * 360 / 128));
    // Beyond 4096 the original reads before its table (S-038); Open1602 stops at tax 0, curve[0] = 76.
    city.satisfaction_bonus = 6000;
    CHECK(sim::satisfaction_of(city, b.resident_classes()[2], 128) == 128);

    // The city tick takes 1/256 off.
    city.satisfaction_bonus = 800;
    sim::consume(city, b, nullptr);
    CHECK(city.satisfaction_bonus == 796);
}

TEST_CASE("consumption: natives dump what fills their store beyond 75 %") {
    sim::City village;
    village.capacity = 40 * 32;
    village.ensure_ware(21);
    village.goods[5] = 35 * 32;  // above 30 t
    village.goods[6] = 20 * 32;
    village.goods[kCloth] = 10;
    sim::consume_natives(village);
    CHECK(village.goods[5] == 35 * 32 - 30 * 32 / 4);
    CHECK(village.goods[6] == 20 * 32);
    CHECK(village.goods[kCloth] == 9);
}

TEST_CASE("consumption: pirates take random goods") {
    sim::City nest;
    nest.ensure_ware(24);
    for (std::uint8_t w = 2; w <= 24; ++w) {
        nest.goods[w] = 20 * 32;
    }
    sim::Random random(3, 4);
    std::uint32_t before = 0;
    for (const std::uint32_t g : nest.goods) {
        before += g;
    }
    for (int i = 0; i < 10; ++i) {
        sim::consume_pirates(nest, random);
    }
    std::uint32_t after = 0;
    for (const std::uint32_t g : nest.goods) {
        after += g;
    }
    CHECK(after < before);
    CHECK((before - after) % (3 * 32) == 0);  // always 3 t from a ware with at least 10 t
    CHECK(before - after <= 20 * 3 * 32);
}

TEST_CASE("consumption: the people complain every 18 minutes while the building material is locked") {
    sim::World world(100, 100);
    sim::City city = make_city(10, 0);
    std::vector<sim::CityEvent> events;

    world.set_time_ms(5 * 60'000);
    sim::remind_material_lock(world, city, &events);
    CHECK(events.empty());  // not locked
    CHECK(city.material_reminder_ms == 0);

    city.material_lock = true;  // a savegame without a clock: it starts now
    sim::remind_material_lock(world, city, &events);
    CHECK(events.empty());
    CHECK(city.material_reminder_ms == 5 * 60'000);

    world.set_time_ms(23 * 60'000 - 1);
    sim::remind_material_lock(world, city, &events);
    CHECK(events.empty());  // not yet
    world.set_time_ms(23 * 60'000);
    sim::remind_material_lock(world, city, &events);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == sim::CityEvent::Kind::MaterialLocked);
    CHECK(events[0].owner == city.owner);
    CHECK(city.material_reminder_ms == 23 * 60'000);  // the next in another 18 minutes

    city.material_lock = false;
    world.set_time_ms(60 * 60'000);
    sim::remind_material_lock(world, city, &events);
    CHECK(events.size() == 1);  // unlocked: no more complaints
}

TEST_CASE("consumption: a city announces each mark of its inhabitants once") {
    CHECK(sim::population_message(99) == 0);
    CHECK(sim::population_message(100) == 0x11);
    CHECK(sim::population_message(299) == 0x12);
    CHECK(sim::population_message(499) == 0x13);
    CHECK(sim::population_message(999) == 0x14);
    CHECK(sim::population_message(1000) == 0x15);
    CHECK(sim::population_message(8999) == 0x1C);
    CHECK(sim::population_message(9000) == 0x1D);
    CHECK(sim::population_message(50'000) == 0x1D);  // 10000 (0x1E) never comes

    sim::City city = make_city(10, 0);
    city.inhabitants.assign(5, 0);
    city.inhabitants[0] = 180;
    std::vector<sim::CityEvent> events;
    sim::announce_population(city, 30, &events);  // workers count too
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == sim::CityEvent::Kind::Population);
    CHECK(events[0].message == 0x12);
    CHECK(city.population_message == 0x12);
    sim::announce_population(city, 30, &events);
    city.inhabitants[0] = 90;  // shrinking and growing again: no second time
    sim::announce_population(city, 30, &events);
    city.inhabitants[0] = 200;
    sim::announce_population(city, 30, &events);
    CHECK(events.size() == 1);
    city.inhabitants[0] = 400;
    sim::announce_population(city, 0, &events);
    REQUIRE(events.size() == 2);
    CHECK(events[1].message == 0x13);
}
