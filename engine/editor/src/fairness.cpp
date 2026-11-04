// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/fairness.hpp"

#include <algorithm>
#include <iterator>
#include <optional>

#include "o1602/assets/game_data.hpp"
#include "o1602/sim/new_game.hpp"

namespace o1602::editor {

namespace {

std::uint32_t distance(std::uint32_t ax, std::uint32_t ay, std::uint32_t bx, std::uint32_t by) {
    const std::uint32_t dx = ax > bx ? ax - bx : bx - ax;
    const std::uint32_t dy = ay > by ? ay - by : by - ay;
    return std::max(dx, dy) + std::min(dx, dy) / 4;
}

/// How far the island's rectangle is from the tile (the longer axis; 0 inside it).
std::uint32_t reach(const sim::WorldIsland& island, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t x1 = island.x + island.map.width() - 1;
    const std::uint32_t y1 = island.y + island.map.height() - 1;
    const std::uint32_t dx = x < island.x ? island.x - x : x > x1 ? x - x1 : 0;
    const std::uint32_t dy = y < island.y ? island.y - y : y > y1 ? y - y1 : 0;
    return std::max(dx, dy);
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> start_of(const sim::World& world, sim::PlayerId p) {
    for (const sim::City& c : world.cities()) {
        if (c.owner == p && c.warehouse_x && c.warehouse_y && c.island < world.islands().size()) {
            const sim::WorldIsland& island = world.islands()[c.island];
            return std::pair{island.x + *c.warehouse_x, island.y + *c.warehouse_y};
        }
    }
    for (const sim::Ship& s : world.ships()) {
        // A ship at sea, not a trade cart (which the ships' list holds too) nor one off the map.
        if (s.owner == p && !s.cart() && !s.sinking && s.x < world.width() && s.y < world.height()) {
            return std::pair{std::uint32_t{s.x}, std::uint32_t{s.y}};
        }
    }
    return std::nullopt;
}

}  // namespace

std::vector<StartArea> start_areas(const sim::World& world, const assets::BuildingTable& buildings) {
    std::vector<StartArea> areas;
    for (std::size_t i = 0; i < world.players().size(); ++i) {
        const sim::Player& p = world.players()[i];
        const bool plays = (p.role == sim::PlayerRole::Human || p.role == sim::PlayerRole::Computer) &&
                           p.status != sim::PlayerStatus::Absent;
        const auto id = static_cast<sim::PlayerId>(i);
        if (!plays) {
            continue;
        }
        if (const auto at = start_of(world, id)) {
            StartArea a;
            a.player = id;
            a.x = at->first;
            a.y = at->second;
            areas.push_back(a);
        }
    }
    if (areas.empty()) {
        return areas;
    }
    // The land of each island once, from the world's tiles.
    const sim::WorldPreview preview = sim::preview_of(world, buildings);
    std::vector<std::uint64_t> land(world.islands().size(), 0);
    for (std::uint32_t y = 0; y < preview.height; ++y) {
        for (std::uint32_t x = 0; x < preview.width; ++x) {
            const sim::World::Cell& c = world.cell(x, y);
            const sim::WorldPreview::Tile t = preview.at(x, y);
            if (c.island != sim::World::kSea && c.island < land.size() && t != sim::WorldPreview::Tile::Sea &&
                t != sim::WorldPreview::Tile::Surf) {
                ++land[c.island];
            }
        }
    }
    for (StartArea& a : areas) {
        for (std::size_t i = 0; i < world.islands().size(); ++i) {
            const sim::WorldIsland& island = world.islands()[i];
            if (reach(island, a.x, a.y) > kStartReach) {
                continue;
            }
            ++a.islands;
            a.land += land[i];
            for (std::size_t c = 0; c < kStartCrops; ++c) {
                const auto ware = static_cast<std::uint32_t>(assets::ware::kTobaccoField + c);
                if ((island.header.fertility >> (ware - 45U) & 1U) != 0) {
                    ++a.crops[c];
                }
            }
            for (const auto& d : island.header.deposits) {
                a.ore += d.ware == assets::ware::kOre ? 1U : 0U;
                a.gold += d.ware == assets::ware::kGold ? 1U : 0U;
            }
        }
        for (const StartArea& b : areas) {
            if (b.player != a.player) {
                const std::uint32_t d = distance(a.x, a.y, b.x, b.y);
                a.rival = a.rival == 0 ? d : std::min(a.rival, d);
            }
        }
    }
    return areas;
}

std::vector<Unfairness> unfairness(const std::vector<StartArea>& all) {
    std::vector<Unfairness> out;
    // A start at sea with no land in reach (a fleet the mission gives) has nothing to compare.
    std::vector<StartArea> areas;
    std::copy_if(all.begin(), all.end(), std::back_inserter(areas), [](const StartArea& a) { return a.islands > 0; });
    if (areas.size() < 2) {
        return out;
    }
    std::uint64_t most = 0;
    std::array<bool, kStartCrops> somebody{};
    bool ore = false;
    bool gold = false;
    for (const StartArea& a : areas) {
        most = std::max(most, a.land);
        for (std::size_t c = 0; c < kStartCrops; ++c) {
            somebody[c] = somebody[c] || a.crops[c] != 0;
        }
        ore = ore || a.ore != 0;
        gold = gold || a.gold != 0;
    }
    for (const StartArea& a : areas) {
        if (a.land * 100 < most * kFairLandPercent) {
            out.push_back(Unfairness{Unfairness::Kind::Land, a.player});
        }
        for (std::size_t c = 0; c < kStartCrops; ++c) {
            if (somebody[c] && a.crops[c] == 0) {
                out.push_back(Unfairness{Unfairness::Kind::Crop, a.player, c});
            }
        }
        if (ore && a.ore == 0) {
            out.push_back(Unfairness{Unfairness::Kind::Ore, a.player});
        }
        if (gold && a.gold == 0) {
            out.push_back(Unfairness{Unfairness::Kind::Gold, a.player});
        }
    }
    return out;
}

}  // namespace o1602::editor
