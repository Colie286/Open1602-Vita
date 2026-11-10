// SPDX-License-Identifier: GPL-3.0-or-later
//
// The chunk around the sections (save/sections.hpp): magic, version and the fingerprint of the
// original's chunks in front of it.
#include "o1602/sim/savegame_extension.hpp"

#include <algorithm>
#include <array>

#include "save/sections.hpp"

#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"

namespace o1602::sim {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {'O', '1', '6', '0', '2', 'S', 'A', 'V'};
constexpr std::uint16_t kFormatVersion = 1;

}  // namespace

core::Bytes encode_map_size(std::uint32_t width, std::uint32_t height) {
    core::ByteWriter out;
    out.u32(width);
    out.u32(height);
    return out.take();
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> map_size(const assets::ChunkFile& file) {
    const assets::Chunk* c = file.find(kMapChunk);
    if (c == nullptr) {
        return std::nullopt;
    }
    core::ByteReader in(file.payload(*c));
    const auto w = in.u32();
    const auto h = in.u32();
    if (!w || !h || *w == 0 || *h == 0 || *w > World::kMaxSide || *h > World::kMaxSide) {
        return std::nullopt;
    }
    return std::pair{*w, *h};
}

std::uint64_t savegame_fingerprint(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const std::uint8_t b : bytes) {
        h ^= b;
        h *= 0x100000001B3ULL;
    }
    return h;
}

core::Bytes encode_extension(const World& world, const assets::BuildingTable& buildings, const Simulation* simulation,
                             std::span<const std::uint8_t> before) {
    core::ByteWriter out;
    out.bytes(kMagic);
    out.u16(kFormatVersion);
    out.u16(0);
    out.u64(savegame_fingerprint(before));
    out.bytes(save::encode_sections(world, buildings, simulation, save::TileMode::Differences, before));
    return out.take();
}

core::Status apply_extension(World& world, const assets::BuildingTable& buildings,
                             std::span<const std::uint8_t> payload, std::span<const std::uint8_t> before,
                             std::vector<std::string>* warnings) {
    core::ByteReader reader(payload);
    const auto magic = reader.bytes(kMagic.size());
    if (!magic || !std::equal(magic->begin(), magic->end(), kMagic.begin())) {
        return make_error(ErrorCode::InvalidFormat, "O1602: not Open1602's chunk");
    }
    const auto version = reader.u16();
    const auto flags = reader.u16();
    const auto fingerprint = reader.u64();
    if (!version || !flags || !fingerprint) {
        return make_error(ErrorCode::InvalidFormat, "O1602: chunk too short");
    }
    if (*version > kFormatVersion) {
        return make_error(ErrorCode::Unsupported, "O1602: chunk of a newer Open1602");
    }
    if (*fingerprint != savegame_fingerprint(before)) {
        return make_error(ErrorCode::InvalidFormat, "O1602: the original's chunks were changed since");
    }
    return save::apply_sections(world, buildings, payload.subspan(payload.size() - reader.remaining()), warnings);
}

}  // namespace o1602::sim
