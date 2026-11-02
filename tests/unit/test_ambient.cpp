// SPDX-License-Identifier: GPL-3.0-or-later
// Townsfolk, sea creatures and jugglers: life in the picture, no part of the game.

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <set>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/render/ambient.hpp"
#include "o1602/render/land_figures.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kHouse = 20605;
constexpr std::uint32_t kTavern = 20907;
constexpr std::uint32_t kMarket = 20801;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}};
    const auto doc = assets::cod::parse(
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20605\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n  Posoffs: 20\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20907\n  Kind: 14\n  Gfx: 20\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 17\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20801\n  Kind: 14\n  Gfx: 30\n  Size: 4, 3\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Rauchfignr: 0, 70\n  EndObj\n"
        "  @Nummer: 5\n  Id: 21201\n  Kind: 19\n  Gfx: 50\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// The townsfolk, the sea creatures and the juggler with the values of figuren.cod that matter.
assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    std::string cod = "Objekt: FIGUR\n";
    for (int n = 93; n <= 100; ++n) {
        cod += "  Nummer: " + std::to_string(n) +
               "\n  Speed: 200\n  Rotate: 8\n  Gfx: 0\n"
               "  Objekt: ANIM\n    Nummer: 0\n    Kind: 1\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 8\n"
               "    AnimSpeed: 85\n  EndObj\n";
    }
    cod +=
        "  Nummer: 101\n  Speed: 330\n  Worktime: 5.7\n  Rotate: 24\n  Gfx: 0\n"
        "  Objekt: ANIM\n    Nummer: 0\n    Kind: 1\n    AnimAnz: 15\n    AnimSpeed: 71\n  EndObj\n"
        "  Nummer: 102\n  Speed: 200\n  Worktime: 3\n  Rotate: 16\n  Gfx: 456\n"
        "  Objekt: ANIM\n    Nummer: 0\n    Kind: 1\n    AnimAnz: 16\n    AnimSpeed: 80\n"
        "    Nummer: 2\n    Kind: 1\n    AnimAnz: 1\n    AnimSpeed: 600\n"
        "    Nummer: 3\n    Kind: 1\n    AnimAnz: 1\n    AnimSpeed: 1100\n  EndObj\n"
        "  Nummer: 103\n  Speed: 200\n  Worktime: 5\n  Gfx: 384\n"
        "  Objekt: ANIM\n    Nummer: 0\n    Kind: 1\n    AnimAnz: 24\n    AnimSpeed: 60\n  EndObj\n"
        "  Nummer: 70\n  Rotate: 8\n  Gfx: 0\n  Fahnoffs: 2.1, 0, 0.6\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 1\n    AnimAnz: 8\n    AnimSpeed: 90\n"
        "    Nummer: 1\n    Kind: 1\n    AnimAnz: 8\n    AnimSpeed: 90\n"
        "    Nummer: 2\n    Kind: 1\n    AnimAnz: 8\n    AnimSpeed: 90\n"
        "    Nummer: 3\n    Kind: 1\n    AnimAnz: 8\n    AnimSpeed: 90\n"
        "    Nummer: 4\n    Kind: 1\n    AnimAnz: 8\n    AnimSpeed: 90\n"
        "  EndObj\n"
        "EndObj\n";
    auto table = assets::FigureTable::from_cod(assets::cod::parse(cod, types));
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A 30 x 20 island at (100, 100) of a 168 x 140 world with a road along row 10, a house of city
/// 0 at its west end, a tavern 9 steps east (inside the 21 x 21 window of the search) and a market;
/// open sea around it.
struct Scene {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{168, 140};
    sim::IslandMap map{30, 20};
    render::AmbientView view;

    Scene() {
        for (std::int32_t y = 0; y < 20; ++y) {
            for (std::int32_t x = 0; x < 30; ++x) {
                REQUIRE(
                    map.place(*buildings.by_id(y == 10 && x >= 3 && x <= 10 ? kRoad : kGround), x, y, 0, 0, 0, 0).ok());
            }
        }
        map.capture_ground();
        REQUIRE(map.place(*buildings.by_id(kHouse), 1, 9, 0, 0, 0, 0).ok());
        REQUIRE(map.place(*buildings.by_id(kTavern), 11, 9, 0, 0, 0, 0).ok());
        REQUIRE(map.place(*buildings.by_id(kMarket), 20, 2, 0, 0, 0, 0).ok());
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 100, 100}).ok());
        sim::City city;
        city.owner = 0;
        city.inhabitants = {10, 40, 0, 0, 0};
        world.add_city(city);
        std::vector<sim::Player> players(1);
        players[0].role = sim::PlayerRole::Human;
        world.set_players(players);
        sim::House h;
        h.x = 1;
        h.y = 9;
        h.city = 0;
        world.add_house(h);
        view.islands = {true};
    }
    sim::City& city() { return *world.city(0, 0); }

    /// One tick of game time for every ambient.
    void tick(std::initializer_list<render::Ambient*> ambients) {
        world.set_time_ms(world.time_ms() + sim::Simulation::kTickMs);
        for (render::Ambient* a : ambients) {
            a->tick(world, buildings, figures);
        }
    }
    /// `ms` of game time, pictures every `frame_ms`; `each` after every picture.
    template <typename F>
    void run(render::Ambient& a, std::uint64_t ms, F&& each, std::uint64_t frame_ms = 100) {
        a.tick(world, buildings, figures);  // starts it, or nothing new
        for (std::uint64_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
            tick({&a});
            if (world.time_ms() % frame_ms == 0) {
                a.frame(world, buildings, figures, view);
                if (!each()) {
                    return;
                }
            }
        }
    }
    void run(render::Ambient& a, std::uint64_t ms) {
        run(a, ms, [] { return true; });
    }
};

bool same(const render::FigureInstance& a, const render::FigureInstance& b) {
    return a.x == b.x && a.y == b.y && a.figure == b.figure && a.anim == b.anim && a.frame == b.frame &&
           a.direction == b.direction;
}
bool same(const render::EffectFigure& a, const render::EffectFigure& b) {
    return a.x == b.x && a.y == b.y && a.figure == b.figure && a.anim == b.anim && a.frame == b.frame &&
           a.direction == b.direction && a.sheet == b.sheet;
}
template <typename T>
bool same(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const T& x, const T& y) { return same(x, y); });
}

}  // namespace

TEST_CASE("ambient: townsfolk walk from their house along the road to a tavern and vanish") {
    Scene s;
    render::Ambient ambient(5);
    std::set<std::uint16_t> seen;
    bool walked = false;
    // Over many rounds a walker comes: the house list rate of the original is low.
    s.run(ambient, 40'000'000, [&] {
        for (const render::FigureInstance& f : ambient.walkers()) {
            walked = true;
            seen.insert(f.figure);
            // Drawn only on the road (row 10, between the house and the tavern).
            CHECK(f.y >= 110.0);
            CHECK(f.y <= 111.0);
            CHECK(f.x >= 103.0);
            CHECK(f.x <= 111.5);
            CHECK(f.sheet == assets::FigureSheet::Traeger);
            // At the height of its house tile: land, 17 px up at full zoom.
            CHECK(std::abs(f.z - render::kLandHeight) < 1e-9);
            // On the line through the tile centres of the road, no lane.
            CHECK(f.y == 110.5);
        }
        return !walked;
    });
    REQUIRE(walked);
    for (const std::uint16_t f : seen) {
        const auto choices = render::townsfolk_for(17);
        CHECK(std::find(choices.begin(), choices.end(), f) != choices.end());
    }
    // ... and vanishes at the tavern: 9 tiles at 0.4 tiles per second.
    s.run(ambient, 40'000);
    CHECK(ambient.walkers().size() <= 1);
}

TEST_CASE("ambient: townsfolk are drawn between two ticks by the share of the next one") {
    Scene s;
    render::Ambient ambient(5);
    s.run(ambient, 40'000'000, [&] { return ambient.walkers().empty(); });
    REQUIRE(!ambient.walkers().empty());
    s.run(ambient, 2'000);  // well on the road
    REQUIRE(!ambient.walkers().empty());
    const auto x_at = [&](double fraction) {
        s.view.tick_fraction = fraction;
        ambient.frame(s.world, s.buildings, s.figures, s.view);
        return ambient.walkers().front().x;
    };
    const double now = x_at(1.0);
    const double before = x_at(0.0);
    // 0.4 tiles per second: 0.02 tiles in a tick of 50 ms, on the way to the tavern in the east.
    CHECK(std::abs(now - before - 0.02) < 1e-3);
    CHECK(std::abs(x_at(0.5) - (now + before) / 2) < 1e-9);
}

TEST_CASE("ambient: no townsfolk in small cities or with foreign soldiers") {
    Scene s;
    s.city().inhabitants = {50, 29, 0, 0, 0};  // settlers and better: 29
    render::Ambient small(5);
    s.run(small, 6'000'000);
    CHECK(small.stats().walkers == 0);

    s.city().inhabitants = {10, 40, 0, 0, 0};
    std::vector<sim::Player> players(2);
    players[1].role = sim::PlayerRole::Computer;
    s.world.set_players(players);
    s.world.add_soldier(sim::make_soldier(105, 105, 1, 1));
    render::Ambient troops(5);
    s.run(troops, 6'000'000);
    CHECK(troops.stats().walkers == 0);
}

TEST_CASE("ambient: every computer sees the same, wherever it looks and however often it draws") {
    Scene s;
    s.world.add_ship(sim::make_ship("Sea Lion", 40, 40, 21, 0, 0));
    // One computer draws 10 times a second looking at the island, the other 1.4 times looking
    // elsewhere, and its player scrolls.
    render::Ambient a(77);
    render::Ambient b(77);
    render::Ambient other(78);
    render::AmbientView away;
    away.islands = {false};
    s.tick({&a, &b, &other});
    std::size_t compared = 0;
    std::size_t walkers = 0;
    std::size_t creatures = 0;
    bool differs = false;
    for (int i = 0; i < 400'000; ++i) {  // 20000 s
        s.tick({&a, &b, &other});
        const std::uint64_t now = s.world.time_ms();
        if (now % 100 == 0) {
            a.frame(s.world, s.buildings, s.figures, s.view);
        }
        if (now % 700 == 0) {
            b.frame(s.world, s.buildings, s.figures, i % 2 == 0 ? away : s.view);
        }
        if (now % 700 == 0 && now % 100 == 0) {
            other.frame(s.world, s.buildings, s.figures, s.view);
            REQUIRE(same(a.walkers(), b.walkers()));
            REQUIRE(same(a.effects(), b.effects()));
            differs = differs || !same(a.effects(), other.effects());
            walkers += a.walkers().size();
            creatures += a.effects().size();
            ++compared;
        }
    }
    CHECK(compared > 20'000);
    CHECK(walkers > 0);
    CHECK(creatures > 0);
    CHECK(differs);  // the seed matters
    CHECK(a.stats().walkers == b.stats().walkers);
    CHECK(a.stats().creatures == b.stats().creatures);
}

TEST_CASE("ambient: sea creatures come up all over the open sea, away from islands and ships") {
    Scene s;
    s.world.add_ship(sim::make_ship("Sea Lion", 40, 40, 21, 0, 0));
    render::Ambient ambient(9, render::Ambient::Config{render::Ambient::kSeaPeriodMs});  // the original's rate
    std::set<std::uint16_t> species;
    std::set<std::pair<int, int>> blocks;
    s.run(ambient, 400'000, [&] {  // 80 chances
        for (const render::EffectFigure& e : ambient.effects()) {
            if (e.sheet != assets::FigureSheet::Fische) {
                continue;
            }
            species.insert(e.figure);
            blocks.emplace(static_cast<int>(e.x) / 28, static_cast<int>(e.y) / 28);
            // In the world, not on the island (a pod spreads by a tile, whales swim away).
            CHECK(e.x > -6.0);
            CHECK(e.y > -6.0);
            CHECK(e.x < 174.0);
            CHECK(e.y < 146.0);
            if (e.figure != render::kFigureWhale) {
                CHECK(!(e.x > 101.5 && e.x < 128.5 && e.y > 101.5 && e.y < 118.5));
                // Nothing comes up within 5 tiles of a ship.
                CHECK(!(std::abs(e.x - 40.5) < 4.5 && std::abs(e.y - 40.5) < 4.5));
            }
        }
        return true;
    });
    CHECK(ambient.stats().creatures > 80);
    CHECK(blocks.size() > 20);  // all over the sea, not only where someone looks
    CHECK(species.count(render::kFigureDolphin) == 1);
    CHECK(species.count(render::kFigureWhale) == 1);
    CHECK(species.count(render::kFigureKraken) == 1);
}

TEST_CASE("ambient: the setting thins the sea creatures out, the same on every computer") {
    const auto count = [](std::uint32_t interval_ms, std::uint64_t seed) {
        Scene s;
        render::Ambient ambient(seed, render::Ambient::Config{interval_ms});
        s.run(ambient, 1'200'000);  // 20 minutes
        return ambient.stats().creatures;
    };
    const std::uint64_t original = count(render::Ambient::kSeaPeriodMs, 5);
    const std::uint64_t calm = count(60'000, 5);
    CHECK(original > 0);
    CHECK(calm > 0);
    CHECK(calm * 6 < original);  // about a twelfth
    CHECK(calm == count(60'000, 5));
    CHECK(count(0, 5) == 0);  // off
}

TEST_CASE("ambient: a juggler on the market of a city with more than 59 citizens") {
    Scene s;
    sim::Production market;
    market.x = 20;
    market.y = 2;
    s.world.set_production(market);
    s.city().inhabitants = {0, 0, 59, 0, 0};
    render::Ambient ambient(1);
    const auto jugglers = [&] {
        std::vector<render::EffectFigure> j;
        for (const render::EffectFigure& e : ambient.effects()) {
            if (e.sheet == assets::FigureSheet::Gaukler) {
                j.push_back(e);
            }
        }
        return j;
    };
    s.run(ambient, 2000);
    CHECK(jugglers().empty());
    s.city().inhabitants = {0, 0, 30, 30, 0};
    s.run(ambient, 1000);  // markets are looked at once a second
    const std::vector<render::EffectFigure> all = jugglers();
    REQUIRE(all.size() == 1);
    const render::EffectFigure& j = all.front();
    CHECK(j.figure == render::kFigureJuggler);
    CHECK(j.anim == (20 + 2) % 5);       // the act follows the market's position
    CHECK(j.x == 100 + 20 + 2.0 + 2.1);  // centre of the 4 x 3 market and Fahnoffs 2.1
    CHECK(j.y == 100 + 2 + 1.5);
    CHECK(j.z == 0.6);
    CHECK(j.ground == 0.6);  // the shadow under his feet, at his height
    CHECK(j.direction == 2);
}

TEST_CASE("ambient: townsfolk and jugglers only on islands the local player has discovered") {
    Scene s;
    sim::Production market;
    market.x = 20;
    market.y = 2;
    s.world.set_production(market);
    s.city().inhabitants = {0, 0, 30, 30, 0};
    s.view.local_player = 0;
    render::Ambient ambient(1);
    const auto jugglers = [&] {
        return std::count_if(ambient.effects().begin(), ambient.effects().end(),
                             [](const render::EffectFigure& e) { return e.sheet == assets::FigureSheet::Gaukler; });
    };
    // Undiscovered: nothing in the picture, although the scenery goes on as on every computer.
    bool walked = false;
    s.run(ambient, 40'000'000, [&] {
        walked = walked || !ambient.walkers().empty();
        return !walked && jugglers() == 0;
    });
    CHECK(!walked);
    CHECK(jugglers() == 0);
    CHECK(ambient.stats().walkers > 0);
    // Discovered: the same scenery shows.
    s.world.island(0).discover(0);
    s.run(ambient, 40'000'000, [&] { return ambient.walkers().empty(); });
    CHECK(!ambient.walkers().empty());
    CHECK(jugglers() == 1);
    // Without a local player (no human at this computer) every island shows.
    s.world.island(0).discovered.clear();
    s.view.local_player = sim::kNoPlayer;
    s.run(ambient, 1000);
    CHECK(jugglers() == 1);
}
