// SPDX-License-Identifier: GPL-3.0-or-later
//
// One description per record for the sections of Open1602's savegame (sections.hpp): the same
// `io_...(a, record)` writes the record with a Writer and reads it with a Reader, so the two sides
// cannot drift apart. The method names give the width in the file (`a.u16(x)` puts x in two bytes,
// whatever the type of x). What only one side does (checks of the data read, values rebuilt on
// reading) stands in `if constexpr (A::kReading)` where it happens. Internal to sim.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "o1602/core/byte_writer.hpp"
#include "o1602/sim/state_io.hpp"

namespace o1602::sim::save {

/// Integers and enumerations: what a.u8() and its siblings take.
template <class T>
concept Field = std::is_integral_v<T> || std::is_enum_v<T>;

/// Writes the records described to the bytes of a section.
class Writer {
public:
    static constexpr bool kReading = false;

    explicit Writer(core::ByteWriter& out) noexcept : m_out(out) {}

    template <Field T>
    void u8(T v) {
        m_out.u8(static_cast<std::uint8_t>(v));
    }
    template <Field T>
    void u16(T v) {
        m_out.u16(static_cast<std::uint16_t>(v));
    }
    template <Field T>
    void u32(T v) {
        m_out.u32(static_cast<std::uint32_t>(v));
    }
    template <Field T>
    void u64(T v) {
        m_out.u64(static_cast<std::uint64_t>(v));
    }
    void i32(std::int32_t v) { m_out.i32(v); }
    void i64(std::int64_t v) { m_out.i64(v); }
    void boolean(bool v) { m_out.boolean(v); }
    void string(const std::string& v) { m_out.string(v); }
    /// A byte the reader takes only the bits of `mask` of (the writer writes the value as it is).
    template <Field T>
    void u8_masked(T v, std::uint8_t /*mask*/) {
        u8(v);
    }
    /// An enumeration in a byte the reader refuses above `last`.
    template <Field T>
    void enum8(T v, T /*last*/) {
        u8(v);
    }
    /// A list: its length (u32), then every element with `fn`. `min_size` is for the reader.
    template <class List, class Fn>
    void list(const List& v, std::size_t /*min_size*/, Fn&& fn) {
        m_out.u32(static_cast<std::uint32_t>(v.size()));
        for (const auto& e : v) {
            fn(e);
        }
    }
    /// A list of numbers that each take 4 bytes, or 1 byte.
    template <class List>
    void u32s(const List& v) {
        list(v, 4, [&](const auto& e) { u32(e); });
    }
    template <class List>
    void u8s(const List& v) {
        list(v, 1, [&](const auto& e) { u8(e); });
    }
    /// A list of flags (std::vector<bool>), one byte each.
    void bools(const std::vector<bool>& v) {
        m_out.u32(static_cast<std::uint32_t>(v.size()));
        for (const bool b : v) {
            m_out.boolean(b);
        }
    }

    [[nodiscard]] static constexpr bool ok() noexcept { return true; }
    [[nodiscard]] core::ByteWriter& out() noexcept { return m_out; }

private:
    core::ByteWriter& m_out;
};

/// Reads the records described from the bytes of a section; after the data ran out or a check
/// failed it reads zeros and ok() is false (StateIn).
class Reader {
public:
    static constexpr bool kReading = true;

    explicit Reader(StateIn& in) noexcept : m_in(in) {}

    template <Field T>
    void u8(T& v) {
        v = static_cast<T>(m_in.u8());
    }
    template <Field T>
    void u16(T& v) {
        v = static_cast<T>(m_in.u16());
    }
    template <Field T>
    void u32(T& v) {
        v = static_cast<T>(m_in.u32());
    }
    template <Field T>
    void u64(T& v) {
        v = static_cast<T>(m_in.u64());
    }
    void i32(std::int32_t& v) { v = m_in.i32(); }
    void i64(std::int64_t& v) { v = m_in.i64(); }
    void boolean(bool& v) { v = m_in.boolean(); }
    void string(std::string& v) { v = m_in.string(); }
    template <Field T>
    void u8_masked(T& v, std::uint8_t mask) {
        v = static_cast<T>(m_in.u8() & mask);
    }
    template <Field T>
    void enum8(T& v, T last) {
        const std::uint8_t b = m_in.u8();
        if (b > static_cast<std::uint8_t>(last)) {
            m_in.fail();
        }
        v = static_cast<T>(b);
    }
    /// A list of count(min_size) elements, each read with `fn` (StateIn::list).
    template <class List, class Fn>
    void list(List& v, std::size_t min_size, Fn&& fn) {
        m_in.list(v, min_size, fn);
    }
    template <class List>
    void u32s(List& v) {
        list(v, 4, [&](auto& e) { u32(e); });
    }
    template <class List>
    void u8s(List& v) {
        list(v, 1, [&](auto& e) { u8(e); });
    }
    void bools(std::vector<bool>& v) {
        v.resize(m_in.count(1));
        for (std::size_t i = 0; i < v.size(); ++i) {
            v[i] = m_in.boolean();
        }
    }

    [[nodiscard]] bool ok() const noexcept { return m_in.ok(); }
    void fail() noexcept { m_in.fail(); }
    [[nodiscard]] StateIn& in() noexcept { return m_in; }

private:
    StateIn& m_in;
};

}  // namespace o1602::sim::save
