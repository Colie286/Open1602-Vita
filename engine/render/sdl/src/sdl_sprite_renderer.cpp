// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/sdl_sprite_renderer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <vector>
#include "o1602/core/profile.hpp"

#ifdef O1602_EXPERIMENTAL_VITA
#include "o1602/platform/vita.hpp"
#endif
namespace o1602::render {

SdlSpriteRenderer::SdlSpriteRenderer(SDL_Renderer* renderer) : m_renderer(renderer) {}

SdlSpriteRenderer::~SdlSpriteRenderer() {
    release();
}

void SdlSpriteRenderer::release() {
    for (SDL_Texture* texture : m_pages) {
        SDL_DestroyTexture(texture);
    }
    m_pages.clear();
    if (m_hatch != nullptr) {
        SDL_DestroyTexture(m_hatch);
        m_hatch = nullptr;
    }
    for (SDL_Texture** t : {&m_checker_pattern, &m_checker}) {
        if (*t != nullptr) {
            SDL_DestroyTexture(*t);
            *t = nullptr;
        }
    }
    m_checker_failed = false;
    m_atlas = nullptr;
    m_cache.reset();
}

core::Status SdlSpriteRenderer::upload(const SpriteAtlas& atlas) {
    release();
    if(atlas.paged())m_cache=PagedSpriteCache::acquire(m_renderer);
    if(!m_cache)for (const RgbaImage& page : atlas.pages()) {
        SDL_Texture* texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                                 static_cast<int>(page.width), static_cast<int>(page.height));
        if (texture == nullptr) {
            release();
            return core::make_error(core::ErrorCode::PlatformError,
                                    std::string("SDL_CreateTexture: ") + SDL_GetError());
        }
        m_pages.push_back(texture);
        if (!SDL_UpdateTexture(texture, nullptr, page.pixels.data(), static_cast<int>(page.width * 4))) {
            release();
            return core::make_error(core::ErrorCode::PlatformError,
                                    std::string("SDL_UpdateTexture: ") + SDL_GetError());
        }
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    m_hatch =
        SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kHatchWidth, kHatchHeight);
    if (m_hatch != nullptr) {
        // Texel (x, y) is lit where hatch_pixel(x, y) holds.
        std::vector<std::uint8_t> pixels(std::size_t{kHatchWidth} * kHatchHeight * 4, 0);
        for (int y = 0; y < kHatchHeight; ++y) {
            for (int x = 0; x < kHatchWidth; ++x) {
                if (hatch_pixel(x, y)) {
                    std::fill_n(pixels.begin() + (y * kHatchWidth + x) * 4, 4, std::uint8_t{255});
                }
            }
        }
        SDL_UpdateTexture(m_hatch, nullptr, pixels.data(), kHatchWidth * 4);
        SDL_SetTextureBlendMode(m_hatch, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(m_hatch, SDL_SCALEMODE_NEAREST);
    }
    m_atlas = &atlas;
    return core::Ok{};
}

bool SdlSpriteRenderer::draw_checker(const DrawCommand& c, std::int32_t height, float x0, float y0, float x1,
                                     float y1) {
    if (m_checker_failed || m_atlas == nullptr) {
        return false;
    }
    const AtlasRect& r = m_atlas->rect(c.sprite);
    const int w = std::max(m_atlas->max_sprite_width(), 1);
    const int h = std::max(m_atlas->max_sprite_height(), 1);
    if (m_checker == nullptr) {
        m_checker_pattern = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w + 1, h);
        m_checker = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        if (m_checker_pattern == nullptr || m_checker == nullptr) {
            m_checker_failed = true;
            return false;
        }
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w + 1) * static_cast<std::size_t>(h) * 4, 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w + 1; ++x) {
                if (hatch_pixel(x, y)) {
                    std::fill_n(pixels.begin() + (static_cast<std::ptrdiff_t>(y) * (w + 1) + x) * 4, 4,
                                std::uint8_t{255});
                }
            }
        }
        SDL_UpdateTexture(m_checker_pattern, nullptr, pixels.data(), (w + 1) * 4);
        SDL_SetTextureScaleMode(m_checker_pattern, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureScaleMode(m_checker, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(m_checker, SDL_BLENDMODE_BLEND);
    }
    // The checker keeps the pattern only where the sprite is: dst = dst * source alpha.
    const SDL_BlendMode mask =
        SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                   SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    SDL_Texture* page = m_pages[r.page];
    SDL_Texture* screen = SDL_GetRenderTarget(m_renderer);
    if (!SDL_SetRenderTarget(m_renderer, m_checker)) {
        m_checker_failed = true;
        return false;
    }
    const bool masked =
        SDL_SetTextureBlendMode(page, mask) && SDL_SetTextureBlendMode(m_checker_pattern, SDL_BLENDMODE_NONE);
    if (masked) {
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
        SDL_RenderClear(m_renderer);
        // The pattern lies on the world's pixels: a column later where the sprite's corner is not lit.
        const int shift = (c.x + c.y - r.height) & 1;
        const auto fw = static_cast<float>(r.width);
        const auto fh = static_cast<float>(height);
        SDL_SetTextureColorMod(m_checker_pattern, c.tint.r, c.tint.g, c.tint.b);
        const SDL_FRect pattern_src{static_cast<float>(shift), 0.0F, fw, fh};
        const SDL_FRect dst{0.0F, 0.0F, fw, fh};
        SDL_RenderTexture(m_renderer, m_checker_pattern, &pattern_src, &dst);
        const SDL_FRect sprite_src{static_cast<float>(r.x), static_cast<float>(r.y), fw, fh};
        SDL_RenderTexture(m_renderer, page, &sprite_src, &dst);
    }
    SDL_SetTextureBlendMode(page, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(m_renderer, screen);
    if (!masked) {
        m_checker_failed = true;
        return false;
    }
    const SDL_FRect src{0.0F, 0.0F, static_cast<float>(r.width), static_cast<float>(height)};
    const SDL_FRect out{x0, y0, x1 - x0, y1 - y0};
    SDL_RenderTexture(m_renderer, m_checker, &src, &out);
    return true;
}

void SdlSpriteRenderer::flush(std::uint16_t page) {
    if (m_indices.empty()) {
        return;
    }
    SDL_Texture* texture = page == kUntextured ? nullptr : page == kHatched ? m_hatch : m_cache?m_cache->texture(page):m_pages[page];
    if (texture == nullptr) {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);  // untextured geometry uses it
    }
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::WatchPhase watch("world_geometry_submit");
#endif
    core::profile::Scope submit(core::profile::Submit);
    SDL_RenderGeometry(m_renderer, texture, m_vertices.data(), static_cast<int>(m_vertices.size()), m_indices.data(),
                       static_cast<int>(m_indices.size()));
    core::profile::add(core::profile::Bind,0);
    ++m_batches;
    m_vertices.clear();
    m_indices.clear();
}

std::size_t SdlSpriteRenderer::draw(const std::vector<DrawCommand>& commands, double origin_x, double origin_y,
                                    double scale) {
    m_batches = 0;
    if (m_atlas == nullptr || (!m_cache&&m_pages.empty())) {
        return 0;
    }
    m_error.clear();
    if(m_cache){SDL_Rect viewport{};SDL_GetRenderViewport(m_renderer,&viewport);const int w=viewport.w,h=viewport.h;
        if(!m_cache->prepare(*m_atlas,commands,origin_x,origin_y,scale,w,h)){m_error=m_cache->error();return 0;}}
    const auto inv_page = 1.0F / static_cast<float>(m_cache?2048:m_atlas->page_size());
    const SDL_FColor white{1.0F, 1.0F, 1.0F, 1.0F};
    const auto s = static_cast<float>(scale);
    // Screen position of a world pixel.
    const auto sx = [&](std::int32_t x) { return static_cast<float>(x * scale - origin_x); };
    const auto sy = [&](std::int32_t y) { return static_cast<float>(y * scale - origin_y); };
    SDL_Rect viewport{};SDL_GetRenderViewport(m_renderer,&viewport);
    std::size_t drawn = 0;
    std::uint16_t current = 0;
    m_vertices.clear();
    m_indices.clear();
    for (const DrawCommand& c : commands) {
        if (c.sprite == DrawCommand::kOverlayDiamond) {  // untextured translucent tile diamond
            if (current != kUntextured) {
                flush(current);
                current = kUntextured;
            }
            const assets::Rgba rgb = c.tint;
            const SDL_FColor colour{rgb.r / 255.0F, rgb.g / 255.0F, rgb.b / 255.0F, rgb.a / 255.0F};
            const float w = static_cast<float>(c.tint_size) * s;
            const float left = sx(c.x);
            const float bottom = sy(c.y);
            const float cy = bottom - w / 4.0F;
            const int base = static_cast<int>(m_vertices.size());
            m_vertices.push_back(SDL_Vertex{{left + w / 2.0F, bottom - w / 2.0F}, colour, {0, 0}});
            m_vertices.push_back(SDL_Vertex{{left + w, cy}, colour, {0, 0}});
            m_vertices.push_back(SDL_Vertex{{left + w / 2.0F, bottom}, colour, {0, 0}});
            m_vertices.push_back(SDL_Vertex{{left, cy}, colour, {0, 0}});
            m_indices.insert(m_indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            continue;
        }
        if (c.sprite == DrawCommand::kOverlayHatch) {  // every second pixel of the tile diamond
            if (m_hatch == nullptr) {
                continue;
            }
            if (current != kHatched) {
                flush(current);
                current = kHatched;
            }
            const SDL_FColor colour{c.tint.r / 255.0F, c.tint.g / 255.0F, c.tint.b / 255.0F, c.tint.a / 255.0F};
            const std::int32_t w = c.tint_size;
            const std::int32_t top = c.y - w / 2;
            // The texture's checkerboard starts a column later where the world's is the other way
            // round at the diamond's corner, so the pattern lines up from tile to tile.
            const std::int32_t shift = (c.x + top) & 1;
            const std::array<std::array<std::int32_t, 2>, 4> corners = {
                {{w / 2, 0}, {w, w / 4}, {w / 2, w / 2}, {0, w / 4}}};  // in the diamond's box
            const int base = static_cast<int>(m_vertices.size());
            for (const auto& [lx, ly] : corners) {
                m_vertices.push_back(
                    SDL_Vertex{{sx(c.x + lx), sy(top + ly)},
                               colour,
                               {static_cast<float>(lx + shift) / kHatchWidth, static_cast<float>(ly) / kHatchHeight}});
            }
            m_indices.insert(m_indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            continue;
        }
        if (c.sprite == DrawCommand::kOverlayOutline) {  // dashed tile edges, 2 x 1 pixel steps
            if (current != kUntextured) {
                flush(current);
                current = kUntextured;
            }
            const SDL_FColor colour{c.tint.r / 255.0F, c.tint.g / 255.0F, c.tint.b / 255.0F, 1.0F};
            for_each_outline_step(c, [&](std::int32_t x, std::int32_t y) {
                const float x0 = sx(c.x + x);
                const float y0 = sy(c.y + y);
                const int base = static_cast<int>(m_vertices.size());
                m_vertices.push_back(SDL_Vertex{{x0, y0}, colour, {0, 0}});
                m_vertices.push_back(SDL_Vertex{{x0 + 2.0F * s, y0}, colour, {0, 0}});
                m_vertices.push_back(SDL_Vertex{{x0 + 2.0F * s, y0 + s}, colour, {0, 0}});
                m_vertices.push_back(SDL_Vertex{{x0, y0 + s}, colour, {0, 0}});
                m_indices.insert(m_indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            });
            continue;
        }
        const auto& bounds=m_atlas->rect(c.sprite);
        if((c.x+bounds.width)*scale-origin_x<0||c.x*scale-origin_x>=viewport.w||
           c.y*scale-origin_y<0||(c.y-bounds.height)*scale-origin_y>=viewport.h)continue;
        const AtlasRect& r = m_cache?m_cache->rect(c.sprite):bounds;
        if (r.empty()) {
            continue;
        }
        if (r.page != current) {
            flush(current);
            current = r.page;
        }
        const float x0 = sx(c.x);
        const float y0 = sy(c.y - r.height);
        // Rows cut off at the bottom (figures in crops) are left out of the quad and the texture.
        const std::int32_t height = r.height - std::min<std::int32_t>(c.cut_bottom, r.height);
        if (height <= 0) {
            continue;
        }
        const float x1 = x0 + static_cast<float>(r.width) * s;
        const float y1 = y0 + static_cast<float>(height) * s;
        const float u0 = static_cast<float>(r.x) * inv_page;
        const float v0 = static_cast<float>(r.y) * inv_page;
        const float u1 = static_cast<float>(r.x + r.width) * inv_page;
        const float v1 = static_cast<float>(r.y + height) * inv_page;
        // Shadows: the texture colour multiplied by the (black, translucent) vertex colour;
        // translucent sprites: white with the opacity as alpha; dimmed ones: a grey vertex colour.
        SDL_FColor colour = white;
        if (c.mode == DrawCommand::Mode::Shadow) {
            colour = SDL_FColor{c.tint.r / 255.0F, c.tint.g / 255.0F, c.tint.b / 255.0F, c.tint.a / 255.0F};
        } else if (c.mode == DrawCommand::Mode::Translucent) {
            colour.a = c.tint.a / 255.0F;
        } else if (c.mode == DrawCommand::Mode::Dimmed) {
            colour = SDL_FColor{c.tint.r / 255.0F, c.tint.g / 255.0F, c.tint.b / 255.0F, 1.0F};
        } else if (c.mode == DrawCommand::Mode::Tinted) {
            colour = SDL_FColor{c.tint.r / 255.0F, c.tint.g / 255.0F, c.tint.b / 255.0F, c.tint.a / 255.0F};
        }
        const int base = static_cast<int>(m_vertices.size());
        m_vertices.push_back(SDL_Vertex{{x0, y0}, colour, {u0, v0}});
        m_vertices.push_back(SDL_Vertex{{x1, y0}, colour, {u1, v0}});
        m_vertices.push_back(SDL_Vertex{{x1, y1}, colour, {u1, v1}});
        m_vertices.push_back(SDL_Vertex{{x0, y1}, colour, {u0, v1}});
        m_indices.insert(m_indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        ++drawn;
        if (c.mode == DrawCommand::Mode::Checkered) {
            flush(current);
            if (!draw_checker(c, height, x0, y0, x1, y1)) {
                // Without render targets: the sprite once more, tinted half way to the colour.
                const SDL_FColor half{(1.0F + c.tint.r / 255.0F) / 2.0F, (1.0F + c.tint.g / 255.0F) / 2.0F,
                                      (1.0F + c.tint.b / 255.0F) / 2.0F, 0.5F};
                const int again = static_cast<int>(m_vertices.size());
                m_vertices.push_back(SDL_Vertex{{x0, y0}, half, {u0, v0}});
                m_vertices.push_back(SDL_Vertex{{x1, y0}, half, {u1, v0}});
                m_vertices.push_back(SDL_Vertex{{x1, y1}, half, {u1, v1}});
                m_vertices.push_back(SDL_Vertex{{x0, y1}, half, {u0, v1}});
                m_indices.insert(m_indices.end(), {again, again + 1, again + 2, again, again + 2, again + 3});
            }
        }
    }
    flush(current);
    return drawn;
}

}  // namespace o1602::render
