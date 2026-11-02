// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the asset loaders using small synthetic files (no original data needed).

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/compress.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/hash.hpp"
#include "o1602/core/numeric.hpp"
#include "o1602/core/png_writer.hpp"
#include "o1602/core/text.hpp"

using namespace o1602;

namespace {

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

void put_chunk(std::vector<std::uint8_t>& out, const char* name, const std::vector<std::uint8_t>& payload) {
    std::uint8_t header[16] = {};
    std::memcpy(header, name, std::strlen(name));
    out.insert(out.end(), header, header + 16);
    put_u32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

}  // namespace

TEST_CASE("chunk file: parses names, offsets and sizes") {
    std::vector<std::uint8_t> file;
    put_chunk(file, "FIRST", {1, 2, 3});
    put_chunk(file, "SECOND_LONG_NAME", {});
    put_chunk(file, "THIRD", {9});

    auto parsed = assets::ChunkFile::from_bytes(file);
    REQUIRE(parsed.ok());
    REQUIRE(parsed->chunks().size() == 3);
    CHECK(parsed->chunks()[0].name == "FIRST");
    CHECK(parsed->chunks()[0].offset == 20);
    CHECK(parsed->chunks()[0].size == 3);
    CHECK(parsed->chunks()[1].name == "SECOND_LONG_NAME");  // all 16 bytes used, no terminator
    CHECK(parsed->chunks()[1].size == 0);
    REQUIRE(parsed->find("THIRD") != nullptr);
    CHECK(parsed->payload(*parsed->find("THIRD"))[0] == 9);
    CHECK(parsed->find("MISSING") == nullptr);
}

TEST_CASE("chunk file: rejects truncated data") {
    std::vector<std::uint8_t> file;
    put_chunk(file, "DATA", {1, 2, 3, 4});
    file.resize(file.size() - 1);
    CHECK(!assets::ChunkFile::from_bytes(file).ok());

    std::vector<std::uint8_t> header_only(10, 'A');
    CHECK(!assets::ChunkFile::from_bytes(header_only).ok());
}

TEST_CASE("cod: decode is byte-wise negation and roundtrips") {
    const std::string plain = "  IDBODEN =   20101\r\n";
    const auto encoded = assets::encode_cod(plain);
    CHECK(encoded[0] == 0xE0);  // -0x20
    CHECK(assets::decode_cod(encoded) == plain);
    // Real-world sample: first bytes of haeuser.cod (f3 f6 c5 c3) decode to "\r\n;="
    const std::vector<std::uint8_t> sample = {0xF3, 0xF6, 0xC5, 0xC3};
    CHECK(assets::decode_cod(sample) == "\r\n;=");
}

TEST_CASE("text: cp1252 to utf8") {
    CHECK(core::cp1252_to_utf8("T\xfcrme") == "T\xc3\xbcrme");  // ue
    CHECK(core::cp1252_to_utf8("\x80") == "\xe2\x82\xac");      // euro sign
    CHECK(core::cp1252_to_utf8("plain") == "plain");
}

TEST_CASE("png: chunks carry the CRC-32 of core::crc32 (IEEE, as zlib's)") {
    // The check value of the CRC-32 of zlib and PNG.
    const std::string check = "123456789";
    CHECK(core::crc32(std::span(reinterpret_cast<const std::uint8_t*>(check.data()), check.size())) == 0xCBF43926U);
    // A bit-by-bit CRC-32 independent of the table of core::crc32.
    const auto reference = [](std::span<const std::uint8_t> data) {
        std::uint32_t c = 0xFFFFFFFFU;
        for (const std::uint8_t b : data) {
            c ^= b;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
        }
        return ~c;
    };
    const std::vector<std::uint8_t> rgba(std::size_t{3} * 2 * 4, 0x7F);
    const std::vector<std::uint8_t> png = core::encode_png_rgba(3, 2, rgba);
    const auto be32 = [&](std::size_t at) {
        return std::uint32_t{png[at]} << 24 | std::uint32_t{png[at + 1]} << 16 | std::uint32_t{png[at + 2]} << 8 |
               std::uint32_t{png[at + 3]};
    };
    std::size_t at = 8;  // after the signature
    int chunks = 0;
    while (at + 12 <= png.size()) {
        const std::uint32_t size = be32(at);
        REQUIRE(at + 12 + size <= png.size());
        const auto typed = std::span(png).subspan(at + 4, 4 + std::size_t{size});
        CHECK(be32(at + 8 + size) == reference(typed));
        at += 12 + size;
        ++chunks;
    }
    CHECK(at == png.size());
    CHECK(chunks == 3);                          // IHDR, IDAT, IEND
    CHECK(be32(png.size() - 4) == 0xAE426082U);  // the CRC every IEND chunk has
}

TEST_CASE("text: well-formed utf8 and the byte order mark") {
    CHECK(core::is_utf8(""));
    CHECK(core::is_utf8("plain"));
    CHECK(core::is_utf8("J\xc3\xbcrgen"));      // ue
    CHECK(core::is_utf8("\xe2\x82\xac"));       // euro sign
    CHECK(core::is_utf8("\xf0\x9f\x8f\xb0"));   // U+1F3F0, four bytes
    CHECK(!core::is_utf8("J\xfcrgen"));         // Windows-1252
    CHECK(!core::is_utf8("\xc3"));              // cut short
    CHECK(!core::is_utf8("\x80"));              // a stray continuation byte
    CHECK(!core::is_utf8("\xc0\xaf"));          // overlong
    CHECK(!core::is_utf8("\xed\xa0\x80"));      // a surrogate
    CHECK(!core::is_utf8("\xf4\x90\x80\x80"));  // beyond U+10FFFF
    CHECK(core::skip_utf8_bom("\xef\xbb\xbfx = 1") == "x = 1");
    CHECK(core::skip_utf8_bom("x = 1") == "x = 1");
    CHECK(core::skip_utf8_bom("\xef\xbb") == "\xef\xbb");
}

TEST_CASE("palette: reads 256 RGBX entries") {
    std::vector<std::uint8_t> payload;
    for (int i = 0; i < 256; ++i) {
        payload.insert(payload.end(), {static_cast<std::uint8_t>(i), 10, 20, 0});
    }
    std::vector<std::uint8_t> file;
    put_chunk(file, "COL", payload);
    auto chunks = assets::ChunkFile::from_bytes(file);
    REQUIRE(chunks.ok());
    auto palette = assets::Palette::from_chunk_file(*chunks);
    REQUIRE(palette.ok());
    CHECK((*palette)[7] == (assets::Rgba{7, 10, 20, 255}));
    CHECK((*palette)[255].r == 255);
}

namespace {

/// Builds a BSH file with one 4x2 sprite:
///   row 0: ..AB   (skip 2, run 2)
///   row 1: C...   (skip 0, run 1)
std::vector<std::uint8_t> make_test_bsh() {
    std::vector<std::uint8_t> rle = {2, 2, 0xA, 0xB, 0xFE, 0, 1, 0xC, 0xFF};
    std::vector<std::uint8_t> payload;
    put_u32(payload, 4);  // offset table: one entry, record starts at 4
    put_u32(payload, 4);  // width
    put_u32(payload, 2);  // height
    put_u32(payload, 1);  // type
    const auto length = static_cast<std::uint32_t>((16 + rle.size() + 3) & ~std::size_t{3});
    put_u32(payload, length);
    payload.insert(payload.end(), rle.begin(), rle.end());
    payload.resize(4 + length, 0);
    std::vector<std::uint8_t> file;
    put_chunk(file, "BSH", payload);
    return file;
}

}  // namespace

TEST_CASE("sprite sheet: decodes RLE rows with transparency") {
    auto chunks = assets::ChunkFile::from_bytes(make_test_bsh());
    REQUIRE(chunks.ok());
    auto sheet = assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    REQUIRE(sheet.ok());
    REQUIRE(sheet->size() == 1);
    CHECK(sheet->info(0).width == 4);
    CHECK(sheet->info(0).height == 2);

    auto image = sheet->decode(0);
    REQUIRE(image.ok());
    const std::vector<std::uint8_t> expected_mask = {0, 0, 1, 1, 1, 0, 0, 0};
    CHECK(image->opaque == expected_mask);
    CHECK(image->indices[2] == 0xA);
    CHECK(image->indices[3] == 0xB);
    CHECK(image->indices[4] == 0xC);
    CHECK(image->clipped_pixels == 0);
    CHECK(!sheet->decode(1).ok());
}

TEST_CASE("sprite sheet: unterminated RLE is an error, not a crash") {
    auto data = make_test_bsh();
    // Replace the end marker (0xFF) and padding with row-continuation bytes.
    for (std::size_t i = 20 + 4 + 16 + 8; i < data.size(); ++i) {
        data[i] = 0xFE;
    }
    auto chunks = assets::ChunkFile::from_bytes(data);
    REQUIRE(chunks.ok());
    auto sheet = assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    REQUIRE(sheet.ok());
    CHECK(!sheet->decode(0).ok());
}

TEST_CASE("sprite sheet: a few bytes cannot claim gigabytes of pixels (HI-7, HI-18)") {
    // One record of two rows, `n` offsets pointing at it.
    const auto sheet_of = [](std::uint32_t width, std::uint32_t height, std::uint32_t n) {
        std::vector<std::uint8_t> payload;
        for (std::uint32_t i = 0; i < n; ++i) {
            put_u32(payload, 4 * n);
        }
        put_u32(payload, width);
        put_u32(payload, height);
        put_u32(payload, 1);
        put_u32(payload, 24);
        payload.insert(payload.end(), {0, 1, 7, 0xFE, 0, 1, 7, 0xFF});
        std::vector<std::uint8_t> file;
        put_chunk(file, "BSH", payload);
        auto chunks = assets::ChunkFile::from_bytes(file);
        REQUIRE(chunks.ok());
        return assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    };
    CHECK(sheet_of(4096, 2, 1).ok());
    CHECK(!sheet_of(4096, 4096, 1).ok());  // 4096 rows from 8 bytes
    CHECK(sheet_of(4096, 32, 1).ok());     // 16 384 pixels per byte of RLE data
    CHECK(!sheet_of(4096, 33, 1).ok());
    // 40 000 offsets to the same 4096 x 2 record: 328 M pixels, over the sheet's budget.
    CHECK(sheet_of(4096, 2, 30'000).ok());
    CHECK(!sheet_of(4096, 2, 40'000).ok());
}

TEST_CASE("sprite sheet: a record stays within its length (HI-8)") {
    // Two records; the first one's RLE data has no end of its own, the second follows directly.
    const auto sheet_of = [](std::uint32_t first_length) {
        std::vector<std::uint8_t> payload;
        put_u32(payload, 8);
        put_u32(payload, 8 + 24);
        for (const std::uint8_t end : {std::uint8_t{0xFE}, std::uint8_t{0xFF}}) {
            put_u32(payload, 1);
            put_u32(payload, 1);
            put_u32(payload, 1);
            put_u32(payload, end == 0xFF ? 24 : first_length);
            payload.insert(payload.end(), {0, 1, 7, end, end, end, end, end});
        }
        std::vector<std::uint8_t> file;
        put_chunk(file, "BSH", payload);
        auto chunks = assets::ChunkFile::from_bytes(file);
        REQUIRE(chunks.ok());
        return assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    };
    const auto sheet = sheet_of(24);
    REQUIRE(sheet.ok());
    CHECK(sheet->decode(1).ok());
    CHECK(!sheet->decode(0).ok());  // ran into the second record before: now its end is the end
    CHECK(!sheet_of(15).ok());      // no room for the header
    CHECK(!sheet_of(49).ok());      // beyond the chunk
    CHECK(sheet_of(48).ok());       // up to the chunk's end

    // Transparent rows at the bottom cost no byte: the original's empty sprites are just their end
    // (0xFF), whatever their size.
    std::vector<std::uint8_t> payload;
    put_u32(payload, 4);
    put_u32(payload, 40);
    put_u32(payload, 30);
    put_u32(payload, 1);
    put_u32(payload, 20);
    payload.insert(payload.end(), {0xFF, 0xFE, 0xFE, 0xFE});
    std::vector<std::uint8_t> file;
    put_chunk(file, "BSH", payload);
    auto chunks = assets::ChunkFile::from_bytes(file);
    REQUIRE(chunks.ok());
    const auto empty = assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    REQUIRE(empty.ok());
    const auto image = empty->decode(0);
    REQUIRE(image.ok());
    CHECK(image->width == 40);
    CHECK(std::count(image->opaque.begin(), image->opaque.end(), 1) == 0);
}

TEST_CASE("sprite sheet: type 2 is raw pixels, unknown types draw nothing") {
    // A 3x2 record of the given type with six raw bytes (0 is transparent).
    const auto sheet_of = [](std::uint32_t type, std::uint32_t length) {
        std::vector<std::uint8_t> payload;
        put_u32(payload, 4);
        put_u32(payload, 3);
        put_u32(payload, 2);
        put_u32(payload, type);
        put_u32(payload, length);
        payload.insert(payload.end(), {5, 0, 6, 0, 7, 8, 0xFF, 0xFE});
        payload.resize(4 + length, 0);
        std::vector<std::uint8_t> file;
        put_chunk(file, "BSH", payload);
        auto chunks = assets::ChunkFile::from_bytes(file);
        REQUIRE(chunks.ok());
        return assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    };
    const auto raw = sheet_of(2, 24);
    REQUIRE(raw.ok());
    const auto image = raw->decode(0, 1);
    REQUIRE(image.ok());
    CHECK(image->width == 4);
    const std::vector<std::uint8_t> expected_indices = {5, 0, 6, 0, 0, 7, 8, 0};
    const std::vector<std::uint8_t> expected_mask = {1, 0, 1, 0, 0, 1, 1, 0};
    CHECK(image->indices == expected_indices);
    CHECK(image->opaque == expected_mask);
    CHECK(!sheet_of(2, 20).ok());  // four bytes for six pixels

    const auto unknown = sheet_of(3, 24);
    REQUIRE(unknown.ok());
    const auto blank = unknown->decode(0);
    REQUIRE(blank.ok());
    CHECK(blank->width == 3);
    CHECK(std::count(blank->opaque.begin(), blank->opaque.end(), 1) == 0);
}

TEST_CASE("sprite sheet: type 13 is the History Edition's true colour RLE in 32-bit words") {
    // A 3x2 record: row 0 ".AB" (skip 1, run 2), row 1 "C.." (skip 0, run 1); pixels B, G, R, 0.
    const auto sheet_of = [](const std::vector<std::uint32_t>& words) {
        std::vector<std::uint8_t> payload;
        put_u32(payload, 4);
        put_u32(payload, 3);
        put_u32(payload, 2);
        put_u32(payload, 13);
        put_u32(payload, static_cast<std::uint32_t>(16 + words.size() * 4));
        for (const std::uint32_t w : words) {
            put_u32(payload, w);
        }
        std::vector<std::uint8_t> file;
        put_chunk(file, "BSH", payload);
        auto chunks = assets::ChunkFile::from_bytes(file);
        REQUIRE(chunks.ok());
        return assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    };
    const auto sheet = sheet_of({1, 2, 0x00332211, 0x00AA5544, 0xFE, 0, 1, 0x00998877, 0xFF});
    REQUIRE(sheet.ok());
    const auto image = sheet->decode(0);
    REQUIRE(image.ok());
    const std::vector<std::uint8_t> expected_mask = {0, 1, 1, 1, 0, 0};
    CHECK(image->opaque == expected_mask);
    const std::vector<std::uint8_t> expected_rgba = {
        0, 0, 0, 0, 0x33, 0x22, 0x11, 255, 0xAA, 0x55, 0x44, 255, 0x99, 0x88, 0x77, 255, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(image->rgba == expected_rgba);
    CHECK(image->to_rgba(assets::Palette{}) == expected_rgba);  // its own colours, not the palette's

    // A run longer than the row is clipped; one longer than the record or no end is an error.
    const auto wide = sheet_of({2, 2, 1, 2, 0xFF});
    REQUIRE(wide.ok());
    REQUIRE(wide->decode(0).ok());
    CHECK(wide->decode(0)->clipped_pixels == 1);
    const auto long_run = sheet_of({0, 1000, 1, 0xFF});
    REQUIRE(long_run.ok());
    CHECK(!long_run->decode(0).ok());
    const auto endless = sheet_of({0, 1, 1, 0xFE});
    REQUIRE(endless.ok());
    CHECK(!endless->decode(0).ok());
}

TEST_CASE("islands: header and INSELHAUS tiles") {
    std::vector<std::uint8_t> header = {19, 70, 60, 0};
    put_u16(header, 90);
    put_u16(header, 50);
    header.resize(116, 0);

    std::vector<std::uint8_t> tiles;
    put_u16(tiles, 1201);
    tiles.push_back(3);
    tiles.push_back(4);
    put_u32(tiles, 0xDEADBEEF);

    std::vector<std::uint8_t> file;
    put_chunk(file, "INSEL5", header);
    put_chunk(file, "INSELHAUS", tiles);
    put_chunk(file, "HIRSCH2", {});

    auto chunks = assets::ChunkFile::from_bytes(file);
    REQUIRE(chunks.ok());
    auto islands = assets::load_islands(*chunks);
    REQUIRE(islands.ok());
    REQUIRE(islands->size() == 1);
    const auto& island = (*islands)[0];
    CHECK(island.header.number == 19);
    CHECK(island.header.width == 70);
    CHECK(island.header.height == 60);
    CHECK(island.header.pos_x == 90);
    CHECK(island.header.pos_y == 50);
    REQUIRE(island.layers.size() == 1);
    REQUIRE(island.layers[0].size() == 1);
    CHECK(island.layers[0][0].cod_id() == 21201);
    CHECK(island.layers[0][0].x == 3);
    CHECK(island.layers[0][0].y == 4);
    CHECK(island.layers[0][0].bits == 0xDEADBEEF);
}

TEST_CASE("islands: an island of more than 255 tiles keeps its size, tiles and deposits (O1602ISL)") {
    std::vector<std::uint8_t> header = {19, 44, 24, 0};  // the low bytes of 300 x 280
    put_u16(header, 90);
    put_u16(header, 50);
    header.resize(116, 0);
    header[0x1A] = 1;  // one deposit
    header[0x1C] = 2;  // iron
    header[0x1D] = 4;  // x low byte
    header[0x1E] = 5;
    std::vector<std::uint8_t> large;
    put_u16(large, 300);
    put_u16(large, 280);
    large.push_back(1);
    put_u16(large, 260);
    put_u16(large, 261);

    assets::IslandTile far;
    far.id = 1201;
    far.x = 299;
    far.y = 279;
    far.bits = 0xDEADBEEF;
    const std::vector<std::uint8_t> tiles = assets::large_tile_bytes({far});
    CHECK(tiles.size() == 10);

    std::vector<std::uint8_t> file;
    put_chunk(file, "INSEL5", header);
    put_chunk(file, std::string(assets::kLargeIslandChunk).c_str(), large);
    put_chunk(file, std::string(assets::kLargeTilesChunk).c_str(), tiles);
    auto chunks = assets::ChunkFile::from_bytes(file);
    REQUIRE(chunks.ok());
    auto islands = assets::load_islands(*chunks);
    REQUIRE(islands.ok());
    REQUIRE(islands->size() == 1);
    const auto& island = (*islands)[0];
    CHECK(island.header.width == 300);
    CHECK(island.header.height == 280);
    REQUIRE(island.header.deposits.size() == 1);
    CHECK(island.header.deposits[0].x == 260);
    CHECK(island.header.deposits[0].y == 261);
    REQUIRE(island.layers.size() == 1);
    REQUIRE(island.layers[0].size() == 1);
    CHECK(island.layers[0][0].x == 299);
    CHECK(island.layers[0][0].y == 279);
    CHECK(island.layers[0][0].bits == 0xDEADBEEF);
}

TEST_CASE("islands: INSEL5 takes its size class from the width, INSEL4 as stored") {
    const auto size_class = [](const char* chunk, std::uint8_t width, std::uint16_t stored) {
        std::vector<std::uint8_t> header = {1, width, 40, 0};
        header.resize(116, 0);
        header[0x60] = 15;
        header[0x62] = static_cast<std::uint8_t>(stored);
        std::vector<std::uint8_t> file;
        put_chunk(file, chunk, header);
        auto chunks = assets::ChunkFile::from_bytes(file);
        REQUIRE(chunks.ok());
        auto islands = assets::load_islands(*chunks);
        REQUIRE(islands.ok());
        REQUIRE(islands->size() == 1);
        return (*islands)[0].header;
    };
    // NORDNAT/med15: 50 tiles wide, stored "lit"; its base island is NORD/med15.
    CHECK(size_class("INSEL5", 50, 0).template_size == 2);
    CHECK(size_class("INSEL5", 50, 0).template_path() == "Nord/med15.scp");
    CHECK(size_class("INSEL5", 32, 4).template_size == 0);
    CHECK(size_class("INSEL5", 33, 0).template_size == 1);
    CHECK(size_class("INSEL5", 55, 0).template_size == 2);
    CHECK(size_class("INSEL5", 75, 0).template_size == 3);
    CHECK(size_class("INSEL5", 110, 0).template_size == 4);
    CHECK(size_class("INSEL5", 111, 1).template_size == 1);  // wider: as stored
    CHECK(size_class("INSEL4", 50, 0).template_size == 0);
}

TEST_CASE("file: a full disk is an error, and an atomic write keeps the old file then") {
    namespace fs = std::filesystem;
    if (fs::exists("/dev/full")) {
        // Every write to /dev/full fails with "no space left"; before the file is closed the data
        // may still sit in a buffer.
        const std::vector<std::uint8_t> data(100, 7);
        CHECK(!core::write_file("/dev/full", data).ok());
    }
    const fs::path dir = test::temp_path("o1602_test_atomic");
    std::error_code ec;
    fs::remove_all(dir, ec);
    REQUIRE(fs::create_directories(dir));
    const fs::path file = dir / "game.o1602";
    const std::vector<std::uint8_t> first = {1, 2, 3};
    const std::vector<std::uint8_t> second = {4, 5, 6, 7};
    REQUIRE(core::write_file_atomic(file, first).ok());
    REQUIRE(core::write_file_atomic(file, second).ok());
    CHECK(*core::read_file(file) == second);
    CHECK(!fs::exists(dir / "game.o1602.tmp"));
    // A target that cannot be replaced (a folder of that name): nothing left behind.
    REQUIRE(fs::create_directories(dir / "folder.o1602" / "inside"));
    CHECK(!core::write_file_atomic(dir / "folder.o1602", first).ok());
    CHECK(!fs::exists(dir / "folder.o1602.tmp"));
    fs::remove_all(dir, ec);
}

TEST_CASE("file: the head of a file is its first bytes, with the size of the whole") {
    namespace fs = std::filesystem;
    const fs::path dir = test::temp_path("o1602_test_head");
    std::error_code ec;
    fs::remove_all(dir, ec);
    REQUIRE(fs::create_directories(dir));
    std::vector<std::uint8_t> data(1000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(i);
    }
    REQUIRE(core::write_file_atomic(dir / "a.bin", data).ok());
    const auto head = core::read_file_head(dir / "a.bin", 100);
    REQUIRE(head.ok());
    CHECK(head->size == 1000);
    CHECK(head->bytes == std::vector<std::uint8_t>(data.begin(), data.begin() + 100));
    const auto whole = core::read_file_head(dir / "a.bin", 5000);
    REQUIRE(whole.ok());
    CHECK(whole->bytes == data);
    CHECK(!core::read_file_head(dir / "missing.bin", 100).ok());
    CHECK(!core::read_file_head(dir, 100).ok());
    fs::remove_all(dir, ec);
}

TEST_CASE("file: paths go to and from UTF-8 unchanged (a user named Juergen)") {
    namespace fs = std::filesystem;
    const std::string name = "J\xC3\xBCrgen";  // "Jürgen" in UTF-8
    CHECK(core::path_to_utf8(core::path_from_utf8(name)) == name);
    const fs::path dir = test::temp_path("o1602_test_utf8");
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path folder = dir / core::path_from_utf8(name);
    REQUIRE(fs::create_directories(folder));
    const std::vector<std::uint8_t> data = {1, 2, 3};
    REQUIRE(core::write_file(folder / "keys.cfg", data).ok());
    CHECK(*core::read_file(folder / "keys.cfg") == data);
    CHECK(core::path_to_utf8(folder.filename()) == name);
    // The case-insensitive lookup compares the names as UTF-8 as well.
    const auto found = core::find_case_insensitive(dir, "j\xC3\xBCrgen/KEYS.CFG");
    REQUIRE(found.has_value());
    CHECK(*core::read_file(*found) == data);
    // Nothing outside the root: no "..", no absolute path.
    CHECK(!core::find_case_insensitive(folder, "../j\xC3\xBCrgen/keys.cfg").has_value());
    CHECK(!core::find_case_insensitive(folder, "..\\j\xC3\xBCrgen\\keys.cfg").has_value());
    CHECK(!core::find_case_insensitive(dir, core::path_to_utf8(folder / "keys.cfg")).has_value());
    // A folder is no file.
    const auto folder_read = core::read_file(folder);
    CHECK(!folder_read.ok());
    fs::remove_all(dir, ec);

    CHECK(core::environment_variable("PATH").has_value());
    CHECK(!core::environment_variable("O1602_TEST_VARIABLE_THAT_IS_NOT_SET").has_value());
}

TEST_CASE("core: xxh64 gives the values of xxHash's reference implementation") {
    const auto text = [](std::string_view t) {
        return std::span(reinterpret_cast<const std::uint8_t*>(t.data()), t.size());
    };
    CHECK(core::xxh64({}) == 0xEF46DB3751D8E999ULL);
    CHECK(core::xxh64(text("abc")) == 0x44BC2CF5AD770999ULL);
    CHECK(core::xxh64(text("abc"), 1602) == 0xA9CF0FA6C252006AULL);
    // Every path: 32 bytes a step, then 8, 4 and single bytes (37 = 32 + 4 + 1; 1032 = 32 * 32 + 8).
    std::vector<std::uint8_t> bytes(37);
    for (std::size_t k = 0; k < bytes.size(); ++k) {
        bytes[k] = static_cast<std::uint8_t>(k);
    }
    CHECK(core::xxh64(bytes) == 0xD93FA2DFEE5C24C9ULL);
    bytes.clear();
    for (int r = 0; r < 4; ++r) {
        for (int k = 0; k < 256; ++k) {
            bytes.push_back(static_cast<std::uint8_t>(k));
        }
    }
    for (const char c : std::string_view("Open1602")) {
        bytes.push_back(static_cast<std::uint8_t>(c));
    }
    CHECK(core::xxh64(bytes) == 0xF104EC2A5DD9F46FULL);
}

TEST_CASE("core: sha256 gives the values of FIPS 180-4's examples") {
    const auto hex = [](const std::array<std::uint8_t, 32>& d) {
        std::string s;
        for (const std::uint8_t b : d) {
            s += "0123456789abcdef"[b >> 4];
            s += "0123456789abcdef"[b & 15];
        }
        return s;
    };
    const auto text = [](std::string_view t) {
        return std::span(reinterpret_cast<const std::uint8_t*>(t.data()), t.size());
    };
    CHECK(hex(core::sha256({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hex(core::sha256(text("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // Two blocks of padding: 56 bytes leave no room for the length in the first.
    CHECK(hex(core::sha256(text("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const std::vector<std::uint8_t> million(1'000'000, 'a');
    CHECK(hex(core::sha256(million)) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    const std::vector<std::uint8_t> block(64, 'x');
    CHECK(hex(core::sha256(block)) == "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c");
}

TEST_CASE("core: a ByteWriter writes lists of numbers little-endian and reuses a buffer from its start") {
    core::ByteWriter out;
    out.u8(9);
    const std::uint16_t halves[] = {0x0102, 0xA0B0};
    const std::uint32_t words[] = {0x01020304U};
    out.u16s(halves);
    out.u32s(words);
    out.u16(0x1234);
    std::vector<std::uint8_t> bytes = out.take();
    const std::vector<std::uint8_t> expected{9, 0x02, 0x01, 0xB0, 0xA0, 0x04, 0x03, 0x02, 0x01, 0x34, 0x12};
    CHECK(bytes == expected);
    const std::uint8_t* memory = bytes.data();
    core::ByteWriter again(std::move(bytes));
    CHECK(again.size() == 0);
    again.u32(7);
    const std::vector<std::uint8_t> seven{7, 0, 0, 0};
    CHECK(again.data() == seven);
    CHECK(again.data().data() == memory);  // the same memory, no new allocation
}

TEST_CASE("core: doubles from data files become integers the same way everywhere") {
    CHECK(core::saturating_cast<std::uint32_t>(2.9) == 2u);
    CHECK(core::saturating_cast<std::uint32_t>(-3.5) == 0u);
    CHECK(core::saturating_cast<std::uint32_t>(1e10) == 0xFFFFFFFFu);
    CHECK(core::saturating_cast<std::int32_t>(-1e10) == std::numeric_limits<std::int32_t>::min());
    CHECK(core::saturating_cast<std::int32_t>(-2.9) == -2);
    CHECK(core::saturating_cast<std::uint8_t>(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(core::saturating_cast<std::int64_t>(1e30) == std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("assets: a game folder typed with quotes or blanks around it is still found") {
    using assets::GameDirectory;
    CHECK(GameDirectory::clean_path_text("\"C:\\Users\\Jonas\\Documents\\Anno 1602\"") ==
          "C:\\Users\\Jonas\\Documents\\Anno 1602");
    CHECK(GameDirectory::clean_path_text("C:\\GOG Games\\Anno 1602\"") == "C:\\GOG Games\\Anno 1602");  // "...\"
    CHECK(GameDirectory::clean_path_text("  /home/a/Anno 1602 \r\n") == "/home/a/Anno 1602");
    CHECK(GameDirectory::clean_path_text(" \" /games/anno \" ") == "/games/anno");
    CHECK(GameDirectory::clean_path_text("\"\"").empty());
    CHECK(GameDirectory::clean_path_text("/plain") == "/plain");

    const auto root = test::temp_path("o1602-test-quoted-game-dir");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "TOOLGFX");
    std::filesystem::create_directories(root / "GFX");
    for (const char* file : {"Haeuser.cod", "Figuren.cod", "TOOLGFX/STADTFLD.COL", "GFX/STADTFLD.BSH"}) {
        std::ofstream(root / file) << "";
    }
    CHECK(!GameDirectory::open(core::path_from_utf8("\"" + core::path_to_utf8(root) + "\"")));
    CHECK(GameDirectory::open(
        core::path_from_utf8(GameDirectory::clean_path_text("\"" + core::path_to_utf8(root) + "\""))));
    std::filesystem::remove_all(root);
}

TEST_CASE("assets: installers' folders named after the game are candidates, others are not") {
    const auto parent = test::temp_path("o1602-test-install-candidates");
    std::filesystem::remove_all(parent);
    for (const char* name : {"Anno 1602 Koenigsedition", "Anno 1602", "Anno 1503", "1602 AD"}) {
        std::filesystem::create_directories(parent / name);
    }
    std::ofstream(parent / "Anno 1602.txt") << "";  // a file, not a folder
    const std::vector<std::filesystem::path> expected{parent / "1602 AD", parent / "Anno 1602",
                                                      parent / "Anno 1602 Koenigsedition"};
    CHECK(assets::GameDirectory::folders_named_1602(parent) == expected);
    CHECK(assets::GameDirectory::folders_named_1602(parent / "missing").empty());
    std::filesystem::remove_all(parent);
}

TEST_CASE("assets: the History Edition's files of a language come from its folder in Loca") {
    using assets::GameDirectory;
    const auto root = test::temp_path("o1602-test-history-edition");
    std::filesystem::remove_all(root);
    for (const char* folder : {"ToolGfx", "GFX", "Loca/de-DE/ToolGfx", "Loca/en-US", "Loca/fr-FR"}) {
        std::filesystem::create_directories(root / folder);
    }
    for (const char* file :
         {"haeuser.cod", "figuren.cod", "ToolGfx/STADTFLD.COL", "ToolGfx/START.BSH", "GFX/STADTFLD.BSH", "text.cod",
          "Loca/de-DE/Text.cod", "Loca/de-DE/ToolGfx/START.BSH", "Loca/fr-FR/Text.cod"}) {
        std::ofstream(root / file) << "";
    }

    CHECK(GameDirectory::language_folder(root, "de") == root / "Loca" / "de-DE");
    CHECK(GameDirectory::language_folder(root, "fr-FR") == root / "Loca" / "fr-FR");
    CHECK(GameDirectory::language_folder(root / "Loca" / "en-US").has_value() == false);  // no Loca there

    auto game = GameDirectory::open(root);
    REQUIRE(game);
    game->choose_language("de");
    CHECK(game->localized_root() == root / "Loca" / "de-DE");
    CHECK(test::same_file(game->resolve("Text.cod"), root / "Loca" / "de-DE" / "Text.cod"));
    CHECK(test::same_file(game->resolve("TOOLGFX/START.BSH"), root / "Loca" / "de-DE" / "ToolGfx" / "START.BSH"));
    CHECK(test::same_file(game->resolve("toolgfx/stadtfld.col"), root / "ToolGfx" / "STADTFLD.COL"));  // not localized
    game->choose_language("fr");
    CHECK(test::same_file(game->resolve("Text.cod"), root / "Loca" / "fr-FR" / "Text.cod"));
    CHECK(test::same_file(game->resolve("ToolGfx/START.BSH"), root / "ToolGfx" / "START.BSH"));
    game->choose_language("xx");  // none of that language: the folder stays
    CHECK(game->localized_root() == root / "Loca" / "fr-FR");
    std::filesystem::remove_all(root / "Loca" / "en-US");
    game->choose_language("en");  // no English folder: the installation's own files are English
    CHECK(game->localized_root().empty());
    CHECK(test::same_file(game->resolve("Text.cod"), root / "text.cod"));
    std::filesystem::create_directories(root / "Loca" / "en-US");

    // A folder of a single language is taken whatever is asked; other editions have none.
    std::filesystem::remove_all(root / "Loca" / "en-US");
    std::filesystem::remove_all(root / "Loca" / "fr-FR");
    CHECK(GameDirectory::language_folder(root, "xx") == root / "Loca" / "de-DE");
    std::filesystem::remove_all(root / "Loca");
    auto plain = GameDirectory::open(root);
    REQUIRE(plain);
    CHECK(plain->localized_root().empty());
    CHECK(test::same_file(plain->resolve("Text.cod"), root / "text.cod"));
    std::filesystem::remove_all(root);
}

TEST_CASE("assets: the candidates below a home folder include Wine, CrossOver and Bottles prefixes") {
    const auto home = test::temp_path("o1602-test-home-candidates");
    std::filesystem::remove_all(home);
    const std::vector<std::filesystem::path> installs{
        home / "GOG Games" / "Anno 1602",
        home / ".wine" / "drive_c" / "GOG Games" / "Anno 1602",
        home / "Library" / "Application Support" / "CrossOver" / "Bottles" / "Anno" / "drive_c" / "GOG Games" /
            "Anno 1602 A.D",
        home / ".local" / "share" / "bottles" / "bottles" / "Games" / "drive_c" / "Program Files (x86)" / "Sunflowers" /
            "Anno 1602",
        home / "Games" / "anno-1602" / "drive_c" / "GOG Games" / "Anno 1602",
        home / "Applications" / "Anno 1602",
        // The History Edition installed with Ubisoft Connect in Heroic's prefix of its own.
        home / "Games" / "Heroic" / "Prefixes" / "default" / "Ubisoft Connect" / "drive_c" / "Program Files (x86)" /
            "Ubisoft" / "Ubisoft Game Launcher" / "games" / "Anno 1602 - History Edition",
    };
    for (const auto& folder : installs) {
        std::filesystem::create_directories(folder);
    }
    const auto found = assets::GameDirectory::home_candidates(home);
    for (const auto& folder : installs) {
        CHECK(std::find(found.begin(), found.end(), folder) != found.end());
    }
    CHECK(assets::GameDirectory::home_candidates(home / "missing").empty());
    std::filesystem::remove_all(home);
}
