// SPDX-License-Identifier: GPL-3.0-or-later
// Pirates and natives.

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/combat.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/military.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kTon = 32;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20103\n  Kind: 35\n  Gfx: 2\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Maxware: 6\n  EndObj\n"  // Kontor
        "  @Nummer: 2\n  Id: 20104\n  Kind: 14\n  Gfx: 6\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 15\n    Maxware: 8\n    Prodmenge: 1\n    Figurnr: 33\n    Nativflg: 1\n"
        "  EndObj\n"                                                       // natives' warrior hut
        "  @Nummer: 3\n  Id: 20401\n  Kind: 1\n  Gfx: 10\n  Size: 1, 1\n"  // road
        "EndObj\n",
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
        "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
        "  Nummer: 31\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 5\n  Maxkanon: 10\n  Speed: 450\n"
        "  Drehtime: 0.2\n  Maxenergy: 95\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
        "  Nummer: 33\n  Gfx: 0\n  Blocknr: 1\n  Speed: 280\n  Shotradius: 1\n  Worktime: 0.8\n  Shottime: 0.6\n"
        "  Maxenergy: 18\n  Hitpoint: 1.2\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// Sea of 120 x 60 tiles; an island (12 x 10 at 60, 20) with the pirates' nest (Kontor at its west
/// coast). Players: 0 human, 1 computer, 2 pirates, 3 natives.
struct World {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{120, 60};
    sim::Rules rules = sim::Rules::open1602();
    std::unique_ptr<sim::Fleet> fleet;
    std::unique_ptr<sim::Combat> combat;
    std::unique_ptr<sim::Military> military;
    std::vector<sim::ShipEvent> events;

    explicit World(bool nest = true) {
        std::vector<sim::Player> players(4);
        players[0].role = sim::PlayerRole::Human;
        players[0].money = 20'000;
        players[1].role = sim::PlayerRole::Computer;
        sim::make_neutral(players[2], sim::faction::kPirates);
        sim::make_neutral(players[3], sim::faction::kNatives);
        world.set_players(players);
        sim::IslandMap map(12, 10);
        for (std::int32_t y = 0; y < 10; ++y) {
            for (std::int32_t x = 0; x < 12; ++x) {
                REQUIRE(map.place(*buildings.by_id(20101), x, y, 0, 0).ok());
            }
        }
        if (nest) {
            REQUIRE(map.place(*buildings.by_id(20103), 0, 4, 0, 0, 2, 0).ok());
            for (std::uint32_t y = 0; y < 10; ++y) {
                for (std::uint32_t x = 0; x < 12; ++x) {
                    map.set_city(x, y, 0, 2);
                }
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 60, 20}).ok());
        if (nest) {
            sim::City city;
            city.island = 0;
            city.index = 0;
            city.owner = 2;
            city.warehouse_x = 0;
            city.warehouse_y = 4;
            city.capacity = 30 * kTon;
            city.ensure_ware(24);
            city.trade.resize(25);
            world.add_city(city);
        }
    }

    void start() {
        fleet = std::make_unique<sim::Fleet>(world, buildings, figures, rules);
        combat = std::make_unique<sim::Combat>(world, buildings, figures);
        military = std::make_unique<sim::Military>(world, buildings, figures, rules, *combat);
        military->set_fleet(fleet.get());
        fleet->set_combat(combat.get());
    }

    void run(std::uint32_t ms) {
        std::vector<sim::ConstructionEvent> construction;
        for (std::uint32_t t = 0; t < ms; t += 50) {
            world.set_time_ms(world.time_ms() + 50);
            combat->update();
            fleet->tick(50);
            military->tick(50, &construction);
            for (const auto& e : fleet->take_events()) {
                events.push_back(e);
            }
            (void)military->take_events();
        }
    }

    [[nodiscard]] std::size_t count(sim::ShipEvent::Kind kind) const {
        return static_cast<std::size_t>(
            std::count_if(events.begin(), events.end(), [&](const auto& e) { return e.kind == kind; }));
    }
    [[nodiscard]] std::size_t pirate_ships() const {
        return static_cast<std::size_t>(
            std::count_if(world.ships().begin(), world.ships().end(), [](const sim::Ship& s) { return s.owner == 2; }));
    }
};

}  // namespace

TEST_CASE("pirates: they come once a player has 200 citizens, one ship at a time, armed by difficulty") {
    World w;
    w.world.set_game_settings(sim::World::GameSettings{2, 0, 1500});
    w.start();
    w.run(60'000);
    CHECK(w.pirate_ships() == 0);               // nobody has reached the citizens
    w.world.player(0)->infra_mask |= 1U << 24;  // citizens: two pirate ships at most
    for (int i = 0; i < 600 && w.pirate_ships() == 0; ++i) {
        w.run(50);
    }
    REQUIRE(w.pirate_ships() == 1);
    CHECK(w.count(sim::ShipEvent::Kind::PirateAppeared) == 1);
    const sim::Ship* pirate = nullptr;
    for (const sim::Ship& s : w.world.ships()) {
        pirate = s.owner == 2 ? &s : pirate;
    }
    REQUIRE(pirate != nullptr);
    // Appeared at an edge of the map; the next step arms it: difficulty 2, two ships: 4 cannons.
    CHECK((pirate->x == 0 || pirate->y == 0 || pirate->x == 119 || pirate->y == 59));
    w.run(20'000);
    pirate = nullptr;
    for (const sim::Ship& s : w.world.ships()) {
        pirate = s.owner == 2 ? &s : pirate;
    }
    REQUIRE(pirate != nullptr);
    CHECK(pirate->cannons == 4);
    // The next one comes 1500 s later.
    w.run(600'000);
    CHECK(w.pirate_ships() == 1);
    w.run(1'000'000);
    CHECK(w.pirate_ships() == 2);
}

TEST_CASE("pirates: none with the setting no pirates") {
    World w;
    w.world.set_game_settings(sim::World::GameSettings{2, 1, 1500});
    w.world.player(0)->infra_mask |= 1U << 30;
    w.start();
    w.run(120'000);
    CHECK(w.pirate_ships() == 0);
}

TEST_CASE("pirates: the rule switches them on against the setting, or off") {
    // The endless games of the two easy levels have the setting "no pirates" (newgame.md).
    sim::World::GameSettings none{1, 1, 1500};
    sim::World::GameSettings some{2, 0, 1500};
    sim::World world{10, 10};
    sim::Rules rules;
    world.set_game_settings(none);
    CHECK(!sim::pirates_enabled(world, rules));
    rules.pirates = sim::Pirates::On;
    CHECK(sim::pirates_enabled(world, rules));
    world.set_game_settings(some);
    rules.pirates = sim::Pirates::Game;
    CHECK(sim::pirates_enabled(world, rules));
    rules.pirates = sim::Pirates::Off;
    CHECK(!sim::pirates_enabled(world, rules));

    // "On" in a game without them: they come as on the harder levels.
    World on;
    on.rules.pirates = sim::Pirates::On;
    on.world.set_game_settings(none);
    on.world.player(0)->infra_mask |= 1U << 24;
    on.start();
    on.run(30'000);
    CHECK(on.pirate_ships() == 1);
    // "Off" in a game with them: none come.
    World off;
    off.rules.pirates = sim::Pirates::Off;
    off.world.set_game_settings(some);
    off.world.player(0)->infra_mask |= 1U << 30;
    off.start();
    off.run(120'000);
    CHECK(off.pirate_ships() == 0);

    // The choice stays with the savegame.
    for (const sim::Pirates choice : {sim::Pirates::Game, sim::Pirates::Off, sim::Pirates::On}) {
        sim::Rules saved;
        saved.pirates = choice;
        off.world.set_saved_rules(saved);
        auto bytes = sim::write_game(off.world, off.buildings);
        REQUIRE(bytes.ok());
        auto back = sim::read_game(*bytes, off.buildings);
        REQUIRE(back.ok());
        REQUIRE(back->saved_rules().has_value());
        CHECK(back->saved_rules()->pirates == choice);
    }
}

TEST_CASE("pirates: one nest for every four players begun, each with the original's ships") {
    // Two players: one nest either way, as in the original.
    World w;
    CHECK(sim::pirate_nests(w.world, w.rules) == 1);
    // Seven players (slots 4 to 8 computers): two nests, unless the rule keeps the original's one.
    const auto seven = [](World& world) {
        std::vector<sim::Player> players = world.world.players();
        players.resize(9);
        for (std::size_t i = 4; i < players.size(); ++i) {
            players[i].role = sim::PlayerRole::Computer;
        }
        world.world.set_players(players);
    };
    seven(w);
    CHECK(sim::pirate_nests(w.world, w.rules) == 2);
    sim::Rules one;
    one.pirate_nests = sim::PirateNests::One;
    CHECK(sim::pirate_nests(w.world, one) == 1);
    CHECK(sim::Rules::original().pirate_nests == sim::PirateNests::One);
    // The parts of the map are strips across its longer side.
    CHECK(sim::pirate_region(w.world, 0, 59, 2) == 0);
    CHECK(sim::pirate_region(w.world, 59, 0, 2) == 0);
    CHECK(sim::pirate_region(w.world, 60, 0, 2) == 1);
    CHECK(sim::pirate_region(w.world, 119, 59, 2) == 1);
    CHECK(sim::pirate_region(w.world, 119, 59, 1) == 0);

    // Citizens: two ships for each nest, the next one after half the interval.
    const auto fleet = [&](sim::PirateNests nests) {
        World game;
        seven(game);
        game.rules.pirate_nests = nests;
        game.world.set_game_settings(sim::World::GameSettings{2, 0, 1500});
        game.world.player(0)->infra_mask |= 1U << 24;
        game.start();
        game.run(800'000);
        const std::size_t early = game.pirate_ships();
        game.run(2'000'000);
        return std::pair{early, game.pirate_ships()};
    };
    using Counts = std::pair<std::size_t, std::size_t>;
    CHECK(fleet(sim::PirateNests::Scaled) == Counts(2, 4));
    CHECK(fleet(sim::PirateNests::One) == Counts(1, 2));

    // The choice stays with the savegame.
    for (const sim::PirateNests choice : {sim::PirateNests::One, sim::PirateNests::Scaled}) {
        sim::Rules saved;
        saved.pirate_nests = choice;
        w.world.set_saved_rules(saved);
        auto bytes = sim::write_game(w.world, w.buildings);
        REQUIRE(bytes.ok());
        auto back = sim::read_game(*bytes, w.buildings);
        REQUIRE(back.ok());
        REQUIRE(back->saved_rules().has_value());
        CHECK(back->saved_rules()->pirate_nests == choice);
    }
}

TEST_CASE("factions: the pirates' faction says how their nests grow, and the game keeps it") {
    World w;
    std::vector<sim::Player> players = w.world.players();
    players.resize(9);
    for (std::size_t i = 4; i < players.size(); ++i) {
        players[i].role = sim::PlayerRole::Computer;  // seven human and computer players
    }
    w.world.set_players(players);
    CHECK(sim::pirate_nests(w.world, w.rules) == 2);
    std::vector<sim::Faction> factions = sim::base_factions();
    factions[sim::faction::kPirates].nests.per = 2;
    w.world.set_factions(factions);
    CHECK(sim::pirate_nests(w.world, w.rules) == 4);
    sim::Rules one;
    one.pirate_nests = sim::PirateNests::One;
    CHECK(sim::pirate_nests(w.world, one) == 1);

    auto bytes = sim::write_game(w.world, w.buildings);
    REQUIRE(bytes.ok());
    auto back = sim::read_game(*bytes, w.buildings);
    REQUIRE(back.ok());
    CHECK(back->factions() == factions);
    CHECK(back->players()[2].faction == sim::faction::kPirates);
    CHECK(back->players()[2].role == sim::PlayerRole::Neutral);
    CHECK(back->players()[3].faction == sim::faction::kNatives);
    CHECK(back->players()[1].role == sim::PlayerRole::Computer);
    CHECK(sim::pirate_nests(*back, w.rules) == 4);
}

TEST_CASE("factions: scripted pirates get no ships from the game, and the savegame keeps them scripted") {
    World w;
    w.world.set_game_settings(sim::World::GameSettings{2, 0, 1500});
    std::vector<sim::Faction> factions = sim::base_factions();
    factions[sim::faction::kPirates].scripted = true;
    w.world.set_factions(factions);
    w.start();
    w.world.player(0)->infra_mask |= 1U << 24;  // citizens: the pirates would come now
    w.run(60'000);
    CHECK(w.pirate_ships() == 0);
    CHECK(w.count(sim::ShipEvent::Kind::PirateAppeared) == 0);
    // Their relations stay the pirates': at war with the settlers.
    CHECK(w.world.acts_as(2, sim::FactionBehaviour::Pirates));
    CHECK(w.world.diplomacy().relation(0, 2).peace == sim::Treaty::None);

    auto bytes = sim::write_game(w.world, w.buildings);
    REQUIRE(bytes.ok());
    auto back = sim::read_game(*bytes, w.buildings);
    REQUIRE(back.ok());
    CHECK(back->factions() == factions);
    CHECK(back->factions()[sim::faction::kPirates].scripted);
}

TEST_CASE("free trader: the reach of the rule stays with the savegame") {
    World w;
    for (const sim::TraderReach choice : {sim::TraderReach::Original, sim::TraderReach::Scaled}) {
        sim::Rules saved;
        saved.trader_reach = choice;
        w.world.set_saved_rules(saved);
        auto bytes = sim::write_game(w.world, w.buildings);
        REQUIRE(bytes.ok());
        auto back = sim::read_game(*bytes, w.buildings);
        REQUIRE(back.ok());
        REQUIRE(back->saved_rules().has_value());
        CHECK(back->saved_rules()->trader_reach == choice);
    }
}

TEST_CASE("pirates: a surrendered ship is plundered and the loot goes to the nest") {
    World w;
    w.start();
    w.world.diplomacy().set_peace(w.world, 2, 1, sim::Treaty::None, 0);
    sim::Ship pirate = sim::make_ship("Pirat", 40, 25, 31, 2, 2);
    pirate.cargo.resize(5);
    pirate.cannons = 6;
    w.world.add_ship(pirate);
    sim::Ship merchant = sim::make_ship("Kauffahrer", 44, 25, 21, 1, 0);
    merchant.cargo.resize(4);
    merchant.cargo[0] = sim::CargoSlot{21, 40 * kTon, 0};  // jewellery: worth taking
    merchant.cargo[1] = sim::CargoSlot{8, 40 * kTon, 0};   // grain: not worth it
    w.world.add_ship(merchant);
    const std::uint32_t victim = w.world.ships().back().id;
    w.run(120'000);
    CHECK(w.count(sim::ShipEvent::Kind::WhiteFlag) == 1);
    CHECK(w.count(sim::ShipEvent::Kind::Plundered) == 1);
    const sim::Ship* m = w.world.ship(victim);
    REQUIRE(m != nullptr);
    CHECK(m->load(21) == 0);
    CHECK(m->load(8) == 40 * kTon);
    CHECK(w.world.city(0, 0)->stock(21) == 30 * kTon);  // the nest's room is 30 t
    CHECK(!w.world.diplomacy().hostile(2, 1));
}

TEST_CASE("pirates: the nest offers its six most valuable goods it holds at 40 % of the base price") {
    // Base price times stock, the highest first, as many as the Kontor's Maxware (6);
    // a ware the nest does not hold is never offered and what is not picked is switched off.
    World w;
    sim::City& nest = *w.world.city(0, 0);
    const std::pair<std::uint8_t, std::uint32_t> stock[] = {{assets::ware::kJewellery, 5}, {assets::ware::kTools, 10},
                                                            {assets::ware::kWood, 40},     {assets::ware::kFood, 20},
                                                            {assets::ware::kSwords, 2},    {assets::ware::kClothes, 1},
                                                            {assets::ware::kBricks, 3},    {assets::ware::kSugar, 1}};
    for (const auto& [ware, tons] : stock) {
        nest.goods[ware] = tons * kTon;
    }
    nest.trade[assets::ware::kBricks].sell = true;
    nest.trade[assets::ware::kGold].buy = true;
    sim::pirate_nest_offer(w.world, w.buildings, nest);
    for (const std::uint8_t ware : {assets::ware::kJewellery, assets::ware::kTools, assets::ware::kWood,
                                    assets::ware::kFood, assets::ware::kSwords, assets::ware::kClothes}) {
        CHECK(nest.trade[ware].sell);
        CHECK(nest.trade[ware].keep == 0);
    }
    CHECK(nest.trade[assets::ware::kJewellery].sell_price == 358);
    CHECK(nest.trade[assets::ware::kTools].sell_price == 55);
    CHECK(!nest.trade[assets::ware::kBricks].sell);
    CHECK(!nest.trade[assets::ware::kSugar].sell);
    CHECK(!nest.trade[assets::ware::kGold].buy);
    // With only two goods in store, only those two.
    for (const auto& [ware, tons] : stock) {
        nest.goods[ware] = 0;
    }
    nest.goods[assets::ware::kCloth] = 4 * kTon;
    nest.goods[assets::ware::kOre] = 1 * kTon;
    sim::pirate_nest_offer(w.world, w.buildings, nest);
    std::size_t offered = 0;
    for (const sim::City::Trade& t : nest.trade) {
        offered += t.sell ? 1U : 0U;
    }
    CHECK(offered == 2);
    CHECK(nest.trade[assets::ware::kCloth].sell);
    CHECK(nest.trade[assets::ware::kOre].sell);
}

TEST_CASE("pirates: a surrender without loot pleases them too, so they stay at peace") {
    World w;
    w.start();
    w.world.diplomacy().set_peace(w.world, 2, 0, sim::Treaty::None, 0);
    w.world.diplomacy().set_peace(w.world, 2, 1, sim::Treaty::None, 0);
    sim::Ship pirate = sim::make_ship("Pirat", 40, 25, 31, 2, 2);
    pirate.cargo.resize(5);
    pirate.cannons = 6;
    w.world.add_ship(pirate);
    sim::Ship merchant = sim::make_ship("Kauffahrer", 44, 25, 21, 1, 0);
    merchant.cargo.resize(4);
    merchant.cargo[0] = sim::CargoSlot{8, 40 * kTon, 0};  // grain: not worth taking
    w.world.add_ship(merchant);
    const std::uint32_t victim = w.world.ships().back().id;
    w.run(60'000);
    CHECK(w.count(sim::ShipEvent::Kind::WhiteFlag) == 1);
    CHECK(w.count(sim::ShipEvent::Kind::Plundered) == 1);
    const sim::Ship* m = w.world.ship(victim);
    REQUIRE(m != nullptr);
    CHECK(m->load(8) == 40 * kTon);
    // The pirates' next look keeps the peace with the owner and makes it with every other player
    // (event 9 to all, 6 to the owner); the ship is not hunted again.
    sim::pirates_reconsider(w.world);
    CHECK(!w.world.diplomacy().hostile(2, 1));
    CHECK(!w.world.diplomacy().hostile(2, 0));
    w.run(60'000);
    CHECK(w.count(sim::ShipEvent::Kind::WhiteFlag) == 1);
    CHECK(w.world.ship(victim) != nullptr);
}

TEST_CASE("pirates: at war from the start, they go after a human's ship on their own") {
    // Feedback #17: nobody did anything to the pirates, yet once a player has citizens they come and
    // hunt a passive human's ship (mood 0, original and History Edition: no message, no event).
    World w;
    w.world.set_game_settings(sim::World::GameSettings{1, 0, 1500});
    w.start();
    sim::Ship ship = sim::make_ship("Bote", 20, 40, 21, 0);
    ship.cargo.resize(4);
    ship.health = 50 * 32;
    w.world.add_ship(ship);
    const std::uint32_t mine = w.world.ships().back().id;
    w.world.player(0)->infra_mask |= 1U << 24;
    CHECK(w.world.diplomacy().hostile(2, 0));
    CHECK(w.world.diplomacy().mood(w.world, 2, 0, 0, w.world.time_ms()) == 0);
    std::uint32_t minutes = 0;
    for (; minutes < 60; ++minutes) {
        w.run(60'000);
        const sim::Ship* s = w.world.ship(mine);
        if (s == nullptr || s->health < 50 * 32) {
            break;
        }
    }
    CHECK(minutes < 10);
}

TEST_CASE("pirates: a ship under the white flag is no prey, and a player with no other is left alone") {
    // The original counts only ships that have not surrendered, as the History Edition.
    for (const bool white_flag : {false, true}) {
        World w;
        w.world.set_game_settings(sim::World::GameSettings{1, 0, 1500});
        w.start();
        sim::Ship ship = sim::make_ship("Bote", 20, 40, 21, 0);
        ship.cargo.resize(4);
        ship.health = 50 * kTon;
        ship.held = white_flag;
        w.world.add_ship(ship);
        w.world.player(0)->infra_mask |= 1U << 24;
        const sim::Target prey = sim::Target::position(20, 40);
        bool hunted = false;
        for (std::uint32_t t = 0; t < 20 * 60'000 && !hunted; t += 50) {
            w.run(50);
            for (const sim::Ship& s : w.world.ships()) {
                hunted = hunted || (s.owner == 2 && s.target == prey);
            }
        }
        CHECK(hunted == !white_flag);
    }
}

TEST_CASE("pirates: their mood decides war and peace; buying peace at their Kontor") {
    World w;
    w.start();
    CHECK(w.world.diplomacy().hostile(2, 0));
    sim::pirates_reconsider(w.world);
    CHECK(w.world.diplomacy().hostile(2, 0));
    // Trade with them makes friends.
    w.world.diplomacy().add_trade(2, 0, 2000);
    sim::pirates_reconsider(w.world);
    CHECK(!w.world.diplomacy().hostile(2, 0));
    // Damage done to them makes enemies again.
    w.world.diplomacy().add_damage(2, 0, 10'000);
    sim::pirates_reconsider(w.world);
    CHECK(w.world.diplomacy().hostile(2, 0));
    // A ship at the nest buys peace: 20 000 gold -> base 1500 (2000 above 20 000), mood below 0.
    sim::Ship ship = sim::make_ship("Bote", 58, 24, 21, 0);
    ship.cargo.resize(4);
    w.world.add_ship(ship);
    const std::uint32_t price = sim::pirate_peace_price(w.world, 2, 0);
    CHECK(price >= 1500);
    sim::DiplomacyOrder order;
    order.player = 0;
    order.kind = sim::DiplomacyOrder::Kind::BuyPeace;
    order.ship = w.world.ships().back().id;
    REQUIRE(sim::apply_diplomacy(w.world, order));
    CHECK(w.world.players()[0].money == static_cast<std::int32_t>(20'000 - price));
    CHECK(!w.world.diplomacy().hostile(2, 0));
    // Not from far away.
    w.world.ships().back().place(10, 10);
    CHECK(!sim::apply_diplomacy(w.world, order));
}

namespace {

/// A natives' village (player 3) on the island with a warrior hut at (5, 4) holding eight trained
/// spearmen; `roads` are island tiles of road with their owner, placed before the territory.
struct Road {
    std::int32_t x;
    std::int32_t y;
    sim::PlayerId owner;
};
void add_village(World& w, std::initializer_list<Road> roads, bool trained = true) {
    sim::IslandMap& map = w.world.island(0).map;
    REQUIRE(map.place(*w.buildings.by_id(20104), 5, 4, 0, 0, 3, 0).ok());
    for (const Road& r : roads) {
        REQUIRE(map.place(*w.buildings.by_id(20401), r.x, r.y, 0, 0).ok());
    }
    for (std::uint32_t y = 0; y < 10; ++y) {
        for (std::uint32_t x = 0; x < 12; ++x) {
            map.set_city(x, y, 0, 3);
        }
    }
    for (const Road& r : roads) {
        // Another player's road belongs to another city.
        map.set_city(static_cast<std::uint32_t>(r.x), static_cast<std::uint32_t>(r.y), r.owner == 3 ? 0 : 1, r.owner);
    }
    sim::City village;
    village.island = 0;
    village.index = 0;
    village.owner = 3;
    village.goodwill = {200, 200, 0, 0};
    village.capacity = 30 * kTon;
    village.ensure_ware(24);
    w.world.add_city(village);
    sim::CostBuilding hut{sim::CostBuilding::Kind::Castle, 0, 5, 4, true};
    for (int i = 0; i < 8 && trained; ++i) {
        hut.slots.push_back(sim::CastleSlot{33, false, true, 18 * 32, 320});
    }
    w.world.add_cost_building(hut);
    w.start();
}

}  // namespace

TEST_CASE("natives: goodwill fades towards 49; warriors come out of their huts") {
    World w(false);
    add_village(w, {{8, 5, 3}});

    std::vector<sim::CityEvent> city_events;
    for (int i = 0; i < 6; ++i) {
        sim::advance_cities(w.world, w.buildings, &city_events);
    }
    CHECK(w.world.city(0, 0)->goodwill[0] == 188);  // 200 * (127/128)^6, truncated each time
    for (int i = 0; i < 400; ++i) {
        sim::advance_cities(w.world, w.buildings, &city_events);
    }
    CHECK(w.world.city(0, 0)->goodwill[0] == 49);
    CHECK(w.world.city(0, 0)->goodwill[2] == 0);

    // Three warriors every third tick; the slots train new ones.
    for (int i = 0; i < 3; ++i) {
        w.military->natives_tick();
    }
    CHECK(w.world.soldiers().size() == 3);
    CHECK(w.world.cost_buildings().back().slots.size() == 8);
    CHECK(w.world.cost_buildings().back().slots.back().training);
    // After an attack (and not in the first 15 minutes): seven, every tick.
    w.world.set_time_ms(20 * 60'000);
    w.world.city(0, 0)->last_attacked_ms = w.world.time_ms() - 60'000;
    w.military->natives_tick();
    CHECK(w.world.soldiers().size() == 7);
}

TEST_CASE("natives: a warrior goes to the first road of the natives a search within 7 reaches") {
    // From the hut at (5, 4), over open ground; the road of another player next to the
    // hut is walked on but is no goal, the far road of the natives is reached after the near one.
    const auto targets = [] {
        World w(false);
        add_village(w, {{4, 4, 1}, {8, 5, 3}, {1, 9, 3}});
        const sim::Random before = w.world.random(sim::RandomStream::Military);
        for (int i = 0; i < 3; ++i) {
            w.military->natives_tick();
        }
        CHECK(w.world.random(sim::RandomStream::Military) == before);  // no random number drawn
        std::vector<sim::Target> t;
        for (const sim::Soldier& s : w.world.soldiers()) {
            t.push_back(s.target);
        }
        return t;
    };
    const std::vector<sim::Target> first = targets();
    REQUIRE(first.size() == 3);
    for (const sim::Target& t : first) {
        CHECK(t == sim::Target::tile_spot(60 + 8, 20 + 5));  // world half tiles of island tile (8, 5)
    }
    CHECK(targets() == first);
}

TEST_CASE("natives: without a road of theirs within 7 tiles no warrior comes out") {
    // Only roads of another player, one next to the hut: they are walked on but are no goal, and
    // no random tile stands in for the missing one.
    World w(false);
    add_village(w, {{4, 4, 1}, {9, 8, 1}});
    for (int i = 0; i < 6; ++i) {
        w.military->natives_tick();
    }
    CHECK(w.world.soldiers().empty());
    // Every place keeps its trained warrior.
    for (const sim::CastleSlot& slot : w.world.cost_buildings().back().slots) {
        CHECK(!slot.training);
    }
}

TEST_CASE("natives: the empty huts of a new game recruit warriors, who then come out") {
    // The village templates bring their huts without warriors; every place the tick looks at
    // recruits one, as in the original, and the trained ones go out later.
    World w(false);
    add_village(w, {{8, 5, 3}}, false);
    w.military->natives_tick();
    w.military->natives_tick();
    CHECK(w.world.cost_buildings().back().slots.empty());  // every 30 s only
    w.military->natives_tick();
    const std::vector<sim::CastleSlot>& slots = w.world.cost_buildings().back().slots;
    REQUIRE(slots.size() == 8);
    for (const sim::CastleSlot& slot : slots) {
        CHECK(slot.figure == 33);
        CHECK(slot.training);
    }
    CHECK(w.world.soldiers().empty());
    // Trained in the hut, three of them go out.
    int ticks = 0;
    for (; ticks < 300 && w.world.soldiers().size() < 3; ++ticks) {
        w.run(10'000);
        w.military->natives_tick();
    }
    CHECK(ticks > 3);  // not before they are trained
    CHECK(w.world.soldiers().size() == 3);
    for (const sim::Soldier& s : w.world.soldiers()) {
        CHECK(s.owner == 3);
    }
}

TEST_CASE("pirates: random places at sea are clamped to the map, as in the original") {
    // 90 tiles around a ship on a map 60 tiles high: most draws fall outside and are clamped to
    // the first or last row instead of being thrown away.
    World w;
    w.world.set_game_settings(sim::World::GameSettings{2, 0, 1500});
    w.world.player(0)->infra_mask |= 1U << 24;
    w.start();
    std::vector<sim::Target> targets;
    for (int i = 0; i < 4000 && targets.size() < 12; ++i) {
        w.run(500);
        for (const sim::Ship& s : w.world.ships()) {
            if (s.owner == 2 && s.target.kind == sim::Target::Kind::Position &&
                (targets.empty() || !(targets.back() == s.target))) {
                targets.push_back(s.target);
            }
        }
    }
    REQUIRE(targets.size() >= 4);
    std::size_t on_edge = 0;
    for (const sim::Target& t : targets) {
        CHECK(t.x < 120);
        CHECK(t.y < 60);
        on_edge += t.y == 0 || t.y == 59 ? 1U : 0U;
    }
    CHECK(on_edge >= 2);
}

TEST_CASE("natives: trading with them is limited by goodwill for every player, the fifth too") {
    World w;
    // Eight players, so that the fifth player has the id 7 (player_slot(4)).
    std::vector<sim::Player> players = w.world.players();
    players.resize(8);
    for (std::size_t p = 4; p < 7; ++p) {
        players[p].role = sim::PlayerRole::Neutral;  // the faction slots between
    }
    w.world.set_players(players);
    // The nest becomes a natives' village selling and buying tobacco goods for goodwill.
    constexpr std::uint8_t kWare = 15;
    sim::City& village = *w.world.city(0, 0);
    village.owner = 3;
    village.goods[kWare] = 20 * kTon;
    village.trade[kWare].sell = true;
    village.trade[kWare].buy = true;
    village.trade[kWare].limit = 30 * kTon;
    village.goodwill = {200, 200, 0, 0};  // as the original's records: no entry for id 7
    for (const sim::PlayerId p : {sim::PlayerId{0}, sim::PlayerId{7}}) {
        w.world.diplomacy().relation(p, 3).trade = sim::Treaty::Signed;
        w.world.diplomacy().relation(3, p).trade = sim::Treaty::Signed;
    }
    w.start();
    const auto ship_of = [&](sim::PlayerId owner) {
        sim::Ship s = sim::make_ship("Bote", 58, 24, 21, owner);
        s.cargo.resize(4);
        w.world.add_ship(s);
        return w.world.ships().back().id;
    };
    const std::uint32_t first = ship_of(0);
    const std::uint32_t fifth = ship_of(7);
    const auto buy = [&](std::uint32_t ship, sim::PlayerId owner) {
        return w.fleet->transfer(sim::CargoTransfer{owner, ship, kWare, true, 20 * kTon, 0xFF});
    };
    const auto per_ton = std::max<std::uint32_t>(1, sim::base_price(kWare) * 42U / 128U);
    const std::uint32_t limit = 200 * kTon / per_ton / kTon * kTon;
    REQUIRE(limit > 0);
    REQUIRE(limit < 20 * kTon);

    // Without an entry the fifth player gets nothing, never everything.
    CHECK(buy(fifth, 7) == 0);
    // Each player starts with the same goodwill.
    sim::fit_goodwill(w.world);
    REQUIRE(village.goodwill.size() == 8);
    CHECK(village.goodwill[7] == sim::kNativeGoodwillStart);
    CHECK(village.goodwill[4] == 0);  // a faction, no player
    CHECK(buy(first, 0) == limit);
    CHECK(buy(fifth, 7) == limit);
    CHECK(village.goodwill[0] == village.goodwill[7]);
    CHECK(village.goodwill[0] < 200);
    // Used up: no more.
    CHECK(buy(first, 0) == 0);
    // Selling to them earns goodwill, the base price a ton.
    const std::uint32_t before = village.goodwill[7];
    const std::uint32_t sold = w.fleet->transfer(sim::CargoTransfer{7, fifth, kWare, false, limit, 0xFF});
    CHECK(sold == limit);
    CHECK(village.goodwill[7] == before + sim::base_price(kWare) * sold / kTon);

    // Goodwill stays below its cap and never wraps (a crafted save).
    village.goodwill[0] = 4'000'000'000U;
    sim::fit_goodwill(w.world);
    CHECK(village.goodwill[0] == sim::kMaxNativeGoodwill);
    CHECK(w.fleet->transfer(sim::CargoTransfer{0, first, kWare, false, limit, 0xFF}) == limit);
    CHECK(village.goodwill[0] == sim::kMaxNativeGoodwill);
}

TEST_CASE("discovery: the setting shows the towns of players, or also natives and pirates, from the start") {
    World w;  // island 0: the pirates' nest
    // Island 1: a native village, island 2: a computer player's town, island 3: nobody's.
    for (std::uint32_t i = 1; i <= 3; ++i) {
        sim::IslandMap map(12, 10);
        for (std::int32_t y = 0; y < 10; ++y) {
            for (std::int32_t x = 0; x < 12; ++x) {
                REQUIRE(map.place(*w.buildings.by_id(20101), x, y, 0, 0).ok());
            }
        }
        assets::IslandHeader header;
        header.number = static_cast<std::uint16_t>(i);
        REQUIRE(w.world.add_island(sim::WorldIsland{header, std::move(map), 4 + 14 * (i - 1), 40}).ok());
    }
    for (const auto& [island, owner] : {std::pair{1, 3}, std::pair{2, 1}}) {
        sim::City city;
        city.island = static_cast<std::uint16_t>(island);
        city.owner = static_cast<sim::PlayerId>(owner);
        w.world.add_city(city);
    }
    const auto seen = [&](std::uint16_t island) {
        const sim::WorldIsland& isl = w.world.islands()[island];
        return isl.discovered_by(0) && isl.discovered_by(1);
    };
    // Nothing, and the original at the start: every island by ship.
    w.world.reveal_islands(sim::Reveal::None);
    w.world.reveal_islands(sim::Reveal::Original);
    CHECK(!seen(0));
    CHECK(!seen(1));
    CHECK(!seen(2));
    // Nothing also when a town is founded.
    w.world.reveal_islands(sim::Reveal::None, 2);
    CHECK(!seen(2));
    // As the original, a town founded by a player: its island, not the natives' or the pirates'.
    w.world.reveal_islands(sim::Reveal::Original, 2);
    w.world.reveal_islands(sim::Reveal::Original, 1);
    w.world.reveal_islands(sim::Reveal::Original, 0);
    CHECK(!seen(0));
    CHECK(!seen(1));
    CHECK(seen(2));
    CHECK(!seen(3));
    // Everything: the natives and the pirates too, but no island without a town.
    w.world.reveal_islands(sim::Reveal::All, 1);  // one island only
    CHECK(!seen(0));
    CHECK(seen(1));
    w.world.reveal_islands(sim::Reveal::All);
    CHECK(seen(0));
    CHECK(!seen(3));
    // Discovering makes no message of a ship.
    CHECK(w.count(sim::ShipEvent::Kind::NativesFound) == 0);

    // The choice stays with the savegame.
    for (const sim::Reveal choice : {sim::Reveal::None, sim::Reveal::Original, sim::Reveal::All}) {
        sim::Rules saved;
        saved.reveal = choice;
        w.world.set_saved_rules(saved);
        auto bytes = sim::write_game(w.world, w.buildings);
        REQUIRE(bytes.ok());
        auto back = sim::read_game(*bytes, w.buildings);
        REQUIRE(back.ok());
        REQUIRE(back->saved_rules().has_value());
        CHECK(back->saved_rules()->reveal == choice);
        CHECK(back->islands()[1].discovered_by(0));  // and the islands it showed
        CHECK(!back->islands()[3].discovered_by(0));
    }
    // A game starts with them shown; the simulation of every computer does the same.
    World game;
    sim::Rules rules;
    rules.reveal = sim::Reveal::All;
    const sim::Simulation s(game.world, game.buildings, game.figures, rules);
    CHECK(game.world.islands()[0].discovered_by(0));
}

TEST_CASE("factions: blocked slots of a multiplayer game still count for the pirates' nests") {
    // A slot blocked in the lobby: nobody plays it, but the world was made for it, and what grows
    // with the players grows with it, also once the game is saved and loaded.
    World w;
    std::vector<sim::Player> players = w.world.players();
    players.resize(9);
    for (std::size_t i = 4; i < players.size(); ++i) {
        players[i].role = sim::PlayerRole::Computer;
        players[i].status = sim::PlayerStatus::Absent;
    }
    w.world.set_players(players);
    CHECK(sim::pirate_nests(w.world, w.rules) == 2);

    auto bytes = sim::write_game(w.world, w.buildings);
    REQUIRE(bytes.ok());
    auto back = sim::read_game(*bytes, w.buildings);
    REQUIRE(back.ok());
    CHECK(back->players()[5].status == sim::PlayerStatus::Absent);
    CHECK(sim::pirate_nests(*back, w.rules) == 2);
}

TEST_CASE("natives: warriors roam around the place they went to, on road and ground") {
    // A wander target is a tile of road or ground, which is no building: as a building target it had
    // no cells, every way failed and the warriors stood in a heap on their road (Windows feedback).
    World w(false);
    add_village(w, {{8, 5, 3}});
    for (int i = 0; i < 3; ++i) {
        w.military->natives_tick();
    }
    REQUIRE(w.world.soldiers().size() == 3);
    std::vector<std::pair<std::int32_t, std::int32_t>> seen;
    for (int step = 0; step < 60; ++step) {
        w.run(1000);
        for (const sim::Soldier& s : w.world.soldiers()) {
            const std::pair<std::int32_t, std::int32_t> c{s.cell_x(), s.cell_y()};
            if (std::find(seen.begin(), seen.end(), c) == seen.end()) {
                seen.push_back(c);
            }
            // Within the wander range of the road (world tile 68, 25 = cells 136, 50).
            CHECK(std::abs(c.first - 136) <= 2 * 9 + 1);
            CHECK(std::abs(c.second - 50) <= 2 * 9 + 1);
        }
    }
    CHECK(seen.size() > 20);
}

namespace {

/// A pirate at war with the human player 0 near the human's empty ship, which raises the white flag.
struct Surrender {
    World w{false};  // no nest: the surrender does not please the pirates, war comes back at once
    std::uint32_t pirate = 0;
    std::uint32_t victim = 0;

    Surrender() {
        w.start();
        w.world.diplomacy().set_peace(w.world, 2, 0, sim::Treaty::None, 0);
        sim::Ship p = sim::make_ship("Pirat", 40, 25, 31, 2, 2);
        p.cargo.resize(5);
        p.cannons = 6;
        w.world.add_ship(p);
        pirate = w.world.ships().back().id;
        sim::Ship mine = sim::make_ship("Bote", 46, 25, 21, 0, 0);
        mine.cargo.resize(4);
        mine.health = 50 * 32;
        w.world.add_ship(mine);
        victim = w.world.ships().back().id;
        sim::ShipCommand flag;
        flag.player = 0;
        flag.ship = victim;
        flag.op = sim::ShipCommand::Op::WhiteFlag;
        REQUIRE(w.fleet->command(flag));
    }

    /// Runs with the pirates' look at war and peace every 5 s, as the game does.
    void run(std::uint32_t ms) {
        for (std::uint32_t t = 0; t < ms; t += 5'000) {
            w.run(5'000);
            sim::pirates_reconsider(w.world);
        }
    }
};

}  // namespace

TEST_CASE("pirates: a surrender is plundered once, then the pirate sails on, also at war again") {
    // Bug report: after the white flag the pirates came to the empty ship and stayed there. Without
    // a nest (or after enough damage) they are at war again within seconds and took the same ship
    // as their next prey over and over.
    Surrender s;
    s.run(30'000);
    CHECK(s.w.count(sim::ShipEvent::Kind::Plundered) == 1);
    CHECK(s.w.world.diplomacy().hostile(2, 0));  // at war again: nothing pleased them
    const sim::Ship* m = s.w.world.ship(s.victim);
    REQUIRE(m != nullptr);
    CHECK(m->held);
    CHECK(m->plundered);
    s.run(170'000);
    CHECK(s.w.count(sim::ShipEvent::Kind::Plundered) == 1);
    CHECK(s.w.count(sim::ShipEvent::Kind::Attacked) == 0);
    const sim::Ship* p = s.w.world.ship(s.pirate);
    REQUIRE(p != nullptr);
    CHECK(!(p->target == sim::Target::other_ship(s.victim)));
    // Under way to somewhere else, not lying at the ship.
    CHECK(!p->target.empty());

    // Sailing on strikes the flag: the ship is prey again, and a new surrender is plundered anew.
    sim::ShipCommand down;
    down.player = 0;
    down.ship = s.victim;
    down.op = sim::ShipCommand::Op::WhiteFlag;
    down.on = false;
    REQUIRE(s.w.fleet->command(down));
    sim::ShipCommand up = down;
    up.on = true;
    REQUIRE(s.w.fleet->command(up));
    CHECK(!s.w.world.ship(s.victim)->plundered);
}

TEST_CASE("pirates: the mark of a plundered surrender stays with the savegame") {
    Surrender s;
    s.run(30'000);
    REQUIRE(s.w.world.ship(s.victim)->plundered);
    auto bytes = sim::write_game(s.w.world, s.w.buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, s.w.buildings, nullptr, &s.w.figures);
    REQUIRE(loaded.ok());
    const sim::Ship* m = loaded->ship(s.victim);
    REQUIRE(m != nullptr);
    CHECK(m->held);
    CHECK(m->plundered);
}
