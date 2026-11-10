// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/package_sprites.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "o1602/core/file.hpp"
#include "o1602/core/png_reader.hpp"

namespace o1602::render {

namespace {

/// The cell of sprite `i` of a grid of `width` x `height` sprites, alpha made 0 or 255.
std::vector<std::uint8_t> cut(const core::PngImage& png, std::uint32_t i, std::uint32_t width, std::uint32_t height) {
    const std::uint32_t columns = png.width / width;
    const std::uint32_t left = (i % columns) * width;
    const std::uint32_t top = (i / columns) * height;
    std::vector<std::uint8_t> rgba(std::size_t{width} * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* src = &png.rgba[((std::size_t{top} + y) * png.width + left) * 4];
        std::uint8_t* dst = &rgba[std::size_t{y} * width * 4];
        for (std::uint32_t x = 0; x < width; ++x, src += 4, dst += 4) {
            if (src[3] >= 128) {
                std::copy_n(src, 3, dst);
                dst[3] = 255;
            }
        }
    }
    return rgba;
}

}  // namespace

PackageImageSource::PackageImageSource(const assets::PackageImage& image, std::size_t zoom) {
    m_sprites.resize(image.count);
    // The level with a file of its own, this one or the next larger one that has one.
    std::size_t from = std::min(zoom, assets::kPackageZoomLevels - 1);
    while (from > 0 && image.files[from].empty()) {
        --from;
    }
    const auto bytes = core::read_file(image.files[from]);
    if (!bytes) {
        m_error = core::path_to_utf8(image.files[from]) + ": " + bytes.error().message;
        return;
    }
    auto png = core::decode_png(*bytes);
    if (!png) {
        m_error = core::path_to_utf8(image.files[from]) + ": " + png.error().message;
        return;
    }
    const std::uint32_t width = assets::package_sprite_width(image, from);
    const std::uint32_t height = assets::package_sprite_height(image, from);
    if ((png->width / width) * (png->height / height) < image.count) {
        m_error = core::path_to_utf8(image.files[from]) + ": fewer sprites than when the package was loaded";
        return;
    }
    for (std::uint32_t i = 0; i < image.count; ++i) {
        Sprite& s = m_sprites[i];
        s.rgba = cut(*png, i, width, height);
        s.width = width;
        s.height = height;
        for (std::size_t step = from; step < zoom; ++step) {
            std::vector<std::uint8_t> half;
            halve_rgba(s.rgba, s.width, s.height, half, s.width, s.height);
            s.rgba = std::move(half);
        }
        // Leave out the transparent rows at the top; a sprite with none opaque is empty.
        std::uint32_t first = 0;
        const auto row_clear = [&](std::uint32_t y) {
            for (std::uint32_t x = 0; x < s.width; ++x) {
                if (s.rgba[(std::size_t{y} * s.width + x) * 4 + 3] != 0) {
                    return false;
                }
            }
            return true;
        };
        while (first < s.height && row_clear(first)) {
            ++first;
        }
        if (first == s.height) {
            s = Sprite{};
            continue;
        }
        s.rgba.erase(s.rgba.begin(), s.rgba.begin() + static_cast<std::ptrdiff_t>(std::size_t{first} * s.width * 4));
        s.height -= first;
    }
}

void PackageImageSource::size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const {
    width = i < m_sprites.size() ? m_sprites[i].width : 0;
    height = i < m_sprites.size() ? m_sprites[i].height : 0;
}

bool PackageImageSource::pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const {
    if (i >= m_sprites.size()) {
        return false;
    }
    rgba = m_sprites[i].rgba;
    return true;
}

void PlacedSources::place(std::uint32_t at, const SpriteSource& source) {
    const auto it = std::upper_bound(m_placed.begin(), m_placed.end(), at,
                                     [](std::uint32_t a, const Placed& p) { return a < p.at; });
    m_placed.insert(it, Placed{at, &source});
    m_count = std::max(m_count, at + source.count());
}

const PlacedSources::Placed* PlacedSources::find(std::uint32_t i) const noexcept {
    const auto it = std::upper_bound(m_placed.begin(), m_placed.end(), i,
                                     [](std::uint32_t a, const Placed& p) { return a < p.at; });
    if (it == m_placed.begin()) {
        return nullptr;
    }
    const Placed& p = *std::prev(it);
    return i - p.at < p.source->count() ? &p : nullptr;
}

void PlacedSources::size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const {
    if (const Placed* p = find(i)) {
        p->source->size(i - p->at, width, height);
    } else {
        width = 0;
        height = 0;
    }
}

bool PlacedSources::pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const {
    if (const Placed* p = find(i)) {
        return p->source->pixels(i - p->at, rgba);
    }
    rgba.clear();
    return true;
}

IconPixels package_icon_pixels(const assets::PackageIcon& icon, double scale) {
    IconPixels out;
    const auto bytes = core::read_file(icon.file);
    if (!bytes) {
        out.error = core::path_to_utf8(icon.file) + ": " + bytes.error().message;
        return out;
    }
    auto png = core::decode_png(*bytes);
    if (!png) {
        out.error = core::path_to_utf8(icon.file) + ": " + png.error().message;
        return out;
    }
    if (scale >= 1.0) {
        out.width = png->width;
        out.height = png->height;
        out.rgba = std::move(png->rgba);
        return out;
    }
    out.width = std::max(1U, static_cast<std::uint32_t>(std::lround(png->width * scale)));
    out.height = std::max(1U, static_cast<std::uint32_t>(std::lround(png->height * scale)));
    out.rgba.assign(std::size_t{out.width} * out.height * 4, 0);
    // Each pixel averages the block of pixels it covers, weighted by their alpha so that the
    // colours of transparent ones do not show at the edges.
    for (std::uint32_t y = 0; y < out.height; ++y) {
        const std::uint32_t y0 = y * png->height / out.height;
        const std::uint32_t y1 = std::max(y0 + 1, (y + 1) * png->height / out.height);
        for (std::uint32_t x = 0; x < out.width; ++x) {
            const std::uint32_t x0 = x * png->width / out.width;
            const std::uint32_t x1 = std::max(x0 + 1, (x + 1) * png->width / out.width);
            std::uint64_t r = 0;
            std::uint64_t g = 0;
            std::uint64_t b = 0;
            std::uint64_t a = 0;
            for (std::uint32_t sy = y0; sy < y1; ++sy) {
                for (std::uint32_t sx = x0; sx < x1; ++sx) {
                    const std::uint8_t* p = &png->rgba[(std::size_t{sy} * png->width + sx) * 4];
                    r += std::uint64_t{p[0]} * p[3];
                    g += std::uint64_t{p[1]} * p[3];
                    b += std::uint64_t{p[2]} * p[3];
                    a += p[3];
                }
            }
            std::uint8_t* d = &out.rgba[(std::size_t{y} * out.width + x) * 4];
            const std::uint64_t n = std::uint64_t{y1 - y0} * (x1 - x0);
            if (a > 0) {
                d[0] = static_cast<std::uint8_t>(r / a);
                d[1] = static_cast<std::uint8_t>(g / a);
                d[2] = static_cast<std::uint8_t>(b / a);
                d[3] = static_cast<std::uint8_t>(a / n);
            }
        }
    }
    return out;
}

}  // namespace o1602::render
