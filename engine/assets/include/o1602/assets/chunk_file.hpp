// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/core/file.hpp"
#include "o1602/core/result.hpp"

namespace o1602::assets {

/// One entry of a chunk container. See docs/formats/chunks.md.
struct Chunk {
    std::string name;        ///< Up to 16 characters, e.g. "INSEL5", "BSH", "COL".
    std::size_t offset = 0;  ///< Offset of the chunk payload within the file.
    std::size_t size = 0;    ///< Payload size in bytes.
};

/// Generic container used by almost all binary data files of the original game
/// (.BSH, .ZEI, .COL, .SCP, .SZS, .SZM, .GAM):
/// a sequence of { char name[16]; uint32_t size; uint8_t payload[size]; }.
///
/// Owns the file contents; chunk payloads are views into that buffer.
class ChunkFile {
public:
    static core::Result<ChunkFile> load(const std::filesystem::path& path);
    static core::Result<ChunkFile> from_bytes(core::Bytes data);

    [[nodiscard]] const std::vector<Chunk>& chunks() const noexcept { return m_chunks; }

    /// Payload of a chunk.
    [[nodiscard]] std::span<const std::uint8_t> payload(const Chunk& chunk) const noexcept {
        return std::span(m_data).subspan(chunk.offset, chunk.size);
    }

    /// First chunk with the given name, or nullptr.
    [[nodiscard]] const Chunk* find(std::string_view name) const noexcept;

    /// Raw file contents.
    [[nodiscard]] std::span<const std::uint8_t> data() const noexcept { return m_data; }

private:
    core::Bytes m_data;
    std::vector<Chunk> m_chunks;
};

}  // namespace o1602::assets
