// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writes a world as a savegame (.GAM) the original can load (docs/re/savegame-writer.md). Open1602
// saves its own games in its own format (save_game.hpp); this writer exports a game for the original
// as long as it fits the original's limits.
//
// The original loads its chunks in a fixed order, each acting on what came before: islands with
// their two layers of tiles (the ground, then what stands on it), the deer, the building lists,
// the cities, players, ships, soldiers and the timers; WIFF closes the file. Many records hold
// fields Open1602 does not model yet. The writer starts each record from the one in the file the
// world was loaded from and changes only what Open1602 keeps; a value the loader would have turned
// into the same state stays as it was, so a world saved without changes comes out byte for byte.
//
// What the original cannot hold (fires, robbers, lava, figures, more islands, cities or players
// than it knows) goes into a chunk of Open1602's own after WIFF, which the original skips
// (savegame_extension.hpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// One chunk of a savegame.
struct SavedChunk {
    std::string name;
    core::Bytes data;
};

/// The chunks of the savegame in the original's order, without WIFF. Needs the world's source
/// file (World::source()) for the fields Open1602 does not model; fails if the world does not fit
/// the original's limits (50 islands, 75 cities, 7 players, 150 ships, ...).
[[nodiscard]] core::Result<std::vector<SavedChunk>> savegame_chunks(const World& world,
                                                                    const assets::BuildingTable& buildings);

class Simulation;

/// The whole file: the chunks, WIFF and Open1602's own chunk (savegame_extension.hpp). With the
/// running `simulation` its clocks and figures are saved too, so the game continues exactly.
[[nodiscard]] core::Result<core::Bytes> write_savegame(const World& world, const assets::BuildingTable& buildings,
                                                       const Simulation* simulation = nullptr);

}  // namespace o1602::sim
