// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/random_elements.hpp"

#include <algorithm>
#include <utility>

#include "o1602/assets/game_data.hpp"
#include "o1602/editor/islands.hpp"
#include "o1602/sim/world_generator.hpp"

namespace o1602::editor {

using core::ErrorCode;
using core::make_error;
using Kind = RandomElement::Kind;

namespace {

std::uint8_t ware_of(const assets::BuildingTable& buildings, std::uint32_t building) {
    const assets::BuildingType* b = buildings.by_id(building);
    return b != nullptr ? b->ware : std::uint8_t{0};
}

/// The deposits of `ware` (and, for iron ore, of vein `type`) on the map's islands.
std::uint32_t deposits_of(const sim::World& world, std::uint8_t ware, std::uint32_t type) {
    std::uint32_t n = 0;
    for (const sim::WorldIsland& island : world.islands()) {
        for (const auto& d : island.header.deposits) {
            n += d.ware == ware && (ware != assets::ware::kOre || d.kind == type) ? 1U : 0U;
        }
    }
    return n;
}

/// The islands fertile for `ware`.
std::uint32_t fertile_for(const sim::World& world, std::uint8_t ware) {
    if (ware < assets::ware::kFirstField || ware >= assets::ware::kFirstField + 32) {
        return 0;
    }
    const std::uint32_t bit = 1U << static_cast<unsigned>(ware - assets::ware::kFirstField);
    return static_cast<std::uint32_t>(
        std::count_if(world.islands().begin(), world.islands().end(),
                      [&](const sim::WorldIsland& i) { return (i.header.fertility & bit) != 0; }));
}

/// The natives' villages on islands of `climate` (0 north, 1 south).
std::uint32_t villages_in(const sim::World& world, std::size_t climate) {
    std::uint32_t n = 0;
    for (const sim::City& c : world.cities()) {
        if (c.island < world.islands().size() && world.acts_as(c.owner, sim::FactionBehaviour::Natives) &&
            world.islands()[c.island].header.climate % 2U == climate) {
            ++n;
        }
    }
    return n;
}

}  // namespace

std::vector<RandomElement> random_elements(const sim::World& world, const assets::BuildingTable& buildings) {
    std::vector<RandomElement> out;
    const auto& planned = world.planned_islands();
    if (!planned) {
        return out;
    }
    const assets::ScenarioPlan& plan = planned->plan;
    for (std::size_t c = 0; c < plan.natives.size(); ++c) {
        out.push_back(RandomElement{Kind::Natives, c, 0, 0, plan.natives[c], villages_in(world, c)});
    }
    for (std::size_t i = 0; i < plan.ores.size(); ++i) {
        const auto& r = plan.ores[i];
        if (r.building != 0) {
            const std::uint8_t ware = ware_of(buildings, r.building);
            out.push_back(RandomElement{Kind::Ore, i, ware, r.type, r.count, deposits_of(world, ware, r.type)});
        }
    }
    for (std::size_t i = 0; i < plan.crops.size(); ++i) {
        const auto& r = plan.crops[i];
        if (r.building != 0) {
            const std::uint8_t ware = ware_of(buildings, r.building);
            out.push_back(RandomElement{Kind::Crop, i, ware, 0, r.count, fertile_for(world, ware)});
        }
    }
    for (std::size_t i = 0; i < plan.treasures.size(); ++i) {
        const auto& r = plan.treasures[i];
        if (r.building != 0) {
            const std::uint8_t ware = ware_of(buildings, r.building);
            out.push_back(
                RandomElement{Kind::Treasure, i, ware, 0, r.count, deposits_of(world, assets::ware::kTreasure, 0)});
        }
    }
    return out;
}

core::Result<std::unique_ptr<Operation>> set_random_count(const sim::World& world, Kind kind, std::size_t slot,
                                                          std::uint32_t count) {
    if (!world.planned_islands()) {
        return make_error(ErrorCode::NotFound, "the world has no plan");
    }
    if (count > kMaxRandom) {
        return make_error(ErrorCode::InvalidFormat, "at most " + std::to_string(kMaxRandom) + " at random");
    }
    sim::PlannedIslands after = *world.planned_islands();
    assets::ScenarioPlan& plan = after.plan;
    const auto entry = [&](auto& list) -> assets::ScenarioPlan::Resource* {
        return slot < list.size() && list[slot].building != 0 ? &list[slot] : nullptr;
    };
    if (kind == Kind::Natives) {
        if (slot >= plan.natives.size()) {
            return make_error(ErrorCode::NotFound, "no such climate");
        }
        plan.natives[slot] = count;
    } else {
        assets::ScenarioPlan::Resource* r = kind == Kind::Ore    ? entry(plan.ores)
                                            : kind == Kind::Crop ? entry(plan.crops)
                                                                 : entry(plan.treasures);
        if (r == nullptr) {
            return make_error(ErrorCode::NotFound, "the plan names nothing there");
        }
        r->count = static_cast<std::uint16_t>(count);
    }
    return std::unique_ptr<Operation>(
        std::make_unique<PlanEdit>(world.planned_islands(), std::move(after), "random_count"));
}

}  // namespace o1602::editor
