// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/core/result.hpp"
#include "o1602/core/hash.hpp"

namespace o1602::assets {

/// Decoded 8-bit palettized image with a binary transparency mask. A true colour sprite (the History
/// Edition's type 13) has its colours in `rgba` instead; its indices are all 0.
struct IndexedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> indices;  ///< width * height palette indices
    std::vector<std::uint8_t> opaque;   ///< width * height, 1 = pixel set, 0 = transparent
    std::vector<std::uint8_t> rgba;     ///< width * height * 4 for true colour sprites, else empty
    std::uint32_t clipped_pixels = 0;   ///< Pixels outside width/height (seen in some ZEI glyphs)

    /// Expands to RGBA8 using the palette (true colour sprites: their own colours); transparent
    /// pixels become (0,0,0,0).
    [[nodiscard]] std::vector<std::uint8_t> to_rgba(const Palette& palette) const;
};

/// Header of one sprite record.
struct SpriteInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t type = 0;    ///< 1 or 6 RLE, 2 raw, 13 true colour RLE (History Edition). Others decode empty.
    std::uint32_t length = 0;  ///< Record size incl. 16 byte header, padded to 4 bytes.
    std::uint32_t offset = 0;  ///< Offset of the record within the BSH/ZEI chunk payload.
};

/// Sprite container: .BSH (graphics) and .ZEI (font glyphs). See docs/formats/bsh.md.
/// Sprites are decoded lazily on request; the sheet only keeps the compressed file in memory.
class SpriteSheet {
public:
    static core::Result<SpriteSheet> from_chunk_file(ChunkFile file);
    static core::Result<SpriteSheet> load(const std::filesystem::path& path);

    [[nodiscard]] std::uint64_t content_hash() const noexcept { return core::xxh64(m_file.data()); }
    [[nodiscard]] std::size_t size() const noexcept { return m_infos.size(); }
    [[nodiscard]] const SpriteInfo& info(std::size_t index) const { return m_infos.at(index); }

    /// Decodes one sprite, RLE, raw or true colour RLE by its type; any other type gives a
    /// transparent image.
    /// `extra_width` widens the image to keep pixels right of the record's width (font glyphs
    /// overhang into the next one).
    [[nodiscard]] core::Result<IndexedImage> decode(std::size_t index, std::uint32_t extra_width = 0) const;

private:
    [[nodiscard]] core::Result<IndexedImage> decode_true_colour(std::size_t index, const SpriteInfo& info,
                                                                IndexedImage image) const;

    ChunkFile m_file;
    Chunk m_chunk;  // stored by value so the sheet stays safely copyable/movable
    std::vector<SpriteInfo> m_infos;
};

}  // namespace o1602::assets
