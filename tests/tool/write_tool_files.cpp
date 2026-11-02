// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602_tool_files <dir>
//
// Writes the small synthetic files the o1602-tool smoke tests run on (tests/CMakeLists.txt), so they
// need no original data: a savegame-like chunk file with one island and one player, a .cod script, a
// sprite sheet, a palette, a truncated chunk file and an empty folder that is no installation.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <system_error>
#include <vector>

#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/core/file.hpp"

namespace fs = std::filesystem;
using namespace o1602;

namespace {

void put_u16(core::Bytes& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put_u32(core::Bytes& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

/// Island 3 (20x15 at 40,30) with two buildings, one human player "Tester" with 12345 gold, WIFF.
core::Bytes savegame() {
    core::Bytes island(116, 0);  // the INSEL5 record
    island[0] = 3;
    island[1] = 20;
    island[2] = 15;
    island[4] = 40;
    island[6] = 30;

    core::Bytes tiles;
    for (const std::uint8_t x : {std::uint8_t{2}, std::uint8_t{5}}) {
        put_u16(tiles, 1201);
        tiles.push_back(x);
        tiles.push_back(4);
        put_u32(tiles, 0);
    }

    core::Bytes player(0x430, 0);  // one PLAYER4 record: money, kind 0 (human), slot 0, colour 1, name
    player[0] = 0x39;              // 12345 = 0x3039
    player[1] = 0x30;
    player[7] = 1;
    constexpr std::string_view kName = "Tester";
    std::copy(kName.begin(), kName.end(), player.begin() + 0x3C0);

    assets::ChunkWriter writer;
    writer.add("INSEL5", island);
    writer.add("INSELHAUS", tiles);
    writer.add("PLAYER4", player);
    writer.add_checksum();
    return writer.take();
}

/// The building table's shape: a constant, a property and numbered HAUS records.
core::Bytes script() {
    return assets::encode_cod(
        "Nahrung = 4\r\n"
        "Version: 2\r\n"
        "Objekt: HAUS\r\n"
        "  @Nummer: 0\r\n"
        "  Id: 7\r\n"
        "  @Nummer: +1\r\n"
        "  Id: 8\r\n"
        "EndObj;\r\n");
}

/// One 4x2 sprite: row 0 "..AB", row 1 "C..." (the RLE of tests/unit/test_assets.cpp).
core::Bytes sprites() {
    const std::vector<std::uint8_t> rle = {2, 2, 0xA, 0xB, 0xFE, 0, 1, 0xC, 0xFF};
    core::Bytes payload;
    put_u32(payload, 4);  // offset table: one entry, the record starts at 4
    put_u32(payload, 4);  // width
    put_u32(payload, 2);  // height
    put_u32(payload, 1);  // type
    const auto length = static_cast<std::uint32_t>((16 + rle.size() + 3) & ~std::size_t{3});
    put_u32(payload, length);
    payload.insert(payload.end(), rle.begin(), rle.end());
    payload.resize(4 + length, 0);
    assets::ChunkWriter writer;
    writer.add("BSH", payload);
    return writer.take();
}

/// 256 grey RGBX entries.
core::Bytes palette() {
    core::Bytes payload;
    for (int i = 0; i < 256; ++i) {
        const auto v = static_cast<std::uint8_t>(i);
        payload.insert(payload.end(), {v, v, v, 0});
    }
    assets::ChunkWriter writer;
    writer.add("COL", payload);
    return writer.take();
}

/// A chunk that claims 100 bytes and has 3.
core::Bytes truncated() {
    assets::ChunkWriter writer;
    writer.add("INSEL5", core::Bytes(100, 0));
    core::Bytes bytes = writer.take();
    bytes.resize(16 + 4 + 3);
    return bytes;
}

/// The head of an Open1602 savegame (magic and an empty header), enough for the tool to tell it
/// from the original's files.
core::Bytes own_save_head() {
    const std::string_view magic = "OPEN1602";
    core::Bytes bytes(magic.begin(), magic.end());
    bytes.resize(32, 0);
    return bytes;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("usage: o1602_tool_files <dir>\n", stderr);
        return 2;
    }
    const fs::path dir(argv[1]);
    std::error_code ec;
    fs::create_directories(dir / "no-installation", ec);
    if (ec) {
        std::fprintf(stderr, "error: cannot create %s\n", (dir / "no-installation").string().c_str());
        return 1;
    }
    const struct {
        const char* name;
        core::Bytes bytes;
    } files[] = {
        {"world.gam", savegame()},  {"haeuser.cod", script()},      {"sprites.bsh", sprites()},
        {"palette.col", palette()}, {"truncated.gam", truncated()}, {"own.o1602", own_save_head()},
    };
    for (const auto& [name, bytes] : files) {
        if (auto status = core::write_file(dir / name, bytes); !status) {
            std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
            return 1;
        }
    }
    std::printf("wrote %zu files to %s\n", std::size(files), dir.string().c_str());
    return 0;
}
