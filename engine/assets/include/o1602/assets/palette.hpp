// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "o1602/assets/chunk_file.hpp"
#include "o1602/core/result.hpp"

namespace o1602::assets {

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    friend bool operator==(const Rgba&, const Rgba&) = default;
};

/// 256 colour palette from a .COL file (e.g. TOOLGFX/STADTFLD.COL). See docs/formats/col.md.
class Palette {
public:
    static constexpr std::size_t kSize = 256;

    static core::Result<Palette> from_chunk_file(const ChunkFile& file);
    static core::Result<Palette> load(const std::filesystem::path& path);

    [[nodiscard]] const Rgba& operator[](std::uint8_t index) const noexcept { return m_colours[index]; }
    [[nodiscard]] const std::array<Rgba, kSize>& colours() const noexcept { return m_colours; }

private:
    std::array<Rgba, kSize> m_colours{};
};

}  // namespace o1602::assets
