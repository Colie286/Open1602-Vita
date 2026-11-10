// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which pixels of a sprite are visible, one bit per pixel, kept after the atlas pixels went to the
// GPU (SpriteAtlas::drop_pixels). Buildings and units are picked by the visible pixel under the
// mouse, the one drawn last winning, as in the original (Map_PickAtScreen 0x00487940 reads an ID per
// screen pixel that the renderer writes with the figure).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "o1602/render/island_renderer.hpp"
#include "o1602/render/sprite_atlas.hpp"

namespace o1602::render {

class SpriteHitMask {
public:
    SpriteHitMask() = default;

    /// Masks of sprites [first, atlas.sprite_count()); the game keeps all (first = 0): buildings
    /// are picked by their pixels as units are. Needs the atlas pixels.
    static SpriteHitMask build(const SpriteAtlas& atlas, std::uint32_t first, GraphicsProgress* progress = nullptr);

    /// Whether pixel (x, y) of the sprite (from its top left corner) is visible. Outside the sprite:
    /// false; sprites without a mask count as visible in their whole rectangle.
    [[nodiscard]] bool visible(std::uint32_t sprite, std::int32_t x, std::int32_t y) const noexcept;

    [[nodiscard]] std::size_t bytes() const noexcept { return m_bits.size() * sizeof(std::uint64_t); }

private:
    struct Entry {
        std::uint64_t bit = 0;  ///< first bit of the sprite in m_bits
        std::uint16_t width = 0;
        std::uint16_t height = 0;
    };
    std::uint32_t m_first = 0;
    std::vector<Entry> m_entries;
    std::vector<std::uint64_t> m_bits;
};

/// Screen-independent rectangle of a draw command in command coordinates: left, top, width,
/// height (cut rows left out); nothing for an empty sprite or an overlay.
[[nodiscard]] std::optional<std::array<std::int32_t, 4>> command_bounds(const DrawCommand& command,
                                                                        const SpriteAtlas& atlas);

/// Whether point (x, y) in command coordinates hits a visible pixel of the command's sprite.
[[nodiscard]] bool command_hit(const DrawCommand& command, const SpriteAtlas& atlas, const SpriteHitMask& mask,
                               std::int32_t x, std::int32_t y);

}  // namespace o1602::render
