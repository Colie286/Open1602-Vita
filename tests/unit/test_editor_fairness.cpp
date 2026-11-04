// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>

#include "../test_framework.hpp"

#include "o1602/editor/fairness.hpp"
#include "o1602/editor/map_image.hpp"
#include "o1602/sim/ship.hpp"

using namespace o1602;
using editor::StartArea;
using editor::Unfairness;

TEST_CASE("editor fairness: uneven land, crops, ore and gold between the starts are found") {
    StartArea a;
    a.player = 0;
    a.islands = 2;
    a.land = 1000;
    a.crops = {1, 0, 1, 0, 0, 0};
    a.ore = 2;
    a.gold = 1;
    StartArea b = a;
    b.player = 1;
    CHECK(editor::unfairness({a, b}).empty());
    CHECK(editor::unfairness({a}).empty());  // one start: nothing to compare

    b.land = 600;  // less than 70 percent of the richest
    b.crops = {1, 0, 0, 0, 0, 1};
    b.gold = 0;
    const auto uneven = editor::unfairness({a, b});
    const std::vector<Unfairness> expected = {
        {Unfairness::Kind::Crop, 0, 5},
        {Unfairness::Kind::Land, 1},
        {Unfairness::Kind::Crop, 1, 2},
        {Unfairness::Kind::Gold, 1},
    };
    CHECK(uneven == expected);

    // A start at sea with no island in reach (a fleet) is left out: nothing to compare it with.
    StartArea fleet;
    fleet.player = 2;
    CHECK(editor::unfairness({a, fleet}).empty());
}

TEST_CASE("editor fairness: a world without starts has no start areas") {
    const assets::BuildingTable buildings;
    sim::World world(40, 40);
    std::vector<sim::Player> players(2);
    players[0].role = sim::PlayerRole::Human;
    players[1].role = sim::PlayerRole::Computer;
    world.set_players(players);
    CHECK(editor::start_areas(world, buildings).empty());  // neither a warehouse nor a ship

    // Two ships at sea: two starts without islands, 20 + 8 / 4 tiles apart.
    sim::Ship s;
    s.owner = 0;
    s.x = 5;
    s.y = 5;
    world.add_ship(s);
    s.owner = 1;
    s.x = 25;
    s.y = 13;
    world.add_ship(s);
    const auto areas = editor::start_areas(world, buildings);
    REQUIRE(areas.size() == 2);
    CHECK(areas[0].player == 0);
    CHECK(areas[0].islands == 0);
    CHECK(areas[0].rival == 22);
    CHECK(areas[1].x == 25);
}

TEST_CASE("editor map picture: a square of the sea's colour a tile, a start marked") {
    const assets::BuildingTable buildings;
    sim::World world(10, 6);
    auto image = editor::map_image(world, buildings, 3);
    CHECK(image.width == 30);
    CHECK(image.height == 18);
    REQUIRE(image.rgba.size() == std::size_t{30} * 18 * 4);
    CHECK(image.rgba[0] == 30);  // sea
    CHECK(image.rgba[3] == 255);

    std::vector<sim::Player> players(2);
    players[0].role = sim::PlayerRole::Human;
    players[1].role = sim::PlayerRole::Computer;
    world.set_players(players);
    sim::Ship s;
    s.owner = 0;
    s.x = 5;
    s.y = 3;
    world.add_ship(s);
    image = editor::map_image(world, buildings, 3);
    const std::size_t middle = (std::size_t{3 * 3 + 1} * 30 + (5 * 3 + 1)) * 4;
    CHECK(image.rgba[middle] == 255);  // the start's white square
}
