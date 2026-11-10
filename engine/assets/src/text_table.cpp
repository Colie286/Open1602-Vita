// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/text_table.hpp"

#include <algorithm>
#include <string>

#include "o1602/assets/cod.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/core/file.hpp"

namespace o1602::assets {

namespace {

/// A line without its line break and trailing blanks.
std::string_view trimmed(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.remove_suffix(1);
    }
    return line;
}

/// Whether a line is a placeholder "[[N]]" of the History Edition alone.
bool placeholder_line(std::string_view line) {
    line = trimmed(line);
    if (line.size() < 5 || !line.starts_with("[[") || !line.ends_with("]]")) {
        return false;
    }
    const std::string_view digits = line.substr(2, line.size() - 4);
    return std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; });
}

/// The History Edition's Text.cod without the lines it inserted into LEISTE, the help texts of its
/// own buttons ("[[676]]" after "Auftrag ansehen", "[[807]]" after "Einheit auflösen"). They would
/// shift the original's line numbers, by which Open1602 finds the help texts.
std::string without_inserted_help(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool leiste = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        end = end == std::string_view::npos ? text.size() : end + 1;
        const std::string_view line = text.substr(pos, end - pos);
        pos = end;
        if (leiste && placeholder_line(line)) {
            continue;
        }
        const std::string_view bare = trimmed(line);
        if (bare.size() >= 2 && bare.front() == '[' && bare.back() == ']' && !placeholder_line(bare)) {
            leiste = bare == "[LEISTE]";
        }
        out += line;
    }
    return out;
}

}  // namespace

TextTable TextTable::parse(std::string_view text) {
    TextTable table;
    std::vector<std::string>* current = nullptr;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1);
        }
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
            const std::string_view name = line.substr(1, line.size() - 2);
            current = name == "END" ? nullptr : &table.m_sections[std::string(name)];
            continue;
        }
        if (current != nullptr) {
            current->emplace_back(line);
        }
        if (end == text.size()) {
            break;
        }
    }
    return table;
}

core::Result<TextTable> TextTable::load(const GameDirectory& game) {
    const auto path = game.resolve("Text.cod");
    if (!path) {
        return core::make_error(core::ErrorCode::NotFound, "Text.cod not found");
    }
    // Kept in Windows-1252 like the names in savegames (City::name).
    auto bytes = core::read_file(*path);
    if (!bytes) {
        return bytes.error();
    }
    // The History Edition's Text.cod has placeholders [[N]] for its own texts (a line "[[676]]"
    // would else start a section).
    std::string text = decode_cod(*bytes);
    if (game.history_texts() != nullptr) {
        text = without_inserted_help(text);
    }
    return parse(game.expand(text));
}

const std::vector<std::string>& TextTable::section(std::string_view name) const {
    static const std::vector<std::string> kEmpty;
    const auto it = m_sections.find(name);
    return it == m_sections.end() ? kEmpty : it->second;
}

}  // namespace o1602::assets
