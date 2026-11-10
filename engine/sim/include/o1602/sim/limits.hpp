// SPDX-License-Identifier: GPL-3.0-or-later
//
// Limits that more than one module checks, each defined once (LC-1). Each names what bounds it;
// other modules derive their own from these (net::kMaxLobbySeats, the menus) and state the relation
// with a static_assert. Limits of one file format alone stay with that format (the .GAM export's
// in savegame_writer.cpp, the wire's in net/protocol.hpp).
#pragma once

#include <cstddef>
#include <cstdint>

namespace o1602::sim {

/// Players with a start in a new world: as many as there are banner colours that stay apart
/// (render::BannerTable::kDefaultColours). The world generator, the start dialogs, the lobby and
/// add_players all take this one.
inline constexpr std::size_t kMaxPlayers = 12;

/// Island numbers (assets::IslandHeader::number) are one byte in the islands' files and in every
/// state section that names an island (ISLE, MAPW, EPOC, FLEE), so a world saves at most this many.
inline constexpr std::size_t kMaxIslandNumbers = 256;
/// Islands the world generator places at most: below kMaxIslandNumbers, 0xFF meaning "no island".
inline constexpr std::uint32_t kMaxPlannedIslands = 250;
static_assert(kMaxPlannedIslands < kMaxIslandNumbers);
/// Islands a world holds: island indexes are 16 bits (WorldIsland, commands), 0xFFFF is the sea
/// (World::kSea).
inline constexpr std::size_t kMaxIslands = 0xFFFF;
static_assert(kMaxIslands <= 0xFFFF, "loops over the islands count with std::uint16_t (LC-23)");

/// Cities a player may found on one island: the original's three-bit city number.
inline constexpr std::uint32_t kMaxCitiesPerIsland = 7;
/// City numbers an island of a loaded game can carry: the eight owner slots of the original's island
/// record. Territory masks keep one bit for each in a byte.
inline constexpr std::uint32_t kCitySlotsPerIsland = 8;
static_assert(kMaxCitiesPerIsland < kCitySlotsPerIsland && kCitySlotsPerIsland <= 8);

/// The longest side of a world in tiles, checked when a world is loaded before anything is
/// allocated. The world generator stays below (WorldSettings::kMaxSide).
inline constexpr std::uint32_t kMaxWorldSide = 4096;

}  // namespace o1602::sim
