// SPDX-License-Identifier: GPL-3.0-or-later
// The build tool: the building list, and the orders a click or a drag gives (engine/app/src/build_tool.hpp).

#include <set>
#include <tuple>

#include "../test_framework.hpp"
#include "build_tool.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/ship.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kTree = 20202;
constexpr std::uint32_t kField = 21301;
constexpr std::uint32_t kKontor = 22001;
constexpr std::uint32_t kKontor2 = 22002;
constexpr std::uint32_t kHouse = 20899;
constexpr std::uint32_t kDwelling = 20605;
constexpr std::uint32_t kPier = 21073;
constexpr std::uint32_t kStoneWall = 22501;
constexpr std::uint32_t kMonument = 20851;
constexpr std::uint32_t kGrain = 21305;
constexpr std::uint32_t kGrainRipe = 21306;
constexpr std::uint32_t kFarm = 21401;
constexpr std::uint32_t kGrownHouse = 20606;
constexpr std::uint32_t kRoadCross = 20402;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        // Ground: no cost, never in the list.
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Baugfx: 30\n  Size: 1, 1\n  KreuzBase: 1\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        // A tree in three variants one record apart.
        "  @Nummer: 2\n  Id: 20202\n  Kind: 10\n  Gfx: 8\n  Baugfx: 20\n  Size: 1, 1\n  RandAnz: 3\n  RandAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20203\n  Kind: 10\n  Gfx: 9\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20204\n  Kind: 10\n  Gfx: 10\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
        "  @Nummer: 5\n  Id: 21301\n  Kind: 10\n  Gfx: 41\n  Baugfx: 10\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 6\n  Id: 22002\n  Kind: 35\n  Gfx: 26\n  Baugfx: 41\n  Size: 2, 3\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 200\n  EndObj\n"
        "  @Nummer: 7\n  Id: 22001\n  Kind: 35\n  Gfx: 20\n  Baugfx: 40\n  Size: 2, 3\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n"
        // A house without a menu icon: not in the list.
        "  @Nummer: 8\n  Id: 20899\n  Kind: 14\n  Gfx: 36\n  Size: 2, 2\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 10\n  EndObj\n"
        // A dwelling (WOHNUNG) and a pier: the History Edition's „Mehrfachplatzierung“.
        "  @Nummer: 9\n  Id: 20605\n  Kind: 14\n  Gfx: 50\n  Baugfx: 50\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 3\n    Money: 20\n  EndObj\n"
        "  @Nummer: 10\n  Id: 21073\n  Kind: 30\n  Gfx: 54\n  Baugfx: 51\n  Size: 1, 1\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 15\n  EndObj\n"
        // The stone wall: Baugfx 0 is the first icon of its set, not none.
        "  @Nummer: 11\n  Id: 22501\n  Kind: 4\n  Gfx: 56\n  Baugfx: 0\n  Size: 1, 1\n  KreuzBase: 11\n"
        "  Objekt: HAUS_BAUKOST\n    Ziegel: 2\n    Money: 18\n  EndObj\n"
        // The monument: no cost (an allowance unlocks it), but the build menu lists it.
        "  @Nummer: 12\n  Id: 20851\n  Kind: 14\n  Gfx: 60\n  Baugfx: 60\n  Size: 1, 1\n"
        // A grain field growing and ripe, and the farm that harvests it (the copy tool).
        "  @Nummer: 13\n  Id: 21305\n  Kind: 10\n  Gfx: 70\n  Baugfx: 12\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 14\n  Id: 21306\n  Kind: 10\n  Gfx: 71\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 45\n  EndObj\n"
        "  @Nummer: 15\n  Id: 21401\n  Kind: 14\n  Gfx: 72\n  Baugfx: 70\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Rohstoff: 45\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n"
        // A grown house, with an icon and costs as the game's data gives them, and a road crossing
        // (the copy tool and the pipette order the first house and the road).
        "  @Nummer: 16\n  Id: 20606\n  Kind: 14\n  Gfx: 76\n  Baugfx: 52\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 1\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 3\n    Money: 40\n  EndObj\n"
        "  @Nummer: 17\n  Id: 20402\n  Kind: 1\n  Gfx: 80\n  Size: 1, 1\n  KreuzBase: 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// One island of ground at (10, 10), 20 x 20 tiles; player 0 has ships at both ends of the map.
struct Map {
    assets::BuildingTable buildings = make_buildings();
    sim::World world{100, 100};

    Map() {
        sim::IslandMap map(20, 20);
        for (std::int32_t y = 0; y < 20; ++y) {
            for (std::int32_t x = 0; x < 20; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
        world.set_players(std::vector<sim::Player>(2));
        world.add_ship(sim::make_ship("Near", 9, 9, 12, 0));
        world.add_ship(sim::make_ship("Far", 90, 90, 12, 0));
        world.add_ship(sim::make_ship("Foreign", 10, 10, 12, 1));
    }
    [[nodiscard]] std::uint32_t ship(std::size_t i) const { return world.ships()[i].id; }
};

}  // namespace

TEST_CASE("build tool: the list holds what the build menu offers, in its order") {
    const assets::BuildingTable buildings = make_buildings();
    app::BuildTool tool(buildings);
    CHECK(tool.mode() == app::BuildTool::Mode::Off);
    CHECK(tool.selected()->id == kStoneWall);
    std::vector<std::uint32_t> order;
    for (const assets::BuildingType* b : tool.buildings()) {
        order.push_back(b->id);
    }
    // By menu icon; ground, tree variants and the house without an icon are left out.
    // The monument costs nothing but is in the menu (#7: picking it kept the building before).
    const std::vector<std::uint32_t> expected{kStoneWall, kField,    kGrain, kTree,       kRoad,     kKontor,
                                              kKontor2,   kDwelling, kPier,  kGrownHouse, kMonument, kFarm};
    CHECK(order == expected);
    CHECK(tool.select(kMonument));
    CHECK(tool.selected()->id == kMonument);
    CHECK(tool.select(kRoad));
    CHECK(tool.selected()->id == kRoad);
    CHECK(!tool.select(kHouse));
    CHECK(!tool.select(kGround));
    tool.rotate(-1);
    CHECK(tool.rotation() == 3);
    tool.rotate(2);
    CHECK(tool.rotation() == 1);
}

TEST_CASE("build tool: no order unless the matching mode is on") {
    const Map m;
    app::BuildTool tool(m.buildings);
    const app::BuildTool::Spot spot{0, 3, 4};
    CHECK(!tool.build_order(m.world, 0, spot).has_value());
    CHECK(!tool.demolish_order(0, spot).has_value());
    tool.set_mode(app::BuildTool::Mode::Demolish);
    CHECK(!tool.build_order(m.world, 0, spot).has_value());
    tool.press({0, 1, 8});
    const auto demolish = tool.demolish_order(0, spot);
    REQUIRE(demolish.has_value());
    CHECK(demolish->from.x == 1);
    CHECK(demolish->from.y == 8);
    CHECK(demolish->to.x == 3);
    CHECK(demolish->to.y == 4);
    // Changing the mode ends the drag.
    tool.set_mode(app::BuildTool::Mode::Build);
    CHECK(!tool.dragging());
}

TEST_CASE("build tool: blueprint mode orders the same buildings as blueprints, and outlives a change of tool") {
    const Map m;
    app::BuildTool tool(m.buildings);
    tool.set_mode(app::BuildTool::Mode::Build);
    REQUIRE(tool.select(kField));
    CHECK(!tool.blueprint());
    tool.set_blueprint(true);
    tool.press({0, 7, 2});
    const auto build = tool.build_order(m.world, 0, {0, 5, 4});
    const auto blueprints = tool.blueprint_order(m.world, 0, {0, 5, 4});
    REQUIRE(build.has_value());
    REQUIRE(blueprints.has_value());
    CHECK(blueprints->kind == sim::BlueprintOrder::Kind::Place);
    CHECK(blueprints->island == build->island);
    REQUIRE(blueprints->items.size() == build->items.size());
    CHECK(blueprints->items.back().x == build->items.back().x);
    tool.set_mode(app::BuildTool::Mode::Copy);
    CHECK(tool.blueprint());
}

TEST_CASE("build tool: a field is dragged over a rectangle, a click places one") {
    const Map m;
    app::BuildTool tool(m.buildings);
    tool.set_mode(app::BuildTool::Mode::Build);
    REQUIRE(tool.select(kField));
    auto click = tool.build_order(m.world, 0, {0, 5, 5});
    REQUIRE(click.has_value());
    REQUIRE(click->items.size() == 1);
    CHECK(click->items[0].x == 5);
    tool.press({0, 7, 2});
    CHECK(tool.hatched_drag());
    const auto drag = tool.build_order(m.world, 0, {0, 5, 4});
    REQUIRE(drag.has_value());
    CHECK(drag->items.size() == 9);  // x 5..7, y 2..4
    CHECK(drag->player == 0);
    CHECK(!drag->ship.has_value());
    tool.release();
    CHECK(!tool.dragging());
}

TEST_CASE("build tool: houses are dragged out side by side, piers along a line, as in the History Edition") {
    const Map m;
    app::BuildTool tool(m.buildings);
    tool.set_mode(app::BuildTool::Mode::Build);
    REQUIRE(tool.select(kDwelling));
    tool.press({0, 6, 3});
    CHECK(tool.multi_drag());
    CHECK(!tool.hatched_drag());
    // From (6, 3) up and left to (1, 0): 2 x 2 houses in steps of their size, row by row from the start.
    const auto block = tool.build_order(m.world, 0, {0, 1, 0});
    REQUIRE(block.has_value());
    std::vector<std::pair<std::int32_t, std::int32_t>> at;
    for (const sim::BuildItem& item : block->items) {
        CHECK(item.building == kDwelling);
        at.emplace_back(item.x, item.y);
    }
    const std::vector<std::pair<std::int32_t, std::int32_t>> expected{{6, 3}, {4, 3}, {2, 3}, {6, 1}, {4, 1}, {2, 1}};
    CHECK(at == expected);
    // A row of piers, one per tile.
    tool.release();
    REQUIRE(tool.select(kPier));
    tool.press({0, 2, 7});
    const auto row = tool.build_order(m.world, 0, {0, 5, 7});
    REQUIRE(row.has_value());
    CHECK(row->items.size() == 4);
    // Piers go along the straight line to the mouse, not over a rectangle (History Edition).
    const auto slant = tool.build_order(m.world, 0, {0, 6, 9});
    REQUIRE(slant.has_value());
    at.clear();
    for (const sim::BuildItem& item : slant->items) {
        at.emplace_back(item.x, item.y);
    }
    const std::vector<std::pair<std::int32_t, std::int32_t>> line{{2, 7}, {3, 8}, {4, 8}, {5, 9}, {6, 9}};
    CHECK(at == line);
}

TEST_CASE("build tool: trees keep their variants during a drag") {
    const Map m;
    app::BuildTool tool(m.buildings);
    tool.set_mode(app::BuildTool::Mode::Build);
    REQUIRE(tool.select(kTree));
    tool.press({0, 0, 0});
    const auto first = tool.build_order(m.world, 0, {0, 9, 9});
    const auto again = tool.build_order(m.world, 0, {0, 9, 9});
    REQUIRE(first.has_value());
    REQUIRE(again.has_value());
    REQUIRE(first->items.size() == 100);
    std::set<std::uint32_t> ids;
    for (std::size_t i = 0; i < first->items.size(); ++i) {
        CHECK(first->items[i].building == again->items[i].building);
        ids.insert(first->items[i].building);
    }
    // A hundred tiles show all three variants.
    const std::set<std::uint32_t> variants{20202, 20203, 20204};
    CHECK(ids == variants);
}

TEST_CASE("build tool: a Kontor is paid from the nearest own ship, or the one chosen") {
    const Map m;
    app::BuildTool tool(m.buildings);
    tool.set_mode(app::BuildTool::Mode::Build);
    REQUIRE(tool.select(kKontor));
    const auto nearest = tool.build_order(m.world, 0, {0, 2, 2});
    REQUIRE(nearest.has_value());
    REQUIRE(nearest->ship.has_value());
    CHECK(*nearest->ship == m.ship(0));  // the foreign ship is nearer still
    // The ship window's button: the first Kontor, paid by that ship.
    REQUIRE(tool.select(kKontor2));  // another building first
    REQUIRE(tool.select_kontor(m.ship(1)));
    CHECK(tool.selected()->id == kKontor);
    const auto chosen = tool.build_order(m.world, 0, {0, 2, 2});
    REQUIRE(chosen.has_value());
    REQUIRE(chosen->ship.has_value());
    CHECK(*chosen->ship == m.ship(1));
    // Another player's order ignores a ship that is not theirs.
    const auto other = tool.build_order(m.world, 1, {0, 2, 2});
    REQUIRE(other.has_value());
    REQUIRE(other->ship.has_value());
    CHECK(*other->ship == m.ship(2));
    // Leaving build mode forgets the chosen ship.
    tool.set_mode(app::BuildTool::Mode::Off);
    tool.set_mode(app::BuildTool::Mode::Build);
    const auto after = tool.build_order(m.world, 0, {0, 2, 2});
    REQUIRE(after.has_value());
    CHECK(*after->ship == m.ship(0));
}

TEST_CASE("build tool: the copy tool takes a piece of the own city and builds it again elsewhere") {
    Map m;
    sim::IslandMap& map = m.world.island(0).map;
    const auto put = [&](std::uint32_t id, std::int32_t x, std::int32_t y, sim::PlayerId owner) {
        REQUIRE(map.build(*m.buildings.by_id(id), x, y, 0, 0, owner, 0).ok());
    };
    put(kFarm, 2, 2, 0);
    put(kGrainRipe, 4, 2, 0);
    put(kGrainRipe, 4, 3, 0);
    put(kRoadCross, 2, 4, 0);
    put(kRoad, 3, 4, 0);
    put(kGrownHouse, 6, 6, 0);
    put(kTree + 1, 5, 5, 0);     // wild forest: no woodcutter in the piece
    put(kDwelling, 2, 6, 1);     // another player's
    put(kStoneWall, 12, 12, 0);  // outside the drag

    app::BuildTool tool(m.buildings);
    CHECK(tool.orderable(*m.buildings.by_id(kRoadCross))->id == kRoad);
    CHECK(tool.orderable(*m.buildings.by_id(kGrainRipe))->id == kGrain);
    CHECK(tool.orderable(*m.buildings.by_id(kGrownHouse))->id == kDwelling);
    CHECK(tool.orderable(*m.buildings.by_id(kTree + 2))->id == kTree + 2);  // a variant keeps its look
    CHECK(tool.orderable(*m.buildings.by_id(kGround)) == nullptr);
    CHECK(tool.orderable(*m.buildings.by_id(kKontor)) == nullptr);

    tool.set_mode(app::BuildTool::Mode::Copy);
    CHECK(tool.copy_selecting());
    CHECK(!tool.places());
    CHECK(!tool.build_order(m.world, 0, {0, 3, 3}).has_value());
    tool.press({0, 2, 2});
    CHECK(!tool.copy_preview(m.world, 0, {0, 7, 7}).empty());
    REQUIRE(tool.take_layout(m.world, 0, {0, 7, 7}));
    CHECK(!tool.dragging());
    CHECK(tool.places());
    REQUIRE(tool.layout().has_value());
    const app::BuildTool::Layout& layout = *tool.layout();
    CHECK(layout.width == 6);
    CHECK(layout.height == 6);
    std::vector<std::tuple<std::uint32_t, std::int32_t, std::int32_t>> items;
    for (const auto& item : layout.items) {
        items.emplace_back(item.building, item.x, item.y);
    }
    // Roads first, then buildings, then fields; the house as the first one; no tree, nothing foreign.
    const std::vector<std::tuple<std::uint32_t, std::int32_t, std::int32_t>> expected{
        {kRoad, 0, 2}, {kRoad, 1, 2}, {kFarm, 0, 0}, {kDwelling, 4, 4}, {kGrain, 2, 0}, {kGrain, 2, 1}};
    CHECK(items == expected);

    // Hung at the mouse by its middle.
    const auto order = tool.build_order(m.world, 0, {0, 12, 3});
    REQUIRE(order.has_value());
    REQUIRE(order->items.size() == 6);
    CHECK(!order->orient_to_road);
    CHECK(order->items[2].building == kFarm);
    CHECK(order->items[2].x == 10);
    CHECK(order->items[2].y == 1);

    // A quarter turn takes east to south: the farm's corner (0, 0) goes to the far side.
    tool.rotate(1);
    const app::BuildTool::Layout& turned = *tool.layout();
    CHECK(turned.width == 6);
    CHECK(turned.items[2].x == 4);
    CHECK(turned.items[2].y == 0);
    CHECK(turned.items[2].rotation == 1);
    CHECK(turned.items[4].x == 5);  // the field (2, 0) goes to (6 - 1 - 0, 2)
    CHECK(turned.items[4].y == 2);
    tool.rotate(3);
    CHECK(tool.layout()->items[2].x == 0);
    CHECK(tool.layout()->items[2].rotation == 0);

    // Putting the piece away keeps the tool; a drag over nothing of the own leaves the hand empty.
    tool.drop_layout();
    CHECK(tool.copy_selecting());
    tool.press({0, 14, 14});
    CHECK(!tool.take_layout(m.world, 0, {0, 18, 18}));
    CHECK(tool.copy_selecting());
}

TEST_CASE("build tool: the move tool picks up an own building and hangs it at the mouse by its middle") {
    Map m;
    sim::City city;
    city.island = 0;
    city.index = 0;
    city.owner = 0;
    m.world.add_city(std::move(city));
    sim::IslandMap& map = m.world.island(0).map;
    REQUIRE(map.build(*m.buildings.by_id(kFarm), 4, 4, 1, 0, 0, 0).ok());
    REQUIRE(map.build(*m.buildings.by_id(kRoad), 9, 9, 0, 0, 0, 0).ok());

    app::BuildTool tool(m.buildings);
    tool.set_mode(app::BuildTool::Mode::Move);
    CHECK(tool.move_selecting());
    CHECK(!tool.move_order(0, {0, 10, 10}).has_value());
    // A road is no building to move, another player's farm not the player's.
    CHECK(tool.take_move(m.world, m.buildings, 0, {0, 9, 9}) == sim::MoveRefusal::NoBuilding);
    CHECK(tool.take_move(m.world, m.buildings, 1, {0, 5, 5}) == sim::MoveRefusal::Nothing);
    CHECK(tool.move_selecting());
    REQUIRE(tool.take_move(m.world, m.buildings, 0, {0, 5, 5}) == sim::MoveRefusal::None);
    REQUIRE(tool.move_hand().has_value());
    CHECK((tool.move_hand()->origin == sim::TilePos{4, 4}));
    CHECK(tool.rotation() == 1);  // it keeps its rotation
    const auto order = tool.move_order(0, {0, 12, 12});
    REQUIRE(order.has_value());
    CHECK((order->from == sim::TilePos{4, 4}));
    CHECK(order->x == 12);  // a 2 x 2 building: the mouse on its upper left middle tile
    CHECK(order->y == 12);
    CHECK(order->rotation == 1);
    CHECK(!tool.move_order(0, {1, 12, 12}).has_value());  // not onto another island
    tool.drop_move();
    CHECK(tool.move_selecting());
    tool.set_mode(app::BuildTool::Mode::Build);
    CHECK(!tool.move_hand().has_value());
}

TEST_CASE("build tool: a template goes into the copy tool's hand, without buildings the game does not know") {
    Map m;
    app::BuildTool tool(m.buildings);
    tool.set_mode(app::BuildTool::Mode::Demolish);
    app::BuildTool::Layout layout;
    layout.width = 4;
    layout.height = 3;
    layout.items = {{kRoad, 0, 2, 0, 1, 1}, {kFarm, 0, 0, 0, 2, 2}, {99999, 2, 0, 0, 1, 1}};
    REQUIRE(tool.hold_layout(layout));
    CHECK(tool.mode() == app::BuildTool::Mode::Copy);
    CHECK(tool.places());
    REQUIRE(tool.layout().has_value());
    CHECK(tool.layout()->items.size() == 2);  // the unknown one left out
    CHECK(tool.layout()->width == 4);
    const auto order = tool.build_order(m.world, 0, {0, 10, 10});
    REQUIRE(order.has_value());
    CHECK(order->items.size() == 2);

    // Nothing the game knows: the copy tool waits for a piece.
    layout.items = {{99999, 0, 0, 0, 1, 1}};
    CHECK(!tool.hold_layout(layout));
    CHECK(tool.copy_selecting());
}
