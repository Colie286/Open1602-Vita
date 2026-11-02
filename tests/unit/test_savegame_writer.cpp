// SPDX-License-Identifier: GPL-3.0-or-later
// The .GAM writer (savegame_writer.hpp) on a small synthetic world,
// without the original data: a world written and loaded again is the same world, written again it
// gives the same bytes, and the original's chunks alone (what the original keeps when it saves the
// game again) hold what the original knows.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_extension.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kSea = 21201;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kHouse = 20605;
constexpr std::uint32_t kWoodcutter = 21001;
constexpr std::uint32_t kToolmaker = 21002;
constexpr std::uint32_t kKontor = 22101;
constexpr std::uint8_t kTools = 22;
constexpr std::uint8_t kWood = 23;
constexpr std::uint16_t kShipFigure = 21;

constexpr sim::PlayerId kHuman = 0;
constexpr sim::PlayerId kComputer = 1;
constexpr sim::PlayerId kTrader = 2;
constexpr sim::PlayerId kPirates = 3;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"  // ground
        "  @Nummer: 1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"  // sea
        "  @Nummer: 2\n  Id: 20401\n  Kind: 1\n  Gfx: 2\n  Size: 1, 1\n"   // road
        "  @Nummer: 3\n  Id: 20605\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n  EndObj\n"
        "  @Nummer: 4\n  Id: 21001\n  Kind: 14\n  Gfx: 20\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 23\n  EndObj\n"
        "  @Nummer: 5\n  Id: 21002\n  Kind: 14\n  Gfx: 21\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 22\n  EndObj\n"
        "  @Nummer: 6\n  Id: 22101\n  Kind: 35\n  Gfx: 30\n  Size: 2, 3\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Maxlager: 30\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of `w` x `h` tiles of ground with a column of sea on its east side.
sim::WorldIsland make_island(const assets::BuildingTable& buildings, std::uint8_t number, std::uint32_t w,
                             std::uint32_t h, std::uint32_t x, std::uint32_t y) {
    sim::IslandMap map(w, h);
    for (std::int32_t ty = 0; ty < static_cast<std::int32_t>(h); ++ty) {
        for (std::int32_t tx = 0; tx < static_cast<std::int32_t>(w); ++tx) {
            const std::uint32_t id = tx + 1 == static_cast<std::int32_t>(w) ? kSea : kGround;
            REQUIRE(map.place(*buildings.by_id(id), tx, ty, 0, 0).ok());
        }
    }
    map.capture_ground();
    sim::WorldIsland island{assets::IslandHeader{}, std::move(map), x, y};
    island.header.number = number;
    island.header.width = static_cast<std::uint8_t>(w);
    island.header.height = static_cast<std::uint8_t>(h);
    island.header.climate = 1;
    island.header.fertility = 0x1181U | 0x40U;
    return island;
}

sim::City make_city(std::uint16_t island, sim::PlayerId owner, std::string name, std::uint16_t kx, std::uint16_t ky) {
    sim::City city;
    city.island = island;
    city.index = 0;
    city.owner = owner;
    city.name = std::move(name);
    city.warehouse_x = kx;
    city.warehouse_y = ky;
    return city;
}

/// The world of the original's size (500 x 350) with two settled islands: a human player and a
/// computer player with a city each (Kontor, road, house, two workshops), the free trader and the
/// pirates; ships with cargo, a trade route and orders; soldiers; houses and production; the
/// clocks and treaties the .GAM keeps.
sim::World make_world(const assets::BuildingTable& buildings) {
    sim::World world;
    {
        sim::WorldIsland island = make_island(buildings, 0, 30, 20, 100, 100);
        sim::IslandMap& map = island.map;
        REQUIRE(map.build(*buildings.by_id(kKontor), 2, 2, 0, 0, kHuman, 0).ok());
        REQUIRE(map.build(*buildings.by_id(kRoad), 5, 3, 1, 0, kHuman, 0).ok());
        REQUIRE(map.build(*buildings.by_id(kHouse), 6, 4, 0, 0, kHuman, 0).ok());
        REQUIRE(map.build(*buildings.by_id(kWoodcutter), 9, 4, 2, 0, kHuman, 0).ok());
        REQUIRE(map.build(*buildings.by_id(kToolmaker), 10, 4, 3, 0, kHuman, 0).ok());
        island.header.deposits.push_back({2, 7, 8, 0x101, 0, 900});
        island.discover(kHuman);
        island.discover(kComputer);
        island.explore(kHuman);
        island.drought.strength = 3;
        island.drought.end_ms = 123'400;
        REQUIRE(world.add_island(std::move(island)).ok());
    }
    {
        sim::WorldIsland island = make_island(buildings, 1, 20, 16, 300, 200);
        REQUIRE(island.map.build(*buildings.by_id(kKontor), 3, 3, 0, 0, kComputer, 0).ok());
        island.discover(kComputer);
        REQUIRE(world.add_island(std::move(island)).ok());
    }

    std::vector<sim::Player> players(4);
    players[kHuman].role = sim::PlayerRole::Human;
    players[kHuman].banner = 0;
    players[kHuman].money = 12'345;
    players[kHuman].name = "Anna";
    players[kHuman].infra_mask = 0x7;
    players[kHuman].soldiers_killed = 4;
    players[kHuman].ships_sunk = 1;
    players[kComputer].role = sim::PlayerRole::Computer;
    players[kComputer].banner = 2;
    players[kComputer].money = -150;
    players[kComputer].name = "Rechner";
    players[kComputer].castles_built = 1;
    players[kComputer].scenario = {sim::scenario_flag::kPassive | sim::scenario_flag::kOwnSize, 0, 2200, 0, 0};
    sim::make_neutral(players[kTrader], sim::faction::kTrader);
    players[kTrader].banner = 4;
    sim::make_neutral(players[kPirates], sim::faction::kPirates);
    players[kPirates].banner = 5;
    world.set_players(players);
    sim::Relation& war = world.diplomacy().relation(kHuman, kComputer);
    war.peace = sim::Treaty::None;
    war.war_time_ms = 60'000;
    war.damage = 77;
    world.diplomacy().relation(kComputer, kHuman).peace = sim::Treaty::None;
    world.diplomacy().relation(kHuman, kComputer).trade = sim::Treaty::Offered;

    sim::City home = make_city(0, kHuman, "Neustadt", 2, 2);
    home.ensure_ware(kWood);
    home.goods[kWood] = 32 * 40;
    home.goods[kTools] = 32 * 12;
    home.trade.resize(kWood + 1);
    home.trade[kWood] = sim::City::Trade{true, false, 45, 30, 32 * 10, 0};
    home.trade[kTools] = sim::City::Trade{false, true, 120, 90, 0, 32 * 30};
    home.inhabitants = {30, 0, 0, 0, 0};
    home.satisfaction = {100, 128, 128, 128, 128};
    home.tax_rate = {150, 128, 128, 128, 128};
    home.food_ratio = 90;
    home.material_lock = true;
    home.purchases = 480 * 75;
    home.sales = 480 * 1234;
    world.add_city(home);
    sim::City other = make_city(1, kComputer, "Zweitstadt", 3, 3);
    other.ensure_ware(kWood);
    other.goods[kWood] = 32 * 3;
    world.add_city(other);

    sim::House house;
    house.island = 0;
    house.x = 6;
    house.y = 4;
    house.city = 0;
    house.inhabitants64 = 30 * 64 + 17;
    house.timer_group = 5;
    house.epoch = 2;
    house.market_distance = 3;
    house.market = true;
    world.add_house(house);
    sim::Production wood;
    wood.island = 0;
    wood.x = 9;
    wood.y = 4;
    wood.rate = 64;
    wood.stock = 3;
    wood.countdown = 17;
    wood.produced = 9;
    wood.elapsed = 120;
    world.set_production(wood);
    sim::Production tools = wood;
    tools.x = 10;
    tools.enabled = false;
    tools.rate = 0;
    tools.idle_cycles = 4;
    world.set_production(tools);

    sim::Ship ship = sim::make_ship("Seeadler", 140, 110, kShipFigure, kHuman, 3);
    ship.health = 1600;
    ship.cannons = 2;
    ship.cargo.resize(4);
    ship.cargo[0] = sim::CargoSlot{kWood, 32 * 20, 30};
    ship.cargo[1] = sim::CargoSlot{kTools, 32 * 5, 95};
    ship.route.active = true;
    ship.route.current = 1;
    ship.route.stops[0].target = sim::Target::city_of(0, 0);
    ship.route.stops[0].entries[0] = sim::RouteEntry{kWood, false, 32 * 30};
    ship.route.stops[1].target = sim::Target::city_of(1, 0);
    ship.route.stops[1].entries[0] = sim::RouteEntry{kWood, true, 32 * 10};
    ship.target = sim::Target::position(200, 150);
    ship.order = sim::Target::position(200, 150);
    world.add_ship(ship);
    sim::Ship computer = sim::make_ship("Handelsschiff", 330, 205, kShipFigure, kComputer, 6);
    computer.cargo.resize(4);
    computer.for_sale = true;
    computer.sale_factor = 100;
    world.add_ship(computer);
    sim::Ship trader = sim::make_ship("Haendler", 50, 300, 29, kTrader, 1);
    trader.trader = true;
    trader.cargo.resize(8);
    trader.trader_goods.resize(kWood + 1);
    trader.trader_goods[kWood] = sim::TraderGood{40, 2, 32 * 25};
    trader.trader_goods[kTools] = sim::TraderGood{110, 0, 32 * 8};
    world.add_ship(trader);
    sim::Ship pirate = sim::make_ship("Pirat", 450, 30, 31, kPirates, 5);
    pirate.cargo.resize(4);
    pirate.cannons = 6;
    pirate.target = sim::Target::other_ship(world.ships()[0].id);
    world.add_ship(pirate);

    sim::Soldier guard = sim::make_soldier(108, 108, 1, kHuman, 0);
    guard.health = 900;
    guard.direction = 5;
    guard.patrol.active = true;
    guard.patrol.points = {sim::Target::spot(2 * 108, 2 * 108), sim::Target::spot(2 * 112, 2 * 106)};
    world.add_soldier(guard);
    sim::Soldier raider = sim::make_soldier(310, 210, 5, kComputer, 1);
    raider.health = 500;
    raider.target = sim::Target::soldier(world.soldiers()[0].id);
    world.add_soldier(raider);

    world.set_time_ms(3'600'000);
    world.set_island_phase(5);
    world.set_city_name_index(7);
    world.set_game_settings(sim::World::GameSettings{2, 0x14, 1500});
    world.set_next_drought_ms(1'800'000);
    return world;
}

/// The bytes the original would see: the chunks of the writer and WIFF, without Open1602's chunk.
core::Bytes original_chunks_only(const sim::World& world, const assets::BuildingTable& buildings) {
    auto chunks = sim::savegame_chunks(world, buildings);
    REQUIRE(chunks.ok());
    assets::ChunkWriter out;
    for (const sim::SavedChunk& c : *chunks) {
        out.add(c.name, c.data);
    }
    out.add_checksum();
    return out.take();
}

sim::World load(const core::Bytes& bytes, const assets::BuildingTable& buildings) {
    auto file = assets::ChunkFile::from_bytes(bytes);
    REQUIRE(file.ok());
    std::vector<std::string> warnings;
    auto world = sim::load_world(*file, buildings, assets::GameDirectory{}, &warnings);
    REQUIRE(world.ok());
    for (const std::string& w : warnings) {
        std::printf("  warning: %s\n", w.c_str());
    }
    CHECK(warnings.empty());
    return std::move(world).value();
}

/// What the original's chunks hold of two worlds is the same.
void check_same(const sim::World& a, const sim::World& b) {
    CHECK(b.width() == a.width());
    CHECK(b.height() == a.height());
    REQUIRE(b.islands().size() == a.islands().size());
    for (std::size_t i = 0; i < a.islands().size(); ++i) {
        const sim::WorldIsland& p = a.islands()[i];
        const sim::WorldIsland& q = b.islands()[i];
        CHECK(q.header.number == p.header.number);
        CHECK(q.x == p.x);
        CHECK(q.y == p.y);
        CHECK(q.header.climate == p.header.climate);
        CHECK((q.header.fertility | 0x1181U) == (p.header.fertility | 0x1181U));
        REQUIRE(q.header.deposits.size() == p.header.deposits.size());
        for (std::size_t k = 0; k < p.header.deposits.size(); ++k) {
            CHECK(q.header.deposits[k].ware == p.header.deposits[k].ware);
            CHECK(q.header.deposits[k].x == p.header.deposits[k].x);
            CHECK(q.header.deposits[k].y == p.header.deposits[k].y);
            CHECK(q.header.deposits[k].amount == p.header.deposits[k].amount);
        }
        CHECK(q.drought.strength == p.drought.strength);
        CHECK(q.drought.end_ms == p.drought.end_ms);
        for (sim::PlayerId pl = 0; pl < assets::kOriginalPlayerSlots; ++pl) {
            CHECK(q.discovered_by(pl) == p.discovered_by(pl));
            CHECK(q.explored_by(pl) == p.explored_by(pl));
        }
        REQUIRE(q.map.width() == p.map.width());
        REQUIRE(q.map.height() == p.map.height());
        bool tiles = true;
        for (std::uint32_t y = 0; y < p.map.height(); ++y) {
            for (std::uint32_t x = 0; x < p.map.width(); ++x) {
                const sim::MapCell& s = p.map.top(x, y);
                const sim::MapCell& t = q.map.top(x, y);
                tiles = tiles && s.sprite == t.sprite && s.rotation == t.rotation && s.owner == t.owner &&
                        s.city == t.city && p.map.ground(x, y).sprite == q.map.ground(x, y).sprite;
            }
        }
        CHECK(tiles);
    }
    REQUIRE(b.players().size() >= a.players().size());
    for (std::size_t i = 0; i < a.players().size(); ++i) {
        const sim::Player& p = a.players()[i];
        const sim::Player& q = b.players()[i];
        CHECK(q.role == p.role);
        CHECK(q.banner == p.banner);
        CHECK(q.money == p.money);
        CHECK(q.name == p.name);
        CHECK(q.status == p.status);
        CHECK(q.infra_mask == p.infra_mask);
        CHECK(q.castles_built == p.castles_built);
        CHECK(q.soldiers_killed == p.soldiers_killed);
        CHECK(q.ships_sunk == p.ships_sunk);
        CHECK(q.scenario == p.scenario);
        for (std::size_t k = 0; k < a.players().size(); ++k) {
            const sim::Relation& r =
                a.diplomacy().relation(static_cast<sim::PlayerId>(i), static_cast<sim::PlayerId>(k));
            const sim::Relation& s =
                b.diplomacy().relation(static_cast<sim::PlayerId>(i), static_cast<sim::PlayerId>(k));
            CHECK(s.peace == r.peace);
            CHECK(s.trade == r.trade);
            CHECK(s.war_time_ms == r.war_time_ms);
            CHECK(s.damage == r.damage);
        }
    }
    REQUIRE(b.cities().size() == a.cities().size());
    for (std::size_t i = 0; i < a.cities().size(); ++i) {
        const sim::City& c = a.cities()[i];
        const sim::City& d = b.cities()[i];
        CHECK(d.island == c.island);
        CHECK(d.index == c.index);
        CHECK(d.owner == c.owner);
        CHECK(d.name == c.name);
        CHECK(d.warehouse_x == c.warehouse_x);
        CHECK(d.warehouse_y == c.warehouse_y);
        CHECK(d.purchases == c.purchases);
        CHECK(d.sales == c.sales);
        CHECK(d.material_lock == c.material_lock);
        CHECK(d.food_ratio == c.food_ratio);
        for (std::size_t k = 0; k < c.inhabitants.size(); ++k) {
            CHECK(d.inhabitants.at(k) == c.inhabitants[k]);
            CHECK(d.satisfaction.at(k) == c.satisfaction[k]);
            CHECK(d.tax_rate.at(k) == c.tax_rate[k]);
        }
        for (std::uint8_t w = 2; w <= 24; ++w) {
            CHECK(d.stock(w) == c.stock(w));
            const sim::City::Trade* s = c.trade_of(w);
            const sim::City::Trade* t = d.trade_of(w);
            if (s != nullptr && (s->sell || s->buy)) {
                REQUIRE(t != nullptr);
                CHECK(t->sell == s->sell);
                CHECK(t->buy == s->buy);
                CHECK(t->sell_price == s->sell_price);
                CHECK(t->buy_price == s->buy_price);
                CHECK(t->keep == s->keep);
                CHECK(t->limit == s->limit);
            }
        }
    }
    REQUIRE(b.houses().size() == a.houses().size());
    for (std::size_t i = 0; i < a.houses().size(); ++i) {
        const sim::House& h = a.houses()[i];
        const sim::House& k = b.houses()[i];
        CHECK(k.island == h.island);
        CHECK(k.x == h.x);
        CHECK(k.y == h.y);
        CHECK(k.city == h.city);
        CHECK(k.inhabitants64 == h.inhabitants64);
        CHECK(k.timer_group == h.timer_group);
        CHECK(k.epoch == h.epoch);
        CHECK(k.market_distance == h.market_distance);
        CHECK(k.market == h.market);
    }
    REQUIRE(b.productions().size() == a.productions().size());
    for (std::size_t i = 0; i < a.productions().size(); ++i) {
        const sim::Production& p = a.productions()[i];
        const sim::Production& q = b.productions()[i];
        CHECK(q.island == p.island);
        CHECK(q.x == p.x);
        CHECK(q.y == p.y);
        CHECK(q.enabled == p.enabled);
        CHECK(q.rate == p.rate);
        CHECK(q.idle_cycles == p.idle_cycles);
        CHECK(q.stock == p.stock);
        CHECK(q.countdown == p.countdown);
        CHECK(q.produced == p.produced);
        CHECK(q.elapsed == p.elapsed);
    }
    REQUIRE(b.ships().size() == a.ships().size());
    for (std::size_t i = 0; i < a.ships().size(); ++i) {
        const sim::Ship& s = a.ships()[i];
        const sim::Ship& t = b.ships()[i];
        CHECK(t.name == s.name);
        CHECK(t.x == s.x);
        CHECK(t.y == s.y);
        CHECK(t.figure == s.figure);
        CHECK(t.owner == s.owner);
        CHECK(t.direction == s.direction);
        CHECK(t.health == s.health);
        CHECK(t.cannons == s.cannons);
        CHECK(t.trader == s.trader);
        CHECK(t.for_sale == s.for_sale);
        CHECK(t.sale_factor == s.sale_factor);
        for (const std::uint8_t w : {kWood, kTools}) {
            CHECK(t.load(w) == s.load(w));
        }
        for (std::size_t c = 0; c < s.cargo.size(); ++c) {
            CHECK(t.cargo.at(c).ware == s.cargo[c].ware);
            CHECK(t.cargo.at(c).amount == s.cargo[c].amount);
            CHECK(t.cargo.at(c).price == s.cargo[c].price);
        }
        CHECK(t.route.active == s.route.active);
        CHECK(t.route.current == s.route.current);
        CHECK(t.route.stops == s.route.stops);
        CHECK(t.target.kind == s.target.kind);
        CHECK(t.order.kind == s.order.kind);
        if (s.target.kind == sim::Target::Kind::Position) {
            CHECK(t.target.x == s.target.x);
            CHECK(t.target.y == s.target.y);
        }
        if (s.target.kind == sim::Target::Kind::Ship) {
            // Ids may change; the target is the ship at the same place in the list.
            const auto index = [](const sim::World& w, std::uint32_t id) {
                for (std::size_t k = 0; k < w.ships().size(); ++k) {
                    if (w.ships()[k].id == id) {
                        return k;
                    }
                }
                return w.ships().size();
            };
            CHECK(index(b, t.target.ship) == index(a, s.target.ship));
        }
        for (std::uint8_t w = 0; w < s.trader_goods.size(); ++w) {
            CHECK(t.trader_goods.at(w).amount == s.trader_goods[w].amount);
            CHECK(t.trader_goods.at(w).price == s.trader_goods[w].price);
            CHECK(t.trader_goods.at(w).age == s.trader_goods[w].age);
        }
    }
    REQUIRE(b.soldiers().size() == a.soldiers().size());
    for (std::size_t i = 0; i < a.soldiers().size(); ++i) {
        const sim::Soldier& s = a.soldiers()[i];
        const sim::Soldier& t = b.soldiers()[i];
        CHECK(t.cell_x() == s.cell_x());
        CHECK(t.cell_y() == s.cell_y());
        CHECK(t.figure == s.figure);
        CHECK(t.owner == s.owner);
        CHECK(t.island == s.island);
        CHECK(t.health == s.health);
        CHECK(t.direction == s.direction);
        CHECK(t.patrol.active == s.patrol.active);
        CHECK(t.patrol.points == s.patrol.points);
        CHECK(t.target.kind == s.target.kind);
    }
    CHECK(b.time_ms() == a.time_ms());
    CHECK(b.island_phase() == a.island_phase());
    CHECK(b.city_name_index() == a.city_name_index());
    CHECK(b.game_settings().difficulty == a.game_settings().difficulty);
    CHECK(b.game_settings().flags == a.game_settings().flags);
    CHECK(b.next_drought_ms() == a.next_drought_ms());
}

}  // namespace

TEST_CASE("save game: a synthetic world written as .GAM loads back the same and writes the same bytes") {
    const auto buildings = make_buildings();
    const sim::World world = make_world(buildings);
    auto bytes = sim::write_savegame(world, buildings);
    REQUIRE(bytes.ok());
    {
        auto file = assets::ChunkFile::from_bytes(*bytes);
        REQUIRE(file.ok());
        CHECK(file->find("WIFF") != nullptr);
        CHECK(file->find(sim::kExtensionChunk) != nullptr);
        CHECK(file->find(sim::kMapChunk) == nullptr);  // the original's size
    }
    const sim::World loaded = load(*bytes, buildings);
    check_same(world, loaded);
    // The loaded world starts its records from the file; written again it is the same file.
    auto again = sim::write_savegame(loaded, buildings);
    REQUIRE(again.ok());
    CHECK(*again == *bytes);
}

TEST_CASE("save game: many soldiers out of slot order load in one pass (HI-5)") {
    // 30 000 SOLDAT3 records from the last slot down, every tenth a duplicate of the one before:
    // the loader asked the world for each id and rebuilt its index after every one, minutes.
    const auto buildings = make_buildings();
    auto chunks = sim::savegame_chunks(make_world(buildings), buildings);
    REQUIRE(chunks.ok());
    constexpr std::uint16_t kCount = 30'000;
    std::vector<std::uint8_t> records;
    std::vector<std::uint16_t> slots;
    for (std::uint16_t i = 0; i < kCount; ++i) {
        const std::uint16_t slot = i % 10 == 9 ? slots.back() : static_cast<std::uint16_t>(kCount - i);
        slots.push_back(slot);
        std::vector<std::uint8_t> r(0x44, 0);
        r[0] = 40;  // x 20, y 20 (half tiles)
        r[2] = 40;
        r[6] = 33;
        r[8] = static_cast<std::uint8_t>(slot);
        r[9] = static_cast<std::uint8_t>(slot >> 8);
        records.insert(records.end(), r.begin(), r.end());
    }
    assets::ChunkWriter out;
    for (const sim::SavedChunk& c : *chunks) {
        if (c.name != "SOLDAT3") {
            out.add(c.name, c.data);
        }
    }
    out.add("SOLDAT3", records);
    out.add_checksum();
    const sim::World loaded = load(out.take(), buildings);
    REQUIRE(loaded.soldiers().size() == kCount);
    // The first record of a slot takes the slot's id, a duplicate the next free one.
    CHECK(loaded.soldiers().front().source_slot == kCount);
    CHECK(loaded.soldier(loaded.soldiers()[1].id)->source_slot == kCount - 1);
    std::vector<std::uint32_t> ids;
    for (const sim::Soldier& s : loaded.soldiers()) {
        ids.push_back(s.id);
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    CHECK(loaded.soldiers()[9].id != loaded.soldiers()[8].id);
    CHECK(loaded.soldiers()[9].source_slot == loaded.soldiers()[8].source_slot);
}

TEST_CASE("save game: the original's chunks of a .GAM alone hold the synthetic world") {
    const auto buildings = make_buildings();
    const sim::World world = make_world(buildings);
    // As if the original had saved the game again: Open1602's own chunk is gone.
    const core::Bytes bytes = original_chunks_only(world, buildings);
    const sim::World loaded = load(bytes, buildings);
    check_same(world, loaded);
    // Its chunks written again are the same chunks.
    auto first = sim::savegame_chunks(world, buildings);
    auto second = sim::savegame_chunks(loaded, buildings);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(second->size() == first->size());
    for (std::size_t i = 0; i < first->size(); ++i) {
        CHECK((*second)[i].name == (*first)[i].name);
        CHECK((*second)[i].data == (*first)[i].data);
    }
    CHECK(original_chunks_only(loaded, buildings) == bytes);
}

TEST_CASE("save game: a ship far beyond the map goes to the nearest open sea, as a map's player needs it") {
    const auto buildings = make_buildings();
    const sim::World world = make_world(buildings);
    auto chunks = sim::savegame_chunks(world, buildings);
    REQUIRE(chunks.ok());
    // The pirate (450, 30) at x 4090, as Seeschlacht keeps the third player's warships.
    bool moved = false;
    assets::ChunkWriter out;
    for (sim::SavedChunk& c : *chunks) {
        if (c.name == "SHIP4") {
            for (std::size_t o = 0; o + 0x1B4 <= c.data.size(); o += 0x1B4) {
                if (std::string_view(reinterpret_cast<const char*>(c.data.data() + o), 5) == "Pirat") {
                    c.data[o + 0x1C] = 0xFA;
                    c.data[o + 0x1D] = 0x0F;
                    moved = true;
                }
            }
        }
        out.add(c.name, c.data);
    }
    out.add_checksum();
    REQUIRE(moved);
    auto file = assets::ChunkFile::from_bytes(out.take());
    REQUIRE(file.ok());
    std::vector<std::string> warnings;
    auto loaded = sim::load_world(*file, buildings, assets::GameDirectory{}, &warnings);
    REQUIRE(loaded.ok());
    REQUIRE(loaded->ships().size() == world.ships().size());
    const sim::Ship* pirate = nullptr;
    for (const sim::Ship& ship : loaded->ships()) {
        if (ship.name == "Pirat") {
            pirate = &ship;
        }
    }
    REQUIRE(pirate != nullptr);
    // Near the right edge at its row, on open sea, and the log says so.
    CHECK(pirate->x < loaded->width());
    CHECK(pirate->x + 10U >= loaded->width());
    CHECK(pirate->y >= 20);
    CHECK(pirate->y <= 40);
    CHECK(loaded->cell(pirate->x, pirate->y).island == sim::World::kSea);
    CHECK(std::any_of(warnings.begin(), warnings.end(),
                      [](const std::string& w) { return w.find("moved to the sea") != std::string::npos; }));
}

TEST_CASE("save game: a satisfaction, tax rate or food supply of 0 loads as 128, as in the original") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    std::vector<sim::City> cities = world.cities();
    REQUIRE(!cities.empty());
    cities[0].satisfaction = {0, 0, 0, 0, 0};
    cities[0].tax_rate = {0, 0, 0, 0, 0};
    cities[0].food_ratio = 0;
    world.set_cities(std::move(cities));
    // The original's loader makes 128 of each stored 0.
    const core::Bytes bytes = original_chunks_only(world, buildings);
    const sim::World loaded = load(bytes, buildings);
    const sim::City& c = loaded.cities().at(0);
    CHECK(c.food_ratio == 128);
    for (std::size_t k = 0; k < 5; ++k) {
        CHECK(c.satisfaction.at(k) == 128);
        CHECK(c.tax_rate.at(k) == 128);
    }
    // Written again, the zeros of the file stay.
    CHECK(original_chunks_only(loaded, buildings) == bytes);
}

TEST_CASE("save game: a route beyond the original's four stops and three wares keeps them in Open1602's chunk only") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    sim::Ship* ship = nullptr;
    for (sim::Ship& s : world.ships()) {
        ship = s.name == "Seeadler" ? &s : ship;
    }
    REQUIRE(ship != nullptr);
    ship->route.stops[5].target = sim::Target::city_of(0, 0);
    ship->route.stops[5].entries[1] = sim::RouteEntry{kTools, false, 32 * 7};
    ship->route.stops[0].entries[5] = sim::RouteEntry{kTools, true, 32 * 9};
    ship->route.current = 5;
    const sim::Ship expected = *ship;
    auto bytes = sim::write_savegame(world, buildings);
    REQUIRE(bytes.ok());
    const sim::World whole = load(*bytes, buildings);
    for (const sim::Ship& s : whole.ships()) {
        if (s.name == "Seeadler") {
            CHECK(s.route.stops == expected.route.stops);
            CHECK(s.route.current == 5);
        }
    }
    // The original sees the first four stops with three wares each, and the route from its start.
    const sim::World original = load(original_chunks_only(world, buildings), buildings);
    for (const sim::Ship& s : original.ships()) {
        if (s.name == "Seeadler") {
            for (std::size_t n = 0; n < sim::TradeRoute::kStops; ++n) {
                sim::RouteStop seen = n < sim::TradeRoute::kOriginalStops ? expected.route.stops[n] : sim::RouteStop{};
                std::fill(seen.entries.begin() + sim::RouteStop::kOriginalEntries, seen.entries.end(),
                          sim::RouteEntry{});
                CHECK(s.route.stops[n] == seen);
            }
            CHECK(s.route.current == 0);
        }
    }
}

TEST_CASE("save game: seats that changed hands since the source file are written as they are now") {
    const auto buildings = make_buildings();
    // Loaded from a .GAM: the players' records come from the file.
    sim::World loaded = load(original_chunks_only(make_world(buildings), buildings), buildings);
    // As a game saved after the host left: the person's seat is the computer's now, and the
    // computer's seat is a person's under another name.
    loaded.player(kHuman)->role = sim::PlayerRole::Computer;
    loaded.player(kComputer)->role = sim::PlayerRole::Human;
    loaded.player(kComputer)->name = "Bert";
    const sim::World again = load(original_chunks_only(loaded, buildings), buildings);
    CHECK(again.player(kHuman)->role == sim::PlayerRole::Computer);
    CHECK(again.player(kHuman)->name == "Anna");
    CHECK(again.player(kComputer)->role == sim::PlayerRole::Human);
    CHECK(again.player(kComputer)->name == "Bert");
}

TEST_CASE("save game: a world beyond the original's limits is not written as .GAM") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    std::vector<sim::Player> players = world.players();
    players.resize(8);
    world.set_players(players);
    CHECK(!sim::write_savegame(world, buildings).ok());
    sim::World ships = make_world(buildings);
    for (std::uint32_t k = 0; k < 150; ++k) {
        ships.add_ship(sim::make_ship("Schiff", 10 + k, 20, kShipFigure, kHuman));
    }
    CHECK(!sim::write_savegame(ships, buildings).ok());
}

TEST_CASE("save game: a larger own world is written with its size and loads back the same") {
    const auto buildings = make_buildings();
    sim::World world(700, 400);
    REQUIRE(world.add_island(make_island(buildings, 0, 30, 20, 600, 350)).ok());
    std::vector<sim::Player> players(2);
    players[0].name = "Anna";
    players[0].money = 500;
    players[1].role = sim::PlayerRole::Computer;
    players[1].banner = 1;
    world.set_players(players);
    world.add_ship(sim::make_ship("Weit", 650, 380, kShipFigure, 0));
    world.add_ship(sim::make_ship("Fern", 690, 10, kShipFigure, 1));
    auto bytes = sim::write_savegame(world, buildings);
    REQUIRE(bytes.ok());
    auto file = assets::ChunkFile::from_bytes(*bytes);
    REQUIRE(file.ok());
    CHECK(file->find(sim::kMapChunk) != nullptr);
    const sim::World loaded = load(*bytes, buildings);
    check_same(world, loaded);
    auto again = sim::write_savegame(loaded, buildings);
    REQUIRE(again.ok());
    CHECK(*again == *bytes);
}

TEST_CASE("save game: a world of 4000 tiles with an island of 1000 is saved and loaded, not as .GAM") {
    const auto buildings = make_buildings();
    sim::World world(4000, 1100);
    sim::WorldIsland island = make_island(buildings, 0, 1000, 600, 2900, 400);
    island.header.width = 1000;  // the helper's header keeps a byte
    island.header.height = 600;
    REQUIRE(world.add_island(std::move(island)).ok());
    std::vector<sim::Player> players(2);
    players[0].name = "Anna";
    players[1].role = sim::PlayerRole::Computer;
    players[1].banner = 1;
    world.set_players(players);
    world.add_ship(sim::make_ship("Weit", 3990, 1090, kShipFigure, 0));
    world.add_ship(sim::make_ship("Fern", 10, 10, kShipFigure, 1));
    CHECK(!sim::write_savegame(world, buildings).ok());  // the original's chunks keep a byte
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    CHECK(loaded->width() == 4000);
    CHECK(loaded->height() == 1100);
    check_same(world, *loaded);
}
