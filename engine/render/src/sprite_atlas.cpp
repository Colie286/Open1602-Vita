// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/sprite_atlas.hpp"

#include <algorithm>
#include <array>
#include <thread>

namespace o1602::render {

using core::ErrorCode;
using core::make_error;

core::Result<SpriteAtlas> SpriteAtlas::build(const assets::SpriteSheet& sheet, const assets::Palette& palette,
                                             std::uint32_t page_size) {
    const assets::SpriteSheet* sheets[] = {&sheet};
    return build(sheets, palette, page_size);
}

core::Result<SpriteAtlas> SpriteAtlas::build(std::span<const assets::SpriteSheet* const> sheets,
                                             const assets::Palette& palette, std::uint32_t page_size) {
    std::vector<SheetSource> sources;
    sources.reserve(sheets.size());
    for (const assets::SpriteSheet* sheet : sheets) {
        sources.emplace_back(*sheet, palette);
    }
    std::vector<const SpriteSource*> pointers;
    for (const SheetSource& s : sources) {
        pointers.push_back(&s);
    }
    return build(std::span<const SpriteSource* const>(pointers), page_size);
}

core::Result<SpriteAtlas> SpriteAtlas::build(std::span<const SpriteSource* const> sources, std::uint32_t page_size,
                                             const std::atomic<bool>* cancel) {
    constexpr std::uint32_t kPad = 1;
    if (page_size == 0 || page_size > 0xFFFF) {
        return make_error(ErrorCode::OutOfRange, "invalid atlas page size");
    }
    SpriteAtlas atlas;
    atlas.m_page_size = page_size;

    struct Entry {
        std::uint32_t index;  ///< global sprite index
        const SpriteSource* source;
        std::uint32_t local;
        std::uint32_t width;
        std::uint32_t height;
    };
    std::vector<Entry> entries;
    std::uint32_t total = 0;
    for (const SpriteSource* source : sources) {
        atlas.m_sheet_bases.push_back(total);
        const std::uint32_t n = source->count();
        for (std::uint32_t i = 0; i < n; ++i) {
            Entry e{total + i, source, i, 0, 0};
            source->size(i, e.width, e.height);
            entries.push_back(e);
        }
        total += n;
    }
    atlas.m_rects.resize(total);

    // Tallest first gives dense shelves; ties by width and index keep the order deterministic.
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.height != b.height ? a.height > b.height : a.width > b.width;
    });

    std::uint32_t shelf_x = 0;
    std::uint32_t shelf_y = 0;
    std::uint32_t shelf_h = 0;
    const auto new_page = [&] {
        RgbaImage page;
        page.width = page_size;
        page.height = page_size;
        page.pixels.assign(std::size_t{page_size} * page_size * 4, 0);
        atlas.m_pages.push_back(std::move(page));
        shelf_x = shelf_y = shelf_h = 0;
    };

    // The sprites are decoded in parallel, a batch at a time (at most 1024 sprites or 64 MB), and
    // placed one after the other in the order above: the atlas is the same on every machine
    // (FB-25). Sources decode from const data; TintedUnitSource guards its cache.
    constexpr std::size_t kBatchSprites = 1024;
    constexpr std::size_t kBatchBytes = std::size_t{64} << 20;
    const unsigned workers = std::clamp(std::thread::hardware_concurrency(), 1U, 8U);
    std::vector<std::vector<std::uint8_t>> decoded;
    std::vector<std::uint8_t> valid;
    const auto fits = [&](const Entry& e) { return e.width + kPad <= page_size && e.height + kPad <= page_size; };
    const auto decode_batch = [&](std::size_t first, std::size_t count) {
        decoded.resize(std::max(decoded.size(), count));
        valid.assign(count, 0);
        std::atomic<std::size_t> next{0};
        const auto work = [&] {
            for (std::size_t j = next++; j < count; j = next++) {
                const Entry& e = entries[first + j];
                if (e.width == 0 || e.height == 0 || !fits(e)) {
                    continue;
                }
                valid[j] =
                    e.source->pixels(e.local, decoded[j]) && decoded[j].size() >= std::size_t{e.width} * e.height * 4
                        ? 1
                        : 0;
            }
        };
        std::vector<std::thread> threads;
        for (unsigned t = 1; t < std::min<std::size_t>(workers, count); ++t) {
            threads.emplace_back(work);
        }
        work();
        for (std::thread& t : threads) {
            t.join();
        }
    };

    std::size_t batch_first = 0;
    std::size_t batch_end = 0;
    for (std::size_t k = 0; k < entries.size(); ++k) {
        const Entry& e = entries[k];
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            return make_error(ErrorCode::Unsupported, "cancelled");
        }
        if (e.width == 0 || e.height == 0) {
            continue;
        }
        if (!fits(e)) {
            return make_error(ErrorCode::OutOfRange,
                              "sprite " + std::to_string(e.index) + " does not fit into an atlas page");
        }
        if (k >= batch_end) {
            batch_first = k;
            batch_end = k;
            std::size_t bytes = 0;
            while (batch_end < entries.size() && batch_end - batch_first < kBatchSprites &&
                   (bytes == 0 || bytes < kBatchBytes)) {
                bytes += std::size_t{entries[batch_end].width} * entries[batch_end].height * 4;
                ++batch_end;
            }
            decode_batch(batch_first, batch_end - batch_first);
        }
        if (valid[k - batch_first] == 0) {
            continue;  // corrupt sprite: drawn as missing
        }
        const std::vector<std::uint8_t>& rgba = decoded[k - batch_first];
        if (atlas.m_pages.empty()) {
            new_page();
        }
        if (shelf_x + e.width + kPad > page_size) {  // next shelf
            shelf_y += shelf_h;
            shelf_x = 0;
            shelf_h = 0;
        }
        if (shelf_y + e.height + kPad > page_size) {
            new_page();
        }
        RgbaImage& page = atlas.m_pages.back();
        for (std::uint32_t y = 0; y < e.height; ++y) {
            const std::size_t src = std::size_t{y} * e.width * 4;
            const std::size_t dst = ((std::size_t{shelf_y} + y) * page_size + shelf_x) * 4;
            std::copy_n(rgba.begin() + static_cast<std::ptrdiff_t>(src), std::size_t{e.width} * 4,
                        page.pixels.begin() + static_cast<std::ptrdiff_t>(dst));
        }
        atlas.m_rects[e.index] = AtlasRect{static_cast<std::uint16_t>(atlas.m_pages.size() - 1),
                                           static_cast<std::uint16_t>(shelf_x), static_cast<std::uint16_t>(shelf_y),
                                           static_cast<std::uint16_t>(e.width), static_cast<std::uint16_t>(e.height)};
        atlas.m_max_width = std::max(atlas.m_max_width, static_cast<std::int32_t>(e.width));
        atlas.m_max_height = std::max(atlas.m_max_height, static_cast<std::int32_t>(e.height));
        shelf_x += e.width + kPad;
        shelf_h = std::max(shelf_h, e.height + kPad);
    }
    return atlas;
}

void SpriteAtlas::drop_pixels() noexcept {
    for (RgbaImage& page : m_pages) {
        page.pixels = {};
    }
}

namespace {

/// Translucent tile diamond: width w, height w/2, bounding box left = x, bottom = y. Hatched, only
/// every second pixel (hatch_pixel at the draw list's x + dx, y + dy) is filled.
void fill_diamond(RgbaImage& target, std::int32_t x, std::int32_t y, std::int32_t w, assets::Rgba colour,
                  std::uint8_t alpha, bool hatched = false, std::int32_t dx = 0, std::int32_t dy = 0) {
    const std::int32_t h = w / 2;
    const std::int32_t top = y - h;
    for (std::int32_t row = 0; row < h; ++row) {
        const std::int32_t ty = top + row;
        if (ty < 0 || ty >= static_cast<std::int32_t>(target.height)) {
            continue;
        }
        // Half width at this row: widest in the middle.
        const std::int32_t d = row < h / 2 ? row : h - 1 - row;
        const std::int32_t half = (d + 1) * 2;
        const std::int32_t cx = x + w / 2;
        const std::int32_t x0 = std::max(0, cx - half);
        const std::int32_t x1 = std::min(static_cast<std::int32_t>(target.width), cx + half);
        for (std::int32_t tx = x0; tx < x1; ++tx) {
            if (hatched && !hatch_pixel(tx + dx, ty + dy)) {
                continue;
            }
            std::uint8_t* p =
                &target.pixels[(static_cast<std::size_t>(ty) * target.width + static_cast<std::size_t>(tx)) * 4];
            p[0] = static_cast<std::uint8_t>((p[0] * (255 - alpha) + colour.r * alpha) / 255);
            p[1] = static_cast<std::uint8_t>((p[1] * (255 - alpha) + colour.g * alpha) / 255);
            p[2] = static_cast<std::uint8_t>((p[2] * (255 - alpha) + colour.b * alpha) / 255);
        }
    }
}

}  // namespace

void blit_draw_list(const std::vector<DrawCommand>& commands, const SpriteAtlas& atlas, RgbaImage& target,
                    std::int32_t origin_x, std::int32_t origin_y) {
    const auto tw = static_cast<std::int32_t>(target.width);
    const auto th = static_cast<std::int32_t>(target.height);
    const std::uint32_t ps = atlas.page_size();
    for (const DrawCommand& c : commands) {
        if (c.sprite == DrawCommand::kOverlayDiamond) {
            fill_diamond(target, c.x - origin_x, c.y - origin_y, c.tint_size, c.tint, c.tint.a);
            continue;
        }
        if (c.sprite == DrawCommand::kOverlayHatch) {
            fill_diamond(target, c.x - origin_x, c.y - origin_y, c.tint_size, c.tint, c.tint.a, true, origin_x,
                         origin_y);
            continue;
        }
        if (c.sprite == DrawCommand::kOverlayOutline) {
            for_each_outline_step(c, [&](std::int32_t x, std::int32_t y) {
                const std::int32_t ty = c.y - origin_y + y;
                if (ty < 0 || ty >= th) {
                    return;
                }
                for (std::int32_t tx = c.x - origin_x + x; tx < c.x - origin_x + x + 2; ++tx) {
                    if (tx >= 0 && tx < tw) {
                        std::uint8_t* p =
                            &target
                                 .pixels[(static_cast<std::size_t>(ty) * target.width + static_cast<std::size_t>(tx)) *
                                         4];
                        p[0] = c.tint.r;
                        p[1] = c.tint.g;
                        p[2] = c.tint.b;
                    }
                }
            });
            continue;
        }
        const AtlasRect& r = atlas.rect(c.sprite);
        if (r.empty()) {
            continue;
        }
        const std::int32_t left = c.x - origin_x;
        const std::int32_t top = c.y - r.height - origin_y;
        const std::int32_t x0 = std::max(0, -left);
        const std::int32_t y0 = std::max(0, -top);
        const std::int32_t x1 = std::min<std::int32_t>(r.width, tw - left);
        const std::int32_t y1 =
            std::min<std::int32_t>(r.height - std::min<std::int32_t>(c.cut_bottom, r.height), th - top);
        const std::vector<std::uint8_t>& page = atlas.pages()[r.page].pixels;
        if (page.empty()) {
            continue;  // pixels were dropped
        }
        const bool shadow = c.mode == DrawCommand::Mode::Shadow;
        const bool translucent = c.mode == DrawCommand::Mode::Translucent;
        const bool dimmed = c.mode == DrawCommand::Mode::Dimmed;
        const bool tinted = c.mode == DrawCommand::Mode::Tinted;
        const bool checkered = c.mode == DrawCommand::Mode::Checkered;
        const std::uint32_t alpha = c.tint.a;
        for (std::int32_t y = y0; y < y1; ++y) {
            const std::uint8_t* src = &page[((std::size_t{r.y} + static_cast<std::size_t>(y)) * ps + r.x) * 4];
            std::uint8_t* dst = &target.pixels[(static_cast<std::size_t>(top + y) * target.width) * 4];
            for (std::int32_t x = x0; x < x1; ++x) {
                const std::uint8_t* s = src + static_cast<std::size_t>(x) * 4;
                if (s[3] == 0) {
                    continue;
                }
                std::uint8_t* d = dst + static_cast<std::size_t>(left + x) * 4;
                if (shadow) {
                    d[0] = static_cast<std::uint8_t>((d[0] * (255 - alpha) + c.tint.r * alpha) / 255);
                    d[1] = static_cast<std::uint8_t>((d[1] * (255 - alpha) + c.tint.g * alpha) / 255);
                    d[2] = static_cast<std::uint8_t>((d[2] * (255 - alpha) + c.tint.b * alpha) / 255);
                } else if (translucent) {
                    for (int k = 0; k < 3; ++k) {
                        d[k] = static_cast<std::uint8_t>((d[k] * (255 - alpha) + s[k] * alpha) / 255);
                    }
                } else if (tinted) {
                    const std::array<std::uint32_t, 3> t{c.tint.r, c.tint.g, c.tint.b};
                    for (std::size_t k = 0; k < 3; ++k) {
                        const std::uint32_t coloured = s[k] * t[k] / 255;
                        d[k] = static_cast<std::uint8_t>((d[k] * (255 - alpha) + coloured * alpha) / 255);
                    }
                } else if (dimmed) {
                    d[0] = static_cast<std::uint8_t>(s[0] * c.tint.r / 255);
                    d[1] = static_cast<std::uint8_t>(s[1] * c.tint.g / 255);
                    d[2] = static_cast<std::uint8_t>(s[2] * c.tint.b / 255);
                    d[3] = s[3];
                } else if (checkered && hatch_pixel(c.x + x, c.y - r.height + y)) {
                    d[0] = c.tint.r;
                    d[1] = c.tint.g;
                    d[2] = c.tint.b;
                    d[3] = s[3];
                } else {
                    std::copy_n(s, 4, d);
                }
            }
        }
    }
}

}  // namespace o1602::render
