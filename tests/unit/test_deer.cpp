// SPDX-License-Identifier: GPL-3.0-or-later
// Deer and hunters.

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kTree = 20202;
constexpr std::uint32_t kGrass = 20102;
constexpr std::uint32_t kGrassGrowing = 20103;
constexpr std::uint32_t kLodge = 20540;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20201\n  Kind: 10\n  Gfx: 1\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 900\n  EndObj\n"
        "  @Nummer: 2\n  Id: 20202\n  Kind: 10\n  Gfx: 6\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20103\n  Kind: 11\n  Gfx: 7\n  Size: 1, 1\n  AnimAnz: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 300\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20102\n  Kind: 11\n  Gfx: 8\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 52\n  EndObj\n"
        "  @Nummer: 5\n  Id: 20540\n  Kind: 14\n  Gfx: 9\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 5\n    Ware: 14\n    Rohstoff: 56\n    Rohmenge: 1\n    Maxlager: 3\n"
        "    Interval: 15\n    Figurnr: 79\n    Figuranz: 1\n    Radius: 8\n  EndObj\n"
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
        "  Nummer: 79\n  Blocknr: 4\n  Speed: 220\n  Rotate: 8\n  Worktime: 2\n  Gfx: 0\n"
        "  Nummer: 89\n  Blocknr: 5\n  Speed: 160\n  Rotate: 8\n  Worktime: 12\n  Gfx: 0\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A forest (x 4..10, y 4..10) with a meadow east of it (x 11..15) and a hunting lodge.
struct Forest {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world;

    explicit Forest(bool lodge = true) {
        sim::IslandMap map(30, 30);
        for (std::int32_t y = 0; y < 30; ++y) {
            for (std::int32_t x = 0; x < 30; ++x) {
                std::uint32_t id = kGround;
                if (x >= 4 && x <= 10 && y >= 4 && y <= 10) {
                    id = kTree;
                } else if (x >= 11 && x <= 15 && y >= 4 && y <= 10) {
                    id = kGrass;
                }
                REQUIRE(map.place(*buildings.by_id(id), x, y, 0, 0, 0, 0).ok());
            }
        }
        if (lodge) {
            REQUIRE(map.place(*buildings.by_id(kLodge), 13, 13, 0, 0, 0, 0).ok());
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 0, 0}).ok());
        sim::City city;
        city.owner = 0;
        world.add_city(city);
        std::vector<sim::Player> players(1);
        players[0].role = sim::PlayerRole::Human;
        world.set_players(players);
        if (lodge) {
            sim::Production p;
            p.x = 13;
            p.y = 13;
            world.set_production(p);
        }
    }
    void tick(sim::Logistics& logistics, std::uint32_t ticks) {
        for (std::uint32_t i = 0; i < ticks; ++i) {
            logistics.tick();
            world.set_time_ms(world.time_ms() + 50);
        }
    }
    std::size_t deer(const sim::Logistics& logistics) const {
        std::size_t n = 0;
        logistics.walkers().for_each(
            [&](sim::Handle, const sim::Walker& w) { n += w.kind == sim::WalkerKind::Deer ? 1U : 0U; });
        return n;
    }
};

}  // namespace

TEST_CASE("deer: graze on the meadow, rest under trees, eat the grass") {
    Forest forest(false);
    forest.world.island(0).deer_spots[0] = sim::DeerSpot{true, 9, 7, 0};
    sim::Logistics logistics(forest.world, forest.buildings, forest.figures);
    logistics.spawn_deer_at_spots();
    REQUIRE(forest.deer(logistics) == 1);
    sim::Walker deer;
    logistics.walkers().for_each([&](sim::Handle, const sim::Walker& w) { deer = w; });
    REQUIRE(deer.field.has_value());
    CHECK(deer.ware == 52);  // grass first
    // The place is about 4 steps away: the original's wavefront takes the first goal from round 5
    // on, here 2 diagonal and 1 straight step (cost 246; an independent model
    // gives the same tile).
    CHECK(*deer.field == (sim::TilePos{12, 5}));
    CHECK(deer.path.size() == 3);
    CHECK(deer.path.back() == (sim::TilePos{12, 5}));
    CHECK(deer.figure == 89);
    // Two grass stops, then a tree; each lasts 12 s, grass is eaten.
    forest.tick(logistics, 20 * 120);
    std::size_t eaten = 0;
    for (std::int32_t y = 4; y <= 10; ++y) {
        for (std::int32_t x = 11; x <= 15; ++x) {
            const auto& cell =
                forest.world.islands()[0].map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            eaten += forest.buildings.by_sprite(cell.sprite)->id == kGrassGrowing ? 1U : 0U;
        }
    }
    CHECK(eaten >= 2);
    CHECK(forest.deer(logistics) == 1);
}

TEST_CASE("deer: a grazing deer calls the hunter, who shoots it and brings 1 t") {
    Forest forest;
    forest.world.island(0).deer_spots[0] = sim::DeerSpot{true, 9, 7, 0};
    sim::Logistics logistics(forest.world, forest.buildings, forest.figures);
    logistics.spawn_deer_at_spots();
    bool shot = false;
    for (int i = 0; i < 20 * 120 && logistics.stats().deer_hunted == 0; ++i) {
        forest.tick(logistics, 1);
        logistics.walkers().for_each(
            [&](sim::Handle, const sim::Walker& w) { shot |= w.kind == sim::WalkerKind::Deer && w.shot; });
    }
    CHECK(shot);
    CHECK(logistics.stats().deer_hunted == 1);
    forest.tick(logistics, 20 * 60);  // the hunter walks home
    CHECK(forest.world.productions()[0].raw == 32);
    CHECK(forest.deer(logistics) == 0);
    // The spot waits a minute, then the next deer comes.
    const auto& spot = forest.world.islands()[0].deer_spots[0];
    CHECK(spot.used);
    CHECK(spot.respawn_ms > forest.world.time_ms() - 60'000);
    forest.world.set_time_ms(spot.respawn_ms);
    logistics.advance_islands();
    CHECK(forest.deer(logistics) == 1);
}

TEST_CASE("deer: the census settles deer in dense forest") {
    Forest forest(false);
    sim::Logistics logistics(forest.world, forest.buildings, forest.figures);
    for (int epoch = 0; epoch < 4; ++epoch) {
        logistics.advance_islands();
    }
    // Spots in the 7 x 7 forest: rows 5, 7, 9 with x = 28, 26, ... -> x 4..10 even, 4 of 5 trees.
    std::size_t used = 0;
    for (const auto& s : forest.world.islands()[0].deer_spots) {
        used += s.used ? 1U : 0U;
        if (s.used) {
            CHECK(s.y % 2 == 1);
            CHECK(s.x >= 4);
            CHECK(s.x <= 10);
        }
    }
    CHECK(used >= 1);
    CHECK(used <= 2);  // min(8, (n + 5) / 8) with n = 12 spots
    CHECK(forest.deer(logistics) == used);
    // New places take the highest free slot below the allowed number.
    CHECK(forest.world.islands()[0].deer_spots[1].used);
}

TEST_CASE("deer: without anything to eat a deer falls and its slot is closed up") {
    Forest forest(false);
    auto& slots = forest.world.island(0).deer_spots;
    slots[0] = sim::DeerSpot{true, 25, 25, 0};  // open ground, no grass within 6 tiles
    slots[1] = sim::DeerSpot{true, 9, 7, 0};
    slots[2] = sim::DeerSpot{true, 9, 9, 0};
    sim::Logistics logistics(forest.world, forest.buildings, forest.figures);
    logistics.spawn_deer_at_spots();
    CHECK(forest.deer(logistics) == 3);
    // The slot goes at once, the last used slot moves into the hole.
    CHECK(slots[0].used);
    CHECK(slots[0].x == 9);
    CHECK(slots[0].y == 9);
    CHECK(slots[1].x == 9);
    CHECK(slots[1].y == 7);
    CHECK(!slots[2].used);
    forest.tick(logistics, 20 * 6);  // falls for 5 s
    CHECK(forest.deer(logistics) == 2);
}

TEST_CASE("deer: a cleared spot is given up, its deer lives on without it") {
    Forest forest(false);
    auto& slots = forest.world.island(0).deer_spots;
    slots[0] = sim::DeerSpot{true, 9, 7, 0};
    sim::Logistics logistics(forest.world, forest.buildings, forest.figures);
    logistics.spawn_deer_at_spots();
    REQUIRE(forest.deer(logistics) == 1);
    // The middle tree is felled; when the expired slot is looked at, it is given up.
    REQUIRE(forest.world.island(0).map.replace(*forest.buildings.by_id(kGround), 9, 7, 0, 0).ok());
    forest.world.set_time_ms(1000);
    logistics.advance_islands();
    CHECK(!slots[0].used);
    CHECK(forest.deer(logistics) == 1);
}
