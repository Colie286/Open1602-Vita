// SPDX-License-Identifier: GPL-3.0-or-later
// Logistics: paths, carriers, market carts and the city warehouse.

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/render/land_figures.hpp"
#include "o1602/render/walker_figures.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kForest = 20201;
constexpr std::uint32_t kBakery = 20503;
constexpr std::uint32_t kDoorBakery = 20504;  // a bakery with its door on the edge
constexpr std::uint32_t kMill = 20505;
constexpr std::uint32_t kWeaver = 20507;
constexpr std::uint32_t kKontor = 22101;
constexpr std::uint32_t kMarket = 20809;
constexpr std::uint32_t kHouse = 20640;
constexpr std::uint32_t kDestroyedKontor = 22121;

constexpr std::uint8_t kFlour = 5;
constexpr std::uint8_t kBread = 10;
constexpr std::uint8_t kCloth = 14;

/// Road, ground and forest; a bakery (flour -> bread), a mill (makes flour), a weaver (makes
/// cloth, a finished good), a warehouse, a market and a destroyed warehouse.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20401\n  Kind: 1\n  Gfx: 0\n  Size: 1, 1\n  Wegspeed: 100, 100, 100, 100\n"
        "  @Nummer: +1\n  Id: 20101\n  Kind: 11\n  Gfx: 1\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: +1\n  Id: 20201\n  Kind: 10\n  Gfx: 2\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: +1\n  Id: 20503\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 10\n    Rohstoff: 5\n    Rohmenge: 1\n"
        "    Prodmenge: 1\n    Interval: 20\n    Figurnr: 80\n    Radius: 8\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20504\n  Kind: 14\n  Gfx: 80\n  Size: 2, 2\n  Tuerflg: 1\n"
        "  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 10\n    Rohstoff: 5\n    Rohmenge: 1\n"
        "    Prodmenge: 1\n    Interval: 20\n    Figurnr: 80\n    Radius: 8\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20505\n  Kind: 14\n  Gfx: 20\n  Size: 2, 2\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 6\n    Ware: 5\n    Rohstoff: 8\n    Rohmenge: 1\n"
        "    Prodmenge: 1\n    Interval: 20\n    Figurnr: 80\n    Radius: 8\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20507\n  Kind: 14\n  Gfx: 30\n  Size: 1, 1\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 6\n    Ware: 14\n    Rohstoff: 4\n    Rohmenge: 1\n"
        "    Prodmenge: 1\n    Interval: 20\n    Figurnr: 80\n    Radius: 8\n  EndObj\n"
        "  @Nummer: +1\n  Id: 22101\n  Kind: 14\n  Gfx: 40\n  Size: 2, 2\n  Tuerflg: 1\n"
        "  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Ware: 1\n    Rohstoff: 1\n    Rohmenge: 1\n    Maxlager: 30\n"
        "    Figurnr: 82\n    Figuranz: 1\n    Radius: 12\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20640\n  Kind: 14\n  Gfx: 60\n  Size: 2, 2\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 2\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20809\n  Kind: 14\n  Gfx: 50\n  Size: 3, 3\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Ware: 1\n    Rohstoff: 1\n    Rohmenge: 1\n    Maxlager: 30\n"
        "    Figurnr: 82\n    Figuranz: 2\n    Radius: 12\n  EndObj\n"
        "  @Nummer: +1\n  Id: 22121\n  Kind: 14\n  Gfx: 70\n  Size: 2, 2\n  Tuerflg: 1\n  Destroyflg: 1\n"
        "  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Ware: 1\n    Rohstoff: 1\n    Rohmenge: 1\n    Maxlager: 20\n"
        "    Figurnr: 82\n    Figuranz: 1\n    Radius: 12\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// TRAEGER and KARREN with the values of figuren.cod.
assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 80\n  Blocknr: 3\n  Speed: 220\n  Rotate: 8\n  Gfx: 0\n  Maxtrag: 4\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 1\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 85\n"
        "    Nummer: 1\n    Kind: 1\n    AnimOffs: 64\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 85\n"
        "  EndObj\n"
        "  Nummer: 82\n  Blocknr: 3\n  Speed: 300\n  Speedtyp: 2\n  Rotate: 8\n  Gfx: 496\n  Maxtrag: 6\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 1\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 60\n"
        "    Nummer: 1\n    Kind: 1\n    AnimOffs: 64\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 60\n"
        "  EndObj\n"
        "  Nummer: 92\n  Blocknr: 3\n  Speed: 340\n  Rotate: 8\n  Gfx: 320\n  Maxtrag: 6\n  Worktime: 0.78\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 1\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 55\n"
        "    Nummer: 1\n    Kind: 1\n    AnimOffs: 64\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 55\n"
        "    Nummer: 2\n    Kind: 1\n    AnimOffs: 128\n    AnimAdd: 1\n    AnimAnz: 6\n    AnimSpeed: 130\n"
        "  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A 30 x 12 island of open ground with a road along row 5 from x = 0 to 29. City 0 of player 0.
struct Town {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world;
    sim::IslandMap map{30, 12};

    Town() {
        for (std::int32_t y = 0; y < 12; ++y) {
            for (std::int32_t x = 0; x < 30; ++x) {
                REQUIRE(map.place(*buildings.by_id(y == 5 ? kRoad : kGround), x, y, 0, 0, 0, 0).ok());
            }
        }
    }

    void place(std::uint32_t id, std::int32_t x, std::int32_t y, std::uint8_t rotation = 0) {
        REQUIRE(map.place(*buildings.by_id(id), x, y, rotation, 0, 0, 0).ok());
    }

    /// Puts the island into the world with a city and one production entry per building listed.
    void finish(const std::vector<std::pair<std::int32_t, std::int32_t>>& productions) {
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
        sim::City city;
        city.island = 0;
        city.index = 0;
        city.owner = 0;
        world.add_city(city);
        for (const auto& [x, y] : productions) {
            sim::Production p;
            p.island = 0;
            p.x = static_cast<std::uint16_t>(x);
            p.y = static_cast<std::uint16_t>(y);
            world.set_production(p);
        }
    }

    sim::Production& production(std::int32_t x, std::int32_t y) {
        const auto i = world.production_index(0, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
        REQUIRE(i.has_value());
        return world.productions()[*i];
    }
    std::uint32_t index(std::int32_t x, std::int32_t y) {
        return *world.production_index(0, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
    }
    sim::City& city() { return *world.city(0, 0); }
};

sim::ProductionEvent needs(std::uint32_t production, std::uint8_t ware) {
    return sim::ProductionEvent{sim::ProductionEvent::Kind::NeedsRawGoods, production, 0, ware};
}

/// Ticks until no figure is out (at most `max` ticks); returns the ticks run.
std::uint32_t run_until_idle(sim::Logistics& logistics, std::uint32_t max = 20'000) {
    std::uint32_t n = 0;
    while (logistics.walkers().size() != 0 && n < max) {
        logistics.tick();
        ++n;
    }
    return n;
}

const sim::Walker* first_walker(const sim::Logistics& logistics) {
    const sim::Walker* found = nullptr;
    logistics.walkers().for_each([&](sim::Handle, const sim::Walker& w) {
        if (found == nullptr) {
            found = &w;
        }
    });
    return found;
}

}  // namespace

TEST_CASE("logistics: walking fields of HAUS and FIGUR records") {
    const auto buildings = make_buildings();
    CHECK(buildings.by_id(kRoad)->walk_speed[0] == 32);    // 100 % -> 32
    CHECK(buildings.by_id(kGround)->walk_speed[0] == 46);  // 145 % -> 46
    CHECK(buildings.by_id(kGround)->walk_speed[2] == 54);  // 170 % -> 54
    CHECK(buildings.by_id(kKontor)->door_on_edge);
    CHECK(buildings.by_id(kKontor)->radius == 12);
    CHECK(buildings.by_id(kMarket)->worker_count == 2);
    const auto figures = make_figures();
    CHECK(figures.get(80)->speed == 220);
    CHECK(figures.get(80)->max_carry == 4 * assets::kStockUnitsPerTon);
    CHECK(figures.get(82)->speed_type == 2);
    // 0.16 * Speed / Wegspeed tiles per second: a carrier makes 1.1 tiles/s on a road, 0.77 on
    // open ground, a cart 1.5 on a road.
    const auto per_second = [](std::uint32_t units_per_tick) { return units_per_tick * 20.0 / 65536.0; };
    CHECK(std::abs(per_second(sim::Logistics::step_speed(*figures.get(80), 32)) - 1.10) < 0.001);
    CHECK(std::abs(per_second(sim::Logistics::step_speed(*figures.get(80), 46)) - 0.765) < 0.001);
    CHECK(std::abs(per_second(sim::Logistics::step_speed(*figures.get(82), 32)) - 1.50) < 0.001);
}

TEST_CASE("pathfinding: catchment circle like the original's table") {
    CHECK((sim::circle_half_widths(10) == std::vector<std::uint8_t>{10, 10, 10, 10, 9, 9, 8, 7, 6, 5, 3}));
    CHECK((sim::circle_half_widths(0) == std::vector<std::uint8_t>{0}));
    CHECK((sim::circle_half_widths(1) == std::vector<std::uint8_t>{1, 1}));
    const auto buildings = make_buildings();
    // A 2 x 2 building has no centre tile: centre = origin, one more column and row.
    const sim::Catchment c = sim::catchment_of(*buildings.by_id(kBakery), 0, {4, 4});
    CHECK((c.centre == sim::TilePos{4, 4}));
    CHECK(c.parity_x == 1);
    CHECK(c.width() == 18);
    const sim::Catchment m = sim::catchment_of(*buildings.by_id(kMarket), 0, {4, 4});
    CHECK((m.centre == sim::TilePos{5, 5}));
    CHECK(m.parity_x == 0);
}

TEST_CASE("pathfinding: fishers and towers show their circle over the water") {
    assets::BuildingType b;
    b.prod_kind = sim::prod::kFisher;
    CHECK(sim::catchment_covers_water(b));
    b.prod_kind = sim::prod::kTower;
    CHECK(sim::catchment_covers_water(b));
    b.prod_kind = 0;
    b.kind = sim::kind::kTowerBeach;
    CHECK(sim::catchment_covers_water(b));
    b.kind = sim::kind::kBuilding;
    b.prod_kind = sim::prod::kMarket;
    CHECK(!sim::catchment_covers_water(b));
}

TEST_CASE("pathfinding: cheapest route, no corner cutting, goals entered not crossed") {
    const auto buildings = make_buildings();
    sim::IslandMap map(12, 12);
    for (std::int32_t y = 0; y < 12; ++y) {
        for (std::int32_t x = 0; x < 12; ++x) {
            REQUIRE(map.place(*buildings.by_id(kForest), x, y, 0, 0).ok());
        }
    }
    // An L of road: (1,1) -> (8,1) -> (8,8); a shortcut over open ground at (2,2)..(7,7) would be
    // diagonal, but forest blocks it except a single ground tile at (5,4).
    for (std::int32_t x = 1; x <= 8; ++x) {
        REQUIRE(map.place(*buildings.by_id(kRoad), x, 1, 0, 0).ok());
    }
    for (std::int32_t y = 1; y <= 8; ++y) {
        REQUIRE(map.place(*buildings.by_id(kRoad), 8, y, 0, 0).ok());
    }
    REQUIRE(map.place(*buildings.by_id(kGround), 7, 2, 0, 0).ok());

    sim::Catchment area;
    area.centre = {5, 5};
    area.radius = 6;
    sim::PathFinder finder;
    finder.begin(map, buildings, area, sim::WalkClass::Land, 0);
    CHECK(finder.cell({7, 2}) == sim::PathFinder::Cell::Walkable);
    CHECK(finder.cell({3, 3}) == sim::PathFinder::Cell::Blocked);
    finder.set({8, 9}, sim::PathFinder::Cell::Goal, 32);  // a building below the road's end
    const auto hit = finder.search({1, 1}, [](sim::TilePos t) {
        return t == sim::TilePos{8, 9} ? sim::PathFinder::Answer::Accept : sim::PathFinder::Answer::Reject;
    });
    REQUIRE(hit.has_value());
    std::vector<sim::TilePos> path;
    REQUIRE(finder.path_to(*hit, path));
    CHECK((path.back() == sim::TilePos{8, 9}));
    // Diagonal steps only where both straight neighbours are walkable: (7,1) -> (8,2) passes the
    // road (8,1) and the ground (7,2), a step past forest would not be allowed.
    for (std::size_t i = 1; i < path.size(); ++i) {
        const sim::TilePos a = path[i - 1];
        const sim::TilePos b = path[i];
        if (a.x != b.x && a.y != b.y) {
            CHECK(finder.cell({b.x, a.y}) == sim::PathFinder::Cell::Walkable);
            CHECK(finder.cell({a.x, b.y}) == sim::PathFinder::Cell::Walkable);
        }
    }
    // The Roads class does not walk on open ground.
    sim::PathFinder roads;
    roads.begin(map, buildings, area, sim::WalkClass::Roads, 2);
    CHECK(roads.cell({7, 2}) == sim::PathFinder::Cell::Blocked);
    // Outside the circle nothing is walkable.
    CHECK(!finder.inside({0, 11}));
    CHECK(finder.cell({0, 11}) == sim::PathFinder::Cell::Blocked);

    // A goal in the way is not crossed: put one on the road, the target behind it is unreachable.
    finder.begin(map, buildings, area, sim::WalkClass::Roads, 0);
    finder.set({8, 4}, sim::PathFinder::Cell::Goal, 32);
    finder.set({8, 9}, sim::PathFinder::Cell::Goal, 32);
    std::vector<sim::TilePos> goals;
    finder.search({1, 1}, [&](sim::TilePos t) {
        goals.push_back(t);
        return sim::PathFinder::Answer::Remember;
    });
    CHECK((goals == std::vector<sim::TilePos>{{8, 4}}));
}

TEST_CASE("logistics: a carrier fetches flour from the mill") {
    Town town;
    town.place(kMill, 10, 3);    // north of the road
    town.place(kBakery, 16, 6);  // south of the road
    town.finish({{10, 3}, {16, 6}});
    town.production(10, 3).stock = 3 * 32;
    sim::Logistics logistics(town.world, town.buildings, town.figures);

    logistics.handle(needs(town.index(16, 6), kFlour));
    REQUIRE(logistics.walkers().size() == 1);
    const sim::Walker& w = *first_walker(logistics);
    CHECK(w.kind == sim::WalkerKind::Carrier);
    CHECK(w.figure == 80);
    CHECK((w.tile == sim::TilePos{16, 6}));  // the bakery's centre (no Tuerflg)
    CHECK(w.reserved == 3 * 32);             // all of it: at least min(4 t / 2, 2 t), at most 4 t
    CHECK(town.production(10, 3).reserved == 3 * 32);
    CHECK(w.path.back().y <= 4);  // ends on a tile of the mill
    // One carrier per building at a time.
    logistics.handle(needs(town.index(16, 6), kFlour));
    CHECK(logistics.walkers().size() == 1);

    const std::uint32_t ticks = run_until_idle(logistics);
    CHECK(ticks > 100);  // a dozen tiles there and back
    CHECK(ticks < 1000);
    CHECK(town.production(16, 6).raw == 3 * 32);
    CHECK(town.production(10, 3).stock == 0);
    CHECK(town.production(10, 3).reserved == 0);
    CHECK(logistics.stats().carrier_trips == 1);
    CHECK(logistics.stats().raw_delivered == 3 * 32);
    // Too little for a trip (below 2 t): nobody goes.
    town.production(10, 3).stock = 32;
    logistics.handle(needs(town.index(16, 6), kFlour));
    CHECK(logistics.walkers().size() == 0);
}

TEST_CASE("logistics: a saved walker whose buildings do not exist is refused") {
    Town town;
    town.place(kMill, 10, 3);
    town.place(kBakery, 16, 6);
    town.finish({{10, 3}, {16, 6}});
    town.production(10, 3).stock = 3 * 32;
    sim::Logistics logistics(town.world, town.buildings, town.figures);
    logistics.handle(needs(town.index(16, 6), kFlour));
    REQUIRE(logistics.walkers().size() == 1);
    core::ByteWriter out;
    logistics.save(out);
    const auto loads = [&](const std::vector<std::uint8_t>& bytes) {
        sim::Logistics other(town.world, town.buildings, town.figures);
        core::ByteReader reader(bytes);
        return other.load(reader, 2);
    };
    CHECK(loads(out.data()));
    // Slot count, generation, present, kind, island, figure, city, then the home (offset 15) and,
    // after the home tile and the "has source" flag, the source (offset 28).
    for (const std::size_t offset : {std::size_t{15}, std::size_t{28}}) {
        core::ByteWriter damaged;
        damaged.bytes(out.data());
        damaged.patch_u32(offset, 0x7FFFFFFFU);
        CHECK(!loads(damaged.data()));
    }
}

TEST_CASE("logistics: carriers fetch from the warehouse, carts bring goods there") {
    Town town;
    town.place(kKontor, 2, 3);  // door on its east edge: (3, 3)
    town.place(kBakery, 8, 6);
    town.place(kWeaver, 14, 4);
    town.place(kMill, 11, 3);
    town.finish({{2, 3}, {8, 6}, {14, 4}, {11, 3}});
    sim::City& city = town.city();
    city.ensure_ware(kCloth);
    city.goods[kFlour] = 200;
    sim::Logistics logistics(town.world, town.buildings, town.figures);
    CHECK(city.capacity == 30 * 32);  // Kontor Maxlager, no market

    // The bakery's carrier takes 4 t from the warehouse.
    logistics.handle(needs(town.index(8, 6), kFlour));
    REQUIRE(logistics.walkers().size() == 1);
    CHECK(first_walker(logistics)->from_warehouse);
    CHECK(city.reserved[kFlour] == 128);
    run_until_idle(logistics);
    CHECK(town.production(8, 6).raw == 128);
    CHECK(city.goods[kFlour] == 200 - 128);
    CHECK(city.reserved[kFlour] == 0);

    // The warehouse's cart collects the weaver's cloth (a finished good) ...
    town.production(14, 4).stock = 2 * 32;
    logistics.handle(needs(town.index(2, 3), sim::kAllWares));
    REQUIRE(logistics.walkers().size() == 1);
    const sim::Walker& cart = *first_walker(logistics);
    CHECK(cart.kind == sim::WalkerKind::Cart);
    CHECK(cart.ware == kCloth);
    CHECK(city.incoming[kCloth] == 64);
    // ... but at most Figuranz carts.
    logistics.handle(needs(town.index(2, 3), sim::kAllWares));
    CHECK(logistics.walkers().size() == 1);
    run_until_idle(logistics);
    CHECK(city.goods[kCloth] == 64);
    CHECK(city.incoming[kCloth] == 0);
    CHECK(town.production(14, 4).stock == 0);

    // Flour (an intermediate good) only from a full mill.
    town.production(11, 3).stock = 5 * 32;
    logistics.handle(needs(town.index(2, 3), sim::kAllWares));
    CHECK(logistics.walkers().size() == 0);
    town.production(11, 3).stock = 6 * 32;
    logistics.handle(needs(town.index(2, 3), sim::kAllWares));
    REQUIRE(logistics.walkers().size() == 1);
    CHECK(first_walker(logistics)->ware == kFlour);
    run_until_idle(logistics);
    CHECK(city.goods[kFlour] == 200 - 128 + 6 * 32);
    CHECK(logistics.stats().goods_collected == 64 + 6 * 32);
}

TEST_CASE("logistics: carts respect reservations and warehouse room (S-023, S-024)") {
    Town town;
    town.place(kMarket, 2, 2);  // 3 x 3, touches the road
    town.place(kKontor, 24, 3);
    town.place(kWeaver, 8, 4);   // near
    town.place(kWeaver, 13, 6);  // farther
    town.finish({{2, 2}, {24, 3}, {8, 4}, {13, 6}});
    sim::Logistics logistics(town.world, town.buildings, town.figures);
    sim::City& city = town.city();
    CHECK(city.capacity == 30 * 32 + 10 * 32);  // a market adds 10 t

    town.production(8, 4).stock = 6 * 32;   // full
    town.production(13, 6).stock = 3 * 32;  // half full
    logistics.handle(needs(town.index(2, 2), sim::kAllWares));
    logistics.handle(needs(town.index(2, 2), sim::kAllWares));
    REQUIRE(logistics.walkers().size() == 2);
    std::vector<std::uint32_t> sources;
    logistics.walkers().for_each([&](sim::Handle, const sim::Walker& w) { sources.push_back(*w.source); });
    // The first cart takes the full weaver, the second one goes to the other weaver instead of
    // the one already reserved (the original would send it to the same one and lose the trip).
    CHECK(sources[0] == town.index(8, 4));
    CHECK(sources[1] == town.index(13, 6));
    run_until_idle(logistics);
    CHECK(city.goods[kCloth] == 9 * 32);
    CHECK(town.production(2, 2).produced == 9 * 32);  // the market counts for its stalls

    // Less than a ton of room: no cart sets out (the original would destroy the load).
    city.goods[kCloth] = city.capacity - 16;
    town.production(8, 4).stock = 6 * 32;
    logistics.handle(needs(town.index(2, 2), sim::kAllWares));
    CHECK(logistics.walkers().size() == 0);
    // Room for 2 t: the cart loads only that much and nothing is lost.
    city.goods[kCloth] = city.capacity - 64;
    logistics.handle(needs(town.index(2, 2), sim::kAllWares));
    REQUIRE(logistics.walkers().size() == 1);
    CHECK(first_walker(logistics)->reserved == 64);
    run_until_idle(logistics);
    CHECK(city.goods[kCloth] == city.capacity);
    CHECK(town.production(8, 4).stock == 4 * 32);
    CHECK(town.production(8, 4).reserved == 0);

    // The player forbids collecting: the weaver is skipped.
    city.goods[kCloth] = 0;
    town.production(8, 4).flags |= sim::Production::kNoPickupFlag;
    town.production(13, 6).stock = 0;
    logistics.handle(needs(town.index(2, 2), sim::kAllWares));
    CHECK(logistics.walkers().size() == 0);
}

TEST_CASE("logistics: a destroyed Kontor gives its own room and no market bonus") {
    Town town;
    town.place(kMarket, 2, 2);
    town.place(kDestroyedKontor, 24, 3);
    town.finish({{2, 2}, {24, 3}});
    sim::Logistics logistics(town.world, town.buildings, town.figures);
    // Base 20 t of the destroyed Kontor; only the standing market counts, as the first storehouse.
    CHECK(town.city().capacity == 20 * 32);
}

TEST_CASE("logistics: after loading a city holds no more than its warehouse takes") {
    Town town;
    town.place(kKontor, 24, 3);
    town.finish({{24, 3}});
    town.city().goods.assign(16, 0);
    town.city().goods[kCloth] = 50 * 32;  // more than the Kontor's 30 t, as in an edited game
    town.city().goods[kFlour] = 10 * 32;
    const sim::Simulation simulation(town.world, town.buildings, town.figures);
    CHECK(town.city().capacity == 30 * 32);
    CHECK(town.city().goods[kCloth] == 30 * 32);
    CHECK(town.city().goods[kFlour] == 10 * 32);
}

TEST_CASE("logistics: carts are not drawn on the tiles of buildings") {
    // Ticks a cart of the market to the weaver and back; returns how many ticks it was drawn.
    const auto drawn_ticks = [](std::int32_t weaver_x, std::int32_t weaver_y) {
        Town town;
        town.place(kMarket, 2, 2);  // 3 x 3, touches the road; the cart starts in its centre (3, 3)
        town.place(kKontor, 24, 3);
        town.place(kWeaver, weaver_x, weaver_y);
        town.finish({{2, 2}, {24, 3}, {weaver_x, weaver_y}});
        sim::Logistics logistics(town.world, town.buildings, town.figures);
        town.production(weaver_x, weaver_y).stock = 2 * 32;
        logistics.handle(needs(town.index(2, 2), sim::kAllWares));
        REQUIRE(logistics.walkers().size() == 1);
        render::WalkerFigures figures;
        std::uint32_t drawn = 0;
        for (std::uint32_t n = 0; logistics.walkers().size() != 0 && n < 20'000; ++n) {
            logistics.tick();
            figures.update(logistics, town.world, town.buildings, town.figures, sim::Logistics::kTickMs);
            drawn += figures.instances().empty() ? 0U : 1U;
        }
        CHECK(town.city().goods[kCloth] == 2 * 32);
        return drawn;
    };
    // A weaver next to the market: the cart crosses the market square into the weaver and back
    // without ever touching the road, and is never seen (as in the original).
    CHECK(drawn_ticks(5, 3) == 0);
    // A weaver down the road: the cart is seen on the road.
    CHECK(drawn_ticks(8, 4) > 0);

    const auto buildings = make_buildings();
    CHECK(render::walker_visible_on(buildings.by_id(kRoad)));
    CHECK(render::walker_visible_on(buildings.by_id(kGround)));
    CHECK(!render::walker_visible_on(buildings.by_id(kForest)));
    CHECK(!render::walker_visible_on(buildings.by_id(kMarket)));
    CHECK(!render::walker_visible_on(nullptr));
}

TEST_CASE("logistics: the simulation sends carriers when a workshop runs short") {
    Town town;
    town.place(kKontor, 2, 3);
    town.place(kBakery, 8, 6);
    town.finish({{2, 3}, {8, 6}});
    town.city().goods.assign(16, 0);
    town.city().goods[kFlour] = 10 * 32;
    sim::Production& bakery = town.production(8, 6);
    bakery.countdown = 1;
    sim::Simulation simulation(town.world, town.buildings, town.figures);
    // Second 1: no flour -> asks; the carrier walks and brings 4 t; the bakery starts at once.
    for (int s = 0; s < 40; ++s) {
        simulation.advance(1000);
    }
    CHECK(simulation.logistics().stats().carrier_trips >= 1);
    CHECK(town.production(8, 6).working());
    CHECK(town.city().goods[kFlour] <= 6 * 32);
    // The bakery keeps asking while it has flour for at most two cycles, so in the long run the
    // warehouse empties into bread.
    for (int s = 0; s < 600; ++s) {
        simulation.advance(1000);
    }
    CHECK(town.city().goods[kFlour] < 2 * 32);
    // The bread went to the warehouse with the Kontor's cart once the bakery was full.
    CHECK(town.city().goods[kBread] + town.production(8, 6).stock >= 8 * 32);
}

TEST_CASE("logistics: a robber waits in his house and robs a cart on its way home") {
    Town town;
    town.place(kKontor, 2, 3);
    town.place(kWeaver, 14, 4);
    town.place(kHouse, 8, 6);  // next to the road, half way
    town.finish({{2, 3}, {14, 4}});
    sim::City& city = town.city();
    city.ensure_ware(kCloth);
    sim::Logistics logistics(town.world, town.buildings, town.figures);

    logistics.spawn_robber(sim::RobberSpawn{0, {8, 6}, 0});
    logistics.spawn_robber(sim::RobberSpawn{0, {8, 6}, 0});  // one per house
    CHECK(logistics.walkers().size() == 1);
    CHECK(first_walker(logistics)->kind == sim::WalkerKind::Robber);

    town.production(14, 4).stock = 4 * 32;
    logistics.handle(needs(town.index(2, 3), sim::kAllWares));
    REQUIRE(logistics.walkers().size() == 2);
    run_until_idle(logistics);
    CHECK(logistics.stats().robberies == 1);
    CHECK(logistics.stats().stolen == 4 * 32);
    CHECK(city.goods[kCloth] == 0);     // never arrived
    CHECK(city.incoming[kCloth] == 0);  // the room it reserved is free again
    CHECK(town.production(14, 4).stock == 0);
}

TEST_CASE("logistics: a robber without a victim gives up after half a minute") {
    Town town;
    town.place(kKontor, 2, 3);
    town.place(kHouse, 12, 6);
    town.finish({{2, 3}});
    sim::Logistics logistics(town.world, town.buildings, town.figures);
    logistics.spawn_robber(sim::RobberSpawn{0, {8, 6}, 0});
    const std::uint32_t ticks = run_until_idle(logistics);
    CHECK(ticks == 30 * 20);
    CHECK(logistics.stats().robberies == 0);
}

TEST_CASE("logistics: with S-056 brought back a robber takes only figures going to the middle of their building") {
    for (const bool original : {false, true}) {
        Town town;
        town.place(kMill, 9, 3);
        town.place(kDoorBakery, 16, 6);  // its carrier comes home to the door, not the middle
        town.place(kHouse, 9, 6);
        town.finish({{9, 3}, {16, 6}});
        sim::OriginalBugs bugs;
        bugs.robber_victims = original;
        town.world.set_original_bugs(bugs);
        town.production(9, 3).stock = 3 * 32;
        sim::Logistics logistics(town.world, town.buildings, town.figures);
        logistics.spawn_robber(sim::RobberSpawn{0, {9, 6}, 0});
        logistics.handle(needs(town.index(16, 6), kFlour));
        REQUIRE(logistics.walkers().size() == 2);
        run_until_idle(logistics);
        CHECK(logistics.stats().robberies == (original ? 0U : 1U));
    }
}

TEST_CASE("logistics: with S-058 brought back a fleeing robber plans 12 tiles and is gone there") {
    for (const bool original : {false, true}) {
        Town town;
        town.place(kKontor, 2, 3);
        town.place(kWeaver, 14, 4);
        town.place(kHouse, 26, 7);  // the only house: far from where he catches the cart
        town.finish({{2, 3}, {14, 4}});
        sim::OriginalBugs bugs;
        bugs.robber_flight = original;
        town.world.set_original_bugs(bugs);
        town.city().ensure_ware(kCloth);
        sim::Logistics logistics(town.world, town.buildings, town.figures);
        logistics.spawn_robber(sim::RobberSpawn{0, {8, 6}, 0});
        town.production(14, 4).stock = 4 * 32;
        logistics.handle(needs(town.index(2, 3), sim::kAllWares));
        std::size_t flight = 0;
        for (std::uint32_t n = 0; n < 20'000 && logistics.walkers().size() != 0; ++n) {
            logistics.tick();
            const sim::Walker* w = first_walker(logistics);
            if (logistics.stats().robberies == 1 && w != nullptr && w->kind == sim::WalkerKind::Robber &&
                w->returning) {
                flight = w->path.size();
                break;
            }
        }
        REQUIRE(logistics.stats().robberies == 1);
        if (original) {
            CHECK(flight == 12);
        } else {
            CHECK(flight > 12);
        }
    }
}

TEST_CASE("logistics: with S-002 brought back a carrier walks on and delivers to what stands in his place") {
    for (const bool original : {false, true}) {
        Town town;
        town.place(kMill, 10, 3);
        town.place(kBakery, 16, 6);
        town.finish({{10, 3}, {16, 6}});
        sim::OriginalBugs bugs;
        bugs.goldsheep = original;
        town.world.set_original_bugs(bugs);
        town.production(10, 3).stock = 3 * 32;
        sim::Logistics logistics(town.world, town.buildings, town.figures);
        logistics.handle(needs(town.index(16, 6), kFlour));
        REQUIRE(logistics.walkers().size() == 1);
        // The bakery is demolished while its carrier is on the way and built again on the same place.
        const auto removed = town.world.remove_production(0, 16, 6);
        REQUIRE(removed.has_value());
        const std::uint32_t gone[] = {*removed};
        logistics.buildings_removed(gone);
        CHECK(logistics.walkers().size() == (original ? 1U : 0U));
        sim::Production again;
        again.island = 0;
        again.x = 16;
        again.y = 6;
        town.world.set_production(again);
        run_until_idle(logistics);
        CHECK(town.production(16, 6).raw == (original ? 3U * 32U : 0U));
    }
}
