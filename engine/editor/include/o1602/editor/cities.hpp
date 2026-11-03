// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's cities (ADR 0013): a city's name, the goods in its warehouses and its owner, each
// changed on a copy of the world and given back as a WorldEdit for undo, like the buildings'.
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "o1602/assets/building_table.hpp"
#include "o1602/core/result.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// The longest name a city takes, as a player's rename (sim::CityOrder::kMaxNameLength).
inline constexpr std::size_t kMaxCityName = 40;

/// The city `city` of island `island`, named `name` (Windows-1252, sim::valid_name). Fails
/// (NotFound) without the city, (InvalidFormat) for a name a player could not give.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> rename_city(const sim::World& world, std::uint16_t island,
                                                                   std::uint8_t city, std::string_view name);

/// The room per ware of the city in 1/32 t (sim::City::capacity, as the game reckons it from the
/// city's Kontor, markets and warehouses); 0 without the city.
[[nodiscard]] std::uint32_t city_room(const sim::World& world, const assets::BuildingTable& buildings,
                                      std::uint16_t island, std::uint8_t city);

/// The city with `amount` (1/32 t) of `ware` in its warehouses, at most its room (city_room()):
/// the game would lose what is above. Fails (NotFound) without the city, (InvalidFormat) for a
/// ware that is no trade good of the world.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> set_city_goods(const sim::World& world,
                                                                      const assets::BuildingTable& buildings,
                                                                      std::uint16_t island, std::uint8_t city,
                                                                      std::uint8_t ware, std::uint32_t amount);

/// The city and every tile it holds given to `owner`, a human or a computer, as when a player
/// takes a city over: its statistics start anew. Fails (NotFound) without the city or the player,
/// (Unsupported) where `owner` has a city on the island already (one Kontor each) or is no human
/// or computer.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> change_city_owner(const sim::World& world, std::uint16_t island,
                                                                         std::uint8_t city, sim::PlayerId owner);

}  // namespace o1602::editor
