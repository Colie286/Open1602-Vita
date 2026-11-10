// SPDX-License-Identifier: GPL-3.0-or-later
//
// The bit reader and the variable-length codes of the Bink decoders (bink.cpp, bink2_video.cpp,
// bink_audio.cpp). Bink stores its bits from the lowest bit of each byte up, as Smacker does; the
// readers behave like the ones the decoders were written against (FFmpeg's little-endian
// GetBitContext): reading past the end gives zeros, and the caller checks overrun().
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <bit>
#include <span>

namespace o1602::video::bink {

class Bits {
public:
    Bits() = default;
    explicit Bits(std::span<const std::uint8_t> data) noexcept : m_data(data), m_size(data.size() * 8) {}

    /// The next `n` bits (0..32) without taking them.
    [[nodiscard]] std::uint32_t peek(int n) const noexcept {
        if (n == 0) {
            return 0;
        }
        const std::size_t byte = m_pos >> 3;
        // Most codec reads need 1..24 bits. One bounded unaligned word load
        // replaces five byte loads, variable 64-bit shifts and a loop per read
        // on ARM32. Never read padding outside the original packet.
        if (n <= 24 && byte < m_data.size() && m_data.size() - byte >= 4) {
            std::uint32_t word;
            std::memcpy(&word, m_data.data() + byte, sizeof(word));
            if constexpr (std::endian::native == std::endian::big) {
                word = ((word & 0xffU) << 24) | ((word & 0xff00U) << 8) |
                       ((word >> 8) & 0xff00U) | (word >> 24);
            }
            return (word >> (m_pos & 7U)) & ((std::uint32_t{1} << n) - 1);
        }
        std::uint64_t window = 0;
        for (std::size_t i = 0; i < 5 && byte + i < m_data.size(); ++i) {
            window |= static_cast<std::uint64_t>(m_data[byte + i]) << (8 * i);
        }
        window >>= (m_pos & 7U);
        return static_cast<std::uint32_t>(window & ((std::uint64_t{1} << n) - 1));
    }
    std::uint32_t bits(int n) noexcept {
        const std::uint32_t v = peek(n);
        skip(n);
        return v;
    }
    std::uint32_t bit() noexcept {
        const auto value = m_pos < m_size ? (m_data[m_pos >> 3] >> (m_pos & 7U)) & 1U : 0U;
        if (m_pos < m_size + 64) ++m_pos;
        return value;
    }
    /// Bits until one equals `stop` or `limit` were read; their number (FFmpeg's get_unary).
    int unary(std::uint32_t stop, int limit) noexcept {
        int i = 0;
        while (i < limit && bit() != stop) {
            ++i;
        }
        return i;
    }
    void skip(std::int64_t n) noexcept {
        if (n >= 0) {
            m_pos += static_cast<std::size_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(n), m_size + 64 - m_pos));
            return;
        }
        m_pos = static_cast<std::size_t>(
            std::clamp<std::int64_t>(static_cast<std::int64_t>(m_pos) + n, 0, static_cast<std::int64_t>(m_size) + 64));
    }
    void seek(std::size_t bit_position) noexcept { m_pos = std::min(bit_position, m_size + 64); }
    void align() noexcept { m_pos = (m_pos + 7) & ~std::size_t{7}; }
    void align32() noexcept { m_pos = (m_pos + 31) & ~std::size_t{31}; }
    [[nodiscard]] std::size_t position() const noexcept { return m_pos; }
    [[nodiscard]] std::int64_t left() const noexcept {
        return static_cast<std::int64_t>(m_size) - static_cast<std::int64_t>(m_pos);
    }
    [[nodiscard]] bool overrun() const noexcept { return m_pos > m_size; }

private:
    std::span<const std::uint8_t> m_data;
    std::size_t m_size = 0;  ///< in bits
    std::size_t m_pos = 0;
};

/// A code table of up to 9 bits: codes as read from the lowest bit up (the first bit read is
/// bit 0 of the code), looked up through the next 9 bits at once.
class Vlc {
public:
    static constexpr int kBits = 9;

    /// Symbol s has the code `codes[s]` of `lengths[s]` bits (1..9).
    template <typename Code, std::size_t N>
    constexpr Vlc(const Code (&codes)[N], const std::uint8_t (&lengths)[N]) noexcept {
        for (std::size_t s = 0; s < N; ++s) {
            const int len = lengths[s];
            for (std::uint32_t rest = 0; rest < (1U << (kBits - len)); ++rest) {
                const std::uint32_t index = static_cast<std::uint32_t>(codes[s]) | (rest << len);
                m_table[index] = Entry{static_cast<std::int16_t>(s), static_cast<std::int8_t>(len)};
            }
        }
    }

    /// The symbol of the next code, -1 for a code the table does not have.
    int read(Bits& in) const noexcept {
        const Entry e = m_table[in.peek(kBits)];
        if (e.length == 0) {
            return -1;
        }
        in.skip(e.length);
        return e.symbol;
    }

private:
    struct Entry {
        std::int16_t symbol = -1;
        std::int8_t length = 0;
    };
    std::array<Entry, std::size_t{1} << kBits> m_table{};
};

}  // namespace o1602::video::bink
