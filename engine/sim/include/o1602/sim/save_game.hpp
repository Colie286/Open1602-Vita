// SPDX-License-Identifier: GPL-3.0-or-later
//
// Open1602's own savegame (docs/formats/o1602-save.md): the whole state of a game in tagged
// sections behind a short header with a CRC-32, compressed with zlib. It has none of the limits of
// the original's format (players, ships, soldiers, towers, map size) and depends on no other file.
//
// The original's files (.SZS, .SZM, .GAM) are only read (world.hpp); a game can still be exported
// to a .GAM the original loads while it fits (savegame_writer.hpp).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

class Simulation;

/// File name extension of Open1602's savegames.
inline constexpr std::string_view kSaveExtension = ".o1602";

struct SaveOptions {
    /// Compress the sections with zlib (if the build has zlib; otherwise they are stored as they are).
    bool compress = true;
};

/// What the section META of a savegame says (docs/formats/o1602-save.md), for a list of savegames.
struct SaveMeta {
    std::uint32_t width = 0;  ///< of the world, in tiles
    std::uint32_t height = 0;
    std::uint64_t time_ms = 0;  ///< game time
    std::string player;         ///< name of the first human player (Windows-1252)
};

/// What the header of a savegame says, for a list of savegames.
struct SaveInfo {
    std::uint16_t version = 0;
    bool compressed = false;
    std::uint64_t size = 0;        ///< of the sections, uncompressed
    std::optional<SaveMeta> meta;  ///< missing in a file without META (or a build without zlib)
};

/// Whether the bytes start like an Open1602 savegame.
[[nodiscard]] bool is_open1602_save(std::span<const std::uint8_t> bytes) noexcept;

/// The whole file. With the running `simulation` its clocks and figures are saved too, so the game
/// continues exactly; without one, a world loaded but not yet run keeps those it was loaded with.
[[nodiscard]] core::Result<core::Bytes> write_game(const World& world, const assets::BuildingTable& buildings,
                                                   const Simulation* simulation = nullptr,
                                                   const SaveOptions& options = {});

/// write_game in two steps, so that the second can run on another thread while the game goes on
/// (a multiplayer host packing the game for a computer that joins, TB-26): the sections, unpacked,
/// as they are now (refused if the game is too large to read back), and the file around them.
/// pack_game reads nothing but `sections`, so any thread may call it.
[[nodiscard]] core::Result<core::Bytes> encode_game(const World& world, const assets::BuildingTable& buildings,
                                                    const Simulation* simulation = nullptr);
[[nodiscard]] core::Result<core::Bytes> pack_game(const core::Bytes& sections, const SaveOptions& options = {});

/// The header of a savegame and its META section (checks magic and version, not the data; unpacks
/// only the head of the sections).
[[nodiscard]] core::Result<SaveInfo> save_info(std::span<const std::uint8_t> bytes);
/// The same from the head of a file of `file_size` bytes (kSaveInfoHead bytes are enough).
[[nodiscard]] core::Result<SaveInfo> save_info(std::span<const std::uint8_t> head, std::uint64_t file_size);
/// How much of a savegame save_info needs: the header and the packed head of the sections.
inline constexpr std::size_t kSaveInfoHead = 64 * 1024;

/// Loads a savegame. Fails (with nothing loaded) if the file is damaged, of a newer Open1602,
/// names buildings the game's data does not have or does not hold together (validate.hpp). With
/// the `figures` it also fails if the state of the running game cannot be read
/// (Simulation::state_readable); without them, the Simulation finds that out when it starts and
/// begins afresh (Simulation::state_damaged).
[[nodiscard]] core::Result<World> read_game(std::span<const std::uint8_t> bytes, const assets::BuildingTable& buildings,
                                            std::vector<std::string>* warnings = nullptr,
                                            const assets::FigureTable* figures = nullptr);

}  // namespace o1602::sim
