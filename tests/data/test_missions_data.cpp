// SPDX-License-Identifier: GPL-3.0-or-later
// Missions of the shipped scenarios.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

struct Data {
    assets::GameDirectory game;
    assets::BuildingTable buildings;
    assets::FigureTable figures;
};

Data data_or_skip(const char* file) {
    auto dir = assets::GameDirectory::from_environment();
    if (!dir) {
        SKIP("ANNO1602_DIR not set");
    }
    if (!dir->resolve(file)) {
        SKIP("scenario not installed");
    }
    auto buildings = assets::BuildingTable::load(*dir);
    REQUIRE(buildings.ok());
    auto figures = assets::FigureTable::load(*dir);
    REQUIRE(figures.ok());
    return Data{std::move(dir).value(), std::move(buildings).value(), std::move(figures).value()};
}

sim::World load(const Data& d, const char* file, std::vector<std::string>* warnings = nullptr) {
    auto chunks = assets::ChunkFile::load(*d.game.resolve(file));
    REQUIRE(chunks.ok());
    auto world = sim::load_world(*chunks, d.buildings, d.game, warnings);
    REQUIRE(world.ok());
    return std::move(world).value();
}

}  // namespace

TEST_CASE("data: Das Atoll: 1000 aristocrats in one city, only the human has the mission") {
    const Data d = data_or_skip("Szenes/Das Atoll.szs");
    std::vector<std::string> warnings;
    sim::World world = load(d, "Szenes/Das Atoll.szs", &warnings);
    CHECK(warnings.empty());
    REQUIRE(world.missions().size() == 1);
    const sim::Mission& m = world.missions()[0];
    CHECK(m.goals == sim::goal::kCity1);
    CHECK(m.cities[0].inhabitants == 1000);
    CHECK(m.cities[0].resident_class == 4);
    CHECK(m.cities[0].count == 1000);
    CHECK(m.success_message == 0x63);
    CHECK(d.game.expand(m.text).rfind("Nach einer langen", 0) == 0);  // History Edition: "[[8]]"
    CHECK(world.players()[0].mission == 0);
    for (std::size_t p = 1; p < world.players().size(); ++p) {
        CHECK(world.players()[p].mission == sim::kNoMission);
    }
    // Nobody wins or loses at the start; every slot is played.
    for (const sim::Player& p : world.players()) {
        CHECK(p.status == sim::PlayerStatus::Playing);
    }
    sim::Simulation s(world, d.buildings, d.figures);
    for (int i = 0; i < 400; ++i) {  // 20 s
        s.tick();
    }
    CHECK(s.take_game_events().empty());
    CHECK(!sim::check_mission(world, d.buildings, d.figures, 0).won);
}

TEST_CASE("data: Kalt erwischt: every gold island alone and two cities of 500 aristocrats") {
    const Data d = data_or_skip("Szenes/Kalt erwischt.szs");
    const sim::World world = load(d, "Szenes/Kalt erwischt.szs");
    REQUIRE(!world.missions().empty());
    const sim::Mission& m = world.missions()[0];
    CHECK(m.goals == (sim::goal::kIslands1 | sim::goal::kCity1 | sim::goal::kCity2));
    CHECK(m.islands[0].resource == 3);
    REQUIRE(!m.islands[0].islands.empty());
    std::size_t gold = 0;
    for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
        const auto& deps = world.islands()[i].header.deposits;
        const bool has =
            std::any_of(deps.begin(), deps.end(), [](const auto& x) { return x.ware == 3 && x.amount >= 32; });
        gold += has ? 1 : 0;
        CHECK(has ==
              (std::find(m.islands[0].islands.begin(), m.islands[0].islands.end(), i) != m.islands[0].islands.end()));
    }
    CHECK(gold == m.islands[0].islands.size());
}

TEST_CASE("data: Die Prinzenprüfung: the city goals sorted, castle and cathedral") {
    const Data d = data_or_skip("Szenes/Die Prinzenprüfung.szs");
    const sim::World world = load(d, "Szenes/Die Prinzenprüfung.szs");
    REQUIRE(!world.missions().empty());
    const sim::Mission& m = world.missions()[0];
    CHECK(m.goals == 0x60003U);
    CHECK(m.cities[0].inhabitants == 2500);
    CHECK(m.cities[1].inhabitants == 1500);
    CHECK(m.cities[2].inhabitants == 0);
}

TEST_CASE("data: Am Ende allein: each human must defeat the other two") {
    const Data d = data_or_skip("Szenes/Am Ende allein.szm");
    const sim::World world = load(d, "Szenes/Am Ende allein.szm");
    REQUIRE(world.missions().size() == 3);
    CHECK((world.missions()[0].defeat == std::vector<sim::PlayerId>{1, 2}));
    CHECK((world.missions()[1].defeat == std::vector<sim::PlayerId>{0, 2}));
    CHECK((world.missions()[2].defeat == std::vector<sim::PlayerId>{0, 1}));
    for (const sim::Mission& m : world.missions()) {
        CHECK(m.goals == (sim::goal::kCity1 | sim::goal::kDefeat));
    }
    // No pirates to defeat: settings bit 5 stays as the file has it.
    CHECK((world.game_settings().flags & 0x20U) == 0);
}

TEST_CASE("data: the missions of every scenario are written back as the original writes them") {
    const Data d = data_or_skip("Szenes/Das Atoll.szs");
    for (const char* file :
         {"Szenes/Das Atoll.szs", "Szenes/Das Atoll.szm", "Szenes/Am Ende allein.szm", "Szenes/Der Magnat2.szs",
          "Szenes/Kalt erwischt.szs", "Szenes/Endlosspiel00.szs", "SAVEGAME/game00.gam", "SAVEGAME/game01.gam"}) {
        if (!d.game.resolve(file)) {
            continue;
        }
        auto chunks = assets::ChunkFile::load(*d.game.resolve(file));
        REQUIRE(chunks.ok());
        const sim::World world = load(d, file);
        auto saved = sim::savegame_chunks(world, d.buildings);
        REQUIRE(saved.ok());
        const auto it =
            std::find_if(saved->begin(), saved->end(), [](const sim::SavedChunk& c) { return c.name == "AUFTRAG4"; });
        REQUIRE(it != saved->end());
        const assets::Chunk* original = chunks->find("AUFTRAG4");
        REQUIRE(original != nullptr);
        const auto bytes = chunks->payload(*original);
        const bool same = std::equal(bytes.begin(), bytes.end(), it->data.begin(), it->data.end());
        if (!same) {
            std::printf("  %s: AUFTRAG4 %zu / %zu bytes\n", file, bytes.size(), it->data.size());
        }
        CHECK(same);
    }
}
