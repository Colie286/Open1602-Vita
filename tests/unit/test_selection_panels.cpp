// SPDX-License-Identifier: GPL-3.0-or-later
// The windows of what the player selects (BT-2) without the original data: a folder with the files
// an installation needs and synthetic layouts with one gadget per Id the windows know, a world with
// workshops, a chapel, a house and a Kontor of the local and another player, ships and cities.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "../test_framework.hpp"
#include "selection_panels.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/unit_selection.hpp"

using namespace o1602;
using app::PanelContext;
using app::Selection;
using app::SelectionPanel;
using app::SelectionPanels;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts = {nullptr, nullptr, nullptr};
constexpr sim::PlayerId kLocal = 0;
constexpr sim::PlayerId kOther = 1;
constexpr std::uint32_t kBakery = 20503;
constexpr std::int32_t kStop = 37009;
constexpr std::int32_t kPickup = 37010;
constexpr std::int32_t kCostValue = 37006;
constexpr std::uint32_t kChapel = 20809;  // DIVERS, second in the build menu
constexpr std::uint32_t kKontor = 22101;
constexpr std::uint32_t kHouse = 20001;
constexpr std::uint32_t kYard = 22111;
constexpr std::uint32_t kCastle = 22521;
constexpr std::int32_t kInfraIcon = 62021;

/// A layout with these gadget Ids, each a text box of its own place (`graphic` a sprite).
std::string gad(const std::vector<std::int32_t>& ids, std::int32_t graphic = 0) {
    std::string out = "Objekt: GADGET\n";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        out += "  @Nummer: " + std::to_string(i) + "\n  Id: " + std::to_string(ids[i]) +
               "\n  Kind: " + (ids[i] == graphic ? "2" : "6") +
               "\n  Pos: " + std::to_string(static_cast<int>(i % 20) * 30) + ", " +
               std::to_string(100 + static_cast<int>(i / 20) * 20) + "\n  Size: 20, 10\n";
    }
    return out + "EndObj\n";
}

void write(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

/// An installation with the PROD (37002..37041), SHIPLIST (61002..61063), STADTLST (63002..63063),
/// SIEDLER (36002..36100), WERFT (39002..39042), BAUSHIP (60002..60052), MILITAR (40002..40130),
/// STADT (43002..43030), KONTOR (34002..34081), LAGER (44002..44400), SKSHIP (51002..51100),
/// MKSHIP (50002..50040), SKSOLDAT (65002..65030), MKSOLDAT (52002..52040), VERTRAG (56001..56070),
/// TRIBUT (64001..64030), INFRA (62002..62021), HANDEL (53002..53100) and the ship window's (SHIP,
/// TRANSFER, TRADE, ROUTE, PIRAT, PIRATORD, TRADER) layouts and the GAME and
/// DIVERS texts the windows show. With `route_row`, ROUTE.GAD has the gadgets of all four stops
/// and MUSIK.GAD its list arrows: the route editor gets Open1602's row below the stops.
struct Game {
    std::filesystem::path root;
    std::optional<assets::GameDirectory> dir;
    assets::TextTable texts;

    explicit Game(const std::string& name, bool route_row = false) {
        root = test::temp_path(name);
        std::filesystem::remove_all(root);
        for (const char* file : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col", "gfx/stadtfld.bsh"}) {
            write(root / file, "");
        }
        std::vector<std::int32_t> ids;
        for (std::int32_t id = 37002; id <= 37041; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "PROD.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 61002; id <= 61063; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "SHIPLIST.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 63002; id <= 63063; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "STADTLST.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 36002; id <= 36100; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "SIEDLER.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 39002; id <= 39042; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "WERFT.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 60002; id <= 60052; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "BAUSHIP.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 40002; id <= 40130; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "MILITAR.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 43002; id <= 43030; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "STADT.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 34002; id <= 34081; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "KONTOR.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 44002; id <= 44400; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "LAGER.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 51002; id <= 51100; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "SKSHIP.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 50002; id <= 50040; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "MKSHIP.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 65002; id <= 65030; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "SKSOLDAT.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 52002; id <= 52040; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "MKSOLDAT.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 56001; id <= 56070; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "VERTRAG.GAD", gad(ids));
        ids.clear();
        for (std::int32_t id = 64001; id <= 64030; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "TRIBUT.GAD", gad(ids));
        // The ship's window and its pages.
        const auto range = [&](const char* file, std::int32_t first, std::int32_t last) {
            ids.clear();
            for (std::int32_t id = first; id <= last; ++id) {
                ids.push_back(id);
            }
            write(root / "GADDATA" / file, gad(ids));
        };
        range("SHIP.GAD", 38001, 38111);
        range("TRANSFER.GAD", 45002, 45160);
        range("TRADE.GAD", 33002, 33060);
        range("ROUTE.GAD", 58001, route_row ? 58260 : 58100);
        if (route_row) {
            range("MUSIK.GAD", 49002, 49012);
        }
        range("PIRAT.GAD", 54002, 54010);
        range("PIRATORD.GAD", 55002, 55045);
        range("TRADER.GAD", 59001, 59094);
        ids.clear();
        for (std::int32_t id = 62002; id <= 62021; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "INFRA.GAD", gad(ids, kInfraIcon));
        ids.clear();
        for (std::int32_t id = 53002; id <= 53100; ++id) {
            ids.push_back(id);
        }
        write(root / "GADDATA" / "HANDEL.GAD", gad(ids));
        auto opened = assets::GameDirectory::open(root);
        REQUIRE(opened.ok());
        dir = std::move(opened).value();
        std::string game = "[GAME]\n";
        for (int i = 0; i < 60; ++i) {
            game += i == 18   ? "Betriebskosten\n"
                    : i == 48 ? "%d%%\n"
                    : i == 49 ? "%dt\n"
                              : "Text " + std::to_string(i) + "\n";
        }
        game += "[DIVERS]\n";
        for (int i = 0; i < 16; ++i) {
            game += "Divers " + std::to_string(i) + "\n";
        }
        texts = assets::TextTable::parse(game);
    }
    ~Game() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
};

/// A bakery (HANDWERK) with running costs 25 at work and 10 at rest, a chapel with 8, a Kontor and
/// a house of the first class (tax 1.5), a small shipyard (three tiles wide, costs 30 and 12) and a
/// castle with three training places; the Kontor sells four wares and buys two.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}};
    const auto doc = assets::cod::parse(
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 50\n  Maxwohn: 2\n  Steuer: 1.5\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20503\n  Gfx: 0\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 10\n    Rohstoff: 5\n"
        "    Rohmenge: 1\n    Prodmenge: 1\n    Interval: 20\n    Kosten: 25, 10\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 1\n  Id: 20809\n  Gfx: 100\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 18\n    Kosten: 8\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 2\n  Id: 22101\n  Gfx: 200\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Maxware: 4, 2\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 3\n  Id: 20001\n  Gfx: 300\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 4\n  Id: 22111\n  Gfx: 400\n  Size: 3, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 14\n    Rohstoff: 4\n    Workstoff: 9\n    Rohmenge: 2\n"
        "    Workmenge: 1\n    Kosten: 30, 12\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 5\n  Id: 22521\n  Gfx: 500\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 15\n    Maxware: 3\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// One island with a bakery of the local player at (2, 2) and one of another player at (6, 2), a
/// chapel, a house, a shipyard, a castle and a Kontor of the local player at (9, 2), (9, 5), (1, 5),
/// (10, 6) and (4, 0) in its city 3 and a Kontor of the other at (6, 5) in its city 4 (the tests
/// add the cities they need).
struct Town {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures;
    sim::World world{40, 40};

    Town() {
        sim::IslandMap map(12, 8);
        REQUIRE(map.place(*buildings.by_id(kBakery), 2, 2, 0, 0, kLocal).ok());
        REQUIRE(map.place(*buildings.by_id(kBakery), 6, 2, 0, 0, kOther).ok());
        REQUIRE(map.place(*buildings.by_id(kChapel), 9, 2, 0, 0, kLocal, 3).ok());
        REQUIRE(map.place(*buildings.by_id(kKontor), 6, 5, 0, 0, kOther, 4).ok());
        REQUIRE(map.place(*buildings.by_id(kHouse), 9, 5, 0, 0, kLocal, 3).ok());
        REQUIRE(map.place(*buildings.by_id(kYard), 1, 5, 0, 0, kLocal, 3).ok());
        REQUIRE(map.place(*buildings.by_id(kCastle), 10, 6, 0, 0, kLocal, 3).ok());
        REQUIRE(map.place(*buildings.by_id(kKontor), 4, 0, 0, 0, kLocal, 3).ok());
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), 4, 4};
        island.header.width = 12;
        island.header.height = 8;
        REQUIRE(world.add_island(std::move(island)).ok());
        world.set_players(std::vector<sim::Player>(2));
        for (const std::uint16_t x : {std::uint16_t{2}, std::uint16_t{6}}) {
            sim::Production p;
            p.island = 0;
            p.x = x;
            p.y = 2;
            p.enabled = true;
            world.set_production(p);
        }
        sim::House house;
        house.x = 9;
        house.y = 5;
        house.city = 3;
        house.inhabitants64 = 2 * 64;
        world.add_house(house);
        sim::CostBuilding yard;
        yard.x = 1;
        yard.y = 5;
        yard.wood = 3 * 32;
        yard.cloth = 32;
        world.add_cost_building(yard);
        sim::CostBuilding castle;
        castle.kind = sim::CostBuilding::Kind::Castle;
        castle.x = 10;
        castle.y = 6;
        castle.weapons = {2 * 32, 0, 32};
        world.add_cost_building(castle);
    }
};

Selection bakery_at(std::uint16_t x) {
    Selection s;
    s.kind = Selection::Kind::Building;
    s.island = 0;
    s.x = x;
    s.y = 2;
    return s;
}

std::vector<std::string> texts(SelectionPanel& panel) {
    std::vector<std::string> out;
    for (const ui::DrawItem& d : panel.panel().draw(kNoFonts)) {
        if (d.kind == ui::DrawItem::Kind::Text && !d.text.empty()) {
            out.push_back(d.text);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("selection panels: an own workshop opens the production window with its running cost") {
    Game g("o1602-test-selection-prod");
    Town t;
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    SelectionPanel* p = panels.panel_for(bakery_at(2), c);
    REQUIRE(p != nullptr);
    p->refresh(bakery_at(2), c);
    const auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Betriebskosten") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "25") != shown.end());
    CHECK(!p->panel().selected(kStop));
    // Another player's workshop has no window.
    CHECK(panels.panel_for(bakery_at(6), c) == nullptr);
}

TEST_CASE("selection panels: the production window's buttons stop the workshop and keep the carts away") {
    Game g("o1602-test-selection-prod-click");
    Town t;
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    SelectionPanel* p = panels.panel_for(bakery_at(2), c);
    REQUIRE(p != nullptr);
    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, kStop}, bakery_at(2), c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, kPickup}, bakery_at(2), c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Pressed, kStop}, bakery_at(2), c, out);  // only clicks count
    REQUIRE(out.size() == 2);
    const auto* stop = std::get_if<sim::ProductionSwitch>(&out[0]);
    REQUIRE(stop != nullptr);
    CHECK(stop->op == sim::ProductionSwitch::Op::Enable);
    CHECK(!stop->on);
    CHECK(stop->player == kLocal);
    CHECK(stop->x == 2);
    CHECK(stop->y == 2);
    const auto* pickup = std::get_if<sim::ProductionSwitch>(&out[1]);
    REQUIRE(pickup != nullptr);
    CHECK(pickup->op == sim::ProductionSwitch::Op::Pickup);
    CHECK(!pickup->on);

    // Run by the game, the window shows the stopped workshop and its cost at rest.
    simulation.execute(out[0]);
    p->refresh(bakery_at(2), c);
    CHECK(p->panel().selected(kStop));
    const auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "10") != shown.end());
    CHECK(p->help(kCostValue, bakery_at(2), c) == std::nullopt);
}

namespace {

constexpr std::int32_t kListHeading = 61002;
constexpr std::int32_t kListPageDown = 61005;
constexpr std::int32_t kListPageUp = 61006;
constexpr std::int32_t kListName = 61031;

/// Fifteen ships of the local player named "Ship 14" down to "Ship 0", one sinking, one foreign.
void add_ships(sim::World& world) {
    for (std::uint32_t k = 0; k < 15; ++k) {
        world.add_ship(sim::make_ship("Ship " + std::to_string(14 - k), 20 + k, 2, 0, kLocal, 2));
    }
    sim::Ship sinking = sim::make_ship("Sinking", 20, 30, 0, kLocal, 2);
    sinking.sinking = true;
    world.add_ship(sinking);
    world.add_ship(sim::make_ship("Foreign", 25, 30, 0, kOther, 2));
}

}  // namespace

TEST_CASE("selection panels: the ship list shows the own ships 13 a page, ordered on a click on the heading") {
    Game g("o1602-test-selection-shiplist");
    Town t;
    add_ships(t.world);
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    Selection list;
    list.kind = Selection::Kind::ShipList;
    SelectionPanel* p = panels.panel_for(list, c);
    REQUIRE(p != nullptr);
    p->opened();
    p->refresh(list, c);
    auto shown = texts(*p);
    // The table's order: the first ship added first; neither the sinking nor the foreign ship.
    CHECK(std::find(shown.begin(), shown.end(), "Ship 14") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Ship 2") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Ship 1") == shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Sinking") == shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Foreign") == shown.end());
    CHECK(std::any_of(shown.begin(), shown.end(),
                      [](const std::string& s) { return s.find("(15)") != std::string::npos; }));

    // The next page has the last two; the wheel goes back row by row.
    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, kListPageDown}, list, c, out);
    p->refresh(list, c);
    shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Ship 1") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Ship 0") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Ship 14") == shown.end());
    CHECK(p->wheel(1));
    p->refresh(list, c);
    shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Ship 2") != shown.end());
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, kListPageUp}, list, c, out);

    // A click on the heading orders by name: "Ship 0" first, and a click on it selects that ship.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, kListHeading}, list, c, out);
    p->refresh(list, c);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, kListName}, list, c, out);
    const auto request = p->take_request();
    REQUIRE(request.has_value());
    CHECK(request->kind == SelectionPanel::Request::Kind::SelectShip);
    const sim::Ship* first = t.world.ship(request->ship);
    REQUIRE(first != nullptr);
    CHECK(first->name == "Ship 0");
    CHECK(!p->take_request().has_value());
    CHECK(out.empty());  // the list sends no commands
}

TEST_CASE("selection panels: the city list shows the own cities, not those without a trade treaty") {
    Game g("o1602-test-selection-citylist");
    Town t;
    const auto city = [&](std::uint8_t index, sim::PlayerId owner, const char* name, bool warehouse) {
        sim::City c;
        c.island = 0;
        c.index = index;
        c.owner = owner;
        c.name = name;
        if (warehouse) {
            c.warehouse_x = 3;
            c.warehouse_y = 1;
        }
        t.world.add_city(c);
    };
    city(0, kLocal, "Zeta", true);
    city(1, kLocal, "Alpha", false);
    city(2, kOther, "Other", true);
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    Selection list;
    list.kind = Selection::Kind::CityList;
    SelectionPanel* p = panels.panel_for(list, c);
    REQUIRE(p != nullptr);
    p->opened();
    p->refresh(list, c);
    const auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Zeta") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Alpha") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Other") == shown.end());

    // In the table's order Zeta comes first; a click on it goes to its warehouse on the world map.
    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 63031}, list, c, out);
    auto request = p->take_request();
    REQUIRE(request.has_value());
    CHECK(request->kind == SelectionPanel::Request::Kind::Centre);
    CHECK(request->x == 4 + 3);
    CHECK(request->y == 4 + 1);
    // Ordered by name, Alpha comes first; without a warehouse it is the island's middle.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 63002}, list, c, out);
    p->refresh(list, c);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 63031}, list, c, out);
    request = p->take_request();
    REQUIRE(request.has_value());
    CHECK(request->x == 4 + 6);
    CHECK(request->y == 4 + 4);
}

namespace {

void add_city(sim::World& world, std::uint8_t index, sim::PlayerId owner, const char* name) {
    sim::City c;
    c.island = 0;
    c.index = index;
    c.owner = owner;
    c.name = name;
    world.add_city(c);
}

Selection building(std::uint16_t x, std::uint16_t y) {
    Selection s = bakery_at(x);
    s.y = y;
    return s;
}

}  // namespace

TEST_CASE("selection panels: a public building shows its city, name, running cost and build menu icon") {
    Game g("o1602-test-selection-infra");
    Town t;
    add_city(t.world, 3, kLocal, "Haven");
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    const Selection chapel = building(9, 2);
    SelectionPanel* p = panels.panel_for(chapel, c);
    REQUIRE(p != nullptr);
    p->refresh(chapel, c);
    const auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Haven") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Divers 1") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Betriebskosten") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "8") != shown.end());
    // The icon: GFXBAUHAUS (81 without the constant) + the DIVERS icons from 71 + its place 1.
    const auto items = p->panel().draw(kNoFonts);
    CHECK(std::any_of(items.begin(), items.end(), [](const ui::DrawItem& d) {
        return d.kind == ui::DrawItem::Kind::Sprite && d.gfx == 81 + 71 + 1;
    }));
    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 62004}, chapel, c, out);
    CHECK(out.empty());
}

TEST_CASE("selection panels: a foreign Kontor opens its trade lists only with a trade treaty") {
    Game g("o1602-test-selection-handel");
    Town t;
    add_city(t.world, 4, kOther, "Elsewhere");
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    const Selection kontor = building(6, 5);
    // Foreign buildings other than Kontor and market have no window, nor has the Kontor without a
    // trade treaty.
    CHECK(panels.panel_for(bakery_at(6), c) == nullptr);
    CHECK(panels.panel_for(kontor, c) == nullptr);

    t.world.diplomacy().relation(kLocal, kOther).trade = sim::Treaty::Signed;
    t.world.diplomacy().relation(kOther, kLocal).trade = sim::Treaty::Signed;
    SelectionPanel* p = panels.panel_for(kontor, c);
    REQUIRE(p != nullptr);
    p->refresh(kontor, c);
    const auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Elsewhere") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Text 35") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Text 36") != shown.end());
}

TEST_CASE("selection panels: a house shows its class and city taxes, its buttons change the tax rate") {
    Game g("o1602-test-selection-siedler");
    Town t;
    sim::City haven;
    haven.island = 0;
    haven.index = 3;
    haven.owner = kLocal;
    haven.name = "Haven";
    haven.inhabitants = {40};
    haven.tax_rate = {128};
    t.world.add_city(haven);
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    const Selection house = building(9, 5);
    SelectionPanel* p = panels.panel_for(house, c);
    REQUIRE(p != nullptr);
    p->refresh(house, c);
    const auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Haven") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Text 3") != shown.end());  // the class's name
    CHECK(std::find(shown.begin(), shown.end(), "40") != shown.end());      // its inhabitants in the city
    // Taxes per minute: trunc(1.5 * 16 / 3) = 8, 8 * 40 * 6 / 32 = 60 at the normal rate.
    CHECK(std::find(shown.begin(), shown.end(), "60") != shown.end());

    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 36006}, house, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 36005}, house, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 36013}, house, c, out);  // no button
    REQUIRE(out.size() == 2);
    const auto* up = std::get_if<sim::CityOrder>(&out[0]);
    REQUIRE(up != nullptr);
    CHECK(up->op == sim::CityOrder::Op::TaxRate);
    CHECK(up->player == kLocal);
    CHECK(up->city == 3);
    CHECK(up->resident_class == 0);
    CHECK(up->rate == 132);
    const auto* down = std::get_if<sim::CityOrder>(&out[1]);
    REQUIRE(down != nullptr);
    CHECK(down->rate == 124);

    // Run by the game, the higher rate brings more: 60 * 132 / 128.
    simulation.execute(out[0]);
    p->refresh(house, c);
    const auto after = texts(*p);
    CHECK(std::find(after.begin(), after.end(), std::to_string(60 * 132 / 128)) != after.end());
}

TEST_CASE("selection panels: the shipyard window orders a ship of the types a small yard builds") {
    Game g("o1602-test-selection-werft");
    Town t;
    add_city(t.world, 3, kLocal, "Haven");
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    const Selection yard = building(1, 5);
    SelectionPanel* p = panels.panel_for(yard, c);
    REQUIRE(p != nullptr);
    p->refresh(yard, c);
    auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Haven") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "30") != shown.end());  // running cost at work
    CHECK(std::find(shown.begin(), shown.end(), "3t") != shown.end());  // wood in the yard
    CHECK(std::find(shown.begin(), shown.end(), "1t") != shown.end());  // cloth
    CHECK(!p->panel().selected(39018));

    // "Order" opens the type choice; a small yard has no big types: a pick of HANDEL2 falls back to
    // HANDEL1, KRIEG1 stays.
    std::vector<sim::Command> out;
    const ui::Panel* yard_page = &p->panel();
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 39016}, yard, c, out);
    CHECK(&p->panel() != yard_page);
    const auto laid_out = p->panel().layout().find(60003)->pos;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 60004}, yard, c, out);
    p->refresh(yard, c);
    CHECK(p->panel().selected(60003));
    // The small types moved to the middle: the click area with the picture.
    const auto moved = p->panel().position(60003);
    REQUIRE(moved.has_value());
    CHECK((*moved)[0] == laid_out[0] + 40);
    CHECK((*moved)[1] == laid_out[1]);
    CHECK(p->panel().layout().find(60003)->offset[0] == 0);
    CHECK(!p->panel().selected(60004));
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 60005}, yard, c, out);
    p->refresh(yard, c);
    CHECK(p->panel().selected(60005));
    shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Haven") != shown.end());
    CHECK(out.empty());
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 60022}, yard, c, out);
    REQUIRE(out.size() == 1);
    const auto* build = std::get_if<sim::ShipyardOrder>(&out[0]);
    REQUIRE(build != nullptr);
    CHECK(build->op == sim::ShipyardOrder::Op::Build);
    CHECK(build->ship_type == 2);
    CHECK(build->player == kLocal);
    CHECK(build->x == 1);
    CHECK(build->y == 5);
    CHECK(&p->panel() == yard_page);  // back on the yard's page

    // Escape leaves the type choice too; the stop button switches the yard off.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 39016}, yard, c, out);
    CHECK(p->back());
    CHECK(!p->back());
    out.clear();
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 39018}, yard, c, out);
    REQUIRE(out.size() == 1);
    const auto* stop = std::get_if<sim::ShipyardOrder>(&out[0]);
    REQUIRE(stop != nullptr);
    CHECK(stop->op == sim::ShipyardOrder::Op::Enable);
    CHECK(!stop->on);
    simulation.execute(out[0]);
    p->refresh(yard, c);
    CHECK(p->panel().selected(39018));
    shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "12") != shown.end());  // running cost at rest
}

TEST_CASE("selection panels: the castle window recruits, shows the training and sends the unit out") {
    Game g("o1602-test-selection-militar");
    Town t;
    add_city(t.world, 3, kLocal, "Haven");
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    const Selection castle = building(10, 6);
    SelectionPanel* p = panels.panel_for(castle, c);
    REQUIRE(p != nullptr);
    p->refresh(castle, c);
    auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Haven") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "2t") != shown.end());  // swords in store
    CHECK(std::find(shown.begin(), shown.end(), "1t") != shown.end());  // cannons

    // Recruiting: the first place, the unit of the owner's colour; nothing else sends a command.
    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 40022}, castle, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 40009}, castle, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Pressed, 40009}, castle, c, out);
    REQUIRE(out.size() == 1);
    const auto* recruit = std::get_if<sim::CastleOrder>(&out[0]);
    REQUIRE(recruit != nullptr);
    CHECK(recruit->op == sim::CastleOrder::Op::Recruit);
    CHECK(recruit->slot == 0);
    CHECK(recruit->player == kLocal);
    CHECK(recruit->x == 10);
    CHECK(recruit->y == 6);

    // A unit half trained in the second place: its progress in tenths; chosen, it can be given up
    // or sent to a spot picked on the map.
    sim::CastleSlot first;
    first.figure = recruit->figure;
    first.progress = 320;
    sim::CastleSlot second;
    second.figure = recruit->figure;
    second.progress = 160;
    t.world.cost_buildings()[1].slots = {first, second};
    p->refresh(castle, c);
    shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "10") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "5") != shown.end());
    out.clear();
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 40092}, castle, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 40003}, castle, c, out);
    REQUIRE(out.size() == 1);
    const auto* cancel = std::get_if<sim::CastleOrder>(&out[0]);
    REQUIRE(cancel != nullptr);
    CHECK(cancel->op == sim::CastleOrder::Op::Cancel);
    CHECK(cancel->slot == 1);
    out.clear();
    CHECK(!p->picking());
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 40004}, castle, c, out);
    CHECK(p->picking());
    CHECK(p->back());
    CHECK(!p->picking());
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 40004}, castle, c, out);
    p->picked(3, 4, castle, c, out);
    CHECK(!p->picking());
    REQUIRE(out.size() == 1);
    const auto* release = std::get_if<sim::CastleOrder>(&out[0]);
    REQUIRE(release != nullptr);
    CHECK(release->op == sim::CastleOrder::Op::Release);
    CHECK(release->slot == 1);
    CHECK(release->spot == sim::Target::spot(7, 9));

    // With Shift on the arrow (or at the pick) every trained unit goes (Open1602); the help says so.
    out.clear();
    PanelContext shifted = c;
    shifted.shift = true;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 40004}, castle, shifted, out);
    p->picked(3, 4, castle, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 40004}, castle, c, out);
    p->picked(3, 4, castle, shifted, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 40004}, castle, c, out);
    p->picked(3, 4, castle, c, out);
    REQUIRE(out.size() == 3);
    CHECK(std::get<sim::CastleOrder>(out[0]).op == sim::CastleOrder::Op::ReleaseAll);
    CHECK(std::get<sim::CastleOrder>(out[0]).spot == sim::Target::spot(7, 9));
    CHECK(std::get<sim::CastleOrder>(out[1]).op == sim::CastleOrder::Op::ReleaseAll);
    CHECK(std::get<sim::CastleOrder>(out[2]).op == sim::CastleOrder::Op::Release);
    const auto arrow = p->help(40004, castle, c);
    REQUIRE(arrow);
    CHECK(arrow->find("Shift+click") != std::string::npos);
}

TEST_CASE("selection panels: the own Kontor shows the city, edits its trade list and opens the stock") {
    Game g("o1602-test-selection-stadt");
    Town t;
    constexpr std::uint8_t kSold = sim::ware::kFirstTrade + 3;
    constexpr std::uint8_t kAdded = sim::ware::kFirstTrade + 1;
    sim::City haven;
    haven.island = 0;
    haven.index = 3;
    haven.owner = kLocal;
    haven.name = "Haven";
    haven.warehouse_x = 4;
    haven.warehouse_y = 0;
    haven.capacity = 30 * 32;
    haven.trade.resize(sim::ware::kLastTrade + 1);
    haven.trade[kSold].sell = true;
    haven.trade[kSold].sell_price = sim::base_price(kSold) / 2;
    t.world.add_city(haven);
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    Selection kontor = building(4, 0);
    kontor.serial = 1;
    SelectionPanel* p = panels.panel_for(kontor, c);
    REQUIRE(p != nullptr);
    p->refresh(kontor, c);
    auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Haven") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Text 15") != shown.end());  // inhabitants

    // The material lock is a command; the sell list a page of its own.
    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 43025}, kontor, c, out);
    REQUIRE(out.size() == 1);
    const auto* lock = std::get_if<sim::CityOrder>(&out[0]);
    REQUIRE(lock != nullptr);
    CHECK(lock->op == sim::CityOrder::Op::MaterialLock);
    CHECK(lock->on);
    CHECK(lock->city == 3);
    out.clear();
    const ui::Panel* city_page = &p->panel();
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 43003}, kontor, c, out);
    CHECK(&p->panel() != city_page);
    p->refresh(kontor, c);
    shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), std::to_string(sim::base_price(kSold) / 2)) != shown.end());

    // The price buttons and the stop button change the chosen ware.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 34023}, kontor, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 34016}, kontor, c, out);
    REQUIRE(out.size() == 2);
    const auto* up = std::get_if<sim::TradeSetting>(&out[0]);
    REQUIRE(up != nullptr);
    CHECK(up->ware == kSold);
    CHECK(up->mode == sim::TradeSetting::Mode::Sell);
    CHECK(up->price == std::optional<std::uint16_t>(static_cast<std::uint16_t>(sim::base_price(kSold) / 2 + 1)));
    const auto* stop = std::get_if<sim::TradeSetting>(&out[1]);
    REQUIRE(stop != nullptr);
    CHECK(stop->mode == sim::TradeSetting::Mode::None);
    out.clear();

    // An empty place opens the ware picker; a ware goes into the list, the picker closes.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 34072}, kontor, c, out);
    const ui::Panel* kontor_page = &p->panel();
    p->refresh(kontor, c);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 44079 + kAdded}, kontor, c, out);
    REQUIRE(out.size() == 1);
    const auto* added = std::get_if<sim::TradeSetting>(&out[0]);
    REQUIRE(added != nullptr);
    CHECK(added->ware == kAdded);
    CHECK(added->mode == sim::TradeSetting::Mode::Sell);
    CHECK(&p->panel() != kontor_page);
    out.clear();

    // The Kontor clicked again shows the stock; a good there opens its page of the goods balance.
    kontor.again = 1;
    p->refresh(kontor, c);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 44079 + kSold}, kontor, c, out);
    const auto request = p->take_request();
    REQUIRE(request.has_value());
    CHECK(request->kind == SelectionPanel::Request::Kind::Goods);
    CHECK(request->ware == kSold);
    CHECK(request->island == 0);
    CHECK(out.empty());
}

TEST_CASE("selection panels: the trade cart button asks where a cart goes, then jumps to it; its window") {
    Game g("o1602-test-selection-cart");
    Town t;
    sim::City haven;
    haven.island = 0;
    haven.index = 3;
    haven.owner = kLocal;
    haven.name = "Haven";
    haven.warehouse_x = 4;
    haven.warehouse_y = 0;
    haven.capacity = 30 * 32;
    haven.trade.resize(sim::ware::kLastTrade + 1);
    t.world.add_city(haven);
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    Selection kontor = building(4, 0);
    kontor.serial = 1;
    SelectionPanel* p = panels.panel_for(kontor, c);
    REQUIRE(p != nullptr);
    p->refresh(kontor, c);
    // No cart yet: the button waits for a click on the map, which sends one out there.
    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 43026}, kontor, c, out);
    CHECK(out.empty());
    CHECK(p->picking());
    p->picked(12, 7, kontor, c, out);
    CHECK(!p->picking());
    REQUIRE(out.size() == 1);
    const auto* request = std::get_if<sim::CartRequest>(&out[0]);
    REQUIRE(request != nullptr);
    CHECK(request->player == kLocal);
    CHECK(request->island == 0);
    CHECK(request->city == 3);
    CHECK(request->x == 12);
    CHECK(request->y == 7);
    CHECK(request->from_x == t.world.islands()[0].x + 4);  // out of the building of this window
    CHECK(request->from_y == t.world.islands()[0].y + 0);
    out.clear();
    // With a cart on the island it selects the cart and looks at it.
    sim::Ship cart = sim::make_ship("Altair", 9, 7, sim::figure::kTradeCart, kLocal);
    cart.cargo.resize(3);
    t.world.add_ship(cart);
    const std::uint32_t id = t.world.ships().back().id;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 43026}, kontor, c, out);
    CHECK(out.empty());
    CHECK(!p->picking());
    const auto jump = p->take_request();
    REQUIRE(jump.has_value());
    CHECK(jump->kind == SelectionPanel::Request::Kind::SelectShip);
    CHECK(jump->ship == id);
    CHECK(jump->x == 9.0);  // where the cart is, not the map's corner
    CHECK(jump->y == 7.0);

    // Open1602: with Shift another cart while the player has more Kontors and markets than carts
    // there (two places here: the Kontor counted twice).
    for (int i = 0; i < 2; ++i) {
        sim::Production place;
        place.x = 4;
        t.world.productions().push_back(place);
    }
    PanelContext shift = c;
    shift.shift = true;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 43026}, kontor, shift, out);
    CHECK(p->picking());
    p->picked(14, 7, kontor, shift, out);
    REQUIRE(out.size() == 1);
    CHECK(std::get<sim::CartRequest>(out[0]).another);
    out.clear();
    // With two carts each click jumps to the next, the nearest to the Kontor first.
    sim::Ship second = sim::make_ship("Beteigeuze", 6, 5, sim::figure::kTradeCart, kLocal);
    second.cargo.resize(3);
    t.world.add_ship(second);
    const std::uint32_t near = t.world.ships().back().id;
    std::vector<std::uint32_t> jumps;
    for (int i = 0; i < 3; ++i) {
        p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 43026}, kontor, c, out);
        const auto to = p->take_request();
        REQUIRE(to.has_value());
        jumps.push_back(to->ship);
    }
    CHECK((jumps == std::vector<std::uint32_t>{near, id, near}));
    // No third place: Shift jumps too.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 43026}, kontor, shift, out);
    CHECK(!p->picking());
    CHECK(p->take_request().has_value());
    CHECK(out.empty());

    // The cart's window (TRADER.GAD): standing next to the own Kontor, the exchange opens the
    // ship's transfer page; the route button switches the route like the ship's.
    Selection selected;
    selected.kind = Selection::Kind::Ship;
    selected.ship = id;
    selected.serial = 2;
    SelectionPanel* w = panels.panel_for(selected, c);
    REQUIRE(w != nullptr);
    w->refresh(selected, c);
    const ui::Panel* root = &w->panel();
    CHECK(root->layout().find(59004) != nullptr);
    w->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 59006}, selected, c, out);
    REQUIRE(out.size() == 1);
    const auto* route = std::get_if<sim::RouteEdit>(&out[0]);
    REQUIRE(route != nullptr);
    CHECK(route->op == sim::RouteEdit::Op::Start);
    out.clear();
    w->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 59004}, selected, c, out);
    w->refresh(selected, c);
    CHECK(&w->panel() != root);
    CHECK(w->panel().layout().find(45002) != nullptr);

    // "Set route": the stops are clicked on the map one after another, from the first free one;
    // a click beside a city or Escape ends it.
    selected.serial = 3;
    w->refresh(selected, c);
    out.clear();
    w->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 59005}, selected, c, out);
    CHECK(w->picking());
    out.clear();                         // the editor drops stops that are no city (none here)
    w->picked(8, 4, selected, c, out);   // Haven's Kontor
    w->picked(13, 9, selected, c, out);  // a house of Haven
    CHECK(w->picking());
    REQUIRE(out.size() == 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto* stop = std::get_if<sim::RouteEdit>(&out[i]);
        REQUIRE(stop != nullptr);
        CHECK(stop->op == sim::RouteEdit::Op::SetStop);
        CHECK(stop->ship == id);
        CHECK(stop->stop == i);
        CHECK(stop->target == sim::Target::city_of(0, 3));
    }
    CHECK(w->back());
    CHECK(!w->picking());
    w->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 59005}, selected, c, out);
    w->picked(0, 0, selected, c, out);  // the sea: no stop, the end
    CHECK(!w->picking());
}

TEST_CASE("selection panels: the military windows of one ship and of a fleet give their orders") {
    Game g("o1602-test-selection-skship");
    Town t;
    sim::Ship avenger = sim::make_ship("Avenger", 20, 30, 0, kLocal);
    avenger.cannons = 2;
    avenger.cargo = {sim::CargoSlot{sim::ware::kCannons, 2 * 32}, sim::CargoSlot{}};
    t.world.add_ship(avenger);
    t.world.add_ship(sim::make_ship("Escort", 22, 30, 0, kLocal));
    t.world.add_ship(sim::make_ship("Raider", 25, 30, 0, kOther));
    const std::uint32_t own = t.world.ships()[0].id;
    const std::uint32_t escort = t.world.ships()[1].id;
    const std::uint32_t foreign = t.world.ships()[2].id;
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    ui::UnitSelection units;
    PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    CHECK(panels.military_panel_for(c) == nullptr);  // nothing selected
    c.units = &units;
    CHECK(panels.military_panel_for(c) == nullptr);
    units.select(t.world, ui::UnitRef{ui::UnitRef::Kind::Ship, own});
    const Selection none;
    SelectionPanel* p = panels.military_panel_for(c);
    REQUIRE(p != nullptr);
    p->refresh(none, c);
    const auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Avenger") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "2") != shown.end());  // mounted cannons

    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51023}, none, c, out);  // white flag
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51011}, none, c, out);  // a cannon off
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51091}, none, c, out);  // the cannons' hold
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51092}, none, c, out);  // an empty hold
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51024}, none, c, out);  // overboard
    REQUIRE(out.size() == 4);
    const auto op = [&](std::size_t i) {
        const auto* o = std::get_if<sim::ShipCommand>(&out[i]);
        REQUIRE(o != nullptr);
        CHECK(o->ship == own);
        CHECK(o->player == kLocal);
        return o->op;
    };
    CHECK(op(0) == sim::ShipCommand::Op::WhiteFlag);
    CHECK(std::get_if<sim::ShipCommand>(&out[0])->on);
    CHECK(op(1) == sim::ShipCommand::Op::UnmountCannon);
    CHECK(op(2) == sim::ShipCommand::Op::MountCannon);
    CHECK(std::get_if<sim::ShipCommand>(&out[2])->hold == 0);
    CHECK(op(3) == sim::ShipCommand::Op::Dump);
    out.clear();

    // Shift + click on the cannons: one command that fills every free place from any hold.
    c.shift = true;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51091}, none, c, out);
    c.shift = false;
    REQUIRE(out.size() == 1);
    CHECK(op(0) == sim::ShipCommand::Op::MountCannon);
    CHECK(std::get_if<sim::ShipCommand>(&out[0])->count == sim::ShipCommand::kAllCannons);
    CHECK(std::get_if<sim::ShipCommand>(&out[0])->hold == 0xFF);
    out.clear();

    // Shift + click on the mounted cannons or the button that takes one off: all of them come off;
    // a plain click on the mounted cannons does nothing.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51012}, none, c, out);
    CHECK(out.empty());
    c.shift = true;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51012}, none, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51011}, none, c, out);
    c.shift = false;
    REQUIRE(out.size() == 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        CHECK(op(i) == sim::ShipCommand::Op::UnmountCannon);
        CHECK(std::get_if<sim::ShipCommand>(&out[i])->count == sim::ShipCommand::kAllCannons);
    }
    out.clear();

    // Patrol: the far end is the next click on the map.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51022}, none, c, out);
    CHECK(p->picking());
    p->picked(12, 9, none, c, out);
    CHECK(!p->picking());
    REQUIRE(out.size() == 1);
    const auto* patrol = std::get_if<sim::ShipCommand>(&out[0]);
    REQUIRE(patrol != nullptr);
    CHECK(patrol->op == sim::ShipCommand::Op::Patrol);
    CHECK(patrol->x == 12);
    CHECK(patrol->y == 9);
    out.clear();

    // Another player's ship: the window shows it, its buttons do nothing.
    units.select(t.world, ui::UnitRef{ui::UnitRef::Kind::Ship, foreign});
    p = panels.military_panel_for(c);
    REQUIRE(p != nullptr);
    p->refresh(none, c);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51023}, none, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 51024}, none, c, out);
    CHECK(out.empty());

    // A fleet: the white flag for every ship.
    units.select(t.world, ui::UnitRef{ui::UnitRef::Kind::Ship, own});
    units.toggle(t.world, ui::UnitRef{ui::UnitRef::Kind::Ship, escort});
    REQUIRE(units.size() == 2);
    SelectionPanel* fleet = panels.military_panel_for(c);
    REQUIRE(fleet != nullptr);
    CHECK(fleet != p);
    fleet->refresh(none, c);
    fleet->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 50022}, none, c, out);
    REQUIRE(out.size() == 2);
    for (const sim::Command& o : out) {
        const auto* flag = std::get_if<sim::ShipCommand>(&o);
        REQUIRE(flag != nullptr);
        CHECK(flag->op == sim::ShipCommand::Op::WhiteFlag);
        CHECK(flag->on);
    }
}

TEST_CASE("selection panels: the military windows of one land unit and of a group give their orders") {
    Game g("o1602-test-selection-sksoldat");
    Town t;
    t.world.add_soldier(sim::make_soldier(20, 30, 0, kLocal));
    t.world.add_soldier(sim::make_soldier(21, 30, 0, kLocal));
    t.world.add_soldier(sim::make_soldier(25, 30, 0, kOther));
    const std::uint32_t own = t.world.soldiers()[0].id;
    const std::uint32_t second = t.world.soldiers()[1].id;
    const std::uint32_t foreign = t.world.soldiers()[2].id;
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    ui::UnitSelection units;
    PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    c.units = &units;
    const Selection none;
    units.select(t.world, ui::UnitRef{ui::UnitRef::Kind::Soldier, own});
    SelectionPanel* p = panels.military_panel_for(c);
    REQUIRE(p != nullptr);
    p->refresh(none, c);

    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 65021}, none, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 65023}, none, c, out);
    REQUIRE(out.size() == 2);
    const auto* stop = std::get_if<sim::SoldierCommand>(&out[0]);
    REQUIRE(stop != nullptr);
    CHECK(stop->op == sim::SoldierCommand::Op::Stop);
    CHECK(stop->soldier == own);
    const auto* disband = std::get_if<sim::SoldierCommand>(&out[1]);
    REQUIRE(disband != nullptr);
    CHECK(disband->op == sim::SoldierCommand::Op::Disband);
    out.clear();

    // Patrol between where it stands and the tile clicked on the map.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 65022}, none, c, out);
    CHECK(p->picking());
    CHECK(p->back());
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 65022}, none, c, out);
    p->picked(12, 9, none, c, out);
    CHECK(!p->picking());
    REQUIRE(out.size() == 3);
    const auto* from = std::get_if<sim::SoldierCommand>(&out[0]);
    const auto* to = std::get_if<sim::SoldierCommand>(&out[1]);
    const auto* go = std::get_if<sim::SoldierCommand>(&out[2]);
    REQUIRE(from != nullptr);
    REQUIRE(to != nullptr);
    REQUIRE(go != nullptr);
    CHECK(from->op == sim::SoldierCommand::Op::SetWaypoint);
    CHECK(from->index == 0);
    CHECK(from->point == sim::Target::spot(40, 60));
    CHECK(to->index == 1);
    CHECK(to->point == sim::Target::tile_spot(12, 9));
    CHECK(go->op == sim::SoldierCommand::Op::Patrol);
    CHECK(go->on);
    out.clear();

    // Another player's unit takes no orders.
    units.select(t.world, ui::UnitRef{ui::UnitRef::Kind::Soldier, foreign});
    p = panels.military_panel_for(c);
    REQUIRE(p != nullptr);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 65023}, none, c, out);
    CHECK(out.empty());

    // A group: "stop all" stops each unit.
    units.select(t.world, ui::UnitRef{ui::UnitRef::Kind::Soldier, own});
    units.toggle(t.world, ui::UnitRef{ui::UnitRef::Kind::Soldier, second});
    REQUIRE(units.size() == 2);
    SelectionPanel* group = panels.military_panel_for(c);
    REQUIRE(group != nullptr);
    CHECK(group != p);
    group->refresh(none, c);
    group->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 52021}, none, c, out);
    CHECK(out.size() == 2);
}

TEST_CASE("selection panels: the diplomacy window offers and accepts treaties and pays a tribute") {
    Game g("o1602-test-selection-vertrag");
    Town t;
    std::vector<sim::Player> players(3);
    players[0].money = 150;
    players[1].role = sim::PlayerRole::Computer;
    players[1].name = "Rival";
    players[2].name = "Friend";
    t.world.set_players(players);
    t.world.diplomacy().relation(kLocal, 2).peace = sim::Treaty::Received;
    t.world.diplomacy().relation(2, kLocal).peace = sim::Treaty::Offered;
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    Selection window;
    window.kind = Selection::Kind::Diplomacy;
    SelectionPanel* p = panels.panel_for(window, c);
    REQUIRE(p != nullptr);
    p->opened();
    p->refresh(window, c);
    auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Rival") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "Friend") != shown.end());

    // A row per partner: no trade treaty with the computer yet (offer one), the human's peace
    // offer (accept it).
    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 56041}, window, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 56052}, window, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 56043}, window, c, out);  // no third partner
    REQUIRE(out.size() == 2);
    const auto* offer = std::get_if<sim::DiplomacyOrder>(&out[0]);
    REQUIRE(offer != nullptr);
    CHECK(offer->kind == sim::DiplomacyOrder::Kind::Trade);
    CHECK(offer->partner == 1);
    CHECK(offer->value == sim::Treaty::Offered);
    const auto* accept = std::get_if<sim::DiplomacyOrder>(&out[1]);
    REQUIRE(accept != nullptr);
    CHECK(accept->kind == sim::DiplomacyOrder::Kind::Peace);
    CHECK(accept->partner == 2);
    CHECK(accept->value == sim::Treaty::Signed);
    out.clear();

    // The hand opens the tribute page; 200 gold are more than the player has, so paying waits.
    const ui::Panel* treaties = &p->panel();
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 56061}, window, c, out);
    CHECK(&p->panel() != treaties);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 64023}, window, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 64023}, window, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 64023}, window, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 64022}, window, c, out);
    p->refresh(window, c);
    shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Rival") != shown.end());
    CHECK(std::find(shown.begin(), shown.end(), "200") != shown.end());
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 64008}, window, c, out);
    CHECK(out.empty());
    t.world.player(kLocal)->money = 1000;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 64008}, window, c, out);
    REQUIRE(out.size() == 1);
    const auto* tribute = std::get_if<sim::DiplomacyOrder>(&out[0]);
    REQUIRE(tribute != nullptr);
    CHECK(tribute->kind == sim::DiplomacyOrder::Kind::Tribute);
    CHECK(tribute->partner == 1);
    CHECK(tribute->gold == 200);
    p->refresh(window, c);
    shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "0") != shown.end());  // the amount starts again

    // Escape goes back to the treaties, and no further.
    CHECK(p->back());
    CHECK(&p->panel() == treaties);
    CHECK(!p->back());
}

TEST_CASE("selection panels: the ship window gives the orders of a ship at sea and near a free island") {
    Game g("o1602-test-selection-ship");
    Town t;
    sim::Ship own = sim::make_ship("Pioneer", 30, 30, 0, kLocal);
    own.cannons = 1;
    own.cargo = {sim::CargoSlot{sim::ware::kCannons, 32}, sim::CargoSlot{}};
    t.world.add_ship(own);
    t.world.add_ship(sim::make_ship("Coaster", 17, 8, 0, kLocal));  // 2 tiles off the island
    sim::Ship offered = sim::make_ship("Bargain", 33, 30, 0, kOther);
    offered.for_sale = true;
    t.world.add_ship(offered);
    t.world.add_ship(sim::make_ship("Stranger", 35, 30, 0, kOther));
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    const auto ship = [&](std::size_t i) {
        Selection s;
        s.kind = Selection::Kind::Ship;
        s.ship = t.world.ships()[i].id;
        s.serial = static_cast<std::uint32_t>(i + 1);
        return s;
    };
    const Selection pioneer = ship(0);
    SelectionPanel* p = panels.panel_for(pioneer, c);
    REQUIRE(p != nullptr);
    p->refresh(pioneer, c);
    const auto shown = texts(*p);
    CHECK(std::find(shown.begin(), shown.end(), "Pioneer") != shown.end());

    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38004}, pioneer, c, out);  // offer for sale
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38006}, pioneer, c, out);  // a cannon off
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38091}, pioneer, c, out);  // the cannon's hold
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38025}, pioneer, c, out);  // scuttle
    REQUIRE(out.size() == 4);
    const auto op = [&](std::size_t i) {
        const auto* o = std::get_if<sim::ShipCommand>(&out[i]);
        REQUIRE(o != nullptr);
        CHECK(o->ship == pioneer.ship);
        return o->op;
    };
    CHECK(op(0) == sim::ShipCommand::Op::OfferForSale);
    CHECK(op(1) == sim::ShipCommand::Op::UnmountCannon);
    CHECK(op(2) == sim::ShipCommand::Op::MountCannon);
    CHECK(op(3) == sim::ShipCommand::Op::Scuttle);
    out.clear();

    // Shift + click on the mounted cannons: all of them come off in one command.
    PanelContext shifted = c;
    shifted.shift = true;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38007}, pioneer, shifted, out);
    REQUIRE(out.size() == 1);
    CHECK(op(0) == sim::ShipCommand::Op::UnmountCannon);
    CHECK(std::get_if<sim::ShipCommand>(&out[0])->count == sim::ShipCommand::kAllCannons);
    out.clear();

    // The route editor is a page of its own; Escape and a right click come back.
    const ui::Panel* root = &p->panel();
    CHECK(!p->wheel(-1));     // the ship's page has no list
    CHECK(!p->steps_back());  // a right click there goes out to the player status
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38021}, pioneer, c, out);
    CHECK(&p->panel() != root);
    p->refresh(pioneer, c);
    CHECK(!p->wheel(-1));  // four stops without Open1602's row (ROUTE.GAD lacks the stops' arrows here)
    CHECK(p->steps_back());
    // Choosing a stop on the map ends first, the editor stays.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 58041}, pioneer, c, out);  // the first stop's symbol
    REQUIRE(p->picking());
    CHECK(p->back());
    CHECK(!p->picking());
    CHECK(&p->panel() != root);
    CHECK(p->steps_back());
    CHECK(p->back());
    CHECK(&p->panel() == root);
    CHECK(!p->steps_back());
    CHECK(!p->back());
    CHECK(out.empty());

    // Close to an island without an own city: "found a Kontor" asks the building tool for one.
    const Selection coaster = ship(1);
    REQUIRE(panels.panel_for(coaster, c) == p);
    p->refresh(coaster, c);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38017}, coaster, c, out);
    const auto request = p->take_request();
    REQUIRE(request.has_value());
    CHECK(request->kind == SelectionPanel::Request::Kind::FoundKontor);
    CHECK(request->ship == coaster.ship);
    // ... and asks the island whether a computer there tolerates it.
    REQUIRE(out.size() == 1);
    const auto* ask = std::get_if<sim::DiplomacyOrder>(&out[0]);
    REQUIRE(ask != nullptr);
    CHECK(ask->kind == sim::DiplomacyOrder::Kind::AskFounding);
    CHECK(ask->ship == coaster.ship);
    out.clear();

    // Another player's ship: bought if it is offered, otherwise nothing.
    const Selection bargain = ship(2);
    p->refresh(bargain, c);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38025}, bargain, c, out);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38004}, bargain, c, out);
    const Selection stranger = ship(3);
    p->refresh(stranger, c);
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38004}, stranger, c, out);
    REQUIRE(out.size() == 1);
    const auto* buy = std::get_if<sim::ShipCommand>(&out[0]);
    REQUIRE(buy != nullptr);
    CHECK(buy->op == sim::ShipCommand::Op::Buy);
    CHECK(buy->ship == bargain.ship);
    CHECK(buy->player == kLocal);
}

TEST_CASE("selection panels: the route editor's ship button and a right click lead back to the ship") {
    Game g("o1602-test-selection-route-back", true);
    Town t;
    t.world.add_ship(sim::make_ship("Pioneer", 30, 30, 0, kLocal));
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    Selection s;
    s.kind = Selection::Kind::Ship;
    s.ship = t.world.ships().back().id;
    s.serial = 1;
    SelectionPanel* p = panels.panel_for(s, c);
    REQUIRE(p != nullptr);
    p->refresh(s, c);
    const ui::Panel* root = &p->panel();
    std::vector<sim::Command> out;
    const auto click = [&](std::int32_t id) {
        p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, id}, s, c, out);
        p->refresh(s, c);
    };
    constexpr std::int32_t kRouteEditor = 38021;
    constexpr std::int32_t kShipButton = 58304;
    click(kRouteEditor);
    REQUIRE(&p->panel() != root);
    REQUIRE(p->panel().layout().find(kShipButton) != nullptr);
    CHECK(p->help(kShipButton, s, c).has_value());
    // The button: straight back to the ship's page, also while choosing a stop.
    click(58041);  // the first stop's symbol: choosing its city on the map
    REQUIRE(p->picking());
    click(kShipButton);
    CHECK(&p->panel() == root);
    CHECK(!p->picking());
    CHECK(!p->steps_back());
    // A right click: one step at a time, the choice of a stop first.
    click(kRouteEditor);
    click(58041);
    REQUIRE(p->picking());
    REQUIRE(p->steps_back());
    CHECK(p->back());
    CHECK(!p->picking());
    CHECK(&p->panel() != root);
    REQUIRE(p->steps_back());
    CHECK(p->back());
    CHECK(&p->panel() == root);
    CHECK(out.empty());
}

TEST_CASE("selection panels: copy route gives the selected ship's stops to the ship clicked on") {
    Game g("o1602-test-selection-route-copy", true);
    Town t;
    sim::Ship trader = sim::make_ship("Coaster", 30, 30, 0, kLocal);
    trader.route.stops[0].target = sim::Target::city_of(0, 0);
    t.world.add_ship(trader);
    t.world.add_ship(sim::make_ship("Warship", 40, 30, 0, kLocal));  // an empty route
    t.world.add_ship(sim::make_ship("Stranger", 50, 30, 0, kOther));
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    const auto select = [&](std::size_t i) {
        Selection s;
        s.kind = Selection::Kind::Ship;
        s.ship = t.world.ships()[i].id;
        s.serial = static_cast<std::uint32_t>(i + 1);
        return s;
    };
    constexpr std::int32_t kRouteEditor = 38021;
    constexpr std::int32_t kRouteCopy = 58301;
    std::vector<sim::Command> out;
    const Selection s = select(0);
    SelectionPanel* p = panels.panel_for(s, c);
    REQUIRE(p != nullptr);
    p->refresh(s, c);
    const auto click = [&](const Selection& on, std::int32_t id) {
        p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, id}, on, c, out);
        p->refresh(on, c);
    };
    click(s, kRouteEditor);
    REQUIRE(p->panel().layout().find(kRouteCopy) != nullptr);
    click(s, kRouteCopy);
    REQUIRE(p->picking());
    // A click on the sea or on a stranger's ship gives the route to nobody.
    p->picked(50, 30, s, c, out);
    CHECK(!p->picking());
    CHECK(out.empty());
    // A click on the own warship with its empty route: it takes the trader's stops.
    click(s, kRouteCopy);
    p->picked(41, 31, s, c, out);
    REQUIRE(out.size() == 1);
    const auto* copy = std::get_if<sim::RouteEdit>(&out[0]);
    REQUIRE(copy != nullptr);
    CHECK(copy->op == sim::RouteEdit::Op::CopyFrom);
    CHECK(copy->ship == t.world.ships()[1].id);
    CHECK(copy->target == sim::Target::other_ship(t.world.ships()[0].id));
    // A ship without a city on its route has nothing to copy: the button does not wait for a click.
    out.clear();
    const Selection w = select(1);
    p = panels.panel_for(w, c);
    REQUIRE(p != nullptr);
    p->refresh(w, c);
    click(w, kRouteEditor);
    click(w, kRouteCopy);
    CHECK(!p->picking());
    CHECK(out.empty());
}

TEST_CASE("selection panels: at the pirates' Kontor a ship trades and deals with the pirates") {
    Game g("o1602-test-selection-pirates");
    Town t;
    std::vector<sim::Player> players = t.world.players();
    sim::make_neutral(players[kOther], sim::faction::kPirates);
    t.world.set_players(players);
    add_city(t.world, 4, kOther, "Nest");
    t.world.city(0, 4)->warehouse_x = 6;
    t.world.city(0, 4)->warehouse_y = 5;
    t.world.add_ship(sim::make_ship("Pioneer", 11, 12, 0, kLocal));  // below the Kontor at world (10, 9)
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    Selection s;
    s.kind = Selection::Kind::Ship;
    s.ship = t.world.ships().back().id;
    s.serial = 1;
    SelectionPanel* p = panels.panel_for(s, c);
    REQUIRE(p != nullptr);
    p->refresh(s, c);
    const ui::Panel* root = &p->panel();
    std::vector<sim::Command> out;
    // With the trade treaty players start with, the trade page opens and stays open.
    t.world.diplomacy().relation(kLocal, kOther).trade = sim::Treaty::Signed;
    t.world.diplomacy().relation(kOther, kLocal).trade = sim::Treaty::Signed;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38005}, s, c, out);
    p->refresh(s, c);
    CHECK(&p->panel() != root);
    CHECK(p->back());
    CHECK(&p->panel() == root);
    // The pirates' own page too.
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38019}, s, c, out);
    p->refresh(s, c);
    const ui::Panel* pirates = &p->panel();
    CHECK(pirates != root);
    CHECK(p->back());
    // Without a treaty, no trade.
    t.world.diplomacy().relation(kLocal, kOther).trade = sim::Treaty::None;
    t.world.diplomacy().relation(kOther, kLocal).trade = sim::Treaty::None;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 38005}, s, c, out);
    p->refresh(s, c);
    CHECK(&p->panel() == root);
    CHECK(out.empty());
}

TEST_CASE("selection panels: the city list shows a pirates' nest once its island is discovered") {
    Game g("o1602-test-selection-citylist-pirates");
    Town t;
    std::vector<sim::Player> players = t.world.players();
    sim::make_neutral(players[kOther], sim::faction::kPirates);
    t.world.set_players(players);
    add_city(t.world, 4, kOther, "Nest");
    t.world.city(0, 4)->warehouse_x = 6;
    t.world.city(0, 4)->warehouse_y = 5;
    sim::Simulation simulation(t.world, t.buildings, t.figures);
    SelectionPanels panels(*g.dir);
    const PanelContext c{simulation, t.buildings, t.figures, &g.texts, kLocal};
    Selection list;
    list.kind = Selection::Kind::CityList;
    SelectionPanel* p = panels.panel_for(list, c);
    REQUIRE(p != nullptr);
    const auto listed = [&] {
        p->opened();
        p->refresh(list, c);
        const auto shown = texts(*p);
        return std::find(shown.begin(), shown.end(), "Nest") != shown.end();
    };
    CHECK(!listed());  // not found yet
    t.world.island(0).discover(kLocal);
    CHECK(listed());
    // A click on it goes to the nest's Kontor.
    std::vector<sim::Command> out;
    p->clicked(ui::PanelEvent{ui::PanelEvent::Kind::Clicked, 63031}, list, c, out);
    const auto request = p->take_request();
    REQUIRE(request.has_value());
    CHECK(request->x == 4 + 6);
    CHECK(request->y == 4 + 5);
    // Another player who discovered it does not make it known to the local one.
    t.world.island(0).discovered.assign(t.world.players().size(), false);
    t.world.island(0).discover(kOther);
    CHECK(!listed());
}
