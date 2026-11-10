// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/chunk_file.hpp"

#include <algorithm>

#include "o1602/core/byte_reader.hpp"
#include "o1602/core/file.hpp"

namespace o1602::assets {

using core::ErrorCode;
using core::make_error;

namespace {
constexpr std::size_t kNameLength = 16;
constexpr std::size_t kHeaderSize = kNameLength + 4;
}  // namespace

core::Result<ChunkFile> ChunkFile::load(const std::filesystem::path& path) {
    auto bytes = core::read_file(path);
    if (!bytes) {
        return bytes.error();
    }
    auto file = from_bytes(std::move(bytes).value());
    if (!file) {
        return make_error(file.error().code, core::path_to_utf8(path) + ": " + file.error().message);
    }
    return file;
}

core::Result<ChunkFile> ChunkFile::from_bytes(core::Bytes data) {
    ChunkFile file;
    file.m_data = std::move(data);

    core::ByteReader reader(file.m_data);
    while (!reader.at_end()) {
        const std::size_t header_offset = reader.offset();
        const auto name_bytes = reader.bytes(kNameLength);
        const auto size = reader.u32();
        if (!name_bytes || !size) {
            return make_error(ErrorCode::InvalidFormat,
                              "truncated chunk header at offset " + std::to_string(header_offset));
        }
        if (!reader.has(*size)) {
            return make_error(ErrorCode::InvalidFormat,
                              "chunk at offset " + std::to_string(header_offset) + " exceeds file size");
        }

        const auto name_end = std::find(name_bytes->begin(), name_bytes->end(), std::uint8_t{0});
        Chunk chunk;
        chunk.name.assign(name_bytes->begin(), name_end);
        chunk.offset = header_offset + kHeaderSize;
        chunk.size = *size;
        if (chunk.name.empty()) {
            return make_error(ErrorCode::InvalidFormat, "unnamed chunk at offset " + std::to_string(header_offset));
        }
        file.m_chunks.push_back(std::move(chunk));
        (void)reader.skip(*size);
    }
    return file;
}

const Chunk* ChunkFile::find(std::string_view name) const noexcept {
    const auto it = std::find_if(m_chunks.begin(), m_chunks.end(), [&](const Chunk& c) { return c.name == name; });
    return it == m_chunks.end() ? nullptr : &*it;
}

}  // namespace o1602::assets
