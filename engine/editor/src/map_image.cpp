// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/map_image.hpp"

#include <algorithm>
#include <array>

#include "o1602/editor/fairness.hpp"
#include "o1602/sim/new_game.hpp"

namespace o1602::editor {

namespace {

/// The colours of sim::WorldPreview's tiles, as the new game's preview shows them.
constexpr std::array<std::array<std::uint8_t, 3>, sim::WorldPreview::kTiles> kColours = {{
    {30, 60, 110},    // sea
    {60, 105, 160},   // surf
    {220, 200, 140},  // beach
    {125, 150, 70},   // ground
    {45, 95, 40},     // forest
    {125, 105, 85},   // mountains
    {190, 60, 40},    // villages
}};

}  // namespace

MapImage map_image(const sim::World& world, const assets::BuildingTable& buildings, std::uint32_t scale) {
    scale = std::clamp<std::uint32_t>(scale, 1, 8);
    const sim::WorldPreview preview = sim::preview_of(world, buildings);
    MapImage image;
    image.width = preview.width * scale;
    image.height = preview.height * scale;
    image.rgba.assign(std::size_t{image.width} * image.height * 4, 255);
    const auto put = [&](std::uint32_t x, std::uint32_t y, const std::array<std::uint8_t, 3>& c) {
        if (x >= image.width || y >= image.height) {
            return;
        }
        std::uint8_t* p = &image.rgba[(std::size_t{y} * image.width + x) * 4];
        p[0] = c[0];
        p[1] = c[1];
        p[2] = c[2];
        p[3] = 255;
    };
    for (std::uint32_t y = 0; y < preview.height; ++y) {
        for (std::uint32_t x = 0; x < preview.width; ++x) {
            const auto& c = kColours[static_cast<std::size_t>(preview.at(x, y))];
            for (std::uint32_t sy = 0; sy < scale; ++sy) {
                for (std::uint32_t sx = 0; sx < scale; ++sx) {
                    put(x * scale + sx, y * scale + sy, c);
                }
            }
        }
    }
    // The starts: a white square with a dark rim, five tiles across.
    for (const StartArea& a : start_areas(world, buildings)) {
        const std::int64_t half = std::int64_t{scale} * 5 / 2;
        const std::int64_t cx = std::int64_t{a.x} * scale + scale / 2;
        const std::int64_t cy = std::int64_t{a.y} * scale + scale / 2;
        for (std::int64_t dy = -half; dy <= half; ++dy) {
            for (std::int64_t dx = -half; dx <= half; ++dx) {
                const std::int64_t px = cx + dx;
                const std::int64_t py = cy + dy;
                if (px < 0 || py < 0) {
                    continue;
                }
                const bool rim = dx == -half || dx == half || dy == -half || dy == half;
                put(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py),
                    rim ? std::array<std::uint8_t, 3>{20, 20, 20} : std::array<std::uint8_t, 3>{255, 255, 255});
            }
        }
    }
    return image;
}

}  // namespace o1602::editor
