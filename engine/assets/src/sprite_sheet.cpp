// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/sprite_sheet.hpp"

#include <algorithm>

#include "o1602/core/byte_reader.hpp"
#include "o1602/core/file.hpp"

namespace o1602::assets {

using core::ErrorCode;
using core::make_error;

namespace {
constexpr std::uint32_t kRecordHeaderSize = 16;
constexpr std::uint8_t kEndOfImage = 0xFF;
constexpr std::uint8_t kEndOfRow = 0xFE;
// Record types (the low byte of +8 is what the original's blitters test, 0x00401B10): 1 and 6 are
// RLE, 2 is width x height raw palette indices with index 0 transparent. Every original record is 1.
// The History Edition adds 13: the same RLE in 32-bit words, each pixel a word B, G, R, 0 (its menu
// background and buttons in START.BSH and a few new buttons in TOOLS.BSH; docs/formats/bsh.md).
constexpr std::uint8_t kTypeRle = 1;
constexpr std::uint8_t kTypeRaw = 2;
constexpr std::uint8_t kTypeRleToo = 6;
constexpr std::uint8_t kTypeTrueColour = 13;
// Sanity limit: the largest original sprites are full-screen images (1024x768).
constexpr std::uint32_t kMaxDimension = 4096;
/// Pixels all sprites of one sheet may claim together (HI-18): 256 M, far above the original's
/// largest sheets. Decoding takes two bytes per pixel, and a font keeps all its glyphs.
constexpr std::uint64_t kMaxSheetPixels = std::uint64_t{256} << 20;
/// Pixels a sprite may claim per byte of its RLE data (HI-7). Transparent rows at the bottom cost
/// no byte, so the rows are no bound; the original's sprites claim at most 3 418 pixels per byte.
constexpr std::uint64_t kMaxPixelsPerByte = 16384;
}  // namespace

std::vector<std::uint8_t> IndexedImage::to_rgba(const Palette& palette) const {
    if (!rgba.empty()) {
        return rgba;
    }
    std::vector<std::uint8_t> out(indices.size() * 4, 0);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (opaque[i] != 0) {
            const Rgba& c = palette[indices[i]];
            out[i * 4 + 0] = c.r;
            out[i * 4 + 1] = c.g;
            out[i * 4 + 2] = c.b;
            out[i * 4 + 3] = 255;
        }
    }
    return out;
}

core::Result<SpriteSheet> SpriteSheet::from_chunk_file(ChunkFile file) {
    SpriteSheet sheet;
    sheet.m_file = std::move(file);
    const Chunk* chunk = sheet.m_file.find("BSH");
    if (chunk == nullptr) {
        chunk = sheet.m_file.find("ZEI");
    }
    if (chunk == nullptr) {
        return make_error(ErrorCode::InvalidFormat, "no BSH or ZEI chunk");
    }
    sheet.m_chunk = *chunk;

    const auto data = sheet.m_file.payload(sheet.m_chunk);
    if (data.size() < 4) {
        return make_error(ErrorCode::InvalidFormat, "sprite chunk too small");
    }

    // The payload starts with a table of uint32 offsets. The table has no explicit count;
    // it ends where the first (lowest) sprite record begins.
    std::size_t count = core::ByteReader::load_u32(data, 0) / 4;
    for (std::size_t i = 0; i < count; ++i) {
        if ((i + 1) * 4 > data.size()) {
            return make_error(ErrorCode::InvalidFormat, "offset table exceeds chunk");
        }
        const std::uint32_t offset = core::ByteReader::load_u32(data, i * 4);
        count = std::min<std::size_t>(count, offset / 4);
    }

    sheet.m_infos.reserve(count);
    std::uint64_t pixels = 0;
    for (std::size_t i = 0; i < count; ++i) {
        SpriteInfo info;
        info.offset = core::ByteReader::load_u32(data, i * 4);
        if (std::size_t{info.offset} + kRecordHeaderSize > data.size()) {
            return make_error(ErrorCode::InvalidFormat, "sprite " + std::to_string(i) + " outside of chunk");
        }
        info.width = core::ByteReader::load_u32(data, info.offset);
        info.height = core::ByteReader::load_u32(data, info.offset + 4);
        info.type = core::ByteReader::load_u32(data, info.offset + 8);
        info.length = core::ByteReader::load_u32(data, info.offset + 12);
        if (info.width > kMaxDimension || info.height > kMaxDimension) {
            return make_error(ErrorCode::InvalidFormat, "sprite " + std::to_string(i) + " has invalid size");
        }
        // The record's length (header, RLE data up to 0xFF, padded to 4 bytes with 0xFE) holds in all
        // 47 294 records of the original's 61 sheets: a record that leaves its chunk or has no room
        // for its header is refused, and the decoder reads nothing beyond it (HI-8). Records need
        // not touch: some sheets have 16 unused bytes between two.
        if (info.length < kRecordHeaderSize || info.length > data.size() - info.offset) {
            return make_error(ErrorCode::InvalidFormat, "sprite " + std::to_string(i) + " has an invalid length");
        }
        if (static_cast<std::uint8_t>(info.type) == kTypeRaw &&
            std::uint64_t{info.width} * info.height > info.length - kRecordHeaderSize) {
            return make_error(ErrorCode::InvalidFormat, "sprite " + std::to_string(i) + " is shorter than its pixels");
        }
        // A few bytes cannot claim a huge sprite (HI-7, HI-18). Several offsets may point at one
        // record, so the sheet as a whole has a budget too.
        const std::uint64_t rle_bytes = info.length - kRecordHeaderSize;
        if (std::uint64_t{info.width} * info.height > kMaxPixelsPerByte * rle_bytes) {
            return make_error(ErrorCode::InvalidFormat,
                              "sprite " + std::to_string(i) + " claims more pixels than its data can hold");
        }
        pixels += std::uint64_t{info.width} * info.height;
        if (pixels > kMaxSheetPixels) {
            return make_error(ErrorCode::InvalidFormat, "the sprites claim more pixels than a sheet may have");
        }
        sheet.m_infos.push_back(info);
    }
    return sheet;
}

core::Result<SpriteSheet> SpriteSheet::load(const std::filesystem::path& path) {
    auto file = ChunkFile::load(path);
    if (!file) {
        return file.error();
    }
    auto sheet = from_chunk_file(std::move(file).value());
    if (!sheet) {
        return make_error(sheet.error().code, core::path_to_utf8(path) + ": " + sheet.error().message);
    }
    return sheet;
}

core::Result<IndexedImage> SpriteSheet::decode_true_colour(std::size_t index, const SpriteInfo& info,
                                                           IndexedImage image) const {
    // As the RLE of type 1 in little-endian words: [skip][count][count pixels B, G, R, 0],
    // 0xFE = next row, 0xFF = end.
    const auto data = m_file.payload(m_chunk);
    image.rgba.assign(image.indices.size() * 4, 0);
    core::ByteReader reader(data.first(std::size_t{info.offset} + info.length));
    (void)reader.seek(info.offset + kRecordHeaderSize);
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    for (;;) {
        const auto code = reader.u32();
        if (!code) {
            return make_error(ErrorCode::InvalidFormat, "sprite " + std::to_string(index) + ": unterminated RLE data");
        }
        if (*code == kEndOfImage) {
            break;
        }
        if (*code == kEndOfRow) {
            x = 0;
            ++y;
            continue;
        }
        const auto run = reader.u32();
        if (!run || *run > reader.remaining() / 4) {
            return make_error(ErrorCode::InvalidFormat, "sprite " + std::to_string(index) + ": truncated run");
        }
        const auto pixels = reader.bytes(std::size_t{*run} * 4);
        if (!pixels) {
            return make_error(ErrorCode::InvalidFormat, "sprite " + std::to_string(index) + ": truncated pixels");
        }
        // A skip beyond the row only clips; adding it in 64 bits keeps x from wrapping around.
        std::uint64_t at = std::uint64_t{x} + *code;
        for (std::size_t p = 0; p < *run; ++p, ++at) {
            if (at < image.width && y < info.height) {
                const std::size_t i = std::size_t{y} * image.width + static_cast<std::size_t>(at);
                image.opaque[i] = 1;
                image.rgba[i * 4 + 0] = (*pixels)[p * 4 + 2];
                image.rgba[i * 4 + 1] = (*pixels)[p * 4 + 1];
                image.rgba[i * 4 + 2] = (*pixels)[p * 4 + 0];
                image.rgba[i * 4 + 3] = 255;
            } else {
                ++image.clipped_pixels;
            }
        }
        x = static_cast<std::uint32_t>(std::min<std::uint64_t>(at, kMaxDimension * 2));
    }
    return image;
}

core::Result<IndexedImage> SpriteSheet::decode(std::size_t index, std::uint32_t extra_width) const {
    if (index >= m_infos.size()) {
        return make_error(ErrorCode::OutOfRange, "sprite index out of range");
    }
    const SpriteInfo& info = m_infos[index];
    const auto data = m_file.payload(m_chunk);

    IndexedImage image;
    image.width = info.width + extra_width;
    image.height = info.height;
    const std::size_t pixel_count = std::size_t{image.width} * info.height;
    image.indices.assign(pixel_count, 0);
    image.opaque.assign(pixel_count, 0);

    const auto type = static_cast<std::uint8_t>(info.type);
    if (type == kTypeRaw) {
        // Rows of palette indices one after the other; 0 is not drawn (0x00403670).
        const auto pixels = data.subspan(std::size_t{info.offset} + kRecordHeaderSize);
        for (std::uint32_t y = 0; y < info.height; ++y) {
            for (std::uint32_t x = 0; x < info.width; ++x) {
                const std::uint8_t colour = pixels[std::size_t{y} * info.width + x];
                const std::size_t i = std::size_t{y} * image.width + x;
                image.indices[i] = colour;
                image.opaque[i] = colour != 0 ? 1 : 0;
            }
        }
        return image;
    }
    if (type == kTypeTrueColour) {
        return decode_true_colour(index, info, std::move(image));
    }
    if (type != kTypeRle && type != kTypeRleToo) {
        return image;  // the original draws nothing for any other type
    }

    // RLE stream: repeated [skip][count][count palette indices], 0xFE = next row, 0xFF = end.
    core::ByteReader reader(data.first(std::size_t{info.offset} + info.length));
    (void)reader.seek(info.offset + kRecordHeaderSize);
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    for (;;) {
        const auto code = reader.u8();
        if (!code) {
            return make_error(ErrorCode::InvalidFormat, "sprite " + std::to_string(index) + ": unterminated RLE data");
        }
        if (*code == kEndOfImage) {
            break;
        }
        if (*code == kEndOfRow) {
            x = 0;
            ++y;
            continue;
        }
        x += *code;
        const auto run = reader.u8();
        if (!run) {
            return make_error(ErrorCode::InvalidFormat, "sprite " + std::to_string(index) + ": truncated run");
        }
        const auto pixels = reader.bytes(*run);
        if (!pixels) {
            return make_error(ErrorCode::InvalidFormat, "sprite " + std::to_string(index) + ": truncated pixels");
        }
        for (const std::uint8_t colour : *pixels) {
            if (x < image.width && y < info.height) {
                const std::size_t i = std::size_t{y} * image.width + x;
                image.indices[i] = colour;
                image.opaque[i] = 1;
            } else {
                ++image.clipped_pixels;
            }
            ++x;
        }
    }
    return image;
}

}  // namespace o1602::assets
