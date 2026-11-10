// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <string_view>
#include "o1602/core/result.hpp"
namespace o1602::assets {
// Explicit, read-only inventory. Reports and reference manifests live outside the originals.
// FNV-1a is a copy-integrity checksum, not a cryptographic authenticity check.
core::Status audit_installation(const std::filesystem::path& root, std::string_view language,
                               const std::filesystem::path& output,
                               const std::filesystem::path& reference = {}, bool hashes = false);
}
