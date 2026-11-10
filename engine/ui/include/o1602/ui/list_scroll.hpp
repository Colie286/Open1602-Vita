// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scrolling a list of rows with the mouse wheel, shared by the menus and windows that show one
// (Open1602; the original scrolls its lists only with their arrows).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace o1602::ui {

/// The first row shown after `notches` of the mouse wheel (up positive) on a list of `count`
/// entries with `rows` rows: a row per notch, not before the first entry, nor so far that the
/// last rows stay empty.
[[nodiscard]] constexpr std::size_t wheel_scroll(std::size_t first, std::size_t count, std::size_t rows,
                                                 std::int32_t notches) noexcept {
    const auto last = static_cast<std::int64_t>(count > rows ? count - rows : 0);
    return static_cast<std::size_t>(std::clamp<std::int64_t>(static_cast<std::int64_t>(first) - notches, 0, last));
}

}  // namespace o1602::ui
