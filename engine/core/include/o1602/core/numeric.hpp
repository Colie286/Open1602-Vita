// SPDX-License-Identifier: GPL-3.0-or-later
//
// Conversions of numbers from data files that every platform does the same way.
#pragma once

#include <concepts>
#include <limits>

namespace o1602::core {

/// A double as an integer: truncated toward zero, clamped to the range of `T`, 0 for NaN. A plain
/// cast of a double out of range is undefined, and x86 and ARM give different results, so a modded
/// data file could make the computers of a multiplayer game disagree.
template <std::integral T>
[[nodiscard]] constexpr T saturating_cast(double value) noexcept {
    if (value != value) {
        return 0;
    }
    if (value <= static_cast<double>(std::numeric_limits<T>::min())) {
        return std::numeric_limits<T>::min();
    }
    if (value >= static_cast<double>(std::numeric_limits<T>::max())) {
        return std::numeric_limits<T>::max();
    }
    return static_cast<T>(value);
}

}  // namespace o1602::core
