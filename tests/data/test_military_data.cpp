// SPDX-License-Identifier: GPL-3.0-or-later
// Military and diplomacy of the original savegames:
// what SOLDAT3, TURM, MILITAR and the diplomacy part of PLAYER4 hold, and that they come back.

#include <cstdio>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "fixtures.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/sim/savegame_extension.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

assets::GameDirectory game_or_skip(const char* file) {
    auto dir = assets::GameDirectory::from_environment();
    if (!dir) {
        SKIP("ANNO1602_DIR not set");
    }
    if (!dir->resolve(file)) {
        SKIP("savegame missing");
    }
    return std::move(dir).value();
}

}  // namespace

TEST_CASE("data: soldiers, towers, castles and diplomacy of a savegame, written back unchanged") {
    const auto game = game_or_skip("SAVEGAME/game00.gam");
    auto buildings = assets::BuildingTable::load(game);
    auto figures = assets::FigureTable::load(game);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    auto file = assets::ChunkFile::load(test::fixture_or_skip(game, "SAVEGAME/game00.gam"));
    REQUIRE(file.ok());
    auto world = sim::load_world(*file, *buildings, game);
    REQUIRE(world.ok());

    // Figure data of the unit types (figuren.cod): swordsman, musketeer, gunner, spearman, tower.
    const assets::FigureType* sword = figures->get(1);
    const assets::FigureType* musket = figures->get(9);
    const assets::FigureType* gunner = figures->get(13);
    const assets::FigureType* spear = figures->get(33);
    const assets::FigureType* tower = figures->get(38);
    REQUIRE(sword != nullptr);
    REQUIRE(musket != nullptr);
    REQUIRE(gunner != nullptr);
    REQUIRE(spear != nullptr);
    REQUIRE(tower != nullptr);
    CHECK(sword->combat.max_health == 20 * 32);
    CHECK(sword->combat.hit == 32);
    CHECK(sword->combat.range8 == 6);
    CHECK(sword->work_time_ms == 800);
    CHECK(sword->combat.hit_delay_ms == 600);
    CHECK(musket->combat.range8 == 32);
    CHECK(musket->combat.max_step == 2);
    CHECK(gunner->combat.hit == 224);
    CHECK(gunner->combat.weapon_amount == 2 * 32);
    CHECK(spear->combat.max_health == 18 * 32);
    CHECK(tower->combat.range8 == 60);
    CHECK(tower->combat.hit == 128);
    CHECK(figures->unit_cost(1) == 80);
    CHECK(figures->unit_cost(4) == 220);
    CHECK(!figures->formations().empty());

    // Nine native warriors, the 61 pirate towers.
    CHECK(world->soldiers().size() == 9);
    for (const sim::Soldier& s : world->soldiers()) {
        CHECK(s.figure == 33);
        CHECK(s.health == 18 * 32);
        CHECK(s.home.kind == sim::Target::Kind::Spot);
        CHECK(world->players()[s.owner].faction == sim::faction::kNatives);
    }
    CHECK(world->towers().size() == 61);
    for (const sim::Tower& t : world->towers()) {
        CHECK(t.figure == 40);
        CHECK(world->players()[t.owner].faction == sim::faction::kPirates);
    }
    // The natives' military huts hold eight trained spearmen each.
    int huts = 0;
    for (const sim::CostBuilding& b : world->cost_buildings()) {
        if (b.kind == sim::CostBuilding::Kind::Castle && !b.slots.empty()) {
            ++huts;
            CHECK(b.slots.size() == 8);
            for (const sim::CastleSlot& slot : b.slots) {
                CHECK(slot.figure == 33);
                CHECK(slot.armed);
                CHECK(!slot.training);
                CHECK(slot.progress == 320);
            }
        }
    }
    CHECK(huts == 3);

    // Diplomacy (Merlin01 at t = 20978): the pirates made peace after a surrender. An older save
    // in the same place shows the time before.
    const sim::Diplomacy& d = world->diplomacy();
    CHECK(d.hostile(5, 6));
    if (world->time_ms() / 100 == 20'978) {
        CHECK(!d.hostile(0, 5));
        CHECK(d.relation(0, 5).damage == 927);
        CHECK(d.relation(0, 0).war_time_ms == 12'945 * 100);
        int surrenders = 0;
        for (const sim::DiplomacyEvent& e : d.events(5)) {
            surrenders += e.type == sim::DiplomacyEventType::AnySurrender ? 1 : 0;
        }
        CHECK(surrenders == 4);
        CHECK(d.mood(*world, 5, 3, 0, world->time_ms()) > d.mood(*world, 5, 1, 0, world->time_ms()));
    }

    // Written back without change.
    auto chunks = sim::savegame_chunks(*world, *buildings);
    REQUIRE(chunks.ok());
    for (const sim::SavedChunk& c : *chunks) {
        if (c.name != "SOLDAT3" && c.name != "TURM" && c.name != "MILITAR" && c.name != "PLAYER4") {
            continue;
        }
        const assets::Chunk* original = file->find(c.name);
        REQUIRE(original != nullptr);
        const auto bytes = file->payload(*original);
        const bool same = std::vector<std::uint8_t>(bytes.begin(), bytes.end()) == c.data;
        if (!same) {
            std::fprintf(stderr, "  %s differs\n", c.name.c_str());
        }
        CHECK(same);
    }
}

TEST_CASE("data: military chunks of every savegame come back byte for byte") {
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        SKIP("ANNO1602_DIR not set");
    }
    auto buildings = assets::BuildingTable::load(*game);
    REQUIRE(buildings.ok());
    int checked = 0;
    for (int n = 0; n < 12; ++n) {
        char name[32];
        std::snprintf(name, sizeof name, "SAVEGAME/game%02d.gam", n);
        const auto path = game->resolve(name);
        if (!path) {
            continue;
        }
        auto file = assets::ChunkFile::load(*path);
        REQUIRE(file.ok());
        auto world = sim::load_world(*file, *buildings, *game);
        REQUIRE(world.ok());
        auto chunks = sim::savegame_chunks(*world, *buildings);
        REQUIRE(chunks.ok());
        for (const sim::SavedChunk& c : *chunks) {
            if (c.name != "SOLDAT3" && c.name != "TURM" && c.name != "MILITAR" && c.name != "PLAYER4") {
                continue;
            }
            const assets::Chunk* original = file->find(c.name);
            if (original == nullptr) {
                continue;
            }
            const auto bytes = file->payload(*original);
            const bool same = std::vector<std::uint8_t>(bytes.begin(), bytes.end()) == c.data;
            if (!same) {
                std::fprintf(stderr, "  %s: %s differs\n", name, c.name.c_str());
            }
            CHECK(same);
        }
        ++checked;
    }
    if (checked == 0) {
        SKIP("no savegames");
    }
}
