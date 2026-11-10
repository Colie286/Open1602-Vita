// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace o1602::core {

using ByteSpan = std::span<const std::uint8_t>;

/// Bounds-checked little-endian reader over a byte span.
/// All original data files are little-endian (x86). Every read is checked so that
/// corrupt or truncated files fail gracefully instead of reading out of bounds.
class ByteReader {
public:
    explicit ByteReader(ByteSpan data) noexcept : m_data(data) {}

    [[nodiscard]] std::size_t offset() const noexcept { return m_offset; }
    [[nodiscard]] std::size_t size() const noexcept { return m_data.size(); }
    [[nodiscard]] std::size_t remaining() const noexcept { return m_data.size() - m_offset; }
    [[nodiscard]] bool has(std::size_t n) const noexcept { return remaining() >= n; }
    [[nodiscard]] bool at_end() const noexcept { return m_offset >= m_data.size(); }

    bool seek(std::size_t offset) noexcept {
        if (offset > m_data.size()) {
            return false;
        }
        m_offset = offset;
        return true;
    }

    bool skip(std::size_t n) noexcept {
        if (!has(n)) {
            return false;
        }
        m_offset += n;
        return true;
    }

    [[nodiscard]] std::optional<std::uint8_t> u8() noexcept {
        if (!has(1)) {
            return std::nullopt;
        }
        return m_data[m_offset++];
    }

    [[nodiscard]] std::optional<std::uint16_t> u16() noexcept {
        if (!has(2)) {
            return std::nullopt;
        }
        const auto v = static_cast<std::uint16_t>(m_data[m_offset] | (m_data[m_offset + 1] << 8));
        m_offset += 2;
        return v;
    }

    [[nodiscard]] std::optional<std::uint32_t> u32() noexcept {
        if (!has(4)) {
            return std::nullopt;
        }
        const auto v = load_u32(m_data, m_offset);
        m_offset += 4;
        return v;
    }

    [[nodiscard]] std::optional<std::uint64_t> u64() noexcept {
        if (!has(8)) {
            return std::nullopt;
        }
        const std::uint64_t lo = load_u32(m_data, m_offset);
        const std::uint64_t hi = load_u32(m_data, m_offset + 4);
        m_offset += 8;
        return lo | (hi << 32);
    }

    /// Returns a sub-span of n bytes and advances.
    [[nodiscard]] std::optional<ByteSpan> bytes(std::size_t n) noexcept {
        if (!has(n)) {
            return std::nullopt;
        }
        auto s = m_data.subspan(m_offset, n);
        m_offset += n;
        return s;
    }

    /// Unchecked helper; caller guarantees offset + 4 <= data.size().
    [[nodiscard]] static std::uint32_t load_u32(ByteSpan data, std::size_t offset) noexcept {
        return static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    }

private:
    ByteSpan m_data;
    std::size_t m_offset = 0;
};

}  // namespace o1602::core
