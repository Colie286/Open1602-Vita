// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/resources.hpp"

#include <algorithm>
#include <utility>

#include "o1602/editor/islands.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::editor {

using core::ErrorCode;
using core::make_error;
using Deposit = assets::IslandHeader::Deposit;

namespace {

/// The building drawn on the tile, nullptr for none.
const assets::BuildingType* top_building(const sim::World& world, const assets::BuildingTable& buildings,
                                         std::uint16_t island, sim::TilePos tile) {
    const sim::IslandMap& map = world.islands()[island].map;
    const sim::MapCell& c = map.top(static_cast<std::uint32_t>(tile.x), static_cast<std::uint32_t>(tile.y));
    return c.empty() ? nullptr : buildings.by_sprite(c.sprite);
}

/// The ore mountain covering the tile.
std::optional<sim::BuildingAt> mountain_at(const sim::World& world, const assets::BuildingTable& buildings,
                                           std::uint16_t island, sim::TilePos tile) {
    auto at = sim::building_at(world, buildings, island, tile);
    if (!at || at->type == nullptr || at->type->prod_kind != assets::prod::kOreMountain ||
        at->type->ware != assets::ware::kOreMountain) {
        return std::nullopt;
    }
    return at;
}

bool is_vein(const Deposit& d) noexcept {
    return d.ware == assets::ware::kOre || d.ware == assets::ware::kGold;
}

}  // namespace

ResourceRefusal resource_fits(const sim::World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                              sim::TilePos tile, Resource resource) {
    if (island >= world.islands().size() || !world.islands()[island].map.contains(tile.x, tile.y)) {
        return ResourceRefusal::NoIsland;
    }
    const sim::WorldIsland& isl = world.islands()[island];
    const auto x = static_cast<std::uint16_t>(tile.x);
    const auto y = static_cast<std::uint16_t>(tile.y);
    switch (resource) {
        case Resource::BigOre:
        case Resource::SmallOre:
        case Resource::Gold: {
            const auto mountain = mountain_at(world, buildings, island, tile);
            if (!mountain) {
                return ResourceRefusal::NoMountain;
            }
            if (resource == Resource::BigOre && mountain->type->ore_mountain != 0) {
                return ResourceRefusal::SmallMount;
            }
            const bool taken = std::any_of(
                isl.header.deposits.begin(), isl.header.deposits.end(),
                [&](const Deposit& d) { return is_vein(d) && d.x == mountain->origin.x && d.y == mountain->origin.y; });
            return taken ? ResourceRefusal::Taken : ResourceRefusal::None;
        }
        case Resource::Treasure: {
            const assets::BuildingType* b = top_building(world, buildings, island, tile);
            if (b == nullptr || b->kind != assets::kind::kGround) {
                return ResourceRefusal::NoGround;
            }
            const bool taken = std::any_of(
                isl.header.deposits.begin(), isl.header.deposits.end(),
                [&](const Deposit& d) { return d.ware == assets::ware::kTreasure && d.x == x && d.y == y; });
            return taken ? ResourceRefusal::Taken : ResourceRefusal::None;
        }
        case Resource::Deer: {
            const assets::BuildingType* b = top_building(world, buildings, island, tile);
            if (b == nullptr || b->kind != assets::kind::kForest) {
                return ResourceRefusal::NoForest;
            }
            bool full = true;
            for (const sim::DeerSpot& s : isl.deer_spots) {
                if (s.used && s.x == x && s.y == y) {
                    return ResourceRefusal::Taken;
                }
                full = full && s.used;
            }
            return full ? ResourceRefusal::Full : ResourceRefusal::None;
        }
    }
    return ResourceRefusal::NoIsland;
}

core::Result<std::unique_ptr<Operation>> place_resource(const sim::World& world, const assets::BuildingTable& buildings,
                                                        std::uint16_t island, sim::TilePos tile, Resource resource) {
    const ResourceRefusal refused = resource_fits(world, buildings, island, tile, resource);
    if (refused != ResourceRefusal::None) {
        return make_error(ErrorCode::InvalidFormat, refusal_name(refused));
    }
    sim::World after = world;
    sim::WorldIsland& isl = after.island(island);
    const auto x = static_cast<std::uint16_t>(tile.x);
    const auto y = static_cast<std::uint16_t>(tile.y);
    if (resource == Resource::Deer) {
        for (sim::DeerSpot& s : isl.deer_spots) {
            if (!s.used) {
                s = sim::DeerSpot{true, x, y, 0};
                break;
            }
        }
        return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "resource_place"));
    }
    Deposit d;
    if (resource == Resource::Treasure) {
        d.ware = assets::ware::kTreasure;
        d.x = x;
        d.y = y;
        d.amount = kTreasureAmount;
    } else {
        const auto mountain = mountain_at(world, buildings, island, tile);  // there: resource_fits
        d.ware = resource == Resource::Gold ? assets::ware::kGold : assets::ware::kOre;
        d.x = static_cast<std::uint16_t>(mountain->origin.x);
        d.y = static_cast<std::uint16_t>(mountain->origin.y);
        d.kind = resource == Resource::BigOre ? 0 : 1;
        d.amount = resource == Resource::BigOre     ? kBigOreAmount
                   : resource == Resource::SmallOre ? kSmallOreAmount
                                                    : kGoldAmount;
    }
    isl.header.deposits.push_back(d);
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "resource_place"));
}

core::Result<std::unique_ptr<Operation>> remove_resource(const sim::World& world,
                                                         const assets::BuildingTable& buildings, std::uint16_t island,
                                                         sim::TilePos tile) {
    if (island >= world.islands().size() || !world.islands()[island].map.contains(tile.x, tile.y)) {
        return make_error(ErrorCode::NotFound, "not on an island");
    }
    const auto x = static_cast<std::uint16_t>(tile.x);
    const auto y = static_cast<std::uint16_t>(tile.y);
    sim::World after = world;
    sim::WorldIsland& isl = after.island(island);
    auto& deposits = isl.header.deposits;
    // The treasure on the tile first, then deer, then the vein of the mountain.
    if (const auto it =
            std::find_if(deposits.begin(), deposits.end(),
                         [&](const Deposit& d) { return d.ware == assets::ware::kTreasure && d.x == x && d.y == y; });
        it != deposits.end()) {
        deposits.erase(it);
        return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "resource_remove"));
    }
    for (sim::DeerSpot& s : isl.deer_spots) {
        if (s.used && s.x == x && s.y == y) {
            s = sim::DeerSpot{};
            return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "resource_remove"));
        }
    }
    if (const auto mountain = mountain_at(world, buildings, island, tile)) {
        const auto before = deposits.size();
        std::erase_if(deposits, [&](const Deposit& d) {
            return is_vein(d) && d.x == mountain->origin.x && d.y == mountain->origin.y;
        });
        if (deposits.size() != before) {
            return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "resource_remove"));
        }
    }
    return make_error(ErrorCode::NotFound, "no deposit, treasure or deer there");
}

const char* refusal_name(ResourceRefusal refusal) noexcept {
    switch (refusal) {
        case ResourceRefusal::None:
            return "none";
        case ResourceRefusal::NoIsland:
            return "not on an island";
        case ResourceRefusal::NoMountain:
            return "no ore mountain";
        case ResourceRefusal::SmallMount:
            return "a big vein needs a big mountain";
        case ResourceRefusal::NoGround:
            return "no open ground";
        case ResourceRefusal::NoForest:
            return "no forest";
        case ResourceRefusal::Taken:
            return "there is one already";
        case ResourceRefusal::Full:
            return "the island has 8 deer spots";
    }
    return "unknown";
}

}  // namespace o1602::editor
