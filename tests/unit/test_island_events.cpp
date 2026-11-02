// SPDX-License-Identifier: GPL-3.0-or-later
// Droughts.

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/island_events.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kGrowing = 21501;
constexpr std::uint32_t kRipe = 21502;
constexpr std::uint32_t kWithered = 21503;
constexpr std::uint32_t kTreeRipe = 21505;
constexpr std::uint8_t kTobacco = 46;  // TABAK: fertility bit 1
constexpr std::uint8_t kTree = 53;     // BAUM: fertility bit 8
constexpr std::uint64_t kEpochMs = 30'000;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        // Tobacco: growing, ripe, withered.
        "  @Nummer: 1\n  Id: 21501\n  Kind: 10\n  Gfx: 10\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 175\n  EndObj\n"
        "  @Nummer: 2\n  Id: 21502\n  Kind: 10\n  Gfx: 15\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 46\n  EndObj\n"
        "  @Nummer: 3\n  Id: 21503\n  Kind: 10\n  Gfx: 16\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 175\n    Doerrflg: 1\n  EndObj\n"
        // A tree: growing and ripe, no withered record.
        "  @Nummer: 4\n  Id: 21504\n  Kind: 10\n  Gfx: 21\n  Size: 1, 1\n  AnimAnz: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 300\n  EndObj\n"
        "  @Nummer: 5\n  Id: 21505\n  Kind: 10\n  Gfx: 22\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of 32 x 8 ripe tobacco fields on fully fertile ground (every pattern position eight
/// times), with a city of a human player.
struct Fields {
    assets::BuildingTable buildings = make_buildings();
    sim::World world{100, 100};
    std::vector<sim::IslandEvent> events;
    sim::Rules rules;

    explicit Fields(std::uint32_t crop = kRipe) {
        sim::IslandMap map(32, 8);
        for (std::int32_t y = 0; y < 8; ++y) {
            for (std::int32_t x = 0; x < 32; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        for (std::int32_t y = 0; y < 8; ++y) {
            for (std::int32_t x = 0; x < 32; ++x) {
                REQUIRE(map.place(*buildings.by_id(crop), x, y, 0, 0, 0, 0).ok());
            }
        }
        assets::IslandHeader header;
        header.fertility = (1U << (kTobacco - 45)) | (1U << (kTree - 45));
        REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), 10, 10}).ok());
        sim::City city;
        city.owner = 0;
        world.add_city(city);
        std::vector<sim::Player> players(1);
        players[0].role = sim::PlayerRole::Human;
        world.set_players(players);
    }

    std::size_t count(std::uint32_t id) const {
        std::size_t n = 0;
        const sim::IslandMap& map = world.islands()[0].map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const assets::BuildingType* b = buildings.by_sprite(map.top(x, y).sprite);
                n += b != nullptr && b->id == id ? 1U : 0U;
            }
        }
        return n;
    }
    const assets::BuildingType* at(std::uint32_t x, std::uint32_t y) const {
        return buildings.by_sprite(world.islands()[0].map.top(x, y).sprite);
    }
    void epoch(const sim::FieldReserved& reserved = {}) {
        world.set_time_ms(world.time_ms() + kEpochMs);
        sim::advance_island_events(world, buildings, rules, reserved, &events);
    }
    std::size_t fertile_at(std::uint8_t strength) const {
        std::size_t n = 0;
        for (std::int32_t y = 0; y < 8; ++y) {
            for (std::int32_t x = 0; x < 32; ++x) {
                n += sim::tile_fertile_at(world, buildings, 0, kTobacco, x, y, strength) ? 1U : 0U;
            }
        }
        return n;
    }
};

}  // namespace

TEST_CASE("drought: the fertility loses strength / 128 before the pattern is chosen") {
    const Fields f;
    // 128 → pattern of 32, 0x40: 64 → 16 of 32, 0x26: 128 − 38 = 90 → 23 of 32.
    CHECK(f.fertile_at(0) == 256);
    CHECK(f.fertile_at(0x40) == 16 * 8);
    CHECK(f.fertile_at(0x26) == 23 * 8);
    // Trees, grass and fish do not suffer.
    std::size_t trees = 0;
    for (std::int32_t y = 0; y < 8; ++y) {
        for (std::int32_t x = 0; x < 32; ++x) {
            trees += sim::tile_fertile_at(f.world, f.buildings, 0, kTree, x, y, 0x40) ? 1U : 0U;
        }
    }
    CHECK(trees == 256);
}

TEST_CASE("drought: ripe fields wither where they bear no more, reserved ones are left to the harvest") {
    Fields f;
    sim::start_drought(f.world, 0, sim::kDroughtStrength, 1'500'000, &f.events);
    REQUIRE(!f.events.empty());
    CHECK(f.events.front().kind == sim::IslandEvent::Kind::DroughtStarted);
    const auto reserved = [](std::uint16_t, sim::TilePos t) { return t.x == 0 && t.y == 0; };
    for (int i = 0; i < 40; ++i) {
        f.epoch(reserved);
    }
    // Every third column per epoch: after 40 epochs every field has been looked at.
    CHECK(f.count(kWithered) == 128 - (sim::tile_fertile_at(f.world, f.buildings, 0, kTobacco, 0, 0, 0x40) ? 0 : 1));
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            if (f.at(x, y)->id == kWithered) {
                CHECK(!sim::tile_fertile_at(f.world, f.buildings, 0, kTobacco, static_cast<std::int32_t>(x),
                                            static_cast<std::int32_t>(y), 0x40));
            }
        }
    }
    CHECK(f.at(0, 0)->id == kRipe);
    // Withered fields have no growth entry.
    CHECK(f.world.growth().entries.empty());
}

TEST_CASE("drought: when it ends the strength halves and the fields grow again") {
    Fields f;
    sim::start_drought(f.world, 0, sim::kDroughtStrength, 1'200'000, nullptr);
    for (int i = 0; i < 39; ++i) {
        f.epoch();
    }
    REQUIRE(f.count(kWithered) == 128);
    REQUIRE(f.world.time_ms() + kEpochMs >= f.world.islands()[0].drought.end_ms);
    f.events.clear();
    // Withered where infertile at 0x40; at 32 (fertility 96, 23 of 32) some of them bear again
    // (the patterns are not nested).
    std::size_t still = 0;
    for (std::int32_t y = 0; y < 8; ++y) {
        for (std::int32_t x = 0; x < 32; ++x) {
            still += !sim::tile_fertile_at(f.world, f.buildings, 0, kTobacco, x, y, 0x40) &&
                             !sim::tile_fertile_at(f.world, f.buildings, 0, kTobacco, x, y, 32)
                         ? 1U
                         : 0U;
        }
    }
    f.epoch();  // 64 → 32
    CHECK(f.world.islands()[0].drought.strength == 32);
    CHECK(f.count(kWithered) == still);
    CHECK(still < 128);
    f.epoch();  // 16
    CHECK(f.world.islands()[0].drought.strength == 16);
    f.epoch();  // 8 → over
    CHECK(f.world.islands()[0].drought.strength == 0);
    CHECK(f.count(kWithered) == 0);
    CHECK(f.count(kGrowing) == 128);
    // One growth entry per recovered field, no more.
    CHECK(f.world.growth().entries.size() == 128);
    bool over = false;
    for (const auto& e : f.events) {
        over |= e.kind == sim::IslandEvent::Kind::DroughtOver;
    }
    CHECK(over);
}

TEST_CASE("drought: a harvest in the drought withers what does not bear, trees go on") {
    Fields f;
    sim::start_drought(f.world, 0, sim::kDroughtStrength, 600'000, nullptr);
    std::size_t withered = 0;
    std::size_t growing = 0;
    for (std::uint16_t x = 0; x < 32; ++x) {
        REQUIRE(sim::harvest_tile(f.world, f.buildings, 0, x, 0, kTobacco));
        const bool fertile = sim::tile_fertile_at(f.world, f.buildings, 0, kTobacco, x, 0, 0x40);
        CHECK(f.at(x, 0)->id == (fertile ? kGrowing : kWithered));
        (fertile ? growing : withered) += 1;
    }
    CHECK(withered == 16);
    CHECK(growing == 16);
    CHECK(f.world.growth().entries.size() == 16);

    Fields trees(kTreeRipe);
    sim::start_drought(trees.world, 0, sim::kDroughtStrength, 600'000, nullptr);
    for (int i = 0; i < 5; ++i) {
        trees.epoch();
    }
    CHECK(trees.count(kTreeRipe) == 256);
}

TEST_CASE("drought: chance by the game's difficulty, at most one per half hour") {
    Fields f;
    f.world.set_game_settings(sim::World::GameSettings{3, 0, 1500});
    f.world.set_next_drought_ms(0);
    std::uint64_t started_at = 0;
    for (int i = 0; i < 200 && started_at == 0; ++i) {
        f.epoch();
        for (const auto& e : f.events) {
            if (e.kind == sim::IslandEvent::Kind::DroughtStarted) {
                started_at = f.world.time_ms();
            }
        }
    }
    REQUIRE(started_at != 0);
    CHECK(f.world.islands()[0].drought.strength == sim::kDroughtStrength);
    CHECK(f.world.islands()[0].drought.end_ms == started_at + 900'000);  // 15 minutes on "hard"
    CHECK(f.world.next_drought_ms() >= started_at + 27 * 60'000);
    CHECK(f.world.next_drought_ms() <= started_at + 34 * 60'000);

    // "No droughts" in the game settings, difficulty easy, or the rule: never.
    for (const auto& [settings, rule] : {std::pair{sim::World::GameSettings{3, 4, 1500}, sim::Droughts::Game},
                                         std::pair{sim::World::GameSettings{0, 0, 1500}, sim::Droughts::Game},
                                         std::pair{sim::World::GameSettings{3, 0, 1500}, sim::Droughts::Off}}) {
        Fields g;
        g.world.set_game_settings(settings);
        g.world.set_next_drought_ms(0);
        g.rules.droughts = rule;
        for (int i = 0; i < 200; ++i) {
            g.epoch();
        }
        CHECK(g.world.islands()[0].drought.strength == 0);
    }
    // The rule overrides the game: frequent droughts in an easy game.
    Fields h;
    h.world.set_game_settings(sim::World::GameSettings{0, 0, 1500});
    h.world.set_next_drought_ms(0);
    h.rules.droughts = sim::Droughts::Frequent;
    bool any = false;
    for (int i = 0; i < 200 && !any; ++i) {
        h.epoch();
        any = h.world.islands()[0].drought.strength != 0;
    }
    CHECK(any);
}

TEST_CASE("drought: a curse of the natives starts a weaker one at once") {
    Fields f;
    f.world.island(0).drought.curse_pending = true;
    f.epoch();
    CHECK(!f.world.islands()[0].drought.curse_pending);
    CHECK(f.world.islands()[0].drought.strength == sim::kCurseStrength);
    // The first scan runs in the same epoch.
    CHECK(f.count(kWithered) > 0);
}
