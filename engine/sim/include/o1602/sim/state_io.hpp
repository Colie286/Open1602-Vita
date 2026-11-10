// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading and writing simulation state in Open1602's own savegame chunk
// (savegame_extension.hpp): little-endian, fixed widths, a reader that remembers the first error.
#pragma once

#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/pool.hpp"

namespace o1602::sim {

/// Reader that yields 0 after the data ran out and remembers that it did.
class StateIn {
public:
    explicit StateIn(core::ByteReader& r) noexcept : m_r(r) {}

    [[nodiscard]] bool ok() const noexcept { return m_ok; }
    [[nodiscard]] std::size_t remaining() const noexcept { return m_r.remaining(); }
    void fail() noexcept { m_ok = false; }

    std::uint8_t u8() { return take(m_r.u8()); }
    std::uint16_t u16() { return take(m_r.u16()); }
    std::uint32_t u32() { return take(m_r.u32()); }
    std::uint64_t u64() { return take(m_r.u64()); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    bool boolean() { return u8() != 0; }
    double real() { return std::bit_cast<double>(u64()); }
    /// A position that older versions saved as floating point, in fixed point units of 1/`unit`.
    /// NaN, infinities and values beyond the range fail the read instead of converting with
    /// undefined behaviour.
    std::int32_t real_fixed(std::int32_t unit) {
        const double v = real() * unit;
        constexpr double kLimit = 2147483520.0;  // below 2^31, exact in double
        if (!(v > -kLimit && v < kLimit)) {      // also false for NaN
            m_ok = false;
            return 0;
        }
        return static_cast<std::int32_t>(v);
    }
    std::string string() {
        const std::uint32_t n = u32();
        const auto b = m_r.bytes(n);
        if (!b) {
            m_ok = false;
            return {};
        }
        return std::string(b->begin(), b->end());
    }
    /// A count of records that each take at least `min_size` bytes: 0 if the data cannot hold them.
    std::uint32_t count(std::size_t min_size = 1) {
        const std::uint32_t n = u32();
        if (min_size != 0 && n > m_r.remaining() / min_size) {
            m_ok = false;
            return 0;
        }
        return n;
    }
    /// Reads a list of count(min_size) records with `read`, one call per record. The list grows
    /// with the records actually read, not with the count up front: a count from damaged data
    /// costs no more memory than the records the data really holds.
    template <class T, class Read>
    void list(std::vector<T>& v, std::size_t min_size, Read read) {
        const std::uint32_t n = count(min_size);
        v.clear();
        for (std::uint32_t i = 0; i < n && m_ok; ++i) {
            read(v.emplace_back());
        }
    }
    TilePos tile() {
        const std::int32_t x = i32();
        return TilePos{x, i32()};
    }
    std::optional<TilePos> maybe_tile() {
        if (!boolean()) {
            return std::nullopt;
        }
        return tile();
    }
    Handle handle() {
        const std::uint32_t index = u32();
        return Handle{index, u32()};
    }

private:
    template <class T>
    T take(std::optional<T> v) {
        if (!v) {
            m_ok = false;
            return T{};
        }
        return *v;
    }
    core::ByteReader& m_r;
    bool m_ok = true;
};

inline void put_real(core::ByteWriter& out, double v) {
    out.u64(std::bit_cast<std::uint64_t>(v));
}
inline void put_tile(core::ByteWriter& out, TilePos t) {
    out.i32(t.x);
    out.i32(t.y);
}
inline void put_maybe_tile(core::ByteWriter& out, const std::optional<TilePos>& t) {
    out.boolean(t.has_value());
    if (t) {
        put_tile(out, *t);
    }
}
inline void put_handle(core::ByteWriter& out, Handle h) {
    out.u32(h.index);
    out.u32(h.generation);
}

}  // namespace o1602::sim
