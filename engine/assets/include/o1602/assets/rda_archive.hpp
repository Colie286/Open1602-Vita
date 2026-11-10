// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The RDA archives of the later Anno games ("Resource File V2.2"), as the History Edition keeps
// its own data in ahe/a1he0.rda (docs/formats/rda.md). Only what Open1602 needs: the directory
// and the stored (neither compressed nor encrypted) files.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::assets {

class RdaArchive {
public:
    struct Entry {
        std::string name;          ///< UTF-8, '/' between folders, e.g. "data/texts.xml"
        std::uint64_t offset = 0;  ///< of the data in the archive
        std::uint64_t stored = 0;  ///< bytes in the archive
        std::uint64_t size = 0;    ///< bytes of the file
        bool readable = false;     ///< stored as it is (its block neither compressed nor encrypted)
    };

    /// Reads the directory of the archive at `path` (the files stay on disk until read()).
    static core::Result<RdaArchive> open(const std::filesystem::path& path);
    /// The same from the archive's bytes (tests).
    static core::Result<RdaArchive> from_bytes(std::vector<std::uint8_t> bytes);

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return m_entries; }
    /// The entry named `name` (case-insensitive, '/' or '\\'), nullptr if there is none.
    [[nodiscard]] const Entry* find(std::string_view name) const;
    /// The bytes of the file `name`.
    [[nodiscard]] core::Result<std::vector<std::uint8_t>> read(std::string_view name) const;

private:
    std::filesystem::path m_path;  ///< empty: the archive is m_bytes
    std::vector<std::uint8_t> m_bytes;
    std::vector<Entry> m_entries;
};

}  // namespace o1602::assets
