// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/data_file.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_set>

#include "o1602/core/text.hpp"

namespace o1602::core {

namespace {

bool is_bare(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// One line of the file, read from left to right.
class LineReader {
public:
    LineReader(std::string_view text, std::size_t line) : m_text(text), m_line(line) {}

    void skip_space() {
        while (m_pos < m_text.size() && (m_text[m_pos] == ' ' || m_text[m_pos] == '\t')) {
            ++m_pos;
        }
    }
    [[nodiscard]] bool done() const noexcept { return m_pos >= m_text.size(); }
    [[nodiscard]] char peek() const noexcept { return done() ? '\0' : m_text[m_pos]; }
    bool take(char c) {
        if (peek() != c) {
            return false;
        }
        ++m_pos;
        return true;
    }
    /// Nothing but blanks and a comment left.
    [[nodiscard]] bool at_end() {
        skip_space();
        return done() || peek() == '#';
    }

    Error error(std::string_view what) const {
        return make_error(ErrorCode::InvalidFormat, "line " + std::to_string(m_line) + ": " + std::string(what));
    }

    /// A key: bare or quoted parts separated by dots, joined with '.'.
    Result<std::string> key() {
        std::string out;
        for (;;) {
            skip_space();
            if (peek() == '"' || peek() == '\'') {
                auto part = string();
                if (!part) {
                    return part.error();
                }
                out += *part;
            } else {
                const std::size_t start = m_pos;
                while (!done() && is_bare(peek())) {
                    ++m_pos;
                }
                if (m_pos == start) {
                    return error("a key is missing");
                }
                out += m_text.substr(start, m_pos - start);
            }
            skip_space();
            if (!take('.')) {
                return out;
            }
            out.push_back('.');
        }
    }

    /// A basic ("...") or literal ('...') string on this line.
    Result<std::string> string() {
        const char quote = peek();
        ++m_pos;
        std::string out;
        while (!done()) {
            const char c = m_text[m_pos++];
            if (c == quote) {
                return out;
            }
            if (c != '\\' || quote == '\'') {
                out.push_back(c);
                continue;
            }
            if (done()) {
                break;
            }
            const char e = m_text[m_pos++];
            switch (e) {
                case 'n':
                    out.push_back('\n');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case '"':
                case '\\':
                    out.push_back(e);
                    break;
                case 'u':
                case 'U': {
                    const std::size_t digits = e == 'u' ? 4 : 8;
                    if (m_pos + digits > m_text.size()) {
                        return error("an incomplete \\u escape");
                    }
                    std::uint32_t cp = 0;
                    for (std::size_t i = 0; i < digits; ++i) {
                        const char h = m_text[m_pos + i];
                        const int v = h >= '0' && h <= '9'   ? h - '0'
                                      : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                      : h >= 'A' && h <= 'F' ? h - 'A' + 10
                                                             : -1;
                        if (v < 0) {
                            return error("an incomplete \\u escape");
                        }
                        cp = cp * 16 + static_cast<std::uint32_t>(v);
                    }
                    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                        return error("a \\u escape that is no character");
                    }
                    m_pos += digits;
                    append_utf8(out, cp);
                    break;
                }
                default:
                    return error(std::string("an unknown escape \\") + e);
            }
        }
        return error("a string is not closed on its line (multi-line strings are not supported)");
    }

    /// A string, an integer or a boolean.
    Result<DataValue> value() {
        skip_space();
        DataValue v;
        v.line = m_line;
        if (peek() == '"' || peek() == '\'') {
            if (m_text.substr(m_pos).starts_with("\"\"\"") || m_text.substr(m_pos).starts_with("'''")) {
                return error("multi-line strings are not supported");
            }
            auto s = string();
            if (!s) {
                return s.error();
            }
            v.value = std::move(s).value();
            return v;
        }
        const std::size_t start = m_pos;
        while (!done() && peek() != ' ' && peek() != '\t' && peek() != '#') {
            ++m_pos;
        }
        const std::string_view word = m_text.substr(start, m_pos - start);
        if (word == "true" || word == "false") {
            v.value = word == "true";
            return v;
        }
        if (auto n = integer(word)) {
            v.value = *n;
            return v;
        }
        if (word.empty()) {
            return error("a value is missing");
        }
        return error("'" + std::string(word) + "' is no string, integer or boolean (strings need quotes)");
    }

private:
    static std::optional<std::int64_t> integer(std::string_view word) {
        bool negative = false;
        if (!word.empty() && (word.front() == '+' || word.front() == '-')) {
            negative = word.front() == '-';
            word.remove_prefix(1);
        }
        int base = 10;
        if (word.starts_with("0x")) {
            base = 16;
            word.remove_prefix(2);
        }
        if (word.empty() || word.front() == '_' || word.back() == '_') {
            return std::nullopt;
        }
        std::uint64_t n = 0;
        for (const char c : word) {
            if (c == '_') {
                continue;
            }
            const int d = c >= '0' && c <= '9'                 ? c - '0'
                          : base == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10
                          : base == 16 && c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                               : -1;
            if (d < 0 || n > (std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(d)) /
                                 static_cast<std::uint64_t>(base)) {
                return std::nullopt;
            }
            n = n * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(d);
        }
        constexpr auto kMax = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (n > kMax + (negative ? 1 : 0)) {
            return std::nullopt;
        }
        return negative ? static_cast<std::int64_t>(0 - n) : static_cast<std::int64_t>(n);
    }

    std::string_view m_text;
    std::size_t m_line;
    std::size_t m_pos = 0;
};

}  // namespace

const DataValue* DataTable::find(std::string_view key) const noexcept {
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const DataEntry& e) { return e.key == key; });
    return it == entries.end() ? nullptr : &it->value;
}

Result<std::vector<DataTable>> parse_data_file(std::string_view utf8) {
    utf8 = skip_utf8_bom(utf8);
    if (!is_utf8(utf8)) {
        return make_error(ErrorCode::InvalidFormat, "the file is not UTF-8");
    }
    std::vector<DataTable> tables(1);
    // Table names and the current table's keys seen so far, for the duplicate checks (HI-6: a
    // linear search per table and key made a large file quadratic).
    std::unordered_set<std::string> table_names;
    std::unordered_set<std::string> keys;
    std::size_t line = 0;
    while (!utf8.empty()) {
        ++line;
        const std::size_t nl = utf8.find('\n');
        std::string_view text = utf8.substr(0, nl);
        utf8 = nl == std::string_view::npos ? std::string_view{} : utf8.substr(nl + 1);
        if (text.ends_with('\r')) {
            text.remove_suffix(1);
        }
        LineReader r(text, line);
        if (r.at_end()) {
            continue;
        }
        if (r.take('[')) {
            if (r.peek() == '[') {
                return r.error("arrays of tables are not supported");
            }
            auto name = r.key();
            if (!name) {
                return name.error();
            }
            r.skip_space();
            if (!r.take(']') || !r.at_end()) {
                return r.error("a table header must end with ']'");
            }
            if (!table_names.insert(*name).second) {
                return r.error("the table [" + *name + "] is given twice");
            }
            tables.push_back(DataTable{std::move(name).value(), line, {}});
            keys.clear();
            continue;
        }
        auto key = r.key();
        if (!key) {
            return key.error();
        }
        if (!r.take('=')) {
            return r.error("'=' is missing after '" + *key + "'");
        }
        r.skip_space();
        if (r.peek() == '[' || r.peek() == '{') {
            return r.error("arrays and inline tables are not supported");
        }
        auto value = r.value();
        if (!value) {
            return value.error();
        }
        if (!r.at_end()) {
            return r.error("something follows the value of '" + *key + "'");
        }
        DataTable& table = tables.back();
        if (!keys.insert(*key).second) {
            return r.error("'" + *key + "' is given twice");
        }
        table.entries.push_back(DataEntry{std::move(key).value(), std::move(value).value()});
    }
    if (tables.front().entries.empty()) {
        tables.erase(tables.begin());
    }
    return tables;
}

std::string data_string(std::string_view utf8) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "\"";
    for (const char c : utf8) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (u < 0x20 || u == 0x7F) {
            out += "\\u00";
            out.push_back(kHex[u >> 4]);
            out.push_back(kHex[u & 0x0F]);
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

Result<std::string> set_data_table(std::string_view utf8, std::string_view table, const std::vector<DataLine>& lines,
                                   const std::vector<std::string>& removed) {
    auto tables = parse_data_file(utf8);
    if (!tables) {
        return tables.error();
    }
    // The file's lines as they are, each with its own ending; the new ones end as the file's do.
    const std::string_view ending = utf8.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    std::vector<std::string_view> text;
    for (std::size_t at = 0; at < utf8.size();) {
        const std::size_t end = utf8.find('\n', at);
        const std::size_t next = end == std::string_view::npos ? utf8.size() : end + 1;
        text.push_back(utf8.substr(at, next - at));
        at = next;
    }
    std::string written;
    for (const DataLine& l : lines) {
        written += l.key + " = " + l.value + std::string(ending);
    }
    const auto it = std::find_if(tables->begin(), tables->end(),
                                 [&](const DataTable& t) { return t.line != 0 && t.name == table; });
    std::string out;
    if (it == tables->end()) {
        out.assign(utf8);
        if (!out.empty() && out.back() != '\n') {
            out += ending;
        }
        if (!out.empty()) {
            out += ending;
        }
        const bool bare = !table.empty() && std::all_of(table.begin(), table.end(), is_bare);
        out += "[" + (bare ? std::string(table) : data_string(table)) + "]" + std::string(ending) + written;
    } else {
        std::unordered_set<std::size_t> dropped;
        std::size_t insert = 0;  // 1-based: before this line
        std::size_t last = it->line;
        for (const DataEntry& e : it->entries) {
            last = std::max(last, e.value.line);
            const bool replaced =
                std::any_of(lines.begin(), lines.end(), [&](const DataLine& l) { return l.key == e.key; }) ||
                std::find(removed.begin(), removed.end(), e.key) != removed.end();
            if (replaced) {
                dropped.insert(e.value.line);
                insert = insert == 0 ? e.value.line : std::min(insert, e.value.line);
            }
        }
        if (insert == 0) {
            insert = last + 1;
        }
        for (std::size_t n = 1; n <= text.size() + 1; ++n) {
            if (n == insert) {
                if (!out.empty() && out.back() != '\n') {
                    out += ending;  // the last line of the file had no ending
                }
                out += written;
            }
            if (n <= text.size() && dropped.count(n) == 0) {
                out += text[n - 1];
            }
        }
    }
    if (auto check = parse_data_file(out); !check) {
        return check.error();
    }
    return out;
}

}  // namespace o1602::core
