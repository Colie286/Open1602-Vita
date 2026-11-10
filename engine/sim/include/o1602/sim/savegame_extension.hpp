// SPDX-License-Identifier: GPL-3.0-or-later
//
// Open1602's own chunk of a savegame (docs/formats/o1602-chunk.md): the state the original's
// chunks cannot hold, after WIFF where the original skips it.
//
// The chunk is authoritative for what it holds: lists are saved whole and in their order (cities,
// houses, the production list with its free slots, growth, ships, soldiers, animals, disasters,
// volcanoes), together with the random generators, the clocks of the simulation and its figures.
// Tiles are saved only where loading the original's chunks would give something else (grass and
// fish outside cities). A game saved and loaded in Open1602 therefore continues exactly as if it
// had never stopped.
//
// The chunk starts with a fingerprint of the file in front of it. If the original saved the game
// again, the chunk is gone; if another tool changed the original's chunks, the fingerprint no
// longer matches and Open1602 loads only the original's chunks.
//
// Every part is a tagged section with its own version, so later versions can add parts and older
// readers skip what they do not know.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

class Simulation;

/// Name of the chunk (not one the original knows, at most 15 characters).
inline constexpr std::string_view kExtensionChunk = "O1602";

/// Size of a world other than the original's 500 x 350 (u32 width, u32 height): a chunk of its own,
/// read before the islands are placed.
inline constexpr std::string_view kMapChunk = "O1602MAP";
[[nodiscard]] core::Bytes encode_map_size(std::uint32_t width, std::uint32_t height);
/// The size the file's O1602MAP gives, nothing without one (or a damaged one).
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> map_size(const assets::ChunkFile& file);

/// FNV-1a (64 bit) of the bytes in front of the chunk.
[[nodiscard]] std::uint64_t savegame_fingerprint(std::span<const std::uint8_t> bytes) noexcept;

/// The chunk's payload. `before` is the file up to the chunk (the original's chunks and WIFF);
/// `simulation`, if given, adds its clocks and figures.
[[nodiscard]] core::Bytes encode_extension(const World& world, const assets::BuildingTable& buildings,
                                           const Simulation* simulation, std::span<const std::uint8_t> before);

/// Applies the chunk to a world loaded from the original's chunks in front of it (`before`).
/// Unchanged world and an error if it does not belong to them, is damaged or has a required section
/// of a later version; other sections of a later version are skipped and reported in `warnings`.
core::Status apply_extension(World& world, const assets::BuildingTable& buildings,
                             std::span<const std::uint8_t> payload, std::span<const std::uint8_t> before,
                             std::vector<std::string>* warnings = nullptr);

}  // namespace o1602::sim
