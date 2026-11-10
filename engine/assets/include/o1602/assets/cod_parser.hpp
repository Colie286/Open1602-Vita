// SPDX-License-Identifier: GPL-3.0-or-later
//
// Interpreter for the .cod description language (haeuser.cod, figuren.cod).
// Behaviour follows the original interpreter in 1602.exe - see docs/formats/cod.md for the
// specification and the addresses it was derived from.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "o1602/assets/cod_predefined.hpp"

namespace o1602::assets::cod {

/// Every symbol holds up to 8 values ("Size: 2, 2" uses two of them).
inline constexpr std::size_t kMaxValues = 8;

enum class ValueType : std::uint8_t {
    Number = 1,
    String = 2,
};

/// One value slot. Numbers are kept both as integer and as floating point value because the
/// original evaluates both representations side by side (e.g. "1.3" -> integer 1, real 1.3).
/// The engine converts reals to fixed point when building simulation data (ADR 0004).
struct Value {
    ValueType type = ValueType::Number;
    std::int32_t integer = 0;
    double real = 0.0;
    std::string text;

    friend bool operator==(const Value&, const Value&) = default;
};

struct Symbol {
    std::uint32_t count = 0;  ///< number of values set by the last assignment
    std::array<Value, kMaxValues> values{};
    bool defined = false;  ///< false if only created by being referenced
};

/// Flat, case-sensitive symbol namespace (the original uses one table with 650 entries).
class SymbolTable {
public:
    [[nodiscard]] const Symbol* find(std::string_view name) const;
    Symbol& get_or_create(std::string_view name);
    /// Equivalent of Cod_DefineSymbol(name, value, slot) @ 0x004020D0.
    void define(std::string_view name, std::int32_t value, std::size_t slot = 0);
    [[nodiscard]] std::size_t size() const noexcept { return m_symbols.size(); }
    [[nodiscard]] const std::unordered_map<std::string, Symbol>& all() const noexcept { return m_symbols; }

private:
    std::unordered_map<std::string, Symbol> m_symbols;
};

/// Values of one property assignment, e.g. "Wegspeed: 145, 120, 170, 100".
struct Field {
    std::vector<Value> values;
    bool assigned_with_equals = false;  ///< '=' instead of ':' (usually constants like IDBODEN = 20101)
    std::uint32_t line = 0;             ///< line of the last assignment

    [[nodiscard]] std::int32_t integer(std::size_t index = 0, std::int32_t fallback = 0) const {
        return index < values.size() ? values[index].integer : fallback;
    }
};

/// A record: the root of a file, or one numbered entry of an "Objekt:" block (e.g. HAUS #12).
struct Object {
    /// Fields in order of first assignment; reassignment replaces the value in place.
    std::vector<std::pair<std::string, Field>> fields;
    /// Every assignment in file order, including repeated ones. Some statements are lists
    /// rather than properties (e.g. one "Soldat: TYPE, a, b" line per unit type in figuren.cod).
    std::vector<std::pair<std::string, std::vector<Value>>> history;
    /// Nested records: type name ("HAUS", "HAUS_PRODTYP", ...) -> Nummer -> record.
    std::map<std::string, std::map<std::int32_t, Object>> children;

    [[nodiscard]] const Field* field(std::string_view name) const;
    Field& set_field(std::string_view name);
    [[nodiscard]] std::int32_t integer(std::string_view name, std::size_t index = 0, std::int32_t fallback = 0) const;
    [[nodiscard]] const std::map<std::int32_t, Object>* records(std::string_view type) const;
};

struct Diagnostic {
    std::uint32_t line = 0;
    std::string message;
};

struct Document {
    Object root;
    SymbolTable symbols;
    /// Highest Nummer set in the blocks of each type (records ObjFill made beyond it do not count).
    std::map<std::string, std::int32_t> highest_nummer;
    std::vector<Diagnostic> diagnostics;
};

/// Parses decoded .cod text (see decode_cod()).
///
/// `predefined` are the constants the original game registers before parsing; use
/// cod_predefined_haeuser() / cod_predefined_figuren() for the original files.
/// Never fails: problems the original would silently accept (or crash on) are reported as
/// diagnostics and handled conservatively.
Document parse(std::string_view text, std::span<const CodPredefinedSymbol> predefined = {});

struct ParseOptions {
    /// Every field keeps at least this many value slots of its symbol, also those an assignment did
    /// not write. The GAD loader (0x405480) copies whole slots: "Color: 5" keeps the second value of
    /// the last "Color:" line.
    std::size_t min_values = 0;
};
Document parse(std::string_view text, std::span<const CodPredefinedSymbol> predefined, const ParseOptions& options);

}  // namespace o1602::assets::cod
