// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/png_writer.hpp"

#include <algorithm>
#include <cassert>
#include <string_view>
#include <utility>

#include "o1602/core/compress.hpp"
#include "o1602/core/file.hpp"

namespace o1602::core {

namespace {

void put_u32_be(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void put_chunk(std::vector<std::uint8_t>& out, std::string_view type, std::span<const std::uint8_t> payload) {
    put_u32_be(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t type_pos = out.size();
    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), payload.begin(), payload.end());
    put_u32_be(out, crc32(std::span(out).subspan(type_pos)));  // over type and data
}

std::vector<std::uint8_t> zlib_compress(std::span<const std::uint8_t> raw) {
    if (auto compressed = zlib_deflate(raw)) {  // the one checked path (compress.hpp)
        return std::move(compressed).value();
    }
    // Fallback: zlib stream made of stored deflate blocks.
    std::vector<std::uint8_t> out = {0x78, 0x01};
    std::size_t pos = 0;
    do {
        const std::size_t len = std::min<std::size_t>(raw.size() - pos, 65535);
        const bool last = pos + len == raw.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(len));
        out.push_back(static_cast<std::uint8_t>(len >> 8));
        out.push_back(static_cast<std::uint8_t>(~len));
        out.push_back(static_cast<std::uint8_t>(~len >> 8));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                   raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    } while (pos < raw.size());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const auto byte : raw) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    put_u32_be(out, (b << 16) | a);
    return out;
}

}  // namespace

std::vector<std::uint8_t> encode_png_rgba(std::uint32_t width, std::uint32_t height,
                                          std::span<const std::uint8_t> rgba) {
    assert(rgba.size() == std::size_t{width} * height * 4);

    std::vector<std::uint8_t> raw;
    raw.reserve((std::size_t{width} * 4 + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);  // filter type: none
        const auto row = rgba.subspan(std::size_t{y} * width * 4, std::size_t{width} * 4);
        raw.insert(raw.end(), row.begin(), row.end());
    }

    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    put_u32_be(ihdr, width);
    put_u32_be(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8 bit, RGBA, deflate, no filter, no interlace
    put_chunk(png, "IHDR", ihdr);
    put_chunk(png, "IDAT", zlib_compress(raw));
    put_chunk(png, "IEND", {});
    return png;
}

Status write_png_rgba(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                      std::span<const std::uint8_t> rgba) {
    if (rgba.size() != std::size_t{width} * height * 4) {
        return make_error(ErrorCode::OutOfRange, "pixel buffer does not match image size");
    }
    return write_file(path, encode_png_rgba(width, height, rgba));
}

}  // namespace o1602::core
