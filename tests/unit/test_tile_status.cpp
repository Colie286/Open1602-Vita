// SPDX-License-Identifier: GPL-3.0-or-later
// The status line over the island tile under the mouse (engine/app/src/tile_status.hpp).

#include <string>

#include "../test_framework.hpp"
#include "tile_status.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/ship.hpp"
#include "o1602/sim/soldier.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr sim::PlayerId kMe = 0;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        // The sea of a fishing ground: fished and growing back, then ripe with fish.
        "  @Nummer: 1\n  Id: 21251\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 450\n  EndObj\n"
        "  @Nummer: 2\n  Id: 21201\n  Kind: 19\n  Gfx: 2\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 57\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20201\n  Kind: 10\n  Gfx: 3\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 900\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20202\n  Kind: 10\n  Gfx: 4\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        // A withered field: the record after it is the next crop's growing one.
        "  @Nummer: 5\n  Id: 20301\n  Kind: 11\n  Gfx: 5\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 300\n  EndObj\n"
        "  @Nummer: 6\n  Id: 20302\n  Kind: 11\n  Gfx: 6\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 300\n  EndObj\n"
        // A workshop of the build menu's HANDWERK list (third entry).
        "  @Nummer: 7\n  Id: 20501\n  Kind: 14\n  Gfx: 7\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n  EndObj\n"
        // A house.
        "  @Nummer: 8\n  Id: 20901\n  Kind: 14\n  Gfx: 8\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::TextTable make_texts() {
    std::string game = "[GAME]\n";
    for (int i = 0; i < 40; ++i) {
        game += i == 22 ? "Geeignet für: %s: %d%% - %s: %d%% - %s: %d%%\n" : i == 37 ? "Verdorrtes Feld\n" : "-\n";
    }
    return assets::TextTable::parse(game +
                                    "[END]\n"
                                    "[ROHST]\nGetreide\nTabak\nGewürze\nZuckerrohr\nBaumwolle\nWeintrauben\nKakao\n"
                                    "Wiese\nBaum\nSteine\nErze\nWild\nFische\n[END]\n"
                                    "[ROHSTFELD]\nGetreidefeld\nTabakfeld\nGewürzfeld\nZuckerrohrfeld\nBaumwollfeld\n"
                                    "Weintrauben\nKakaofeld\nWiese\nWald\nSteine\nErze\nWild\nFischgründe\n[END]\n"
                                    "[HANDWERK]\nSteinmetz\nWerkzeugschmiede\nWeberei\n[END]\n"
                                    "[WOHNHAUS]\nHaus\n[END]\n");
}

/// An island of one row: ground, fished sea, sea with fish, forest, withered field, Weberei, house.
sim::WorldIsland make_island(const assets::BuildingTable& buildings) {
    sim::IslandMap map(7, 1);
    const std::uint32_t ids[] = {20101, 21251, 21201, 20202, 20301, 20501, 20901};
    for (std::int32_t x = 0; x < 7; ++x) {
        REQUIRE(map.place(*buildings.by_id(ids[static_cast<std::size_t>(x)]), x, 0, 0, 0, 0, 0).ok());
    }
    map.capture_ground();
    assets::IslandHeader header;
    header.climate = 0;
    header.fertility = 1U << (48 - 45);  // sugar cane
    sim::WorldIsland island{header, std::move(map), 0, 0};
    island.discover(kMe);
    return island;
}

struct Island {
    assets::BuildingTable buildings = make_buildings();
    assets::TextTable texts = make_texts();
    sim::WorldIsland island = make_island(buildings);

    [[nodiscard]] std::string at(std::int32_t x) const { return app::tile_status(island, x, 0, kMe, buildings, texts); }
};

}  // namespace

TEST_CASE("tile status: fishing grounds, ripe or fished, are named") {
    const Island i;
    CHECK(i.at(2) == "Fischgründe");
    CHECK(i.at(1) == "Fischgründe");  // the ripe record after the growing one
}

TEST_CASE("tile status: buildings by their menu name, houses, withered fields, bare ground") {
    const Island i;
    CHECK(i.at(5) == "Weberei");
    CHECK(i.at(6) == "Haus");
    CHECK(i.at(4) == "Verdorrtes Feld");
    CHECK(i.at(0).empty());
    CHECK(i.at(9).empty());  // off the island
}

TEST_CASE("tile status: the crops of grass and trees only once the island is explored") {
    Island i;
    CHECK(i.at(3).empty());
    i.island.explore(kMe);
    // The original names tobacco, grapes and sugar cane in this order.
    CHECK(i.at(3) == "Geeignet für: Tabak: 50% - Weintrauben: 50% - Zuckerrohr: 100%");
}

TEST_CASE("tile status: an island of a package's climate names it and the crops it grows") {
    Island i;
    i.island.explore(kMe);
    sim::Climate tundra;
    tundra.id = "mod:tundra";
    tundra.names[""] = "Tundra";
    tundra.crops = (1U << (46 - 45)) | (1U << (48 - 45)) | (1U << (51 - 45));  // tobacco, sugar cane, cocoa
    const std::vector<sim::Climate> climates = {tundra};
    i.island.climate = 1;
    CHECK(app::tile_status(i.island, 3, 0, kMe, i.buildings, i.texts, climates) ==
          "Tundra - Geeignet für: Tabak: 50% - Zuckerrohr: 100% - Kakao: 50%");
    // A climate of one crop.
    std::vector<sim::Climate> one = climates;
    one[0].crops = 1U << (47 - 45);  // spices
    CHECK(app::tile_status(i.island, 3, 0, kMe, i.buildings, i.texts, one) == "Tundra - Geeignet für: Gewürze: 50%");
    // Without the climates the island shows the original's of its header.
    CHECK(i.at(3) == "Geeignet für: Tabak: 50% - Weintrauben: 50% - Zuckerrohr: 100%");
}

TEST_CASE("tile status: an undiscovered island shows its terrain, not its buildings") {
    Island i;
    REQUIRE(i.island.map.place(*i.buildings.by_id(20501), 0, 0, 0, 0, 0, 0).ok());  // over the ground
    CHECK(i.at(0) == "Weberei");
    i.island.discovered.clear();
    CHECK(i.at(0).empty());
    CHECK(i.at(2) == "Fischgründe");
}

TEST_CASE("tile status: ships by their owner, land units by their kind") {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    auto figures = assets::FigureTable::from_cod(assets::cod::parse(
        "Objekt: FIGUR\n  Nummer: 1\n  Kind: 3\n  Gfx: 0\n  Nummer: 2\n  Kind: 1\n  Gfx: 0\nEndObj\n", types));
    REQUIRE(figures.ok());
    std::string leiste = "[LEISTE]\n";
    for (int i = 0; i < 230; ++i) {
        leiste += i == 224   ? "Schiff\n"
                  : i == 225 ? "Piratenschiff\n"
                  : i == 226 ? "Freier Händler\n"
                  : i == 227 ? "Eingeborenenkrieger\n"
                             : "-\n";
    }
    const auto texts = assets::TextTable::parse(
        leiste + "[END]\n[FIGKIND]\nUnbenützt\nInfanterist\nKavallerist\nMusketier\nKanonier\n[END]\n");

    sim::World world;
    std::vector<sim::Player> players(4);
    sim::make_neutral(players[1], sim::faction::kPirates);
    sim::make_neutral(players[2], sim::faction::kTrader);
    sim::make_neutral(players[3], sim::faction::kNatives);
    world.set_players(std::move(players));
    for (sim::PlayerId p = 0; p < 3; ++p) {
        world.add_ship(sim::make_ship("", 10, 10, 0, p));
    }
    world.add_soldier(sim::make_soldier(5, 5, 1, kMe));
    world.add_soldier(sim::make_soldier(6, 5, 2, 3));

    const auto ship = [&](std::size_t i) {
        return app::unit_status(world, ui::UnitRef{ui::UnitRef::Kind::Ship, world.ships()[i].id}, *figures, texts);
    };
    const auto soldier = [&](std::size_t i) {
        return app::unit_status(world, ui::UnitRef{ui::UnitRef::Kind::Soldier, world.soldiers()[i].id}, *figures,
                                texts);
    };
    CHECK(ship(0) == "Schiff");
    CHECK(ship(1) == "Piratenschiff");
    CHECK(ship(2) == "Freier Händler");
    CHECK(soldier(0) == "Musketier");
    CHECK(soldier(1) == "Eingeborenenkrieger");  // whatever its figure
    CHECK(app::unit_status(world, ui::UnitRef{ui::UnitRef::Kind::Ship, 999}, *figures, texts).empty());
}
