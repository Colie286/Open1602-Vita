// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/chunk_writer.hpp"

#include <algorithm>

namespace o1602::assets {

std::uint32_t payload_checksum(std::span<const std::uint8_t> payload) noexcept {
    std::uint32_t sum = 0;
    for (std::size_t o = 0; o + 4 <= payload.size(); o += 4) {
        sum += static_cast<std::uint32_t>(payload[o]) | (static_cast<std::uint32_t>(payload[o + 1]) << 8) |
               (static_cast<std::uint32_t>(payload[o + 2]) << 16) | (static_cast<std::uint32_t>(payload[o + 3]) << 24);
    }
    return sum;
}

void ChunkWriter::add(std::string_view name, std::span<const std::uint8_t> payload) {
    constexpr std::size_t kNameSize = 16;
    const std::size_t at = m_data.size();
    m_data.resize(at + kNameSize + 4 + payload.size(), 0);
    std::copy_n(name.begin(), std::min(name.size(), kNameSize - 1), m_data.begin() + static_cast<std::ptrdiff_t>(at));
    const auto size = static_cast<std::uint32_t>(payload.size());
    for (std::size_t k = 0; k < 4; ++k) {
        m_data[at + kNameSize + k] = static_cast<std::uint8_t>(size >> (8 * k));
    }
    std::copy(payload.begin(), payload.end(), m_data.begin() + static_cast<std::ptrdiff_t>(at + kNameSize + 4));
    m_sum += payload_checksum(payload);
}

void ChunkWriter::add_checksum() {
    const std::uint32_t sum = m_sum;
    const std::uint8_t data[4] = {static_cast<std::uint8_t>(sum), static_cast<std::uint8_t>(sum >> 8),
                                  static_cast<std::uint8_t>(sum >> 16), static_cast<std::uint8_t>(sum >> 24)};
    add("WIFF", data);
    m_sum = sum;  // WIFF itself is not part of the sum
}

}  // namespace o1602::assets
