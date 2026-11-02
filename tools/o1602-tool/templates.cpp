// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool template-stats: what the original's island templates are made of, as the island
// generator's rules need it: every building id the templates use with its
// kind, size, rotations and how many templates have it; what lies next to rivers, river mouths,
// slopes and rocks; how far rocks lie from the ore mountains; how large the patches of each ground
// are; and a few templates with rivers as maps of letters.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "tool.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/island_templates.hpp"

namespace o1602::tool {

namespace {

namespace kind = assets::kind;

/// The surf before a river mouth; the other ids of its kind (22) are surf along a straight coast.
constexpr std::uint32_t kMouthSurfId = assets::id::island_shape::kMouthSurf;

/// One letter per kind for the maps.
char letter(std::int32_t k) {
    switch (k) {
        case kind::kSea:
            return '~';
        case kind::kSurf:
        case kind::kSurfCorner:
            return ',';
        case kind::kBeach:
        case kind::kBeachOuter:
        case kind::kBeachInner:
        case kind::kBeachVariant:
            return '.';
        case kind::kBeachMouth:
            return 'B';
        case kind::kRiverMouth:
            return 'M';
        case kind::kRiver:
            return 'r';
        case kind::kRiverCorner:
            return 'c';
        case kind::kBridge:
            return '=';
        case kind::kRock:
            return 'R';
        case kind::kForest:
            return 'f';
        case kind::kGround:
            return ' ';
        case kind::kSlope:
            return 's';
        case kind::kSlopeCorner:
            return 'S';
        case kind::kSlopeSpring:
            return 'Q';
        case kind::kMine:
            return 'm';
        default:
            return '#';
    }
}

/// The kinds whose surroundings are counted.
bool watched(std::int32_t k) {
    return k == kind::kRock || k == kind::kRiver || k == kind::kRiverCorner || k == kind::kRiverMouth ||
           k == kind::kBeachMouth || k == kind::kBridge || k == kind::kSlope || k == kind::kSlopeCorner ||
           k == kind::kSlopeSpring;
}

struct IdStats {
    std::int32_t kind = 0;
    std::uint32_t size_x = 1;
    std::uint32_t size_y = 1;
    std::uint64_t tiles = 0;
    std::uint64_t buildings = 0;  ///< origins
    std::array<std::uint64_t, 4> rotations{};
    std::set<std::string> templates;
    std::array<std::uint64_t, 2> by_climate{};
};

}  // namespace

int cmd_template_stats(const Args& args) {
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        return fail(game.error());
    }
    auto buildings = assets::BuildingTable::load(*game);
    if (!buildings) {
        return fail(buildings.error());
    }
    const std::size_t maps = args.number("--count").value_or(4);
    std::map<std::uint32_t, IdStats> ids;
    // Neighbours of watched ids: (id, direction relative to the tile's rotation, neighbour kind).
    std::map<std::tuple<std::uint32_t, std::uint8_t, std::int32_t>, std::uint64_t> around;
    std::map<std::int32_t, std::uint64_t> rock_distance;  // Chebyshev distance to a mountain tile
    std::map<std::uint32_t, std::pair<std::uint64_t, std::uint64_t>> ground_patches;  // id: patches, tiles
    std::uint32_t templates = 0;
    std::uint32_t with_rivers = 0;
    std::size_t printed = 0;
    std::size_t printed_natives = 0;
    std::vector<std::string> warnings;
    for (const bool natives : {false, true}) {
        for (std::uint8_t climate = 0; climate < 2; ++climate) {
            for (std::size_t size = 0; size < sim::templates::kSizeClasses; ++size) {
                for (std::uint16_t number = 0; number < 100; ++number) {
                    const std::string name =
                        sim::templates::template_path(climate, static_cast<std::uint8_t>(size), number, natives);
                    const auto path = game->resolve(name);
                    if (!path) {
                        continue;
                    }
                    auto file = assets::ChunkFile::load(*path);
                    if (!file) {
                        continue;
                    }
                    auto islands = assets::load_islands(*file);
                    if (!islands || islands->empty()) {
                        continue;
                    }
                    const sim::IslandMap map = sim::IslandMap::from_island((*islands)[0], *buildings, &warnings);
                    ++templates;
                    const auto w = static_cast<std::int32_t>(map.width());
                    const auto h = static_cast<std::int32_t>(map.height());
                    std::vector<std::int32_t> kinds(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), -1);
                    std::vector<std::uint32_t> tile_ids(kinds.size(), 0);
                    std::vector<std::uint8_t> rotations(kinds.size(), 0);
                    const auto at = [w](std::int32_t x, std::int32_t y) {
                        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
                    };
                    bool river = false;
                    for (std::int32_t y = 0; y < h; ++y) {
                        for (std::int32_t x = 0; x < w; ++x) {
                            const auto ux = static_cast<std::uint32_t>(x);
                            const auto uy = static_cast<std::uint32_t>(y);
                            const sim::SubTile sub = map.sub_tile(*buildings, ux, uy);
                            if (sub.building == nullptr) {
                                continue;
                            }
                            const assets::BuildingType& b = *sub.building;
                            IdStats& s = ids[b.id];
                            s.kind = b.kind;
                            s.size_x = b.size_x;
                            s.size_y = b.size_y;
                            ++s.tiles;
                            ++s.by_climate[climate];
                            s.templates.insert(name);
                            if (sub.origin_x == x && sub.origin_y == y) {
                                ++s.buildings;
                                ++s.rotations[map.top(ux, uy).rotation & 3U];
                            }
                            kinds[at(x, y)] = b.kind;
                            tile_ids[at(x, y)] = b.id;
                            rotations[at(x, y)] = static_cast<std::uint8_t>(map.top(ux, uy).rotation & 3U);
                            river = river || b.kind == kind::kRiver || b.id == kMouthSurfId;
                        }
                    }
                    with_rivers += river ? 1U : 0U;
                    // Neighbours: N, E, S, W turned by the tile's rotation (0: as laid).
                    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> kSides = {
                        {{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
                    for (std::int32_t y = 0; y < h; ++y) {
                        for (std::int32_t x = 0; x < w; ++x) {
                            const std::int32_t k = kinds[at(x, y)];
                            if (!watched(k)) {
                                continue;
                            }
                            for (std::uint8_t side = 0; side < 4; ++side) {
                                const auto [dx, dy] = kSides[side];
                                const std::int32_t nx = x + dx;
                                const std::int32_t ny = y + dy;
                                const std::int32_t nk =
                                    nx < 0 || ny < 0 || nx >= w || ny >= h ? kind::kSea : kinds[at(nx, ny)];
                                const auto relative = static_cast<std::uint8_t>(
                                    static_cast<unsigned>(side + 4 - rotations[at(x, y)]) & 3U);
                                ++around[{tile_ids[at(x, y)], relative, nk}];
                            }
                            if (k == kind::kRock) {
                                std::int32_t best = 99;
                                for (std::int32_t yy = 0; yy < h; ++yy) {
                                    for (std::int32_t xx = 0; xx < w; ++xx) {
                                        const std::uint32_t id = tile_ids[at(xx, yy)];
                                        const bool mountain = id == 21103 || id == 21107 || id == 21108 || id == 21109;
                                        if (mountain) {
                                            best = std::min(best, std::max(std::abs(xx - x), std::abs(yy - y)));
                                        }
                                    }
                                }
                                ++rock_distance[best];
                            }
                        }
                    }
                    // Patches of one ground id (4-connected).
                    std::vector<bool> seen(kinds.size(), false);
                    for (std::int32_t y = 0; y < h; ++y) {
                        for (std::int32_t x = 0; x < w; ++x) {
                            if (seen[at(x, y)] || kinds[at(x, y)] != kind::kGround) {
                                continue;
                            }
                            const std::uint32_t id = tile_ids[at(x, y)];
                            std::vector<std::pair<std::int32_t, std::int32_t>> stack{{x, y}};
                            seen[at(x, y)] = true;
                            std::uint64_t count = 0;
                            while (!stack.empty()) {
                                const auto [cx, cy] = stack.back();
                                stack.pop_back();
                                ++count;
                                for (const auto& [dx, dy] : kSides) {
                                    const std::int32_t nx = cx + dx;
                                    const std::int32_t ny = cy + dy;
                                    if (nx >= 0 && ny >= 0 && nx < w && ny < h && !seen[at(nx, ny)] &&
                                        kinds[at(nx, ny)] == kind::kGround && tile_ids[at(nx, ny)] == id) {
                                        seen[at(nx, ny)] = true;
                                        stack.emplace_back(nx, ny);
                                    }
                                }
                            }
                            ++ground_patches[id].first;
                            ground_patches[id].second += count;
                        }
                    }
                    // Templates with rivers, and with a native village, as maps; the buildings that
                    // are no terrain (#) listed below the map.
                    const bool show_river = river && printed < maps;
                    const bool show_natives = natives && printed_natives < maps;
                    if (show_river || show_natives) {
                        printed += show_river ? 1U : 0U;
                        printed_natives += show_natives ? 1U : 0U;
                        std::printf(
                            "map %s (%dx%d): ~ sea , surf . beach B beach mouth M surf before a river mouth "
                            "0-3 river, a-d river corner (by rotation) = bridge R rock f forest s/S/Q slope "
                            "m mine # other\n",
                            name.c_str(), w, h);
                        for (std::int32_t y = 0; y < h; ++y) {
                            std::string line;
                            for (std::int32_t x = 0; x < w; ++x) {
                                const std::int32_t k = kinds[at(x, y)];
                                line += k < 0 ? '?' : letter(k);
                                if (k == kind::kRiver || k == kind::kRiverCorner) {
                                    line.back() = static_cast<char>(k == kind::kRiver ? '0' + rotations[at(x, y)]
                                                                                      : 'a' + rotations[at(x, y)]);
                                } else if (k == kind::kRiverMouth && tile_ids[at(x, y)] != kMouthSurfId) {
                                    line.back() = ',';  // straight surf of the same kind
                                }
                            }
                            std::printf("  %s\n", line.c_str());
                        }
                        for (std::int32_t y = 0; y < h; ++y) {
                            for (std::int32_t x = 0; x < w; ++x) {
                                const auto ux = static_cast<std::uint32_t>(x);
                                const auto uy = static_cast<std::uint32_t>(y);
                                const sim::SubTile sub = map.sub_tile(*buildings, ux, uy);
                                if (sub.building != nullptr && sub.origin_x == x && sub.origin_y == y &&
                                    letter(sub.building->kind) == '#') {
                                    std::printf("  building %u kind %d prod %d at %d,%d size %ux%u rotation %u\n",
                                                sub.building->id, sub.building->kind, sub.building->prod_kind, x, y,
                                                sub.building->size_x, sub.building->size_y,
                                                static_cast<unsigned>(map.top(ux, uy).rotation));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    std::printf("templates %u, with rivers %u\n", templates, with_rivers);
    std::printf("\nid kind size tiles buildings templates north south rotations(0/1/2/3)\n");
    for (const auto& [id, s] : ids) {
        std::printf("%u %d %ux%u %llu %llu %zu %llu %llu %llu/%llu/%llu/%llu\n", id, s.kind, s.size_x, s.size_y,
                    static_cast<unsigned long long>(s.tiles), static_cast<unsigned long long>(s.buildings),
                    s.templates.size(), static_cast<unsigned long long>(s.by_climate[0]),
                    static_cast<unsigned long long>(s.by_climate[1]), static_cast<unsigned long long>(s.rotations[0]),
                    static_cast<unsigned long long>(s.rotations[1]), static_cast<unsigned long long>(s.rotations[2]),
                    static_cast<unsigned long long>(s.rotations[3]));
    }
    std::printf("\nneighbours: id side(0 N,1 E,2 S,3 W turned back by the tile's rotation) kind count\n");
    for (const auto& [key, count] : around) {
        std::printf("%u %u %d %llu\n", std::get<0>(key), std::get<1>(key), std::get<2>(key),
                    static_cast<unsigned long long>(count));
    }
    std::printf("\nrock tiles by distance to an ore mountain (99: none on the island)\n");
    for (const auto& [d, count] : rock_distance) {
        std::printf("%d %llu\n", d, static_cast<unsigned long long>(count));
    }
    std::printf("\nground patches: id patches tiles\n");
    for (const auto& [id, p] : ground_patches) {
        std::printf("%u %llu %llu\n", id, static_cast<unsigned long long>(p.first),
                    static_cast<unsigned long long>(p.second));
    }
    return 0;
}

}  // namespace o1602::tool
