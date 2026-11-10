// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compression and checksums for Open1602's own files (docs/formats/o1602-save.md): zlib streams
// when the build has zlib, CRC-32 always.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <memory>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::core {

/// Whether this build can write and read zlib streams.
[[nodiscard]] bool has_zlib() noexcept;

/// A zlib stream of `data` (level 6). Fails if the build has no zlib.
[[nodiscard]] Result<std::vector<std::uint8_t>> zlib_deflate(std::span<const std::uint8_t> data, int level = 6);

/// Reuses zlib's working allocation for independent streams, never their dictionary.
/// The returned view stays valid until the next encode or destruction. One owner/thread.
class ZlibDeflater {
public:
    explicit ZlibDeflater(int level=1);
    ~ZlibDeflater();
    ZlibDeflater(const ZlibDeflater&)=delete;
    ZlibDeflater& operator=(const ZlibDeflater&)=delete;
    Result<std::span<const std::uint8_t>> encode(std::span<const std::uint8_t>);
private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The data of a zlib stream, which must be exactly `size` bytes long.
[[nodiscard]] Result<std::vector<std::uint8_t>> zlib_inflate(std::span<const std::uint8_t> stream, std::size_t size);

/// The first `size` bytes of a zlib stream's data (fewer if the data, or the part of the stream
/// given, is shorter), without unpacking the rest: for the head of a savegame.
[[nodiscard]] Result<std::vector<std::uint8_t>> zlib_inflate_prefix(std::span<const std::uint8_t> stream,
                                                                    std::size_t size);

/// CRC-32 (IEEE 802.3, the one of zlib and PNG).
[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept;

}  // namespace o1602::core
