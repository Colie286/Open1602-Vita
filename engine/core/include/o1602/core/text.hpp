// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace o1602::core {

/// Converts Windows-1252 (the encoding of all original text files) to UTF-8.
std::string cp1252_to_utf8(std::string_view input);

/// UTF-8 (typed text, Open1602's own texts) to the Windows-1252 of the game's fonts; what it cannot
/// show is left out.
[[nodiscard]] std::string utf8_to_cp1252(std::string_view utf8);

/// Whether `text` is well-formed UTF-8 (no stray or missing continuation bytes, no overlong forms,
/// no surrogates, nothing beyond U+10FFFF). Plain ASCII is.
[[nodiscard]] bool is_utf8(std::string_view text) noexcept;

/// `text` without a leading UTF-8 byte order mark (as some Windows editors write one).
[[nodiscard]] std::string_view skip_utf8_bom(std::string_view text) noexcept;

/// Compares two strings, ignoring the case of ASCII letters (file and savegame names).
[[nodiscard]] constexpr bool equals_ignore_case(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') {
            x = static_cast<char>(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = static_cast<char>(y - 'A' + 'a');
        }
        if (x != y) {
            return false;
        }
    }
    return true;
}

}  // namespace o1602::core
