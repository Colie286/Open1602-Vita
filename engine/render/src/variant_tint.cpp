// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/variant_tint.hpp"

#include <algorithm>
#include <cmath>

#include "o1602/render/flag_tint.hpp"

namespace o1602::render {

namespace {

double luminance(const std::uint8_t* p) noexcept {
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
}

}  // namespace

void tint_variant(std::span<const RgbaView> variants, std::size_t base, std::size_t pattern, assets::Rgba colour,
                  std::vector<std::uint8_t>& out) {
    out.clear();
    if (base >= variants.size() || variants[base].pixels == nullptr) {
        return;
    }
    const RgbaView& b = variants[base];
    const std::size_t w = b.width;
    const std::size_t h = b.height;
    out.assign(b.pixels, b.pixels + w * h * 4);
    std::vector<const std::uint8_t*> others;
    for (std::size_t v = 0; v < variants.size(); ++v) {
        if (v != base && variants[v].pixels != nullptr && variants[v].width == b.width &&
            variants[v].height == b.height) {
            others.push_back(variants[v].pixels);
        }
    }
    if (others.empty()) {
        return;
    }
    // Candidates first, then only those with enough candidate neighbours.
    std::vector<std::uint8_t> candidate(w * h, 0);
    for (std::size_t i = 0; i < w * h; ++i) {
        const std::uint8_t* p = b.pixels + i * 4;
        if (p[3] == 0 || std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]}) > kVariantTintGreyRange ||
            luminance(p) < kVariantTintMinLuminance) {
            continue;
        }
        candidate[i] = std::all_of(others.begin(), others.end(), [&](const std::uint8_t* o) {
            const std::uint8_t* q = o + i * 4;
            return q[3] != 0 && (q[0] != p[0] || q[1] != p[1] || q[2] != p[2]);
        });
    }
    std::vector<std::size_t> cloth;
    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t i = y * w + x;
            if (candidate[i] == 0) {
                continue;
            }
            int neighbours = 0;
            for (std::size_t ny = y > 0 ? y - 1 : 0; ny <= std::min(y + 1, h - 1); ++ny) {
                for (std::size_t nx = x > 0 ? x - 1 : 0; nx <= std::min(x + 1, w - 1); ++nx) {
                    neighbours += (ny != y || nx != x) ? candidate[ny * w + nx] : 0;
                }
            }
            if (neighbours >= kVariantTintNeighbours) {
                cloth.push_back(i);
            }
        }
    }
    // The shading: the pattern's luminance where it has the pixel, scaled to the base's brightness.
    const std::uint8_t* p = pattern < variants.size() && pattern != base && variants[pattern].pixels != nullptr &&
                                    variants[pattern].width == b.width && variants[pattern].height == b.height
                                ? variants[pattern].pixels
                                : b.pixels;
    const auto shading = [&](std::size_t i) {
        return p[i * 4 + 3] != 0 ? luminance(p + i * 4) : luminance(b.pixels + i * 4);
    };
    double base_sum = 0.0;
    double pattern_sum = 0.0;
    for (const std::size_t i : cloth) {
        base_sum += luminance(b.pixels + i * 4);
        pattern_sum += shading(i);
    }
    const double scale = pattern_sum > 0.0 ? base_sum / pattern_sum : 1.0;
    for (const std::size_t i : cloth) {
        const auto shade =
            static_cast<std::uint8_t>(std::clamp(std::lround(0.48 * shading(i) * scale + 5.0), 0L, 255L));
        const assets::Rgba c = tint_shade(shade, colour);
        out[i * 4] = c.r;
        out[i * 4 + 1] = c.g;
        out[i * 4 + 2] = c.b;
    }
}

}  // namespace o1602::render
