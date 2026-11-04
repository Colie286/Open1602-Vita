// SPDX-License-Identifier: GPL-3.0-or-later
// The computer players on a small synthetic world, without the original data: they
// found a city with their ship through the same orders a player gives, run the same on every
// computer, keep their state through a savegame and ignore the players that are not theirs.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "../../engine/sim/src/ai/ai_archive.hpp"
#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/assets/goods.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/sim/ai.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/computer_orders.hpp"
#include "o1602/sim/computer_slots.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kSea = 21201;
constexpr std::uint32_t kBeach = 20501;
constexpr std::uint32_t kTree = 20202;
constexpr std::uint32_t kKontor = 22101;  // the Id the computer players build as their first Kontor
constexpr std::uint16_t kShipFigure = 21;

constexpr sim::PlayerId kHuman = 0;
constexpr sim::PlayerId kComputer = 1;
constexpr sim::PlayerId kTrader = 2;
constexpr sim::PlayerId kNatives = 3;

/// `extra` are further HAUS records, numbered on from the Kontor.
assets::BuildingTable make_buildings(const std::string& extra = {}) {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        std::string("Objekt: HAUS\n") +
            "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"                   // ground
            "  @Nummer: 1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"                   // sea
            "  @Nummer: 2\n  Id: 20501\n  Kind: 23\n  Gfx: 2\n  Size: 1, 1\n  Destroyflg: 1\n"  // beach
            "  @Nummer: 3\n  Id: 20202\n  Kind: 10\n  Gfx: 3\n  Size: 1, 1\n"                   // a tree
            "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
            // Roads: stone and dirt, each a family of straight, curve, T and cross.
            "  @Nummer: 4\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Size: 1, 1\n  KreuzBase: 4\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
            "  @Nummer: 5\n  Id: 20402\n  Kind: 1\n  Gfx: 5\n  Size: 1, 1\n  KreuzBase: 4\n"
            "  @Nummer: 6\n  Id: 20403\n  Kind: 1\n  Gfx: 6\n  Size: 1, 1\n  KreuzBase: 4\n"
            "  @Nummer: 7\n  Id: 20404\n  Kind: 1\n  Gfx: 7\n  Size: 1, 1\n  KreuzBase: 4\n"
            "  @Nummer: 8\n  Id: 20411\n  Kind: 1\n  Gfx: 8\n  Size: 1, 1\n  KreuzBase: 8\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 2\n  EndObj\n"
            "  @Nummer: 9\n  Id: 20412\n  Kind: 1\n  Gfx: 9\n  Size: 1, 1\n  KreuzBase: 8\n"
            "  @Nummer: 10\n  Id: 20413\n  Kind: 1\n  Gfx: 10\n  Size: 1, 1\n  KreuzBase: 8\n"
            "  @Nummer: 11\n  Id: 20414\n  Kind: 1\n  Gfx: 11\n  Size: 1, 1\n  KreuzBase: 8\n"
            // Kontor I at the beach.
            "  @Nummer: 12\n  Id: 22101\n  Kind: 35\n  Gfx: 20\n  Size: 2, 3\n  Strandflg: 1\n  PlaceFlg: 1\n"
            "  KreuzBase: 12\n  Ausbauflg: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n    Maxlager: 30\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n" +
            extra + "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
        "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of 90 x 90 tiles of sea with one island of 50 x 50 tiles at (30, 20): open ground, a
/// wood (columns 32..43), a beach facing east (column 46) and sea (columns 47..49). Every crop grows
/// there fully. Player 0 is human, 1 a computer with a ship east of the island, 2 the trader, 3 the
/// natives. With `lake`, a lake inside the island (columns 26..30, rows 18..32) with a beach facing
/// it (column 25) and a second wood (columns 2..12): the lake has more green around it than the
/// coast, but ships cannot reach it from the sea.
struct Game {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{90, 90};

    explicit Game(bool computer_ship = true, std::int32_t computer_money = 10000, bool lake = false) {
        add_islet(2, 2);
        sim::IslandMap map(50, 50);
        for (std::int32_t y = 0; y < 50; ++y) {
            for (std::int32_t x = 0; x < 50; ++x) {
                std::uint32_t id = kGround;
                const bool lake_row = lake && y >= 18 && y <= 32;
                if (lake_row && x >= 26 && x <= 30) {
                    id = kSea;
                } else if (lake_row && x == 25) {
                    id = kBeach;
                } else if (x > 46) {
                    id = kSea;
                } else if (x == 46) {
                    id = kBeach;
                } else if ((x >= 32 && x <= 43) || (lake && x >= 2 && x <= 12)) {
                    id = kTree;
                }
                REQUIRE(map.place(*buildings.by_id(id), x, y, id == kBeach ? 3 : 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), 30, 20};
        island.header.number = static_cast<std::uint8_t>(world.islands().size());
        island.header.width = 50;
        island.header.height = 50;
        island.header.fertility = 0xFFFFFFFFU;
        REQUIRE(world.add_island(std::move(island)).ok());

        std::vector<sim::Player> players(4);
        players[kHuman].role = sim::PlayerRole::Human;
        players[kHuman].money = 10000;
        players[kComputer].role = sim::PlayerRole::Computer;
        players[kComputer].money = computer_money;
        sim::make_neutral(players[kTrader], sim::faction::kTrader);
        sim::make_neutral(players[kNatives], sim::faction::kNatives);
        world.set_players(players);
        if (computer_ship) {
            add_ship(kComputer, 86, 45);
        }
    }

    /// A rock of 5 x 5 tiles of ground, too small for a colony.
    void add_islet(std::uint32_t x, std::uint32_t y) {
        sim::IslandMap map(5, 5);
        for (std::int32_t ty = 0; ty < 5; ++ty) {
            for (std::int32_t tx = 0; tx < 5; ++tx) {
                REQUIRE(map.place(*buildings.by_id(kGround), tx, ty, 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland islet{assets::IslandHeader{}, std::move(map), x, y};
        islet.header.number = static_cast<std::uint8_t>(world.islands().size());
        islet.header.width = 5;
        islet.header.height = 5;
        REQUIRE(world.add_island(std::move(islet)).ok());
    }

    sim::Ship& add_ship(sim::PlayerId owner, std::uint32_t x, std::uint32_t y) {
        sim::Ship s = sim::make_ship("Ship", x, y, kShipFigure, owner, 2);
        s.cargo.resize(4);
        world.add_ship(s);
        return world.ships().back();
    }

    [[nodiscard]] std::size_t cities_of(sim::PlayerId owner) const {
        return static_cast<std::size_t>(std::count_if(world.cities().begin(), world.cities().end(),
                                                      [&](const sim::City& c) { return c.owner == owner; }));
    }
};

/// Ticks until `done` holds or `ms` of game time have passed; whether `done` held.
template <class F>
bool run_until(sim::Simulation& s, std::uint32_t ms, F&& done) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
        if (done()) {
            return true;
        }
    }
    return false;
}

void run(sim::Simulation& s, std::uint32_t ms) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
    }
}

bool same(const std::vector<sim::ComputerPlayers::View>& a, const std::vector<sim::ComputerPlayers::View>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.player != y.player || x.cities != y.cities || x.max_stage != y.max_stage || x.score != y.score ||
            x.settling != y.settling || x.settle_failed != y.settle_failed || x.stack != y.stack ||
            x.plans.size() != y.plans.size()) {
            return false;
        }
        for (std::size_t k = 0; k < x.plans.size(); ++k) {
            const auto& p = x.plans[k];
            const auto& q = y.plans[k];
            if (p.island != q.island || p.city != q.city || p.stage != q.stage || p.flags != q.flags ||
                p.slots != q.slots) {
                return false;
            }
        }
    }
    return true;
}

/// A pirate slot behind the natives (slot 4).
void add_pirates(Game& g) {
    std::vector<sim::Player> players = g.world.players();
    players.push_back(sim::Player{});
    sim::make_neutral(players.back(), sim::faction::kPirates);
    g.world.set_players(players);
}

/// Runs until the computer has founded its city, then `ms` more.
void found_city(Game& g, sim::Simulation& s, std::uint32_t ms = 1'000) {
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    run(s, ms);
}

bool remembers(const sim::World& world, sim::PlayerId who, sim::DiplomacyEventType type, sim::PlayerId partner) {
    const auto& events = world.diplomacy().events(who);
    return std::any_of(events.begin(), events.end(),
                       [&](const sim::DiplomacyEvent& e) { return e.type == type && e.partner == partner; });
}

std::vector<std::uint8_t> saved(const sim::ComputerPlayers& ai) {
    core::ByteWriter out;
    ai.save(out);
    return out.take();
}

}  // namespace

TEST_CASE("ai: a computer with a ship founds a city through a build order") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    const std::uint32_t ship = g.world.ships().front().id;
    bool directed = false;
    const bool founded = run_until(s, 600'000, [&] {
        directed = directed || s.computers().directs(ship);
        return g.cities_of(kComputer) != 0;
    });
    REQUIRE(founded);
    CHECK(directed);  // the ship sailed under the computer's command
    CHECK(!s.computers().directs(ship));
    const sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = &c;
        }
    }
    REQUIRE(city != nullptr);
    CHECK(city->island == 1);
    REQUIRE(city->warehouse_x.has_value());
    REQUIRE(city->warehouse_y.has_value());
    // A Kontor at the coast, its warehouse.
    const sim::MapCell& at = g.world.islands()[1].map.top(*city->warehouse_x, *city->warehouse_y);
    const assets::BuildingType* kontor = g.buildings.by_sprite(at.sprite);
    REQUIRE(kontor != nullptr);
    CHECK(kontor->id == kKontor);
    CHECK(at.owner == kComputer);
    // Built like a player's Kontor: the game reports the new city.
    bool reported = false;
    for (const auto& e : s.take_construction_events()) {
        reported = reported || (e.kind == sim::ConstructionEvent::Kind::CityFounded && e.island == 1);
    }
    CHECK(reported);
    const auto view = s.computers().view();
    REQUIRE(view.size() == 1);
    CHECK(view[0].player == kComputer);
    CHECK(view[0].cities == 1);
    CHECK(!view[0].settling);
}

TEST_CASE("ai: a ship whose goods do not fit into the full home warehouse does not lie there for good") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 30'000);
    sim::City* home = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            home = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(home != nullptr);
    sim::Ship& ship = g.world.ships().front();
    REQUIRE(ship.owner == kComputer);
    // At the Kontor: on the first sea tile east of it (the island lies at 30, 20; sea from x 47).
    const sim::WorldIsland& isl = g.world.islands()[home->island];
    ship.place(isl.x + 48, isl.y + *home->warehouse_y);
    REQUIRE(sim::building_in_reach(g.world, g.buildings, home->island, *home->warehouse_x, *home->warehouse_y, ship.x,
                                   ship.y));
    // Nothing else to do but this: a hold of a ware the warehouse is full of (S-097: it stays on board).
    const std::uint8_t ware = g.buildings.materials().tools;
    home->ensure_ware(ware);
    home->goods[ware] = home->capacity;
    ship.route = sim::TradeRoute{};
    ship.target = sim::Target{};
    for (sim::CargoSlot& slot : ship.cargo) {
        slot = sim::CargoSlot{};
    }
    ship.cargo[0] = sim::CargoSlot{ware, 10 * 32, 0};
    const std::uint32_t id = ship.id;
    // Every 10 s the computer looks at its ships; before, it tried to unload on every look.
    const bool sails = run_until(s, 60'000, [&] {
        const sim::Ship* now = g.world.ship(id);
        return now != nullptr && (now->route.active || !now->target.empty());
    });
    CHECK(sails);
    CHECK(g.world.ship(id)->load(ware) == 10 * 32);  // nothing thrown away
}

TEST_CASE("ai: a computer's ship under the white flag gets its next job, which strikes the flag") {
    // A ship a pirate plundered keeps its white flag; the original's pickers do not look at the flag
    // (the original tests the wrong bit), so the next job sails it on.
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 30'000);
    sim::City* home = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            home = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(home != nullptr);
    sim::Ship& ship = g.world.ships().front();
    REQUIRE(ship.owner == kComputer);
    const sim::WorldIsland& isl = g.world.islands()[home->island];
    ship.place(isl.x + 48, isl.y + *home->warehouse_y);
    const std::uint8_t ware = g.buildings.materials().tools;
    home->ensure_ware(ware);
    home->goods[ware] = home->capacity;
    ship.route = sim::TradeRoute{};
    ship.target = sim::Target{};
    for (sim::CargoSlot& slot : ship.cargo) {
        slot = sim::CargoSlot{};
    }
    ship.cargo[0] = sim::CargoSlot{ware, 10 * 32, 0};
    ship.held = true;
    const std::uint32_t id = ship.id;
    const bool sails = run_until(s, 60'000, [&] {
        const sim::Ship* now = g.world.ship(id);
        return now != nullptr && (now->route.active || !now->target.empty());
    });
    CHECK(sails);
    CHECK(!g.world.ship(id)->held);
}

TEST_CASE("ai: the original's computer players (Rules::bugs.classic_ai) found their city too") {
    Game g;
    sim::Rules rules;
    rules.bugs.classic_ai = true;
    sim::Simulation s(g.world, g.buildings, g.figures, rules);
    CHECK(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
}

TEST_CASE("ai: computer opponents switched off found nothing, by the rule or by the game's settings") {
    {
        Game g;
        sim::Rules rules;
        rules.computer_players = sim::Computers::Off;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        CHECK(!run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    }
    {
        // Bit 1 of the settings word (TIMERS +0xA8): the original skips its AI tick.
        Game g;
        sim::World::GameSettings settings = g.world.game_settings();
        settings.flags |= sim::World::GameSettings::kNoComputers;
        g.world.set_game_settings(settings);
        sim::Simulation s(g.world, g.buildings, g.figures);
        CHECK(!run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    }
    {
        // Switched on, they play whatever the game says.
        Game g;
        sim::World::GameSettings settings = g.world.game_settings();
        settings.flags |= sim::World::GameSettings::kNoComputers;
        g.world.set_game_settings(settings);
        sim::Rules rules;
        rules.computer_players = sim::Computers::On;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        CHECK(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    }
}

TEST_CASE("ai: a computer founds its city at the open sea, not at a lake its ship cannot reach") {
    Game g(true, 10000, true);
    sim::Simulation s(g.world, g.buildings, g.figures);
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    const sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = &c;
        }
    }
    REQUIRE(city != nullptr);
    REQUIRE(city->warehouse_x.has_value());
    CHECK(*city->warehouse_x >= 40);  // at the beach of column 46, not at the lake (column 25)
}

TEST_CASE("ai: a person who takes a seat over finds its ships standing") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    const std::uint32_t id = g.world.ships().front().id;
    // The computer sends its ship off to found a city; a person takes the seat over on the way.
    REQUIRE(run_until(s, 600'000, [&] { return !g.world.ship(id)->target.empty(); }));
    s.submit(sim::PlayerControl{kComputer, false});
    run(s, 1'000);
    const sim::Ship* ship = g.world.ship(id);
    REQUIRE(ship != nullptr);
    CHECK(ship->target.empty());
    const std::int32_t x = ship->x;
    const std::int32_t y = ship->y;
    run(s, 60'000);
    CHECK(g.world.ship(id)->x == x);
    CHECK(g.world.ship(id)->y == y);
    CHECK(g.cities_of(kComputer) == 0);
}

TEST_CASE("ai: the new city gets roads from the computer's own build orders") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    const std::int32_t after_founding = g.world.players()[kComputer].money;
    run(s, 60'000);
    std::size_t roads = 0;
    const sim::IslandMap& map = g.world.islands()[1].map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const sim::MapCell& c = map.top(x, y);
            const assets::BuildingType* b = c.empty() ? nullptr : g.buildings.by_sprite(c.sprite);
            roads += b != nullptr && b->kind == 1 && c.owner == kComputer ? 1U : 0U;
        }
    }
    CHECK(roads > 0);
    // Paid like any player's buildings, and never into debt.
    CHECK(g.world.players()[kComputer].money < after_founding);
    CHECK(g.world.players()[kComputer].money > 0);
}

TEST_CASE("ai: two runs of the same game stay the same, tick by tick") {
    Game a;
    Game b;
    sim::Simulation sa(a.world, a.buildings, a.figures);
    sim::Simulation sb(b.world, b.buildings, b.figures);
    // Through the voyage, the founding and the first roads.
    for (int step = 0; step < 12; ++step) {
        run(sa, 5'000);
        run(sb, 5'000);
        const auto ha = sa.state_hash();
        CHECK(ha == sb.state_hash());
        CHECK(same(sa.computers().view(), sb.computers().view()));
        CHECK(saved(sa.computers()) == saved(sb.computers()));
    }
    CHECK(a.cities_of(kComputer) == 1);
}

// Lockstep needs the same game on every machine (ADR 0004). This hash is checked on every build of
// CI (GCC, Clang, MSVC, Apple Clang), so a difference between compilers or standard libraries shows
// here. A change that alters the game on purpose updates the number, and says why in its PR.
TEST_CASE("ai: a computer's first minutes give the same state on every platform") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 90'000);
    const std::uint64_t hash = s.state_hash();
    // Names left empty without Text.cod (MD-10); SIMU in parts (DS-3); tiles by blocks (TB-26); a ship
    // whose goods do not fit at home is free again; the computers' state version 4 (a search's target);
    // the revised bread chain by default; the computers' state version 5 (the slots counted).
    // The wider hunter's hut by default.
    constexpr std::uint64_t kExpected = 0x6e90c0ea8852cc84ULL;
    if (hash != kExpected) {
        std::printf("  state hash after 90 s: 0x%016llx\n", static_cast<unsigned long long>(hash));
    }
    CHECK(hash == kExpected);
    CHECK(g.cities_of(kComputer) == 1);
}

TEST_CASE("ai: the fingerprint with the blocks' hashes kept is the one taken anew while the computer builds (TB-26)") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    for (int k = 0; k < 18; ++k) {
        run(s, 5'000);
        CHECK(s.state_hash() == s.state_hash_anew());
    }
    CHECK(g.cities_of(kComputer) == 1);
    // A tile changed behind the game's back changes the fingerprint at once.
    const std::uint64_t hash = s.state_hash();
    sim::IslandMap& map = g.world.island(0).map;
    map.set_anim_phase(0, map.height() - 1, static_cast<std::uint8_t>(map.top(0, map.height() - 1).anim_phase ^ 1U));
    CHECK(s.state_hash() != hash);
    CHECK(s.state_hash() == s.state_hash_anew());
}

TEST_CASE("ai: a game saved while the computer is under way goes on as if never saved") {
    // Saved before, during and after the voyage of the founding ship.
    bool under_way = false;
    for (const std::uint32_t at : {4'000U, 8'000U, 15'000U, 40'000U}) {
        Game a;
        sim::Simulation sa(a.world, a.buildings, a.figures);
        run(sa, at);
        const std::uint32_t ship = a.world.ships().front().id;
        under_way = under_way || (sa.computers().view().front().settling && sa.computers().directs(ship));
        const auto bytes = sim::write_game(a.world, a.buildings, &sa, sim::SaveOptions{false});
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, a.buildings, nullptr, &a.figures);
        REQUIRE(loaded.ok());
        sim::World world = std::move(loaded).value();
        sim::Simulation sb(world, a.buildings, a.figures);
        CHECK(!sb.state_damaged());
        CHECK(same(sa.computers().view(), sb.computers().view()));
        CHECK(saved(sa.computers()) == saved(sb.computers()));
        CHECK(sa.state_hash() == sb.state_hash());
        CHECK(sa.computers().directs(ship) == sb.computers().directs(ship));
        run(sa, 30'000);
        run(sb, 30'000);
        CHECK(same(sa.computers().view(), sb.computers().view()));
        CHECK(sa.state_hash() == sb.state_hash());
        CHECK(a.cities_of(kComputer) == 1);
    }
    CHECK(under_way);
}

TEST_CASE("ai: a saved state with values out of range is refused, not run") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 8'000);
    const std::vector<std::uint8_t> bytes = saved(s.computers());
    sim::ai::State good;
    {
        core::ByteReader in(bytes);
        sim::StateIn reader(in);
        sim::ai::Loader loader(reader, s.computers().state_version());
        loader(good);
        REQUIRE(reader.ok());
        REQUIRE(!good.players.empty());
    }
    const auto loads = [&](sim::ai::State state) {
        core::ByteWriter out;
        sim::ai::Saver saver(out);
        saver(state);
        const std::vector<std::uint8_t> data = out.take();
        core::ByteReader in(data);
        sim::ComputerPlayers computers(s);
        return computers.load(in, sim::ComputerPlayers::max_state_version());
    };
    CHECK(loads(good));
    sim::ai::State bad = good;
    bad.players[0].search.segment = -2'000'000'000;  // crashed the first step after loading
    CHECK(!loads(bad));
    bad = good;
    bad.players[0].search.slot = 0xFF;
    CHECK(!loads(bad));
    bad = good;
    bad.players.push_back(bad.players[0]);  // one computer acting twice a tick
    CHECK(!loads(bad));
    bad = good;
    bad.players[0].player = 200;
    CHECK(!loads(bad));
    bad = good;
    bad.players[0].stack = {sim::ai::kDevelop, sim::ai::kDevelop};
    CHECK(!loads(bad));
    bad = good;
    bad.players[0].stack = {0xEE};
    CHECK(!loads(bad));
}

TEST_CASE("ai: a state of version 1 loads and goes on as the same state") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 8'000);
    const std::vector<std::uint8_t> now = saved(s.computers());
    sim::ai::State state;
    {
        core::ByteReader in(now);
        sim::StateIn reader(in);
        sim::ai::Loader loader(reader, s.computers().state_version());
        loader(state);
        REQUIRE(reader.ok());
    }
    // As version 1 wrote it: with an (empty) list of attack plans and a state byte per island.
    core::ByteWriter old;
    sim::ai::Saver saver(old, 1);
    saver(state);
    const std::vector<std::uint8_t> v1 = old.take();
    CHECK(v1 != now);
    core::ByteReader in(v1);
    sim::ComputerPlayers computers(s);
    REQUIRE(computers.load(in, 1));
    CHECK(saved(computers) == now);
}

TEST_CASE("ai: a state of version 2 loads without a yard request") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 8'000);
    sim::ai::State state;
    {
        const std::vector<std::uint8_t> now = saved(s.computers());
        core::ByteReader in(now);
        sim::StateIn reader(in);
        sim::ai::Loader loader(reader, s.computers().state_version());
        loader(state);
        REQUIRE(reader.ok());
    }
    REQUIRE(!state.players.empty());
    for (sim::ai::AiPlayer& p : state.players) {
        p.yard_wanted = true;  // version 2 has no room for it
    }
    core::ByteWriter old;
    sim::ai::Saver saver(old, 2);
    saver(state);
    const std::vector<std::uint8_t> v2 = old.take();
    core::ByteReader in(v2);
    sim::ComputerPlayers computers(s);
    REQUIRE(computers.load(in, 2));
    // Saved again as the newest version: one byte more per computer, and no request (version 3),
    // the search's target (version 4) and the number of slots of each computer and city (version 5).
    const std::vector<std::uint8_t> again = saved(computers);
    std::size_t plans = 0;
    for (const sim::ai::AiPlayer& p : state.players) {
        plans += p.plans.size();
    }
    CHECK(again.size() == v2.size() + state.players.size() * (1 + 2 * sizeof(std::int32_t) + sizeof(std::uint32_t)) +
                              plans * sizeof(std::uint32_t));
    core::ByteReader back(again);
    sim::StateIn reader(back);
    sim::ai::State loaded;
    sim::ai::Loader loader(reader, computers.state_version());
    loader(loaded);
    REQUIRE(reader.ok());
    for (const sim::ai::AiPlayer& p : loaded.players) {
        CHECK(!p.yard_wanted);
    }
}

TEST_CASE("ai: land freed on its island lets a computer's city search its sites anew, not the original's") {
    for (const bool classic : {false, true}) {
        Game g;
        sim::Rules rules;
        rules.bugs.classic_ai = classic;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s);
        // Every search of the city given up.
        sim::ai::State state;
        {
            const std::vector<std::uint8_t> now = saved(s.computers());
            core::ByteReader in(now);
            sim::StateIn reader(in);
            sim::ai::Loader loader(reader, s.computers().state_version());
            loader(state);
            REQUIRE(reader.ok());
        }
        std::optional<std::uint16_t> island;
        for (sim::ai::AiPlayer& p : state.players) {
            for (sim::ai::CityPlan& r : p.plans) {
                island = r.island;
                r.flags |= sim::ai::city_flag::kMarket;
                for (sim::ai::SlotState& slot : r.slots) {
                    slot.attempts = 2;
                    slot.fail_level = 2;
                }
            }
        }
        REQUIRE(island.has_value());
        core::ByteWriter out;
        sim::ai::Saver saver(out);
        saver(state);
        const std::vector<std::uint8_t> bytes = out.take();
        core::ByteReader in(bytes);
        sim::ComputerPlayers computers(s);
        REQUIRE(computers.load(in, sim::ComputerPlayers::max_state_version()));
        const auto given_up = [&](bool expected) {
            for (const auto& v : computers.view()) {
                for (const auto& c : v.plans) {
                    CHECK(((c.flags & sim::ai::city_flag::kMarket) != 0) == expected);
                    for (const auto& slot : c.slots) {
                        CHECK((slot[1] == 2 && slot[2] == 2) == expected);
                    }
                }
            }
        };
        computers.land_freed(static_cast<std::uint16_t>(*island + 1));  // another island
        given_up(true);
        computers.land_freed(*island);
        given_up(classic);  // the original waits for its hourly new start
    }
}

TEST_CASE("ai: the names for new cities and ships travel with the saved game") {
    Game a;
    sim::Simulation sa(a.world, a.buildings, a.figures);
    sa.offer_names({"Neustadt", "Altdorf"}, {"Seeadler"});
    sa.offer_names({"Newtown"}, {"Sea eagle"});  // the game has names already
    const std::vector<std::string> german{"Neustadt", "Altdorf"};
    CHECK(sa.city_names() == german);
    run(sa, 40'000);
    const auto bytes = sim::write_game(a.world, a.buildings, &sa, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, a.buildings, nullptr, &a.figures);
    REQUIRE(loaded.ok());
    sim::World world = std::move(loaded).value();
    sim::Simulation sb(world, a.buildings, a.figures);
    CHECK(!sb.state_damaged());
    // An installation in another language keeps the game's names, and the computers stay alike.
    sb.offer_names({"Newtown"}, {"Sea eagle"});
    CHECK(sb.city_names() == sa.city_names());
    CHECK(sa.state_hash() == sb.state_hash());
    CHECK(a.cities_of(kComputer) == 1);
    CHECK(a.world.cities().front().name == "Neustadt");
}

TEST_CASE("ai: the players' ships without a name get names of their own from the list") {
    // The start ships of the players five on are copies of a level's ship and come without a name.
    Game a;
    a.add_ship(kComputer, 80, 60).name.clear();
    a.add_ship(kHuman, 82, 60).name.clear();
    sim::Ship trader = sim::make_ship("", 84, 60, kShipFigure, kTrader, 2);
    a.world.add_ship(trader);
    sim::Simulation sa(a.world, a.buildings, a.figures);
    sa.offer_names({"Neustadt"}, {"Seeadler", "Gull", "Albatros"});
    const auto& ships = a.world.ships();
    CHECK(ships[0].name == "Ship");  // a named ship keeps its name
    REQUIRE(!ships[1].name.empty());
    REQUIRE(!ships[2].name.empty());
    CHECK(ships[1].name != ships[2].name);
    CHECK(ships[3].name.empty());  // only the players' ships
}

TEST_CASE("ai: the state of the computers goes to bytes and back whole") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 20'000);
    const auto bytes = saved(s.computers());
    sim::ComputerPlayers copy(s);
    core::ByteReader in(bytes);
    REQUIRE(copy.load(in, s.computers().state_version()));
    CHECK(in.at_end());
    CHECK(same(copy.view(), s.computers().view()));
    CHECK(saved(copy) == bytes);
}

TEST_CASE("ai: damaged state is refused and leaves the computers as they were") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 20'000);
    const auto bytes = saved(s.computers());
    REQUIRE(bytes.size() > 4);

    // A copy that knows a different moment: the state before the founding.
    Game h;
    sim::Simulation t(h.world, h.buildings, h.figures);
    run(t, 2'000);
    sim::ComputerPlayers copy(s);
    {
        const auto earlier = saved(t.computers());
        core::ByteReader in(earlier);
        REQUIRE(copy.load(in, t.computers().state_version()));
    }
    const auto before = saved(copy);
    REQUIRE(before != bytes);

    // Every truncation of the good state.
    std::size_t accepted = 0;
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        core::ByteReader in(std::span(bytes.data(), n));
        accepted += copy.load(in, s.computers().state_version()) ? 1U : 0U;
    }
    CHECK(accepted == 0);
    CHECK(saved(copy) == before);

    // An unknown version, and garbage after the right one.
    {
        core::ByteReader in(bytes);
        CHECK(!copy.load(in, 0x63));
    }
    for (const std::uint8_t fill : {std::uint8_t{0xFF}, std::uint8_t{0x7F}, std::uint8_t{0x01}}) {
        std::vector<std::uint8_t> garbage(4096, fill);
        std::copy_n(bytes.begin(), 4, garbage.begin());
        core::ByteReader in(garbage);
        CHECK(!copy.load(in, s.computers().state_version()));
    }
    CHECK(saved(copy) == before);
}

TEST_CASE("ai: reset takes the computers from the world, and only them") {
    Game g;
    // A pirate slot too: the pirates are run by the same machinery.
    std::vector<sim::Player> players = g.world.players();
    players.push_back(sim::Player{});
    sim::make_neutral(players.back(), sim::faction::kPirates);
    g.world.set_players(players);
    sim::Simulation s(g.world, g.buildings, g.figures);
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    run(s, 5'000);

    sim::ComputerPlayers fresh(s);
    fresh.reset();
    const auto view = fresh.view();
    REQUIRE(view.size() == 2);
    CHECK(view[0].player == kComputer);
    CHECK(view[1].player == 4);
    for (const auto& v : view) {
        CHECK(v.player != kHuman);
        CHECK(v.player != kTrader);
        CHECK(v.player != kNatives);
    }
    // The computer's city is known again, nothing is half done.
    CHECK(view[0].cities == 1);
    REQUIRE(view[0].plans.size() == 1);
    CHECK(view[0].plans[0].island == 1);
    CHECK(!view[0].settling);
    CHECK(view[0].stack.empty());
    CHECK(view[1].cities == 0);
}

TEST_CASE("ai: humans, the trader and the natives are left alone") {
    Game g(false);
    // The computer's slot is played by a human now; a human with a ship and money too.
    g.world.player(kComputer)->role = sim::PlayerRole::Human;
    sim::Ship& ship = g.add_ship(kHuman, 86, 45);
    const std::uint32_t id = ship.id;
    const std::uint32_t x = ship.x;
    const std::uint32_t y = ship.y;
    const std::vector<sim::Player> before = g.world.players();
    sim::Simulation s(g.world, g.buildings, g.figures);
    CHECK(s.computers().view().empty());
    run(s, 120'000);
    CHECK(s.computers().view().empty());
    CHECK(g.world.cities().empty());
    CHECK(!s.computers().directs(id));
    const sim::Ship* after = g.world.ship(id);
    REQUIRE(after != nullptr);
    CHECK(after->x == x);
    CHECK(after->y == y);
    for (std::size_t p = 0; p < before.size(); ++p) {
        CHECK(g.world.players()[p].money == before[p].money);
    }
}

TEST_CASE("ai: a seat the computer did not hold before is taken on when it becomes the computer's") {
    // A game saved after the host left gives the other people's seats to the computer.
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 5'000);
    REQUIRE(s.computers().view().size() == 1);
    g.world.player(kHuman)->role = sim::PlayerRole::Computer;
    run(s, 1'000);
    const auto view = s.computers().view();
    REQUIRE(view.size() == 2);
    CHECK(view[0].player == kComputer);
    CHECK(view[1].player == kHuman);
}

TEST_CASE("ai: a computer without a ship founds nothing and harms nobody") {
    Game g(false);
    sim::Simulation s(g.world, g.buildings, g.figures);
    const std::int32_t money = g.world.players()[kComputer].money;
    run(s, 60'000);
    CHECK(g.world.cities().empty());
    CHECK(g.world.ships().empty());
    CHECK(g.world.players()[kComputer].money == money);
    CHECK(g.world.players()[kHuman].money == 10000);
    const auto view = s.computers().view();
    REQUIRE(view.size() == 1);
    CHECK(view[0].cities == 0);
    CHECK(!view[0].settling);
    CHECK(!s.computers().directs(0));
    CHECK(!s.computers().directs(12345));
}

TEST_CASE("ai: a computer a scenario keeps quiet stays so until a human breaks the peace") {
    Game g;
    g.add_ship(kHuman, 86, 10);  // a human still in the game
    g.world.player(kComputer)->scenario.flags = sim::scenario_flag::kPassive | sim::scenario_flag::kNoTrader;
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    run(s, 120'000);
    CHECK(g.world.players()[kComputer].scenario.has(sim::scenario_flag::kPassive));
    // War with the human ends the switch for good; the others stay.
    auto& d = g.world.diplomacy();
    REQUIRE(d.set_peace(g.world, kHuman, kComputer, sim::Treaty::None, g.world.time_ms()));
    CHECK(run_until(s, 600'000,
                    [&] { return !g.world.players()[kComputer].scenario.has(sim::scenario_flag::kPassive); }));
    CHECK(g.world.players()[kComputer].scenario.flags == sim::scenario_flag::kNoTrader);
}

TEST_CASE("ai: a computer the scenario forbids colonies founds its first city, wants no second") {
    Game g;
    g.world.player(kComputer)->scenario.flags = sim::scenario_flag::kNoColonies;
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    run(s, 600'000);
    CHECK(g.cities_of(kComputer) == 1);
    const auto view = s.computers().view();
    REQUIRE(view.size() == 1);
    CHECK(!view[0].settling);
}

TEST_CASE("ai: a computer without money founds its city all the same") {
    // The original gives a computer short of money its founding money (task 4).
    Game g(true, 0);
    sim::Simulation s(g.world, g.buildings, g.figures);
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    run(s, 30'000);
    CHECK(g.world.players()[kComputer].money >= 0);
    CHECK(g.world.players()[kHuman].money == 10000);
}

TEST_CASE("ai: a city founded next to a young colony is remembered against the founder") {
    struct Case {
        sim::PlayerId founder = kHuman;
        std::uint16_t island = 0;  ///< where the new city lies
        bool colony = true;        ///< the computer has a colony on the islet
        std::uint32_t people = 0;  ///< its inhabitants (two classes)
        bool war = false;
    };
    // The computer's home city on the big island (island 1), a colony on the islet (island 0); then
    // somebody founds a city.
    const auto remembered = [](const Case& k) {
        Game g(false);
        const auto add_city = [&](std::uint16_t island, std::uint8_t index, sim::PlayerId owner, std::uint32_t people) {
            sim::City c;
            c.island = island;
            c.index = index;
            c.owner = owner;
            c.inhabitants = {people - people / 2, people / 2};
            g.world.add_city(c);
        };
        add_city(1, 0, kComputer, 0);
        if (k.colony) {
            add_city(0, 0, kComputer, k.people);
        }
        if (k.war) {
            g.world.diplomacy().relation(kComputer, kHuman).peace = sim::Treaty::None;
        }
        add_city(k.island, 1, k.founder, 0);
        sim::Simulation s(g.world, g.buildings, g.figures);
        sim::ComputerPlayers ai(s);
        ai.reset();
        ai.city_founded(k.island, 1);
        const auto& events = g.world.diplomacy().events(kComputer);
        return std::any_of(events.begin(), events.end(), [&](const sim::DiplomacyEvent& e) {
            return e.type == sim::DiplomacyEventType::Settled && e.partner == k.founder;
        });
    };

    const auto with = [](auto&& change) {
        Case k;
        change(k);
        return k;
    };
    CHECK(remembered(Case{}));                                         // at peace with the founder
    CHECK(remembered(with([](Case& k) { k.people = 9; })));            // nine are still a young colony
    CHECK(!remembered(with([](Case& k) { k.people = 10; })));          // ten are not
    CHECK(!remembered(with([](Case& k) { k.war = true; })));           // at war: nothing more to hold against him
    CHECK(!remembered(with([](Case& k) { k.island = 1; })));           // on the island of the home city
    CHECK(!remembered(with([](Case& k) { k.founder = kComputer; })));  // its own second city there
    CHECK(!remembered(with([](Case& k) { k.colony = false; })));       // no colony of the computer there
}

TEST_CASE("ai: a computer answers a ship that is about to found a city on its island") {
    using Kind = sim::DiplomacyNotice::Kind;
    struct Case {
        sim::PlayerId asker = kHuman;
        std::uint16_t island = 0;  ///< where the ship wants to found
        std::uint32_t people = 9;  ///< inhabitants of the computer's colony on the islet
        std::uint8_t max_stage = 0x29;
        bool home = true;  ///< the computer's home city is known (on the big island)
        bool war = false;
        std::uint16_t size = 0;  ///< template size of the islet
    };
    // The computer's home city on the big island (island 1), a colony on the islet (island 0).
    const auto answer = [](const Case& k) -> std::optional<sim::DiplomacyNotice> {
        Game g(false);
        const auto add_city = [&](std::uint16_t island, sim::PlayerId owner, std::uint32_t people) {
            sim::City c;
            c.island = island;
            c.owner = owner;
            c.inhabitants = {people - people / 2, people / 2};
            g.world.add_city(c);
        };
        add_city(1, kComputer, 400);
        add_city(0, kComputer, k.people);
        g.world.island(0).header.template_size = k.size;
        if (k.war) {
            g.world.diplomacy().relation(kComputer, kHuman).peace = sim::Treaty::None;
        }
        sim::Simulation s(g.world, g.buildings, g.figures);
        sim::ComputerPlayers ai(s);
        ai.reset();
        sim::ai::State state;
        {
            const std::vector<std::uint8_t> now = saved(ai);
            core::ByteReader in(now);
            sim::StateIn reader(in);
            sim::ai::Loader loader(reader, ai.state_version());
            loader(state);
            REQUIRE(reader.ok());
        }
        for (sim::ai::AiPlayer& p : state.players) {
            p.max_stage = k.max_stage;
            p.home_valid = k.home;
            p.home_island = 1;
        }
        core::ByteWriter out;
        sim::ai::Saver saver(out, sim::ComputerPlayers::max_state_version());
        saver(state);
        const std::vector<std::uint8_t> bytes = out.take();
        core::ByteReader in(bytes);
        REQUIRE(ai.load(in, sim::ComputerPlayers::max_state_version()));
        (void)g.world.diplomacy().take_notices();
        ai.founding_query(k.asker, k.island);
        const auto notices = g.world.diplomacy().take_notices();
        if (notices.empty()) {
            return std::nullopt;
        }
        REQUIRE(notices.size() == 1);
        CHECK(notices[0].to == k.asker);
        CHECK(notices[0].from == kComputer);
        CHECK(notices[0].island == k.island);  // its city there
        return notices[0];
    };
    const auto kind = [&](const Case& k) { return answer(k).value().kind; };
    const auto with = [](auto&& change) {
        Case k;
        change(k);
        return k;
    };
    // A young colony of a computer that has reached the merchants: it would tolerate it.
    CHECK(kind(Case{}) == Kind::FoundingTolerated);
    CHECK(kind(with([](Case& k) { k.people = 10; })) == Kind::FoundingRefused);       // a colony of ten is not young
    CHECK(kind(with([](Case& k) { k.max_stage = 0x28; })) == Kind::FoundingRefused);  // not that far yet
    // On the island of its home city, or with no home city known: a protest under threat of war.
    CHECK(kind(with([](Case& k) { k.island = 1; })) == Kind::FoundingProtest);
    CHECK(kind(with([](Case& k) { k.home = false; })) == Kind::FoundingProtest);
    // No answer at war, on a large island, to itself, or where it has no city.
    CHECK(!answer(with([](Case& k) { k.war = true; })));
    CHECK(!answer(with([](Case& k) { k.size = 4; })));
    CHECK(answer(with([](Case& k) { k.size = 3; })));
    CHECK(!answer(with([](Case& k) { k.asker = kComputer; })));
    CHECK(!answer(with([](Case& k) { k.island = 7; })));
}

TEST_CASE("ai: a founding the computers do not know of is ignored") {
    Game g(false);
    sim::Simulation s(g.world, g.buildings, g.figures);
    sim::ComputerPlayers ai(s);
    ai.reset();
    const auto before = saved(ai);
    ai.city_founded(1, 0);   // no such city
    ai.city_founded(99, 7);  // no such island
    CHECK(saved(ai) == before);
    for (const auto& e : g.world.diplomacy().events(kComputer)) {
        CHECK(e.type == sim::DiplomacyEventType::None);
    }
}

TEST_CASE("ai: computers sign trade treaties with each other and leave their peace alone") {
    Game g;
    // Slot 0 is a computer with a city on the islet.
    g.world.player(kHuman)->role = sim::PlayerRole::Computer;
    sim::City c;
    c.island = 0;
    c.owner = kHuman;
    g.world.add_city(c);
    sim::Simulation s(g.world, g.buildings, g.figures);
    const auto& d = g.world.diplomacy();
    REQUIRE(d.relation(kComputer, kHuman).trade == sim::Treaty::None);
    const sim::Treaty with_trader = d.relation(kComputer, kTrader).trade;
    const bool signed_both = run_until(s, 600'000, [&] {
        return d.relation(kComputer, kHuman).trade == sim::Treaty::Signed &&
               d.relation(kHuman, kComputer).trade == sim::Treaty::Signed;
    });
    CHECK(signed_both);
    CHECK(g.cities_of(kComputer) == 1);
    CHECK(d.relation(kComputer, kHuman).peace == sim::Treaty::Signed);
    CHECK(d.relation(kHuman, kComputer).peace == sim::Treaty::Signed);
    CHECK(d.relation(kComputer, kTrader).trade == with_trader);
}

TEST_CASE("ai: a computer at war takes a human's peace offer, unless it is still angry") {
    for (const bool angry : {false, true}) {
        Game g;
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        auto& d = g.world.diplomacy();
        d.relation(kComputer, kHuman).peace = sim::Treaty::None;
        d.relation(kHuman, kComputer).peace = sim::Treaty::None;
        if (angry) {
            d.relation(kComputer, kHuman).damage = 400;  // a mood of -100
        }
        REQUIRE(d.set_peace(g.world, kHuman, kComputer, sim::Treaty::Offered, g.world.time_ms()));
        REQUIRE(d.relation(kComputer, kHuman).peace == sim::Treaty::Received);
        // A changed treaty is answered at once.
        run(s, 1'000);
        CHECK(d.relation(kComputer, kHuman).peace == (angry ? sim::Treaty::Received : sim::Treaty::Signed));
        CHECK(d.relation(kHuman, kComputer).peace == (angry ? sim::Treaty::Offered : sim::Treaty::Signed));
    }
}

TEST_CASE("ai: a computer a human has hurt declares war if it is as strong, else pays the pirates against him") {
    for (const bool human_fleet : {false, true}) {
        Game g;
        add_pirates(g);
        constexpr sim::PlayerId kPirates = 4;
        // The cannoneer's damage weighs the cannons of a fleet.
        const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
        auto figures = assets::FigureTable::from_cod(assets::cod::parse(
            "Objekt: FIGUR\n"
            "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
            "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
            "  Nummer: 13\n  Gfx: 0\n  Hitpoint: 50.0\n  Worktime: 1.0\n"
            "EndObj\n",
            types));
        REQUIRE(figures.ok());
        g.figures = std::move(figures).value();
        if (human_fleet) {
            g.add_ship(kHuman, 5, 80).cannons = 20;
        }
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        auto& d = g.world.diplomacy();
        REQUIRE(d.relation(kComputer, kHuman).peace == sim::Treaty::Signed);
        d.relation(kComputer, kHuman).damage = 400;
        // Rich enough to bribe the pirates, with 9000 to spare.
        g.world.player(kComputer)->money = 30'000;
        const std::int32_t before = g.world.players()[kComputer].money;
        // The computer looks at its relations every 30 s.
        run(s, 31'000);
        if (!human_fleet) {
            CHECK(d.relation(kComputer, kHuman).peace == sim::Treaty::None);
            CHECK(d.relation(kHuman, kComputer).peace == sim::Treaty::None);
            CHECK(!remembers(g.world, kPirates, sim::DiplomacyEventType::BribedBy, kComputer));
        } else {
            CHECK(d.relation(kComputer, kHuman).peace == sim::Treaty::Signed);
            CHECK(remembers(g.world, kPirates, sim::DiplomacyEventType::BribedBy, kComputer));
            CHECK(remembers(g.world, kPirates, sim::DiplomacyEventType::BribedAgainst, kHuman));
            CHECK(g.world.players()[kComputer].money < before);
        }
    }
}

TEST_CASE("ai: a rich computer buys peace from the pirates, a poor one does not") {
    for (const std::int32_t money : {9'000, 100'000}) {
        Game g;
        add_pirates(g);
        constexpr sim::PlayerId kPirates = 4;
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        g.world.player(kComputer)->money = money;
        run(s, 7'000);  // the 6-second recruiting phase
        const bool bought = remembers(g.world, kPirates, sim::DiplomacyEventType::PeaceBought, kComputer);
        CHECK(bought == (money > 10'000));
        CHECK((g.world.players()[kComputer].money < money) == bought);
    }
}

TEST_CASE("ai: the computer switches its workshops by their switch first, as the original") {
    // A toolsmith and a weaving mill, both out of raw material for five cycles (Maxnorohst 5).
    Game g;
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 20990\n  Gfx: 100\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Maxnorohst: 5\n    Ware: 22\n    Rohstoff: 10\n"
        "    Rohmenge: 1\n    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
        "  @Nummer: 14\n  Id: 20991\n  Gfx: 101\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Maxnorohst: 5\n    Ware: 19\n    Rohstoff: 4\n"
        "    Rohmenge: 1\n    Prodmenge: 1\n    Interval: 20\n  EndObj\n");
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(city != nullptr);
    sim::IslandMap& map = g.world.island(city->island).map;
    // Two free tiles of the city's ground.
    std::vector<std::pair<std::int32_t, std::int32_t>> free;
    for (std::uint32_t y = 0; y < 50 && free.size() < 2; ++y) {
        for (std::uint32_t x = 0; x < 46 && free.size() < 2; ++x) {
            const sim::MapCell& cell = map.top(x, y);
            if (cell.city == city->index && cell.owner == kComputer &&
                g.buildings.by_sprite(cell.sprite) == g.buildings.by_id(kGround)) {
                free.emplace_back(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
            }
        }
    }
    REQUIRE(free.size() == 2);
    const auto put = [&](std::uint32_t id, std::pair<std::int32_t, std::int32_t> at, bool enabled) {
        REQUIRE(map.build(*g.buildings.by_id(id), at.first, at.second, 0, 0, kComputer, city->index).ok());
        sim::Production p;
        p.island = city->island;
        p.x = static_cast<std::uint16_t>(at.first);
        p.y = static_cast<std::uint16_t>(at.second);
        p.enabled = enabled;
        p.idle_cycles = 5;
        g.world.set_production(p);
        return *g.world.production_index(p.island, p.x, p.y);
    };
    const std::uint32_t smith = put(20990, free[0], false);  // switched off while it was starved
    const std::uint32_t mill = put(20991, free[1], true);    // still on
    constexpr std::uint32_t kTon = sim::kStockUnitsPerTon;
    city->goods.resize(std::max<std::size_t>(city->goods.size(), 25));
    city->goods[10] = 5 * kTon;  // iron
    city->goods[22] = 0;         // tools
    city->goods[4] = 10 * kTon;  // wool

    // The off toolsmith comes on: its idle count belongs to the time it was on. The starved mill
    // goes off although it has wool, and on again at a later refresh, which clears its count.
    bool smith_on = false;
    bool mill_off = false;
    bool mill_back = false;
    run_until(s, 60'000, [&] {
        city->goods[10] = 5 * kTon;
        city->goods[22] = 0;
        city->goods[4] = 10 * kTon;
        const auto& prods = g.world.productions();
        smith_on = smith_on || prods[smith].enabled;
        mill_off = mill_off || !prods[mill].enabled;
        mill_back = mill_back || (mill_off && prods[mill].enabled);
        return smith_on && mill_back;
    });
    CHECK(smith_on);
    CHECK(mill_off);
    CHECK(mill_back);

    // At 40 t of tools the toolsmith goes off, starved or not, and stays off.
    bool smith_off = false;
    run_until(s, 60'000, [&] {
        city->goods[22] = 40 * kTon;
        smith_off = !g.world.productions()[smith].enabled;
        return smith_off;
    });
    CHECK(smith_off);
    run(s, 20'000);
    CHECK(!g.world.productions()[smith].enabled);
}

TEST_CASE("ai: a farm leaves the trees of the city's woodcutter standing (S-122)") {
    // A woodcutter in the wood west of the Kontor and a tobacco plantation next to it, both on the
    // road the computer lays from its Kontor. The original plants the tobacco over the woodcutter's
    // trees; the woodcutter then lacks trees and its fields go round. Pastures do the same with
    // their grass.
    const auto fields_in_reach = [] {
        Game g;
        g.buildings = make_buildings(
            // Woodcutter, tobacco plantation, a tobacco field growing and ripe.
            "  @Nummer: 13\n  Id: 20701\n  Kind: 14\n  Gfx: 30\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 23\n    Rohstoff: 53\n    Rohmenge: 1\n"
            "    Prodmenge: 1\n    Maxlager: 4\n    Interval: 20\n    Radius: 5\n  EndObj\n"
            "  @Nummer: 14\n  Id: 20702\n  Kind: 14\n  Gfx: 40\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 6\n    Rohstoff: 46\n    Rohmenge: 1\n"
            "    Prodmenge: 1\n    Maxlager: 4\n    Interval: 20\n    Radius: 4\n  EndObj\n"
            "  @Nummer: 15\n  Id: 20703\n  Kind: 10\n  Gfx: 50\n  Size: 1, 1\n  AnimAnz: 4\n  AnimAdd: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 160\n  EndObj\n"
            "  @Nummer: 16\n  Id: 20704\n  Kind: 10\n  Gfx: 54\n  Size: 1, 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 46\n  EndObj\n");
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s, 5'000);  // the Kontor at 44,16, a road west along row 17
        sim::BuildOrder order;
        order.player = kComputer;
        order.island = 1;
        order.items = {{20701, 37, 18, 0, 0}, {20702, 40, 18, 0, 0}};
        const sim::BuildPlan plan = sim::execute_build(g.world, g.buildings, order, {}, nullptr);
        REQUIRE(plan.placed == 2);
        run(s, 30'000);
        const sim::IslandMap& map = g.world.islands()[1].map;
        const std::vector<std::uint8_t>& circle = sim::circle_table(5);
        std::size_t fields = 0;
        for (std::int32_t y = 18 - 5; y <= 19 + 5; ++y) {
            for (std::int32_t x = 37 - 5; x <= 38 + 5; ++x) {
                const std::int32_t dx = std::max({37 - x, 0, x - 38});
                const std::int32_t dy = std::max({18 - y, 0, y - 19});
                const sim::MapCell& c = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                const assets::BuildingType* b = c.empty() ? nullptr : g.buildings.by_sprite(c.sprite);
                if (dx <= circle[static_cast<std::size_t>(dy)] && b != nullptr && (b->id == 20703 || b->id == 20704)) {
                    ++fields;
                }
            }
        }
        return fields;
    };
    CHECK(fields_in_reach() == 0);  // the original plants 39 of them at once
}

TEST_CASE("ai: a computer that finds no island for its city gives its ship to the pirates and leaves") {
    // The big island under water: only the rock is left, too small for a colony (root task
    // Ship op 5 with value 5, then the players' check retires the computer).
    Game g;
    add_pirates(g);
    constexpr sim::PlayerId kPirates = 4;
    sim::IslandMap& map = g.world.island(1).map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            map.set_sprite(x, y, static_cast<std::uint16_t>(g.buildings.by_id(kSea)->gfx));
        }
    }
    sim::Simulation s(g.world, g.buildings, g.figures);
    const std::uint32_t ship = g.world.ships().front().id;
    REQUIRE(run_until(s, 3'600'000, [&] { return g.world.ship(ship)->owner != kComputer; }));
    CHECK(g.world.ship(ship)->owner == kPirates);
    CHECK(g.cities_of(kComputer) == 0);
    run(s, 6'000);  // the players' check every 5 s
    CHECK(!g.world.players()[kComputer].active());
}

TEST_CASE("ai: the pirates' nest offers its goods every 42 s without any food (task 5)") {
    // A nest with a pirate Kontor of 6 wares, loot but no food, and a pirate ship at sea.
    Game g(false);
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 22990\n  Kind: 35\n  Gfx: 120\n  Size: 2, 3\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Maxware: 6\n    Maxlager: 30\n  EndObj\n");
    add_pirates(g);
    constexpr sim::PlayerId kPirates = 4;
    g.world.player(kHuman)->infra_mask |= 1U << 24;  // citizens: the pirates may sail
    REQUIRE(g.world.island(1).map.build(*g.buildings.by_id(22990), 44, 10, 0, 0, kPirates, 0).ok());
    sim::City nest;
    nest.island = 1;
    nest.index = 0;
    nest.owner = kPirates;
    nest.warehouse_x = 44;
    nest.warehouse_y = 10;
    nest.capacity = 30 * 32;
    nest.ensure_ware(24);
    nest.trade.resize(25);
    nest.goods[assets::ware::kJewellery] = 5 * 32;
    nest.goods[assets::ware::kTools] = 10 * 32;
    g.world.add_city(nest);
    g.add_ship(kPirates, 86, 45);
    sim::Simulation s(g.world, g.buildings, g.figures);
    const auto offered = [&] {
        const sim::City* c = g.world.city(1, 0);
        return c->trade[assets::ware::kJewellery].sell && c->trade[assets::ware::kTools].sell;
    };
    run(s, 30'000);
    CHECK(!offered());  // not before 42 s
    REQUIRE(run_until(s, 60'000, offered));
    CHECK(g.world.city(1, 0)->trade[assets::ware::kJewellery].sell_price == 358);
    // Sold out, the next offer drops the ware.
    g.world.city(1, 0)->goods[assets::ware::kJewellery] = 0;
    REQUIRE(run_until(s, 60'000, [&] { return !g.world.city(1, 0)->trade[assets::ware::kJewellery].sell; }));
    CHECK(g.world.city(1, 0)->trade[assets::ware::kTools].sell);
}

TEST_CASE("ships: only a computer's ship can be given to the pirates") {
    Game g(false);
    add_pirates(g);
    const sim::Ship& mine = g.add_ship(kHuman, 80, 40);
    const std::uint32_t human_ship = mine.id;
    const std::uint32_t computer_ship = g.add_ship(kComputer, 86, 45).id;
    sim::Simulation s(g.world, g.buildings, g.figures);
    const auto give = [&](sim::PlayerId player, std::uint32_t ship) {
        sim::ShipCommand c;
        c.player = player;
        c.ship = ship;
        c.op = sim::ShipCommand::Op::ToPirates;
        return s.execute(c).accepted;
    };
    CHECK(!give(kHuman, human_ship));
    CHECK(!give(kHuman, computer_ship));  // not his
    CHECK(give(kComputer, computer_ship));
    CHECK(g.world.ship(computer_ship)->owner == 4);
}

TEST_CASE("ai: a computer short of ships buys one a player at peace offers (Rules::computers_buy_ships)") {
    // Open1602 (the History Edition's computers buy ships too): a ship at no more than its yard
    // price, if half the money is left. Not with the rule off (the original's), not from an enemy,
    // not at a price above the yard's.
    struct Case {
        bool rule = true;
        bool war = false;
        std::uint16_t factor = 96;
        bool bought = true;
    };
    for (const Case c :
         {Case{}, Case{false, false, 96, false}, Case{true, true, 96, false}, Case{true, false, 200, false}}) {
        Game g;
        sim::Rules rules;
        rules.computers_buy_ships = c.rule;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s);
        // The computer's only ship goes (to the human): with its city it wants one again.
        g.world.ships().front().owner = kHuman;
        sim::Ship& offered = g.add_ship(kHuman, 10, 80);
        offered.for_sale = true;
        offered.sale_factor = c.factor;
        const std::uint32_t id = offered.id;
        if (c.war) {
            g.world.diplomacy().relation(kHuman, kComputer).peace = sim::Treaty::None;
            g.world.diplomacy().relation(kComputer, kHuman).peace = sim::Treaty::None;
        }
        const std::int32_t human_money = g.world.player(kHuman)->money;
        const bool bought = run_until(s, 120'000, [&] { return g.world.ship(id)->owner == kComputer; });
        CHECK(bought == c.bought);
        if (bought) {
            CHECK(g.world.player(kHuman)->money == human_money + 1000 * c.factor / 128);
            CHECK(!g.world.ship(id)->for_sale);
        }
    }
    CHECK(!sim::Rules::original().computers_buy_ships);
}

namespace {

/// Stone walls (a family of straight, curve, T and cross) and a gate, numbered on from the Kontor.
const std::string kWallRecords =
    "  @Nummer: 13\n  Id: 22501\n  Kind: 4\n  Gfx: 30\n  Size: 1, 1\n  KreuzBase: 13\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 10\n    Ziegel: 1\n  EndObj\n"
    "  @Nummer: 14\n  Id: 22502\n  Kind: 4\n  Gfx: 31\n  Size: 1, 1\n  KreuzBase: 13\n"
    "  @Nummer: 15\n  Id: 22503\n  Kind: 4\n  Gfx: 32\n  Size: 1, 1\n  KreuzBase: 13\n"
    "  @Nummer: 16\n  Id: 22504\n  Kind: 4\n  Gfx: 33\n  Size: 1, 1\n  KreuzBase: 13\n"
    "  @Nummer: 17\n  Id: 20304\n  Kind: 3\n  Gfx: 34\n  Size: 1, 1\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 20\n    Ziegel: 2\n  EndObj\n";

struct WallCount {
    std::size_t walls = 0;
    std::size_t gates = 0;
    std::size_t inner = 0;  ///< walls with all eight neighbours in the city
};

WallCount walls_of(const Game& g, const sim::City& city) {
    WallCount n;
    const sim::IslandMap& map = g.world.islands()[city.island].map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const sim::MapCell& c = map.top(x, y);
            const assets::BuildingType* b = c.empty() ? nullptr : g.buildings.by_sprite(c.sprite);
            if (b == nullptr || c.city != city.index) {
                continue;
            }
            if (b->kind == 3) {
                ++n.gates;
            }
            if (b->kind != 4) {
                continue;
            }
            ++n.walls;
            bool inside = true;
            for (std::int32_t dy = -1; dy <= 1; ++dy) {
                for (std::int32_t dx = -1; dx <= 1; ++dx) {
                    const auto nx = static_cast<std::int32_t>(x) + dx;
                    const auto ny = static_cast<std::int32_t>(y) + dy;
                    inside = inside && map.contains(nx, ny) &&
                             map.top(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny)).city == city.index;
                }
            }
            n.inner += inside ? 1U : 0U;
        }
    }
    return n;
}

sim::City* computer_city(Game& g) {
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            return g.world.city(c.island, c.index);
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("ai: a computer at war with the human walls in its city once it has the bricks and gold") {
    Game g(true, 50'000);
    g.buildings = make_buildings(kWallRecords);
    sim::Simulation s(g.world, g.buildings, g.figures);
    g.add_ship(kHuman, 5, 80);  // still in the game
    g.world.diplomacy().relation(kComputer, kHuman).peace = sim::Treaty::None;
    g.world.diplomacy().relation(kHuman, kComputer).peace = sim::Treaty::None;
    found_city(g, s);
    sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    // No walls while the bricks are short: below 16 t at war.
    const std::uint8_t bricks = g.buildings.materials().bricks;
    city->ensure_ware(bricks);
    city->goods[bricks] = 15 * sim::kStockUnitsPerTon;
    run(s, 60'000);
    CHECK(walls_of(g, *city).walls == 0);
    // With enough of them the border goes up, a wall on every side of the city but none inside.
    g.world.player(kComputer)->money = 50'000;
    REQUIRE(run_until(s, 120'000, [&] {
        city->goods[bricks] = city->capacity;
        return walls_of(g, *city).walls >= 20;
    }));
    CHECK(walls_of(g, *city).inner == 0);
    CHECK(walls_of(g, *city).gates == 0);  // never at war
}

/// Whether the city has a wall at (x, y).
bool wall_at(const Game& g, const sim::City& city, std::uint32_t x, std::uint32_t y) {
    const sim::MapCell& c = g.world.islands()[city.island].map.top(x, y);
    const assets::BuildingType* b = c.empty() ? nullptr : g.buildings.by_sprite(c.sprite);
    return b != nullptr && b->kind == 4 && c.city == city.index;
}

TEST_CASE("ai: a computer at peace walls its coast and the border by free beach, not the inland border") {
    Game g(true, 50'000);
    g.buildings = make_buildings(kWallRecords);
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    const std::uint8_t bricks = g.buildings.materials().bricks;
    city->ensure_ware(bricks);
    // Below the citizens nothing, whatever the bricks.
    city->goods[bricks] = 60 * sim::kStockUnitsPerTon;
    run(s, 30'000);
    CHECK(walls_of(g, *city).walls == 0);
    // With them, and more than 40 t of bricks.
    g.world.player(kComputer)->set_unlocked(25, true);
    run_until(s, 120'000, [&] {
        city->goods[bricks] = 60 * sim::kStockUnitsPerTon;
        return false;
    });
    CHECK(walls_of(g, *city).walls > 0);
    CHECK(wall_at(g, *city, 46, 12));  // the beach
    // The border inland (column 36) faces neither another city nor free beach.
    for (std::uint32_t y = 0; y < 50; ++y) {
        CHECK(!wall_at(g, *city, 36, y));
    }
}

TEST_CASE("ai: a wall the city grew past is demolished") {
    Game g(true, 50'000);
    g.buildings = make_buildings(kWallRecords);
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    sim::IslandMap& map = g.world.island(city->island).map;
    // A wall on free ground in the middle of the city, away from the coast.
    std::optional<std::pair<std::int32_t, std::int32_t>> at;
    for (std::int32_t y = 3; y < 47 && !at; ++y) {
        for (std::int32_t x = 3; x < 43 && !at; ++x) {
            bool inside = true;
            for (std::int32_t dy = -3; dy <= 3; ++dy) {
                for (std::int32_t dx = -3; dx <= 3; ++dx) {
                    const sim::MapCell& n =
                        map.top(static_cast<std::uint32_t>(x + dx), static_cast<std::uint32_t>(y + dy));
                    inside = inside && n.city == city->index;
                }
            }
            const sim::MapCell& cell = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            const std::int32_t k = g.buildings.by_sprite(cell.sprite)->kind;
            if (inside && (k == 10 || k == 11)) {
                at.emplace(x, y);
            }
        }
    }
    REQUIRE(at.has_value());
    REQUIRE(map.build(*g.buildings.by_id(22501), at->first, at->second, 0, 0, kComputer, city->index).ok());
    REQUIRE(wall_at(g, *city, static_cast<std::uint32_t>(at->first), static_cast<std::uint32_t>(at->second)));
    CHECK(run_until(s, 120'000, [&] {
        return !wall_at(g, *city, static_cast<std::uint32_t>(at->first), static_cast<std::uint32_t>(at->second));
    }));
}

TEST_CASE("ai: a computer builds the producer of a package's good its inhabitants need (MD-4)") {
    // A workshop of a package (2 x 2) makes ware 59 out of nothing; the city's class needs it.
    Game g;
    constexpr std::uint32_t kPepperMill = 20992;
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 59\n"
        "    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
    assets::ResidentClass settlers;
    settlers.max_per_house = 10;
    settlers.needs.push_back({59, 100});
    g.buildings.set_resident_classes({settlers});
    g.buildings.by_id(kKontor)->ware = sim::ware::kAll;  // the Kontor holds every good: workshops go near it
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 60'000);  // and its first roads
    sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(city != nullptr);
    // 100 inhabitants use 7.3 t a minute of it; a workshop makes 3 t: the city wants three.
    city->inhabitants.assign(1, 100);
    const sim::IslandMap& map = g.world.island(city->island).map;
    const auto mills = [&] {
        std::size_t n = 0;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const sim::MapCell& cell = map.top(x, y);
                const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                n += b != nullptr && b->id == kPepperMill && cell.owner == kComputer ? 1U : 0U;
            }
        }
        return n / 4;  // four tiles each
    };
    CHECK(run_until(s, 600'000, [&] {
        city->inhabitants.assign(1, 100);
        return mills() != 0;
    }));
    // The survey counts it in the first slot after the original's.
    run(s, 60'000);
    const auto view = s.computers().view();
    REQUIRE(view[0].plans.size() >= 1);
    REQUIRE(view[0].plans[0].slots.size() == sim::ai::kSlots + 1);
    CHECK(view[0].plans[0].slots[sim::ai::kSlots][0] == mills());
    CHECK(mills() >= 1);
}

TEST_CASE("ai: a computer builds a package's farm that makes its good from nothing (raw = \"\")") {
    // A plantation of a package (2 x 2) without fields makes ware 59; the city's class needs it.
    Game g;
    constexpr std::uint32_t kPepperFarm = 20992;
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Maxlager: 4\n    Ware: 59\n"
        "    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
    assets::ResidentClass settlers;
    settlers.max_per_house = 10;
    settlers.needs.push_back({59, 100});
    g.buildings.set_resident_classes({settlers});
    g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 60'000);
    sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(city != nullptr);
    const sim::IslandMap& map = g.world.island(city->island).map;
    const auto farms = [&] {
        std::size_t n = 0;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const sim::MapCell& cell = map.top(x, y);
                const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                n += b != nullptr && b->id == kPepperFarm && cell.owner == kComputer ? 1U : 0U;
            }
        }
        return n / 4;
    };
    CHECK(run_until(s, 600'000, [&] {
        city->inhabitants.assign(1, 100);
        return farms() != 0;
    }));
}

TEST_CASE("ai: the computers' cheats come from the game's balance (balance.toml [computers])") {
    // A poor computer gets money for its founding: the original's 799, or what the balance gives.
    const auto money_after_founding = [](std::uint32_t founding_money) {
        Game g(true, 300);
        sim::Balance balance;
        balance.ai_founding_money = founding_money;
        g.world.set_balance(balance);
        sim::Simulation s(g.world, g.buildings, g.figures);
        REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
        return g.world.players()[kComputer].money;
    };
    const std::int32_t original = money_after_founding(799);
    CHECK(money_after_founding(5799) == original + 5000);
    CHECK(money_after_founding(0) == original - 799);
}

TEST_CASE("ai: a stage's build order from the game's balance (computers.toml) is what the city builds (MD-4)") {
    // The pioneers' order of a package asks for two workshops of a package's good, where the
    // inhabitants alone want one.
    const auto mills_built = [](bool with_order) {
        Game g;
        static constexpr std::uint32_t kPepperMill = 20992;
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 59\n"
            "    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
        assets::ResidentClass settlers;
        settlers.max_per_house = 10;
        settlers.needs.push_back({59, 100});
        g.buildings.set_resident_classes({settlers});
        g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
        if (with_order) {
            sim::Balance balance;
            balance.computer_orders.push_back(
                {sim::ComputerList::Pioneers, {sim::ComputerNeed{sim::ComputerNeed::Kind::Package, 59, 2}}});
            REQUIRE(sim::valid(balance));
            g.world.set_balance(balance);
        }
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s, 60'000);
        sim::City* city = nullptr;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                city = g.world.city(c.island, c.index);
            }
        }
        REQUIRE(city != nullptr);
        const sim::IslandMap& map = g.world.island(city->island).map;
        const auto mills = [&] {
            std::size_t n = 0;
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::MapCell& cell = map.top(x, y);
                    const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                    n += b != nullptr && b->id == kPepperMill && cell.owner == kComputer ? 1U : 0U;
                }
            }
            return n / 4;
        };
        // Ten inhabitants use 0.7 t a minute: one workshop is enough for them.
        for (std::uint32_t t = 0; t < 600'000; t += 10'000) {
            city->inhabitants.assign(1, 10);
            run(s, 10'000);
        }
        return mills();
    };
    CHECK(mills_built(false) == 1);
    CHECK(mills_built(true) == 2);
}

TEST_CASE("ai: a package's building replaces a producer of the original's ([producer.<name>], MD-4)") {
    // The founded city's order of a package asks for a bakery; a package names its own workshop as
    // the bakery, without the mill first. Without the change the game has no bakery to build.
    const auto workshops_built = [](bool with_change) {
        Game g;
        static constexpr std::uint32_t kSmokehouse = 20992;
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 59\n"
            "    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
        g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
        sim::Balance balance;
        balance.computer_orders.push_back(
            {sim::ComputerList::Founded,
             {sim::ComputerNeed{sim::ComputerNeed::Kind::Market, 0, 1},
              sim::ComputerNeed{sim::ComputerNeed::Kind::Building, sim::ai::kSlotBakery, 1},
              sim::ComputerNeed{sim::ComputerNeed::Kind::Houses, 0, 8}}});
        if (with_change) {
            sim::ComputerProducer bakery;
            bakery.slot = sim::ai::kSlotBakery;
            bakery.building = kSmokehouse;
            bakery.after = sim::kNoProducer;
            balance.computer_producers.push_back(bakery);
        }
        REQUIRE(sim::valid(balance));
        g.world.set_balance(balance);
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s, 120'000);
        std::size_t n = 0;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner != kComputer) {
                continue;
            }
            const sim::IslandMap& map = g.world.island(c.island).map;
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::MapCell& cell = map.top(x, y);
                    const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                    n += b != nullptr && b->id == kSmokehouse && cell.owner == kComputer ? 1U : 0U;
                }
            }
        }
        if (with_change) {
            // The survey counts it as the city's bakery, so the order's one is enough.
            const auto view = s.computers().view();
            REQUIRE(!view.empty());
            REQUIRE(!view[0].plans.empty());
            CHECK(view[0].plans[0].slots[sim::ai::kSlotBakery][0] == n / 4);
        }
        return n / 4;  // four tiles each
    };
    CHECK(workshops_built(false) == 0);
    CHECK(workshops_built(true) == 1);
}

TEST_CASE("ai: a package's slots count its good as food and remove a producer (slots.toml)") {
    // As above, the package's workshop is the bakery. Counted as food, the survey finds it on the
    // city's food counter; with the bakery removed the order asks for nothing.
    struct Built {
        std::size_t workshops = 0;
        std::uint32_t food = 0;
    };
    const auto built = [](std::string_view slots_toml) {
        Game g;
        static constexpr std::uint32_t kSmokehouse = 20992;
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 59\n"
            "    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
        g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
        sim::Balance balance;
        balance.computer_orders.push_back(
            {sim::ComputerList::Founded,
             {sim::ComputerNeed{sim::ComputerNeed::Kind::Market, 0, 1},
              sim::ComputerNeed{sim::ComputerNeed::Kind::Building, sim::ai::kSlotBakery, 1},
              sim::ComputerNeed{sim::ComputerNeed::Kind::Houses, 0, 8}}});
        sim::ComputerProducer bakery;
        bakery.slot = sim::ai::kSlotBakery;
        bakery.building = kSmokehouse;
        bakery.after = sim::kNoProducer;
        balance.computer_producers.push_back(bakery);
        assets::Goods goods;
        assets::OwnGood fish;
        fish.id = "smoke:fish";
        fish.price = 10;
        fish.icon = assets::ware::kFirstTrade;
        REQUIRE(goods.add(fish).ok());
        REQUIRE(goods.own("smoke:fish")->number == 59);
        std::vector<std::string> warnings;
        sim::apply_slots_file(balance.computer_slots, "smoke", slots_toml, warnings, goods);
        REQUIRE(warnings.empty());
        REQUIRE(sim::valid(balance));
        g.world.set_balance(balance);
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s, 120'000);
        Built out;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner != kComputer) {
                continue;
            }
            const sim::IslandMap& map = g.world.island(c.island).map;
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::MapCell& cell = map.top(x, y);
                    const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                    out.workshops += b != nullptr && b->id == kSmokehouse && cell.owner == kComputer ? 1U : 0U;
                }
            }
        }
        out.workshops /= 4;  // four tiles each
        const auto view = s.computers().view();
        REQUIRE(!view.empty());
        REQUIRE(!view[0].plans.empty());
        out.food = view[0].plans[0].slots[sim::ai::kSlotFood][0];
        return out;
    };
    const Built plain = built("");
    CHECK(plain.workshops == 1);
    CHECK(plain.food == 0);
    const Built food = built("[food]\ncounts = \"smoke:fish\"\n");
    CHECK(food.workshops == 1);
    CHECK(food.food == 1);
    CHECK(built("[bakery]\nremove = true\n").workshops == 0);
}

TEST_CASE("ai: a package's public building of a kind of its own is planned by the computers (slots.toml)") {
    // A court of its own kind: the founded city's order asks for one. The test world has no houses
    // for it to serve, so the computer plans it (its "covered" flag) and finds no site. `ahead`
    // public buildings of a kind of their own come before it, so its flag and service may lie
    // beyond 32 bits, and the computers' state needs version 6 to keep them.
    struct Planned {
        std::uint64_t flags = 0;
        std::uint16_t version = 0;
        bool kept = false;
    };
    const auto planned = [](bool with_slot, std::size_t ahead) {
        Game g;
        static constexpr std::uint32_t kCourt = 20992;
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 18\n    Radius: 12\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
        g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
        g.buildings.by_id(kCourt)->own_service = assets::public_building::own(ahead);
        const std::vector<assets::OwnBuilding> own = {{"rome:court", kCourt}};
        sim::Balance balance;
        std::vector<std::string> warnings;
        if (with_slot && ahead == 0) {
            sim::apply_slots_file(balance.computer_slots, "rome",
                                  "[court]\nkind = \"public\"\nbuilding = \"rome:court\"\n", warnings, {}, own,
                                  &g.buildings);
        } else if (with_slot) {
            balance.computer_slots = sim::original_computer_slots();
            for (std::size_t i = 0; i < ahead; ++i) {
                balance.computer_slots.push_back(
                    sim::own_public_slot(i, "other_" + std::to_string(i), kCourt, assets::public_building::own(i)));
            }
            balance.computer_slots.push_back(
                sim::own_public_slot(ahead, "court", kCourt, assets::public_building::own(ahead)));
        }
        sim::apply_computers_file(balance.computer_orders, "rome", "[founded]\norder = \"market 1, court 1\"\n",
                                  warnings, {}, nullptr, own, &balance.computer_slots);
        REQUIRE(warnings.size() == (with_slot ? 0U : 1U));
        REQUIRE(sim::valid(balance));
        g.world.set_balance(balance);
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s, 60'000);
        const auto view = s.computers().view();
        REQUIRE(!view.empty());
        REQUIRE(!view[0].plans.empty());
        Planned out{view[0].plans[0].flags, s.computers().state_version()};
        const auto bytes = saved(s.computers());
        sim::ComputerPlayers copy(s);
        core::ByteReader in(bytes);
        out.kept = copy.load(in, out.version) && in.at_end() && same(copy.view(), view);
        return out;
    };
    using sim::ai::city_flag::own_public;
    CHECK((planned(false, 0).flags & own_public(0)) == 0);
    const Planned first = planned(true, 0);
    CHECK((first.flags & own_public(0)) != 0);
    CHECK(first.version == 5);  // within 32 bits: the bytes of version 5
    CHECK(first.kept);
    const Planned far = planned(true, 30);
    CHECK(own_public(30) > 0xFFFF'FFFFU);
    CHECK((far.flags & own_public(30)) != 0);
    CHECK(far.version == sim::ComputerPlayers::max_state_version());
    CHECK(far.kept);
}

TEST_CASE("ai: when a city moves on to the next stage comes from the game's balance ([computer_stages])") {
    // A city founded moves on to the pioneers with two inhabitants, by default; a package may ask
    // for more.
    const auto max_stage = [](std::uint32_t pioneers_at) {
        Game g;
        sim::Balance balance;
        balance.ai_pioneers_inhabitants = pioneers_at;
        g.world.set_balance(balance);
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        sim::City* city = nullptr;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                city = g.world.city(c.island, c.index);
            }
        }
        REQUIRE(city != nullptr);
        for (std::uint32_t t = 0; t < 120'000; t += 10'000) {
            city->inhabitants.assign(1, 10);
            run(s, 10'000);
        }
        const auto view = s.computers().view();
        REQUIRE(!view.empty());
        return view.front().max_stage;
    };
    CHECK(max_stage(2) >= 0x14);  // the pioneers (stage 0x14) or later
    CHECK(max_stage(11) == 0);    // ten inhabitants are not enough: still a city just founded
}

TEST_CASE("ai: improved computers settle on a large island another computer holds, the original's do not") {
    // The big island counts as one of the largest (a template of size class 4); a second computer
    // has a young city there (fewer than 50 inhabitants) and two more on islets, so that the island
    // is open to others.
    constexpr sim::PlayerId kOther = 4;
    const auto founded = [&](bool classic, sim::PlayerRole other = sim::PlayerRole::Computer) {
        Game g;
        g.add_islet(2, 80);
        g.world.island(1).header.template_number = 1;
        g.world.island(1).header.template_size = 4;
        std::vector<sim::Player> players = g.world.players();
        players.push_back(sim::Player{});
        players.back().role = other;
        g.world.set_players(players);
        const auto add_city = [&](std::uint16_t island, sim::PlayerId owner, std::uint32_t people) {
            sim::City c;
            c.island = island;
            c.owner = owner;
            c.inhabitants = {people};
            g.world.add_city(c);
        };
        add_city(1, kOther, 20);
        add_city(0, kOther, 20);
        add_city(2, kOther, 20);
        sim::Rules rules;
        rules.bugs.classic_ai = classic;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        return run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; });
    };
    CHECK(founded(false));
    CHECK(!founded(true));
    // A human's city there the original's computers settle beside.
    CHECK(founded(true, sim::PlayerRole::Human));
}
