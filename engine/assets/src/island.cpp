// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/island.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <span>

#include "o1602/core/byte_reader.hpp"

namespace o1602::assets {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::size_t kMinHeaderSize = 8;
constexpr std::size_t kTileSize = 8;

/// Size class of an INSEL5 island by its width, as the original's loader sets it (0x00469900) before it
/// builds the template path: the stored value is ignored. Wider islands keep it.
std::uint16_t size_class_of_width(std::uint32_t width, std::uint16_t stored) {
    static constexpr std::uint8_t kLimits[] = {33, 43, 56, 76, 111};  // lit, mit, med, big, lar
    for (std::uint16_t c = 0; c < std::size(kLimits); ++c) {
        if (width < kLimits[c]) {
            return c;
        }
    }
    return stored;
}

core::Result<IslandHeader> parse_header(const Chunk& chunk, std::span<const std::uint8_t> data) {
    if (data.size() < kMinHeaderSize) {
        return make_error(ErrorCode::InvalidFormat, chunk.name + " chunk too small");
    }
    core::ByteReader r(data);
    IslandHeader h;
    h.chunk_name = chunk.name;
    h.number = *r.u8();
    h.width = *r.u8();
    h.height = *r.u8();
    h.flags = *r.u8();
    h.pos_x = *r.u16();
    h.pos_y = *r.u16();
    if (data.size() >= 0x66) {
        h.template_number = static_cast<std::uint16_t>(data[0x60] | (data[0x61] << 8));
        h.template_size = static_cast<std::uint16_t>(data[0x62] | (data[0x63] << 8));
        if (chunk.name == "INSEL5") {
            // NORDNAT/med15 stores "lit" for its 50 tiles; the original loads its base NORD/med15.
            h.template_size = size_class_of_width(h.width, h.template_size);
        }
        h.climate = data[0x64];
        h.stored_complete = data[0x65] != 0;
        h.explored = data[0x1B];
        // Deposits: count at +0x1A (at most 2 in the original), entries at +0x1C.
        const std::size_t deposits = std::min<std::size_t>(data[0x1A], 2);
        for (std::size_t i = 0; i < deposits; ++i) {
            const std::size_t o = 0x1C + 8 * i;
            IslandHeader::Deposit d;
            d.ware = data[o];
            d.x = data[o + 1];
            d.y = data[o + 2];
            d.mask = data[o + 3];
            d.kind = data[o + 4];
            d.amount = static_cast<std::uint16_t>(data[o + 6] | (data[o + 7] << 8));
            if (d.ware != 0) {
                h.deposits.push_back(d);
            }
        }
        h.deer_phase = static_cast<std::uint8_t>(data[0x08] & 3U);
        h.last_phase = static_cast<std::uint8_t>(data[0x0A] & 7U);
        h.fertility = static_cast<std::uint32_t>(data[0x5C]) | (static_cast<std::uint32_t>(data[0x5D]) << 8) |
                      (static_cast<std::uint32_t>(data[0x5E]) << 16) | (static_cast<std::uint32_t>(data[0x5F]) << 24);
        h.curse_pending = (data[0x03] & 1U) != 0;
        h.volcano_erupted = (data[0x03] & 4U) != 0;
        const std::size_t craters = std::min<std::size_t>(data[0x17], 2);
        for (std::size_t i = 0; i < craters; ++i) {
            const std::size_t o = 0x3C + 8 * i;
            h.craters.push_back(IslandHeader::Crater{data[o + 1], data[o + 2],
                                                     static_cast<std::uint16_t>(data[o + 6] | (data[o + 7] << 8))});
        }
    }
    if (data.size() >= 0x70) {
        h.drought_strength = data[0x66];
        h.discovered = static_cast<std::uint32_t>(data[0x68]) | (static_cast<std::uint32_t>(data[0x69]) << 8) |
                       (static_cast<std::uint32_t>(data[0x6A]) << 16) | (static_cast<std::uint32_t>(data[0x6B]) << 24);
        h.drought_end = static_cast<std::uint32_t>(data[0x6C]) | (static_cast<std::uint32_t>(data[0x6D]) << 8) |
                        (static_cast<std::uint32_t>(data[0x6E]) << 16) | (static_cast<std::uint32_t>(data[0x6F]) << 24);
    }
    h.raw.assign(data.begin(), data.end());
    return h;
}

core::Result<std::vector<IslandTile>> parse_tiles(std::span<const std::uint8_t> data) {
    if (data.size() % kTileSize != 0) {
        return make_error(ErrorCode::InvalidFormat, "INSELHAUS size is not a multiple of 8");
    }
    std::vector<IslandTile> tiles;
    tiles.reserve(data.size() / kTileSize);
    core::ByteReader r(data);
    while (!r.at_end()) {
        IslandTile t;
        t.id = *r.u16();
        t.x = *r.u8();
        t.y = *r.u8();
        t.bits = *r.u32();
        tiles.push_back(t);
    }
    return tiles;
}

}  // namespace

std::string IslandHeader::template_path() const {
    // Name tables of the original: 0x0049A8A4 (climate directories) and 0x0049A890 (size prefixes).
    static constexpr const char* kClimate[] = {"Nord", "Sued", "Nord", "Sued"};
    static constexpr const char* kSize[] = {"lit", "mit", "med", "big", "lar"};
    if (template_number == kNoTemplate || stored_complete || climate > 3 || template_size > 4) {
        return {};
    }
    char name[32];
    std::snprintf(name, sizeof(name), "%s/%s%02u.scp", kClimate[climate], kSize[template_size],
                  static_cast<unsigned>(template_number));
    return name;
}

core::Result<std::vector<Island>> load_islands(const ChunkFile& file) {
    std::vector<Island> islands;
    for (const Chunk& chunk : file.chunks()) {
        if (chunk.name == "INSEL3" || chunk.name == "INSEL4" || chunk.name == "INSEL5") {
            auto header = parse_header(chunk, file.payload(chunk));
            if (!header) {
                return header.error();
            }
            islands.push_back(Island{std::move(header).value(), {}});
        } else if (chunk.name == "INSELHAUS") {
            if (islands.empty()) {
                return make_error(ErrorCode::InvalidFormat, "INSELHAUS chunk without preceding island header");
            }
            auto tiles = parse_tiles(file.payload(chunk));
            if (!tiles) {
                return tiles.error();
            }
            islands.back().layers.push_back(std::move(tiles).value());
        }
    }
    return islands;
}

}  // namespace o1602::assets
