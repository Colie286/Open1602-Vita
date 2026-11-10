// SPDX-License-Identifier: GPL-3.0-or-later
//
// The printf patterns of Text.cod ("%dt %s", "%0.1f", "%d%%") filled safely: the patterns are data,
// so they are never handed to printf. Arguments fill the conversions in order; a conversion without
// an argument stays empty, an argument of the wrong kind is converted.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <variant>

namespace o1602::ui {

using TextArg = std::variant<std::int64_t, double, std::string_view>;

/// Fills %d %i %u (integer), %s (text), %f with an optional precision (%0.1f, %.2f) and %%.
[[nodiscard]] std::string format_text(std::string_view pattern, std::initializer_list<TextArg> args);

}  // namespace o1602::ui
