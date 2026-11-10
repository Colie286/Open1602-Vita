// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::assets {

/// .cod files (haeuser.cod, figuren.cod, Text.cod, Editor.cod) are plain text scripts in
/// Windows-1252 where every byte is stored negated (two's complement): stored = -plain.
/// See docs/formats/cod.md. Verified against 1602.exe @ 0x0048F94D (Text.cod loader).

/// Decodes raw .cod bytes to the original Windows-1252 text.
std::string decode_cod(std::span<const std::uint8_t> encoded);

/// Encodes Windows-1252 text into the .cod representation (inverse of decode_cod).
std::vector<std::uint8_t> encode_cod(std::string_view plain);

/// Loads a .cod file and returns its content converted to UTF-8.
core::Result<std::string> load_cod_utf8(const std::filesystem::path& path);

}  // namespace o1602::assets
