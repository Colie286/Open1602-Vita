// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/cod.hpp"

#include <string_view>

#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"

namespace o1602::assets {

std::string decode_cod(std::span<const std::uint8_t> encoded) {
    std::string out(encoded.size(), '\0');
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        out[i] = static_cast<char>(static_cast<std::uint8_t>(-encoded[i]));
    }
    return out;
}

std::vector<std::uint8_t> encode_cod(std::string_view plain) {
    std::vector<std::uint8_t> out(plain.size());
    for (std::size_t i = 0; i < plain.size(); ++i) {
        out[i] = static_cast<std::uint8_t>(-static_cast<std::uint8_t>(plain[i]));
    }
    return out;
}

core::Result<std::string> load_cod_utf8(const std::filesystem::path& path) {
    auto bytes = core::read_file(path);
    if (!bytes) {
        return bytes.error();
    }
    return core::cp1252_to_utf8(decode_cod(*bytes));
}

}  // namespace o1602::assets
