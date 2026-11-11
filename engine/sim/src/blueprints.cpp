// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/blueprints.hpp"

#include <algorithm>

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::sim {

namespace {

bool in_rect(const BlueprintFootprint& f, TilePos a, TilePos b) noexcept {
    const std::int32_t x0 = std::min(a.x, b.x);
    const std::int32_t x1 = std::max(a.x, b.x);
    const std::int32_t y0 = std::min(a.y, b.y);
    const std::int32_t y1 = std::max(a.y, b.y);
    return f.x <= x1 && f.y <= y1 && f.x + static_cast<std::int32_t>(f.w) - 1 >= x0 &&
           f.y + static_cast<std::int32_t>(f.h) - 1 >= y0;
}

bool overlaps(const BlueprintFootprint& a, const BlueprintFootprint& b) noexcept {
    return in_rect(a, {b.x, b.y}, {b.x + static_cast<std::int32_t>(b.w) - 1, b.y + static_cast<std::int32_t>(b.h) - 1});
}

std::size_t count_of(const World& world, PlayerId player) {
    return static_cast<std::size_t>(std::count_if(world.blueprints().begin(), world.blueprints().end(),
                                                  [&](const Blueprint& b) { return b.player == player; }));
}

std::size_t place(World& world, const assets::BuildingTable& buildings, const BlueprintOrder& order) {
    BuildOrder build;
    build.player = order.player;
    build.island = order.island;
    build.orient_to_road = false;
    build.items = order.items;
    const BuildPlan plan = plan_blueprints(world, buildings, build);
    std::vector<Blueprint>& list = world.blueprints();
    std::size_t count = count_of(world, order.player);
    std::size_t placed = 0;
    for (std::size_t i = 0; i < plan.items.size() && count < kMaxBlueprints; ++i) {
        const PlannedBuild& p = plan.items[i];
        if (!p.ok() || p.type == nullptr) {
            continue;
        }
        // The record ordered, not the one it becomes on the ground (a ripe crop, a beach wall):
        // building it again decides that.
        const Blueprint b{order.player, order.island, order.items[i].building, p.x, p.y, p.rotation, p.anim_phase};
        const auto f = blueprint_footprint(buildings, b);
        if (!f) {
            continue;
        }
        // An older blueprint of the player under it gives way.
        std::erase_if(list, [&](const Blueprint& old) {
            if (old.player != order.player || old.island != order.island) {
                return false;
            }
            const auto of = blueprint_footprint(buildings, old);
            const bool gone = of && overlaps(*of, *f);
            count -= gone ? 1 : 0;
            return gone;
        });
        list.push_back(b);
        ++count;
        ++placed;
    }
    return placed;
}

std::size_t remove(World& world, const assets::BuildingTable& buildings, const BlueprintOrder& order) {
    return std::erase_if(world.blueprints(), [&](const Blueprint& b) {
        if (b.player != order.player || b.island != order.island) {
            return false;
        }
        const auto f = blueprint_footprint(buildings, b);
        return !f || in_rect(*f, order.from, order.to);
    });
}

/// Builds the given blueprints (indices into World::blueprints(), in placement order), one order
/// per city; what was built leaves the list through the Placed events (drop_covered_blueprints).
std::size_t build(World& world, const assets::BuildingTable& buildings, PlayerId player, std::uint16_t island,
                  const std::vector<std::size_t>& chosen, const std::vector<std::string>& city_names,
                  std::vector<ConstructionEvent>* events) {
    std::vector<std::uint8_t> cities;
    std::vector<std::vector<Blueprint>> by_city;
    for (const std::size_t i : chosen) {
        const Blueprint& b = world.blueprints()[i];
        const std::uint8_t city = blueprint_city(world, b);
        auto at = std::find(cities.begin(), cities.end(), city);
        if (at == cities.end()) {
            cities.push_back(city);
            by_city.emplace_back();
            at = cities.end() - 1;
        }
        by_city[static_cast<std::size_t>(at - cities.begin())].push_back(b);
    }
    std::size_t built = 0;
    for (const std::vector<Blueprint>& group : by_city) {
        BuildOrder order;
        order.player = player;
        order.island = island;
        order.orient_to_road = false;
        for (const Blueprint& b : group) {
            order.items.push_back(BuildItem{b.building, b.x, b.y, b.rotation, b.anim_phase});
        }
        std::vector<ConstructionEvent> own;
        std::vector<ConstructionEvent>* sink = events != nullptr ? events : &own;
        const std::size_t first = sink->size();
        const BuildPlan plan = execute_build(world, buildings, order, city_names, sink);
        built += plan.placed;
        drop_covered_blueprints(world, buildings, std::span(*sink).subspan(first));
    }
    return built;
}

std::vector<std::size_t> chosen_by(const World& world, const assets::BuildingTable& buildings,
                                   const BlueprintOrder& order) {
    std::vector<std::size_t> chosen;
    const std::vector<Blueprint>& list = world.blueprints();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Blueprint& b = list[i];
        if (b.player != order.player || b.island != order.island) {
            continue;
        }
        if (order.city != MapCell::kNoCity) {
            if (blueprint_city(world, b) == order.city) {
                chosen.push_back(i);
            }
        } else if (const auto f = blueprint_footprint(buildings, b); f && in_rect(*f, order.from, order.to)) {
            chosen.push_back(i);
        }
    }
    return chosen;
}

}  // namespace

BuildPlan plan_blueprints(const World& world, const assets::BuildingTable& buildings, const BuildOrder& order) {
    BuildPlan plan;
    plan_blueprint(world, buildings, order, plan);
    for (std::size_t i = 0; i < plan.items.size(); ++i) {
        PlannedBuild& p = plan.items[i];
        const assets::BuildingType* t = buildings.by_id(order.items[i].building);
        if (p.ok() && (t == nullptr || t->kind == kind::kHq || plan.city == MapCell::kNoCity)) {
            p.refusal = BuildRefusal::Territory;  // no Kontor, and only in an own city
        }
    }
    return plan;
}

std::size_t execute_blueprints(World& world, const assets::BuildingTable& buildings, const BlueprintOrder& order,
                               const std::vector<std::string>& city_names, std::vector<ConstructionEvent>* events) {
    if (order.island >= world.islands().size() || world.player(order.player) == nullptr) {
        return 0;
    }
    switch (order.kind) {
        case BlueprintOrder::Kind::Place:
            return place(world, buildings, order);
        case BlueprintOrder::Kind::Remove:
            return remove(world, buildings, order);
        case BlueprintOrder::Kind::Build:
            return build(world, buildings, order.player, order.island, chosen_by(world, buildings, order), city_names,
                         events);
        case BlueprintOrder::Kind::Auto: {
            const City* city = world.city(order.island, order.city);
            if (city == nullptr || city->owner != order.player) {
                return 0;
            }
            std::vector<BlueprintAuto>& autos = world.blueprint_auto();
            const BlueprintAuto entry{order.player, order.island, order.city};
            const auto at = std::find(autos.begin(), autos.end(), entry);
            if (order.on && at == autos.end()) {
                autos.push_back(entry);
                return 1;
            }
            if (!order.on && at != autos.end()) {
                autos.erase(at);
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

void drop_covered_blueprints(World& world, const assets::BuildingTable& buildings,
                             std::span<const ConstructionEvent> events) {
    if (world.blueprints().empty()) {
        return;
    }
    for (const ConstructionEvent& e : events) {
        if (e.kind != ConstructionEvent::Kind::Placed || e.type == nullptr) {
            continue;
        }
        BlueprintFootprint placed{e.x, e.y, 1, 1};
        rotated_size(*e.type, 0, placed.w, placed.h);
        if (const auto at = building_at(world, buildings, e.island, {e.x, e.y});
            at && at->origin == TilePos{e.x, e.y}) {
            placed.w = at->width;
            placed.h = at->height;
        }
        std::erase_if(world.blueprints(), [&](const Blueprint& b) {
            if (b.island != e.island) {
                return false;
            }
            const auto f = blueprint_footprint(buildings, b);
            return !f || overlaps(*f, placed);
        });
    }
}

std::size_t build_auto_blueprints(World& world, const assets::BuildingTable& buildings,
                                  const std::vector<std::string>& city_names, std::vector<ConstructionEvent>* events) {
    std::size_t built = 0;
    // A copy: building may dissolve cities and change the list.
    const std::vector<BlueprintAuto> autos = world.blueprint_auto();
    for (const BlueprintAuto& a : autos) {
        const City* city = world.city(a.island, a.city);
        if (city == nullptr || city->owner != a.player) {
            std::erase(world.blueprint_auto(), a);  // the city is gone or changed hands
            continue;
        }
        BlueprintOrder order;
        order.player = a.player;
        order.kind = BlueprintOrder::Kind::Build;
        order.island = a.island;
        order.city = a.city;
        const std::vector<std::size_t> chosen = chosen_by(world, buildings, order);
        if (!chosen.empty()) {
            built += build(world, buildings, a.player, a.island, chosen, city_names, events);
        }
    }
    return built;
}

std::uint8_t blueprint_city(const World& world, const Blueprint& blueprint) noexcept {
    if (blueprint.island >= world.islands().size()) {
        return MapCell::kNoCity;
    }
    const IslandMap& map = world.islands()[blueprint.island].map;
    if (!map.contains(blueprint.x, blueprint.y)) {
        return MapCell::kNoCity;
    }
    return map.top(static_cast<std::uint32_t>(blueprint.x), static_cast<std::uint32_t>(blueprint.y)).city;
}

std::optional<BlueprintFootprint> blueprint_footprint(const assets::BuildingTable& buildings,
                                                      const Blueprint& blueprint) noexcept {
    const assets::BuildingType* t = buildings.by_id(blueprint.building);
    if (t == nullptr) {
        return std::nullopt;
    }
    BlueprintFootprint f{blueprint.x, blueprint.y, 1, 1};
    rotated_size(*t, blueprint.rotation, f.w, f.h);
    return f;
}

std::optional<std::size_t> blueprint_at(const World& world, const assets::BuildingTable& buildings, PlayerId player,
                                        std::uint16_t island, TilePos tile) {
    const std::vector<Blueprint>& list = world.blueprints();
    for (std::size_t i = list.size(); i-- > 0;) {
        const Blueprint& b = list[i];
        if (b.player != player || b.island != island) {
            continue;
        }
        if (const auto f = blueprint_footprint(buildings, b); f && f->covers(tile.x, tile.y)) {
            return i;
        }
    }
    return std::nullopt;
}

bool blueprint_auto(const World& world, PlayerId player, std::uint16_t island, std::uint8_t city) noexcept {
    const BlueprintAuto entry{player, island, city};
    return std::find(world.blueprint_auto().begin(), world.blueprint_auto().end(), entry) !=
           world.blueprint_auto().end();
}

BuildBill blueprint_costs(const World& world, const assets::BuildingTable& buildings, PlayerId player,
                          std::uint16_t island, std::uint8_t city) {
    BuildBill bill;
    for (const Blueprint& b : world.blueprints()) {
        if (b.player != player || b.island != island || blueprint_city(world, b) != city) {
            continue;
        }
        if (const assets::BuildingType* t = buildings.by_id(b.building)) {
            bill.add(t->build_cost);
        }
    }
    return bill;
}

}  // namespace o1602::sim
