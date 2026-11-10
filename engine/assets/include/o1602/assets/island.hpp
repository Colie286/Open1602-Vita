// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "o1602/assets/chunk_file.hpp"
#include "o1602/core/result.hpp"

namespace o1602::assets {

/// Fields of the INSEL3 / INSEL4 / INSEL5 chunk that are verified so far. See docs/formats/scp.md.
struct IslandHeader {
    std::string chunk_name;  ///< "INSEL3" (40 bytes), "INSEL4" or "INSEL5" (116 bytes)
    // One byte each in the original's files; 16 bits in Open1602, so that its own savegames can hold
    // more than 256 islands and islands wider than 255 tiles (LC-2, LC-10, LC-11).
    std::uint16_t number = 0;
    std::uint16_t width = 0;   ///< in tiles
    std::uint16_t height = 0;  ///< in tiles
    std::uint8_t flags = 0;    ///< unknown, 0 in all samples so far
    std::uint16_t pos_x = 0;   ///< position on the 500x350 world map
    std::uint16_t pos_y = 0;

    // Template reference (offsets 0x60..0x65, read from any header of at least 0x66 bytes: INSEL4,
    // INSEL5). Islands in scenarios and savegames store only their deviations from an island template
    // (.SCP); the base terrain comes from the template.
    static constexpr std::uint16_t kNoTemplate = 0xFFFF;
    std::uint16_t template_number = kNoTemplate;  ///< file number (e.g. 12 -> big12.scp)
    std::uint16_t template_size = 0;              ///< 0 lit, 1 mit, 2 med, 3 big, 4 lar
    std::uint8_t climate = 0;                     ///< 0 Nord, 1 Sued (2, 3 name Nord and Sued again)
    bool stored_complete = false;                 ///< all tiles are stored, no template needed

    /// Ore and gold deposits (INSEL5 +0x1C, 8 bytes each, count at +0x1A; island record +0x20).
    struct Deposit {
        std::uint8_t ware = 0;  ///< 2 iron ore, 3 gold
        std::uint16_t x = 0;    ///< origin tile of the mountain it lies in (one byte in the original's)
        std::uint16_t y = 0;
        /// Byte 3: bit p = player slot p knows the deposit (explored it, or a computer bound it to
        /// a mountain of its own; docs/re/ships.md#erkunden). 32 bits in Open1602.
        std::uint32_t mask = 0;
        std::uint8_t kind = 0;     ///< 0 big vein, 1 small vein
        std::uint16_t amount = 0;  ///< left, 1/32 t
    };
    std::vector<Deposit> deposits;
    /// INSEL5 +0x1B: bit p = player slot p has explored the island's crops (island +0x42).
    std::uint8_t explored = 0;
    /// INSEL5 +0x08: island epochs (of 30 s) until the next deer census, 0..3 (island +0x68).
    std::uint8_t deer_phase = 0;
    /// INSEL5 +0x0A: the phase of the island epochs the island was last ticked in (island +0x64);
    /// an island whose value differs from TIMERS +0x01 is ticked at once.
    std::uint8_t last_phase = 0;
    /// INSEL5 +0x5C: bit (ware - 45) set = the island is fertile for that crop or resource.
    std::uint32_t fertility = 0;
    /// Drought (docs/re/growth.md): INSEL5 +0x66 strength (0 = none, 0x40 random, 0x26 curse;
    /// island +0x45), +0x6C end as game time in 1/10 s (island +0x60), +0x03 bit 0 a curse of the
    /// natives that turns into a drought at the next island epoch (island +0x44 bit 5).
    std::uint8_t drought_strength = 0;
    std::uint32_t drought_end = 0;
    /// INSEL5 +0x68: bit p = player slot p has discovered the island (island +0x43, 0x00468740).
    std::uint32_t discovered = 0;
    bool curse_pending = false;
    /// Volcano craters (docs/re/volcanoes.md): INSEL5 +0x17 count (at most 2), entries of 8 bytes
    /// at +0x3C (island record +0x30): [1] x, [2] y = origin of the volcano mountain, [6..7] lava
    /// throws left of a running eruption. +0x03 bit 2: a volcano of the island has erupted.
    struct Crater {
        std::uint8_t x = 0;
        std::uint8_t y = 0;
        std::uint16_t steps_left = 0;
    };
    std::vector<Crater> craters;
    bool volcano_erupted = false;

    std::vector<std::uint8_t> raw;  ///< complete chunk payload for not yet decoded fields

    /// Template file relative to the game directory, e.g. "Sued/big06.scp", or empty if the island
    /// does not use a template. Rule of the original loader (path built like 0x00469690).
    [[nodiscard]] std::string template_path() const;
};

/// One entry of an INSELHAUS chunk (8 bytes).
struct IslandTile {
    /// Building / terrain id. Matches the "Id" values in haeuser.cod minus 20000
    /// (e.g. 1201 -> IDMEER+0 = 21201). Verified for all ids of NORD/big.scp, see docs/formats/scp.md.
    std::uint16_t id = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    /// Packed attributes, decoded by the accessors below (layout from the build routine
    /// 1602.exe @ 0x004084D0, see docs/formats/scp.md).
    std::uint32_t bits = 0;

    static constexpr std::uint32_t kCodIdOffset = 20000;
    static constexpr std::uint8_t kNoCity = 7;

    [[nodiscard]] std::uint32_t cod_id() const noexcept { return std::uint32_t{id} + kCodIdOffset; }
    [[nodiscard]] std::uint8_t rotation() const noexcept { return static_cast<std::uint8_t>(bits & 3U); }
    [[nodiscard]] std::uint8_t anim_phase() const noexcept { return static_cast<std::uint8_t>((bits >> 2) & 0xFU); }
    [[nodiscard]] std::uint8_t island_number() const noexcept { return static_cast<std::uint8_t>((bits >> 6) & 0xFFU); }
    [[nodiscard]] std::uint8_t city() const noexcept { return static_cast<std::uint8_t>((bits >> 14) & 7U); }
    [[nodiscard]] std::uint8_t random() const noexcept { return static_cast<std::uint8_t>((bits >> 17) & 0x1FU); }
    [[nodiscard]] std::uint8_t player() const noexcept { return static_cast<std::uint8_t>((bits >> 22) & 0xFU); }
};

struct Island {
    IslandHeader header;
    /// Each INSELHAUS chunk following the header is one layer.
    std::vector<std::vector<IslandTile>> layers;
};

/// Extracts all islands from a chunk file (.SCP island template, .SZS/.SZM scenario, .GAM savegame).
core::Result<std::vector<Island>> load_islands(const ChunkFile& file);

}  // namespace o1602::assets
