// SPDX-License-Identifier: GPL-3.0-or-later
// Placement, tile attributes and sprite selection.

#include <functional>
#include <set>
#include <utility>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/island.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/sim/island_map.hpp"

using namespace o1602;

namespace {

assets::BuildingType make_building(std::uint32_t gfx, std::uint32_t w, std::uint32_t h) {
    assets::BuildingType b;
    b.id = 20100;
    b.gfx = gfx;
    b.size_x = w;
    b.size_y = h;
    b.rotate = w * h;
    return b;
}

}  // namespace

TEST_CASE("island tile: attribute bits") {
    assets::IslandTile t;
    t.bits = 0x01C3C4C7;  // first tile of NORD/big.scp
    CHECK(t.rotation() == 3);
    CHECK(t.anim_phase() == 1);
    CHECK(t.island_number() == 19);
    CHECK(t.city() == assets::IslandTile::kNoCity);
    CHECK(t.random() == 1);
    CHECK(t.player() == 7);
}

TEST_CASE("island map: placement order for rotation 0 and 2") {
    const auto b = make_building(100, 3, 2);  // 3 wide, 2 deep
    sim::IslandMap map(8, 8);
    REQUIRE(map.place(b, 1, 1, 0, 0).ok());
    CHECK(map.top(1, 1).sprite == 100);
    CHECK(map.top(3, 1).sprite == 102);
    CHECK(map.top(1, 2).sprite == 103);
    CHECK(map.top(3, 2).sprite == 105);

    sim::IslandMap map2(8, 8);
    REQUIRE(map2.place(b, 1, 1, 2, 0).ok());
    CHECK(map2.top(3, 2).sprite == 100);  // traversal starts at the far corner
    CHECK(map2.top(1, 1).sprite == 105);
    CHECK(map2.top(1, 1).rotation == 2);
}

TEST_CASE("island map: rotations 1 and 3 swap the footprint") {
    const auto b = make_building(100, 3, 2);
    for (std::uint8_t r : {std::uint8_t{1}, std::uint8_t{3}}) {
        sim::IslandMap map(8, 8);
        REQUIRE(map.place(b, 0, 0, r, 0).ok());
        std::size_t covered = 0;
        for (std::uint32_t y = 0; y < 8; ++y) {
            for (std::uint32_t x = 0; x < 8; ++x) {
                if (!map.top(x, y).empty()) {
                    ++covered;
                    CHECK(x < 2);  // rotated footprint: 2 wide, 3 deep
                    CHECK(y < 3);
                }
            }
        }
        CHECK(covered == 6);
    }
}

TEST_CASE("island map: sub_tile is the inverse of place for all rotations") {
    // Build a one-entry table through the cod interpreter to keep BuildingTable's invariants.
    const assets::CodPredefinedSymbol haus_type[] = {{"HAUS", 1}};
    const auto doc =
        assets::cod::parse("Objekt: HAUS\n@Nummer: 0\nId: 20100\nGfx: 100\nSize: 3, 2\nRotate: 6\nEndObj\n", haus_type);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    const assets::BuildingType* b = table->by_id(20100);
    REQUIRE(b != nullptr);

    for (std::uint8_t r = 0; r < 4; ++r) {
        sim::IslandMap map(10, 10);
        REQUIRE(map.place(*b, 4, 5, r, 0).ok());
        std::set<std::uint32_t> sprites;
        for (std::uint32_t y = 0; y < 10; ++y) {
            for (std::uint32_t x = 0; x < 10; ++x) {
                if (map.top(x, y).empty()) {
                    continue;
                }
                sprites.insert(map.top(x, y).sprite);
                const auto sub = map.sub_tile(*table, x, y);
                CHECK(sub.building == b);
                CHECK(sub.origin_x == 4);
                CHECK(sub.origin_y == 5);
            }
        }
        CHECK(sprites.size() == 6);
        CHECK(*sprites.begin() == 100);
        CHECK(*sprites.rbegin() == 105);
    }
}

TEST_CASE("island map: out of bounds placement fails without changes, ground layer keeps old tile") {
    const auto b = make_building(100, 3, 2);
    const auto ground = make_building(7, 1, 1);
    sim::IslandMap map(4, 4);
    CHECK(!map.place(b, 2, 3, 0, 0).ok());
    CHECK(map.top(2, 3).empty());
    REQUIRE(map.place(ground, 0, 0, 0, 0).ok());
    REQUIRE(map.place(b, 0, 0, 0, 0).ok());
    CHECK(map.top(0, 0).sprite == 100);
    CHECK(map.ground(0, 0).sprite == 7);
}

TEST_CASE("render: sprite index = base + animation frame + rotation step") {
    assets::BuildingType b = make_building(100, 2, 2);  // rotate = 4
    b.anim_count = 3;
    b.anim_add = 16;
    sim::MapCell cell{101, 1, 2};  // sub-tile 1, rotation 1, phase 2
    render::RenderOptions o;
    CHECK(render::sprite_for_cell(cell, &b, o) == 101 + 16 * 2 + 4);
    o.anim_step = 1;  // frame (2 + 1) % 3 = 0
    CHECK(render::sprite_for_cell(cell, &b, o) == 101 + 4);
    o.view_rotation = 3;  // (1 + 3) & 3 = 0
    CHECK(render::sprite_for_cell(cell, &b, o) == 101);
}

TEST_CASE("island header: template path from number, size and climate") {
    assets::IslandHeader h;
    CHECK(h.template_path().empty());  // default: no template
    h.template_number = 12;
    h.template_size = 3;
    h.climate = 0;
    CHECK(h.template_path() == "Nord/big12.scp");
    h.template_number = 7;
    h.template_size = 0;
    h.climate = 1;
    CHECK(h.template_path() == "Sued/lit07.scp");
    h.stored_complete = true;
    CHECK(h.template_path().empty());
    h.stored_complete = false;
    h.template_number = assets::IslandHeader::kNoTemplate;
    CHECK(h.template_path().empty());
}

TEST_CASE("island map: template is placed below the island's own entries") {
    assets::Island base;
    base.header.width = 4;
    base.header.height = 4;
    assets::Island island = base;
    // template: grass everywhere (of no city: attribute city 7, it becomes the ground); island: one
    // house of city 0
    base.layers.emplace_back();
    for (std::uint8_t y = 0; y < 4; ++y) {
        for (std::uint8_t x = 0; x < 4; ++x) {
            base.layers[0].push_back(assets::IslandTile{0, x, y, 7U << 14});
        }
    }
    island.layers.push_back({assets::IslandTile{1, 1, 1, 0}});
    // minimal table with ids 20000 (grass) and 20001 (house)
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n@Nummer: 0\nId: 20000\nGfx: 10\nSize: 1, 1\n"
        "@Nummer: +1\nId: 20001\nGfx: 100\nSize: 2, 2\nEndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    std::vector<std::string> warnings;
    const auto map = sim::IslandMap::from_island(island, *table, &warnings, &base);
    CHECK(warnings.empty());
    CHECK(map.top(0, 0).sprite == 10);
    CHECK(map.top(1, 1).sprite == 100);
    CHECK(map.top(2, 2).sprite == 103);
    CHECK(map.ground(2, 2).sprite == 10);  // template tile kept as ground
    const auto without = sim::IslandMap::from_island(island, *table);
    CHECK(without.top(0, 0).empty());
}

TEST_CASE("island map: every change of a tile gives its block, and only its block, a new revision (TB-26)") {
    const auto b = make_building(100, 2, 3);
    sim::IslandMap map(40, 40);  // blocks of 16 x 16 tiles: 3 x 3, the last ones 8 wide or high
    REQUIRE(map.blocks_x() == 3);
    REQUIRE(map.blocks_y() == 3);
    const auto revisions = [&] {
        std::vector<std::uint64_t> out;
        for (std::size_t k = 0; k < 9; ++k) {
            out.push_back(map.block_revision(k));
        }
        return out;
    };
    // Which blocks a change gave a new revision.
    const auto changed = [&](const std::vector<std::uint64_t>& before) {
        std::set<std::size_t> out;
        for (std::size_t k = 0; k < 9; ++k) {
            if (map.block_revision(k) != before[k]) {
                out.insert(k);
            }
        }
        return out;
    };
    std::vector<std::uint64_t> before = revisions();
    CHECK(std::set<std::uint64_t>(before.begin(), before.end()).size() == 9);
    const std::vector<std::pair<std::set<std::size_t>, std::function<void()>>> changes = {
        {{0}, [&] { map.set_sprite(1, 2, 7); }},
        {{4}, [&] { map.set_rotation(17, 16, 2); }},
        {{8}, [&] { map.set_anim_phase(39, 39, 3); }},
        {{2}, [&] { map.restore_ground(32, 15); }},
        {{6}, [&] { map.set_top_cell(3, 20 + 16, sim::MapCell{}); }},
        {{5}, [&] { map.set_ground_cell(33, 17, sim::MapCell{}); }},
        {{3}, [&] { map.set_city(0, 31, 1, 0); }},
        {{0, 1, 3, 4}, [&] { REQUIRE(map.place(b, 15, 14, 0, 0).ok()); }},  // columns 15-16, rows 14-16
        {{4}, [&] { REQUIRE(map.build(b, 20, 18, 0, 0, 0, 0).ok()); }},
        {{4, 7}, [&] { REQUIRE(map.replace(b, 20, 30, 0, 0).ok()); }},           // rows 30-32
        {{6, 7}, [&] { REQUIRE(map.load(b, 14, 36, 1, 0, 0, 0, true).ok()); }},  // rotated: columns 14-16
        {{0, 1, 2, 3, 4, 5, 6, 7, 8}, [&] { map.capture_ground(); }},
    };
    for (const auto& [blocks, change] : changes) {
        before = revisions();
        change();
        CHECK(changed(before) == blocks);
    }
    before = revisions();
    CHECK(!map.place(b, 39, 0, 0, 0).ok());  // does not fit: nothing changes
    CHECK(changed(before).empty());
    const sim::IslandMap copy = map;  // the same tiles, the same revisions
    CHECK(copy.block_revision(4) == map.block_revision(4));
}
