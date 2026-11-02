// SPDX-License-Identifier: GPL-3.0-or-later
// New endless games with the original data: the islands
// a world plan gets, the start state, and the savegame of a new game.

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "fixtures.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_extension.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world_generator.hpp"
#include "o1602/sim/world_plan.hpp"

using namespace o1602;

namespace {

assets::GameDirectory game_or_skip() {
    auto dir = assets::GameDirectory::from_environment();
    if (!dir) {
        SKIP("ANNO1602_DIR not set");
    }
    if (!dir->resolve("Szenes/Endlosspiel00.szs")) {
        SKIP("no endless game files");
    }
    return std::move(dir).value();
}

std::uint32_t u32(std::span<const std::uint8_t> d, std::size_t o) {
    return std::uint32_t{d[o]} | (std::uint32_t{d[o + 1]} << 8) | (std::uint32_t{d[o + 2]} << 16) |
           (std::uint32_t{d[o + 3]} << 24);
}

}  // namespace

TEST_CASE("new game: the game id of the original") {
    // game00 was started on 16 October 2026 at 14:06 (TIMERS +0xAC = 0x2A83867A).
    CHECK(sim::new_game_id(2026, 10, 16, 14, 6, 0x7A) == 0x2A83867AU);
    CHECK(sim::endless_file(2, 1) == "Szenes/Endlosspiel21.szs");
    CHECK(sim::endless_file(2, 1, true) == "Szenes/Endlosspiel21.szm");
}

TEST_CASE("data: every player of a multiplayer endless game starts with the small trading cog") {
    const auto game = game_or_skip();
    if (!game.resolve("Szenes/Endlosspiel00.szm")) {
        SKIP("no multiplayer endless game files");
    }
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    auto text = assets::TextTable::load(game);
    REQUIRE(text.ok());
    constexpr std::uint16_t kSmallCog = 21;  // HANDEL1; the single player files give slots 2 and 3 HANDEL2
    for (std::uint8_t d = 0; d < sim::NewGameOptions::kDifficulties; ++d) {
        for (std::uint8_t v = 0; v < sim::NewGameOptions::kLayouts; ++v) {
            sim::NewGameOptions options;
            options.difficulty = d;
            options.layout = v;
            options.multiplayer = true;
            options.world.seed = 5;
            auto world = sim::start_new_game(game, *buildings, *text, options);
            REQUIRE(world.ok());
            REQUIRE(world->ships().size() == 4);
            for (const sim::Ship& ship : world->ships()) {
                CHECK(ship.owner < 4);
                CHECK(ship.figure == kSmallCog);
            }
        }
    }
}

TEST_CASE("data: the world plan of an endless game gives the original's islands") {
    const auto game = game_or_skip();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    auto file = assets::ChunkFile::load(*game.resolve("Szenes/Endlosspiel00.szs"));
    REQUIRE(file.ok());
    REQUIRE(sim::plans_islands(*file));
    auto plan = assets::load_scenario_plan(*file);
    REQUIRE(plan.ok());
    REQUIRE(plan->has_value());
    CHECK((*plan)->islands.size() == 18);
    CHECK((*plan)->natives[0] == 2);
    CHECK((*plan)->ores[2].building == 22405);  // gold mine: 3 units

    // Start index 0 of the table: templates, villages, crops and veins as the original's algorithm
    // gives them.
    sim::WorldStart start;
    start.seed = 0;
    auto world = sim::generate_islands(*file, game, *buildings, start);
    REQUIRE(world.ok());
    struct Expect {
        std::uint8_t climate;
        std::uint8_t size;
        std::uint16_t template_number;
        bool natives;
        std::set<std::uint8_t> crops;
        std::vector<std::array<std::uint16_t, 4>> ores;  // ware, kind, x, y
    };
    const std::vector<Expect> expected = {
        {0, 0, 11, false, {48}, {}},
        {0, 0, 31, false, {46, 50}, {}},
        {1, 0, 44, false, {47, 49}, {}},
        {1, 0, 48, false, {49, 51}, {}},
        {1, 0, 31, false, {47, 51}, {}},
        {1, 1, 5, true, {49, 51}, {{2, 1, 7, 27}}},
        {1, 1, 41, true, {49, 51}, {}},
        {0, 1, 36, false, {50}, {{2, 0, 22, 29}, {2, 1, 30, 11}}},
        {0, 1, 35, false, {46, 50}, {}},
        {1, 2, 19, false, {47, 49}, {}},
        {0, 2, 11, false, {50}, {{2, 1, 11, 15}, {3, 1, 14, 13}}},
        {0, 2, 9, false, {46, 48}, {}},
        {0, 2, 20, true, {46, 48}, {}},
        {1, 3, 19, false, {47, 51}, {{2, 0, 50, 15}, {2, 1, 45, 7}}},
        {0, 3, 2, true, {46, 50}, {{2, 0, 22, 10}, {2, 1, 30, 23}}},
        {0, 3, 37, false, {46, 48}, {{2, 0, 32, 18}}},
        {1, 3, 17, false, {47, 49}, {{2, 1, 45, 27}}},
        {1, 3, 62, false, {47, 51}, {{2, 0, 22, 14}, {2, 1, 54, 16}}},
    };
    REQUIRE(world->islands.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const sim::GeneratedIsland& g = world->islands[i];
        const Expect& e = expected[i];
        CHECK(g.number == i);
        CHECK(g.climate == e.climate);
        CHECK(g.size == e.size);
        CHECK(g.template_number == e.template_number);
        CHECK(g.natives == e.natives);
        std::set<std::uint8_t> crops;
        for (std::uint8_t w = 46; w <= 51; ++w) {
            if ((g.fertility >> (w - 45)) & 1U) {
                crops.insert(w);
            }
        }
        CHECK(crops == e.crops);
        CHECK((g.fertility & 0x1181U) == 0x1181U);  // grain, grass, trees, fish everywhere
        std::vector<std::array<std::uint16_t, 4>> ores;
        for (const auto& d : g.deposits) {
            if (d.ware == 2 || d.ware == 3) {
                ores.push_back({d.ware, d.kind, d.x, d.y});
            }
        }
        CHECK(ores == e.ores);
    }
    // 2 of the 3 gold units find no free mountain (S-019); one big iron vein no island.
    CHECK(world->lost >= 4);

    // With Rules::guarantee_resources nothing that the plan asks for is missing.
    start.guarantee_resources = true;
    for (std::uint64_t seed = 0; seed < 512; seed += 37) {
        start.seed = seed;
        auto w = sim::generate_islands(*file, game, *buildings, start);
        REQUIRE(w.ok());
        bool gold = false;
        std::set<std::uint8_t> crops;
        for (const auto& g : w->islands) {
            for (const auto& d : g.deposits) {
                gold = gold || d.ware == 3;
            }
            for (std::uint8_t c = 46; c <= 51; ++c) {
                if ((g.fertility >> (c - 45)) & 1U) {
                    crops.insert(c);
                }
            }
        }
        CHECK(gold);
        CHECK(crops.size() == 6);
    }
}

TEST_CASE("data: islands drawn from a seed are the same for the same seed") {
    const auto game = game_or_skip();
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    auto file = assets::ChunkFile::load(*game.resolve("Szenes/Endlosspiel12.szs"));
    REQUIRE(file.ok());
    sim::WorldStart start;
    start.draws = sim::WorldStart::Draws::Seeded;
    start.seed = 0x1602'0000'1234ULL;
    auto a = sim::generate_islands(*file, game, *buildings, start);
    auto b = sim::generate_islands(*file, game, *buildings, start);
    start.seed += 1;
    auto c = sim::generate_islands(*file, game, *buildings, start);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    REQUIRE(a->file.data().size() == b->file.data().size());
    CHECK(std::equal(a->file.data().begin(), a->file.data().end(), b->file.data().begin()));
    bool differs = false;
    for (std::size_t i = 0; i < a->islands.size() && i < c->islands.size(); ++i) {
        differs = differs || a->islands[i].template_number != c->islands[i].template_number;
    }
    CHECK(differs);
}

TEST_CASE("data: a new endless game and its savegame") {
    const auto game = game_or_skip();
    auto buildings = assets::BuildingTable::load(game);
    auto text = assets::TextTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(text.ok());
    sim::NewGameOptions options;
    options.difficulty = 1;
    options.layout = 2;
    options.colour = 2;  // yellow
    options.name = "Merlin";
    options.game_id = 0x2B12345A;
    options.world.seed = 99;
    std::vector<std::string> warnings;
    auto world = sim::start_new_game(game, *buildings, *text, options, &warnings);
    REQUIRE(world.ok());
    CHECK(warnings.empty());
    CHECK(world->islands().size() == 17);  // layout 2
    CHECK(world->time_ms() == 0);
    CHECK(world->game_settings().difficulty == 1);
    CHECK(world->game_settings().flags == 9);  // easy levels: no pirates, ore never runs out
    CHECK(world->next_drought_ms() == 5'400'000);
    // The human in slot 0 with the chosen colour, the computers with the others in order.
    const auto& p = world->players();
    REQUIRE(p.size() == 7);
    CHECK(p[0].role == sim::PlayerRole::Human);
    CHECK(p[0].name == "Merlin");
    CHECK(p[0].money == 15'000);
    CHECK(p[1].role == sim::PlayerRole::Computer);
    CHECK(p[1].name == "Roter Spieler");
    CHECK(p[2].name == "Blauer Spieler");
    CHECK(p[3].money == 15'000);
    CHECK(p[4].money == 1'000'000);
    // Four start ships; the human's with 50 t tools, 30 t wood, 10 t food.
    REQUIRE(world->ships().size() == 4);
    CHECK(world->ships()[0].owner == 0);
    const auto ware = [&](std::uint32_t producer) { return buildings->by_id(producer)->ware; };
    CHECK(world->ships()[0].load(ware(20515)) == 50 * 32);  // tools
    CHECK(world->ships()[0].load(ware(22001)) == 30 * 32);  // wood
    CHECK(world->ships()[0].load(ware(21075)) == 10 * 32);  // food
    CHECK(world->ships()[1].load(ware(22001)) == 50 * 32);
    // Native villages belong to the natives.
    for (const sim::City& c : world->cities()) {
        CHECK(c.owner == 6);
    }

    // Saved, it has the chunks of the original's first save, and loads back to itself.
    auto chunks = sim::savegame_chunks(*world, *buildings);
    REQUIRE(chunks.ok());
    std::vector<std::string> names;
    for (const sim::SavedChunk& c : *chunks) {
        if (c.name != "INSEL5" && c.name != "INSELHAUS" && c.name != "HIRSCH2") {
            names.push_back(c.name);
        }
    }
    CHECK(names == (std::vector<std::string>{"NAME", "PRODLIST2", "WERFT", "MILITAR", "SIEDLER", "ROHWACHS2", "STADT4",
                                             "KONTOR2", "MARKT2", "PLAYER4", "AUFTRAG4", "HANDLER", "SHIP4", "SOLDAT3",
                                             "TURM", "TIMERS"}));
    for (const sim::SavedChunk& c : *chunks) {
        if (c.name == "TIMERS") {
            REQUIRE(c.data.size() == 0x298);
            CHECK(u32(c.data, 0x9C) == 12'800);
            CHECK(u32(c.data, 0xA0) == 0);
            CHECK(c.data[0xA7] == 1);
            CHECK(u32(c.data, 0xAC) == 0x2B12345AU);
            CHECK(u32(c.data, 0xB4) == 54'000);
            CHECK(u32(c.data, 0xB8) == 1'500);
            CHECK(u32(c.data, 0xBC) == 1);
            CHECK(u32(c.data, 0xCC) >= 42'000);
            CHECK(u32(c.data, 0xCC) < 52'000);
        }
        if (c.name == "PLAYER4") {
            CHECK(c.data[4] == 0x00);
            CHECK(c.data[7] == 2);
            CHECK(c.data[0x430 + 4] == 0x0C);
            CHECK(c.data[0x430 + 7] == 0);
            CHECK(c.data[4 * 0x430 + 4] == 0x0D);
        }
        if (c.name == "SHIP4") {
            CHECK(u32(c.data, 0x30) == 0);
        }
        if (c.name == "NAME") {
            CHECK(std::string(c.data.begin(), c.data.end()) == "Endlosspiel");
        }
    }
    auto bytes = sim::write_savegame(*world, *buildings);
    REQUIRE(bytes.ok());
    auto saved = assets::ChunkFile::from_bytes(*bytes);
    REQUIRE(saved.ok());
    auto again = sim::load_world(*saved, *buildings, game);
    REQUIRE(again.ok());
    auto rewritten = sim::savegame_chunks(*again, *buildings);
    REQUIRE(rewritten.ok());
    REQUIRE(rewritten->size() == chunks->size());
    for (std::size_t i = 0; i < chunks->size(); ++i) {
        CHECK((*rewritten)[i].name == (*chunks)[i].name);
        CHECK((*rewritten)[i].data == (*chunks)[i].data);
    }
}

TEST_CASE("data: endless games the original started come out of the same islands and records") {
    const auto game = game_or_skip();
    auto buildings = assets::BuildingTable::load(game);
    auto text = assets::TextTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(text.ok());
    // Saved by the original about 8 s after starting "Durchschnittlich", Merlin, yellow: the
    // layout and the start in the island table are the only ones that give these templates.
    struct Start {
        const char* file;
        std::uint8_t layout;
        std::uint32_t start;
    };
    std::size_t compared = 0;
    for (const Start& s : {Start{"SAVEGAME/game06.gam", 2, 399}, Start{"SAVEGAME/game07.gam", 0, 22}}) {
        const auto path = test::fixture(game, s.file);
        if (!path) {
            continue;
        }
        auto original = assets::ChunkFile::load(*path);
        REQUIRE(original.ok());
        const assets::Chunk* name = original->find("NAME");
        if (name == nullptr || original->payload(*name).size() != 11) {
            continue;  // another savegame in this place
        }
        sim::NewGameOptions options;
        options.difficulty = 1;
        options.layout = s.layout;
        options.colour = 2;
        options.name = "Merlin";
        options.game_id = u32(original->payload(*original->find("TIMERS")), 0xAC);
        options.world.seed = s.start;
        options.world.guarantee_resources = false;
        auto world = sim::start_new_game(game, *buildings, *text, options);
        REQUIRE(world.ok());
        auto bytes = sim::write_savegame(*world, *buildings, nullptr);
        REQUIRE(bytes.ok());
        auto mine = assets::ChunkFile::from_bytes(*bytes);
        REQUIRE(mine.ok());
        // Chunk by chunk in the same order; what the 8 s of play changed is left out: the deer
        // census of the first island epoch, harvests of the natives, the computer players'
        // ships, the first free trader and the clocks.
        const auto all = [](const assets::ChunkFile& f, const char* n) {
            std::vector<std::vector<std::uint8_t>> out;
            for (const assets::Chunk& c : f.chunks()) {
                if (c.name == n) {
                    out.emplace_back(f.payload(c).begin(), f.payload(c).end());
                }
            }
            return out;
        };
        for (const char* n : {"NAME", "PLAYER4", "AUFTRAG4", "STADT4", "KONTOR2", "MARKT2"}) {
            CHECK(all(*original, n) == all(*mine, n));
        }
        const auto a = all(*original, "INSEL5");
        const auto b = all(*mine, "INSEL5");
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            auto x = a[i];
            auto y = b[i];
            for (std::size_t o = 8; o < 12; ++o) {
                x[o] = y[o] = 0;  // deer census and island epoch phase
            }
            for (std::size_t o = 0x68; o < 0x6C; ++o) {
                x[o] = y[o] = 0;  // islands the computer players' ships discovered meanwhile
            }
            for (std::size_t d = 0; d < 2; ++d) {
                const std::size_t o = 0x1C + 8 * d;
                if (x[o] == 58 && y[o] == 58) {
                    x[o + 1] = y[o + 1] = 0;  // a treasure: the original stores the wrong column (S-067)
                    x[o + 2] = y[o + 2] = 0;
                }
            }
            CHECK(x == y);
        }
        ++compared;
    }
    if (compared == 0) {
        SKIP("no savegames of fresh endless games");
    }
}

namespace {

/// What start area `start` has within reach in a world made from `plan`: the crops (fertility bits),
/// whether the home island has iron, whether gold lies near.
struct Reach {
    std::uint32_t fertility = 0;
    bool home_iron = false;
    bool gold = false;
};

Reach reach_of(const sim::World& world, const sim::StartArea& start) {
    Reach r;
    for (const sim::WorldIsland& island : world.islands()) {
        const std::int64_t dx = std::int64_t{island.x} + island.map.width() / 2 - start.x;
        const std::int64_t dy = std::int64_t{island.y} + island.map.height() / 2 - start.y;
        const bool home = island.header.number == start.home_island;
        if (dx * dx + dy * dy > std::int64_t{start.reach} * start.reach && !home) {
            continue;
        }
        r.fertility |= island.header.fertility;
        for (const auto& d : island.header.deposits) {
            r.gold = r.gold || d.ware == 3;
            r.home_iron = r.home_iron || (home && d.ware == 2);
        }
    }
    return r;
}

}  // namespace

TEST_CASE("data: an own world: ships at their starts, every start area alike, saved and loaded again") {
    const auto game = game_or_skip();
    auto buildings = assets::BuildingTable::load(game);
    auto text = assets::TextTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(text.ok());
    const std::vector<sim::TemplateInfo> templates = sim::island_templates(game, *buildings);
    CHECK(templates.size() > 400);
    for (const std::uint64_t seed : {11ULL, 12ULL, 13ULL}) {
        sim::NewGameOptions options;
        options.difficulty = 1;
        options.world.seed = seed;
        options.custom = sim::WorldSettings{};
        std::vector<std::string> warnings;
        auto world = sim::start_new_game(game, *buildings, *text, options, &warnings);
        REQUIRE(world.ok());
        for (const auto& w : warnings) {
            std::fprintf(stderr, "  warning: %s\n", w.c_str());
        }
        CHECK(warnings.empty());
        CHECK(world->width() == 500);
        CHECK(world->height() == 350);
        // The plan the game was made from (same settings, same seed).
        sim::WorldSettings settings;
        settings.difficulty = 1;
        settings.seed = seed;
        auto plan = sim::plan_world(settings, templates);
        REQUIRE(plan.ok());
        CHECK(world->islands().size() == plan->plan.islands.size());
        REQUIRE(world->ships().size() == 4);
        for (const sim::Ship& ship : world->ships()) {
            REQUIRE(ship.owner < 4);
            CHECK(ship.x == plan->starts[ship.owner].x);
            CHECK(ship.y == plan->starts[ship.owner].y);
        }
        // Every start: the six crops near, iron on the home island, gold near; no village there.
        constexpr std::uint32_t kCrops = 0x3FU << 1;  // tobacco (46) .. cocoa (51), bit = ware - 45
        for (const sim::StartArea& start : plan->starts) {
            const Reach r = reach_of(*world, start);
            CHECK((r.fertility & kCrops) == kCrops);
            CHECK(r.home_iron);
            CHECK(r.gold);
            for (const sim::City& c : world->cities()) {
                CHECK(world->islands()[c.island].header.number != start.home_island);
            }
        }
        // Saved and loaded: the same world, and saved again the same file.
        auto bytes = sim::write_savegame(*world, *buildings);
        REQUIRE(bytes.ok());
        auto file = assets::ChunkFile::from_bytes(*bytes);
        REQUIRE(file.ok());
        CHECK(file->find(sim::kMapChunk) == nullptr);  // the original's size: the original loads it
        auto again = sim::load_world(*file, *buildings, game);
        REQUIRE(again.ok());
        auto rewritten = sim::write_savegame(*again, *buildings);
        REQUIRE(rewritten.ok());
        CHECK(*rewritten == *bytes);
    }
}

TEST_CASE("data: an own world larger than the original's, with two players, plays and is saved") {
    const auto game = game_or_skip();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    auto text = assets::TextTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    REQUIRE(text.ok());
    sim::NewGameOptions options;
    options.world.seed = 21;
    sim::WorldSettings settings;
    settings.width = 900;
    settings.height = 600;
    settings.players = 2;
    options.custom = settings;
    auto world = sim::start_new_game(game, *buildings, *text, options);
    REQUIRE(world.ok());
    CHECK(world->width() == 900);
    CHECK(world->height() == 600);
    CHECK(world->islands().size() > 50);
    CHECK(world->ships().size() == 2);
    // Ten minutes of play: the traders and pirates find their way on the larger sea.
    {
        sim::Simulation s(*world, *buildings, *figures);
        s.set_speed(8);
        for (int i = 0; i < 750; ++i) {
            s.advance(100);
        }
    }
    auto bytes = sim::write_savegame(*world, *buildings);
    REQUIRE(bytes.ok());
    auto file = assets::ChunkFile::from_bytes(*bytes);
    REQUIRE(file.ok());
    CHECK(file->find(sim::kMapChunk) != nullptr);
    auto again = sim::load_world(*file, *buildings, game);
    REQUIRE(again.ok());
    CHECK(again->width() == 900);
    CHECK(again->height() == 600);
    CHECK(again->islands().size() == world->islands().size());
    CHECK(again->ships().size() == world->ships().size());
    auto rewritten = sim::write_savegame(*again, *buildings);
    REQUIRE(rewritten.ok());
    CHECK(*rewritten == *bytes);
}

TEST_CASE("data: an own world with six players, all ships at one place, nobody out after ten minutes") {
    const auto game = game_or_skip();
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    auto text = assets::TextTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    REQUIRE(text.ok());
    sim::NewGameOptions options;
    options.difficulty = 1;
    options.world.seed = 1602;
    sim::WorldSettings settings;
    settings.width = 612;  // as the multiplayer menu sizes it for six
    settings.height = 429;
    settings.players = 6;
    settings.starts = sim::WorldSettings::Starts::Together;
    options.custom = settings;
    std::vector<std::string> warnings;
    auto world = sim::start_new_game(game, *buildings, *text, options, &warnings);
    REQUIRE(world.ok());
    for (const auto& w : warnings) {
        std::fprintf(stderr, "  warning: %s\n", w.c_str());
    }
    REQUIRE(world->players().size() == 9);
    // A ship for each of the six players, in slots 0-3 and 7-8, all at the same place; the factions
    // keep theirs where the level has them.
    std::set<sim::PlayerId> owners;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> places;
    for (const sim::Ship& ship : world->ships()) {
        if (ship.owner < 4 || ship.owner >= 7) {
            owners.insert(ship.owner);
            places.emplace_back(ship.x, ship.y);
        }
    }
    CHECK((owners == std::set<sim::PlayerId>{0, 1, 2, 3, 7, 8}));
    // Together, as the original: a few tiles apart around one place.
    for (const auto& [x, y] : places) {
        CHECK(std::max(x, places[0].first) - std::min(x, places[0].first) <= 20);
        CHECK(std::max(y, places[0].second) - std::min(y, places[0].second) <= 20);
    }
    CHECK(world->players()[7].role == sim::PlayerRole::Computer);
    CHECK(world->players()[7].banner == sim::kFirstExtraBanner);
    CHECK(world->players()[8].banner == sim::kFirstExtraBanner + 1);

    // As the multiplayer host makes it: the human in slot 0, the computer plays the rest.
    world->player(0)->role = sim::PlayerRole::Human;
    {
        sim::Simulation s(*world, *buildings, *figures);
        s.set_speed(8);
        for (int i = 0; i < 750; ++i) {
            s.advance(100);
        }
        for (const sim::PlayerId p : owners) {
            if (!world->players()[p].active()) {
                std::fprintf(stderr, "  player %d is out of the game\n", int{p});
            }
            CHECK(world->players()[p].active());
        }
        // Saved and loaded in Open1602's format: all nine slots come back.
        auto bytes = sim::write_game(*world, *buildings, &s);
        REQUIRE(bytes.ok());
        auto again = sim::read_game(*bytes, *buildings, nullptr, &*figures);
        REQUIRE(again.ok());
        CHECK(again->players().size() == 9);
        CHECK(again->players()[8].banner == sim::kFirstExtraBanner + 1);
    }
}
