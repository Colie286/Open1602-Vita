// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/growth.hpp"

#include <algorithm>
#include <optional>
#include <tuple>
#include <unordered_set>

#include "o1602/sim/construction.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint8_t kCounterMask = 7;  // class counters count 0..7

std::uint64_t key(std::uint16_t island, std::uint16_t x, std::uint16_t y) noexcept {
    return (std::uint64_t{island} << 32) | (std::uint64_t{y} << 16) | x;
}

const assets::BuildingType* tile_record(const World& world, const assets::BuildingTable& buildings,
                                        std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    if (island >= world.islands().size()) {
        return nullptr;
    }
    const IslandMap& map = world.islands()[island].map;
    if (!map.contains(x, y)) {
        return nullptr;
    }
    const MapCell& cell = map.top(x, y);
    return cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
}

/// Replaces the record of a 1x1-or-larger tile, keeping its position in the footprint.
void swap_record(World& world, std::uint16_t island, std::uint16_t x, std::uint16_t y, const assets::BuildingType& from,
                 const assets::BuildingType& to) {
    IslandMap& map = world.island(island).map;
    const MapCell& cell = map.top(x, y);
    const std::uint32_t sub = cell.sprite >= from.gfx ? cell.sprite - from.gfx : 0;
    map.set_sprite(x, y, static_cast<std::uint16_t>(to.gfx + (sub < to.tile_count() ? sub : 0)));
}

}  // namespace

const assets::BuildingType* ripe_record(const assets::BuildingTable& buildings,
                                        const assets::BuildingType& growing) noexcept {
    if (growing.prod_kind != prod::kGrowing || growing.withered) {
        return nullptr;
    }
    const assets::BuildingType* next = buildings.by_record(growing.record + 1);
    return next != nullptr && next->prod_kind == prod::kRipe ? next : nullptr;
}

const assets::BuildingType* growing_record(const assets::BuildingTable& buildings,
                                           const assets::BuildingType& ripe) noexcept {
    if (ripe.prod_kind != prod::kRipe || ripe.record == 0) {
        return nullptr;
    }
    const assets::BuildingType* prev = buildings.by_record(ripe.record - 1);
    return prev != nullptr && prev->prod_kind == prod::kGrowing ? prev : nullptr;
}

const assets::BuildingType* withered_record(const assets::BuildingTable& buildings,
                                            const assets::BuildingType& ripe) noexcept {
    if (ripe.prod_kind != prod::kRipe) {
        return nullptr;
    }
    const assets::BuildingType* next = buildings.by_record(ripe.record + 1);
    return next != nullptr && next->prod_kind == prod::kGrowing && next->withered ? next : nullptr;
}

const assets::BuildingType* ripe_of_withered(const assets::BuildingTable& buildings,
                                             const assets::BuildingType& withered) noexcept {
    if (withered.prod_kind != prod::kGrowing || !withered.withered || withered.record == 0) {
        return nullptr;
    }
    const assets::BuildingType* prev = buildings.by_record(withered.record - 1);
    return prev != nullptr && prev->prod_kind == prod::kRipe ? prev : nullptr;
}

std::uint8_t crop_ware(const assets::BuildingTable& buildings, const assets::BuildingType& b) noexcept {
    if (b.prod_kind == prod::kRipe) {
        return b.ware;
    }
    const assets::BuildingType* ripe = ripe_record(buildings, b);
    return ripe != nullptr ? ripe->ware : 0;
}

std::size_t advance_growth(World& world, const assets::BuildingTable& buildings, std::uint32_t dt_ms) {
    GrowthState& g = world.growth();
    bool ticked = false;
    for (std::size_t c = 0; c < GrowthState::kClasses; ++c) {
        g.time_ms[c] += dt_ms;
        const std::uint32_t period = growth_period_ms(static_cast<std::uint32_t>(c));
        if (g.time_ms[c] >= period) {
            // The original sets the clock to 0 and loses the rest of the step; Open1602 keeps it,
            // so growth does not depend on the step length.
            g.time_ms[c] -= period;
            g.counter[c] = static_cast<std::uint8_t>((g.counter[c] + 1) & kCounterMask);
            ticked = true;
        }
    }
    if (!ticked) {
        return 0;
    }
    std::size_t ripened = 0;
    std::size_t out = 0;
    for (std::size_t i = 0; i < g.entries.size(); ++i) {
        GrowthEntry e = g.entries[i];
        bool keep = true;
        const std::uint8_t counter = g.counter[e.growth_class % GrowthState::kClasses];
        if (e.seen != counter) {
            // At most one stage per tick, even if the entry missed several.
            e.seen = counter;
            const assets::BuildingType* b = tile_record(world, buildings, e.island, e.x, e.y);
            if (b == nullptr || b->prod_kind != prod::kGrowing) {
                keep = false;  // the tile was built over or harvested otherwise
            } else {
                ++e.stage;
                if (e.stage >= b->anim_count) {
                    if (const assets::BuildingType* ripe = ripe_record(buildings, *b)) {
                        swap_record(world, e.island, e.x, e.y, *b, *ripe);
                        world.island(e.island).map.set_anim_phase(e.x, e.y, 0);
                        ++ripened;
                    }
                    keep = false;  // withered tiles stay withered
                } else if (b->anim_time == 0) {
                    // The stage is the picture: Gfx + phase * AnimAdd.
                    world.island(e.island).map.set_anim_phase(e.x, e.y, e.stage);
                }
            }
        }
        if (keep) {
            g.entries[out++] = e;
        }
    }
    g.entries.resize(out);
    return ripened;
}

std::optional<GrowthEntry> missing_growth_entry(const GrowthState& growth, const assets::BuildingTable& buildings,
                                                std::uint16_t island, std::uint16_t x, std::uint16_t y,
                                                const MapCell& cell) {
    const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
    if (b == nullptr || b->prod_kind != prod::kGrowing || b->withered) {
        return std::nullopt;
    }
    GrowthEntry e;
    e.island = island;
    e.x = x;
    e.y = y;
    e.growth_class = b->growth_class;
    e.seen = growth.counter[b->growth_class % GrowthState::kClasses];
    e.stage = static_cast<std::uint8_t>(b->anim_time == 0 ? cell.anim_phase : 0);
    return e;
}

void add_missing_growth(World& world, const assets::BuildingTable& buildings) {
    GrowthState& g = world.growth();
    std::unordered_set<std::uint64_t> present;
    present.reserve(g.entries.size());
    for (const GrowthEntry& e : g.entries) {
        present.insert(key(e.island, e.x, e.y));
    }
    for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
        const IslandMap& map = world.islands()[i].map;
        for (std::uint16_t y = 0; y < map.height(); ++y) {
            for (std::uint16_t x = 0; x < map.width(); ++x) {
                if (present.contains(key(i, x, y))) {
                    continue;
                }
                if (const auto e = missing_growth_entry(g, buildings, i, x, y, map.top(x, y))) {
                    g.entries.push_back(*e);
                }
            }
        }
    }
}

bool harvest_tile(World& world, const assets::BuildingTable& buildings, std::uint16_t island, std::uint16_t x,
                  std::uint16_t y, std::uint8_t ware) {
    const assets::BuildingType* ripe = tile_record(world, buildings, island, x, y);
    if (ripe == nullptr || ripe->prod_kind != prod::kRipe || ripe->ware != ware) {
        return false;
    }
    const assets::BuildingType* growing = growing_record(buildings, *ripe);
    if (growing == nullptr) {
        return false;
    }
    // During a drought a field that is not fertile at its strength withers. Only fields and
    // plantations have a withered record; the original turned trees and grass on poor ground into
    // the next variant.
    if (world.islands()[island].drought.strength != 0) {
        if (const assets::BuildingType* withered = withered_record(buildings, *ripe);
            withered != nullptr && !tile_fertile(world, buildings, island, ware, x, y)) {
            swap_record(world, island, x, y, *ripe, *withered);
            if (withered->anim_time == 0) {
                world.island(island).map.set_anim_phase(x, y, 0);
            }
            return true;
        }
    }
    swap_record(world, island, x, y, *ripe, *growing);
    if (growing->anim_time == 0) {
        world.island(island).map.set_anim_phase(x, y, 0);
    }
    GrowthState& g = world.growth();
    GrowthEntry e;
    e.island = island;
    e.x = x;
    e.y = y;
    e.growth_class = growing->growth_class;  // without the random 0..2 of a newly planted tile
    e.seen = g.counter[growing->growth_class % GrowthState::kClasses];
    e.stage = 0;
    g.entries.push_back(e);
    return true;
}

namespace {

void forget_growth(World& world, std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    std::erase_if(world.growth().entries,
                  [&](const GrowthEntry& e) { return e.island == island && e.x == x && e.y == y; });
}

}  // namespace

void forget_growth(World& world, std::uint16_t island, std::vector<TilePos>& tiles, std::size_t first_new) {
    if (tiles.empty()) {
        return;
    }
    const auto before = [](const TilePos& a, const TilePos& b) { return std::tie(a.y, a.x) < std::tie(b.y, b.x); };
    std::sort(tiles.begin(), tiles.end(), before);
    auto& entries = world.growth().entries;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const GrowthEntry& e = entries[i];
        const bool gone = i < first_new && e.island == island &&
                          std::binary_search(tiles.begin(), tiles.end(), TilePos{e.x, e.y}, before);
        if (!gone) {
            entries[kept++] = e;
        }
    }
    entries.resize(kept);
}

bool wither_tile(World& world, const assets::BuildingTable& buildings, std::uint16_t island, std::uint16_t x,
                 std::uint16_t y, std::vector<TilePos>* forgotten) {
    const assets::BuildingType* ripe = tile_record(world, buildings, island, x, y);
    const assets::BuildingType* withered = ripe != nullptr ? withered_record(buildings, *ripe) : nullptr;
    if (withered == nullptr) {
        return false;
    }
    swap_record(world, island, x, y, *ripe, *withered);
    if (withered->anim_time == 0) {
        world.island(island).map.set_anim_phase(x, y, 0);
    }
    // The original gives the withered field a growth entry that ends where it started (a withered
    // field stays withered); Open1602 keeps none.
    if (forgotten != nullptr) {
        forgotten->push_back({x, y});
    } else {
        forget_growth(world, island, x, y);
    }
    return true;
}

bool recover_tile(World& world, const assets::BuildingTable& buildings, std::uint16_t island, std::uint16_t x,
                  std::uint16_t y, std::vector<TilePos>* forgotten) {
    const assets::BuildingType* withered = tile_record(world, buildings, island, x, y);
    const assets::BuildingType* ripe = withered != nullptr ? ripe_of_withered(buildings, *withered) : nullptr;
    const assets::BuildingType* growing = ripe != nullptr ? growing_record(buildings, *ripe) : nullptr;
    if (growing == nullptr) {
        return false;
    }
    swap_record(world, island, x, y, *withered, *growing);
    if (growing->anim_time == 0) {
        world.island(island).map.set_anim_phase(x, y, 0);
    }
    if (forgotten != nullptr) {
        forgotten->push_back({x, y});
    } else {
        forget_growth(world, island, x, y);
    }
    GrowthState& g = world.growth();
    GrowthEntry e;
    e.island = island;
    e.x = x;
    e.y = y;
    e.growth_class = growing->growth_class;
    e.seen = g.counter[growing->growth_class % GrowthState::kClasses];
    e.stage = 0;
    g.entries.push_back(e);
    return true;
}

}  // namespace o1602::sim
