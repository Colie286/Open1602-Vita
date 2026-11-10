// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/flag_tint.hpp"

#include <algorithm>
#include <cmath>

namespace o1602::render {

assets::Rgba tint_shade(std::uint8_t shade, assets::Rgba colour) noexcept {
    const auto channel = [&](std::uint8_t c) -> std::uint8_t {
        if (shade <= kTintNeutral) {
            return static_cast<std::uint8_t>(c * shade / kTintNeutral);
        }
        return static_cast<std::uint8_t>(c + (255 - c) * (shade - kTintNeutral) / (255 - kTintNeutral));
    };
    return assets::Rgba{channel(colour.r), channel(colour.g), channel(colour.b), 255};
}

void tint_rgba(std::vector<std::uint8_t>& rgba, assets::Rgba colour) {
    for (std::size_t p = 0; p + 3 < rgba.size(); p += 4) {
        if (rgba[p + 3] == 0 || rgba[p] != rgba[p + 1] || rgba[p] != rgba[p + 2]) {
            continue;
        }
        const assets::Rgba c = tint_shade(rgba[p], colour);
        rgba[p] = c.r;
        rgba[p + 1] = c.g;
        rgba[p + 2] = c.b;
    }
}

void original_flag_to_template(std::vector<std::uint8_t>& rgba) {
    for (std::size_t p = 0; p + 3 < rgba.size(); p += 4) {
        if (rgba[p + 3] == 0) {
            continue;
        }
        const double luminance = 0.299 * rgba[p] + 0.587 * rgba[p + 1] + 0.114 * rgba[p + 2];
        const auto shade = static_cast<std::uint8_t>(std::clamp(std::lround(0.48 * luminance + 5.0), 0L, 255L));
        rgba[p] = rgba[p + 1] = rgba[p + 2] = shade;
    }
}

std::uint32_t TintedFlagSource::count() const {
    return static_cast<std::uint32_t>(m_colours.size()) * m_frames;
}

void TintedFlagSource::size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const {
    m_base.size(m_first + i % m_frames, width, height);
}

bool TintedFlagSource::pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const {
    if (!m_base.pixels(m_first + i % m_frames, rgba)) {
        return false;
    }
    if (m_from_original) {
        original_flag_to_template(rgba);
    }
    tint_rgba(rgba, m_colours[i / m_frames]);
    return true;
}

}  // namespace o1602::render
