// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/history_texts.hpp"

#include <algorithm>
#include <charconv>

#include "o1602/assets/rda_archive.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"

namespace o1602::assets {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::string_view kTextsFile = "data/texts.xml";
/// The file's language for English, if it has no list of languages naming one.
constexpr std::int32_t kEnglishId = 1;

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

/// The language part of a code: "de" of "de_de", "de-DE" or "de".
std::string primary_language(std::string_view code) {
    return lower_ascii(code.substr(0, code.find_first_of("-_")));
}

/// The value of the attribute `name` in the tag `tag` ("<line lineID=\"7\">"), nothing if absent.
std::optional<std::int32_t> attribute(std::string_view tag, std::string_view name) {
    const std::string key = std::string(name) + "=\"";
    const std::size_t at = tag.find(key);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    const char* first = tag.data() + at + key.size();
    std::int32_t value = 0;
    const auto [end, ec] = std::from_chars(first, tag.data() + tag.size(), value);
    if (ec != std::errc{} || end == tag.data() + tag.size() || *end != '"') {
        return std::nullopt;
    }
    return value;
}

/// XML text without its five predefined entities and numeric references.
std::string unescape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        const std::size_t semicolon = text.find(';', i);
        if (semicolon == std::string_view::npos || semicolon - i > 10) {
            out.push_back('&');
            continue;
        }
        const std::string_view entity = text.substr(i + 1, semicolon - i - 1);
        std::uint32_t code = 0;
        if (entity == "amp") {
            code = '&';
        } else if (entity == "lt") {
            code = '<';
        } else if (entity == "gt") {
            code = '>';
        } else if (entity == "quot") {
            code = '"';
        } else if (entity == "apos") {
            code = '\'';
        } else if (entity.size() > 1 && entity[0] == '#') {
            const bool hex = entity[1] == 'x' || entity[1] == 'X';
            const std::string_view digits = entity.substr(hex ? 2 : 1);
            const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), code, hex ? 16 : 10);
            if (ec != std::errc{} || end != digits.data() + digits.size() || code == 0 || code > 0x10FFFF) {
                code = 0;
            }
        }
        if (code == 0) {
            out.push_back('&');
            continue;
        }
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6U)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12U)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18U)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
        }
        i = semicolon;
    }
    return out;
}

/// Text between `open_end` (just after an opening tag) and the closing tag `close`; the position
/// after the closing tag. Nothing if it is missing.
std::optional<std::pair<std::string_view, std::size_t>> element_text(std::string_view xml, std::size_t open_end,
                                                                     std::string_view close) {
    const std::size_t end = xml.find(close, open_end);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::pair{xml.substr(open_end, end - open_end), end + close.size()};
}

}  // namespace

core::Result<HistoryTexts> HistoryTexts::parse(std::string_view xml) {
    xml = core::skip_utf8_bom(xml);
    HistoryTexts texts;
    // <language languageID="16">de_de</language>
    for (std::size_t at = xml.find("<language "); at != std::string_view::npos; at = xml.find("<language ", at + 1)) {
        const std::size_t tag_end = xml.find('>', at);
        if (tag_end == std::string_view::npos) {
            break;
        }
        const auto id = attribute(xml.substr(at, tag_end - at), "languageID");
        const auto code = element_text(xml, tag_end + 1, "</language>");
        if (id && code) {
            texts.m_languages.emplace(primary_language(code->first), *id);
        }
    }
    // <line lineID="7"><translation languageID="16">Das Atoll</translation>...</line>
    for (std::size_t at = xml.find("<line "); at != std::string_view::npos;) {
        const std::size_t tag_end = xml.find('>', at);
        const std::size_t line_end = tag_end == std::string_view::npos ? tag_end : xml.find("</line>", tag_end);
        if (line_end == std::string_view::npos) {
            return make_error(ErrorCode::InvalidFormat, "texts.xml: a line without its end");
        }
        const auto line_id = attribute(xml.substr(at, tag_end - at), "lineID");
        const std::string_view line = xml.substr(tag_end + 1, line_end - tag_end - 1);
        for (std::size_t t = line.find("<translation "); line_id && t != std::string_view::npos;) {
            const std::size_t t_end = line.find('>', t);
            if (t_end == std::string_view::npos) {
                break;
            }
            const auto language = attribute(line.substr(t, t_end - t), "languageID");
            const auto content = element_text(line, t_end + 1, "</translation>");
            if (!content) {
                break;
            }
            if (language) {
                std::string value = unescape(content->first);
                std::erase(value, '\r');  // the file's line ends are CR LF
                texts.m_lines[*line_id][*language] = std::move(value);
            }
            t = line.find("<translation ", content->second);
        }
        at = xml.find("<line ", line_end);
    }
    if (texts.m_lines.empty()) {
        return make_error(ErrorCode::InvalidFormat, "texts.xml has no lines");
    }
    return texts;
}

core::Result<HistoryTexts> HistoryTexts::load(const std::filesystem::path& rda) {
    auto archive = RdaArchive::open(rda);
    if (!archive) {
        return archive.error();
    }
    auto bytes = archive->read(kTextsFile);
    if (!bytes) {
        return make_error(bytes.error().code, core::path_to_utf8(rda) + ": " + bytes.error().message);
    }
    return parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

std::optional<std::string> HistoryTexts::text(std::int32_t id, std::string_view language) const {
    const auto line = m_lines.find(id);
    if (line == m_lines.end()) {
        return std::nullopt;
    }
    for (const std::string& code : {primary_language(language), std::string("en")}) {
        const auto lang = m_languages.find(code);
        const std::int32_t lang_id = lang != m_languages.end() ? lang->second : code == "en" ? kEnglishId : -1;
        if (const auto t = line->second.find(lang_id); t != line->second.end()) {
            return t->second;
        }
    }
    return std::nullopt;
}

std::vector<std::int32_t> HistoryTexts::find_all(std::string_view name, std::string_view language) const {
    const std::string wanted = lower_ascii(name);
    std::vector<std::int32_t> out;
    const auto add = [&](std::int32_t id) {
        if (std::find(out.begin(), out.end(), id) == out.end()) {
            out.push_back(id);
        }
    };
    for (const std::string& code : {primary_language(language), std::string("en")}) {
        for (const auto& [id, translations] : m_lines) {
            if (const auto t = text(id, code); t && lower_ascii(*t) == wanted) {
                add(id);
            }
        }
    }
    // Then any language: the Königsedition's German names in an English installation.
    for (const auto& [id, translations] : m_lines) {
        for (const auto& [lang, t] : translations) {
            if (lower_ascii(t) == wanted) {
                add(id);
            }
        }
    }
    return out;
}

std::string HistoryTexts::expand(std::string_view text, std::string_view language) const {
    std::string out;
    std::size_t done = 0;
    for (std::size_t at = text.find("[["); at != std::string_view::npos; at = text.find("[[", at + 2)) {
        const std::size_t end = text.find("]]", at + 2);
        if (end == std::string_view::npos) {
            break;
        }
        std::int32_t id = 0;
        const auto [last, ec] = std::from_chars(text.data() + at + 2, text.data() + end, id);
        if (ec != std::errc{} || last != text.data() + end) {
            continue;
        }
        if (const auto t = this->text(id, language)) {
            out.append(text.substr(done, at - done));
            out.append(*t);
            done = end + 2;
            at = end;
        }
    }
    out.append(text.substr(done));
    return out;
}

}  // namespace o1602::assets
