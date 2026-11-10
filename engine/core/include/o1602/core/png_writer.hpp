// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::core {

/// Encodes an 8-bit RGBA image as PNG. Uses zlib when available, otherwise stored
/// (uncompressed) deflate blocks, so the core library has no hard dependency.
std::vector<std::uint8_t> encode_png_rgba(std::uint32_t width, std::uint32_t height,
                                          std::span<const std::uint8_t> rgba);

Status write_png_rgba(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
                      std::span<const std::uint8_t> rgba);

}  // namespace o1602::core
