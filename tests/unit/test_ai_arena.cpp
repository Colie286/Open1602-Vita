// SPDX-License-Identifier: GPL-3.0-or-later
// The computer players over the first half hour of a game, without the original data (BT-1): a
// synthetic table with the buildings of the founding and pioneer stages (market, houses, woodcutter,
// fishing hut, chapel), two computers on two islands. They found their cities and build them up
// through site search, build orders and roads, run the same every time and through a savegame.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "golden.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/file.hpp"
#include "o1602/sim/ai.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kSea = 21201;
constexpr std::uint32_t kShoal = 21212;
constexpr std::uint32_t kBeach = 20501;
constexpr std::uint32_t kTree = 20202;
constexpr std::uint32_t kKontor = 22101;
constexpr std::uint32_t kMarket = 20809;
constexpr std::uint32_t kWoodcutter = 20701;
constexpr std::uint32_t kFisher = 20711;
constexpr std::uint32_t kChapel = 20803;
constexpr std::uint32_t kRoadDirt = 20411;
constexpr std::uint32_t kWebstube = 20507;
constexpr std::uint32_t kYard = 22115;
constexpr std::uint16_t kShipFigure = 21;

constexpr sim::PlayerId kFirst = 0;
constexpr sim::PlayerId kSecond = 1;
constexpr sim::PlayerId kTrader = 2;
constexpr sim::PlayerId kNatives = 3;

/// With `yard`, a weaving hut and the small shipyard too, so that a computer wants a yard.
assets::BuildingTable make_buildings(bool yard = false) {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}, {"BGRUPPE_WARE", 6}};
    const std::string yard_records =
        "  @Nummer: +1\n  Id: 20507\n  Kind: 14\n  Gfx: 90\n  Size: 2, 2\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Ware: 19\n    Rohstoff: 4\n    Rohmenge: 1\n    Prodmenge: 1\n"
        "    Maxlager: 4\n    Interval: 20\n    Kosten: 5, 2\n    Arbeiter: 2\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n"
        "  @Nummer: +1\n  Id: 22115\n  Kind: 36\n  Gfx: 100\n  Size: 2, 3\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 14\n    Rohstoff: 23\n    Rohmenge: 2\n    Kosten: 30, 12\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 200\n  EndObj\n";
    const auto doc = assets::cod::parse(
        "Nahrung: 1.3\n"
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 14, 0.6\n  EndObj\n"
        "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 14, 0.6\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: +1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: +1\n  Id: 20501\n  Kind: 23\n  Gfx: 2\n  Size: 1, 1\n  Destroyflg: 1\n"
        // A tree: growing, then ripe for the woodcutter.
        "  @Nummer: +1\n  Id: 20201\n  Kind: 10\n  Gfx: 3\n  Size: 1, 1\n  AnimAnz: 4\n  AnimAdd: 1\n"
        "  Wegspeed: 145, 120, 170, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 160\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20202\n  Kind: 10\n  Gfx: 7\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        // Fish shoals in the sea: growing, then ripe for the fishing hut.
        "  @Nummer: +1\n  Id: 21211\n  Kind: 19\n  Gfx: 8\n  Size: 1, 1\n  AnimAnz: 6\n  AnimAdd: 1\n"
        "  AnimTime: 130\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 450\n  EndObj\n"
        "  @Nummer: +1\n  Id: 21212\n  Kind: 19\n  Gfx: 14\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 57\n  EndObj\n"
        // Roads: stone and dirt, each a family of straight, curve, T and cross.
        "  @Nummer: +1\n  Id: 20401\n  Kind: 1\n  Gfx: 15\n  Size: 1, 1\n  KreuzBase: 7\n"
        "  Wegspeed: 100, 100, 100, 100\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20402\n  Kind: 1\n  Gfx: 16\n  Size: 1, 1\n  KreuzBase: 7\n  Wegspeed: 100, 100, 100, "
        "100\n"
        "  @Nummer: +1\n  Id: 20403\n  Kind: 1\n  Gfx: 17\n  Size: 1, 1\n  KreuzBase: 7\n  Wegspeed: 100, 100, 100, "
        "100\n"
        "  @Nummer: +1\n  Id: 20404\n  Kind: 1\n  Gfx: 18\n  Size: 1, 1\n  KreuzBase: 7\n  Wegspeed: 100, 100, 100, "
        "100\n"
        "  @Nummer: +1\n  Id: 20411\n  Kind: 1\n  Gfx: 19\n  Size: 1, 1\n  KreuzBase: 11\n"
        "  Wegspeed: 100, 100, 100, 100\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 2\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20412\n  Kind: 1\n  Gfx: 20\n  Size: 1, 1\n  KreuzBase: 11\n  Wegspeed: 100, 100, 100, "
        "100\n"
        "  @Nummer: +1\n  Id: 20413\n  Kind: 1\n  Gfx: 21\n  Size: 1, 1\n  KreuzBase: 11\n  Wegspeed: 100, 100, 100, "
        "100\n"
        "  @Nummer: +1\n  Id: 20414\n  Kind: 1\n  Gfx: 22\n  Size: 1, 1\n  KreuzBase: 11\n  Wegspeed: 100, 100, 100, "
        "100\n"
        // Kontor I at the beach.
        "  @Nummer: +1\n  Id: 22101\n  Kind: 35\n  Gfx: 30\n  Size: 2, 3\n  Wegspeed: 120, 120, 120, 100\n  Strandflg: "
        "1\n  PlaceFlg: 1\n"
        "  KreuzBase: 15\n  Ausbauflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Ware: 1\n    Rohstoff: 1\n    Rohmenge: 1\n    Radius: 14\n    "
        "Maxlager: 30\n    Figurnr: 82\n    Figuranz: 1\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n"
        // Market, pioneers' house, chapel.
        "  @Nummer: +1\n  Id: 20809\n  Kind: 14\n  Gfx: 40\n  Size: 4, 3\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Ware: 1\n    Rohstoff: 1\n    Rohmenge: 1\n    Radius: 16\n    "
        "Maxlager: 30\n    Figurnr: 82\n    Figuranz: 2\n    Kosten: 10, 5\n"
        "    Arbeiter: 4\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 4\n    Money: 200\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20605\n  Kind: 14\n  Gfx: 50\n  Size: 2, 2\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 20\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20803\n  Kind: 14\n  Gfx: 60\n  Size: 2, 1\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 18\n    Radius: 8\n    Kosten: 5\n    Arbeiter: 2\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 2\n    Money: 100\n  EndObj\n"
        // Woodcutter (wood from trees) and fishing hut (food from fish).
        "  @Nummer: +1\n  Id: 20701\n  Kind: 14\n  Gfx: 70\n  Size: 2, 2\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 23\n    Rohstoff: 53\n    Rohmenge: 1\n    Prodmenge: 1\n    "
        "Maxlager: 4\n"
        "    Interval: 20\n    Figurnr: 75\n    Radius: 5\n    Kosten: 5, 2\n    Arbeiter: 2\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20711\n  Kind: 28\n  Gfx: 80\n  Size: 1, 1\n  Wegspeed: 120, 120, 120, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 6\n    Ware: 14\n    Rohstoff: 57\n    Rohmenge: 1\n    Prodmenge: 1\n    "
        "Maxlager: 4\n"
        "    Interval: 20\n    Figurnr: 86\n    Radius: 6\n    Kosten: 5, 2\n    Arbeiter: 2\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 2\n    Money: 100\n  EndObj\n" +
            (yard ? yard_records : std::string()) + "EndObj\n",
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
        "  Nummer: 75\n  Blocknr: 4\n  Speed: 220\n  Rotate: 8\n  Worktime: 8\n  Gfx: 0\n"
        "  Nummer: 86\n  Blocknr: 4\n  Speed: 320\n  Rotate: 8\n  Worktime: 14.5\n  Maxtrag: 3\n  Gfx: 0\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of 150 x 80 tiles of sea with two islands of 50 x 50 tiles, at (10, 15) and (90, 15):
/// open ground, a wood (columns 28..37), a beach facing east (column 46) and sea with fish (columns
/// 47..49). Players 0 and 1 are computers, each with a ship east of its island; 2 the trader, 3 the
/// natives. With `yard`, the buildings of make_buildings(true) and on each island a lake (columns
/// 38..40, rows 18..32) behind the coast, closer to the city's roads than the sea, with a beach on
/// its east shore (column 41).
struct Arena {
    bool yard = false;
    assets::BuildingTable buildings;
    assets::FigureTable figures = make_figures();
    sim::World world{150, 80};

    explicit Arena(bool with_yard = false) : yard(with_yard), buildings(make_buildings(with_yard)) {
        add_island(10, 15);
        add_island(90, 15);
        std::vector<sim::Player> players(4);
        for (const sim::PlayerId p : {kFirst, kSecond}) {
            players[p].role = sim::PlayerRole::Computer;
            players[p].money = 20000;
        }
        sim::make_neutral(players[kTrader], sim::faction::kTrader);
        sim::make_neutral(players[kNatives], sim::faction::kNatives);
        world.set_players(players);
        add_ship(kFirst, 66, 40);
        add_ship(kSecond, 146, 40);
    }

    void add_island(std::uint32_t at_x, std::uint32_t at_y) {
        sim::IslandMap map(50, 50);
        for (std::int32_t y = 0; y < 50; ++y) {
            for (std::int32_t x = 0; x < 50; ++x) {
                std::uint32_t id = kGround;
                const bool lake_row = yard && y >= 18 && y <= 32;
                if (lake_row && x >= 38 && x <= 40) {
                    id = kSea;
                } else if (lake_row && x == 41) {
                    id = kBeach;
                } else if (x > 46) {
                    id = x >= 48 && y % 2 == 0 ? kShoal : kSea;
                } else if (x == 46) {
                    id = kBeach;
                } else if (x >= 28 && x <= 37) {
                    id = kTree;
                }
                REQUIRE(map.place(*buildings.by_id(id), x, y, id == kBeach ? 3 : 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), at_x, at_y};
        island.header.number = static_cast<std::uint8_t>(world.islands().size());
        island.header.width = 50;
        island.header.height = 50;
        island.header.fertility = 0xFFFFFFFFU;
        REQUIRE(world.add_island(std::move(island)).ok());
    }

    void add_ship(sim::PlayerId owner, std::uint32_t x, std::uint32_t y) {
        sim::Ship s = sim::make_ship("Ship", x, y, kShipFigure, owner, 2);
        s.cargo.resize(4);
        world.add_ship(s);
    }

    /// Buildings of `owner` by Id, each counted once at its origin.
    [[nodiscard]] std::map<std::uint32_t, std::size_t> built(sim::PlayerId owner) const {
        std::map<std::uint32_t, std::size_t> n;
        for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
            const sim::IslandMap& map = world.islands()[i].map;
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::TilePos at{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
                    const auto b = sim::building_at(world, buildings, i, at);
                    if (b && b->origin.x == at.x && b->origin.y == at.y && map.top(x, y).owner == owner) {
                        ++n[b->type->id];
                    }
                }
            }
        }
        return n;
    }

    [[nodiscard]] std::size_t houses(sim::PlayerId owner) const {
        return static_cast<std::size_t>(
            std::count_if(world.houses().begin(), world.houses().end(), [&](const sim::House& h) {
                const sim::City* c = world.city(h.island, h.city);
                return c != nullptr && c->owner == owner;
            }));
    }

    [[nodiscard]] std::uint64_t inhabitants(sim::PlayerId owner) const {
        std::uint64_t n = 0;
        for (const sim::City& c : world.cities()) {
            if (c.owner == owner) {
                for (const std::uint32_t k : c.inhabitants) {
                    n += k;
                }
            }
        }
        return n;
    }

    [[nodiscard]] std::size_t cities_of(sim::PlayerId owner) const {
        return static_cast<std::size_t>(std::count_if(world.cities().begin(), world.cities().end(),
                                                      [&](const sim::City& c) { return c.owner == owner; }));
    }
};

void run(sim::Simulation& s, std::uint32_t ms) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
    }
}

std::vector<std::uint8_t> saved(const sim::ComputerPlayers& ai) {
    core::ByteWriter out;
    ai.save(out);
    return out.take();
}

}  // namespace

TEST_CASE("ai arena: two computers found their cities and build them up over half an hour") {
    Arena a;
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 30 * 60'000);
    for (const sim::PlayerId p : {kFirst, kSecond}) {
        CHECK(a.cities_of(p) == 1);
        auto built = a.built(p);
        CHECK(built[kKontor] == 1);
        CHECK(built[kMarket] >= 1);
        CHECK(built[kWoodcutter] >= 1);
        CHECK(built[kFisher] >= 1);
        CHECK(built[kChapel] >= 1);
        CHECK(a.houses(p) >= 8);
        CHECK(built[kRoadDirt] >= 20);
        CHECK(a.inhabitants(p) >= 50);
        CHECK(a.world.players()[p].money > 0);
        if (a.houses(p) < 8 || a.inhabitants(p) < 50) {
            std::printf("  player %u: %llu inhabitants, money %d:", static_cast<unsigned>(p),
                        static_cast<unsigned long long>(a.inhabitants(p)), a.world.players()[p].money);
            for (const auto& [id, n] : built) {
                std::printf(" %u x%zu", id, n);
            }
            std::printf("\n");
        }
    }
}

TEST_CASE("ai arena: a computer builds its shipyard at the open sea, not at a lake inside the island") {
    Arena a(true);
    sim::Simulation s(a.world, a.buildings, a.figures);
    REQUIRE(a.cities_of(kFirst) == 0);
    for (int minute = 0; minute < 10 && a.cities_of(kFirst) == 0; ++minute) {
        run(s, 60'000);
    }
    REQUIRE(a.cities_of(kFirst) == 1);
    run(s, 60'000);
    // A weaving hut in the city makes the computer want a yard, the loss of its ship makes it ask.
    std::uint16_t island = 0;
    std::uint8_t index = 0;
    for (const sim::City& c : a.world.cities()) {
        if (c.owner == kFirst) {
            island = c.island;
            index = c.index;
        }
    }
    bool weaving = false;
    for (std::int32_t y = 0; y < 50 && !weaving; ++y) {
        for (std::int32_t x = 0; x < 50 && !weaving; ++x) {
            sim::BuildOrder order;
            order.player = kFirst;
            order.island = island;
            order.items.push_back(sim::BuildItem{kWebstube, x, y, 0, 0});
            const sim::BuildPlan plan = sim::plan_build(a.world, a.buildings, order);
            if (plan.city == index && plan.items.front().ok()) {
                weaving = sim::execute_build(a.world, a.buildings, order, {}, nullptr).items.front().ok();
            }
        }
    }
    REQUIRE(weaving);
    a.world.erase_ships([](const sim::Ship& ship) { return ship.owner == kFirst; });
    std::optional<sim::TilePos> yard;
    for (int minute = 0; minute < 15 && !yard; ++minute) {
        run(s, 60'000);
        const sim::IslandMap& map = a.world.islands()[island].map;
        for (std::uint32_t y = 0; y < map.height() && !yard; ++y) {
            for (std::uint32_t x = 0; x < map.width() && !yard; ++x) {
                const sim::TilePos at{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
                const auto b = sim::building_at(a.world, a.buildings, island, at);
                if (b && b->type->id == kYard && b->origin.x == at.x && b->origin.y == at.y) {
                    yard = at;
                }
            }
        }
    }
    REQUIRE(yard.has_value());
    CHECK(yard->x >= 43);  // its water side on the beach of column 46, not on the lake's (column 41)
}

TEST_CASE("ai arena: two runs of the same game stay the same, minute by minute") {
    Arena a;
    Arena b;
    sim::Simulation sa(a.world, a.buildings, a.figures);
    sim::Simulation sb(b.world, b.buildings, b.figures);
    for (int minute = 0; minute < 12; ++minute) {
        run(sa, 60'000);
        run(sb, 60'000);
        CHECK(sa.state_hash() == sb.state_hash());
        CHECK(saved(sa.computers()) == saved(sb.computers()));
    }
    CHECK(a.built(kFirst) == b.built(kFirst));
}

TEST_CASE("ai arena: a game saved while the computers build goes on as if never saved") {
    for (const std::uint32_t at : {3 * 60'000U, 7 * 60'000U}) {
        Arena a;
        sim::Simulation sa(a.world, a.buildings, a.figures);
        run(sa, at);
        const auto bytes = sim::write_game(a.world, a.buildings, &sa, sim::SaveOptions{false});
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, a.buildings, nullptr, &a.figures);
        REQUIRE(loaded.ok());
        sim::World world = std::move(loaded).value();
        sim::Simulation sb(world, a.buildings, a.figures);
        CHECK(!sb.state_damaged());
        CHECK(sa.state_hash() == sb.state_hash());
        run(sa, 3 * 60'000);
        run(sb, 3 * 60'000);
        CHECK(sa.state_hash() == sb.state_hash());
        CHECK(saved(sa.computers()) == saved(sb.computers()));
    }
}

// The arena for an hour, the same on every platform (golden hash, DS-2): two computers with 150
// houses each by then, through a savegame every ten minutes. O1602_DETERMINISM_MINUTES runs longer
// or shorter (the golden value is for 60), O1602_HASH_OUT writes the hash of every minute to a file:
// two platforms' files show the first minute they differ.
TEST_CASE("ai arena: an hour of two computers develops the same on every platform (golden hash)") {
    const auto minutes_text = core::environment_variable("O1602_DETERMINISM_MINUTES");
    const unsigned long minutes = minutes_text ? std::stoul(*minutes_text) : 60UL;
    Arena a;
    sim::Simulation s(a.world, a.buildings, a.figures);
    std::string out;
    for (unsigned long minute = 1; minute <= minutes; ++minute) {
        if (minute % 10 == 0) {
            // Half a minute, then through a savegame, then the other half on both.
            run(s, 30'000);
            const auto bytes = sim::write_game(a.world, a.buildings, &s, sim::SaveOptions{false});
            REQUIRE(bytes.ok());
            auto loaded = sim::read_game(*bytes, a.buildings, nullptr, &a.figures);
            REQUIRE(loaded.ok());
            sim::World world = std::move(loaded).value();
            sim::Simulation copy(world, a.buildings, a.figures);
            run(s, 30'000);
            run(copy, 30'000);
            CHECK(copy.state_hash() == s.state_hash());
        } else {
            run(s, 60'000);
        }
        char line[64];
        std::snprintf(line, sizeof line, "%3lu %016llx\n", minute, static_cast<unsigned long long>(s.state_hash()));
        out += line;
    }
    CHECK(!s.state_damaged());
    CHECK(a.houses(kFirst) >= 100);
    CHECK(a.houses(kSecond) >= 100);
    if (const auto path = core::environment_variable("O1602_HASH_OUT")) {
        std::ofstream(*path, std::ios::binary) << out;
    }
    if (minutes == 60) {
        // The computers' state version 4 (a search's target); the revised bread chain by default.
        test::check_golden("arena after an hour", s.state_hash(), 0x0f97fea171afb034ULL);
    }
}

TEST_CASE("ai arena: a computer without ships or yard buys the cheapest ship on offer") {
    // The original's way out for a computer that lost its only ship and cannot plan a yard yet (no
    // weaving hut): the cheapest ship another player offers, while its money covers the price; at
    // war or not, and with the original's rules too.
    Arena a;
    a.world.player(kSecond)->role = sim::PlayerRole::Human;  // keeps its offers as they are
    sim::Simulation s(a.world, a.buildings, a.figures, sim::Rules::original());
    for (int minute = 0; minute < 10 && a.cities_of(kFirst) == 0; ++minute) {
        run(s, 60'000);
    }
    REQUIRE(a.cities_of(kFirst) == 1);
    a.world.erase_ships([](const sim::Ship& ship) { return ship.owner == kFirst; });
    a.add_ship(kSecond, 140, 50);
    a.add_ship(kSecond, 140, 60);
    sim::Ship& dear = a.world.ships()[a.world.ships().size() - 2];
    sim::Ship& cheap = a.world.ships().back();
    dear.for_sale = true;
    dear.sale_factor = 128;
    cheap.for_sale = true;
    cheap.sale_factor = 64;
    const std::uint32_t dear_id = dear.id;
    const std::uint32_t cheap_id = cheap.id;
    a.world.diplomacy().relation(kFirst, kSecond).peace = sim::Treaty::None;
    a.world.diplomacy().relation(kSecond, kFirst).peace = sim::Treaty::None;
    const std::int32_t money = a.world.player(kSecond)->money;
    bool bought = false;
    for (int minute = 0; minute < 15 && !bought; ++minute) {
        run(s, 60'000);
        bought = a.world.ship(cheap_id)->owner == kFirst;
    }
    CHECK(bought);
    CHECK(a.world.ship(dear_id)->owner == kSecond);
    CHECK(a.world.player(kSecond)->money == money + 500);
}
