// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/text.hpp"

#include <array>
#include <cstdint>

namespace o1602::core {

namespace {

// Unicode code points for bytes 0x80..0x9F in Windows-1252 (0 = undefined).
constexpr std::array<std::uint16_t, 32> kCp1252High = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}  // namespace

std::string cp1252_to_utf8(std::string_view input) {
    std::string out;
    out.reserve(input.size() + input.size() / 8);
    for (const char ch : input) {
        const auto b = static_cast<std::uint8_t>(ch);
        std::uint32_t cp = b;
        if (b >= 0x80 && b <= 0x9F) {
            cp = kCp1252High[b - 0x80];
            if (cp == 0) {
                cp = 0xFFFD;  // replacement character for undefined bytes
            }
        }
        append_utf8(out, cp);
    }
    return out;
}

std::string utf8_to_cp1252(std::string_view utf8) {
    // The letters of Windows-1252 at 0x80..0x9F that are not Latin-1.
    struct Letter {
        char32_t code;
        unsigned char byte;
    };
    static constexpr Letter kExtra[] = {
        {0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87},
        {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8A}, {0x2039, 0x8B}, {0x0152, 0x8C}, {0x017D, 0x8E}, {0x2018, 0x91},
        {0x2019, 0x92}, {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97}, {0x02DC, 0x98},
        {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B}, {0x0153, 0x9C}, {0x017E, 0x9E}, {0x0178, 0x9F},
    };
    std::string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        char32_t cp = 0;
        std::size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1FU;
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0FU;
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07U;
            len = 4;
        } else {
            ++i;  // a stray continuation byte
            continue;
        }
        if (i + len > utf8.size()) {
            break;
        }
        bool ok = true;
        for (std::size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(utf8[i + k]);
            ok = ok && (cc & 0xC0) == 0x80;
            cp = (cp << 6U) | (cc & 0x3FU);
        }
        i += len;
        if (!ok) {
            continue;
        }
        if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
            out.push_back(static_cast<char>(cp));
            continue;
        }
        for (const auto& [u, b] : kExtra) {
            if (u == cp) {
                out.push_back(static_cast<char>(b));
            }
        }
    }
    return out;
}

bool is_utf8(std::string_view text) noexcept {
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t len = 0;
        char32_t cp = 0;
        char32_t min = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1FU;
            min = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0FU;
            min = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07U;
            min = 0x10000;
        } else {
            return false;  // a stray continuation byte, or 0xF8..0xFF
        }
        if (i + len > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6U) | (cc & 0x3FU);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += len;
    }
    return true;
}

std::string_view skip_utf8_bom(std::string_view text) noexcept {
    return text.starts_with("\xEF\xBB\xBF") ? text.substr(3) : text;
}

}  // namespace o1602::core
