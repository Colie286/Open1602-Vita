// SPDX-License-Identifier: GPL-3.0-or-later
// The computer players in a new endless game with the original data: they found
// their cities and build them up, and a game saved in between goes on exactly the same.

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "fixtures.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

struct Setup {
    assets::GameDirectory game;
    assets::BuildingTable buildings;
    assets::FigureTable figures;
    assets::TextTable text;
};

Setup setup_or_skip() {
    auto dir = assets::GameDirectory::from_environment();
    if (!dir) {
        SKIP("ANNO1602_DIR not set");
    }
    if (!dir->resolve("Szenes/Endlosspiel10.szs")) {
        SKIP("no endless game files");
    }
    auto buildings = assets::BuildingTable::load(*dir);
    auto figures = assets::FigureTable::load(*dir);
    auto text = assets::TextTable::load(*dir);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    REQUIRE(text.ok());
    return Setup{std::move(dir).value(), std::move(buildings).value(), std::move(figures).value(),
                 std::move(text).value()};
}

sim::World new_game(const Setup& s, std::uint64_t seed) {
    sim::NewGameOptions o;
    o.difficulty = 1;
    o.layout = 0;
    o.name = "Test";
    o.world.seed = seed;
    auto world = sim::start_new_game(s.game, s.buildings, s.text, o);
    REQUIRE(world.ok());
    return std::move(world).value();
}

/// The sections of two uncompressed Open1602 savegames that differ.
void report(const core::Bytes& one, const core::Bytes& two) {
    const auto sections = [](const core::Bytes& b) {
        std::vector<std::pair<std::string, std::span<const std::uint8_t>>> out;
        std::size_t o = 32;
        while (o + 12 <= b.size()) {
            const std::uint32_t size =
                static_cast<std::uint32_t>(b[o + 8]) | static_cast<std::uint32_t>(b[o + 9]) << 8U |
                static_cast<std::uint32_t>(b[o + 10]) << 16U | static_cast<std::uint32_t>(b[o + 11]) << 24U;
            out.emplace_back(
                std::string(b.begin() + static_cast<std::ptrdiff_t>(o), b.begin() + static_cast<std::ptrdiff_t>(o) + 4),
                std::span<const std::uint8_t>(b).subspan(o + 12, std::min<std::size_t>(size, b.size() - o - 12)));
            o += 12 + size;
        }
        return out;
    };
    const auto a = sections(one);
    const auto b = sections(two);
    for (std::size_t k = 0; k < std::min(a.size(), b.size()); ++k) {
        const auto& x = a[k].second;
        const auto& y = b[k].second;
        if (a[k].first != b[k].first || x.size() != y.size() || !std::equal(x.begin(), x.end(), y.begin())) {
            std::size_t d = 0;
            while (d < x.size() && d < y.size() && x[d] == y[d]) {
                ++d;
            }
            std::fprintf(stderr, "  section %s: %zu / %zu bytes, first difference at %zu\n", a[k].first.c_str(),
                         x.size(), y.size(), d);
        }
    }
}

void run_minutes(sim::Simulation& sim, std::uint32_t minutes) {
    for (std::uint32_t t = 0; t < minutes * 60'000 / sim::Simulation::kTickMs; ++t) {
        sim.tick();
    }
}

}  // namespace

TEST_CASE("data: the computers of a new endless game found their cities and build them up") {
    const Setup s = setup_or_skip();
    sim::World world = new_game(s, 7);
    sim::Simulation sim(world, s.buildings, s.figures);
    run_minutes(sim, 30);
    const auto view = sim.computers().view();
    std::size_t computers = 0;
    for (const auto& v : view) {
        const sim::Player* p = world.player(v.player);
        REQUIRE(p != nullptr);
        if (p->role != sim::PlayerRole::Computer) {
            continue;
        }
        ++computers;
        CHECK(v.cities >= 1);
        CHECK(v.max_stage >= 0x14);  // grown out of the founding stage
        CHECK(p->money > 0);
        // Its city: a Kontor, a market, houses with people, a woodcutter and roads.
        const sim::City* city = nullptr;
        for (const sim::City& c : world.cities()) {
            if (c.owner == v.player) {
                city = &c;
            }
        }
        REQUIRE(city != nullptr);
        std::uint32_t people = 0;
        for (const std::uint32_t n : city->inhabitants) {
            people += n;
        }
        CHECK(people >= 20);
        std::size_t houses = 0;
        for (const sim::House& h : world.houses()) {
            houses += h.island == city->island && h.city == city->index ? 1U : 0U;
        }
        CHECK(houses >= 8);
        std::set<std::uint32_t> kinds;
        std::size_t roads = 0;
        const sim::IslandMap& map = world.islands()[city->island].map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const sim::MapCell& c = map.top(x, y);
                if (c.city != city->index || c.empty()) {
                    continue;
                }
                const assets::BuildingType* b = s.buildings.by_sprite(c.sprite);
                if (b == nullptr) {
                    continue;
                }
                roads += b->kind == 1 ? 1U : 0U;
                kinds.insert(b->id);
            }
        }
        CHECK(kinds.contains(22101));  // Kontor I
        CHECK(kinds.contains(20809));  // market
        CHECK(kinds.contains(22001));  // woodcutter
        CHECK(roads >= 20);
    }
    CHECK(computers == 3);
}

TEST_CASE("data: a game loaded from Open1602's savegame exports the same .GAM as the original file") {
    const Setup s = setup_or_skip();
    // Every savegame of the installation: the export through the own format loses nothing.
    std::size_t tested = 0;
    for (const char* name : {"SAVEGAME/game00.gam", "SAVEGAME/game10.gam"}) {
        const auto path = test::fixture(s.game, name);
        if (!path) {
            continue;
        }
        auto file = assets::ChunkFile::load(*path);
        REQUIRE(file.ok());
        auto world = sim::load_world(*file, s.buildings, s.game);
        REQUIRE(world.ok());
        auto direct = sim::write_savegame(*world, s.buildings);
        REQUIRE(direct.ok());
        auto own = sim::write_game(*world, s.buildings);
        REQUIRE(own.ok());
        auto loaded = sim::read_game(*own, s.buildings);
        REQUIRE(loaded.ok());
        auto exported = sim::write_savegame(*loaded, s.buildings);
        REQUIRE(exported.ok());
        // Open1602's chunk at the end carries the save's time stamp of nothing: compare the
        // original's chunks.
        auto a = assets::ChunkFile::from_bytes(*direct);
        auto b = assets::ChunkFile::from_bytes(*exported);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(a->chunks().size() == b->chunks().size());
        for (std::size_t i = 0; i < a->chunks().size(); ++i) {
            const auto& ca = a->chunks()[i];
            const auto& cb = b->chunks()[i];
            CHECK(ca.name == cb.name);
            const auto pa = a->payload(ca);
            const auto pb = b->payload(cb);
            if (ca.name != "O1602") {
                CHECK(std::equal(pa.begin(), pa.end(), pb.begin(), pb.end()));
            }
        }
        ++tested;
    }
    if (tested == 0) {
        SKIP("no savegame of the original (the History Edition has none)");
    }
}

TEST_CASE("data: a world without its original file exports its players to a .GAM") {
    const Setup s = setup_or_skip();
    sim::World world = new_game(s, 7);
    world.set_source(nullptr);  // as a game from an older .o1602 without the original's bytes
    auto exported = sim::write_savegame(world, s.buildings);
    REQUIRE(exported.ok());
    auto file = assets::ChunkFile::from_bytes(*exported);
    REQUIRE(file.ok());
    auto players = assets::load_players(*file);
    REQUIRE(players.ok());
    REQUIRE(players->size() >= world.players().size());
    for (std::size_t p = 0; p < world.players().size(); ++p) {
        const sim::Player& want = world.players()[p];
        const assets::PlayerRecord& got = (*players)[p];
        if (want.status == sim::PlayerStatus::Playing) {
            CHECK(sim::role_from_original(got.kind) == want.role);
        }
        CHECK(sim::banner_from_original(got.colour) == want.banner);
        CHECK(got.name == want.name);
    }
    // The islands keep their templates.
    auto islands = assets::load_islands(*file);
    REQUIRE(islands.ok());
    for (std::size_t i = 0; i < islands->size(); ++i) {
        CHECK((*islands)[i].header.template_number == world.islands()[i].header.template_number);
    }
}

TEST_CASE("data: a game with computer players saved in between goes on exactly the same") {
    const Setup s = setup_or_skip();
    sim::World world = new_game(s, 11);
    sim::Simulation a(world, s.buildings, s.figures);
    run_minutes(a, 12);
    auto saved = sim::write_game(world, s.buildings, &a);
    REQUIRE(saved.ok());
    auto loaded = sim::read_game(*saved, s.buildings);
    REQUIRE(loaded.ok());
    sim::Simulation b(*loaded, s.buildings, s.figures);
    run_minutes(a, 4);
    run_minutes(b, 4);
    auto first = sim::write_game(world, s.buildings, &a);
    auto second = sim::write_game(*loaded, s.buildings, &b);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(*first == *second);
    // The computers kept building meanwhile.
    std::size_t plans = 0;
    for (const auto& v : b.computers().view()) {
        plans += v.plans.size();
    }
    CHECK(plans >= 3);
    // The same through a savegame in the original's format with Open1602's chunk: the cities'
    // records keep some bytes of the scenario (a city founded during play has none), so only the
    // game itself is compared, through Open1602's own savegame.
    auto legacy = sim::write_savegame(world, s.buildings, &a);
    REQUIRE(legacy.ok());
    auto file = assets::ChunkFile::from_bytes(*legacy);
    REQUIRE(file.ok());
    auto reloaded = sim::load_world(*file, s.buildings, s.game);
    REQUIRE(reloaded.ok());
    sim::Simulation c(*reloaded, s.buildings, s.figures);
    run_minutes(a, 2);
    run_minutes(c, 2);
    // The original's file each came from differs (ORIG); the game is what is compared.
    world.set_source(nullptr);
    reloaded->set_source(nullptr);
    auto third = sim::write_game(world, s.buildings, &a, sim::SaveOptions{false});
    auto fourth = sim::write_game(*reloaded, s.buildings, &c, sim::SaveOptions{false});
    REQUIRE(third.ok());
    REQUIRE(fourth.ok());
    if (*third != *fourth) {
        report(*third, *fourth);
    }
    CHECK(*third == *fourth);
}

// Several game hours (review BD-6): the computers keep their cities and keep building, nothing
// leaves the map or loses its owner, and the game still saves and goes on exactly the same.
TEST_CASE("data: the computers of a new endless game stay sound over three game hours") {
    const Setup s = setup_or_skip();
    sim::World world = new_game(s, 23);
    sim::Simulation sim(world, s.buildings, s.figures);
    const auto houses_of = [&](sim::PlayerId owner) {
        std::size_t n = 0;
        for (const sim::House& h : world.houses()) {
            for (const sim::City& c : world.cities()) {
                n += c.owner == owner && h.island == c.island && h.city == c.index ? 1U : 0U;
            }
        }
        return n;
    };
    const auto sound = [&] {
        bool ok = true;
        for (const sim::Ship& ship : world.ships()) {
            ok = ok && ship.x < world.width() && ship.y < world.height();
            ok = ok && (ship.owner == sim::kNoPlayer || world.player(ship.owner) != nullptr);
        }
        for (const sim::Soldier& soldier : world.soldiers()) {
            ok = ok && (soldier.owner == sim::kNoPlayer || world.player(soldier.owner) != nullptr);
        }
        return ok;
    };

    run_minutes(sim, 30);
    std::vector<std::size_t> early;
    for (const auto& v : sim.computers().view()) {
        early.push_back(houses_of(v.player));
    }
    for (int quarter = 0; quarter < 10; ++quarter) {
        run_minutes(sim, 15);
        CHECK(sound());
    }
    const auto view = sim.computers().view();
    REQUIRE(view.size() == early.size());
    for (std::size_t i = 0; i < view.size(); ++i) {
        const sim::Player* p = world.player(view[i].player);
        REQUIRE(p != nullptr);
        if (p->role != sim::PlayerRole::Computer) {
            continue;
        }
        std::printf("  computer %u: %zu cities, stage 0x%02x, %zu houses (%zu after 30 min), money %lld\n",
                    static_cast<unsigned>(view[i].player), static_cast<std::size_t>(view[i].cities),
                    static_cast<unsigned>(view[i].max_stage), houses_of(view[i].player), early[i],
                    static_cast<long long>(p->money));
        CHECK(view[i].cities >= 1);
        CHECK(houses_of(view[i].player) >= early[i]);  // the cities grew, or at least kept their houses
    }
    // A computer that wants more ships plans a shipyard whatever its stage, so after
    // three hours at least one of them has more than the ship it started with (the original's
    // computers had warships after about two hours).
    std::size_t yards = 0;
    for (const sim::CostBuilding& b : world.cost_buildings()) {
        yards += b.kind == sim::CostBuilding::Kind::Shipyard ? 1U : 0U;
    }
    CHECK(yards >= 1);

    auto saved = sim::write_game(world, s.buildings, &sim);
    REQUIRE(saved.ok());
    auto loaded = sim::read_game(*saved, s.buildings);
    REQUIRE(loaded.ok());
    sim::Simulation again(*loaded, s.buildings, s.figures);
    CHECK(!again.state_damaged());
    run_minutes(sim, 2);
    run_minutes(again, 2);
    auto first = sim::write_game(world, s.buildings, &sim);
    auto second = sim::write_game(*loaded, s.buildings, &again);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    if (*first != *second) {
        report(*first, *second);
    }
    CHECK(*first == *second);
}

TEST_CASE("data: the computer of the tutorial stays as it is; the flag survives saving") {
    const Setup s = setup_or_skip();
    // "Diplomatie & Handel" has a computer opponent; TIMERS +0xA5 marks the tutorial. The original's
    // computer there builds nothing: its money only grows by taxes.
    const auto path = s.game.resolve("Szenes/Einführungsspiel2.szs");
    if (!path) {
        SKIP("no tutorial scenario");
    }
    auto file = assets::ChunkFile::load(*path);
    REQUIRE(file.ok());
    auto world = sim::load_world(*file, s.buildings, s.game);
    REQUIRE(world.ok());
    CHECK(world->game_settings().tutorial);
    const auto productions = world->productions().size();
    const auto houses = world->houses().size();
    const std::int32_t money = world->players()[1].money;
    REQUIRE(world->players()[1].role == sim::PlayerRole::Computer);
    {
        sim::Simulation simulation(*world, s.buildings, s.figures, sim::Rules::original());
        for (int second = 0; second < 600; ++second) {
            simulation.advance(1000);
        }
        // Open1602 saves the flag in its own savegame and writes it into the .GAM.
        auto own = sim::write_game(*world, s.buildings, &simulation);
        REQUIRE(own.ok());
        auto loaded = sim::read_game(*own, s.buildings);
        REQUIRE(loaded.ok());
        CHECK(loaded->game_settings().tutorial);
        auto gam = sim::write_savegame(*world, s.buildings, &simulation);
        REQUIRE(gam.ok());
        auto exported = assets::ChunkFile::from_bytes(*gam);
        REQUIRE(exported.ok());
        auto again = sim::load_world(*exported, s.buildings, s.game);
        REQUIRE(again.ok());
        CHECK(again->game_settings().tutorial);
    }
    CHECK(world->productions().size() == productions);
    CHECK(world->houses().size() == houses);
    CHECK(world->players()[1].money >= money);

    // An endless game is no tutorial.
    const auto endless = s.game.resolve("Szenes/Endlosspiel10.szs");
    REQUIRE(endless.has_value());
    auto efile = assets::ChunkFile::load(*endless);
    REQUIRE(efile.ok());
    auto eworld = sim::load_world(*efile, s.buildings, s.game);
    REQUIRE(eworld.ok());
    CHECK(!eworld->game_settings().tutorial);
}

TEST_CASE("data: no ship of the computers in game10 lies idle for long (America, new ships at the yard)") {
    const Setup s = setup_or_skip();
    const auto path = test::fixture(s.game, "SAVEGAME/game10.gam");
    if (!path) {
        SKIP("no game10.gam");
    }
    auto file = assets::ChunkFile::load(*path);
    REQUIRE(file.ok());
    auto world = sim::load_world(*file, s.buildings, s.game);
    REQUIRE(world.ok());
    sim::Simulation sim(*world, s.buildings, s.figures);
    sim.offer_names(s.text.section("STAEDTE"), s.text.section("SHIPS"));
    // The longest time each computer ship stayed on one tile, sampled every second. Before, America
    // lay 17 minutes at its full home warehouse with goods that did not fit, and a new ship at a
    // yard next to the Kontor was sent there again on every look and never got a job.
    struct Watch {
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint32_t still = 0;
        std::uint32_t longest = 0;
        bool named = false;
    };
    std::map<std::uint32_t, Watch> ships;
    constexpr std::uint32_t kMinutes = 50;
    for (std::uint32_t second = 0; second < kMinutes * 60; ++second) {
        for (std::uint32_t t = 0; t < 1000 / sim::Simulation::kTickMs; ++t) {
            sim.tick();
        }
        for (const sim::Ship& ship : world->ships()) {
            const sim::Player* owner = world->player(ship.owner);
            if (ship.trader || owner == nullptr || owner->role != sim::PlayerRole::Computer) {
                continue;
            }
            auto [it, fresh] = ships.try_emplace(ship.id);
            Watch& w = it->second;
            w.named = !ship.name.empty();
            w.still = !fresh && w.x == ship.x && w.y == ship.y ? w.still + 1 : 0;
            w.longest = std::max(w.longest, w.still);
            w.x = ship.x;
            w.y = ship.y;
        }
    }
    CHECK(ships.size() >= 4);
    for (const auto& [id, w] : ships) {
        if (w.longest >= 300 || !w.named) {
            std::printf("  ship %u: still for %u s, named %d\n", id, w.longest, w.named ? 1 : 0);
        }
        CHECK(w.longest < 300);
        CHECK(w.named);
    }
}

TEST_CASE("data: the units aboard at the start of the tutorial's land battle load, land and survive saving") {
    const Setup s = setup_or_skip();
    // „Landgefecht“ starts with the human's ship carrying a swordsman, two riders and a musketeer.
    const auto path = s.game.resolve("Szenes/Einführungsspiel4.szs");
    if (!path) {
        SKIP("no tutorial scenario");
    }
    auto file = assets::ChunkFile::load(*path);
    REQUIRE(file.ok());
    auto world = sim::load_world(*file, s.buildings, s.game);
    REQUIRE(world.ok());
    const auto holds = [](const sim::World& w) {
        std::vector<std::uint8_t> wares;
        for (const sim::Ship& ship : w.ships()) {
            for (const sim::CargoSlot& c : ship.cargo) {
                if (ship.owner == 0 && c.ware != 0) {
                    CHECK(c.amount == 50 * sim::kStockUnitsPerTon);
                    wares.push_back(c.ware);
                }
            }
        }
        return wares;
    };
    // By family, whatever colour the file names: SOLDAT1, KAVALERIE1, MUSKETIER1.
    const std::vector<std::uint8_t> expected{assets::unit_ware(1), assets::unit_ware(5), assets::unit_ware(5),
                                             assets::unit_ware(9)};
    CHECK(holds(*world) == expected);
    {
        auto gam = sim::write_savegame(*world, s.buildings, nullptr);
        REQUIRE(gam.ok());
        auto exported = assets::ChunkFile::from_bytes(*gam);
        REQUIRE(exported.ok());
        auto again = sim::load_world(*exported, s.buildings, s.game);
        REQUIRE(again.ok());
        CHECK(holds(*again) == expected);
    }
    // They land on the coast by the enemy's units.
    sim::Simulation simulation(*world, s.buildings, s.figures, sim::Rules::original());
    sim::Ship* ship = nullptr;
    for (sim::Ship& sh : world->ships()) {
        ship = sh.owner == 0 ? &sh : ship;
    }
    REQUIRE(ship != nullptr);
    ship->place(235, 131);
    sim::UnloadOrder unload;
    unload.player = 0;
    unload.ship = ship->id;
    simulation.submit(unload);
    simulation.advance(200);
    std::size_t landed = 0;
    for (const sim::Soldier& soldier : world->soldiers()) {
        landed += soldier.owner == 0 && soldier.island == 0 ? 1U : 0U;
    }
    CHECK(landed == 4);
}
