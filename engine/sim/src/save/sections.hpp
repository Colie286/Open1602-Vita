// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tagged sections of the game state, shared by Open1602's own savegame (save_game.hpp) and the
// chunk it adds to savegames in the original's format (savegame_extension.hpp). Internal to sim.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

class Simulation;

namespace save {

/// How the tiles are saved: all of them with the world's size (own savegame), or only those that
/// loading the original's chunks in front (`before`) would not give back (chunk in a .GAM).
enum class TileMode : std::uint8_t { Full, Differences };

/// Whether the sections of a TileMode::Full save carry the original's file the game came from
/// (ORIG, for the export as .GAM). It does not change while the game runs, so the fingerprint of
/// the game (Simulation::state_hash()) leaves it out.
enum class Origin : std::uint8_t { Keep, Leave };

[[nodiscard]] core::Bytes encode_sections(const World& world, const assets::BuildingTable& buildings,
                                          const Simulation* simulation, TileMode tiles,
                                          std::span<const std::uint8_t> before, Origin origin = Origin::Keep);
/// The same, appended to `out`. With `block_hashes` (TileMode::Full only), MAPW holds the hashes of
/// the tiles block by block instead of the tiles: the fingerprint of the game (Simulation::state_hash()),
/// which takes again only the blocks that changed since it was last taken with the same hashes.
void encode_sections(core::ByteWriter& out, const World& world, const assets::BuildingTable& buildings,
                     const Simulation* simulation, TileMode tiles, std::span<const std::uint8_t> before,
                     Origin origin = Origin::Keep, std::vector<std::vector<BlockHash>>* block_hashes = nullptr);

/// A world with the size and the islands of the sections (MAPW); the rest comes with apply_sections.
[[nodiscard]] core::Result<World> world_from_sections(std::span<const std::uint8_t> sections,
                                                      const assets::BuildingTable& buildings,
                                                      std::vector<std::string>* warnings);

/// The META section, if the sections (or their head) begin with it.
[[nodiscard]] std::optional<SaveMeta> read_meta(std::span<const std::uint8_t> sections);

/// Reads every section and, only if all of them are sound and fit the world, applies them.
/// Sections of a later version that are not required are skipped and reported in `warnings`.
core::Status apply_sections(World& world, const assets::BuildingTable& buildings,
                            std::span<const std::uint8_t> sections, std::vector<std::string>* warnings);

}  // namespace save
}  // namespace o1602::sim
