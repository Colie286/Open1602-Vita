// SPDX-License-Identifier: GPL-3.0-or-later
//
// Names a content package gives what it adds, by language: `name.<language> = "..."` in its data
// files (goods.toml, classes.toml), kept in Windows-1252 as the game's fonts take them.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace o1602::assets {

/// Names by language code ("en", "de"), Windows-1252.
using LocalizedNames = std::vector<std::pair<std::string, std::string>>;

/// The name in `language`, else in English, else the first given, else `fallback`.
[[nodiscard]] std::string name_in(const LocalizedNames& names, std::string_view language, std::string_view fallback);

/// The language of a data file's key `name.<language>` ("pt-br": letters and '-', at most 16),
/// empty when `key` is no such key.
[[nodiscard]] std::string_view name_key_language(std::string_view key) noexcept;

}  // namespace o1602::assets
