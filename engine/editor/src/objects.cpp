// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/objects.hpp"

#include <utility>

#include "o1602/editor/islands.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::editor {

using core::ErrorCode;
using core::make_error;

namespace {

/// The tiles of `at` show their ground again; false if they show it already (terrain, not built).
bool clear_to_ground(sim::IslandMap& map, const sim::BuildingAt& at) {
    bool cleared = false;
    for (std::uint32_t j = 0; j < at.height; ++j) {
        for (std::uint32_t i = 0; i < at.width; ++i) {
            const auto x = static_cast<std::uint32_t>(at.origin.x) + i;
            const auto y = static_cast<std::uint32_t>(at.origin.y) + j;
            if (x >= map.width() || y >= map.height()) {
                continue;
            }
            const sim::MapCell& top = map.top(x, y);
            const sim::MapCell& ground = map.ground(x, y);
            if (top.sprite != ground.sprite || top.rotation != ground.rotation) {
                map.restore_ground(x, y);
                cleared = true;
            }
        }
    }
    return cleared;
}

}  // namespace

PlacedBuildings place_buildings(const sim::World& world, const assets::BuildingTable& buildings, sim::BuildOrder order,
                                const std::vector<std::string>& city_names) {
    order.free = true;
    PlacedBuildings result;
    result.plan = sim::plan_build(world, buildings, order);
    if (result.plan.placed == 0) {
        return result;
    }
    sim::World after = world;
    result.plan = sim::execute_build(after, buildings, order, city_names, nullptr);
    if (result.plan.placed > 0) {
        result.step = std::make_unique<WorldEdit>(std::move(after), "building_place");
    }
    return result;
}

core::Result<std::unique_ptr<Operation>> remove_building(const sim::World& world,
                                                         const assets::BuildingTable& buildings, std::uint16_t island,
                                                         sim::TilePos tile) {
    if (island >= world.islands().size()) {
        return make_error(ErrorCode::NotFound, "no such island");
    }
    const auto at = sim::building_at(world, buildings, island, tile);
    if (!at) {
        return make_error(ErrorCode::NotFound, "no building there");
    }
    const sim::IslandMap& map = world.islands()[island].map;
    const sim::MapCell& cell =
        map.top(static_cast<std::uint32_t>(at->origin.x), static_cast<std::uint32_t>(at->origin.y));
    const std::int32_t k = at->type->kind;
    if ((at->city == sim::MapCell::kNoCity || world.city(island, at->city) == nullptr) &&
        (k == assets::kind::kPier || k == assets::kind::kRuin || k == assets::kind::kBeachRuin)) {
        // A pier or ruin outside every city (left by a Kontor taken away in an older map): the
        // ground under it comes back.
        sim::World after = world;
        if (!clear_to_ground(after.island(island).map, *at)) {
            return make_error(ErrorCode::Unsupported, "the building cannot be taken away");
        }
        return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "building_remove"));
    }
    if (at->city == sim::MapCell::kNoCity || world.city(island, at->city) == nullptr) {
        return make_error(ErrorCode::NotFound, "no building there");  // terrain
    }
    const bool kontor = at->type->kind == assets::kind::kHq;
    sim::DemolishOrder order;
    order.player = cell.owner;
    order.island = island;
    order.from = at->origin;
    order.to = at->origin;
    if (sim::plan_demolish(world, buildings, order).empty()) {
        return make_error(ErrorCode::Unsupported, "the building cannot be taken away");
    }
    sim::World after = world;
    const sim::DemolishResult removed = sim::execute_demolish(after, buildings, order, nullptr);
    if (removed.removed == 0) {
        return make_error(ErrorCode::Unsupported, "the building cannot be taken away");
    }
    if (kontor) {
        // With the city gone, what was left of it (its piers, roads) fell to ruins: in the editor
        // the ground comes back there instead, and the piers in front of it go too.
        sim::IslandMap& m = after.island(island).map;
        const sim::IslandMap& was = world.islands()[island].map;
        const auto ruin = [&](const sim::IslandMap& in, std::uint32_t x, std::uint32_t y) {
            const assets::BuildingType* b = buildings.by_sprite(in.top(x, y).sprite);
            return b != nullptr && (b->kind == assets::kind::kRuin || b->kind == assets::kind::kBeachRuin);
        };
        for (std::uint32_t y = 0; y < m.height(); ++y) {
            for (std::uint32_t x = 0; x < m.width(); ++x) {
                const sim::MapCell& top = m.top(x, y);
                const bool pier = [&] {
                    const assets::BuildingType* b = buildings.by_sprite(top.sprite);
                    return b != nullptr && b->kind == assets::kind::kPier;
                }();
                if (top.city != sim::MapCell::kNoCity && after.city(island, top.city) != nullptr) {
                    continue;
                }
                if ((ruin(m, x, y) && !ruin(was, x, y)) || pier) {
                    const sim::MapCell& ground = m.ground(x, y);
                    if (top.sprite != ground.sprite || top.rotation != ground.rotation) {
                        m.restore_ground(x, y);
                    }
                }
            }
        }
    }
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "building_remove"));
}

}  // namespace o1602::editor
