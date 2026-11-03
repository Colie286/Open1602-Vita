// SPDX-License-Identifier: GPL-3.0-or-later
// Climates of a package's own (item 9, docs/modding/climates.md): climates.toml, the crops of their
// islands and the section CLIM that keeps them with the game.

#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/climates.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t bit(std::uint8_t crop) {
    return 1U << (crop - sim::ware::kCornField);
}

std::vector<sim::Climate> read(std::string_view text, std::vector<std::string>& warnings) {
    std::vector<sim::Climate> climates;
    sim::apply_climates_file(climates, "mod", text, warnings);
    return climates;
}

/// An island of the north with sugar cane's bit.
sim::WorldIsland north_island() {
    assets::IslandHeader header;
    header.climate = sim::climate::kNorth;
    header.fertility = bit(sim::ware::kSugarField);
    return sim::WorldIsland{header, sim::IslandMap(8, 8), 10, 10};
}

}  // namespace

TEST_CASE("climates: a package names climates like the north or the south, with their crops and trees") {
    std::vector<std::string> warnings;
    const auto climates = read(
        "[tundra]\n"
        "like = \"north\"\n"
        "name = \"Tundra\"\n"
        "name.de = \"Tundra (de)\"\n"
        "crops = \"tobacco, cocoa\"\n"
        "forest = \"anno1602:mixed_forest, anno1602:21306\"\n"
        "ground = \"anno1602:22722, anno1602:22724\"\n"
        "share = 30\n"
        "[jungle]\n"
        "like = \"south\"\n",
        warnings);
    CHECK(warnings.empty());
    REQUIRE(climates.size() == 2);
    const sim::Climate& tundra = climates[0];
    CHECK(tundra.id == "mod:tundra");
    CHECK(tundra.like == sim::climate::kNorth);
    CHECK(tundra.crops == (bit(sim::ware::kTobaccoField) | bit(sim::ware::kCocoaField)));
    CHECK((tundra.forest == std::vector<std::uint32_t>{21304, 21306}));
    CHECK((tundra.ground == std::vector<std::uint32_t>{22722, 22724}));
    CHECK(tundra.share == 30);
    CHECK(tundra.name("de") == "Tundra (de)");
    CHECK(tundra.name("fr") == "Tundra");
    // Without its own fields: the like's crops, no forest of its own, no share, the id as its name.
    const sim::Climate& jungle = climates[1];
    CHECK(jungle.crops == sim::original_climate_crops(sim::climate::kSouth));
    CHECK(jungle.forest.empty());
    CHECK(jungle.ground.empty());
    CHECK(jungle.share == 0);
    CHECK(jungle.name("en") == "mod:jungle");
    CHECK(sim::valid(climates));
}

TEST_CASE("climates: the original's crops by climate, the crops by name") {
    CHECK(sim::original_climate_crops(sim::climate::kNorth) ==
          (bit(sim::ware::kTobaccoField) | bit(sim::ware::kSugarField) | bit(sim::ware::kWineField)));
    CHECK(sim::original_climate_crops(sim::climate::kSouth) ==
          (bit(sim::ware::kSpiceField) | bit(sim::ware::kCottonField) | bit(sim::ware::kCocoaField)));
    CHECK(sim::original_climate_crops(2) == 0);  // the original's other numbers grow nothing half
    CHECK(sim::crop_bit("wine") == bit(sim::ware::kWineField));
    CHECK(!sim::crop_bit("grain").has_value());
}

TEST_CASE("climates: what cannot be read is passed over with the file and the line") {
    std::vector<std::string> warnings;
    const auto climates = read(
        "[Bad]\nlike = \"north\"\n"    // a name with a capital letter
        "[nowhere]\nshare = 10\n"      // no like
        "[cold]\nlike = \"arctic\"\n"  // no like of the original's
        "[dry]\nlike = \"south\"\n"
        "crops = \"cocoa, rice\"\n"        // rice is no crop
        "share = 150\n"                    // more than 100
        "forest = \"anno1602:nothing\"\n"  // no building
        "colour = 3\n",                    // no field
        warnings);
    CHECK(warnings.size() == 7);
    for (const std::string& w : warnings) {
        CHECK(w.starts_with("mod/climates.toml line "));
    }
    REQUIRE(climates.size() == 1);
    CHECK(climates[0].id == "mod:dry");
    CHECK(climates[0].crops == bit(sim::ware::kCocoaField));
    CHECK(climates[0].share == 0);
    CHECK(climates[0].forest.empty());
    // At most kMaxClimates in all.
    std::string many;
    for (std::size_t i = 0; i < sim::kMaxClimates + 2; ++i) {
        many += "[c" + std::to_string(i) + "]\nlike = \"south\"\n";
    }
    warnings.clear();
    CHECK(read(many, warnings).size() == sim::kMaxClimates);
    CHECK(warnings.size() == 1);
}

TEST_CASE("climates: an island of a package's climate grows its crops half, its own bits fully") {
    std::vector<std::string> warnings;
    const auto climates = read("[tundra]\nlike = \"north\"\ncrops = \"tobacco, cocoa\"\n", warnings);
    sim::WorldIsland island = north_island();
    // The original's north: tobacco and wine half, sugar cane fully, cocoa not at all.
    CHECK(sim::island_fertility(climates, island, sim::ware::kTobaccoField) == 64);
    CHECK(sim::island_fertility(climates, island, sim::ware::kWineField) == 64);
    CHECK(sim::island_fertility(climates, island, sim::ware::kSugarField) == 128);
    CHECK(sim::island_fertility(climates, island, sim::ware::kCocoaField) == 0);
    CHECK(sim::island_climate(climates, island) == nullptr);
    island.climate = 1;
    CHECK(sim::island_climate(climates, island) == &climates[0]);
    CHECK(sim::island_fertility(climates, island, sim::ware::kTobaccoField) == 64);
    CHECK(sim::island_fertility(climates, island, sim::ware::kWineField) == 0);
    CHECK(sim::island_fertility(climates, island, sim::ware::kSugarField) == 128);  // its bit
    CHECK(sim::island_fertility(climates, island, sim::ware::kCocoaField) == 64);
    // A climate the game does not have: the header's.
    island.climate = 2;
    CHECK(sim::island_climate(climates, island) == nullptr);
    CHECK(sim::island_fertility(climates, island, sim::ware::kWineField) == 64);
}

TEST_CASE("climates: a game keeps its climates and its islands' in the savegame (CLIM)") {
    const assets::BuildingTable buildings;
    sim::World world(60, 40);
    REQUIRE(world.add_island(north_island()).ok());
    sim::WorldIsland second = north_island();
    second.header.number = 1;
    second.x = 30;
    REQUIRE(world.add_island(std::move(second)).ok());
    std::vector<std::string> warnings;
    sim::Balance balance;
    balance.climates = read(
        "[tundra]\nlike = \"north\"\nname.de = \"Tundra\"\ncrops = \"wine\"\nforest = \"anno1602:21304\"\nshare = 40\n",
        warnings);
    // Without ground CLIM stays version 1; the second save below has ground (version 2).
    world.set_balance(balance);
    world.island(1).climate = 1;
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings);
    REQUIRE(loaded.ok());
    CHECK(loaded->balance() == balance);
    CHECK(loaded->islands()[0].climate == 0);
    CHECK(loaded->islands()[1].climate == 1);
    CHECK(sim::island_fertility(*loaded, loaded->islands()[1], sim::ware::kWineField) == 64);
    balance.climates[0].ground = {22722};
    world.set_balance(balance);
    auto with_ground = sim::write_game(world, buildings);
    REQUIRE(with_ground.ok());
    auto loaded_ground = sim::read_game(*with_ground, buildings);
    REQUIRE(loaded_ground.ok());
    CHECK(loaded_ground->balance() == balance);

    // An island of a climate the game does not have is a damaged file.
    world.island(0).climate = 2;
    auto damaged = sim::write_game(world, buildings);
    REQUIRE(damaged.ok());
    CHECK(!sim::read_game(*damaged, buildings).ok());
}
