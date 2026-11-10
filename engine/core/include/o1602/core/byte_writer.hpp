// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace o1602::core {

/// Little-endian writer into a growing byte buffer; the counterpart of ByteReader.
class ByteWriter {
public:
    ByteWriter() = default;
    /// Writes into `buffer` from its start, keeping its memory: a buffer taken back with take()
    /// after the last time is written again without allocating.
    explicit ByteWriter(std::vector<std::uint8_t> buffer) noexcept : m_data(std::move(buffer)) { m_data.clear(); }

    void u8(std::uint8_t v) { m_data.push_back(v); }
    void u16(std::uint16_t v) { put(v, 2); }
    void u32(std::uint32_t v) { put(v, 4); }
    void u64(std::uint64_t v) { put(v, 8); }
    void i32(std::int32_t v) { put(static_cast<std::uint32_t>(v), 4); }
    void i64(std::int64_t v) { put(static_cast<std::uint64_t>(v), 8); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void bytes(std::span<const std::uint8_t> data) { m_data.insert(m_data.end(), data.begin(), data.end()); }
    /// Numbers of 2 or 4 bytes each, one after the other, without a length.
    void u16s(std::span<const std::uint16_t> v) { put_all(v); }
    void u32s(std::span<const std::uint32_t> v) { put_all(v); }
    /// A string with its length (u32) in front.
    void string(std::string_view s) {
        u32(static_cast<std::uint32_t>(s.size()));
        m_data.insert(m_data.end(), s.begin(), s.end());
    }
    /// Overwrites a u32 written earlier (sizes known only afterwards).
    void patch_u32(std::size_t offset, std::uint32_t v) {
        for (std::size_t k = 0; k < 4; ++k) {
            m_data[offset + k] = static_cast<std::uint8_t>(v >> (8 * k));
        }
    }

    /// Room for `n` bytes in all, so that writing them allocates nothing more.
    void reserve(std::size_t n) { m_data.reserve(n); }

    [[nodiscard]] std::size_t size() const noexcept { return m_data.size(); }
    [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept { return m_data; }
    [[nodiscard]] std::vector<std::uint8_t> take() noexcept { return std::move(m_data); }

private:
    void put(std::uint64_t v, std::size_t n) {
        // One growth check for the whole number instead of one per byte: a savegame is millions of
        // small numbers (Simulation::state_hash() writes one every few seconds).
        const std::size_t at = m_data.size();
        m_data.resize(at + n);
        std::uint8_t* out = m_data.data() + at;
        for (std::size_t k = 0; k < n; ++k) {
            out[k] = static_cast<std::uint8_t>(v >> (8 * k));
        }
    }
    template <class T>
    void put_all(std::span<const T> v) {
        const std::size_t at = m_data.size();
        m_data.resize(at + v.size() * sizeof(T));
        std::uint8_t* out = m_data.data() + at;
        for (const T x : v) {
            for (std::size_t k = 0; k < sizeof(T); ++k) {
                *out++ = static_cast<std::uint8_t>(x >> (8 * k));
            }
        }
    }
    std::vector<std::uint8_t> m_data;
};

}  // namespace o1602::core
