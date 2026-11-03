// SPDX-License-Identifier: GPL-3.0-or-later
// New games (new_game.hpp) without the original data: the reset of a synthetic
// endless file, the scenario of an own world, and whole new games started from a small synthetic
// installation, with the computer players running on them.

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"
#include "golden.hpp"
#include "test_support.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/sim/ai.hpp"
#include "o1602/sim/climates.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/savegame_extension.hpp"
#include "o1602/sim/score.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/world_plan.hpp"

using namespace o1602;

namespace {

constexpr std::size_t kPlayerSize = 0x430;
constexpr std::size_t kShipSize = 0x1B4;
constexpr std::size_t kEndlessTimersSize = 0x294;
constexpr std::uint16_t kShipFigure = 21;       // HANDEL1, the small trading cog
constexpr std::uint16_t kLargeShipFigure = 23;  // HANDEL2, the large one

// Template files per climate and size class, as the original draws them.
constexpr std::array<std::array<std::uint16_t, 5>, 2> kTemplateCount = {{{51, 48, 36, 67, 5}, {49, 45, 48, 63, 6}}};
constexpr std::array<const char*, 2> kClimateDir = {"Nord", "Sued"};
constexpr std::array<const char*, 2> kNativeDir = {"NordNat", "SuedNat"};
constexpr std::array<const char*, 5> kSizeName = {"lit", "mit", "med", "big", "lar"};

void put_u16(std::vector<std::uint8_t>& d, std::size_t o, std::uint32_t v) {
    d[o] = static_cast<std::uint8_t>(v);
    d[o + 1] = static_cast<std::uint8_t>(v >> 8);
}

void put_u32(std::vector<std::uint8_t>& d, std::size_t o, std::uint32_t v) {
    put_u16(d, o, v);
    put_u16(d, o + 2, v >> 16);
}

std::uint32_t u32(std::span<const std::uint8_t> d, std::size_t o) {
    return std::uint32_t{d[o]} | (std::uint32_t{d[o + 1]} << 8) | (std::uint32_t{d[o + 2]} << 16) |
           (std::uint32_t{d[o + 3]} << 24);
}

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

const assets::BuildingTable& buildings() {
    static const assets::BuildingTable table = make_buildings();
    return table;
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

/// PLAYER4 of a level's file: seven records with money, the editor's control and colour bytes,
/// leftovers in the second name and the relation bytes.
std::vector<std::uint8_t> level_players() {
    std::vector<std::uint8_t> d(7 * kPlayerSize, 0);
    for (std::size_t slot = 0; slot < 7; ++slot) {
        const std::size_t o = slot * kPlayerSize;
        put_u32(d, o, static_cast<std::uint32_t>(30'000 + 1'000 * slot));
        d[o + 4] = 0x0C;
        d[o + 7] = static_cast<std::uint8_t>(slot);
        d[o + 0x40] = 0x5A;  // relations: kept
        std::fill_n(d.begin() + static_cast<std::ptrdiff_t>(o + 0x3C0), 8, std::uint8_t{'E'});
        std::fill_n(d.begin() + static_cast<std::ptrdiff_t>(o + 0x400), 16, std::uint8_t{0xEE});
    }
    return d;
}

/// SHIP4: a ship for each of the four players and one of the free trader, all on open sea in the
/// east, with the editor's garbage at +0x30.
std::vector<std::uint8_t> level_ships(std::uint16_t large_from = 5) {
    std::vector<std::uint8_t> d;
    for (std::uint8_t owner = 0; owner < 5; ++owner) {
        std::vector<std::uint8_t> r(kShipSize, 0);
        std::copy_n("Ship", 4, r.begin());
        put_u16(r, 0x1C, 470);
        put_u16(r, 0x1E, 40U + 40U * owner);
        put_u32(r, 0x30, 0xDEADBEEFU);
        put_u16(r, 0x48, owner >= large_from ? kLargeShipFigure : kShipFigure);
        r[0x4B] = owner;
        r[0x4D] = 0xFF;  // at sea
        d.insert(d.end(), r.begin(), r.end());
    }
    return d;
}

/// SZENE with six islands in the west and middle of the original map, none planned for natives.
std::vector<std::uint8_t> level_plan() {
    std::vector<std::uint8_t> d(0x494, 0);
    constexpr std::array<std::array<std::uint32_t, 4>, 6> kIslands = {{
        {0, 0, 40, 40},  // climate, size, x, y
        {0, 1, 140, 40},
        {1, 2, 240, 40},
        {1, 0, 40, 160},
        {0, 3, 140, 160},
        {2, 1, 240, 160},  // climate by position
    }};
    put_u32(d, 0x64, static_cast<std::uint32_t>(kIslands.size()));
    for (std::size_t i = 0; i < kIslands.size(); ++i) {
        const std::size_t o = 0x174 + 16 * i;
        d[o] = static_cast<std::uint8_t>(kIslands[i][0]);
        d[o + 1] = static_cast<std::uint8_t>(kIslands[i][1]);
        d[o + 3] = static_cast<std::uint8_t>(i);
        put_u32(d, o + 8, kIslands[i][2]);
        put_u32(d, o + 12, kIslands[i][3]);
    }
    return d;
}

/// A saved rand() table: 512 numbers of 0..32767.
std::vector<std::uint8_t> random_table() {
    std::vector<std::uint8_t> d;
    std::uint32_t state = 1602;
    for (int i = 0; i < 512; ++i) {
        state = state * 214013U + 2531011U;
        const std::uint32_t v = (state >> 16) & 0x7FFFU;
        d.push_back(static_cast<std::uint8_t>(v));
        d.push_back(static_cast<std::uint8_t>(v >> 8));
    }
    return d;
}

/// An endless level as the editor saved it (Endlosspiel<D><V>.szs); the ships of the slots from
/// `large_from` on are large trading cogs.
/// `mission`: the SZENE_MISSNR chunk the original's levels have (1 in the .szs, 0 in the .szm).
std::vector<std::uint8_t> endless_bytes(std::uint16_t large_from = 5, std::optional<std::uint32_t> mission = {}) {
    std::vector<std::uint8_t> timers(kEndlessTimersSize, 0);
    timers[0x10] = 0x77;             // not reset: kept
    timers[0xA6] = 1;                // the level's difficulty: kept
    put_u32(timers, 0xA0, 123'456);  // a game time left by the editor
    put_u32(timers, 0xB8, 999);      // the pirate interval the original does not read
    std::vector<std::uint8_t> bytes;
    test::put_chunk(bytes, "NAME", {'L', 'e', 'v', 'e', 'l'});
    test::put_chunk(bytes, "CUSTOM", {1, 2, 3, 4});
    if (mission) {
        std::vector<std::uint8_t> number;
        test::put_u32(number, *mission);
        test::put_chunk(bytes, "SZENE_MISSNR", number);
    }
    test::put_chunk(bytes, "SZENE", level_plan());
    test::put_chunk(bytes, "RANDTAB", random_table());
    test::put_chunk(bytes, "PLAYER4", level_players());
    test::put_chunk(bytes, "SHIP4", level_ships(large_from));
    test::put_chunk(bytes, "TIMERS", timers);
    return bytes;
}

assets::ChunkFile endless() {
    auto file = assets::ChunkFile::from_bytes(endless_bytes());
    REQUIRE(file.ok());
    return std::move(file).value();
}

std::string template_path(std::uint8_t climate, std::uint8_t size, std::uint16_t number, bool natives) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s/%s%02u.scp", (natives ? kNativeDir : kClimateDir)[climate], kSizeName[size],
                  static_cast<unsigned>(number));
    return buf;
}

/// An island template of open ground, as narrow as its size class allows (the loader takes the class of
/// an INSEL5 from its width): 16 x 16 tiles for the smallest class to 76 x 76 for the largest.
std::vector<std::uint8_t> template_file(std::uint8_t climate, std::uint8_t size, std::uint16_t number) {
    static constexpr std::uint32_t kSides[] = {16, 33, 43, 56, 76};
    const std::uint32_t side = kSides[size];
    std::vector<std::uint8_t> header(116, 0);
    header[1] = static_cast<std::uint8_t>(side);
    header[2] = static_cast<std::uint8_t>(side);
    put_u16(header, 0x60, number);
    put_u16(header, 0x62, size);
    header[0x64] = climate;
    header[0x65] = 1;  // stored complete
    std::vector<std::uint8_t> tiles;
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            tiles.insert(tiles.end(), {101, 0, static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)});
            test::put_u32(tiles, (7U << 14) | (7U << 22));  // city 7, owner 7: nobody's ground
        }
    }
    std::vector<std::uint8_t> file;
    test::put_chunk(file, "INSEL5", header);
    test::put_chunk(file, "INSELHAUS", tiles);
    return file;
}

/// An installation of its own in the temporary folder: what GameDirectory::open looks for, every
/// island template (a native version of every third), the endless level of difficulty 1,
/// layout 2, and that of difficulty 0, layout 0 for one player and for several as the original's
/// files have them: large cogs for slots 2 and 3 in the .szs, small ones for all in the .szm.
/// Without `level_name` the levels have the German game's names (sim::endless_file); with it they
/// are called as another language calls them, `level_name` and the two digits, and only their
/// SZENE_MISSNR tells them from other scenarios (as in Decoy12.szs, which is none).
struct FakeInstallation {
    std::filesystem::path root;
    assets::GameDirectory game;

    explicit FakeInstallation(std::string_view level_name = {}) {
        root = test::temp_path(level_name.empty() ? "o1602_new_game" : "o1602_new_game_named");
        std::filesystem::remove_all(root);
        for (const char* dir : {"toolgfx", "gfx", "Szenes", "Nord", "Sued", "NordNat", "SuedNat"}) {
            std::filesystem::create_directories(root / dir);
        }
        for (const char* file : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col", "gfx/stadtfld.bsh"}) {
            std::ofstream(root / file, std::ios::binary) << "x";
        }
        for (std::uint8_t climate = 0; climate < 2; ++climate) {
            for (std::uint8_t size = 0; size < 5; ++size) {
                for (std::uint16_t n = 0; n < kTemplateCount[climate][size]; ++n) {
                    const auto bytes = template_file(climate, size, n);
                    write(template_path(climate, size, n, false), bytes);
                    if (n % 3 == 0) {
                        write(template_path(climate, size, n, true), bytes);
                    }
                }
            }
        }
        if (level_name.empty()) {
            write(sim::endless_file(1, 2), endless_bytes());
            write(sim::endless_file(0, 0), endless_bytes(2));
            write(sim::endless_file(0, 0, true), endless_bytes());
        } else {
            const std::string level = "Szenes/" + std::string(level_name);
            write("Szenes/Decoy12.szs", endless_bytes());
            write(level + "12.szs", endless_bytes(5, 1));
            write(level + "00.szs", endless_bytes(2, 1));
            write(level + "00.szm", endless_bytes(5, 0));
        }
        auto opened = assets::GameDirectory::open(root);
        REQUIRE(opened.ok());
        game = std::move(opened).value();
    }
    ~FakeInstallation() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    FakeInstallation(const FakeInstallation&) = delete;
    FakeInstallation& operator=(const FakeInstallation&) = delete;

    void write(const std::string& relative, const std::vector<std::uint8_t>& bytes) const {
        std::ofstream out(root / relative, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
};

const FakeInstallation& installation() {
    static const FakeInstallation fake;
    return fake;
}

const assets::TextTable& texts() {
    static const assets::TextTable t =
        assets::TextTable::parse("[PLAYCOL]\nRoter Spieler\nBlauer Spieler\nGelber Spieler\nWeisser Spieler\n[END]\n");
    return t;
}

sim::NewGameOptions options(std::uint64_t seed) {
    sim::NewGameOptions o;
    o.difficulty = 1;
    o.layout = 2;
    o.colour = 2;
    o.name = "Anna";
    o.game_id = 0x2A83867AU;
    o.world.seed = seed;
    return o;
}

/// Islands, players and ships of two worlds are the same.
bool same_world(const sim::World& a, const sim::World& b) {
    if (a.width() != b.width() || a.height() != b.height() || a.islands().size() != b.islands().size() ||
        a.players().size() != b.players().size() || a.ships().size() != b.ships().size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.islands().size(); ++i) {
        const sim::WorldIsland& x = a.islands()[i];
        const sim::WorldIsland& y = b.islands()[i];
        if (x.header.raw != y.header.raw || x.x != y.x || x.y != y.y) {
            return false;
        }
    }
    for (std::size_t p = 0; p < a.players().size(); ++p) {
        const sim::Player& x = a.players()[p];
        const sim::Player& y = b.players()[p];
        if (x.role != y.role || x.banner != y.banner || x.money != y.money || x.name != y.name) {
            return false;
        }
    }
    for (std::size_t s = 0; s < a.ships().size(); ++s) {
        const sim::Ship& x = a.ships()[s];
        const sim::Ship& y = b.ships()[s];
        if (x.owner != y.owner || x.x != y.x || x.y != y.y) {
            return false;
        }
    }
    return true;
}

const sim::Ship* ship_of(const sim::World& world, sim::PlayerId owner) {
    const auto it =
        std::find_if(world.ships().begin(), world.ships().end(), [&](const sim::Ship& s) { return s.owner == owner; });
    return it == world.ships().end() ? nullptr : &*it;
}

void run(sim::Simulation& s, std::uint32_t ms) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
    }
}

}  // namespace

TEST_CASE("new game: the reset of an endless level sets players, clocks and ships as the original") {
    const auto level = endless();
    auto scenario = sim::new_game_scenario(level, texts(), options(5));
    REQUIRE(scenario.ok());

    // NAME first and the game's own, no CUSTOM; the other chunks in the level's order.
    REQUIRE(!scenario->chunks().empty());
    CHECK(scenario->chunks().front().name == "NAME");
    const auto name = scenario->payload(scenario->chunks().front());
    CHECK(std::string_view(reinterpret_cast<const char*>(name.data()), name.size()) == "Endlosspiel");
    CHECK(scenario->find("CUSTOM") == nullptr);
    REQUIRE(scenario->find("SZENE") != nullptr);
    CHECK(std::ranges::equal(scenario->payload(*scenario->find("SZENE")), level.payload(*level.find("SZENE"))));
    CHECK(std::ranges::equal(scenario->payload(*scenario->find("RANDTAB")), level.payload(*level.find("RANDTAB"))));

    // The human in slot 0 with the chosen banner, the computers with the other three in order and
    // named after them, then trader, pirates and natives. Money stays the level's.
    auto players = assets::load_players(*scenario);
    REQUIRE(players.ok());
    REQUIRE(players->size() == 7);
    struct Expected {
        assets::PlayerKind kind;
        assets::PlayerColour colour;
        const char* name;
    };
    const std::array<Expected, 7> expected = {{
        {assets::PlayerKind::Human, assets::PlayerColour::Yellow, "Anna"},
        {assets::PlayerKind::Computer, assets::PlayerColour::Red, "Roter Spieler"},
        {assets::PlayerKind::Computer, assets::PlayerColour::Blue, "Blauer Spieler"},
        {assets::PlayerKind::Computer, assets::PlayerColour::White, "Weisser Spieler"},
        {assets::PlayerKind::Trader, assets::PlayerColour::Trader, "Trader4"},
        {assets::PlayerKind::Pirate, assets::PlayerColour::Pirate, "Pirat5"},
        {assets::PlayerKind::Natives, assets::PlayerColour::Natives, "Native6"},
    }};
    for (std::size_t slot = 0; slot < expected.size(); ++slot) {
        const assets::PlayerRecord& p = (*players)[slot];
        CHECK(p.slot == slot);
        CHECK(p.kind == expected[slot].kind);
        CHECK(p.colour == expected[slot].colour);
        CHECK(p.name == expected[slot].name);
        CHECK(p.money == static_cast<std::int32_t>(30'000 + 1'000 * slot));
        CHECK(p.infra_mask == (slot < 5 ? 0U : 0xFFFFFFFFU));
    }
    const auto records = scenario->payload(*scenario->find("PLAYER4"));
    for (std::size_t slot = 0; slot < 7; ++slot) {
        const std::size_t o = slot * kPlayerSize;
        CHECK(records[o + 0x40] == 0x5A);
        CHECK(std::all_of(records.begin() + static_cast<std::ptrdiff_t>(o + 0x400),
                          records.begin() + static_cast<std::ptrdiff_t>(o + kPlayerSize),
                          [](std::uint8_t b) { return b == 0; }));
    }

    // TIMERS at the size of a savegame: the game time and the clocks of a new game, the rest kept.
    const auto timers = scenario->payload(*scenario->find("TIMERS"));
    REQUIRE(timers.size() == 0x298);
    CHECK(timers[0x10] == 0x77);
    CHECK(timers[0xA6] == 1);
    CHECK(u32(timers, 0xA0) == 0);
    CHECK(timers[0xA7] == 1);
    CHECK(u32(timers, 0xAC) == 0x2A83867AU);
    CHECK(u32(timers, 0xB4) == 54'000);
    CHECK(u32(timers, 0xB8) == 1'500);
    CHECK(u32(timers, 0xBC) == 1);
    CHECK(u32(timers, 0xC0) == 33);
    const std::uint32_t eruption = u32(timers, 0xCC);
    CHECK(eruption >= 42'000);  // 70 to 86 minutes
    CHECK(eruption <= 42'000 + 10 * (60 * 15 + 31));

    // The ships as the level has them, without the editor's garbage.
    const auto ships = scenario->payload(*scenario->find("SHIP4"));
    const auto before = level.payload(*level.find("SHIP4"));
    REQUIRE(ships.size() == before.size());
    for (std::size_t o = 0; o < ships.size(); ++o) {
        const bool garbage = o % kShipSize >= 0x30 && o % kShipSize < 0x34;
        CHECK(ships[o] == (garbage ? 0 : before[o]));
    }

    // The same seed draws the same eruption time, the clock is the only thing a seed changes here.
    auto again = sim::new_game_scenario(level, texts(), options(5));
    REQUIRE(again.ok());
    CHECK(std::ranges::equal(again->data(), scenario->data()));
}

TEST_CASE("new game: colours wrap, names without [PLAYCOL] stay empty (MD-10), a long name is cut to 63 bytes") {
    const auto level = endless();
    const assets::TextTable none = assets::TextTable::parse("");
    sim::NewGameOptions o = options(1);
    o.colour = 4;  // red: banners beyond the original's four wrap
    o.name = std::string(80, 'x');
    auto scenario = sim::new_game_scenario(level, none, o);
    REQUIRE(scenario.ok());
    auto players = assets::load_players(*scenario);
    REQUIRE(players.ok());
    REQUIRE(players->size() == 7);
    CHECK((*players)[0].colour == assets::PlayerColour::Red);
    CHECK((*players)[0].name == std::string(63, 'x'));
    CHECK((*players)[1].colour == assets::PlayerColour::Blue);
    CHECK((*players)[1].name.empty());  // the game shows it in its language (app::player_name)
    CHECK((*players)[2].name.empty());
    CHECK((*players)[3].name.empty());
}

TEST_CASE("new game: an own world's scenario puts the players' ships at their starts") {
    const auto level = endless();
    auto scenario = sim::new_game_scenario(level, texts(), options(1));
    REQUIRE(scenario.ok());
    sim::WorldPlan plan;
    plan.width = sim::World::kOriginalWidth;
    plan.height = sim::World::kOriginalHeight;
    for (std::uint32_t n = 0; n < 4; ++n) {
        plan.starts.push_back(sim::StartArea{100 + 70 * n, 300 - 10 * n, 0, 0});
    }

    // Three players: the fourth player's ship and the trader's are left out.
    auto own = sim::own_world_scenario(*scenario, plan, 3);
    REQUIRE(own.ok());
    CHECK(own->find(sim::kMapChunk) == nullptr);  // the original's size
    auto ships = assets::load_ships(*own);
    REQUIRE(ships.ok());
    REQUIRE(ships->size() == 3);
    for (std::size_t i = 0; i < ships->size(); ++i) {
        const auto& s = (*ships)[i];
        CHECK(s.owner == i);
        CHECK(s.x == plan.starts[i].x);
        CHECK(s.y == plan.starts[i].y);
        CHECK(s.figure == kShipFigure);
    }
    // Everything else as the new game's scenario has it.
    CHECK(std::ranges::equal(own->payload(*own->find("PLAYER4")), scenario->payload(*scenario->find("PLAYER4"))));
    CHECK(std::ranges::equal(own->payload(*own->find("TIMERS")), scenario->payload(*scenario->find("TIMERS"))));

    // A larger world names its size; one size chunk only, even from a scenario that had one.
    plan.width = 800;
    plan.height = 600;
    auto large = sim::own_world_scenario(*scenario, plan, 4);
    REQUIRE(large.ok());
    CHECK((sim::map_size(*large) == std::pair<std::uint32_t, std::uint32_t>{800, 600}));
    plan.width = 640;
    auto twice = sim::own_world_scenario(*large, plan, 4);
    REQUIRE(twice.ok());
    CHECK((sim::map_size(*twice) == std::pair<std::uint32_t, std::uint32_t>{640, 600}));
    CHECK(std::count_if(twice->chunks().begin(), twice->chunks().end(),
                        [](const assets::Chunk& c) { return c.name == sim::kMapChunk; }) == 1);
    auto four = assets::load_ships(*twice);
    REQUIRE(four.ok());
    CHECK(four->size() == 4);
}

TEST_CASE("new game: settings without a level are refused") {
    const auto& fake = installation();
    sim::NewGameOptions o = options(1);
    o.difficulty = sim::NewGameOptions::kDifficulties;
    auto difficulty = sim::start_new_game(fake.game, buildings(), texts(), o);
    REQUIRE(!difficulty.ok());
    CHECK(difficulty.error().code == core::ErrorCode::OutOfRange);
    o = options(1);
    o.layout = sim::NewGameOptions::kLayouts;
    CHECK(!sim::start_new_game(fake.game, buildings(), texts(), o).ok());
    o = options(1);
    o.layout = 0;  // Endlosspiel10.szs is not there
    auto missing = sim::start_new_game(fake.game, buildings(), texts(), o);
    REQUIRE(!missing.ok());
    CHECK(missing.error().code == core::ErrorCode::NotFound);
}

TEST_CASE("new game: a multiplayer endless game starts every player with the small trading cog") {
    CHECK(sim::endless_file(2, 1) == "Szenes/Endlosspiel21.szs");
    CHECK(sim::endless_file(2, 1, true) == "Szenes/Endlosspiel21.szm");
    const auto& fake = installation();
    sim::NewGameOptions o = options(3);
    o.difficulty = 0;
    o.layout = 0;
    auto single = sim::start_new_game(fake.game, buildings(), texts(), o);
    REQUIRE(single.ok());
    for (sim::PlayerId p = 0; p < 4; ++p) {
        const sim::Ship* ship = ship_of(*single, p);
        REQUIRE(ship != nullptr);
        CHECK(ship->figure == (p < 2 ? kShipFigure : kLargeShipFigure));
    }
    // Human players may sit in any of the four slots: the original's .szm gives each the small one.
    o.multiplayer = true;
    auto multi = sim::start_new_game(fake.game, buildings(), texts(), o);
    REQUIRE(multi.ok());
    for (sim::PlayerId p = 0; p < 4; ++p) {
        const sim::Ship* ship = ship_of(*multi, p);
        REQUIRE(ship != nullptr);
        CHECK(ship->figure == kShipFigure);
    }
    o.difficulty = 1;
    o.layout = 2;  // Endlosspiel12.szm is not there
    auto missing = sim::start_new_game(fake.game, buildings(), texts(), o);
    REQUIRE(!missing.ok());
    CHECK(missing.error().code == core::ErrorCode::NotFound);
}

TEST_CASE("new game: the levels of an installation in another language are found by their scenario") {
    // The English game does not call its levels Endlosspiel: Start in the dialog did nothing.
    static const FakeInstallation fake("Endless Game");
    const auto german = sim::find_endless_file(installation().game, 1, 2);
    REQUIRE(german.ok());
    CHECK(german->path.filename() == "Endlosspiel12.szs");
    CHECK(german->name == "Endlosspiel");
    const auto found = sim::find_endless_file(fake.game, 1, 2);
    REQUIRE(found.ok());
    CHECK(found->path.filename() == "Endless Game12.szs");
    CHECK(found->name == "Endless Game");
    const auto multi = sim::find_endless_file(fake.game, 0, 0, true);
    REQUIRE(multi.ok());
    CHECK(multi->path.filename() == "Endless Game00.szm");
    const auto missing = sim::find_endless_file(fake.game, 1, 2, true);
    REQUIRE(!missing.ok());
    CHECK(missing.error().code == core::ErrorCode::NotFound);

    // The game is that of the German installation, under the installation's name.
    auto world = sim::start_new_game(fake.game, buildings(), texts(), options(7));
    REQUIRE(world.ok());
    auto same = sim::start_new_game(installation().game, buildings(), texts(), options(7));
    REQUIRE(same.ok());
    CHECK(world->islands().size() == same->islands().size());
    CHECK(sim::game_identity(*world).name == "Endless Game");
    CHECK(sim::game_identity(*same).name == "Endlosspiel");
}

TEST_CASE("new game: an endless game: its islands from the plan, the same seed the same world") {
    const auto& fake = installation();
    std::vector<std::string> warnings;
    auto world = sim::start_new_game(fake.game, buildings(), texts(), options(7), &warnings);
    REQUIRE(world.ok());
    CHECK(warnings.empty());
    CHECK(sim::validate(*world).ok());
    CHECK(world->width() == sim::World::kOriginalWidth);
    CHECK(world->height() == sim::World::kOriginalHeight);
    REQUIRE(world->islands().size() == 6);
    for (std::size_t i = 0; i < world->islands().size(); ++i) {
        CHECK(world->islands()[i].header.number == i);
    }
    CHECK(world->islands()[1].x == 140);
    CHECK(world->islands()[1].y == 40);
    REQUIRE(world->players().size() == 7);
    CHECK(world->players()[0].role == sim::PlayerRole::Human);
    CHECK(world->players()[0].name == "Anna");
    for (sim::PlayerId p = 1; p < 4; ++p) {
        CHECK(world->players()[p].role == sim::PlayerRole::Computer);
        const sim::Ship* ship = ship_of(*world, p);
        REQUIRE(ship != nullptr);
        CHECK(ship->x == 470);  // where the level put it
        CHECK(ship->y == 40U + 40U * p);
    }
    CHECK(world->players()[4].faction == sim::faction::kTrader);
    CHECK(world->game_settings().difficulty == 1);
    CHECK(world->game_settings().pirate_interval_s == 1'500);  // not the level's

    auto again = sim::start_new_game(fake.game, buildings(), texts(), options(7));
    REQUIRE(again.ok());
    CHECK(same_world(*world, *again));
    // Another start of the table, another world of the same plan.
    auto other = sim::start_new_game(fake.game, buildings(), texts(), options(8));
    REQUIRE(other.ok());
    CHECK(other->islands().size() == world->islands().size());
    CHECK(!same_world(*world, *other));
}

TEST_CASE("new game: the preview of a world is the world the game gets, in small") {
    const auto& fake = installation();
    auto world = sim::start_new_game(fake.game, buildings(), texts(), options(7));
    REQUIRE(world.ok());
    auto preview = sim::preview_world(fake.game, buildings(), texts(), options(7));
    REQUIRE(preview.ok());
    CHECK(preview->width == world->width());
    CHECK(preview->height == world->height());
    REQUIRE(preview->tiles.size() == std::size_t{preview->width} * preview->height);
    CHECK(preview->islands == world->islands().size());
    CHECK(preview->land > 0);
    // Land only where an island is, the open sea elsewhere; every ship's place a start.
    std::uint64_t land = 0;
    for (std::uint32_t y = 0; y < preview->height; ++y) {
        for (std::uint32_t x = 0; x < preview->width; ++x) {
            const auto t = preview->at(x, y);
            const bool water = t == sim::WorldPreview::Tile::Sea || t == sim::WorldPreview::Tile::Surf;
            land += water ? 0U : 1U;
            if (world->cell(x, y).island == sim::World::kSea) {
                CHECK(t == sim::WorldPreview::Tile::Sea);
            }
        }
    }
    CHECK(land == preview->land);
    CHECK(!preview->starts.empty());
    for (const sim::Ship& ship : world->ships()) {
        CHECK(std::any_of(preview->starts.begin(), preview->starts.end(),
                          [&](const sim::WorldPreview::Start& st) { return st.x == ship.x && st.y == ship.y; }));
    }
    // The same seed, the same picture; another, another world.
    auto again = sim::preview_world(fake.game, buildings(), texts(), options(7));
    REQUIRE(again.ok());
    CHECK(again->tiles == preview->tiles);
    const sim::WorldPreview of = sim::preview_of(*world, buildings());
    CHECK(of.tiles == preview->tiles);
    // Settings the game refuses, the preview refuses too.
    sim::NewGameOptions bad = options(7);
    bad.difficulty = sim::NewGameOptions::kDifficulties;
    CHECK(!sim::preview_world(fake.game, buildings(), texts(), bad).ok());
}

TEST_CASE("new game: the seed picks the map, so the same seed is the same map") {
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        const std::uint8_t layout = sim::layout_for_seed(seed);
        CHECK(layout < sim::NewGameOptions::kLayouts);
        CHECK(sim::layout_for_seed(seed) == layout);
    }
    // Every map can come from some seed, and a large seed (the random default) is fine too.
    std::array<bool, sim::NewGameOptions::kLayouts> seen{};
    for (std::uint64_t seed = 0; seed < sim::NewGameOptions::kLayouts; ++seed) {
        seen[sim::layout_for_seed(seed)] = true;
    }
    CHECK(std::ranges::all_of(seen, [](bool s) { return s; }));
    CHECK(sim::layout_for_seed(0xFFFF'FFFF'FFFF'FFFFULL) < sim::NewGameOptions::kLayouts);
    // Fixed values: a seed written down in a test or a bug report keeps its map.
    CHECK(sim::layout_for_seed(1) == 1);
    CHECK(sim::layout_for_seed(3) == 0);
}

TEST_CASE("new game: an own world with six players: the plan's size and starts, computers alike on two runs") {
    const auto& fake = installation();
    sim::NewGameOptions o = options(11);
    sim::WorldSettings settings;
    settings.width = 600;
    settings.height = 420;
    settings.players = 6;
    o.custom = settings;
    std::vector<std::string> warnings;
    auto world = sim::start_new_game(fake.game, buildings(), texts(), o, &warnings);
    REQUIRE(world.ok());
    CHECK(sim::validate(*world).ok());
    CHECK(world->width() == 600);
    CHECK(world->height() == 420);

    // The plan start_new_game made, drawn again from the same settings.
    settings.difficulty = o.difficulty;
    settings.seed = o.world.seed;
    auto plan = sim::plan_world(settings, sim::island_templates(fake.game, buildings()));
    REQUIRE(plan.ok());
    REQUIRE(plan->starts.size() == 6);
    CHECK(world->islands().size() == plan->plan.islands.size());
    REQUIRE(world->players().size() == 9);
    for (std::size_t n = 0; n < 6; ++n) {
        const sim::PlayerId slot = sim::player_slot(n);
        CHECK(world->players()[slot].role == (n == 0 ? sim::PlayerRole::Human : sim::PlayerRole::Computer));
        const sim::Ship* ship = ship_of(*world, slot);
        REQUIRE(ship != nullptr);
        CHECK(ship->x == plan->starts[n].x);
        CHECK(ship->y == plan->starts[n].y);
    }
    CHECK(ship_of(*world, 4) == nullptr);  // the trader's ship of the level has no start

    // The computers run the same on two copies of the game, the extra players among them.
    auto copy = sim::start_new_game(fake.game, buildings(), texts(), o);
    REQUIRE(copy.ok());
    REQUIRE(same_world(*world, *copy));
    const assets::FigureTable figures = make_figures();
    sim::Simulation a(*world, buildings(), figures);
    sim::Simulation b(*copy, buildings(), figures);
    const auto view = a.computers().view();
    CHECK(std::any_of(view.begin(), view.end(), [](const auto& v) { return v.player == sim::player_slot(5); }));
    CHECK(std::none_of(view.begin(), view.end(), [](const auto& v) { return v.player == 0; }));
    for (int step = 0; step < 6; ++step) {
        run(a, 10'000);
        run(b, 10'000);
        const auto hash = a.state_hash();
        CHECK(hash == b.state_hash());
    }
    CHECK(!a.state_damaged());
    // And the same on every platform (golden hash, DS-2): a planned world, five computers.
    // The computers' state version 5 (the slots counted); the revised bread chain by default.
    test::check_golden("six players after 60 s", a.state_hash(), 0xf431ea0349d281aaULL);
}

TEST_CASE("new game: an own world gives a package's climate its share of the islands of its like") {
    const auto& fake = installation();
    sim::NewGameOptions o = options(5);
    sim::WorldSettings settings;
    settings.players = 4;
    o.custom = settings;
    auto plain = sim::start_new_game(fake.game, buildings(), texts(), o);
    REQUIRE(plain.ok());
    CHECK(plain->balance().climates.empty());

    sim::Climate tundra;
    tundra.id = "mod:tundra";
    tundra.like = sim::climate::kNorth;
    tundra.crops = 1U << (sim::ware::kCocoaField - sim::ware::kCornField);
    tundra.share = 100;
    o.world.climates = {tundra};
    auto world = sim::start_new_game(fake.game, buildings(), texts(), o);
    REQUIRE(world.ok());
    CHECK(sim::validate(*world).ok());
    REQUIRE(world->balance().climates.size() == 1);
    CHECK(world->balance().climates[0] == tundra);
    const std::uint32_t north = sim::original_climate_crops(sim::climate::kNorth);
    std::size_t taken = 0;
    for (const sim::WorldIsland& island : world->islands()) {
        if (island.climate == 0) {
            continue;
        }
        ++taken;
        CHECK(island.climate == 1);
        CHECK(island.header.climate == sim::climate::kNorth);  // the original's climate stays in the header
        CHECK((island.header.fertility & north) == 0);         // only its own crops are given
        CHECK(sim::island_fertility(*world, island, sim::ware::kCocoaField) >= 64);
        CHECK(sim::island_fertility(*world, island, sim::ware::kTobaccoField) == 0);
    }
    // Every northern island but the homes.
    std::size_t northern = 0;
    for (const sim::WorldIsland& island : plain->islands()) {
        northern += island.header.climate == sim::climate::kNorth ? 1U : 0U;
    }
    CHECK(taken > 0);
    CHECK(taken < northern + 1);
    // The same seed, the same world.
    auto again = sim::start_new_game(fake.game, buildings(), texts(), o);
    REQUIRE(again.ok());
    CHECK(same_world(*world, *again));
}

TEST_CASE("new game: a colour the level's file does not have is swapped with the player who had it") {
    sim::World world;
    std::vector<sim::Player> players(4);
    for (std::size_t s = 0; s < players.size(); ++s) {
        players[s].banner = static_cast<sim::BannerId>(s);
    }
    players[3].banner = sim::kFirstExtraBanner + 1;  // a computer flies green
    world.set_players(std::move(players));

    // Green: the computer gets the human's red.
    const auto swapped = sim::take_banner(world, 0, sim::kFirstExtraBanner + 1);
    REQUIRE(swapped.has_value());
    CHECK(*swapped == 3);
    CHECK(world.player(0)->banner == sim::kFirstExtraBanner + 1);
    CHECK(world.player(3)->banner == 0);

    // Orange: nobody has it.
    CHECK(!sim::take_banner(world, 0, sim::kFirstExtraBanner).has_value());
    CHECK(world.player(0)->banner == sim::kFirstExtraBanner);
    CHECK(world.player(3)->banner == 0);

    // Its own banner, or no player: nothing changes.
    CHECK(!sim::take_banner(world, 0, sim::kFirstExtraBanner).has_value());
    CHECK(!sim::take_banner(world, 40, 1).has_value());
    CHECK(world.player(1)->banner == 1);
}
