// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws DrawCommands with an SDL_Renderer: atlas pages become textures, sprites are batched into
// one SDL_RenderGeometry call per run of consecutive sprites on the same page (painter's order
// is preserved). Nearest filtering keeps the pixel art sharp at integer pixel scales.
#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include "o1602/render/paged_sprite_cache.hpp"

#include "o1602/core/result.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/render/sprite_atlas.hpp"

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Vertex;

namespace o1602::render {

class SdlSpriteRenderer {
public:
    /// Its textures are `renderer`'s: it goes before the renderer does.
    explicit SdlSpriteRenderer(SDL_Renderer* renderer);
    ~SdlSpriteRenderer();
    SdlSpriteRenderer(const SdlSpriteRenderer&) = delete;
    SdlSpriteRenderer& operator=(const SdlSpriteRenderer&) = delete;

    /// Uploads the atlas pages (replacing previous ones). The atlas must outlive draw calls.
    core::Status upload(const SpriteAtlas& atlas);

    /// Draws at screen = (x, y) * scale - origin; returns the number of sprites drawn. A scale that
    /// is no whole number gives uneven pixels (render::RenderPlan avoids it for the map).
    std::size_t draw(const std::vector<DrawCommand>& commands, double origin_x, double origin_y, double scale);
    /// Draw calls (SDL_RenderGeometry) of the last draw (FB-7): a new one each time the atlas page
    /// changes between commands.
    [[nodiscard]] std::size_t batches() const noexcept { return m_batches; }

    [[nodiscard]] const std::string& error() const noexcept { return m_error; }
    [[nodiscard]] const std::shared_ptr<PagedSpriteCache>& cache() const noexcept { return m_cache; }
private:
    std::shared_ptr<PagedSpriteCache> m_cache;
    std::string m_error;
    static constexpr std::uint16_t kUntextured = 0xFFFF;  ///< batch of overlay diamonds
    static constexpr std::uint16_t kHatched = 0xFFFE;     ///< batch of hatched diamonds (m_hatch)

    void release();
    void flush(std::uint16_t page);
    /// The checker of a DrawCommand::Mode::Checkered sprite, over the sprite already drawn:
    /// built in m_checker from m_checker_pattern masked by the sprite's shape, then drawn at
    /// (x0, y0) - (x1, y1). False where the renderer cannot (no render targets or blend mode).
    bool draw_checker(const DrawCommand& c, std::int32_t height, float x0, float y0, float x1, float y1);

    SDL_Renderer* m_renderer;
    const SpriteAtlas* m_atlas = nullptr;
    std::vector<SDL_Texture*> m_pages;
    /// Checkerboard, white and clear, one tile diamond wide (and a column more to shift the
    /// pattern by a pixel), tinted by hatched diamonds. Not repeated: SDL 3.2 has no wrap mode.
    static constexpr int kHatchWidth = 65;
    static constexpr int kHatchHeight = 32;
    SDL_Texture* m_hatch = nullptr;
    /// For Mode::Checkered: a white checkerboard as large as the largest sprite and a column more
    /// (hatch_pixel), and the render target the checker of one sprite is put together in.
    SDL_Texture* m_checker_pattern = nullptr;
    SDL_Texture* m_checker = nullptr;
    bool m_checker_failed = false;
    std::vector<SDL_Vertex> m_vertices;
    std::vector<int> m_indices;
    std::size_t m_batches = 0;
};

}  // namespace o1602::render
