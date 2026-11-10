// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/save_game.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>

#include "save/sections.hpp"

#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/compress.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/validate.hpp"

namespace o1602::sim {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {'O', 'P', 'E', 'N', '1', '6', '0', '2'};
constexpr std::uint16_t kVersion = 1;
constexpr std::uint16_t kCompressed = 1;  ///< flag: the sections are a zlib stream
constexpr std::size_t kHeaderSize = 32;
/// The sections of a game are at most this large: a 4096 x 4096 world of land, 20 bytes a tile in
/// MAPW, needs about 330 MB, and its units and cities more.
constexpr std::uint64_t kMaxSize = 1ULL << 30;  // 1 GiB
/// A file that names no map size (no META) may promise at most this much, as before META.
constexpr std::uint64_t kMaxSizeWithoutMeta = 1ULL << 28;
/// Beyond the map's tiles (MAPW, at most 20 bytes a tile), what the rest of a game may need.
constexpr std::uint64_t kMaxSizeBeyondMap = 128ULL << 20;
/// The prefix of the sections that holds META (it comes first and is small).
constexpr std::size_t kHead = 4096;

/// How large the sections of a game on a world of this size can be: a file promising more is
/// damaged or made to cost memory (HI-11: a host's snapshot is such a file for its clients).
std::uint64_t max_size_for(const SaveMeta& meta) noexcept {
    constexpr std::uint64_t kSide = 1U << 16;  // keeps the product far from wrapping
    return std::min(kMaxSize,
                    std::min<std::uint64_t>(meta.width, kSide) * std::min<std::uint64_t>(meta.height, kSide) * 20 +
                        kMaxSizeBeyondMap);
}

/// META at the start of the stored sections, without unpacking more than its prefix.
std::optional<SaveMeta> meta_of(std::span<const std::uint8_t> stored, bool compressed) {
    if (compressed) {
        auto head = core::zlib_inflate_prefix(stored, kHead);
        return head ? save::read_meta(*head) : std::nullopt;
    }
    return save::read_meta(stored.subspan(0, std::min(stored.size(), kHead)));
}

struct Header {
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint64_t size = 0;    ///< sections, uncompressed
    std::uint32_t crc = 0;     ///< CRC-32 of the sections, uncompressed
    std::uint32_t stored = 0;  ///< bytes behind the header
};

/// The header of a file of `file_size` bytes, `bytes` its start (at least the header).
core::Result<Header> read_header(std::span<const std::uint8_t> bytes, std::uint64_t file_size) {
    if (!is_open1602_save(bytes)) {
        return make_error(ErrorCode::InvalidFormat, "not an Open1602 savegame");
    }
    core::ByteReader in(bytes.subspan(kMagic.size()));
    Header h;
    const auto version = in.u16();
    const auto flags = in.u16();
    const auto reserved = in.u32();
    const auto size = in.u64();
    const auto crc = in.u32();
    const auto stored = in.u32();
    if (!version || !flags || !reserved || !size || !crc || !stored) {
        return make_error(ErrorCode::InvalidFormat, "savegame: header too short");
    }
    h.version = *version;
    h.flags = *flags;
    h.size = *size;
    h.crc = *crc;
    h.stored = *stored;
    if (h.version > kVersion) {
        return make_error(ErrorCode::Unsupported,
                          "savegame of a newer Open1602 (format " + std::to_string(h.version) + ")");
    }
    if (h.size > kMaxSize || file_size - kHeaderSize != h.stored) {
        return make_error(ErrorCode::InvalidFormat, "savegame: cut off or damaged");
    }
    return h;
}

}  // namespace

bool is_open1602_save(std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= kHeaderSize && std::equal(kMagic.begin(), kMagic.end(), bytes.begin());
}

core::Result<core::Bytes> write_game(const World& world, const assets::BuildingTable& buildings,
                                     const Simulation* simulation, const SaveOptions& options) {
    auto sections = encode_game(world, buildings, simulation);
    if (!sections) {
        return sections.error();
    }
    return pack_game(*sections, options);
}

core::Result<core::Bytes> encode_game(const World& world, const assets::BuildingTable& buildings,
                                      const Simulation* simulation) {
    core::Bytes sections =
        save::encode_sections(world, buildings, simulation, save::TileMode::Full, std::span<const std::uint8_t>{});
    // Before packing: a game too large to read back fails at once, not after seconds of deflate,
    // and with a message that says why (DS-4).
    const SaveMeta meta{world.width(), world.height(), 0, {}};
    if (sections.size() > max_size_for(meta)) {
        return make_error(ErrorCode::LimitExceeded, "savegame: the game is too large (" +
                                                        std::to_string(sections.size() >> 20) + " MiB of " +
                                                        std::to_string(max_size_for(meta) >> 20) + " MiB)");
    }
    return sections;
}

core::Result<core::Bytes> pack_game(const core::Bytes& sections, const SaveOptions& options) {
    std::uint16_t flags = 0;
    core::Bytes stored;
    if (options.compress && core::has_zlib()) {
        auto packed = core::zlib_deflate(sections);
        if (!packed) {
            return packed.error();
        }
        stored = std::move(packed).value();
        flags |= kCompressed;
    } else {
        stored = sections;
    }
    if (stored.size() > 0xFFFFFFFFULL) {
        return make_error(ErrorCode::LimitExceeded, "savegame: the game is too large");
    }
    core::ByteWriter out;
    out.bytes(kMagic);
    out.u16(kVersion);
    out.u16(flags);
    out.u32(0);
    out.u64(sections.size());
    out.u32(core::crc32(sections));
    out.u32(static_cast<std::uint32_t>(stored.size()));
    out.bytes(stored);
    return out.take();
}

core::Result<SaveInfo> save_info(std::span<const std::uint8_t> bytes) {
    return save_info(bytes, bytes.size());
}

core::Result<SaveInfo> save_info(std::span<const std::uint8_t> head, std::uint64_t file_size) {
    const std::span<const std::uint8_t> bytes = head;
    auto h = read_header(bytes, file_size);
    if (!h) {
        return h.error();
    }
    SaveInfo info{h->version, (h->flags & kCompressed) != 0, h->size, std::nullopt};
    // META comes first and is small; a list of savegames needs no more of the file.
    info.meta = meta_of(bytes.subspan(kHeaderSize), info.compressed);
    return info;
}

core::Result<World> read_game(std::span<const std::uint8_t> bytes, const assets::BuildingTable& buildings,
                              std::vector<std::string>* warnings, const assets::FigureTable* figures) {
    auto h = read_header(bytes, bytes.size());
    if (!h) {
        return h.error();
    }
    const auto stored = bytes.subspan(kHeaderSize);
    // What the world the file names can need, before unpacking all of it.
    const std::optional<SaveMeta> meta = meta_of(stored, (h->flags & kCompressed) != 0);
    if (h->size > (meta ? max_size_for(*meta) : kMaxSizeWithoutMeta)) {
        return make_error(ErrorCode::InvalidFormat, "savegame: larger than its world can be (damaged)");
    }
    core::Bytes unpacked;
    std::span<const std::uint8_t> sections = stored;
    if ((h->flags & kCompressed) != 0) {
        auto data = core::zlib_inflate(stored, static_cast<std::size_t>(h->size));
        if (!data) {
            return data.error();
        }
        unpacked = std::move(data).value();
        sections = unpacked;
    } else if (stored.size() != h->size) {
        return make_error(ErrorCode::InvalidFormat, "savegame: cut off or damaged");
    }
    if (core::crc32(sections) != h->crc) {
        return make_error(ErrorCode::InvalidFormat, "savegame: damaged (checksum)");
    }
    auto world = save::world_from_sections(sections, buildings, warnings);
    if (!world) {
        return world.error();
    }
    // world_from_sections already reported the sections it skipped.
    if (auto status = save::apply_sections(*world, buildings, sections, nullptr); !status) {
        return status.error();
    }
    if (auto status = validate(*world); !status) {
        return status.error();
    }
    if (figures != nullptr) {
        if (auto status = Simulation::state_readable(*world, buildings, *figures); !status) {
            return make_error(status.error().code,
                              "savegame: the state of the running game is damaged (" + status.error().message + ")");
        }
    }
    return world;
}

}  // namespace o1602::sim
