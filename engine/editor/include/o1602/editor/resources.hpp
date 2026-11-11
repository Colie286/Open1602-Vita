// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's riches of an island (ADR 0013): ore and gold veins in its mountains, buried
// treasures and the forest spots deer live at, put down and taken away on a copy of the world given
// back as a WorldEdit for undo, like the buildings'.
#pragma once

#include <cstdint>
#include <memory>

#include "o1602/assets/building_table.hpp"
#include "o1602/core/result.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// What the editor puts on an island.
enum class Resource : std::uint8_t {
    BigOre,    ///< a big iron vein (240 t), only in a big ore mountain
    SmallOre,  ///< a small iron vein (80 t), in any ore mountain
    Gold,      ///< a gold vein (50 t), in any ore mountain
    Treasure,  ///< a buried treasure (1000 gold) under open ground
    Deer,      ///< a spot in a forest where deer live, at most 8 an island
};
inline constexpr std::size_t kResources = 5;

/// Amounts of a new vein (1/32 t) and of a treasure (gold), as a new game gives them.
inline constexpr std::uint16_t kBigOreAmount = 0x1E00;
inline constexpr std::uint16_t kSmallOreAmount = 0x0A00;
inline constexpr std::uint16_t kGoldAmount = 0x0640;
inline constexpr std::uint16_t kTreasureAmount = 1000;

/// Why a resource cannot go on a tile.
enum class ResourceRefusal : std::uint8_t {
    None,
    NoIsland,    ///< not on an island
    NoMountain,  ///< a vein outside an ore mountain
    SmallMount,  ///< a big vein in a small mountain
    NoGround,    ///< a treasure not under open ground
    NoForest,    ///< deer not in a forest
    Taken,       ///< the mountain has its vein, the tile its treasure or deer already
    Full,        ///< the island has its 8 deer spots
};

/// Whether `resource` can go on tile `tile` of island `island`.
[[nodiscard]] ResourceRefusal resource_fits(const sim::World& world, const assets::BuildingTable& buildings,
                                            std::uint16_t island, sim::TilePos tile, Resource resource);

/// `resource` on tile `tile` of island `island`: a vein in the mountain covering the tile (at its
/// origin, as mines look for it), a treasure or deer on the tile. Nobody knows a new deposit.
/// Fails (InvalidFormat) with the refusal's name where it cannot go.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> place_resource(const sim::World& world,
                                                                      const assets::BuildingTable& buildings,
                                                                      std::uint16_t island, sim::TilePos tile,
                                                                      Resource resource);

/// The treasure or deer on tile `tile` of island `island` taken away, else the vein of the
/// mountain covering it. Fails (NotFound) where there is none.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> remove_resource(const sim::World& world,
                                                                       const assets::BuildingTable& buildings,
                                                                       std::uint16_t island, sim::TilePos tile);

/// The name of a refusal for messages and logs.
[[nodiscard]] const char* refusal_name(ResourceRefusal refusal) noexcept;

}  // namespace o1602::editor
