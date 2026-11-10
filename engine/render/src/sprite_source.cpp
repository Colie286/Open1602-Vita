// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/sprite_source.hpp"

namespace o1602::render {

namespace {

bool decode_rgba(const assets::SpriteSheet& sheet, const assets::Palette& palette, std::uint32_t i,
                 std::vector<std::uint8_t>& rgba, std::uint32_t& width, std::uint32_t& height) {
    auto image = sheet.decode(i);
    if (!image) {
        return false;
    }
    width = image->width;
    height = image->height;
    rgba.assign(std::size_t{width} * height * 4, 0);
    for (std::size_t p = 0; p < std::size_t{width} * height; ++p) {
        if (image->opaque[p] == 0) {
            continue;
        }
        const assets::Rgba& c = palette[image->indices[p]];
        rgba[p * 4] = c.r;
        rgba[p * 4 + 1] = c.g;
        rgba[p * 4 + 2] = c.b;
        rgba[p * 4 + 3] = 255;
    }
    return true;
}

}  // namespace

std::uint32_t SheetSource::count() const {
    return static_cast<std::uint32_t>(m_sheet.size());
}

void SheetSource::size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const {
    const auto& info = m_sheet.info(i);
    width = info.width;
    height = info.height;
}

bool SheetSource::pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const {
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    return decode_rgba(m_sheet, m_palette, i, rgba, w, h);
}

std::uint32_t HalfSizeSource::count() const {
    return static_cast<std::uint32_t>(m_sheet.size());
}

void HalfSizeSource::size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const {
    const auto& info = m_sheet.info(i);
    width = (info.width + 1) / 2;
    height = (info.height + 1) / 2;
}

bool HalfSizeSource::pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const {
    std::vector<std::uint8_t> full;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    if (!decode_rgba(m_sheet, m_palette, i, full, w, h)) {
        return false;
    }
    std::uint32_t hw = 0;
    std::uint32_t hh = 0;
    halve_rgba(full, w, h, rgba, hw, hh);
    return true;
}

void halve_rgba(const std::vector<std::uint8_t>& src, std::uint32_t width, std::uint32_t height,
                std::vector<std::uint8_t>& dst, std::uint32_t& out_width, std::uint32_t& out_height) {
    out_width = (width + 1) / 2;
    out_height = (height + 1) / 2;
    dst.assign(std::size_t{out_width} * out_height * 4, 0);
    for (std::uint32_t y = 0; y < out_height; ++y) {
        for (std::uint32_t x = 0; x < out_width; ++x) {
            std::uint32_t sum[3] = {0, 0, 0};
            std::uint32_t opaque = 0;
            for (std::uint32_t dy = 0; dy < 2; ++dy) {
                for (std::uint32_t dx = 0; dx < 2; ++dx) {
                    const std::uint32_t sx = 2 * x + dx;
                    const std::uint32_t sy = 2 * y + dy;
                    if (sx >= width || sy >= height) {
                        continue;
                    }
                    const std::size_t p = (std::size_t{sy} * width + sx) * 4;
                    if (src[p + 3] == 0) {
                        continue;
                    }
                    sum[0] += src[p];
                    sum[1] += src[p + 1];
                    sum[2] += src[p + 2];
                    ++opaque;
                }
            }
            if (opaque < 2) {
                continue;
            }
            const std::size_t q = (std::size_t{y} * out_width + x) * 4;
            dst[q] = static_cast<std::uint8_t>(sum[0] / opaque);
            dst[q + 1] = static_cast<std::uint8_t>(sum[1] / opaque);
            dst[q + 2] = static_cast<std::uint8_t>(sum[2] / opaque);
            dst[q + 3] = 255;
        }
    }
}

}  // namespace o1602::render
