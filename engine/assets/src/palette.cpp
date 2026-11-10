// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/palette.hpp"

namespace o1602::assets {

using core::ErrorCode;
using core::make_error;

core::Result<Palette> Palette::from_chunk_file(const ChunkFile& file) {
    const Chunk* chunk = file.find("COL");
    if (chunk == nullptr) {
        return make_error(ErrorCode::InvalidFormat, "no COL chunk");
    }
    if (chunk->size < kSize * 4) {
        return make_error(ErrorCode::InvalidFormat, "COL chunk too small");
    }
    const auto data = file.payload(*chunk);
    Palette palette;
    for (std::size_t i = 0; i < kSize; ++i) {
        // Stored as R, G, B, unused (always 0).
        palette.m_colours[i] = Rgba{data[i * 4], data[i * 4 + 1], data[i * 4 + 2], 255};
    }
    return palette;
}

core::Result<Palette> Palette::load(const std::filesystem::path& path) {
    auto file = ChunkFile::load(path);
    if (!file) {
        return file.error();
    }
    return from_chunk_file(*file);
}

}  // namespace o1602::assets
