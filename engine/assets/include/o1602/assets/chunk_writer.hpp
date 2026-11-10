// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writes the chunk container of the original's data files (docs/formats/chunks.md): a sequence of
// { char name[16]; uint32_t size; uint8_t payload[size]; }. A savegame ends with WIFF, the 32-bit
// sum of all payload words before it (0x00490730); the original never reads it.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "o1602/core/file.hpp"

namespace o1602::assets {

class ChunkWriter {
public:
    /// Appends a chunk; the name is padded with zeros to 16 bytes (at most 15 characters).
    void add(std::string_view name, std::span<const std::uint8_t> payload);
    /// Appends WIFF with the sum of the payloads so far.
    void add_checksum();
    /// Sum of the payload words so far (whole 32-bit words of each payload, little-endian).
    [[nodiscard]] std::uint32_t checksum() const noexcept { return m_sum; }
    [[nodiscard]] const core::Bytes& bytes() const noexcept { return m_data; }
    [[nodiscard]] core::Bytes take() noexcept { return std::move(m_data); }

private:
    core::Bytes m_data;
    std::uint32_t m_sum = 0;
};

/// Sum of the whole little-endian 32-bit words of a payload (a trailing part is not counted).
[[nodiscard]] std::uint32_t payload_checksum(std::span<const std::uint8_t> payload) noexcept;

}  // namespace o1602::assets
