// SPDX-License-Identifier: GPL-3.0-or-later
// Ships of the original savegames: what SHIP4 and HANDLER hold, and twenty
// minutes at sea with the free traders, the computer players' routes and the pirates' orders.

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "fixtures.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/simulation.hpp"

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

TEST_CASE("data: ships of a savegame with their routes, the trader's goods and the island it lies at") {
    const auto game = game_or_skip("SAVEGAME/game00.gam");
    auto buildings = assets::BuildingTable::load(game);
    REQUIRE(buildings.ok());
    auto file = assets::ChunkFile::load(test::fixture_or_skip(game, "SAVEGAME/game00.gam"));
    REQUIRE(file.ok());
    auto world = sim::load_world(*file, *buildings, game);
    REQUIRE(world.ok());
    std::size_t traders = 0;
    std::size_t routes = 0;
    std::set<std::uint32_t> ids;
    for (const sim::Ship& s : world->ships()) {
        ids.insert(s.id);
        CHECK(s.health != 0);
        if (s.trader) {
            ++traders;
            CHECK(s.figure == 29);
            std::uint32_t goods = 0;
            for (const sim::TraderGood& g : s.trader_goods) {
                goods += g.amount;
                CHECK(g.price <= 1023);
            }
            CHECK(goods != 0);
        }
        if (s.route.has_stops()) {
            ++routes;
        }
    }
    CHECK(ids.size() == world->ships().size());  // every ship its own id
    CHECK(traders == 7);                         // (20 Kontors + 2) / 3
    CHECK(routes >= 2);
    // Kontor trade settings of the computer players: food sold at half the base price.
    bool food_offer = false;
    for (const sim::City& c : world->cities()) {
        const sim::City::Trade* t = c.trade_of(14);
        if (t != nullptr && t->sell && t->sell_price == 13) {
            food_offer = true;
        }
    }
    CHECK(food_offer);
}

TEST_CASE("data: twenty minutes at sea: ships stay on water, traders trade, nobody gives up") {
    const auto game = game_or_skip("SAVEGAME/game00.gam");
    for (const char* name : {"SAVEGAME/game00.gam", "SAVEGAME/game03.gam"}) {
        const auto path = test::fixture(game, name);
        if (!path) {
            continue;
        }
        auto buildings = assets::BuildingTable::load(game);
        auto figures = assets::FigureTable::load(game);
        REQUIRE(buildings.ok());
        REQUIRE(figures.ok());
        auto file = assets::ChunkFile::load(*path);
        REQUIRE(file.ok());
        auto world = sim::load_world(*file, *buildings, game);
        REQUIRE(world.ok());
        sim::Simulation sim(*world, *buildings, *figures);
        sim.set_speed(8);
        std::size_t traded = 0;
        std::size_t gave_up = 0;
        std::size_t on_land = 0;
        for (int i = 0; i < 20 * 600 / 8; ++i) {
            sim.advance(100);
            for (const sim::ShipEvent& e : sim.take_ship_events()) {
                traded += e.kind == sim::ShipEvent::Kind::Traded ? 1U : 0U;
                // Pirates sail to their hideout, whose narrow harbour another pirate may block;
                // their own behaviour comes with the military.
                const sim::Player* owner = world->player(e.player);
                const bool pirate = owner != nullptr && owner->faction == sim::faction::kPirates;
                gave_up += e.kind == sim::ShipEvent::Kind::GaveUp && !pirate ? 1U : 0U;
            }
            for (const sim::Ship& s : world->ships()) {
                on_land += sim.fleet().sea().sailable(s.x, s.y) ? 0U : 1U;
            }
        }
        std::fprintf(stderr, "  %s: %zu trades\n", name, traded);
        CHECK(traded > 20);
        CHECK(gave_up == 0);
        CHECK(on_land == 0);
    }
}

TEST_CASE("data: explored islands and known deposits (game10) come back byte for byte") {
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
        if (n == 10 && test::fixture(*game, name)) {
            // Endlos_Laufend: player 0 explored islands 8, 9, 11, 13 and 14 with a ship; the iron
            // ore of island 10 belongs to computer 2's mine without anybody having explored it.
            std::set<unsigned> explored;
            for (const sim::WorldIsland& i : world->islands()) {
                if (i.explored_by(0)) {
                    explored.insert(i.header.number);
                }
                if (i.header.number == 10) {
                    CHECK(!i.explored_by(2));
                    bool bound = false;
                    for (const auto& d : i.header.deposits) {
                        bound = bound || sim::deposit_known(d, 2);
                    }
                    CHECK(bound);
                }
            }
            const std::set<unsigned> expected{8, 9, 11, 13, 14};
            CHECK(explored == expected);
        }
        auto chunks = sim::savegame_chunks(*world, *buildings);
        REQUIRE(chunks.ok());
        std::vector<std::vector<std::uint8_t>> before;
        for (const assets::Chunk& c : file->chunks()) {
            if (c.name == "INSEL5") {
                const auto bytes = file->payload(c);
                before.emplace_back(bytes.begin(), bytes.end());
            }
        }
        std::size_t k = 0;
        for (const sim::SavedChunk& c : *chunks) {
            if (c.name != "INSEL5") {
                continue;
            }
            REQUIRE(k < before.size());
            // Byte 0x1B (explored) and the deposits' byte 3 (known) as stored.
            CHECK(c.data.size() >= 0x2C);
            CHECK(c.data[0x1B] == before[k][0x1B]);
            CHECK(c.data[0x1C + 3] == before[k][0x1C + 3]);
            CHECK(c.data[0x24 + 3] == before[k][0x24 + 3]);
            ++k;
        }
        CHECK(k == before.size());
        ++checked;
    }
    if (checked == 0) {
        SKIP("no savegames");
    }
}
