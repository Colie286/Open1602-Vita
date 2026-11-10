// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The History Edition's texts: data/texts.xml in ahe/a1he0.rda, one line per number with a
// translation per language. Its scenario files are named by these numbers ("Szenes/[[7]].szs" is
// "Das Atoll"), and its Text.cod and scenarios hold placeholders [[N]] for them
// (docs/formats/rda.md).

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::assets {

class HistoryTexts {
public:
    /// Parses texts.xml (UTF-8).
    static core::Result<HistoryTexts> parse(std::string_view xml);
    /// data/texts.xml of the archive `rda` (the History Edition's ahe/a1he0.rda).
    static core::Result<HistoryTexts> load(const std::filesystem::path& rda);

    /// Line `id` in `language` ("de", "en", ...; the file's codes are "de_de", "en_us", ...), else
    /// in English; UTF-8. Nothing if the line is unknown.
    [[nodiscard]] std::optional<std::string> text(std::int32_t id, std::string_view language) const;

    /// The numbers of the lines that are `name` (case-insensitive, UTF-8): those in `language`
    /// first, then those in English, then those in any language.
    [[nodiscard]] std::vector<std::int32_t> find_all(std::string_view name, std::string_view language) const;

    /// `text` with each placeholder [[N]] of a known line replaced by its text in `language`.
    [[nodiscard]] std::string expand(std::string_view text, std::string_view language) const;

    [[nodiscard]] std::size_t size() const noexcept { return m_lines.size(); }

private:
    std::map<std::string, std::int32_t> m_languages;                      ///< "de" -> languageID
    std::map<std::int32_t, std::map<std::int32_t, std::string>> m_lines;  ///< lineID -> languageID -> text
};

}  // namespace o1602::assets
