// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading zip archives, as a package of mods can be one (docs/modding/packages.md): the files
// stored or packed with deflate, without encryption and without zip64.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::core {

/// A file of a zip archive.
struct ZipEntry {
    std::string path;  ///< as the archive names it, '/' between folders
    std::vector<std::uint8_t> bytes;
};

/// What an archive may hold at most, against archives made to cost memory.
struct ZipLimits {
    std::size_t max_entries = 16384;
    std::uint64_t max_total = std::uint64_t{1} << 30;  ///< every file unpacked together
};

/// The files of a zip archive, in the order of its central directory; folders (names ending in
/// '/') are left out. Fails on a damaged archive, a file whose checksum does not match, an
/// encrypted file, a method other than stored and deflate (deflate needs zlib), zip64, and an
/// archive beyond `limits`. The paths are not checked: what writes them checks them.
[[nodiscard]] Result<std::vector<ZipEntry>> read_zip(std::span<const std::uint8_t> archive,
                                                     const ZipLimits& limits = {});

}  // namespace o1602::core
