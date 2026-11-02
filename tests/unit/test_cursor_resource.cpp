// SPDX-License-Identifier: GPL-3.0-or-later
// The original's mouse cursors from a PE file's resources.

#include <cstdint>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cursor_resource.hpp"

using namespace o1602;

namespace {

void put16(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    d[at] = static_cast<std::uint8_t>(v);
    d[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    put16(d, at, v & 0xFFFF);
    put16(d, at + 2, v >> 16);
}

/// A 32-bit PE file with one resource section: cursor group `group` with one 32 x 32 black-and-white
/// image (id 1), hot spot (5, 9). Its top row: transparent, white, black, inverted, then
/// transparent; the rest transparent.
std::vector<std::uint8_t> make_pe(std::uint16_t group) {
    constexpr std::size_t kSection = 0x200;
    constexpr std::uint32_t kRva = 0x1000;
    std::vector<std::uint8_t> d(kSection + 512, 0);
    d[0] = 'M';
    d[1] = 'Z';
    put32(d, 0x3C, 0x40);
    put32(d, 0x40, 0x4550);  // "PE\0\0"
    put16(d, 0x44, 0x14C);
    put16(d, 0x46, 1);    // one section
    put16(d, 0x54, 224);  // size of the optional header
    put16(d, 0x58, 0x10B);
    put32(d, 0x58 + 96 + 16, kRva);  // the resources' directory
    put32(d, 0x58 + 96 + 20, 488);
    const std::size_t sec = 0x58 + 224;
    put32(d, sec + 8, 512);
    put32(d, sec + 12, kRva);
    put32(d, sec + 16, 512);
    put32(d, sec + 20, kSection);
    const auto dir = [&](std::size_t at, std::uint32_t id, std::uint32_t target) {
        put16(d, kSection + at + 14, 1);  // one numbered entry
        put32(d, kSection + at + 16, id);
        put32(d, kSection + at + 20, target);
    };
    constexpr std::uint32_t kSub = 0x80000000U;
    // The root lists two types.
    put16(d, kSection + 14, 2);
    put32(d, kSection + 16, 1);
    put32(d, kSection + 20, kSub | 32);
    put32(d, kSection + 24, 12);
    put32(d, kSection + 28, kSub | 56);
    dir(32, 1, kSub | 80);
    dir(56, group, kSub | 104);
    dir(80, 0x409, 128);
    dir(104, 0x409, 144);
    put32(d, kSection + 128, kRva + 160);
    put32(d, kSection + 132, 308);
    put32(d, kSection + 144, kRva + 468);
    put32(d, kSection + 148, 20);
    // The image: hot spot, header, colours black and white, picture, mask.
    const std::size_t c = kSection + 160;
    put16(d, c, 5);
    put16(d, c + 2, 9);
    put32(d, c + 4, 40);
    put32(d, c + 8, 32);
    put32(d, c + 12, 64);
    put16(d, c + 16, 1);
    put16(d, c + 18, 1);
    d[c + 48] = d[c + 49] = d[c + 50] = 255;
    const std::size_t picture = c + 52;
    const std::size_t mask = picture + 128;
    for (std::size_t i = 0; i < 128; ++i) {
        d[mask + i] = 0xFF;
    }
    const std::size_t top = 31 * 4;  // rows from the bottom
    d[picture + top] = 0b0101'0000;  // white at x 1, inverted at x 3
    d[mask + top] = 0b1001'1111;     // opaque at x 1 and 2
    // The group: reserved, type 2, one image of id 1.
    const std::size_t g = kSection + 468;
    put16(d, g + 2, 2);
    put16(d, g + 4, 1);
    put16(d, g + 6, 32);
    put16(d, g + 8, 64);
    put16(d, g + 12, 1);
    put32(d, g + 14, 308);
    put16(d, g + 18, 1);
    return d;
}

}  // namespace

TEST_CASE("cursor resource: a black-and-white cursor with its hot spot") {
    const auto pe = make_pe(assets::kTargetCursor);
    const auto c = assets::read_cursor(pe, assets::kTargetCursor);
    REQUIRE(static_cast<bool>(c));
    CHECK(c->width == 32);
    CHECK(c->height == 32);
    CHECK(c->hot_x == 5);
    CHECK(c->hot_y == 9);
    REQUIRE(c->rgba.size() == 32U * 32U * 4U);
    const auto pixel = [&](std::size_t x, std::size_t y) {
        const std::uint8_t* p = &c->rgba[(y * 32 + x) * 4];
        return std::vector<std::uint8_t>{p[0], p[1], p[2], p[3]};
    };
    CHECK(pixel(0, 0)[3] == 0);
    CHECK((pixel(1, 0) == std::vector<std::uint8_t>{255, 255, 255, 255}));
    CHECK((pixel(2, 0) == std::vector<std::uint8_t>{0, 0, 0, 255}));
    CHECK((pixel(3, 0) == std::vector<std::uint8_t>{0, 0, 0, 255}));  // inverted: black
    CHECK(pixel(4, 0)[3] == 0);
    CHECK(pixel(1, 1)[3] == 0);
}

TEST_CASE("cursor resource: another id, cut and foreign files are refused") {
    const auto pe = make_pe(assets::kTargetCursor);
    CHECK(!assets::read_cursor(pe, assets::kArrowCursor));
    for (const std::size_t size : {std::size_t{0}, std::size_t{0x40}, std::size_t{0x200}, std::size_t{0x200 + 300}}) {
        const std::vector<std::uint8_t> cut(pe.begin(), pe.begin() + static_cast<std::ptrdiff_t>(size));
        CHECK(!assets::read_cursor(cut, assets::kTargetCursor));
    }
    std::vector<std::uint8_t> colour = pe;
    colour[0x200 + 160 + 18] = 8;  // 8 bits per pixel
    CHECK(!assets::read_cursor(colour, assets::kTargetCursor));
    std::vector<std::uint8_t> zip = pe;
    zip[0] = 'P';
    CHECK(!assets::read_cursor(zip, assets::kTargetCursor));
}
