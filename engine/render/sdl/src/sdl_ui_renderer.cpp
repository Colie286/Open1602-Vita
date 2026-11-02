// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/sdl_ui_renderer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "o1602/render/package_sprites.hpp"
#include "o1602/render/shape_tint.hpp"
#include "o1602/render/variant_tint.hpp"
#include "o1602/ui/package_icons.hpp"

namespace o1602::render {

namespace {

constexpr int kModeSprite = 0;
constexpr int kModeMask = 1;
constexpr int kModeRaster = 2;          ///< the shape where (x + y) of the sprite is even
constexpr int kModeRasterOdd = 3;       ///< where it is odd: a sprite at an odd place stays on the screen's even pixels
constexpr int kModeFaded = 4;           ///< the sprite in its own colours where (x + y) of the sprite is even
constexpr int kModeFadedOdd = 5;        ///< the same where it is odd
constexpr std::size_t kMaxTexts = 512;  // cached text textures before the least recently used go
constexpr std::int32_t kCheckerSize = 64;
// RGBA bytes of interface sprites kept: far more than the interface draws at a time, so that only
// sprites of tints no longer shown go.
constexpr std::size_t kMaxSpriteBytes = std::size_t{96} << 20U;
constexpr std::uint8_t kMaskAlpha = 110;  // opacity of a GraphicFill shadow

/// A checkerboard (white, every second pixel, coloured when drawn) for greyed and faded gadgets; one
/// column wider than the tiles taken from it, so a tile can start on either parity.
constexpr std::int32_t kCheckerWidth = kCheckerSize + 1;
std::vector<std::uint8_t> checker_pixels() {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kCheckerWidth * kCheckerSize * 4), 0);
    for (std::int32_t y = 0; y < kCheckerSize; ++y) {
        for (std::int32_t x = 0; x < kCheckerWidth; ++x) {
            const auto i = static_cast<std::size_t>((y * kCheckerWidth + x) * 4);
            rgba[i] = 255;
            rgba[i + 1] = 255;
            rgba[i + 2] = 255;
            rgba[i + 3] = ((x + y) & 1) == 0 ? 255 : 0;
        }
    }
    return rgba;
}

/// Whether a raster pixel at (x, y) of a sprite is drawn in `mode`.
constexpr bool raster_pixel(int mode, std::uint32_t x, std::uint32_t y) noexcept {
    const std::uint32_t odd = (x + y) & 1U;
    return mode == kModeRaster || mode == kModeFaded         ? odd == 0
           : mode == kModeRasterOdd || mode == kModeFadedOdd ? odd == 1
                                                             : true;
}

/// Whether `mode` keeps the sprite's own colours.
constexpr bool own_colours(int mode) noexcept {
    return mode == kModeSprite || mode == kModeFaded || mode == kModeFadedOdd;
}

/// Makes the pixels of RGBA `rgba` that `mode` leaves out transparent.
void clear_raster(std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t height, int mode) {
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t i = (std::size_t{y} * width + x) * 4;
            if (i + 3 < rgba.size() && !raster_pixel(mode, x, y)) {
                rgba[i + 3] = 0;
            }
        }
    }
}

}  // namespace

SdlUiRenderer::SdlUiRenderer(SDL_Renderer* renderer, const assets::GameDirectory& game, const assets::Palette& palette,
                             ui::ArtSize size, bool data_fixes)
    : m_renderer(renderer), m_palette(palette), m_size(size), m_texts(kMaxTexts) {
    for (std::int32_t b = 0; b < static_cast<std::int32_t>(m_sheets.size()); ++b) {
        if (const auto path = game.resolve(ui::sheet_file(b, size))) {
            if (auto sheet = assets::SpriteSheet::load(*path)) {
                m_sheets[static_cast<std::size_t>(b)] = std::move(sheet).value();
            }
        }
    }
    // The fonts of every size: text takes the size its scale shows unshrunk (ui::text_art_size()).
    for (std::size_t z = 0; z < m_fonts.size(); ++z) {
        const auto font_size = static_cast<ui::ArtSize>(z);
        for (std::int32_t f = 0; f < static_cast<std::int32_t>(ui::kFontSlots); ++f) {
            auto font = ui::Font::load(game, ui::font_file(f, font_size));
            if (!font && f == ui::kOwnFontSlot) {
                // The History Edition lacks ZEI14V: the small font, as at the other sizes.
                font = ui::Font::load(game, ui::font_file(1, font_size));
            }
            if (font) {
                font->set_design_scale(ui::art_scale(font_size));
                if (data_fixes) {
                    (void)font->fix_accents();
                }
                m_fonts[z][static_cast<std::size_t>(f)] = std::move(font).value();
            }
        }
    }
    for (std::size_t c = 0; c < m_colours.size(); ++c) {
        m_colours[c] = ui::text_recolour(palette, c);
    }
}

SdlUiRenderer::~SdlUiRenderer() {
    const auto destroy = [](auto& map) {
        for (auto& [k, t] : map) {
            SDL_DestroyTexture(t.texture);
        }
    };
    for (auto& [k, s] : m_sprites) {
        SDL_DestroyTexture(s.texture.texture);
    }
    m_texts.for_each([](const Texture& t) { SDL_DestroyTexture(t.texture); });
    destroy(m_images);
    if (m_checker.texture != nullptr) {
        SDL_DestroyTexture(m_checker.texture);
    }
}

void SdlUiRenderer::next_frame() {
    m_texts.next_frame();
    ++m_frame;
    if (m_sprite_bytes > kMaxSpriteBytes) {
        trim_sprites(kMaxSpriteBytes / 4 * 3);
    }
}

void SdlUiRenderer::trim_sprites(std::size_t bytes) {
    std::vector<std::pair<std::uint64_t, std::map<SpriteKey, CachedSprite>::iterator>> by_age;
    by_age.reserve(m_sprites.size());
    for (auto it = m_sprites.begin(); it != m_sprites.end(); ++it) {
        by_age.emplace_back(it->second.frame, it);
    }
    std::sort(by_age.begin(), by_age.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [frame, it] : by_age) {
        if (m_sprite_bytes <= bytes || frame == m_frame) {
            break;
        }
        const Texture& t = it->second.texture;
        m_sprite_bytes -= std::min(m_sprite_bytes,
                                   std::size_t{static_cast<std::uint32_t>(t.w)} * static_cast<std::uint32_t>(t.h) * 4);
        SDL_DestroyTexture(t.texture);
        m_sprites.erase(it);
    }
}

void SdlUiRenderer::release_textures() {
    const auto destroy = [](auto& map) {
        for (auto& [k, t] : map) {
            SDL_DestroyTexture(t.texture);
        }
        map.clear();
    };
    for (auto& [k, s] : m_sprites) {
        SDL_DestroyTexture(s.texture.texture);
    }
    m_sprites.clear();
    m_sprite_bytes = 0;
    m_texts.for_each([](const Texture& t) { SDL_DestroyTexture(t.texture); });
    m_texts = TextCache<Texture>(kMaxTexts);
    destroy(m_images);
    if (m_checker.texture != nullptr) {
        SDL_DestroyTexture(m_checker.texture);
    }
    m_checker = Texture{};
}

const ui::Font* SdlUiRenderer::font(std::int32_t slot, ui::ArtSize size) const noexcept {
    if (slot < 0 || slot >= static_cast<std::int32_t>(ui::kFontSlots)) {
        return nullptr;
    }
    const auto i = static_cast<std::size_t>(slot);
    const auto& own = m_fonts[static_cast<std::size_t>(size)][i];
    // A size whose font is missing (the History Edition has only the large ones): the large font.
    const auto& large = m_fonts[static_cast<std::size_t>(ui::ArtSize::Large)][i];
    return own ? &*own : large ? &*large : nullptr;
}

std::array<const ui::Font*, ui::kFontSlots> SdlUiRenderer::fonts(double scale) const noexcept {
    const ui::ArtSize size = ui::text_art_size(scale);
    std::array<const ui::Font*, ui::kFontSlots> f{};
    for (std::size_t i = 0; i < f.size(); ++i) {
        f[i] = font(static_cast<std::int32_t>(i), size);
    }
    return f;
}

double SdlUiRenderer::art_scale() const noexcept {
    // The art of the three sizes is drawn for screens 640, 800 and 1024 wide.
    return ui::art_scale(m_size);
}

std::optional<std::pair<std::int32_t, std::int32_t>> SdlUiRenderer::sprite_size(std::int32_t block,
                                                                                std::int32_t gfx) const {
    if (const assets::PackageIcon* icon = assets::package_icon_at(ui::package_icons(), gfx)) {
        // As package_icon_pixels() makes it at the art's scale.
        const double scale = std::min(1.0, art_scale());
        return std::make_pair(std::max(1, static_cast<std::int32_t>(std::lround(icon->width * scale))),
                              std::max(1, static_cast<std::int32_t>(std::lround(icon->height * scale))));
    }
    if (block < 0 || block >= static_cast<std::int32_t>(m_sheets.size()) ||
        !m_sheets[static_cast<std::size_t>(block)]) {
        return std::nullopt;
    }
    const auto& sheet = *m_sheets[static_cast<std::size_t>(block)];
    if (gfx < 0 || static_cast<std::size_t>(gfx) >= sheet.size()) {
        return std::nullopt;
    }
    const auto& info = sheet.info(static_cast<std::size_t>(gfx));
    return std::make_pair(static_cast<std::int32_t>(info.width), static_cast<std::int32_t>(info.height));
}

SdlUiRenderer::Texture SdlUiRenderer::make(const std::vector<std::uint8_t>& rgba, std::int32_t w, std::int32_t h) {
    Texture t;
    if (w <= 0 || h <= 0 ||
        rgba.size() < std::size_t{static_cast<std::uint32_t>(w)} * static_cast<std::uint32_t>(h) * 4) {
        return t;  // nothing to show, or fewer pixels than the size says
    }
    t.texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t.texture == nullptr) {
        return t;
    }
    SDL_UpdateTexture(t.texture, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(t.texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t.texture, SDL_SCALEMODE_NEAREST);
    t.w = w;
    t.h = h;
    return t;
}

std::vector<std::uint8_t> SdlUiRenderer::tinted(const assets::SpriteSheet& sheet, std::int32_t gfx,
                                                const ui::ColourTint& tint) const {
    std::vector<std::uint8_t> out;
    if (tint.shape) {
        // One sprite and the outline of its cloth (the banners of the colour screen).
        const auto image = sheet.decode(static_cast<std::size_t>(gfx));
        if (!image) {
            return out;
        }
        const std::vector<std::uint8_t> pixels = image->to_rgba(m_palette);
        std::vector<std::uint8_t> shape_pixels;
        RgbaView shape;
        if (const std::int32_t s = gfx + *tint.shape; s >= 0 && static_cast<std::size_t>(s) < sheet.size()) {
            if (auto outline = sheet.decode(static_cast<std::size_t>(s))) {
                shape_pixels = outline->to_rgba(m_palette);
                shape = RgbaView{shape_pixels.data(), outline->width, outline->height};
            }
        }
        tint_shape(RgbaView{pixels.data(), image->width, image->height}, shape, tint.colour, out);
        return out;
    }
    // The family's variants around the drawn one (the base); missing ones are left out.
    std::vector<std::vector<std::uint8_t>> pixels(tint.variants);
    std::vector<RgbaView> views(tint.variants);
    for (std::int32_t v = 0; v < tint.variants; ++v) {
        const std::int32_t g = gfx + (v - tint.base) * tint.stride;
        if (g < 0 || static_cast<std::size_t>(g) >= sheet.size()) {
            continue;
        }
        if (auto image = sheet.decode(static_cast<std::size_t>(g))) {
            pixels[static_cast<std::size_t>(v)] = image->to_rgba(m_palette);
            views[static_cast<std::size_t>(v)] =
                RgbaView{pixels[static_cast<std::size_t>(v)].data(), image->width, image->height};
        }
    }
    tint_variant(views, tint.base, tint.pattern, tint.colour, out);
    return out;
}

SdlUiRenderer::Texture SdlUiRenderer::sprite(std::int32_t block, std::int32_t gfx, int mode, std::int32_t colour,
                                             const std::optional<ui::ColourTint>& tint) {
    const assets::PackageIcon* icon = assets::package_icon_at(ui::package_icons(), gfx);
    if (icon != nullptr) {
        block = -1;  // the same picture in every sheet
    }
    const bool tinting = own_colours(mode) && tint && (tint->shape || tint->base < tint->variants);
    const SpriteKey key{
        block,
        gfx,
        mode,
        own_colours(mode) ? 0 : colour,
        tinting ? 0x01000000U | (std::uint32_t{tint->colour.r} << 16U) | (std::uint32_t{tint->colour.g} << 8U) |
                      tint->colour.b
                : 0U,
        tinting ? tint->stride : 0,
        tinting ? (std::uint32_t{tint->variants} << 16U) | (std::uint32_t{tint->pattern} << 8U) | tint->base : 0U,
        tinting ? tint->shape : std::nullopt};
    if (const auto it = m_sprites.find(key); it != m_sprites.end()) {
        it->second.frame = m_frame;
        return it->second.texture;
    }
    Texture t;
    if (icon != nullptr) {
        t = package_icon(*icon, mode, colour);
    } else if (block >= 0 && block < static_cast<std::int32_t>(m_sheets.size()) &&
               m_sheets[static_cast<std::size_t>(block)] && gfx >= 0 &&
               static_cast<std::size_t>(gfx) < m_sheets[static_cast<std::size_t>(block)]->size()) {
        if (auto image = m_sheets[static_cast<std::size_t>(block)]->decode(static_cast<std::size_t>(gfx))) {
            std::vector<std::uint8_t> rgba;
            if (own_colours(mode)) {
                rgba = tinting ? tinted(*m_sheets[static_cast<std::size_t>(block)], gfx, *tint)
                               : image->to_rgba(m_palette);
                if (mode != kModeSprite) {
                    clear_raster(rgba, image->width, image->height, mode);
                }
            } else {
                // The shape in one palette colour, solid or as a checkerboard.
                const assets::Rgba c = m_palette[static_cast<std::uint8_t>(std::clamp(colour, 0, 255))];
                rgba.assign(std::size_t{image->width} * image->height * 4, 0);
                for (std::uint32_t y = 0; y < image->height; ++y) {
                    for (std::uint32_t x = 0; x < image->width; ++x) {
                        const std::size_t i = std::size_t{y} * image->width + x;
                        if (image->opaque[i] == 0 || !raster_pixel(mode, x, y)) {
                            continue;
                        }
                        rgba[4 * i] = c.r;
                        rgba[4 * i + 1] = c.g;
                        rgba[4 * i + 2] = c.b;
                        rgba[4 * i + 3] = 255;
                    }
                }
            }
            t = make(rgba, static_cast<std::int32_t>(image->width), static_cast<std::int32_t>(image->height));
        }
    }
    m_sprites.emplace(key, CachedSprite{t, m_frame});
    m_sprite_bytes += std::size_t{static_cast<std::uint32_t>(t.w)} * static_cast<std::uint32_t>(t.h) * 4;
    return t;
}

SdlUiRenderer::Texture SdlUiRenderer::package_icon(const assets::PackageIcon& icon, int mode, std::int32_t colour) {
    IconPixels picture = package_icon_pixels(icon, art_scale());
    if (!picture.error.empty()) {
        std::fprintf(stderr, "mods: icon %s: %s\n", icon.id.c_str(), picture.error.c_str());
        return Texture{};
    }
    if (own_colours(mode)) {
        if (mode != kModeSprite) {
            clear_raster(picture.rgba, picture.width, picture.height, mode);
        }
    } else {
        // Its shape in one palette colour, as the sheets' sprites: the pixels of at least half alpha.
        const assets::Rgba c = m_palette[static_cast<std::uint8_t>(std::clamp(colour, 0, 255))];
        for (std::uint32_t y = 0; y < picture.height; ++y) {
            for (std::uint32_t x = 0; x < picture.width; ++x) {
                std::uint8_t* p = &picture.rgba[(std::size_t{y} * picture.width + x) * 4];
                const bool shown = p[3] >= 128 && raster_pixel(mode, x, y);
                p[0] = c.r;
                p[1] = c.g;
                p[2] = c.b;
                p[3] = shown ? 255 : 0;
            }
        }
    }
    return make(picture.rgba, static_cast<std::int32_t>(picture.width), static_cast<std::int32_t>(picture.height));
}

void SdlUiRenderer::forget_package_icons() {
    for (auto it = m_sprites.begin(); it != m_sprites.end();) {
        if (std::get<1>(it->first) >= assets::kFirstPackageIcon) {
            const Texture& t = it->second.texture;
            m_sprite_bytes -= std::min(
                m_sprite_bytes, std::size_t{static_cast<std::uint32_t>(t.w)} * static_cast<std::uint32_t>(t.h) * 4);
            SDL_DestroyTexture(t.texture);
            it = m_sprites.erase(it);
        } else {
            ++it;
        }
    }
}

SdlUiRenderer::Texture SdlUiRenderer::text(const std::string& s, std::int32_t slot, ui::ArtSize size,
                                           std::int32_t colour, std::int32_t ink) {
    // One cache for all sizes: the slot and the size in one number; likewise the text colour and
    // the ink.
    const std::int32_t key = slot + static_cast<std::int32_t>(ui::kFontSlots) * static_cast<std::int32_t>(size);
    colour = std::clamp(colour, 0, static_cast<std::int32_t>(ui::kTextColours) - 1);
    ink = std::clamp(ink, -1, 255);
    const std::int32_t shade = colour + static_cast<std::int32_t>(ui::kTextColours) * (ink + 1);
    if (const Texture* cached = m_texts.find(s, key, shade)) {
        return *cached;
    }
    Texture t;
    if (const ui::Font* f = font(slot, size)) {
        assets::IndexedImage canvas;
        canvas.width = f->width(s) + 2;            // glyphs may overhang the last advance
        canvas.height = f->height() + f->above();  // the accents above the cell (D-012)
        canvas.indices.assign(std::size_t{canvas.width} * canvas.height, 0);
        canvas.opaque.assign(canvas.indices.size(), 0);
        const ui::Recolour recolour =
            ink < 0 ? m_colours[static_cast<std::size_t>(colour)]
                    : ui::text_recolour(m_palette, static_cast<std::size_t>(colour), static_cast<std::uint8_t>(ink));
        f->draw(canvas, s, 0, static_cast<std::int32_t>(canvas.height), recolour);
        t = make(canvas.to_rgba(m_palette), static_cast<std::int32_t>(canvas.width),
                 static_cast<std::int32_t>(canvas.height));
        if (t.texture != nullptr && f->design_scale() > ui::art_scale(size)) {
            // A larger font standing in for a missing one is shrunk: blended, so that no stroke
            // of a glyph drops out.
            SDL_SetTextureScaleMode(t.texture, SDL_SCALEMODE_LINEAR);
        }
    }
    m_texts.insert(s, key, shade, t, [](const Texture& old) { SDL_DestroyTexture(old.texture); });
    return t;
}

void SdlUiRenderer::draw_widened(std::int32_t block, std::int32_t gfx, const ui::Rect& rect, double scale) {
    const Texture t = sprite(block, gfx, kModeSprite, 0);
    if (t.texture == nullptr || t.w < 3) {
        return;
    }
    const double stretch = scale / art_scale();
    const SDL_Rect clip{rect.x, rect.y, rect.w, rect.h};
    SDL_SetRenderClipRect(m_renderer, &clip);
    SDL_SetTextureAlphaMod(t.texture, 255);
    const auto h = static_cast<float>(t.h * stretch);
    const auto y = static_cast<float>(rect.y);
    const auto part = [&](float src_x, float src_w, float x, bool mirrored = false) {
        const SDL_FRect src{src_x, 0.0F, src_w, static_cast<float>(t.h)};
        const SDL_FRect dst{x, y, static_cast<float>(static_cast<double>(src_w) * stretch), h};
        SDL_RenderTextureRotated(m_renderer, t.texture, &src, &dst, 0.0, nullptr,
                                 mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    };
    if (rect.w <= t.w * stretch) {
        part(0.0F, static_cast<float>(t.w), static_cast<float>(rect.x));  // as wide as the art: as it is
        SDL_SetRenderClipRect(m_renderer, nullptr);
        return;
    }
    // The left two thirds as they are, the middle third repeated, the right third (with the
    // frame's end) at the right edge: one frame, however wide. Every other copy is mirrored, so
    // neighbours meet at the same column of the art and no seam shows.
    const auto third = static_cast<float>(t.w / 3);
    const auto left_w = static_cast<float>(t.w) - third;
    const auto right_x = static_cast<float>(rect.x + rect.w) - static_cast<float>(static_cast<double>(third) * stretch);
    part(0.0F, left_w, static_cast<float>(rect.x));
    const auto step = static_cast<float>(static_cast<double>(third) * stretch);
    bool mirrored = true;  // the first copy starts where the left part ends: at the third's right column
    for (float x = static_cast<float>(rect.x + static_cast<double>(left_w) * stretch); x < right_x && step > 0.0F;
         x += step) {
        part(third, third, x, mirrored);
        mirrored = !mirrored;
    }
    part(left_w, third, right_x);
    SDL_SetRenderClipRect(m_renderer, nullptr);
}

void SdlUiRenderer::draw_lengthened(std::int32_t block, std::int32_t gfx, const ui::Rect& rect, double scale,
                                    std::int32_t edge) {
    const Texture t = sprite(block, gfx, kModeSprite, 0);
    edge = std::clamp(edge, 0, t.h / 4);
    if (t.texture == nullptr || t.h < 4) {
        return;
    }
    const double stretch = scale / art_scale();
    const auto bottom = static_cast<float>(rect.y + rect.h);
    const auto edge_h = static_cast<float>(edge * stretch);
    // From the art's edge on, which moves to the bottom.
    float y = static_cast<float>(rect.y + t.h * stretch) - edge_h;
    if (y + edge_h >= bottom - 0.5F) {
        return;  // the art reaches the bottom
    }
    const SDL_Rect clip{rect.x, rect.y, rect.w, rect.h};
    SDL_SetRenderClipRect(m_renderer, &clip);
    SDL_SetTextureAlphaMod(t.texture, 255);
    const auto w = static_cast<float>(t.w * stretch);
    const std::int32_t body = t.h / 2 - edge;  // rows of the lower half without the edge
    const SDL_FRect src{0.0F, static_cast<float>(t.h / 2), static_cast<float>(t.w), static_cast<float>(body)};
    const auto step = static_cast<float>(body * stretch);
    // The first copy upside down: it meets the row above the edge with that same row.
    bool flipped = true;
    for (; y < bottom - edge_h && step > 0.0F; y += step) {
        const SDL_FRect dst{static_cast<float>(rect.x), y, w, step};
        SDL_RenderTextureRotated(m_renderer, t.texture, &src, &dst, 0.0, nullptr,
                                 flipped ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
        flipped = !flipped;
    }
    if (edge > 0) {
        const SDL_FRect src_edge{0.0F, static_cast<float>(t.h - edge), static_cast<float>(t.w),
                                 static_cast<float>(edge)};
        const SDL_FRect dst{static_cast<float>(rect.x), bottom - edge_h, w, edge_h};
        SDL_RenderTexture(m_renderer, t.texture, &src_edge, &dst);
    }
    SDL_SetRenderClipRect(m_renderer, nullptr);
}

void SdlUiRenderer::fill(const ui::Rect& rect, std::uint8_t colour) {
    const assets::Rgba c = m_palette[colour];
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, 255);
    const SDL_FRect r{static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.w),
                      static_cast<float>(rect.h)};
    SDL_RenderFillRect(m_renderer, &r);
}

void SdlUiRenderer::image(int key, const std::vector<std::uint8_t>& rgba, std::int32_t width, std::int32_t height,
                          const ui::Rect& at, bool update) {
    auto it = m_images.find(key);
    if (it != m_images.end() && (it->second.w != width || it->second.h != height)) {
        SDL_DestroyTexture(it->second.texture);
        m_images.erase(it);
        it = m_images.end();
    }
    if (it == m_images.end()) {
        it = m_images.emplace(key, make(rgba, width, height)).first;
    } else if (update && it->second.texture != nullptr &&
               rgba.size() >= std::size_t{static_cast<std::uint32_t>(width)} * static_cast<std::uint32_t>(height) * 4) {
        SDL_UpdateTexture(it->second.texture, nullptr, rgba.data(), width * 4);
    }
    if (it->second.texture != nullptr) {
        const SDL_FRect dst{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.w),
                            static_cast<float>(at.h)};
        SDL_RenderTexture(m_renderer, it->second.texture, nullptr, &dst);
    }
}

void SdlUiRenderer::draw(const std::vector<ui::DrawItem>& items, std::int32_t ox, std::int32_t oy, double scale) {
    const double stretch = scale / art_scale();
    const auto blit = [&](const Texture& t, double x, double y, bool half, std::uint8_t alpha = 255) {
        if (t.texture == nullptr) {
            return;
        }
        // Alphaflg: the original's 50 % checkerboard as real half transparency.
        SDL_SetTextureAlphaMod(t.texture, half ? static_cast<std::uint8_t>(alpha / 2) : alpha);
        const SDL_FRect dst{static_cast<float>(ox + x), static_cast<float>(oy + y),
                            static_cast<float>(std::round(t.w * stretch)),
                            static_cast<float>(std::round(t.h * stretch))};
        SDL_RenderTexture(m_renderer, t.texture, nullptr, &dst);
    };
    // Whether a sprite drawn at (x, y) starts on an even pixel of the 1024 layout's screen.
    const auto even_place = [&](std::int32_t x, std::int32_t y) {
        return ((std::lround((ox + x) / scale) + std::lround((oy + y) / scale)) & 1) == 0;
    };
    // Every second pixel of (x, y, w, h) in `c`; row r starts at (h - r) & 1, so the
    // pixels where x + r + h is even.
    const auto checkerboard = [&](const ui::DrawItem& d, const assets::Rgba& c) {
        if (m_checker.texture == nullptr) {
            m_checker = make(checker_pixels(), kCheckerWidth, kCheckerSize);
        }
        if (m_checker.texture == nullptr || d.w <= 0 || d.h <= 0) {
            return;
        }
        SDL_SetTextureAlphaMod(m_checker.texture, 255);
        SDL_SetTextureColorMod(m_checker.texture, c.r, c.g, c.b);
        const auto shift = static_cast<float>(d.h & 1);
        // Tiled, not stretched: a wider rect keeps the pattern's pixels (FB-21).
        for (std::int32_t ty = 0; ty < d.h; ty += kCheckerSize) {
            for (std::int32_t tx = 0; tx < d.w; tx += kCheckerSize) {
                const auto tw = static_cast<float>(std::min(d.w - tx, kCheckerSize));
                const auto th = static_cast<float>(std::min(d.h - ty, kCheckerSize));
                const SDL_FRect src{shift, 0.0F, tw, th};
                const SDL_FRect dst{static_cast<float>(ox + d.x + tx), static_cast<float>(oy + d.y + ty), tw, th};
                SDL_RenderTexture(m_renderer, m_checker.texture, &src, &dst);
            }
        }
    };
    for (const ui::DrawItem& d : items) {
        switch (d.kind) {
            case ui::DrawItem::Kind::Sprite: {
                // Faded: only its pixels on the screen's even x + y.
                const int mode = !d.faded ? kModeSprite : even_place(d.x, d.y) ? kModeFaded : kModeFadedOdd;
                const Texture t = sprite(d.block, d.gfx, mode, 0, d.tint);
                if (d.w > 0 && d.h > 0 && t.texture != nullptr && t.w > 0 && t.h > 0) {
                    // Fitted into the box, its proportions kept and centred (icons in Open1602's windows).
                    const double fit = std::min(static_cast<double>(d.w) / t.w, static_cast<double>(d.h) / t.h);
                    const double w = std::round(t.w * fit);
                    const double h = std::round(t.h * fit);
                    SDL_SetTextureAlphaMod(t.texture, d.half ? 128 : 255);
                    const SDL_FRect dst{static_cast<float>(ox + d.x + std::trunc((d.w - w) / 2)),
                                        static_cast<float>(oy + d.y + std::trunc((d.h - h) / 2)), static_cast<float>(w),
                                        static_cast<float>(h)};
                    SDL_RenderTexture(m_renderer, t.texture, nullptr, &dst);
                } else if (d.rows > 0 && t.texture != nullptr) {
                    // A band of the art's rows, at its place.
                    const double first = std::min<double>(d.first_row * art_scale(), t.h);
                    const double count = std::min<double>(d.rows * art_scale(), t.h - first);
                    SDL_SetTextureAlphaMod(t.texture, d.half ? 128 : 255);
                    const SDL_FRect src{0.0F, static_cast<float>(first), static_cast<float>(t.w),
                                        static_cast<float>(count)};
                    const SDL_FRect dst{
                        static_cast<float>(ox + d.x), static_cast<float>(oy + d.y + std::round(first * stretch)),
                        static_cast<float>(std::round(t.w * stretch)), static_cast<float>(std::round(count * stretch))};
                    SDL_RenderTexture(m_renderer, t.texture, &src, &dst);
                } else {
                    blit(t, d.x, d.y, d.half);
                }
                break;
            }
            case ui::DrawItem::Kind::SpriteMask:
                // The shadow of the build menu's building: the original fills it solid black, which
                // looks harsh today; translucent like the figures' shadows (as the History Edition).
                blit(sprite(d.block, d.gfx,
                            !d.faded               ? kModeMask
                            : even_place(d.x, d.y) ? kModeRaster
                                                   : kModeRasterOdd,
                            d.colour),
                     d.x, d.y, d.half, kMaskAlpha);
                break;
            case ui::DrawItem::Kind::SpriteRaster: {
                // On the pixels where x + y of the screen is even (the 1024 layout's).
                blit(sprite(d.block, d.gfx, even_place(d.x, d.y) ? kModeRaster : kModeRasterOdd, d.colour), d.x, d.y,
                     false);
                break;
            }
            case ui::DrawItem::Kind::Text: {
                // In the fonts of the size that are not shrunk at this scale (ui::text_art_size()).
                const ui::ArtSize size = ui::text_art_size(scale);
                const ui::Font* f = font(d.block, size);
                const Texture t = text(d.text, d.block, size, d.colour, d.ink);
                if (f == nullptr || t.texture == nullptr) {
                    break;
                }
                const double by = scale / f->design_scale();
                const double w = t.w * by;
                const double x = d.align == ui::Align::Centre  ? d.x - std::trunc(w / 2)
                                 : d.align == ui::Align::Right ? d.x - w
                                                               : d.x;
                SDL_SetTextureAlphaMod(t.texture, 255);
                const SDL_FRect dst{static_cast<float>(ox + x), static_cast<float>(oy + d.y - t.h * by),
                                    static_cast<float>(std::round(w)), static_cast<float>(std::round(t.h * by))};
                SDL_RenderTexture(m_renderer, t.texture, nullptr, &dst);
                break;
            }
            case ui::DrawItem::Kind::Fill: {
                const assets::Rgba c = m_palette[static_cast<std::uint8_t>(std::clamp(d.colour, 0, 255))];
                if (d.faded) {
                    checkerboard(d, c);  // a faded fill or bar: its own colour on every second pixel
                    break;
                }
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, d.half ? 128 : 255);
                const SDL_FRect r{static_cast<float>(ox + d.x), static_cast<float>(oy + d.y), static_cast<float>(d.w),
                                  static_cast<float>(d.h)};
                SDL_RenderFillRect(m_renderer, &r);
                break;
            }
            case ui::DrawItem::Kind::Raster:
                checkerboard(d, assets::Rgba{0, 0, 0, 255});
                break;
        }
    }
}

}  // namespace o1602::render
