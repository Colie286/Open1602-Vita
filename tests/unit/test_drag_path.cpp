// SPDX-License-Identifier: GPL-3.0-or-later
// The path of a dragged road or wall. The expected paths come from an
// independent model of the original's search.

#include <string>
#include <string_view>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/drag_path.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRock = 20102;
constexpr std::uint32_t kGate = 20103;
constexpr std::uint32_t kSquare = 20104;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20102\n  Kind: 15\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20103\n  Kind: 3\n  Gfx: 2\n  Size: 3, 1\n"
        "  @Nummer: 3\n  Id: 20104\n  Kind: 13\n  Gfx: 5\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island from rows of '.' (ground), '#' (rock), 's' (square), 'g' (a gate of three tiles,
/// written at its first tile).
struct Land {
    assets::BuildingTable buildings = make_buildings();
    sim::World world{400, 400};

    explicit Land(std::string_view rows) {
        std::vector<std::string_view> lines;
        while (!rows.empty()) {
            const auto nl = rows.find('\n');
            const std::string_view line = rows.substr(0, nl);
            if (!line.empty()) {
                lines.push_back(line);
            }
            rows = nl == std::string_view::npos ? std::string_view{} : rows.substr(nl + 1);
        }
        const auto w = static_cast<std::uint32_t>(lines[0].size());
        const auto h = static_cast<std::uint32_t>(lines.size());
        sim::IslandMap map(w, h);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const char c = lines[y][x];
                const std::uint32_t id = c == '#' ? kRock : c == 's' ? kSquare : kGround;
                if (c == 'g' || (x > 0 && lines[y][x - 1] == 'g') || (x > 1 && lines[y][x - 2] == 'g')) {
                    continue;
                }
                REQUIRE(
                    map.place(*buildings.by_id(id), static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), 0, 0)
                        .ok());
            }
            for (std::uint32_t x = 0; x < w; ++x) {
                if (lines[y][x] == 'g') {
                    REQUIRE(map.place(*buildings.by_id(kGate), static_cast<std::int32_t>(x),
                                      static_cast<std::int32_t>(y), 0, 0)
                                .ok());
                }
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 0, 0}).ok());
    }

    std::vector<sim::DragStep> drag(sim::TilePos from, sim::TilePos to, std::uint8_t rotation = 0,
                                    sim::DragLine line = sim::DragLine::Road) const {
        return sim::drag_path(world, buildings, 0, line, from, to, rotation);
    }
};

using Path = std::vector<sim::DragStep>;

constexpr std::string_view kOpen8 = "........\n........\n........\n........\n........\n........\n........\n........\n";

}  // namespace

TEST_CASE("drag path: straight lines and open land (y first, then x)") {
    const Land land(kOpen8);
    CHECK((land.drag({1, 2}, {5, 2}) == Path{{1, 2, 0}, {2, 2, 0}, {3, 2, 0}, {4, 2, 0}, {5, 2, 0}}));
    CHECK((land.drag({3, 5}, {3, 2}) == Path{{3, 5, 3}, {3, 4, 3}, {3, 3, 3}, {3, 2, 3}}));
    CHECK((land.drag({1, 1}, {4, 3}) == Path{{1, 1, 1}, {1, 2, 1}, {1, 3, 0}, {2, 3, 0}, {3, 3, 0}, {4, 3, 0}}));
    CHECK((land.drag({4, 4}, {2, 2}) == Path{{4, 4, 3}, {4, 3, 3}, {4, 2, 2}, {3, 2, 2}, {2, 2, 2}}));
    CHECK((land.drag({5, 1}, {2, 3}) == Path{{5, 1, 1}, {5, 2, 1}, {5, 3, 2}, {4, 3, 2}, {3, 3, 2}, {2, 3, 2}}));
    // A click: the cursor with the player's rotation.
    CHECK((land.drag({3, 3}, {3, 3}, 2) == Path{{3, 3, 2}}));
}

TEST_CASE("drag path: around obstacles, the northern way wins a tie") {
    const Land detour("...#....\n...#....\n...#....\n...#....\n........\n");
    CHECK((detour.drag({1, 2}, {5, 2}) ==
           Path{{1, 2, 1}, {1, 3, 1}, {1, 4, 0}, {2, 4, 0}, {3, 4, 0}, {4, 4, 3}, {4, 3, 3}, {4, 2, 0}, {5, 2, 0}}));
    const Land tie(".......\n...#...\n...#...\n...#...\n.......\n");
    CHECK((tie.drag({1, 2}, {5, 2}) ==
           Path{{1, 2, 3}, {1, 1, 3}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 1}, {4, 1, 1}, {4, 2, 0}, {5, 2, 0}}));
}

TEST_CASE("drag path: an unreachable or blocked cursor gives the cursor tile alone") {
    const Land blocked("......\n...#..\n......\n");
    CHECK((blocked.drag({0, 1}, {3, 1}, 1) == Path{{3, 1, 1}}));
    const Land pocket(".....#...\n.....#...\n.....#...\n");
    CHECK((pocket.drag({1, 1}, {7, 1}) == Path{{7, 1, 0}}));
    // The search starts on the start tile even when the line cannot take it.
    const Land start(".#....\n......\n");
    CHECK((start.drag({1, 0}, {4, 0}) == Path{{1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}}));
}

TEST_CASE("drag path: at most 257 steps") {
    const Land line(std::string(300, '.') + "\n");
    const Path far = line.drag({0, 0}, {257, 0});
    REQUIRE(far.size() == 258);
    CHECK((far.front() == sim::DragStep{0, 0, 0}));
    CHECK((far.back() == sim::DragStep{257, 0, 0}));
    CHECK((line.drag({0, 0}, {258, 0}, 3) == Path{{258, 0, 3}}));
}

TEST_CASE("drag path: roads pass gates only in the middle, walls never cross squares") {
    const Land gate("#g##\n....\n");
    // Gate at x 1..3 of row 0, its middle tile is x 2. Rock at 0; the road goes through the middle.
    CHECK((gate.drag({2, 1}, {2, 0}) == Path{{2, 1, 3}, {2, 0, 3}}));
    CHECK((gate.drag({1, 1}, {1, 0}, 1) == Path{{1, 0, 1}}));
    const Land square("..s..\n..s..\n.....\n");
    CHECK((square.drag({0, 0}, {4, 0}, 0, sim::DragLine::Road).size() == 5));
    CHECK((square.drag({0, 0}, {4, 0}, 0, sim::DragLine::Wall).size() == 9));
}

TEST_CASE("drag path: only the start tile's city") {
    Land land(kOpen8);
    auto& map = land.world.island(0).map;
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            map.set_city(x, y, 0, 0);
        }
    }
    CHECK((land.drag({1, 1}, {3, 1}).size() == 3));
    CHECK((land.drag({1, 1}, {5, 1}, 2) == Path{{5, 1, 2}}));
    CHECK((land.drag({6, 1}, {4, 1}).size() == 3));
}
