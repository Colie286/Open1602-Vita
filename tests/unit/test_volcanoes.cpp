// SPDX-License-Identifier: GPL-3.0-or-later
// Volcanoes: craters, the eruption roll, lava.

#include <algorithm>
#include <cmath>
#include <map>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/volcanoes.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kHouse = 20605;
constexpr std::uint32_t kKontor = 20501;
constexpr std::uint32_t kMountain = 21103;  // a mountain that is no volcano
constexpr std::int32_t kIslandX = 100;
constexpr std::int32_t kIslandY = 80;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}};
    const auto doc = assets::cod::parse(
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Posoffs: 20\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n  Posoffs: 20\n"
        "  @Nummer: 2\n  Id: 20605\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n  Posoffs: 20\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"
        "  @Nummer: 3\n  Id: 21108\n  Kind: 15\n  Gfx: 20\n  Size: 4, 4\n  Posoffs: 20\n"
        "  @Nummer: 4\n  Id: 21103\n  Kind: 15\n  Gfx: 40\n  Size: 4, 4\n  Posoffs: 20\n"
        "  @Nummer: 5\n  Id: 20501\n  Kind: 35\n  Gfx: 60\n  Size: 3, 2\n  Posoffs: 20\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 12\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A 40 x 40 island of ground at (100, 80) on the world map, with the volcano mountain at
/// (18, 18) and city 0 of a human player.
struct Volcano {
    assets::BuildingTable buildings = make_buildings();
    sim::World world;
    sim::IslandMap map{40, 40};
    std::vector<sim::VolcanoEvent> events;
    std::vector<sim::DisasterEvent> disasters;
    std::vector<sim::ConstructionEvent> construction;

    Volcano() {
        for (std::int32_t y = 0; y < 40; ++y) {
            for (std::int32_t x = 0; x < 40; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0, 0, 0).ok());
            }
        }
        map.capture_ground();
        place(sim::kVolcanoRecordId, 18, 18);
    }
    void place(std::uint32_t id, std::int32_t x, std::int32_t y, std::uint8_t city = sim::MapCell::kNoCity) {
        REQUIRE(map.place(*buildings.by_id(id), x, y, 0, 0, city == 0 ? 0 : sim::kNoPlayer, city).ok());
    }
    void finish() {
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), kIslandX, kIslandY};
        island.header.number = 3;
        island.craters = sim::find_craters(island.map, buildings);
        REQUIRE(world.add_island(std::move(island)).ok());
        sim::City city;
        city.owner = 0;
        city.capacity = 30 * 32;
        city.inhabitants = {0};
        city.satisfaction = {128};
        city.tax_rate = {128};
        world.add_city(city);
        std::vector<sim::Player> players(1);
        players[0].role = sim::PlayerRole::Human;
        world.set_players(players);
    }
    std::uint32_t id(std::int32_t x, std::int32_t y) const {
        const sim::MapCell& c =
            world.islands()[0].map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        return c.empty() ? 0 : buildings.by_sprite(c.sprite)->id;
    }
    /// Game time on to `ms`, the volcanoes in 50-ms ticks like the simulation.
    void run_to(std::uint64_t ms) {
        while (world.time_ms() < ms) {
            world.set_time_ms(world.time_ms() + 50);
            sim::advance_volcanoes(world, buildings, sim::kLavaSpeed, &events, &disasters, &construction);
        }
    }
    /// A lump just above the ground of island tile (x, y), landing at its next step.
    void drop(std::int32_t x, std::int32_t y) {
        sim::LavaLump p;
        p.x = (std::int64_t{kIslandX} + x) * 65536 + 32768;
        p.y = (std::int64_t{kIslandY} + y) * 65536 + 32768;
        p.z = 20 * 1835;  // on the ground
        p.launch_us = 0;
        p.leg_start_us = world.time_ms() * 1000;
        p.leg_end_us = world.time_ms() * 1000;
        world.volcanoes().lava.push_back(p);
    }
    std::size_t count(sim::VolcanoEvent::Kind kind) const {
        return static_cast<std::size_t>(
            std::count_if(events.begin(), events.end(), [&](const sim::VolcanoEvent& e) { return e.kind == kind; }));
    }
};

}  // namespace

TEST_CASE("volcanoes: craters are the origins of the big ore mountain, top row first, right to left") {
    Volcano v;
    v.place(sim::kVolcanoRecordId, 2, 2);
    v.place(sim::kVolcanoRecordId, 30, 2);
    v.place(kMountain, 10, 1);
    const std::vector<sim::Crater> craters = sim::find_craters(v.map, v.buildings);
    REQUIRE(craters.size() == 2);  // the third one at (18, 18) is not kept
    CHECK(craters[0].x == 30);
    CHECK(craters[0].y == 2);
    CHECK(craters[1].x == 2);
    CHECK(craters[1].y == 2);
}

TEST_CASE("volcanoes: intervals shrink with the craters, an eruption takes 28.9 s") {
    CHECK(sim::eruption_interval_ms(0) == 2'100'000);
    CHECK(sim::eruption_interval_ms(1) == 2'100'000);
    CHECK(sim::eruption_interval_ms(3) == 1'180'000);
    CHECK(sim::eruption_interval_ms(8) == 480'000);
    CHECK(sim::eruption_interval_ms(20) == 360'000);
    CHECK(sim::eruption_interval_ms(50) == 360'000);
    CHECK(sim::eruption_step_us(1) == 500'000);
    // The last throw after 28.43 s, the end half a second later.
    CHECK(sim::eruption_step_us(140) > 28'400'000);
    CHECK(sim::eruption_step_us(140) < 28'450'000);
    CHECK(sim::eruption_step_us(141) - sim::eruption_step_us(140) == 500'000);
    sim::Random random(7, 1);
    const std::uint64_t first = sim::first_eruption_ms(random);
    CHECK(first >= 4'200'000);
    CHECK(first <= 4'200'000 + (15 * 60 + 31) * 1000);
}

TEST_CASE("volcanoes: the roll waits for the time and the settings, then the volcano erupts") {
    Volcano v;
    v.finish();
    REQUIRE(v.world.islands()[0].craters.size() == 1);
    v.world.volcanoes().next_ms = 600'000;
    v.world.set_time_ms(599'000);
    sim::roll_eruptions(v.world, sim::Rules{}, &v.events);
    CHECK(v.events.empty());

    v.world.set_time_ms(600'000);
    sim::World::GameSettings settings;
    settings.flags = 1U << 8;  // no volcanoes
    v.world.set_game_settings(settings);
    sim::roll_eruptions(v.world, sim::Rules{}, &v.events);
    CHECK(v.events.empty());
    sim::Rules on;
    on.volcanoes = sim::Volcanoes::On;
    sim::roll_eruptions(v.world, on, &v.events);
    REQUIRE(v.events.size() == 1);
    CHECK(v.events[0].kind == sim::VolcanoEvent::Kind::Erupted);
    CHECK(v.events[0].message);
    CHECK(v.events[0].x == 18);
    CHECK(v.world.islands()[0].volcano_erupted);
    CHECK(v.world.islands()[0].craters[0].steps_left == 140);
    // One crater in the world: 35 minutes and up to 15.5 more.
    const std::uint64_t next = v.world.volcanoes().next_ms;
    CHECK(next >= 600'000 + 2'100'000);
    CHECK(next <= 600'000 + 2'100'000 + 931'000);
    // While it runs no other eruption starts.
    v.world.volcanoes().next_ms = 0;
    sim::roll_eruptions(v.world, on, &v.events);
    CHECK(v.events.size() == 1);
}

TEST_CASE("volcanoes: the crater that erupted last is passed over once") {
    Volcano v;
    v.finish();
    v.world.volcanoes().next_ms = 0;
    v.world.volcanoes().last = sim::CraterRef{0, 18, 18};
    v.world.set_time_ms(10'000);
    sim::roll_eruptions(v.world, sim::Rules{}, &v.events);
    CHECK(v.events.empty());
    CHECK(!v.world.volcanoes().last.has_value());
    sim::roll_eruptions(v.world, sim::Rules{}, &v.events);  // the next island epoch
    CHECK(v.count(sim::VolcanoEvent::Kind::Erupted) == 1);
}

TEST_CASE("volcanoes: 141 lumps land 0.85 to 13.6 tiles from the mountain, then it is over") {
    Volcano v;
    v.finish();
    v.world.set_time_ms(1000);
    REQUIRE(sim::erupt(v.world, sim::CraterRef{0, 18, 18}, 1234, true, &v.events));
    const double cx = kIslandX + 20.0;
    const double cy = kIslandY + 20.0;
    std::map<std::uint64_t, sim::LavaLump> flying;
    std::vector<double> ranges;
    std::size_t thrown = 0;
    while (v.world.time_ms() < 40'000) {
        v.run_to(v.world.time_ms() + 50);
        std::map<std::uint64_t, sim::LavaLump> now;
        for (const sim::LavaLump& p : v.world.volcanoes().lava) {
            now[p.launch_us] = p;
            thrown += flying.count(p.launch_us) == 0 ? 1U : 0U;
        }
        for (const auto& [launch, p] : flying) {
            if (now.count(launch) == 0) {
                const sim::LavaPoint at = sim::lava_position(p, p.leg_end_us);
                ranges.push_back(std::hypot(at.x - cx, at.y - cy));
            }
        }
        flying = std::move(now);
    }
    CHECK(thrown == 141);
    CHECK(ranges.size() == 141);
    CHECK(v.world.volcanoes().lava.empty());
    CHECK(v.world.volcanoes().eruptions.empty());
    CHECK(v.count(sim::VolcanoEvent::Kind::Ended) == 1);
    REQUIRE(v.world.volcanoes().last.has_value());
    CHECK(v.world.volcanoes().last->x == 18);
    CHECK(v.world.islands()[0].craters[0].steps_left == 0);
    for (const double r : ranges) {
        CHECK(r > 0.8);
        CHECK(r < 13.7);
    }
}

TEST_CASE("volcanoes: lava sets houses on fire and destroys roads and the Kontor") {
    Volcano v;
    v.place(kHouse, 4, 4, 0);
    v.place(kRoad, 4, 8, 0);
    v.place(kKontor, 10, 4, 0);
    v.finish();
    sim::House h;
    h.x = 4;
    h.y = 4;
    h.city = 0;
    h.inhabitants64 = 64;
    v.world.add_house(h);
    v.world.set_time_ms(5000);

    v.drop(5, 5);    // the house's lower right tile
    v.drop(4, 8);    // road
    v.drop(30, 30);  // open ground: nothing
    sim::advance_volcanoes(v.world, v.buildings, sim::kLavaSpeed, &v.events, &v.disasters, &v.construction);
    CHECK(v.world.volcanoes().lava.empty());
    const sim::Disaster* fire = sim::disaster_at(v.world, 0, 4, 4);
    REQUIRE(fire != nullptr);
    CHECK(fire->kind == sim::Disaster::Kind::Fire);
    CHECK(fire->duration == 20);
    CHECK(v.id(4, 8) == kGround);
    CHECK(v.count(sim::VolcanoEvent::Kind::Fire) == 1);
    CHECK(v.count(sim::VolcanoEvent::Kind::Destroyed) == 1);

    // A burning house does not catch fire anew.
    v.world.disasters()[0].elapsed = 7;
    v.drop(4, 4);
    sim::advance_volcanoes(v.world, v.buildings, sim::kLavaSpeed, &v.events, &v.disasters, &v.construction);
    CHECK(sim::disaster_at(v.world, 0, 4, 4)->elapsed == 7);
    CHECK(v.count(sim::VolcanoEvent::Kind::Fire) == 1);

    // The Kontor is destroyed at once.
    v.drop(11, 5);
    sim::advance_volcanoes(v.world, v.buildings, sim::kLavaSpeed, &v.events, &v.disasters, &v.construction);
    CHECK(v.id(10, 4) == kGround);
    CHECK(v.count(sim::VolcanoEvent::Kind::Destroyed) == 2);
}

TEST_CASE("volcanoes: the same seed throws the same lava; the deposit of the mountain goes") {
    const auto run = [](std::uint16_t seed) {
        Volcano v;
        for (std::int32_t y = 8; y < 32; y += 3) {
            for (std::int32_t x = 6; x < 34; x += 3) {
                if (x < 16 || x > 23 || y < 16 || y > 23) {
                    v.place(kRoad, x, y, 0);
                }
            }
        }
        v.finish();
        auto& deposits = v.world.island(0).header.deposits;
        deposits.push_back(assets::IslandHeader::Deposit{2, 18, 18, 0, 0, 100});
        deposits.push_back(assets::IslandHeader::Deposit{3, 4, 4, 0, 0, 100});
        v.world.set_time_ms(1000);
        REQUIRE(sim::erupt(v.world, sim::CraterRef{0, 18, 18}, seed, true, &v.events));
        CHECK(deposits.size() == 1);
        CHECK(deposits[0].x == 4);
        v.run_to(40'000);
        std::vector<std::pair<std::uint16_t, std::uint16_t>> hits;
        for (const sim::VolcanoEvent& e : v.events) {
            if (e.kind == sim::VolcanoEvent::Kind::Destroyed) {
                hits.emplace_back(e.x, e.y);
            }
        }
        return hits;
    };
    const auto a = run(77);
    CHECK(!a.empty());
    CHECK(a == run(77));
    CHECK(a != run(78));
}
