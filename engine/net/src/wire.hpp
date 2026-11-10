// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading messages: a reader that remembers the first error, so that a decoder can read on and
// check once at the end.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "o1602/core/byte_reader.hpp"
#include "o1602/net/protocol.hpp"
#include "o1602/sim/command_io.hpp"

namespace o1602::net::detail {

/// Reader that remembers the first error, so that a decoder can read on and check once.
class In {
public:
    explicit In(std::span<const std::uint8_t> bytes) noexcept : m_reader(bytes) {}

    [[nodiscard]] bool ok() const noexcept { return m_ok; }
    [[nodiscard]] bool at_end() const noexcept { return m_reader.at_end(); }
    void fail() noexcept { m_ok = false; }
    core::ByteReader& reader() noexcept { return m_reader; }

    std::uint8_t u8() { return take(m_reader.u8()); }
    std::uint16_t u16() { return take(m_reader.u16()); }
    std::uint32_t u32() { return take(m_reader.u32()); }
    std::uint64_t u64() { return take(m_reader.u64()); }
    bool boolean() {
        const std::uint8_t b = u8();
        if (b > 1) {
            fail();
        }
        return b != 0;
    }
    /// A count of at most `limit` things, each at least one byte: no more than the bytes left, so
    /// that a short message cannot make the reader reserve room for the limit (MS-26).
    std::uint32_t count(std::uint32_t limit) {
        const std::uint32_t n = u32();
        if (n > limit || n > m_reader.remaining()) {
            fail();
            return 0;
        }
        return n;
    }
    std::string text(std::size_t limit) {
        const std::uint32_t n = count(static_cast<std::uint32_t>(limit));
        const auto bytes = m_ok ? m_reader.bytes(n) : std::nullopt;
        if (!bytes) {
            fail();
            return {};
        }
        return {bytes->begin(), bytes->end()};
    }
    std::vector<sim::Command> commands() {
        const std::uint32_t n = count(kMaxCommandsPerMessage);
        std::vector<sim::Command> out;
        out.reserve(n);
        for (std::uint32_t i = 0; i < n && m_ok; ++i) {
            auto command = sim::read_command(m_reader);
            if (!command) {
                fail();
                break;
            }
            out.push_back(std::move(*command));
        }
        return out;
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

    core::ByteReader m_reader;
    bool m_ok = true;
};

}  // namespace o1602::net::detail
