// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reimplementation of the .cod interpreter. Specification: docs/formats/cod.md.
// Original functions (1602.exe): Cod_ParseLine 0x00402260 (tokenizer + evaluation),
// Cod_DefineSymbol 0x004020D0, Cod_LoadHaeuser 0x00460750 / Cod_LoadFiguren 0x00441210
// (Objekt / EndObj / Nummer / ObjFill handling).
#include "o1602/assets/cod_parser.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace o1602::assets::cod {

// ---------------------------------------------------------------------------------------------
// SymbolTable / Object

const Symbol* SymbolTable::find(std::string_view name) const {
    const auto it = m_symbols.find(std::string(name));
    return it == m_symbols.end() ? nullptr : &it->second;
}

Symbol& SymbolTable::get_or_create(std::string_view name) {
    return m_symbols.try_emplace(std::string(name)).first->second;
}

void SymbolTable::define(std::string_view name, std::int32_t value, std::size_t slot) {
    Symbol& symbol = get_or_create(name);
    if (slot < kMaxValues) {
        Value& v = symbol.values[slot];
        v.type = ValueType::Number;
        v.integer = value;
        v.real = static_cast<double>(value);
    }
    symbol.defined = true;
}

const Field* Object::field(std::string_view name) const {
    const auto it = std::find_if(fields.begin(), fields.end(), [&](const auto& f) { return f.first == name; });
    return it == fields.end() ? nullptr : &it->second;
}

Field& Object::set_field(std::string_view name) {
    const auto it = std::find_if(fields.begin(), fields.end(), [&](const auto& f) { return f.first == name; });
    if (it != fields.end()) {
        return it->second;
    }
    return fields.emplace_back(std::string(name), Field{}).second;
}

std::int32_t Object::integer(std::string_view name, std::size_t index, std::int32_t fallback) const {
    const Field* f = field(name);
    return f == nullptr ? fallback : f->integer(index, fallback);
}

const std::map<std::int32_t, Object>* Object::records(std::string_view type) const {
    const auto it = children.find(std::string(type));
    return it == children.end() ? nullptr : &it->second;
}

namespace {

// ---------------------------------------------------------------------------------------------
// Character classes of the MSVC "C" locale as used by the original (_pctype masks).

constexpr int kEnd = -1;

/// Digits as the original's strtol() reads them, where long has 32 bits: a value beyond the range
/// stays at 0x7FFFFFFF. Not strtol() itself, whose long has 64 bits on Linux and macOS.
std::int32_t parse_digits(const std::string& digits, int base) {
    constexpr std::int64_t kMax = std::numeric_limits<std::int32_t>::max();
    std::int64_t v = 0;
    for (const char d : digits) {
        const int digit = d <= '9' ? d - '0' : (d | 0x20) - 'a' + 10;
        v = std::min(v * base + digit, kMax + 1);
    }
    return static_cast<std::int32_t>(std::min(v, kMax));
}

/// `digits.digits` without strtod(), which reads the decimal point of the locale. Up to 15
/// significant digits this is one exact division and so rounds like strtod() (Clinger's fast
/// path); digits beyond are dropped, the same on every platform.
double parse_real(const std::string& digits) {
    constexpr std::uint64_t kExact = std::uint64_t{1} << 53;
    std::uint64_t mantissa = 0;
    int scale = 0;
    bool fraction = false;
    double big = 0.0;  // integer parts beyond 2^53: no COD file has them, but they must not wrap
    for (const char d : digits) {
        if (d == '.') {
            fraction = true;
            continue;
        }
        if (mantissa < kExact / 10 && scale < 22) {
            mantissa = mantissa * 10 + static_cast<std::uint64_t>(d - '0');
            scale += fraction ? 1 : 0;
        } else if (!fraction) {
            big = (big == 0.0 ? static_cast<double>(mantissa) : big) * 10.0;
        }
    }
    if (big != 0.0) {
        return big;
    }
    double power = 1.0;
    for (int i = 0; i < scale; ++i) {
        power *= 10.0;  // exact up to 10^22
    }
    return static_cast<double>(mantissa) / power;
}

bool is_alpha(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
bool is_digit(int c) {
    return c >= '0' && c <= '9';
}
bool is_alnum(int c) {
    return is_alpha(c) || is_digit(c);
}
bool is_xdigit(int c) {
    return is_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
bool is_punct(int c) {
    return c > 0x20 && c < 0x7F && !is_alnum(c);
}
/// _pctype mask 0x117 (upper | lower | digit | punct | alpha): first "meaningful" character.
bool is_meaningful(int c) {
    return is_alnum(c) || is_punct(c);
}

enum class Op : std::uint8_t { None = 0, Add = 1, Sub = 2, Assign = 3, Mul = 4, Div = 5, Or = 6, And = 7 };

enum class TokenKind : std::uint8_t { Number, String, Comma, Assign };

struct Token {
    TokenKind kind = TokenKind::Number;
    Op op = Op::None;
    Value value;
    std::string identifier;  ///< set if the value came from a symbol reference
};

struct Statement {
    bool has_lhs = false;
    std::string lhs;
    std::size_t lhs_slot = 0;
    std::uint32_t count = 0;
    bool relative = false;  ///< '@' prefix: do not reset the target before evaluation
    bool used_equals = false;
    std::uint32_t line = 0;
    std::vector<Token> tokens;
};

class Parser {
public:
    Parser(std::string_view text, Document& doc) : m_text(text), m_doc(doc) {}

    bool at_end() const { return m_end; }

    /// Cod_ParseLine: reads one logical line and returns its statement.
    Statement read_statement() {
        Statement st;
        st.line = m_line;
        Op op = Op::None;
        int c = get();
        while (c != '\n') {
            if (c == kEnd || c == 0) {
                m_end = true;
                break;
            }
            switch (c) {
                case '"': {
                    Token t{TokenKind::String, op, {}, {}};
                    t.value.type = ValueType::String;
                    c = get();
                    while (c != '"' && c != kEnd && c != 0) {
                        t.value.text.push_back(static_cast<char>(c));
                        c = get();
                    }
                    if (c != '"') {
                        diag(st.line, "unterminated string literal");
                    }
                    st.tokens.push_back(std::move(t));
                    c = get();
                    break;
                }
                case '$':
                case '%': {
                    const bool hex = c == '$';
                    std::string digits;
                    c = get();
                    while (hex ? is_xdigit(c) : (c == '0' || c == '1')) {
                        digits.push_back(static_cast<char>(c));
                        c = get();
                    }
                    const std::int32_t v = parse_digits(digits, hex ? 16 : 2);
                    st.tokens.push_back(number_token(op, v, static_cast<double>(v)));
                    break;
                }
                case '&':
                    op = Op::And;
                    c = skip();
                    break;
                case '*':
                    op = Op::Mul;
                    c = skip();
                    break;
                case '+':
                    op = Op::Add;
                    c = skip();
                    break;
                case '-':
                    op = Op::Sub;
                    c = skip();
                    break;
                case '/':
                    op = Op::Div;
                    c = skip();
                    break;
                case '|':
                    op = Op::Or;
                    c = skip();
                    break;
                case '@':
                    st.relative = true;
                    c = skip();
                    break;
                case ',':
                    op = Op::Assign;
                    c = skip();
                    ++st.count;
                    st.tokens.push_back(Token{TokenKind::Comma, Op::None, {}, {}});
                    break;
                case ':':
                case '=':
                    st.used_equals = st.used_equals || c == '=';
                    op = Op::Assign;
                    c = skip();
                    st.count = 1;
                    st.tokens.push_back(Token{TokenKind::Assign, Op::None, {}, {}});
                    break;
                case ';':  // comment until end of line
                    do {
                        c = get();
                    } while (c != '\n' && c != kEnd && c != 0);
                    break;
                default:
                    if (is_alpha(c)) {
                        c = read_identifier(c, st, op);
                    } else if (is_digit(c)) {
                        c = read_number(c, st, op);
                    } else {
                        c = get();  // whitespace and any other character are ignored
                    }
                    break;
            }
        }
        return st;
    }

    /// Evaluation part of Cod_ParseLine: applies the tokens to the target symbol.
    void evaluate(const Statement& st) {
        Symbol& sym = m_doc.symbols.get_or_create(st.lhs);
        sym.count = st.count;
        sym.defined = true;
        std::size_t cur = st.lhs_slot;
        const auto reset = [&](std::size_t i) {
            if (i < kMaxValues) {
                sym.values[i] = Value{};
            }
        };
        if (!st.relative) {
            reset(cur);
        }
        for (const Token& t : st.tokens) {
            if (t.kind == TokenKind::Assign) {
                cur = st.lhs_slot;
                continue;
            }
            if (t.kind == TokenKind::Comma) {
                ++cur;
                if (!st.relative) {
                    reset(cur);
                }
                continue;
            }
            if (cur >= kMaxValues) {
                diag(st.line, "more than " + std::to_string(kMaxValues) + " values for '" + st.lhs + "' (ignored)");
                break;
            }
            Value& v = sym.values[cur];
            if (t.kind == TokenKind::String) {
                if (t.op == Op::None || t.op == Op::Assign) {
                    v.text = t.value.text;
                } else if (t.op == Op::Add) {
                    v.text += t.value.text;
                }
                v.type = ValueType::String;
            } else {
                apply_number(v, t, st);
            }
        }
    }

    void diag(std::uint32_t line, std::string message) {
        // A hostile file with a new undefined name on every line made one message each (HI-3).
        constexpr std::size_t kMaxDiagnostics = 1000;
        if (m_doc.diagnostics.size() < kMaxDiagnostics) {
            m_doc.diagnostics.push_back(Diagnostic{line, std::move(message)});
        } else if (m_doc.diagnostics.size() == kMaxDiagnostics) {
            m_doc.diagnostics.push_back(Diagnostic{line, "more problems follow, they are not listed"});
        }
    }

private:
    int get() {
        if (m_pos >= m_text.size()) {
            return kEnd;
        }
        const auto c = static_cast<unsigned char>(m_text[m_pos++]);
        if (c == '\n') {
            ++m_line;
        }
        return c;
    }

    /// The next character without taking it; kEnd at the end.
    [[nodiscard]] int peek() const { return m_pos < m_text.size() ? static_cast<unsigned char>(m_text[m_pos]) : kEnd; }

    /// After an operator the original skips everything up to the next "meaningful" character,
    /// including line breaks (an operator at the end of a line continues the statement).
    int skip() {
        int c = 0;
        do {
            c = get();
        } while (!is_meaningful(c) && c != kEnd);
        return c;
    }

    static Token number_token(Op op, std::int32_t integer, double real) {
        Token t{TokenKind::Number, op, {}, {}};
        t.value.integer = integer;
        t.value.real = real;
        return t;
    }

    int read_identifier(int c, Statement& st, Op op) {
        std::string name;
        while (is_alnum(c) || c == '_') {
            name.push_back(static_cast<char>(c));
            c = get();
        }
        std::size_t slot = 0;
        if (c == '[') {
            c = get();
            while (is_digit(c)) {
                slot = slot * 10 + static_cast<std::size_t>(c - '0');
                c = get();
            }
            c = get();  // the original consumes the character after the digits (']') unchecked
        }

        if (st.count == 0) {  // first identifier of the line is the assignment target
            st.has_lhs = true;
            st.lhs = std::move(name);
            st.lhs_slot = std::min(slot, kMaxValues - 1);
            st.count = 1;
            m_doc.symbols.get_or_create(st.lhs);
            return c;
        }

        Symbol& sym = m_doc.symbols.get_or_create(name);
        // D-001..D-004: the original silently defines unknown names as 0; we do the same but report them.
        if (!sym.defined && m_reported_undefined.insert(name).second) {
            diag(st.line, "undefined symbol '" + name + "' (treated as 0)");
        }
        Token t{TokenKind::Number, op, {}, name};
        if (slot < kMaxValues) {
            t.value = sym.values[slot];
            t.kind = t.value.type == ValueType::String ? TokenKind::String : TokenKind::Number;
        }
        st.tokens.push_back(std::move(t));
        return c;
    }

    int read_number(int c, Statement& st, Op op) {
        std::string digits;
        while (is_digit(c)) {
            digits.push_back(static_cast<char>(c));
            c = get();
        }
        if (c == '.') {
            digits.push_back('.');
            c = get();
            while (is_digit(c)) {
                digits.push_back(static_cast<char>(c));
                c = get();
            }
            const double real = parse_real(digits);
            // ftol(): truncation toward zero
            const double clamped = std::clamp(real, static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                                              static_cast<double>(std::numeric_limits<std::int32_t>::max()));
            st.tokens.push_back(number_token(op, static_cast<std::int32_t>(clamped), real));
        } else {
            const std::int32_t v = parse_digits(digits, 10);
            st.tokens.push_back(number_token(op, v, static_cast<double>(v)));
            // The History Edition marks palette colours with a c ("COL_RED = 249c" in TOOLS.INC);
            // its game reads the number. A c that begins a name ("2cm") is left to the identifier.
            if ((c == 'c' || c == 'C') && !is_alpha(peek()) && !is_digit(peek()) && peek() != '_') {
                c = get();
            }
        }
        return c;
    }

    void apply_number(Value& v, const Token& t, const Statement& st) {
        // 32-bit wrap-around like the original x86 code, without signed overflow UB.
        const auto wrap = [](std::int64_t x) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(x)); };
        const std::int32_t a = v.integer;
        const std::int32_t b = t.value.integer;
        switch (t.op) {
            case Op::None:
            case Op::Assign:
                v.integer = b;
                v.real = t.value.real;
                break;
            case Op::Add:
                v.integer = wrap(std::int64_t{a} + b);
                v.real += t.value.real;
                break;
            case Op::Sub:
                v.integer = wrap(std::int64_t{a} - b);
                v.real -= t.value.real;
                break;
            case Op::Mul:
                v.integer = wrap(std::int64_t{a} * b);
                v.real *= t.value.real;
                break;
            case Op::Div:
                if (b == 0) {
                    diag(st.line, "division by zero in '" + st.lhs + "' (original would crash; value kept)");
                } else {
                    v.integer = (a == std::numeric_limits<std::int32_t>::min() && b == -1) ? a : a / b;
                    v.real /= t.value.real;
                }
                break;
            case Op::Or:
                v.integer = a | b;
                v.real = static_cast<double>(v.integer);
                break;
            case Op::And:
                v.integer = a & b;
                v.real = static_cast<double>(v.integer);
                break;
        }
        v.type = ValueType::Number;
    }

    std::string_view m_text;
    Document& m_doc;
    std::size_t m_pos = 0;
    std::uint32_t m_line = 1;
    bool m_end = false;
    std::set<std::string> m_reported_undefined;
};

// ---------------------------------------------------------------------------------------------
// Object structure (Objekt / EndObj / Nummer / ObjFill), as in Cod_LoadHaeuser / Cod_LoadFiguren.

constexpr std::int32_t kMaxObjFill = 10000;  // original: clamped to the table size (e.g. MAXHAUS = 500)
// Records all ObjFill statements of one file may make, nested records counted: a copy holds the
// children of its source, so nested fills multiply (HI-2). 400 times the original's 500 houses.
constexpr std::int64_t kMaxFilledRecords = 200000;

/// The record and its nested records, counted up to just over `limit`.
std::int64_t record_count(const Object& o, std::int64_t limit) {
    std::int64_t n = 1;
    for (const auto& [type, records] : o.children) {
        for (const auto& [nummer, child] : records) {
            if (n > limit) {
                return n;
            }
            n += record_count(child, limit - n);
        }
    }
    return n;
}

struct Level {
    std::string type;  ///< "" for the root level
    std::int32_t nummer = 0;
    Object* parent = nullptr;  ///< record that holds this level's records (nullptr for root)
};

class Builder {
public:
    Builder(Document& doc, Parser& parser, std::size_t min_values)
        : m_doc(doc), m_parser(parser), m_min_values(std::min(min_values, kMaxValues)) {
        m_levels.push_back(Level{});
    }

    void handle(const Statement& st) {
        const Symbol& sym = *m_doc.symbols.find(st.lhs);
        if (st.lhs == "Objekt") {
            std::string type;
            for (const Token& t : st.tokens) {
                if (!t.identifier.empty()) {
                    type = t.identifier;
                    break;
                }
            }
            if (type.empty()) {
                type = std::to_string(sym.values[0].integer);
            }
            Object* parent = &current_record();
            m_levels.push_back(Level{std::move(type), 0, parent});
            m_doc.symbols.define("Nummer", 0);
        } else if (st.lhs == "EndObj") {
            if (m_levels.size() <= 1) {
                m_parser.diag(st.line, "EndObj without matching Objekt");
                return;
            }
            m_levels.pop_back();
            m_doc.symbols.define("Nummer", m_levels.back().nummer);
        } else if (st.lhs == "Nummer") {
            m_levels.back().nummer = sym.values[0].integer;
            if (!m_levels.back().type.empty()) {
                auto [it, fresh] = m_doc.highest_nummer.try_emplace(m_levels.back().type, m_levels.back().nummer);
                if (!fresh) {
                    it->second = std::max(it->second, m_levels.back().nummer);
                }
            }
        } else if (st.lhs == "ObjFill") {
            obj_fill(st, sym);
        } else {
            Field& field = current_record().set_field(st.lhs);
            const std::size_t n = std::max(
                m_min_values, std::min<std::size_t>(kMaxValues, st.lhs_slot + std::max<std::uint32_t>(st.count, 1)));
            field.values.assign(sym.values.begin(), sym.values.begin() + static_cast<std::ptrdiff_t>(n));
            field.assigned_with_equals = st.used_equals;
            field.line = st.line;
            current_record().history.emplace_back(st.lhs, field.values);
        }
    }

    void finish(std::uint32_t line) {
        if (m_levels.size() > 1) {
            m_parser.diag(line, std::to_string(m_levels.size() - 1) + " Objekt block(s) not closed at end of file");
        }
    }

private:
    Object& current_record() {
        Level& level = m_levels.back();
        if (level.parent == nullptr) {
            return m_doc.root;
        }
        return level.parent->children[level.type][level.nummer];
    }

    void obj_fill(const Statement& st, const Symbol& sym) {
        Level& level = m_levels.back();
        if (level.parent == nullptr) {
            m_parser.diag(st.line, "ObjFill outside of an Objekt block (ignored)");
            return;
        }
        auto& records = level.parent->children[level.type];
        const std::int32_t source_index = sym.values[0].integer;
        std::int32_t count = st.count < 2 ? 1 : sym.values[1].integer;
        if (count < 0 || count > kMaxObjFill) {
            m_parser.diag(st.line, "ObjFill count " + std::to_string(count) + " out of range (clamped)");
            count = std::clamp(count, 0, kMaxObjFill);
        }
        if (count == 0) {
            return;
        }
        const auto it = records.find(source_index);
        // Fails as a whole rather than filling part of the range, so every machine gets the same table.
        const std::int64_t left = kMaxFilledRecords - m_filled;
        const std::int64_t size = it == records.end() ? 1 : record_count(it->second, left);
        if (m_fill_refused || size * count > left) {
            if (!m_fill_refused) {
                m_parser.diag(st.line, "ObjFill would make more than " + std::to_string(kMaxFilledRecords) +
                                           " records in this file (this and later ObjFill ignored)");
            }
            m_fill_refused = true;
            return;
        }
        m_filled += size * count;
        const Object source = it == records.end() ? Object{} : it->second;
        for (std::int32_t i = 0; i < count; ++i) {
            const std::int64_t nummer = std::int64_t{level.nummer} + i;
            if (nummer > std::numeric_limits<std::int32_t>::max()) {
                break;
            }
            records[static_cast<std::int32_t>(nummer)] = source;
        }
    }

    Document& m_doc;
    Parser& m_parser;
    std::size_t m_min_values;
    std::vector<Level> m_levels;
    std::int64_t m_filled = 0;  ///< records ObjFill made so far
    bool m_fill_refused = false;
};

}  // namespace

Document parse(std::string_view text, std::span<const CodPredefinedSymbol> predefined) {
    return parse(text, predefined, ParseOptions{});
}

Document parse(std::string_view text, std::span<const CodPredefinedSymbol> predefined, const ParseOptions& options) {
    Document doc;
    for (const auto& p : predefined) {
        doc.symbols.define(p.name, p.value);
    }
    Parser parser(text, doc);
    Builder builder(doc, parser, options.min_values);
    std::uint32_t last_line = 1;
    while (!parser.at_end()) {
        const Statement st = parser.read_statement();
        last_line = st.line;
        if (!st.has_lhs) {
            continue;
        }
        parser.evaluate(st);
        builder.handle(st);
    }
    builder.finish(last_line);
    return doc;
}

}  // namespace o1602::assets::cod
