// SPDX-License-Identifier: GPL-3.0-or-later
// Open1602's own savegame (docs/formats/o1602-save.md): the whole world without the limits of the
// original's format, a checksum, compression, and nothing loaded from a damaged file or a world
// that does not hold together.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>

#include "../test_framework.hpp"
#include "golden.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/core/compress.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/state_io.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/volcanoes.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kHouse = 20501;

assets::BuildingTable make_buildings(bool with_house = true) {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    std::string cod =
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n";
    if (with_house) {
        cod += "  @Nummer: 2\n  Id: 20501\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n";
    }
    cod += "EndObj\n";
    const auto doc = assets::cod::parse(cod, types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world far beyond the original's limits: 900 x 600 tiles, nine players, 200 ships, 500 units.
sim::World make_world(const assets::BuildingTable& buildings) {
    sim::World world(900, 600);
    for (std::uint16_t i = 0; i < 2; ++i) {
        sim::IslandMap map(30, 20);
        for (std::int32_t y = 0; y < 20; ++y) {
            for (std::int32_t x = 0; x < 30; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        REQUIRE(map.build(*buildings.by_id(kRoad), 3, 4, 0, 0, 1, 0).ok());
        REQUIRE(map.build(*buildings.by_id(kHouse), 5, 4, 1, 2, 1, 0).ok());
        assets::IslandHeader header;
        header.number = static_cast<std::uint8_t>(i);
        header.width = 30;
        header.height = 20;
        header.deposits.push_back({2, 7, 8, 0x101, 0, 900});
        REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), 100U + 400U * i, 300}).ok());
    }
    std::vector<sim::Player> players(9);
    for (std::size_t p = 0; p < players.size(); ++p) {
        players[p].role = p == 0 ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
        players[p].banner = static_cast<sim::BannerId>(p);
        players[p].money = 1000 * static_cast<std::int32_t>(p);
        players[p].name = "Spieler " + std::to_string(p);
    }
    world.set_players(players);
    sim::City city;
    city.island = 0;
    city.index = 0;
    city.owner = 1;
    city.name = "Neustadt";
    city.goods.assign(30, 64);
    city.purchases = 480 * 75;
    city.sales = 480 * 1234;
    world.add_city(city);
    for (std::uint32_t k = 0; k < 200; ++k) {
        sim::Ship ship;
        ship.x = static_cast<std::uint16_t>(10 + k);
        ship.y = static_cast<std::uint16_t>(50 + k % 7);
        ship.owner = static_cast<sim::PlayerId>(k % 9);
        ship.name = "Schiff " + std::to_string(k);
        ship.cargo.resize(8);
        ship.cargo[0].ware = 23;
        ship.cargo[0].amount = 32 * (k % 50);
        if (k == 3) {
            ship.target = sim::Target::position(60, 60);
            ship.queue = {sim::Target::position(70, 60), sim::Target::position(70, 70)};
            ship.attack_move = true;
        }
        world.add_ship(ship);
    }
    for (std::uint32_t k = 0; k < 500; ++k) {
        sim::Soldier soldier;
        soldier.pos_x = static_cast<std::int32_t>((100 + k % 30) * 4096);
        soldier.pos_y = 300 * 4096;
        soldier.island = 0;
        soldier.owner = static_cast<sim::PlayerId>(k % 9);
        soldier.figure = 1;
        if (k == 7) {
            soldier.queue = {sim::Target::spot(220, 600)};
            soldier.hold = true;
        }
        world.add_soldier(soldier);
    }
    world.set_time_ms(123'456'750);
    return world;
}

}  // namespace

TEST_CASE("save game: a world beyond the original's limits comes back whole") {
    const auto buildings = make_buildings();
    const sim::World world = make_world(buildings);
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    CHECK(sim::is_open1602_save(*bytes));
    auto info = sim::save_info(*bytes);
    REQUIRE(info.ok());
    CHECK(info->version == 1);
    CHECK(info->compressed == core::has_zlib());
    // The list of savegames reads META from the head of the file (without zlib, packed files have none).
    REQUIRE(info->meta.has_value());
    CHECK(info->meta->width == 900);
    CHECK(info->meta->height == 600);
    CHECK(info->meta->time_ms == 123'456'750);
    CHECK(info->meta->player == "Spieler 0");
    auto unpacked = sim::write_game(world, buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(unpacked.ok());
    auto plain_info = sim::save_info(*unpacked);
    REQUIRE(plain_info.ok());
    REQUIRE(plain_info->meta.has_value());
    CHECK(plain_info->meta->player == "Spieler 0");
    std::vector<std::string> warnings;
    auto loaded = sim::read_game(*bytes, buildings, &warnings);
    REQUIRE(loaded.ok());
    CHECK(warnings.empty());
    CHECK(loaded->width() == 900);
    CHECK(loaded->height() == 600);
    CHECK(loaded->islands().size() == 2);
    CHECK(loaded->islands()[1].x == 500);
    CHECK(loaded->players().size() == 9);
    CHECK(loaded->players()[8].name == "Spieler 8");
    CHECK(loaded->ships().size() == 200);
    CHECK(loaded->soldiers().size() == 500);
    CHECK(loaded->cities().size() == 1);
    CHECK(loaded->cities()[0].name == "Neustadt");
    CHECK(loaded->cities()[0].purchases == 480 * 75);  // CSTA
    CHECK(loaded->cities()[0].sales == 480 * 1234);
    CHECK(loaded->time_ms() == 123'456'750);
    // Queued orders (QUEU).
    CHECK(loaded->ships()[3].queue == world.ships()[3].queue);
    CHECK(loaded->ships()[3].queue.size() == 2);
    CHECK(loaded->soldiers()[7].queue.size() == 1);
    // Attack move and hold position (STNC).
    CHECK(loaded->ships()[3].attack_move);
    CHECK(!loaded->ships()[4].attack_move);
    CHECK(loaded->soldiers()[7].hold);
    // Both layers of every tile.
    const sim::IslandMap& a = world.islands()[0].map;
    const sim::IslandMap& b = loaded->islands()[0].map;
    for (std::uint32_t y = 0; y < a.height(); ++y) {
        for (std::uint32_t x = 0; x < a.width(); ++x) {
            CHECK(a.top(x, y).sprite == b.top(x, y).sprite);
            CHECK(a.top(x, y).rotation == b.top(x, y).rotation);
            CHECK(a.top(x, y).owner == b.top(x, y).owner);
            CHECK(a.ground(x, y).sprite == b.ground(x, y).sprite);
        }
    }
    CHECK(loaded->islands()[0].header.deposits.size() == 1);
    CHECK(loaded->islands()[0].header.deposits[0].mask == 0x101U);
    // The format is canonical: saving the loaded world gives the same bytes.
    auto again = sim::write_game(*loaded, buildings);
    REQUIRE(again.ok());
    CHECK(*again == *bytes);
    // Uncompressed it holds the same.
    auto plain = sim::write_game(world, buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(plain.ok());
    auto from_plain = sim::read_game(*plain, buildings);
    REQUIRE(from_plain.ok());
    auto plain_again = sim::write_game(*from_plain, buildings);
    REQUIRE(plain_again.ok());
    CHECK(*plain_again == *bytes);
}

namespace {

/// The version of the first section `tag` in an unpacked save (header of 32 bytes, then sections).
std::uint16_t section_version(const core::Bytes& bytes, std::string_view tag) {
    std::size_t at = 32;
    while (at + 12 <= bytes.size()) {
        const std::string_view id(reinterpret_cast<const char*>(bytes.data() + at), 4);
        const auto version = static_cast<std::uint16_t>(bytes[at + 4] | bytes[at + 5] << 8);
        const std::uint32_t size = std::uint32_t{bytes[at + 8]} | std::uint32_t{bytes[at + 9]} << 8 |
                                   std::uint32_t{bytes[at + 10]} << 16 | std::uint32_t{bytes[at + 11]} << 24;
        if (id == tag) {
            return version;
        }
        at += 12 + size;
    }
    return 0;
}

}  // namespace

TEST_CASE("save game: the fingerprint of the running game sees every tile, and only the game") {
    const auto buildings = make_buildings();
    const assets::FigureTable figures;
    sim::World world = make_world(buildings);
    sim::World twin = make_world(buildings);
    sim::Simulation simulation(world, buildings, figures);
    sim::Simulation other(twin, buildings, figures);
    const std::uint64_t start = simulation.state_hash();
    CHECK(simulation.state_hash() == start);  // the buffer kept from last time adds nothing
    CHECK(other.state_hash() == start);
    // One road on the second island.
    REQUIRE(world.island(1).map.build(*buildings.by_id(kRoad), 20, 10, 0, 0, 1, 0).ok());
    const std::uint64_t built = simulation.state_hash();
    CHECK(built != start);
    REQUIRE(twin.island(1).map.build(*buildings.by_id(kRoad), 20, 10, 0, 0, 1, 0).ok());
    CHECK(other.state_hash() == built);
}

TEST_CASE("save game: the players' stored groups come back, outside the game's fingerprint (UGRP)") {
    const auto buildings = make_buildings();
    const assets::FigureTable figures;
    sim::World world = make_world(buildings);
    sim::UnitGroups groups;
    groups[0] = {{true, 3, -8, 4}, {true, 5, 8, -4}};
    groups[9] = {{false, 70000, 0, 0}};
    {
        sim::Simulation simulation(world, buildings, figures);
        const std::uint64_t before = simulation.state_hash();
        world.set_unit_groups(1, groups);
        CHECK(simulation.state_hash() == before);  // every computer has its own
    }
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    REQUIRE(loaded->unit_groups().size() == 2);
    CHECK(loaded->unit_groups()[1] == groups);
    CHECK(loaded->unit_groups()[0] == sim::UnitGroups{});
    // Without groups the section is not written.
    world.set_unit_groups({});
    auto none = sim::write_game(world, buildings);
    REQUIRE(none.ok());
    auto loaded_none = sim::read_game(*none, buildings);
    REQUIRE(loaded_none.ok());
    CHECK(loaded_none->unit_groups().empty());
}

TEST_CASE("save game: the content packages a game uses come back in their order (PACK)") {
    const auto buildings = make_buildings();
    sim::World world(60, 40);
    auto without = sim::write_game(world, buildings);
    REQUIRE(without.ok());
    auto older = sim::read_game(*without, buildings);
    REQUIRE(older.ok());
    CHECK(!older->packages().has_value());  // plays with the packages installed

    world.set_packages({});
    auto none = sim::write_game(world, buildings);
    REQUIRE(none.ok());
    auto loaded_none = sim::read_game(*none, buildings);
    REQUIRE(loaded_none.ok());
    REQUIRE(loaded_none->packages().has_value());
    CHECK(loaded_none->packages()->empty());

    const std::vector<assets::ContentPackage> packages{{"zeta", "1.2", 0x1234'5678'9ABC'DEF0ULL},
                                                       {"\xc3\xa4rger", "", 7}};
    world.set_packages(packages);
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    REQUIRE(loaded->packages().has_value());
    CHECK(*loaded->packages() == packages);
}

TEST_CASE("save game: fight counters beyond 65 535 come back whole, smaller ones keep SCOR 1 (LC-9)") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    REQUIRE(!world.players().empty());
    {
        auto narrow = sim::write_game(world, buildings, nullptr, sim::SaveOptions{false});
        REQUIRE(narrow.ok());
        CHECK(section_version(*narrow, "SCOR") == 1);
    }
    std::vector<sim::Player> players = world.players();
    players[0].soldiers_killed = 100'000;
    players[0].ships_lost = 70'000;
    players[0].soldiers_lost = 5;
    sim::add_count(players[0].soldiers_lost, 0xFFFFFFFFU);  // stops at the top, never wraps
    world.set_players(players);
    auto bytes = sim::write_game(world, buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    CHECK(section_version(*bytes, "SCOR") == 2);
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    CHECK(loaded->players()[0].soldiers_killed == 100'000);
    CHECK(loaded->players()[0].ships_lost == 70'000);
    CHECK(loaded->players()[0].soldiers_lost == 0xFFFFFFFFU);
}

TEST_CASE("save game: craters and deer spots beyond 255 tiles come back whole (SC-13)") {
    const auto buildings = make_buildings();
    // A world that fits 8 bits keeps version 1 of ISLE and VOLC: the bytes of earlier versions.
    {
        auto narrow = sim::write_game(make_world(buildings), buildings, nullptr, sim::SaveOptions{false});
        REQUIRE(narrow.ok());
        CHECK(section_version(*narrow, "ISLE") == 1);
        CHECK(section_version(*narrow, "VOLC") == 1);
    }
    // One island 300 tiles wide, one player: within the original's limits but for the island.
    sim::World world(400, 100);
    sim::IslandMap map(300, 20);
    for (std::int32_t y = 0; y < 20; ++y) {
        for (std::int32_t x = 0; x < 300; ++x) {
            REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
        }
    }
    map.capture_ground();
    assets::IslandHeader header;
    header.number = 0;  // header.width is the original's byte; the map holds the real size
    REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), 50, 40}).ok());
    std::vector<sim::Player> players(1);
    players[0].role = sim::PlayerRole::Human;
    world.set_players(players);
    sim::WorldIsland& island = world.island(0);
    island.craters.push_back(sim::Crater{280, 5, 0});
    island.deer_spots[0] = sim::DeerSpot{true, 290, 12, 60'000};
    island.deer_spots[1] = sim::DeerSpot{true, 7, 3, 0};
    // The eruption finds its crater at x = 280 (an 8-bit model kept it at 24).
    REQUIRE(sim::erupt(world, sim::CraterRef{0, 280, 5}, 77, false, nullptr));
    world.volcanoes().last = sim::CraterRef{0, 280, 5};
    CHECK(sim::validate(world).ok());

    auto bytes = sim::write_game(world, buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    CHECK(section_version(*bytes, "ISLE") == 2);
    CHECK(section_version(*bytes, "VOLC") == 2);
    std::vector<std::string> warnings;
    auto loaded = sim::read_game(*bytes, buildings, &warnings);
    REQUIRE(loaded.ok());
    CHECK(warnings.empty());
    const sim::WorldIsland& back = loaded->islands()[0];
    CHECK(back.map.width() == 300);
    REQUIRE(back.craters.size() == 1);
    CHECK(back.craters[0].x == 280);
    CHECK(back.craters[0].y == 5);
    CHECK(back.craters[0].steps_left == island.craters[0].steps_left);
    CHECK(back.deer_spots[0].used);
    CHECK(back.deer_spots[0].x == 290);
    CHECK(back.deer_spots[0].y == 12);
    CHECK(back.deer_spots[0].respawn_ms == 60'000);
    CHECK(back.deer_spots[1].x == 7);
    REQUIRE(loaded->volcanoes().eruptions.size() == 1);
    CHECK(loaded->volcanoes().eruptions[0].crater == (sim::CraterRef{0, 280, 5}));
    CHECK(loaded->volcanoes().last == std::optional<sim::CraterRef>(sim::CraterRef{0, 280, 5}));
    auto again = sim::write_game(*loaded, buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(again.ok());
    CHECK(*again == *bytes);

    // A deer spot outside its island is refused like a crater outside it.
    sim::World outside = std::move(loaded).value();
    outside.island(0).deer_spots[2] = sim::DeerSpot{true, 300, 0, 0};
    CHECK(!sim::validate(outside).ok());

    // The original's format has one byte per island coordinate: the export refuses the island
    // instead of cutting x = 280 down to 24.
    auto chunks = sim::savegame_chunks(world, buildings);
    REQUIRE(!chunks.ok());
    CHECK(chunks.error().code == core::ErrorCode::LimitExceeded);
    CHECK(chunks.error().message.find("255") != std::string::npos);
    CHECK(!sim::write_savegame(world, buildings).ok());
}

TEST_CASE("save game: a damaged, cut off or newer file loads nothing") {
    const auto buildings = make_buildings();
    const sim::World world = make_world(buildings);
    auto bytes = sim::write_game(world, buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    // One byte of the data changed: the checksum catches it.
    core::Bytes damaged = *bytes;
    damaged[damaged.size() / 2] ^= 0x40;
    auto r1 = sim::read_game(damaged, buildings);
    CHECK(!r1.ok());
    // Cut off.
    core::Bytes cut(bytes->begin(), bytes->end() - 100);
    CHECK(!sim::read_game(cut, buildings).ok());
    CHECK(!sim::read_game(core::Bytes(bytes->begin(), bytes->begin() + 20), buildings).ok());
    // A newer format.
    core::Bytes newer = *bytes;
    newer[8] = 2;
    auto r3 = sim::read_game(newer, buildings);
    REQUIRE(!r3.ok());
    CHECK(r3.error().code == core::ErrorCode::Unsupported);
    // Not a savegame at all.
    CHECK(!sim::is_open1602_save(core::Bytes(64, 0)));
    // A building the game's data does not have.
    const auto without_house = make_buildings(false);
    auto r4 = sim::read_game(*bytes, without_house);
    CHECK(!r4.ok());
}

TEST_CASE("save game: unpacking follows the data, not what the header promises") {
    if (!core::has_zlib()) {
        SKIP("no zlib");
    }
    std::vector<std::uint8_t> data(3'000'000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(i * 7 / 1000);
    }
    auto packed = core::zlib_deflate(data);
    REQUIRE(packed.ok());
    auto back = core::zlib_inflate(*packed, data.size());
    REQUIRE(back.ok());
    CHECK(*back == data);
    CHECK(!core::zlib_inflate(*packed, data.size() - 1).ok());  // more data than promised
    CHECK(!core::zlib_inflate(*packed, data.size() + 1).ok());  // less
    const std::vector<std::uint8_t> cut(packed->begin(), packed->end() - 10);
    CHECK(!core::zlib_inflate(cut, data.size()).ok());
    // A 2 GiB promise from a few bytes is refused before anything is allocated.
    CHECK(!core::zlib_inflate(std::span(packed->data(), 33), std::size_t{1} << 31).ok());
    // The fastest level (large games) is read the same way.
    auto fast = core::zlib_deflate(data, 1);
    REQUIRE(fast.ok());
    auto fast_back = core::zlib_inflate(*fast, data.size());
    REQUIRE(fast_back.ok());
    CHECK(*fast_back == data);
    auto empty = core::zlib_deflate({});
    REQUIRE(empty.ok());
    auto none = core::zlib_inflate(*empty, 0);
    REQUIRE(none.ok());
    CHECK(none->empty());
}

TEST_CASE("save game: a list grows with the records the data holds, not with its count") {
    // A count of a million records of 2 bytes each, followed by three bytes.
    const std::vector<std::uint8_t> data = {0x40, 0x42, 0x0F, 0x00, 1, 0, 2};
    core::ByteReader reader(data);
    sim::StateIn in(reader);
    std::vector<std::uint64_t> list;
    in.list(list, 2, [&](std::uint64_t& v) { v = in.u16(); });
    CHECK(!in.ok());
    CHECK(list.empty());  // the count does not fit: nothing is allocated

    const std::vector<std::uint8_t> two = {2, 0, 0, 0, 5, 0, 6, 0};
    core::ByteReader reader2(two);
    sim::StateIn in2(reader2);
    in2.list(list, 2, [&](std::uint64_t& v) { v = in2.u16(); });
    CHECK(in2.ok());
    REQUIRE(list.size() == 2);
    CHECK(list[1] == 6);
}

TEST_CASE("save game: an island far outside the world or a world beyond the limit is refused") {
    const auto buildings = make_buildings();
    sim::World world(100, 100);
    for (const auto& [x, y] : {std::pair{0xFFFFFFFFU, 0U}, std::pair{0U, 0xFFFFFFFFU}, std::pair{99U, 0U}}) {
        sim::IslandMap map(2, 2);
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), x, y};
        CHECK(!world.add_island(std::move(island)).ok());
    }
    CHECK(world.islands().empty());

    // MAPW naming a world of 65535 x 65535 tiles (25 GB): refused, nothing allocated.
    core::ByteWriter sections;
    sections.bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("MAPW"), 4));
    sections.u16(1);
    sections.u16(0);
    sections.u32(12);
    sections.u32(0xFFFF);
    sections.u32(0xFFFF);
    sections.u32(0);
    const core::Bytes body = sections.take();
    core::ByteWriter file;
    file.bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("OPEN1602"), 8));
    file.u16(1);
    file.u16(0);
    file.u32(0);
    file.u64(body.size());
    file.u32(core::crc32(body));
    file.u32(static_cast<std::uint32_t>(body.size()));
    file.bytes(body);
    const core::Bytes bytes = file.take();
    auto loaded = sim::read_game(bytes, buildings);
    REQUIRE(!loaded.ok());
    CHECK(loaded.error().message.find("world size") != std::string::npos);
}

TEST_CASE("save game: a world whose references do not hold is refused (validate)") {
    const auto buildings = make_buildings();
    const sim::World good = make_world(buildings);
    CHECK(sim::validate(good).ok());

    const auto refused = [&](const auto& damage) {
        sim::World world = make_world(buildings);
        damage(world);
        if (sim::validate(world).ok()) {
            return false;
        }
        // Saved as it is, the file does not load either.
        auto bytes = sim::write_game(world, buildings);
        return bytes.ok() && !sim::read_game(*bytes, buildings).ok();
    };
    CHECK(refused([](sim::World& w) { w.city(0, 0)->owner = 40; }));
    CHECK(refused([](sim::World& w) { w.ships()[5].owner = 9; }));
    CHECK(refused([](sim::World& w) { w.soldiers()[3].island = 2; }));
    CHECK(refused([](sim::World& w) { w.soldiers()[4].pos_x = 900 * 4096; }));
    CHECK(refused([](sim::World& w) { w.island(1).craters.push_back(sim::Crater{30, 0, 0}); }));
    // Ids are unique and below the next one; island numbers are unique; at most 254 players.
    CHECK(refused([](sim::World& w) { w.ships()[6].id = w.ships()[5].id; }));
    CHECK(refused([](sim::World& w) { w.set_next_ship_id(0xFFFFFFFFU); }));
    CHECK(refused([](sim::World& w) { w.island(1).header.number = w.island(0).header.number; }));
    CHECK(refused([](sim::World& w) {
        std::vector<sim::Player> players = w.players();
        players.resize(300, players.back());
        w.set_players(std::move(players));
    }));
    CHECK(refused([](sim::World& w) {
        sim::House house;
        house.island = 0;
        house.x = 4;
        house.y = 20;
        w.add_house(house);
    }));
}

TEST_CASE("save game: a damaged state of the running game is refused when loading (SIMU)") {
    const auto buildings = make_buildings();
    const assets::FigureTable figures;
    sim::World world = make_world(buildings);
    sim::Simulation simulation(world, buildings, figures);
    for (int t = 0; t < 20; ++t) {
        simulation.tick();
    }
    auto bytes = sim::write_game(world, buildings, &simulation, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings, nullptr, &figures);
    REQUIRE(loaded.ok());
    CHECK(sim::Simulation::state_readable(*loaded, buildings, figures));

    // The version of the SIMU section (its first two bytes) made unknown, the checksum made right.
    constexpr std::size_t kHeader = 32;
    core::Bytes damaged = *bytes;
    const std::string_view text(reinterpret_cast<const char*>(damaged.data()), damaged.size());
    const std::size_t at = text.find("SIMU", kHeader);
    REQUIRE(at != std::string_view::npos);
    damaged[at + 12] = 0x63;
    damaged[at + 13] = 0;
    const std::uint32_t crc = core::crc32(std::span(damaged).subspan(kHeader));
    for (std::size_t b = 0; b < 4; ++b) {
        damaged[24 + b] = static_cast<std::uint8_t>(crc >> (8 * b));
    }
    auto refused = sim::read_game(damaged, buildings, nullptr, &figures);
    REQUIRE(!refused.ok());
    CHECK(refused.error().message.find("SIMU") != std::string::npos);
    // Without the figures the world loads, and the game starts afresh and says so.
    auto fresh = sim::read_game(damaged, buildings);
    REQUIRE(fresh.ok());
    sim::Simulation started(*fresh, buildings, figures);
    CHECK(started.state_damaged());
}

namespace {

core::Bytes stored_save(int version) {
    const std::filesystem::path path =
        std::filesystem::path(O1602_TEST_SAVES) / ("simu-v" + std::to_string(version) + ".o1602");
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return core::Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// Where the part `name` of the running game's state starts in a savegame written unpacked, or 0.
std::size_t part_at(const core::Bytes& bytes, std::string_view name) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const std::size_t simu = text.find("SIMU", 32);
    const std::size_t at = simu == std::string_view::npos ? simu : text.find(name, simu);
    return at == std::string_view::npos ? 0 : at;
}

/// The checksum of an unpacked savegame made right after its sections were changed.
void fix_crc(core::Bytes& bytes) {
    constexpr std::size_t kHeader = 32;
    const std::uint32_t crc = core::crc32(std::span(bytes).subspan(kHeader));
    for (std::size_t b = 0; b < 4; ++b) {
        bytes[24 + b] = static_cast<std::uint8_t>(crc >> (8 * b));
    }
}

}  // namespace

TEST_CASE("save game: games of every earlier state version load and run on as they did (DS-3)") {
    if (!core::has_zlib()) {
        return;  // the stored games are packed
    }
    // tests/unit/saves: the same small game saved by Open1602 with SIMU state versions 1 to 6 (the
    // last before the parts), and the world after 200 more ticks as those versions ran it. Versions
    // 1 to 3 had no state of the computers, so these start them anew.
    const auto buildings = make_buildings();
    const assets::FigureTable figures;
    for (int version = 1; version <= 6; ++version) {
        const core::Bytes bytes = stored_save(version);
        auto loaded = sim::read_game(bytes, buildings, nullptr, &figures);
        REQUIRE(loaded.ok());
        // Saved again before it runs: the state goes through as it came and still loads.
        auto again = sim::write_game(*loaded, buildings, nullptr, sim::SaveOptions{false});
        REQUIRE(again.ok());
        REQUIRE(sim::read_game(*again, buildings, nullptr, &figures).ok());

        sim::Simulation simulation(*loaded, buildings, figures);
        CHECK(!simulation.state_damaged());
        CHECK(simulation.speed() == (version >= 6 ? 3U : 1U));  // version 6 kept the speed
        for (int t = 0; t < 200; ++t) {
            simulation.tick();
        }
        test::check_golden(("state version " + std::to_string(version)).c_str(), test::world_hash(*loaded, buildings),
                           version <= 3 ? 0xdb627ed88c1fb63eULL : 0x55b5de3354ec1ceeULL);

        // Saved in parts, it comes back with the same game.
        auto parts = sim::write_game(*loaded, buildings, &simulation, sim::SaveOptions{false});
        REQUIRE(parts.ok());
        CHECK(part_at(*parts, "AIST") != 0);
        auto back = sim::read_game(*parts, buildings, nullptr, &figures);
        REQUIRE(back.ok());
        sim::Simulation resumed(*back, buildings, figures);
        CHECK(!resumed.state_damaged());
        CHECK(resumed.state_hash() == simulation.state_hash());
    }
}

TEST_CASE("save game: a part of the state from a later Open1602 is skipped if optional, else refused (DS-3)") {
    const auto buildings = make_buildings();
    const assets::FigureTable figures;
    sim::World world = make_world(buildings);
    sim::Simulation simulation(world, buildings, figures);
    for (int t = 0; t < 20; ++t) {
        simulation.tick();
    }
    auto bytes = sim::write_game(world, buildings, &simulation, sim::SaveOptions{false});
    REQUIRE(bytes.ok());

    // The computers' plans of a later version (part header: tag, then the u16 version): the game
    // loads, and the computers look at the world anew.
    core::Bytes later_ai = *bytes;
    const std::size_t ai = part_at(later_ai, "AIST");
    REQUIRE(ai != 0);
    later_ai[ai + 4] = 0x63;
    later_ai[ai + 5] = 0;
    fix_crc(later_ai);
    auto loaded = sim::read_game(later_ai, buildings, nullptr, &figures);
    REQUIRE(loaded.ok());
    sim::Simulation started(*loaded, buildings, figures);
    CHECK(!started.state_damaged());

    // The figures of a later version are required: the file is refused and says why.
    core::Bytes later_walkers = *bytes;
    const std::size_t walkers = part_at(later_walkers, "LOGI");
    REQUIRE(walkers != 0);
    later_walkers[walkers + 4] = 0x63;
    later_walkers[walkers + 5] = 0;
    fix_crc(later_walkers);
    auto refused = sim::read_game(later_walkers, buildings, nullptr, &figures);
    REQUIRE(!refused.ok());
    CHECK(refused.error().message.find("LOGI (version 99) needs a newer Open1602") != std::string::npos);

    // A part's body that holds more than its reader takes is damage, not something to pass over.
    core::Bytes longer = *bytes;
    const std::size_t military = part_at(longer, "MILI");
    REQUIRE(military != 0);
    longer[military + 4] = 2;  // version 2 reads two of the three clocks
    longer[military + 5] = 0;
    fix_crc(longer);
    auto damaged = sim::read_game(longer, buildings, nullptr, &figures);
    REQUIRE(!damaged.ok());
    CHECK(damaged.error().message.find("MILI: 4 bytes not read") != std::string::npos);
}

TEST_CASE("save game: a scenario's own settings come back, and the head of the file has them") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    sim::ScenarioInfo info;
    info.names = {{"en", "Hard Times"}};
    info.briefings = {{"en", std::string(12'000, 'x')}};  // long texts come after the settings
    sim::Rules rules = sim::Rules::original();
    rules.max_army = 123;
    info.rules = rules;
    world.set_scenario_info(info);
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto head = sim::save_info(std::span<const std::uint8_t>(*bytes).first(std::min(bytes->size(), sim::kSaveInfoHead)),
                               bytes->size());
    REQUIRE(head.ok());
    if (head->meta) {  // a build without zlib packs nothing and reads META anyway
        REQUIRE(head->meta->scenario.has_value());
        REQUIRE(head->meta->scenario->rules.has_value());
        CHECK(head->meta->scenario->rules->max_army == 123);
        CHECK(head->meta->scenario->names.front().text == "Hard Times");
    }
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    REQUIRE(loaded->scenario_info().has_value());
    CHECK(loaded->scenario_info()->rules == info.rules);
    CHECK(!loaded->saved_rules().has_value());  // not the settings of a game played

    // Without them the file has none.
    info.rules.reset();
    world.set_scenario_info(info);
    bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    CHECK(!loaded->scenario_info()->rules.has_value());
}
