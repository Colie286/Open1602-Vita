// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's objects (ADR 0013): buildings placed for a player without costs and taken away
// again. A building founds a city, claims land and registers with the simulation's lists as when a
// player builds it, so these operations keep the whole world as it was before for undo
// (WorldEdit), like the islands'.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/core/result.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// What placing buildings gives: the step (none if nothing could be built) and what became of
/// each item, for the refusal to show.
struct PlacedBuildings {
    std::unique_ptr<Operation> step;
    sim::BuildPlan plan;
};

/// Builds `order` for its player as the game builds it, but free (BuildOrder::free): no costs, no
/// locks, no limits, and a treasure under it stays. Checked like a player's order otherwise: the
/// terrain, the player's land (a Kontor founds a city on an island without one of the player's),
/// coasts, rivers and rock faces. New cities take the next name of `city_names`, or `own_name` if
/// one is given (the natives' and pirates': "Eingeborenendorf", "Eingeborenendorf 2", ...), which
/// leaves the list's turn as it was. An owner founds one city on an island, the natives and the
/// pirates as well (CityLimit).
[[nodiscard]] PlacedBuildings place_buildings(const sim::World& world, const assets::BuildingTable& buildings,
                                              sim::BuildOrder order, const std::vector<std::string>& city_names,
                                              std::string_view own_name = {});

/// A plan that founds a city for an owner who has one on the island already (the natives and
/// pirates, whom the game does not hold to one): its items refused (CityLimit). True if so.
bool refuse_second_city(const sim::World& world, const sim::BuildOrder& order, sim::BuildPlan& plan);

/// `name`, or with the first number from 2 on that no city of the world has yet: "Piratennest 2".
[[nodiscard]] std::string numbered_name(const sim::World& world, std::string_view name);

/// Takes the building covering `tile` of island `island` away, as its owner demolishes it: the
/// ground comes back, land it held is checked again. A Kontor goes only once its city has no other
/// building. Fails (NotFound) without a building there and (Unsupported) where it cannot go.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> remove_building(const sim::World& world,
                                                                       const assets::BuildingTable& buildings,
                                                                       std::uint16_t island, sim::TilePos tile);

}  // namespace o1602::editor
