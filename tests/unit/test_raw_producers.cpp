// SPDX-License-Identifier: GPL-3.0-or-later
// Raw material: growth of crops, farm workers, cattle, fishing boats and mines.

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kWheatGrowing = 21501;   // record 2
constexpr std::uint32_t kWheatRipe = 21502;      // record 3
constexpr std::uint32_t kWheatWithered = 21503;  // record 4
constexpr std::uint32_t kGrassGrowing = 21504;
constexpr std::uint32_t kGrassRipe = 21505;
constexpr std::uint32_t kSea = 21201;
constexpr std::uint32_t kShoalGrowing = 21211;
constexpr std::uint32_t kShoalRipe = 21212;
constexpr std::uint32_t kFarm = 20515;
constexpr std::uint32_t kCattle = 20516;
constexpr std::uint32_t kFisher = 20517;
constexpr std::uint32_t kMountain = 21101;
constexpr std::uint32_t kSmallMine = 22401;
constexpr std::uint32_t kDeepMine = 22402;
constexpr std::uint32_t kGoldMine = 22403;
constexpr std::uint32_t kStonemason = 20513;
constexpr std::uint32_t kQuarry = 22407;

constexpr std::uint8_t kGrain = 45;  // GETREIDE
constexpr std::uint8_t kGrass = 52;  // GRAS
constexpr std::uint8_t kFish = 57;   // FISCHE
constexpr std::uint8_t kOre = 2;     // EISENERZ
constexpr std::uint8_t kGold = 3;    // GOLD
constexpr std::uint8_t kStone = 54;  // STEINE

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n  Wegspeed: 100, 100, 100, 100\n"
        // Wheat: growing (5 stages of 35 s -> class 0), ripe, withered.
        "  @Nummer: 2\n  Id: 21501\n  Kind: 11\n  Gfx: 10\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Wegspeed: 145, 120, 170, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 175\n  EndObj\n"
        "  @Nummer: 3\n  Id: 21502\n  Kind: 11\n  Gfx: 15\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 45\n  EndObj\n"
        "  @Nummer: 4\n  Id: 21503\n  Kind: 11\n  Gfx: 16\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 175\n    Doerrflg: 1\n  EndObj\n"
        // Meadow: one stage of 300 s -> class 31 (257 s).
        "  @Nummer: 5\n  Id: 21504\n  Kind: 11\n  Gfx: 21\n  Size: 1, 1\n  AnimAnz: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 300\n  EndObj\n"
        "  @Nummer: 6\n  Id: 21505\n  Kind: 11\n  Gfx: 22\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 52\n  EndObj\n"
        // Sea and fish shoals (animated: the stage is not drawn).
        "  @Nummer: 7\n  Id: 21201\n  Kind: 19\n  Gfx: 30\n  Size: 1, 1\n"
        "  @Nummer: 8\n  Id: 21211\n  Kind: 19\n  Gfx: 31\n  Size: 1, 1\n  AnimAnz: 6\n  AnimAdd: 1\n  AnimTime: 130\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 450\n  EndObj\n"
        "  @Nummer: 9\n  Id: 21212\n  Kind: 19\n  Gfx: 37\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 57\n  EndObj\n"
        // Farm, cattle farm, fishing hut.
        "  @Nummer: 10\n  Id: 20515\n  Kind: 14\n  Gfx: 40\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 8\n    Rohstoff: 45\n    Rohmenge: 1\n    Maxlager: 4\n"
        "    Interval: 20\n    Figurnr: 75\n    Radius: 5\n  EndObj\n"
        "  @Nummer: 11\n  Id: 20516\n  Kind: 14\n  Gfx: 44\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 4\n    Ware: 7\n    Rohstoff: 52\n    Rohmenge: 1\n    Maxlager: 4\n"
        "    Interval: 20\n    Figurnr: 87\n    Figuranz: 2\n    Radius: 5\n  EndObj\n"
        "  @Nummer: 12\n  Id: 20517\n  Kind: 14\n  Gfx: 48\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 6\n    Ware: 7\n    Rohstoff: 57\n    Rohmenge: 1\n    Maxlager: 4\n"
        "    Interval: 20\n    Figurnr: 86\n    Radius: 6\n  EndObj\n"
        // A mountain and three mines (1 x 2, the mountain on the side the rotation faces).
        "  @Nummer: 13\n  Id: 21101\n  Kind: 15\n  Gfx: 50\n  Size: 3, 3\n"
        "  @Nummer: 14\n  Id: 22401\n  Kind: 34\n  Gfx: 60\n  Size: 1, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 3\n    Ware: 2\n    Rohmenge: 1\n    Erzbergnr: 1\n    Maxlager: 5\n"
        "    Interval: 40\n  EndObj\n"
        "  @Nummer: 15\n  Id: 22402\n  Kind: 34\n  Gfx: 62\n  Size: 1, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 3\n    Ware: 2\n    Rohmenge: 1\n    Erzbergnr: 0\n    Maxlager: 5\n"
        "    Interval: 40\n  EndObj\n"
        "  @Nummer: 16\n  Id: 22403\n  Kind: 34\n  Gfx: 64\n  Size: 1, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 3\n    Ware: 3\n    Rohmenge: 1\n    Erzbergnr: 1\n    Maxlager: 5\n"
        "    Interval: 80\n  EndObj\n"
        // Stonemason (takes stone like a farm) and quarry (PRODTYP 11 STEINBRUCH).
        "  @Nummer: 17\n  Id: 20513\n  Kind: 14\n  Gfx: 70\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 24\n    Rohstoff: 54\n    Rohmenge: 1\n    Maxlager: 4\n"
        "    Interval: 20\n    Figurnr: 75\n    Radius: 6\n  EndObj\n"
        "  @Nummer: 18\n  Id: 22407\n  Kind: 34\n  Gfx: 74\n  Size: 1, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 11\n    Ware: 54\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// MAEHER, SCHAF and FISCHER with the values of figuren.cod.
assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 75\n  Blocknr: 4\n  Speed: 220\n  Rotate: 8\n  Worktime: 8\n  Gfx: 0\n"
        "  Nummer: 86\n  Blocknr: 4\n  Speed: 320\n  Rotate: 8\n  Worktime: 14.5\n  Maxtrag: 3\n  Gfx: 0\n"
        "  Nummer: 87\n  Blocknr: 5\n  Speed: 200\n  Rotate: 8\n  Worktime: 18\n  Maxtrag: 3\n  Gfx: 0\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

struct Farmland {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world;
    sim::IslandMap map{24, 16};

    Farmland() {
        for (std::int32_t y = 0; y < 16; ++y) {
            for (std::int32_t x = 0; x < 24; ++x) {
                REQUIRE(map.place(*buildings.by_id(x >= 18 ? kSea : kGround), x, y, 0, 0, 0, 0).ok());
            }
        }
    }
    void place(std::uint32_t id, std::int32_t x, std::int32_t y, std::uint8_t rotation = 0) {
        REQUIRE(map.place(*buildings.by_id(id), x, y, rotation, 0, 0, 0).ok());
    }
    void finish(const std::vector<std::pair<std::int32_t, std::int32_t>>& productions,
                std::vector<assets::IslandHeader::Deposit> deposits = {}) {
        assets::IslandHeader header;
        header.deposits = std::move(deposits);
        REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), 10, 10}).ok());
        sim::City city;
        city.owner = 0;
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
        sim::add_missing_growth(world, buildings);
    }
    const sim::MapCell& tile(std::int32_t x, std::int32_t y) const {
        return world.islands()[0].map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    }
    std::uint32_t id(std::int32_t x, std::int32_t y) const { return buildings.by_sprite(tile(x, y).sprite)->id; }
    sim::Production& production(std::int32_t x, std::int32_t y) { return world.productions()[index(x, y)]; }
    std::uint32_t index(std::int32_t x, std::int32_t y) const {
        return *world.production_index(0, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
    }
};

sim::ProductionEvent needs(std::uint32_t production, std::uint8_t ware) {
    return sim::ProductionEvent{sim::ProductionEvent::Kind::NeedsRawGoods, production, 0, ware};
}

std::uint32_t run_until_idle(sim::Logistics& logistics, std::uint32_t max = 40'000) {
    std::uint32_t n = 0;
    while (logistics.walkers().size() != 0 && n < max) {
        logistics.tick();
        ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("growth: classes and record chains of crops") {
    const auto buildings = make_buildings();
    CHECK(buildings.by_id(kWheatGrowing)->growth_class == 0);   // 35 s per stage: below 47 s
    CHECK(buildings.by_id(kGrassGrowing)->growth_class == 31);  // 300 s, clamped
    CHECK(buildings.by_id(kShoalGrowing)->growth_class == 5);   // 75 s per stage: 40 + 7 * 5
    CHECK(buildings.by_id(kWheatWithered)->withered);
    CHECK(sim::growth_period_ms(0) == 40'000);
    CHECK(sim::growth_period_ms(31) == 257'000);
    CHECK(sim::ripe_record(buildings, *buildings.by_id(kWheatGrowing)) == buildings.by_id(kWheatRipe));
    CHECK(sim::growing_record(buildings, *buildings.by_id(kWheatRipe)) == buildings.by_id(kWheatGrowing));
    CHECK(sim::ripe_record(buildings, *buildings.by_id(kWheatWithered)) == nullptr);
    CHECK(sim::crop_ware(buildings, *buildings.by_id(kWheatGrowing)) == kGrain);
    CHECK(sim::crop_ware(buildings, *buildings.by_id(kGround)) == 0);
}

TEST_CASE("growth: a field grows stage by stage, ripens and is harvested") {
    Farmland land;
    land.place(kWheatGrowing, 3, 3);
    land.place(kWheatWithered, 4, 3);
    land.finish({});
    sim::World& world = land.world;
    REQUIRE(world.growth().entries.size() == 1);  // withered tiles do not grow
    // Five ticks of class 0 (40 s each); the stage shows as the animation phase.
    for (int stage = 1; stage <= 4; ++stage) {
        CHECK(sim::advance_growth(world, land.buildings, 40'000) == 0);
        CHECK(land.tile(3, 3).anim_phase == stage);
    }
    CHECK(sim::advance_growth(world, land.buildings, 39'950) == 0);
    CHECK(sim::advance_growth(world, land.buildings, 50) == 1);
    CHECK(land.id(3, 3) == kWheatRipe);
    CHECK(world.growth().entries.empty());
    // Harvest: growing again from stage 0; the wrong ware or a growing tile gives nothing.
    CHECK(!sim::harvest_tile(world, land.buildings, 0, 3, 3, kGrass));
    CHECK(sim::harvest_tile(world, land.buildings, 0, 3, 3, kGrain));
    CHECK(land.id(3, 3) == kWheatGrowing);
    CHECK(land.tile(3, 3).anim_phase == 0);
    CHECK(world.growth().entries.size() == 1);
    CHECK(!sim::harvest_tile(world, land.buildings, 0, 3, 3, kGrain));
    // The clock keeps the rest of a step: 4 x 30 s are three ticks of 40 s.
    std::size_t ripened = 0;
    for (int i = 0; i < 4; ++i) {
        ripened += sim::advance_growth(world, land.buildings, 30'000);
    }
    CHECK(ripened == 0);
    CHECK(land.tile(3, 3).anim_phase == 3);
}

TEST_CASE("raw producers: a farm worker harvests the nearest ripe field") {
    Farmland land;
    land.place(kFarm, 4, 4);
    land.place(kWheatRipe, 7, 4);  // nearer
    land.place(kWheatRipe, 4, 8);
    land.place(kWheatGrowing, 6, 4);
    land.finish({{4, 4}});
    sim::Logistics logistics(land.world, land.buildings, land.figures);
    logistics.handle(needs(land.index(4, 4), kGrain));
    REQUIRE(logistics.walkers().size() == 1);
    sim::Walker walker;
    logistics.walkers().for_each([&](sim::Handle, const sim::Walker& w) { walker = w; });
    CHECK(walker.kind == sim::WalkerKind::Harvester);
    REQUIRE(walker.field.has_value());
    CHECK((*walker.field == sim::TilePos{7, 4}));
    CHECK(walker.anim() == 1);
    // One worker per farm.
    logistics.handle(needs(land.index(4, 4), kGrain));
    CHECK(logistics.walkers().size() == 1);
    // Walk a little, then work for Worktime (8 s): the work animation runs.
    bool worked = false;
    for (int i = 0; i < 2000 && logistics.walkers().size() != 0; ++i) {
        logistics.tick();
        logistics.walkers().for_each(
            [&](sim::Handle, const sim::Walker& w) { worked |= w.working() && w.anim() == 2; });
    }
    CHECK(worked);
    CHECK(logistics.walkers().size() == 0);
    CHECK(land.production(4, 4).raw == 32);  // exactly a ton
    CHECK(land.id(7, 4) == kWheatGrowing);
    CHECK(logistics.stats().harvested == 32);
    // The next one takes the other field; with nothing ripe nobody goes.
    logistics.handle(needs(land.index(4, 4), kGrain));
    run_until_idle(logistics);
    CHECK(land.id(4, 8) == kWheatGrowing);
    logistics.handle(needs(land.index(4, 4), kGrain));
    CHECK(logistics.walkers().size() == 0);
}

TEST_CASE("raw producers: the stonemason works the quarry, which never runs out") {
    Farmland land;
    land.place(kStonemason, 4, 4);
    land.place(kQuarry, 8, 4);
    land.finish({{4, 4}});
    sim::Logistics logistics(land.world, land.buildings, land.figures);
    for (int trip = 1; trip <= 3; ++trip) {
        logistics.handle(needs(land.index(4, 4), kStone));
        REQUIRE(logistics.walkers().size() == 1);
        logistics.walkers().for_each([&](sim::Handle, const sim::Walker& w) {
            CHECK(w.kind == sim::WalkerKind::Harvester);
            REQUIRE(w.field.has_value());
            CHECK(w.field->x == 8);
        });
        run_until_idle(logistics);
        CHECK(land.production(4, 4).raw == 32U * static_cast<std::uint32_t>(trip));
        CHECK(land.id(8, 4) == kQuarry);
    }
}

TEST_CASE("raw producers: cattle graze until full, two farms never share a tile") {
    Farmland land;
    land.place(kCattle, 4, 4);
    for (std::int32_t x = 2; x < 9; ++x) {
        land.place(kGrassRipe, x, 7);
    }
    land.finish({{4, 4}});
    sim::Logistics logistics(land.world, land.buildings, land.figures);
    logistics.handle(needs(land.index(4, 4), kGrass));
    logistics.handle(needs(land.index(4, 4), kGrass));
    logistics.handle(needs(land.index(4, 4), kGrass));  // Figuranz 2
    REQUIRE(logistics.walkers().size() == 2);
    std::vector<sim::TilePos> fields;
    logistics.walkers().for_each([&](sim::Handle, const sim::Walker& w) { fields.push_back(*w.field); });
    CHECK(!(fields[0] == fields[1]));
    run_until_idle(logistics);
    // Each animal eats three tiles (Maxtrag 3 t) and brings 3 t.
    CHECK(land.production(4, 4).raw == 6 * 32);
    std::size_t eaten = 0;
    for (std::int32_t x = 2; x < 9; ++x) {
        eaten += land.id(x, 7) == kGrassGrowing ? 1U : 0U;
    }
    CHECK(eaten == 6);
}

TEST_CASE("raw producers: a fishing boat fishes shoals on the sea") {
    Farmland land;
    land.place(kFisher, 17, 6);  // on the coast, the sea begins at x = 18
    land.place(kShoalRipe, 20, 4);
    land.place(kShoalRipe, 21, 9);
    land.place(kShoalRipe, 22, 7);
    land.finish({{17, 6}});
    sim::Logistics logistics(land.world, land.buildings, land.figures);
    logistics.handle(needs(land.index(17, 6), kFish));
    REQUIRE(logistics.walkers().size() == 1);
    // The boat stays on water.
    bool on_land = false;
    for (int i = 0; i < 20'000 && logistics.walkers().size() != 0; ++i) {
        logistics.tick();
        logistics.walkers().for_each([&](sim::Handle, const sim::Walker& w) { on_land |= w.tile.x < 17; });
    }
    CHECK(!on_land);
    CHECK(logistics.walkers().size() == 0);
    CHECK(land.production(17, 6).raw == 3 * 32);
    CHECK(land.id(20, 4) == kShoalGrowing);
}

TEST_CASE("raw producers: mines take ore from their deposit") {
    Farmland land;
    land.place(kMountain, 4, 4);   // 4..6 x 4..6
    land.place(kSmallMine, 7, 4);  // rotation 0: the mountain is at x - 1
    land.place(kDeepMine, 7, 6);
    land.place(kMountain, 10, 10);
    land.place(kGoldMine, 13, 10);
    // A big iron vein with 121 t and a small gold vein.
    land.finish({{7, 4}, {7, 6}, {13, 10}},
                {{kOre, 4, 4, 0, 0, static_cast<std::uint16_t>(121 * 32)}, {kGold, 10, 10, 0, 1, 50 * 32}});
    sim::Logistics logistics(land.world, land.buildings, land.figures);
    auto& deposits = land.world.island(0).header.deposits;
    logistics.handle(needs(land.index(7, 4), 0));
    CHECK(land.production(7, 4).raw == 32);
    CHECK(deposits[0].amount == 120 * 32);
    // At 120 t the big vein still serves every mine; below only the deep mine.
    logistics.handle(needs(land.index(7, 4), 0));
    CHECK(land.production(7, 4).raw == 64);
    CHECK(deposits[0].amount == 119 * 32);
    logistics.handle(needs(land.index(7, 4), 0));
    CHECK(land.production(7, 4).raw == 64);
    logistics.handle(needs(land.index(7, 6), 0));
    CHECK(land.production(7, 6).raw == 32);
    CHECK(deposits[0].amount == 118 * 32);
    // Gold never runs out.
    logistics.handle(needs(land.index(13, 10), 0));
    CHECK(land.production(13, 10).raw == 32);
    CHECK(deposits[1].amount == 50 * 32);
    // An exhausted vein disappears.
    deposits[0].amount = 32;
    logistics.handle(needs(land.index(7, 6), 0));
    CHECK(deposits.size() == 1);
    CHECK(deposits[0].ware == kGold);
    CHECK(logistics.stats().ore_mined == 5 * 32);
}

TEST_CASE("raw producers: with endless veins the deep mine digs a big vein for ever") {
    Farmland land;
    land.place(kMountain, 4, 4);
    land.place(kSmallMine, 7, 4);
    land.place(kDeepMine, 7, 6);
    land.place(kMountain, 10, 10);
    land.place(kSmallMine, 13, 10);
    // A big iron vein with 121 t and a small one with 2 t.
    land.finish({{7, 4}, {7, 6}, {13, 10}},
                {{kOre, 4, 4, 0, 0, static_cast<std::uint16_t>(121 * 32)}, {kOre, 10, 10, 0, 1, 2 * 32}});
    // As the game says: the settings word's bit 3, which every endless game of the original sets.
    sim::Rules rules;
    CHECK(rules.ore_veins == sim::OreVeins::Game);
    CHECK(!rules.endless_veins(false));
    CHECK(rules.endless_veins(true));
    rules.ore_veins = sim::OreVeins::Finite;
    CHECK(!rules.endless_veins(true));
    rules.ore_veins = sim::OreVeins::Endless;
    CHECK(rules.endless_veins(false));
    rules.ore_veins = sim::OreVeins::Game;
    sim::World::GameSettings settings = land.world.game_settings();
    settings.flags |= sim::World::GameSettings::kEndlessVeins;
    land.world.set_game_settings(settings);
    sim::Logistics logistics(land.world, land.buildings, land.figures, &rules);
    auto& deposits = land.world.island(0).header.deposits;
    // Down to 120 t and to the first ton below it as in the original.
    logistics.handle(needs(land.index(7, 4), 0));
    logistics.handle(needs(land.index(7, 4), 0));
    CHECK(land.production(7, 4).raw == 64);
    CHECK(deposits[0].amount == 119 * 32);
    // The ore mine still stops below 120 t.
    logistics.handle(needs(land.index(7, 4), 0));
    CHECK(land.production(7, 4).raw == 64);
    // The deep mine digs on, and the vein keeps its 119 t.
    for (int i = 0; i < 200; ++i) {
        land.production(7, 6).raw = 0;
        logistics.handle(needs(land.index(7, 6), 0));
        CHECK(land.production(7, 6).raw == 32);
    }
    CHECK(deposits[0].amount == 119 * 32);
    // Small veins still run out.
    logistics.handle(needs(land.index(13, 10), 0));
    logistics.handle(needs(land.index(13, 10), 0));
    CHECK(deposits.size() == 1);
    CHECK(logistics.stats().ore_mined == 204 * 32);

    // The rule stays with the savegame; as the game says, like the original, is the default.
    CHECK(sim::Rules::original().ore_veins == sim::OreVeins::Game);
    for (const auto veins : {sim::OreVeins::Game, sim::OreVeins::Finite, sim::OreVeins::Endless}) {
        sim::Rules saved;
        saved.ore_veins = veins;
        land.world.set_saved_rules(saved);
        auto bytes = sim::write_game(land.world, land.buildings);
        REQUIRE(bytes.ok());
        auto back = sim::read_game(*bytes, land.buildings);
        REQUIRE(back.ok());
        REQUIRE(back->saved_rules().has_value());
        CHECK(back->saved_rules()->ore_veins == veins);
    }
}
