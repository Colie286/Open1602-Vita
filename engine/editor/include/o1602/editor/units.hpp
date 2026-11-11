// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's ships and land units (ADR 0013): put down for a player where the game lets them be,
// and taken away again, each on a copy of the world given back as a WorldEdit for undo, like the
// buildings'.
#pragma once

#include <cstdint>
#include <memory>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/result.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// Why a ship or a land unit cannot go on a tile.
enum class UnitRefusal : std::uint8_t {
    None,
    Outside,   ///< not on the map
    NoWater,   ///< a ship on land or on water it cannot sail
    NoGround,  ///< a land unit on the sea, or on ground soldiers do not walk on
    Taken,     ///< another ship next to it, another unit on the same tile
    NoOwner,   ///< not a human or computer player, nor the natives or pirates
    NoType,    ///< no ship or land unit of the figure tables
};

/// Whether a ship can go on world tile (x, y): water ships sail on, no other ship within a tile.
[[nodiscard]] UnitRefusal ship_fits(const sim::World& world, const assets::BuildingTable& buildings, std::int64_t x,
                                    std::int64_t y);
/// Whether a land unit can go on world tile (x, y): an island's ground soldiers walk on, no other
/// unit on the tile.
[[nodiscard]] UnitRefusal unit_fits(const sim::World& world, const assets::BuildingTable& buildings, std::int64_t x,
                                    std::int64_t y);

/// A ship of `figure` (one of the shipyards' or a package's ship, or the pirates' ship) for `owner` on world tile (x,
/// y), facing `direction` (0..7), new and empty as a shipyard launches it; unnamed, so the game names it when it
/// starts. Fails (InvalidFormat) with the refusal's name where it cannot go.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> place_ship(
    const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
    sim::PlayerId owner, std::uint16_t figure, std::int64_t x, std::int64_t y, std::uint8_t direction);

/// A land unit of the family `family` (its first figure, FigureTable::unit_families) for `owner`
/// on world tile (x, y), facing `direction` (0..7), at full health.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> place_unit(
    const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
    sim::PlayerId owner, std::uint16_t family, std::int64_t x, std::int64_t y, std::uint8_t direction);

/// The ship `id` taken away, with every order of others that names it. Fails (NotFound).
[[nodiscard]] core::Result<std::unique_ptr<Operation>> remove_ship(const sim::World& world, std::uint32_t id);
/// The land unit `id` taken away, with every order of others that names it. Fails (NotFound).
[[nodiscard]] core::Result<std::unique_ptr<Operation>> remove_unit(const sim::World& world, std::uint32_t id);

/// The name of a refusal for messages and logs.
[[nodiscard]] const char* refusal_name(UnitRefusal refusal) noexcept;

}  // namespace o1602::editor
