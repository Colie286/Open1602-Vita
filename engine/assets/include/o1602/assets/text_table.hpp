// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texts of Text.cod: sections in brackets ([STAEDTE], [MELDUNG], ...) with one text per line up to
// [END] (docs/formats/cod.md). The original loads them at 0x0048F94D.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::assets {

class GameDirectory;

class TextTable {
public:
    /// Splits decoded Text.cod text (Windows-1252 or UTF-8, kept as given) into its sections.
    static TextTable parse(std::string_view text);
    /// Reads Text.cod of an installation (texts stay in Windows-1252).
    static core::Result<TextTable> load(const GameDirectory& game);

    /// Lines of a section without the trailing blanks of each line; empty if it does not exist.
    [[nodiscard]] const std::vector<std::string>& section(std::string_view name) const;

private:
    std::map<std::string, std::vector<std::string>, std::less<>> m_sections;
};

}  // namespace o1602::assets
