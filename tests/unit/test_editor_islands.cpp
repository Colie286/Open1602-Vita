// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's islands (ADR 0013): new ones, moved, turned, copied and taken out, each one step
// that undo takes back to the same bytes.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/editor/document.hpp"
#include "o1602/editor/islands.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kSea = 21201;
constexpr std::uint32_t kHall = 20701;  // 3 x 2 tiles

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20701\n  Kind: 14\n  Gfx: 10\n  Size: 3, 2\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of w x h tiles: ground with a ring of sea, numbered `number`, at (x, y).
sim::WorldIsland make_island(const assets::BuildingTable& buildings, std::uint32_t w, std::uint32_t h,
                             std::uint16_t number, std::uint32_t x, std::uint32_t y) {
    sim::IslandMap map(w, h);
    for (std::uint32_t ty = 0; ty < h; ++ty) {
        for (std::uint32_t tx = 0; tx < w; ++tx) {
            const bool edge = tx == 0 || ty == 0 || tx + 1 == w || ty + 1 == h;
            REQUIRE(map.place(*buildings.by_id(edge ? kSea : kGround), static_cast<std::int32_t>(tx),
                              static_cast<std::int32_t>(ty), 0, 0)
                        .ok());
        }
    }
    map.capture_ground();
    assets::IslandHeader header;
    header.number = number;
    header.width = static_cast<std::uint16_t>(w);
    header.height = static_cast<std::uint16_t>(h);
    header.pos_x = static_cast<std::uint16_t>(x);
    header.pos_y = static_cast<std::uint16_t>(y);
    return sim::WorldIsland{header, std::move(map), x, y};
}

/// The original's map with two islands: 10 x 8 at (40, 30) and 12 x 10 at (100, 60).
sim::World make_world(const assets::BuildingTable& buildings) {
    sim::World world(500, 350);
    REQUIRE(world.add_island(make_island(buildings, 10, 8, 0, 40, 30), &buildings).ok());
    REQUIRE(world.add_island(make_island(buildings, 12, 10, 1, 100, 60), &buildings).ok());
    return world;
}

bool same_tile(const sim::MapCell& a, const sim::MapCell& b) {
    return a.sprite == b.sprite && a.rotation == b.rotation;
}

}  // namespace

TEST_CASE("editor islands: a move is one step, and undo gives the same bytes") {
    const auto buildings = make_buildings();
    editor::Document document(make_world(buildings));
    const auto before = document.save(buildings);
    REQUIRE(before.ok());

    auto op = editor::move_island(document.world(), 0, 200, 150, buildings);
    REQUIRE(op.ok());
    document.perform(std::move(op).value());
    const sim::World& world = document.world();
    CHECK(world.islands()[0].x == 200);
    CHECK(world.islands()[0].y == 150);
    CHECK(world.islands()[0].header.pos_x == 200);
    CHECK(world.cell(205, 154).island == 0);
    CHECK(world.cell(205, 154).local_x == 5);
    CHECK(world.cell(45, 34).island == sim::World::kSea);  // the old place is open sea
    CHECK(sim::validate(world).ok());
    CHECK(document.history().next_undo() == "island_move");

    CHECK(document.undo());
    auto after = document.save(buildings);
    REQUIRE(after.ok());
    CHECK(*after == *before);
}

TEST_CASE("editor islands: a move onto another island or off the map is refused") {
    const auto buildings = make_buildings();
    const sim::World world = make_world(buildings);
    CHECK(!editor::move_island(world, 0, 104, 63, buildings).ok());  // land on land
    CHECK(!editor::move_island(world, 0, 495, 10, buildings).ok());  // off the map
    CHECK(!editor::move_island(world, 7, 10, 10, buildings).ok());   // no such island
    // The rings of sea may overlap, as in the original.
    CHECK(editor::move_island(world, 0, 91, 60, buildings).ok());
    CHECK(editor::free_place(world, 200, 200, 10, 8));
    CHECK(!editor::free_place(world, 95, 55, 10, 8));
    CHECK(editor::free_place(world, 95, 55, 10, 8, std::size_t{1}));  // the island being moved
    CHECK(!editor::free_place(world, -1, 0, 10, 8));
}

TEST_CASE("editor islands: a turn moves every tile a quarter clockwise, four turns give it back") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    // A hall of 3 x 2 tiles at (2, 3) and a deposit in it, a deer and a growing tree.
    REQUIRE(world.island(0).map.place(*buildings.by_id(kHall), 2, 3, 0, 0).ok());
    world.island(0).header.deposits.push_back({2, 2, 3, 0, 0, 100});
    world.add_animal(sim::Animal{0, 6, 2, 1});
    sim::GrowthState growth;
    growth.entries.push_back(sim::GrowthEntry{0, 7, 5, 1, 0, 0});
    world.growth() = growth;
    const sim::World start = world;

    auto op = editor::rotate_island(world, 0, buildings);
    REQUIRE(op.ok());
    (*op)->apply(world);
    const sim::WorldIsland& turned = world.islands()[0];
    REQUIRE(turned.map.width() == 8);
    REQUIRE(turned.map.height() == 10);
    CHECK(turned.header.width == 8);
    CHECK(turned.header.height == 10);
    // (x, y) goes to (h - 1 - y, x) with h = 8.
    CHECK(turned.map.top(7, 0).sprite == start.islands()[0].map.top(0, 0).sprite);
    const sim::SubTile hall = turned.map.sub_tile(buildings, 3, 3);
    REQUIRE(hall.building != nullptr);
    CHECK(hall.building->id == kHall);
    // The hall took (2..4, 3..4); turned it takes (8 - 1 - 4 .. 8 - 1 - 3, 2..4) = (3..4, 2..4).
    CHECK(hall.origin_x == 3);
    CHECK(hall.origin_y == 2);
    CHECK(turned.map.top(3, 2).rotation == 1);
    CHECK(turned.header.deposits[0].x == 3);
    CHECK(turned.header.deposits[0].y == 2);
    CHECK(world.animals()[0].x == 5);  // (6, 2) -> (8 - 1 - 2, 6)
    CHECK(world.animals()[0].y == 6);
    CHECK(world.growth().entries[0].x == 2);  // (7, 5) -> (8 - 1 - 5, 7)
    CHECK(world.growth().entries[0].y == 7);
    // About the same centre: 10 x 8 at (40, 30) becomes 8 x 10 at (41, 29).
    CHECK(turned.x == 41);
    CHECK(turned.y == 29);
    CHECK(sim::validate(world).ok());

    for (int i = 0; i < 3; ++i) {
        auto again = editor::rotate_island(world, 0, buildings);
        REQUIRE(again.ok());
        (*again)->apply(world);
    }
    const sim::WorldIsland& back = world.islands()[0];
    REQUIRE(back.map.width() == 10);
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 10; ++x) {
            CHECK(same_tile(back.map.top(x, y), start.islands()[0].map.top(x, y)));
            CHECK(same_tile(back.map.ground(x, y), start.islands()[0].map.ground(x, y)));
        }
    }
    CHECK(back.header.deposits[0].x == 2);
    CHECK(back.header.deposits[0].y == 3);
    CHECK(world.animals()[0].x == 6);
    CHECK(world.growth().entries[0].y == 5);
    CHECK(back.x == 40);
    CHECK(back.y == 30);
}

TEST_CASE("editor islands: a copy gets a free number and the island's animals, nobody knows it") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    world.add_animal(sim::Animal{0, 3, 3, 1});
    world.island(0).discover(0);
    auto op = editor::copy_island(world, 0, 300, 200, buildings);
    REQUIRE(op.ok());
    (*op)->apply(world);
    REQUIRE(world.islands().size() == 3);
    const sim::WorldIsland& copy = world.islands()[2];
    CHECK(copy.header.number == 2);
    CHECK(copy.x == 300);
    CHECK(!copy.discovered_by(0));
    CHECK(world.cell(305, 204).island == 2);
    REQUIRE(world.animals().size() == 2);
    CHECK(world.animals()[1].island == 2);
    CHECK(sim::validate(world).ok());
    (*op)->revert(world);
    CHECK(world.islands().size() == 2);
    CHECK(world.cell(305, 204).island == sim::World::kSea);
}

TEST_CASE("editor islands: taking one out moves the later ones forward with what names them") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    world.set_players({sim::Player{sim::PlayerRole::Human, 0, 0, "Merlin"}});
    sim::City city;
    city.island = 1;
    city.index = 0;
    city.owner = 0;
    city.name = "Harbour";
    world.add_city(city);
    world.add_animal(sim::Animal{0, 3, 3, 1});
    world.add_animal(sim::Animal{1, 4, 4, 1});

    // An island with a city stays.
    auto refused = editor::remove_island(world, 1, buildings);
    CHECK(!refused.ok());
    CHECK(world.island_use(1) == sim::World::IslandUse::Cities);

    auto op = editor::remove_island(world, 0, buildings);
    REQUIRE(op.ok());
    (*op)->apply(world);
    REQUIRE(world.islands().size() == 1);
    CHECK(world.islands()[0].header.number == 1);
    CHECK(world.cell(105, 65).island == 0);
    CHECK(world.cell(45, 34).island == sim::World::kSea);
    REQUIRE(world.cities().size() == 1);
    CHECK(world.cities()[0].island == 0);
    CHECK(world.city(0, 0) != nullptr);
    REQUIRE(world.animals().size() == 1);  // the deer of the island went with it
    CHECK(world.animals()[0].island == 0);
    CHECK(sim::validate(world).ok());

    (*op)->revert(world);
    CHECK(world.islands().size() == 2);
    CHECK(world.city(1, 0) != nullptr);
}

TEST_CASE("editor islands: a new island of its own shape, added where there is room") {
    const auto buildings = make_buildings();
    editor::Document document(make_world(buildings));
    editor::NewIsland request;
    request.shape = editor::NewIsland::Shape::Own;
    request.size = 0;
    request.seed = 7;
    // A template needs the original's files.
    editor::NewIsland from_template = request;
    from_template.shape = editor::NewIsland::Shape::Template;
    CHECK(!editor::make_island(from_template, 5, buildings, nullptr).ok());

    auto made = editor::make_island(request, 5, buildings, nullptr);
    REQUIRE(made.ok());
    CHECK(made->map.width() == 30);
    CHECK(made->map.height() == 30);
    CHECK(made->header.number == 5);
    CHECK(made->header.fertility != 0);
    CHECK(made->header.deposits.empty());
    // The same request makes the same island.
    auto again = editor::make_island(request, 5, buildings, nullptr);
    REQUIRE(again.ok());
    CHECK(again->map.top(15, 15).sprite == made->map.top(15, 15).sprite);

    REQUIRE(editor::free_place(document.world(), 300, 100, 30, 30));
    auto op = editor::add_island(document.world(), std::move(made).value(), 300, 100, buildings);
    REQUIRE(op.ok());
    document.perform(std::move(op).value());
    REQUIRE(document.world().islands().size() == 3);
    CHECK(document.world().islands()[2].x == 300);
    CHECK(sim::validate(document.world()).ok());
    CHECK(document.undo());
    CHECK(document.world().islands().size() == 2);

    // A number that is taken gets a free one.
    auto twin = editor::make_island(request, 1, buildings, nullptr);
    REQUIRE(twin.ok());
    auto added = editor::add_island(document.world(), std::move(twin).value(), 300, 100, buildings);
    REQUIRE(added.ok());
    document.perform(std::move(added).value());
    CHECK(document.world().islands()[2].header.number == 2);
}

TEST_CASE("editor islands: the state of a running game keeps the islands as they are") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    world.set_simulation_state({1, 2, 3});
    CHECK(world.island_use(0) == sim::World::IslandUse::RunningGame);
    CHECK(!editor::move_island(world, 0, 200, 150, buildings).ok());
    CHECK(!editor::rotate_island(world, 0, buildings).ok());
    CHECK(!editor::remove_island(world, 0, buildings).ok());
    CHECK(!editor::copy_island(world, 0, 200, 150, buildings).ok());
}

TEST_CASE("editor islands: ships at an island's coast go with it, others keep it from their place") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    sim::Ship anchored;
    anchored.id = 1;
    anchored.x = 40;  // the island's ring of sea at (40, 30)
    anchored.y = 31;
    anchored.pos_x = 40 * 4096 + 2048;
    anchored.pos_y = 31 * 4096 + 2048;
    world.ships().push_back(anchored);
    sim::Ship other;
    other.id = 2;
    other.x = 205;
    other.y = 154;
    other.pos_x = 205 * 4096;
    other.pos_y = 154 * 4096;
    world.ships().push_back(other);
    CHECK(editor::ship_in_way(world, 200, 150, 10, 8, std::size_t{0}));
    CHECK(!editor::ship_in_way(world, 300, 150, 10, 8, std::size_t{0}));
    CHECK(!editor::ship_in_way(world, 35, 25, 10, 8, std::size_t{0}));  // its own ship goes along
    CHECK(!editor::move_island(world, 0, 200, 150, buildings).ok());    // onto the other ship
    auto op = editor::move_island(world, 0, 300, 150, buildings);
    REQUIRE(op.ok());
    (*op)->apply(world);
    CHECK(world.ships()[0].x == 300);
    CHECK(world.ships()[0].y == 151);
    CHECK(world.ships()[0].pos_x == 300 * 4096 + 2048);
    CHECK(world.ships()[1].x == 205);
    (*op)->revert(world);
    CHECK(world.ships()[0].x == 40);
}

TEST_CASE("editor islands: the map grows and is cut at the right and the bottom, undo gives the same bytes") {
    const auto buildings = make_buildings();
    editor::Document document(make_world(buildings));
    const auto before = document.save(buildings);
    REQUIRE(before.ok());
    CHECK((editor::used_size(document.world()) == std::pair<std::uint32_t, std::uint32_t>{112, 70}));

    auto grow = editor::resize_world(document.world(), 700, 500, buildings);
    REQUIRE(grow.ok());
    document.perform(std::move(grow).value());
    CHECK(document.world().width() == 700);
    CHECK(document.world().height() == 500);
    CHECK(document.world().cell(105, 64).island == 1);
    CHECK(document.world().cell(650, 450).island == sim::World::kSea);
    CHECK(sim::validate(document.world()).ok());
    CHECK(document.history().next_undo() == "world_size");

    auto cut = editor::resize_world(document.world(), 112, 70, buildings);
    REQUIRE(cut.ok());
    document.perform(std::move(cut).value());
    CHECK(document.world().width() == 112);
    CHECK(document.world().cell(111, 69).island == 1);
    CHECK(sim::validate(document.world()).ok());
    auto saved = document.save(buildings);
    REQUIRE(saved.ok());

    CHECK(document.undo());
    CHECK(document.undo());
    auto after = document.save(buildings);
    REQUIRE(after.ok());
    CHECK(*after == *before);
}

TEST_CASE("editor islands: the map is not cut below what it holds") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    CHECK(!editor::resize_world(world, 111, 70, buildings).ok());  // the second island
    CHECK(!editor::resize_world(world, 112, 69, buildings).ok());
    CHECK(!editor::resize_world(world, 500, 350, buildings).ok());  // the size it has
    CHECK(!editor::resize_world(world, 0, 350, buildings).ok());
    CHECK(!editor::resize_world(world, sim::World::kMaxSide + 1, 350, buildings).ok());
    sim::Ship ship;
    ship.id = 1;
    ship.x = 300;
    ship.y = 200;
    ship.pos_x = 300 * 4096;
    ship.pos_y = 200 * 4096;
    world.ships().push_back(ship);
    CHECK((editor::used_size(world) == std::pair<std::uint32_t, std::uint32_t>{301, 201}));
    CHECK(!editor::resize_world(world, 300, 350, buildings).ok());
    CHECK(editor::resize_world(world, 301, 201, buildings).ok());
    world.set_simulation_state({1, 2, 3});
    CHECK(!editor::resize_world(world, 400, 300, buildings).ok());
}

TEST_CASE("editor islands: the crops an island grows are one step") {
    const auto buildings = make_buildings();
    editor::Document document(make_world(buildings));
    const std::uint32_t before = document.world().islands()[1].header.fertility;
    auto op = editor::set_fertility(document.world(), 1, before | 1U << 1U);  // tobacco
    REQUIRE(op.ok());
    document.perform(std::move(op).value());
    CHECK(document.world().islands()[1].header.fertility == (before | 1U << 1U));
    CHECK(document.history().next_undo() == "island_fertility");
    CHECK(!editor::set_fertility(document.world(), 1, before | 1U << 1U).ok());  // nothing changes
    CHECK(!editor::set_fertility(document.world(), 5, 0).ok());
    CHECK(document.undo());
    CHECK(document.world().islands()[1].header.fertility == before);
}

TEST_CASE("editor islands: an island saved alone comes back with its tiles, without who knew it") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    world.island(1).header.fertility = 0x7E;
    world.island(1).header.discovered = 3;
    auto bytes = editor::save_island(world, 1, buildings);
    REQUIRE(bytes.ok());
    auto island = editor::load_island(*bytes, buildings, nullptr);
    REQUIRE(island.ok());
    CHECK(island->map.width() == 12);
    CHECK(island->map.height() == 10);
    CHECK(island->x == 0);
    CHECK(island->header.fertility == 0x7E);
    CHECK(island->header.discovered == 0);
    for (std::uint32_t y = 0; y < 10; ++y) {
        for (std::uint32_t x = 0; x < 12; ++x) {
            CHECK(same_tile(island->map.top(x, y), world.islands()[1].map.top(x, y)));
        }
    }
    auto op = editor::add_island(world, std::move(island).value(), 200, 150, buildings);
    REQUIRE(op.ok());
    (*op)->apply(world);
    CHECK(world.islands().size() == 3);
    CHECK(world.islands()[2].header.number == 2);  // 1 is taken
    CHECK(sim::validate(world).ok());
    CHECK(!editor::load_island(std::vector<std::uint8_t>{1, 2, 3}, buildings, nullptr).ok());
    world.set_simulation_state({1});
    CHECK(!editor::save_island(world, 1, buildings).ok());
}

TEST_CASE("editor islands: placeholders of the world plan are set, changed and taken out, each one step") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    CHECK(!editor::add_placeholder(world, 0, 2, 200, 100).ok());  // no plan
    sim::PlannedIslands plan;
    assets::ScenarioPlan::Island stored;  // island 1 is in the world: not made again, no placeholder
    stored.number = 1;
    stored.x = 100;
    stored.y = 60;
    plan.plan.islands.push_back(stored);
    world.set_planned_islands(plan);
    CHECK(!editor::placeholder_open(world, 0));
    CHECK(!editor::placeholder_at(world, 101, 61).has_value());

    editor::Document document(std::move(world));
    auto op = editor::add_placeholder(document.world(), 1, 2, 200, 100);
    REQUIRE(op.ok());
    document.perform(std::move(op).value());
    const auto& islands = document.world().planned_islands()->plan.islands;
    REQUIRE(islands.size() == 2);
    CHECK(islands[1].number == 2);  // 0 and 1 are taken
    CHECK(islands[1].climate == 1);
    CHECK(islands[1].template_number == assets::ScenarioPlan::kRandomTemplate);
    CHECK(document.history().next_undo() == "placeholder_new");
    CHECK(editor::placeholder_at(document.world(), 249, 151) == std::optional<std::size_t>{1});  // 50 x 52
    CHECK(!editor::placeholder_at(document.world(), 250, 100).has_value());
    CHECK(!editor::free_place(document.world(), 210, 110, 10, 8));  // no island on a placeholder
    CHECK((editor::used_size(document.world()) == std::pair<std::uint32_t, std::uint32_t>{250, 152}));

    // Not on an island, not on another placeholder, not off the map.
    CHECK(!editor::add_placeholder(document.world(), 0, 0, 95, 55).ok());
    CHECK(!editor::add_placeholder(document.world(), 0, 0, 230, 120).ok());
    CHECK(!editor::add_placeholder(document.world(), 0, 4, 450, 300).ok());

    assets::ScenarioPlan::Island changed = islands[1];
    changed.size = 4;
    changed.climate = 2;
    changed.x = 300;
    auto change = editor::change_placeholder(document.world(), 1, changed);
    REQUIRE(change.ok());
    document.perform(std::move(change).value());
    CHECK(document.world().planned_islands()->plan.islands[1].size == 4);
    CHECK(document.world().planned_islands()->plan.islands[1].x == 300);
    CHECK(!editor::change_placeholder(document.world(), 0, stored).ok());  // a stored island

    auto remove = editor::remove_placeholder(document.world(), 1);
    REQUIRE(remove.ok());
    document.perform(std::move(remove).value());
    CHECK(document.world().planned_islands()->plan.islands.size() == 1);
    CHECK(document.undo());
    CHECK(document.undo());
    CHECK(document.world().planned_islands()->plan.islands[1].x == 200);
    CHECK(document.undo());
    CHECK(document.world().planned_islands()->plan.islands.size() == 1);
}

TEST_CASE("editor islands: a package's island template and climate for a new island") {
    const auto buildings = make_buildings();
    // A package's island file: 20 x 18 tiles of ground, of the south.
    std::vector<std::uint8_t> header(116, 0);
    header[1] = 20;
    header[2] = 18;
    header[0x60] = 0xFF;
    header[0x61] = 0xFF;
    header[0x64] = 1;
    header[0x65] = 1;
    std::vector<std::uint8_t> tiles;
    for (std::uint8_t y = 0; y < 18; ++y) {
        for (std::uint8_t x = 0; x < 20; ++x) {
            tiles.insert(tiles.end(), {kGround - 20000, 0, x, y});
            test::put_u32(tiles, (7U << 14) | (7U << 22));  // terrain of nobody
        }
    }
    std::vector<std::uint8_t> bytes;
    test::put_chunk(bytes, "INSEL5", header);
    test::put_chunk(bytes, "INSELHAUS", tiles);
    const std::filesystem::path file = test::temp_path("o1602_editor_reef.scp");
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    sim::PackageIsland reef;
    reef.id = "atoll:reef";
    reef.package = "atoll";
    reef.file = file;
    reef.climate = 1;
    reef.size = 0;
    reef.width = 20;
    reef.height = 18;
    const std::vector<sim::PackageIsland> islands{reef};

    editor::NewIsland request;
    request.climate = 1;
    request.own_climate = 2;
    request.size = 0;
    request.package_islands = islands;
    // The package's comes after the original's templates of its climate and class; those need the
    // original's files.
    request.seed = sim::package_template_number(islands, 0);
    auto made = editor::make_island(request, 4, buildings, nullptr);
    REQUIRE(made.ok());
    CHECK(made->map.width() == 20);
    CHECK(made->map.height() == 18);
    CHECK(made->header.number == 4);
    CHECK(made->header.climate == 1);
    CHECK(made->header.template_number == assets::IslandHeader::kNoTemplate);
    CHECK(made->climate == 2);
    CHECK(made->map.top(5, 5).sprite == buildings.by_id(kGround)->gfx);
    request.seed = 0;
    CHECK(!editor::make_island(request, 4, buildings, nullptr).ok());
    // Of the north there is none of the package: only the original's.
    request.climate = 0;
    request.seed = sim::package_template_number(islands, 0);
    CHECK(!editor::make_island(request, 4, buildings, nullptr).ok());
    std::filesystem::remove(file);
}
