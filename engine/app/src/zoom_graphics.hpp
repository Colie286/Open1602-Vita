// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "o1602/render/sdl_sprite_renderer.hpp"
#include "o1602/render/sprite_hit_mask.hpp"
#include "o1602/render/world_graphics.hpp"

namespace o1602::app {

/// Sprites of one zoom level: atlas (CPU side only rects remain) and its GPU textures.
struct ZoomGraphics {
    render::WorldGraphics graphics;
    std::unique_ptr<render::SdlSpriteRenderer> renderer;
    render::SpriteHitMask hit_mask;  ///< visible pixels of all sprites, for picking buildings and units
};

}  // namespace o1602::app
