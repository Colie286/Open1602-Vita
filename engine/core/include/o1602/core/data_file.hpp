// SPDX-License-Identifier: GPL-3.0-or-later
//
// The data files of content packages (ADR 0007, section 2): a small part of TOML that is enough
// for tables of entries, so that a mod's file is valid TOML and editors highlight it.
//
//   # A comment.
//   [lighthouse]                  a table: bare (A-Z a-z 0-9 _ -) or quoted ["anno1602:42"]
//   text = "The lighthouse shines."
//   text.de = 'Der Leuchtturm strahlt.'
//   like = 0x2A                   integers: decimal or 0x hex, an optional sign, _ between digits
//   video_always = true
//
// Supported: tables, bare, quoted and dotted keys (kept as written, "text.de"), basic strings with
// the escapes \n \t \" \\ \uXXXX \UXXXXXXXX, literal strings, integers and booleans. Not supported
// (an error, never a guess): arrays, inline tables, floats, dates and multi-line strings.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::core {

/// A value: a string (UTF-8), an integer or a boolean.
struct DataValue {
    std::variant<std::string, std::int64_t, bool> value;
    std::size_t line = 0;  ///< 1-based, for messages to the modder

    [[nodiscard]] const std::string* string() const noexcept { return std::get_if<std::string>(&value); }
    [[nodiscard]] const std::int64_t* integer() const noexcept { return std::get_if<std::int64_t>(&value); }
    [[nodiscard]] const bool* boolean() const noexcept { return std::get_if<bool>(&value); }
};

struct DataEntry {
    std::string key;  ///< dotted keys joined with '.' ("text.de")
    DataValue value;
};

/// A table and its entries in the order of the file. Entries before the first table header belong
/// to a table with an empty name.
struct DataTable {
    std::string name;
    std::size_t line = 0;
    std::vector<DataEntry> entries;

    /// The entry of `key`, or nullptr.
    [[nodiscard]] const DataValue* find(std::string_view key) const noexcept;
};

/// Reads a data file (UTF-8, a byte order mark is passed over). An error names the line ("line 12:
/// ..."); a table or key given twice is one, as in TOML.
[[nodiscard]] Result<std::vector<DataTable>> parse_data_file(std::string_view utf8);

}  // namespace o1602::core
