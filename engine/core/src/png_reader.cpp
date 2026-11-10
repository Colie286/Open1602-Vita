// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/png_reader.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>

#include "o1602/core/compress.hpp"

namespace o1602::core {

namespace {

constexpr std::array<std::uint8_t, 8> kSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

/// Colour types of IHDR.
constexpr std::uint8_t kGrey = 0;
constexpr std::uint8_t kRgb = 2;
constexpr std::uint8_t kPalette = 3;
constexpr std::uint8_t kGreyAlpha = 4;
constexpr std::uint8_t kRgba = 6;

std::uint32_t u32_be(std::span<const std::uint8_t> d, std::size_t at) noexcept {
    return std::uint32_t{d[at]} << 24 | std::uint32_t{d[at + 1]} << 16 | std::uint32_t{d[at + 2]} << 8 | d[at + 3];
}

Error damaged(std::string_view what) {
    return make_error(ErrorCode::InvalidFormat, "PNG: " + std::string(what));
}

struct Chunk {
    std::string_view type;
    std::span<const std::uint8_t> data;
};

/// The chunks of a file in order, each checked against its CRC; stops after IEND.
Result<std::vector<Chunk>> chunks(std::span<const std::uint8_t> file) {
    if (file.size() < kSignature.size() || !std::equal(kSignature.begin(), kSignature.end(), file.begin())) {
        return damaged("not a PNG file");
    }
    std::vector<Chunk> out;
    std::size_t at = kSignature.size();
    while (at + 12 <= file.size()) {
        const std::uint32_t length = u32_be(file, at);
        if (length > file.size() - at - 12) {
            return damaged("a chunk runs past the end of the file");
        }
        const auto typed = file.subspan(at + 4, std::size_t{length} + 4);  // type and data
        if (crc32(typed) != u32_be(file, at + 8 + length)) {
            return damaged("a chunk is damaged (CRC)");
        }
        Chunk c;
        c.type = std::string_view(reinterpret_cast<const char*>(typed.data()), 4);
        c.data = typed.subspan(4);
        out.push_back(c);
        at += std::size_t{length} + 12;
        if (c.type == "IEND") {
            return out;
        }
    }
    return damaged("the file ends before IEND");
}

struct Header {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t depth = 0;
    std::uint8_t colour = 0;
    std::uint8_t interlace = 0;

    [[nodiscard]] std::uint32_t channels() const noexcept {
        switch (colour) {
            case kRgb:
                return 3;
            case kGreyAlpha:
                return 2;
            case kRgba:
                return 4;
            default:
                return 1;
        }
    }
    /// Bytes of one row without its filter byte.
    [[nodiscard]] std::size_t row_bytes() const noexcept { return (std::size_t{width} * channels() * depth + 7) / 8; }
    /// Bytes per pixel the filters step back by (at least one).
    [[nodiscard]] std::size_t filter_step() const noexcept { return std::max<std::size_t>(1, channels() * depth / 8); }
};

Result<Header> header(const std::vector<Chunk>& list) {
    if (list.empty() || list.front().type != "IHDR" || list.front().data.size() != 13) {
        return damaged("the file does not start with IHDR");
    }
    const auto d = list.front().data;
    Header h;
    h.width = u32_be(d, 0);
    h.height = u32_be(d, 4);
    h.depth = d[8];
    h.colour = d[9];
    h.interlace = d[12];
    const bool depth_ok =
        (h.colour == kGrey && (h.depth == 1 || h.depth == 2 || h.depth == 4 || h.depth == 8 || h.depth == 16)) ||
        (h.colour == kPalette && (h.depth == 1 || h.depth == 2 || h.depth == 4 || h.depth == 8)) ||
        ((h.colour == kRgb || h.colour == kGreyAlpha || h.colour == kRgba) && (h.depth == 8 || h.depth == 16));
    if (!depth_ok || d[10] != 0 || d[11] != 0 || h.interlace > 1) {
        return damaged("unknown colour type, bit depth, compression or filter method");
    }
    if (h.width == 0 || h.height == 0) {
        return damaged("the image is empty");
    }
    if (h.width > kMaxPngSide || h.height > kMaxPngSide || std::uint64_t{h.width} * h.height > kMaxPngPixels) {
        return make_error(ErrorCode::LimitExceeded, "PNG: " + std::to_string(h.width) + " x " +
                                                        std::to_string(h.height) + " pixels, at most " +
                                                        std::to_string(kMaxPngSide) + " a side and " +
                                                        std::to_string(kMaxPngPixels >> 20) + " megapixels");
    }
    return h;
}

std::uint8_t paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c) noexcept {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

/// Undoes the filters of the rows in place; `raw` holds each row after its filter byte.
bool unfilter(std::vector<std::uint8_t>& raw, const Header& h) {
    const std::size_t row = h.row_bytes();
    const std::size_t step = h.filter_step();
    for (std::uint32_t y = 0; y < h.height; ++y) {
        std::uint8_t* line = raw.data() + std::size_t{y} * (row + 1);
        const std::uint8_t filter = line[0];
        std::uint8_t* cur = line + 1;
        const std::uint8_t* up = y > 0 ? cur - (row + 1) : nullptr;
        for (std::size_t i = 0; i < row; ++i) {
            const std::uint8_t a = i >= step ? cur[i - step] : 0;
            const std::uint8_t b = up != nullptr ? up[i] : 0;
            const std::uint8_t c = up != nullptr && i >= step ? up[i - step] : 0;
            switch (filter) {
                case 0:
                    break;
                case 1:
                    cur[i] = static_cast<std::uint8_t>(cur[i] + a);
                    break;
                case 2:
                    cur[i] = static_cast<std::uint8_t>(cur[i] + b);
                    break;
                case 3:
                    cur[i] = static_cast<std::uint8_t>(cur[i] + (a + b) / 2);
                    break;
                case 4:
                    cur[i] = static_cast<std::uint8_t>(cur[i] + paeth(a, b, c));
                    break;
                default:
                    return false;
            }
        }
    }
    return true;
}

/// Sample `index` of a row of samples of `depth` bits, scaled to 8 bits unless `keep` (palette).
std::uint8_t sample(const std::uint8_t* line, std::size_t index, std::uint8_t depth, bool keep) noexcept {
    switch (depth) {
        case 16:
            return line[index * 2];
        case 8:
            return line[index];
        default: {
            const std::size_t bit = index * depth;
            const auto max = static_cast<unsigned>((1U << depth) - 1);
            const unsigned v = (line[bit / 8] >> (8 - depth - bit % 8)) & max;
            return static_cast<std::uint8_t>(keep ? v : v * 255 / max);
        }
    }
}

/// The full sample value (for comparing with tRNS, which gives it at the image's depth).
std::uint32_t raw_sample(const std::uint8_t* line, std::size_t index, std::uint8_t depth) noexcept {
    if (depth == 16) {
        return std::uint32_t{line[index * 2]} << 8 | line[index * 2 + 1];
    }
    if (depth == 8) {
        return line[index];
    }
    const std::size_t bit = index * depth;
    return (line[bit / 8] >> (8 - depth - bit % 8)) & ((1U << depth) - 1);
}

}  // namespace

Result<PngImage> png_size(std::span<const std::uint8_t> file) {
    // The signature and IHDR are the first 33 bytes: the rest of the file is not needed.
    constexpr std::size_t kHead = kSignature.size() + 12 + 13;
    if (file.size() < kHead || !std::equal(kSignature.begin(), kSignature.end(), file.begin()) ||
        u32_be(file, 8) != 13) {
        return damaged("not a PNG file");
    }
    const auto typed = file.subspan(12, 17);
    if (crc32(typed) != u32_be(file, 29)) {
        return damaged("a chunk is damaged (CRC)");
    }
    const std::vector<Chunk> list = {
        Chunk{std::string_view(reinterpret_cast<const char*>(typed.data()), 4), typed.subspan(4)}};
    auto h = header(list);
    if (!h) {
        return h.error();
    }
    PngImage image;
    image.width = h->width;
    image.height = h->height;
    return image;
}

Result<PngImage> decode_png(std::span<const std::uint8_t> file) {
    auto list = chunks(file);
    if (!list) {
        return list.error();
    }
    auto h = header(*list);
    if (!h) {
        return h.error();
    }
    if (h->interlace != 0) {
        return make_error(ErrorCode::Unsupported, "PNG: interlaced images are not read; save it without interlacing");
    }
    std::vector<std::uint8_t> packed;
    std::span<const std::uint8_t> palette;
    std::span<const std::uint8_t> transparency;
    for (const Chunk& c : *list) {
        if (c.type == "IDAT") {
            packed.insert(packed.end(), c.data.begin(), c.data.end());
        } else if (c.type == "PLTE") {
            palette = c.data;
        } else if (c.type == "tRNS") {
            transparency = c.data;
        }
    }
    if (h->colour == kPalette && (palette.empty() || palette.size() % 3 != 0 || palette.size() > 768)) {
        return damaged("a palette image without a valid PLTE");
    }
    const std::size_t row = h->row_bytes();
    auto raw = zlib_inflate(packed, std::size_t{h->height} * (row + 1));
    if (!raw) {
        return damaged("the pixel data is damaged");
    }
    if (!unfilter(*raw, *h)) {
        return damaged("unknown row filter");
    }

    PngImage image;
    image.width = h->width;
    image.height = h->height;
    image.rgba.resize(std::size_t{h->width} * h->height * 4);
    const std::uint32_t channels = h->channels();
    for (std::uint32_t y = 0; y < h->height; ++y) {
        const std::uint8_t* line = raw->data() + std::size_t{y} * (row + 1) + 1;
        std::uint8_t* out = image.rgba.data() + std::size_t{y} * h->width * 4;
        for (std::uint32_t x = 0; x < h->width; ++x, out += 4) {
            const std::size_t s = std::size_t{x} * channels;
            switch (h->colour) {
                case kPalette: {
                    const std::uint8_t i = sample(line, x, h->depth, true);
                    if (std::size_t{i} * 3 + 2 >= palette.size()) {
                        return damaged("a pixel names a colour beyond the palette");
                    }
                    out[0] = palette[std::size_t{i} * 3];
                    out[1] = palette[std::size_t{i} * 3 + 1];
                    out[2] = palette[std::size_t{i} * 3 + 2];
                    out[3] = i < transparency.size() ? transparency[i] : 255;
                    break;
                }
                case kGrey:
                case kGreyAlpha: {
                    const std::uint8_t v = sample(line, s, h->depth, false);
                    out[0] = out[1] = out[2] = v;
                    if (h->colour == kGreyAlpha) {
                        out[3] = sample(line, s + 1, h->depth, false);
                    } else {
                        const bool clear =
                            transparency.size() >= 2 &&
                            raw_sample(line, s, h->depth) == (std::uint32_t{transparency[0]} << 8 | transparency[1]);
                        out[3] = clear ? 0 : 255;
                    }
                    break;
                }
                default: {  // RGB, RGBA
                    for (std::uint32_t k = 0; k < 3; ++k) {
                        out[k] = sample(line, s + k, h->depth, false);
                    }
                    if (h->colour == kRgba) {
                        out[3] = sample(line, s + 3, h->depth, false);
                    } else {
                        bool clear = transparency.size() >= 6;
                        for (std::uint32_t k = 0; k < 3 && clear; ++k) {
                            const std::uint32_t key = std::uint32_t{transparency[k * 2]} << 8 | transparency[k * 2 + 1];
                            clear = raw_sample(line, s + k, h->depth) == key;
                        }
                        out[3] = clear ? 0 : 255;
                    }
                    break;
                }
            }
        }
    }
    return image;
}

}  // namespace o1602::core
