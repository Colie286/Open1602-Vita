// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/island_renderer.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace o1602::render {

std::uint32_t building_sprite(const sim::MapCell& cell, const assets::BuildingType& building, std::uint32_t frame,
                              std::uint8_t view_rotation) {
    std::uint32_t sprite = cell.sprite;
    if (building.anim_count > 1) {
        sprite += building.anim_add * frame;
    }
    return sprite + ((static_cast<std::uint32_t>(cell.rotation) + view_rotation) & 3U) * building.rotate;
}

std::uint32_t default_frame(const sim::MapCell& cell, const assets::BuildingType& building,
                            std::uint32_t global_frame) {
    if (building.anim_count < 2) {
        return 0;
    }
    // HAUS_PRODTYP Kind ROHSTWACHS (10) without AnimTime keeps the tile phase; with AnimTime == 0
    // the global frame never advances, so the general formula gives the same picture.
    return (cell.anim_phase + global_frame) % building.anim_count;
}

bool has_production_animation(const assets::BuildingType& building) {
    return building.anim_count >= 2 && building.prod_kind >= static_cast<std::int32_t>(sim::ProductionKind::Handwerk) &&
           building.prod_kind <= static_cast<std::int32_t>(sim::ProductionKind::Markt);
}

std::uint32_t production_frame(const assets::BuildingType& building, std::uint32_t global_frame,
                               const sim::Production* production, std::optional<std::uint8_t> phase) {
    const std::uint32_t count = building.anim_count;
    if (count < 2) {
        return 0;
    }
    if (building.prod_kind == static_cast<std::int32_t>(sim::ProductionKind::Markt)) {  // 0x0047CCD0
        const std::uint32_t progress = production != nullptr ? production->produced : 0;
        return std::min(((count - 1) * progress + sim::Production::kProgressDone / 2) / sim::Production::kProgressDone,
                        count - 1);
    }
    if (building.stock_animation) {  // 0x0047CC80
        if (production == nullptr || building.max_stock == 0) {
            return 0;
        }
        // The original does not clamp; a store above Maxlager (not seen in savegames) would pick a
        // sprite of the next building type. Open1602 stays on the last frame.
        const std::uint32_t frame = ((count - 1) * production->stock + building.max_stock / 2) / building.max_stock;
        return std::min(frame, count - 1);
    }
    std::uint32_t frame = global_frame;  // 0x0047CD10: no state -> the global frame alone
    if (production != nullptr) {
        frame =
            phase.value_or(production->anim_phase) + (production->enabled && production->working() ? global_frame : 0);
    }
    return frame % count;
}

std::uint8_t phase_after_change(const assets::BuildingType& building, std::uint8_t phase, std::uint32_t global_frame,
                                bool working) {
    if (building.anim_count < 2) {
        return 0;
    }
    if (working) {
        return static_cast<std::uint8_t>((phase - global_frame) & 0x0FU);
    }
    return building.continuous_animation ? static_cast<std::uint8_t>((global_frame + phase) & 0x0FU) : 0;
}

std::uint32_t animated_sprite(const sim::MapCell& cell, const assets::BuildingType* building,
                              std::uint32_t global_frame, std::uint8_t view_rotation) {
    if (building == nullptr) {
        return cell.sprite;
    }
    return building_sprite(cell, *building, default_frame(cell, *building, global_frame), view_rotation);
}

std::uint32_t sprite_for_cell(const sim::MapCell& cell, const assets::BuildingType* building,
                              const RenderOptions& options) {
    const std::uint32_t frame = building == nullptr ? 0 : building->anim_frame + options.anim_step;
    return animated_sprite(cell, building, frame, options.view_rotation);
}

namespace {

struct ViewTransform {
    std::uint32_t width;  ///< island size in view space
    std::uint32_t height;
    std::uint8_t rotation;
    std::uint32_t map_width;
    std::uint32_t map_height;

    /// View space -> map coordinates.
    void to_map(std::uint32_t vx, std::uint32_t vy, std::uint32_t& mx, std::uint32_t& my) const {
        switch (rotation & 3U) {
            case 0:
                mx = vx;
                my = vy;
                break;
            case 1:
                mx = vy;
                my = map_height - 1 - vx;
                break;
            case 2:
                mx = map_width - 1 - vx;
                my = map_height - 1 - vy;
                break;
            default:
                mx = map_width - 1 - vy;
                my = vx;
                break;
        }
    }
};

}  // namespace

std::vector<DrawCommand> build_draw_list(const sim::IslandMap& map, const assets::BuildingTable& buildings,
                                         const RenderOptions& options) {
    const std::int32_t tw = tile_width(options.zoom);
    const std::int32_t th = tile_height(options.zoom);
    const bool swap = (options.view_rotation & 1U) != 0;
    const ViewTransform view{swap ? map.height() : map.width(), swap ? map.width() : map.height(),
                             options.view_rotation, map.width(), map.height()};

    std::vector<DrawCommand> commands;
    commands.reserve(std::size_t{map.width()} * map.height() * 5 / 4);

    const auto emit = [&](const sim::MapCell& cell, std::int32_t sx, std::int32_t row_bottom) {
        const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
        const std::int32_t elevation = b == nullptr ? 0 : b->posoffs * tw / 64;
        commands.push_back(DrawCommand{sprite_for_cell(cell, b, options), sx, row_bottom - elevation});
    };

    const std::uint32_t rows = view.width + view.height - 1;
    for (std::uint32_t d = 0; d < rows; ++d) {
        const std::uint32_t x_begin = d >= view.height ? d - view.height + 1 : 0;
        const std::uint32_t x_end = std::min(d, view.width - 1);
        for (int pass = 0; pass < 2; ++pass) {
            const bool high_pass = pass == 1;
            for (std::uint32_t vx = x_begin; vx <= x_end; ++vx) {
                const std::uint32_t vy = d - vx;
                std::uint32_t mx = 0;
                std::uint32_t my = 0;
                view.to_map(vx, vy, mx, my);
                const sim::MapCell& cell = map.top(mx, my);
                if (cell.empty()) {
                    continue;
                }
                const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
                if ((b != nullptr && b->high) != high_pass) {
                    continue;
                }
                const std::int32_t sx = (static_cast<std::int32_t>(vx) - static_cast<std::int32_t>(vy)) * (tw / 2);
                const std::int32_t row_bottom = static_cast<std::int32_t>(d) * (th / 2) + th - 1;
                if (b != nullptr && b->draw_ground && !map.ground(mx, my).empty()) {
                    emit(map.ground(mx, my), sx, row_bottom);
                }
                emit(cell, sx, row_bottom);
            }
        }
    }
    return commands;
}

RgbaImage rasterize(const std::vector<DrawCommand>& commands, const assets::SpriteSheet& sheet,
                    const assets::Palette& palette, assets::Rgba background, std::size_t* missing_sprites) {
    std::unordered_map<std::uint32_t, assets::IndexedImage> cache;
    std::size_t missing = 0;
    const auto sprite = [&](std::uint32_t index) -> const assets::IndexedImage* {
        if (index >= sheet.size()) {
            return nullptr;
        }
        auto it = cache.find(index);
        if (it == cache.end()) {
            auto decoded = sheet.decode(index);
            if (!decoded) {
                return nullptr;
            }
            it = cache.emplace(index, std::move(decoded).value()).first;
        }
        return &it->second;
    };

    // Bounding box of all sprites.
    std::int32_t min_x = std::numeric_limits<std::int32_t>::max();
    std::int32_t min_y = min_x;
    std::int32_t max_x = std::numeric_limits<std::int32_t>::min();
    std::int32_t max_y = max_x;
    for (const DrawCommand& c : commands) {
        const assets::IndexedImage* img = sprite(c.sprite);
        if (img == nullptr) {
            ++missing;
            continue;
        }
        min_x = std::min(min_x, c.x);
        max_x = std::max(max_x, c.x + static_cast<std::int32_t>(img->width));
        min_y = std::min(min_y, c.y - static_cast<std::int32_t>(img->height));
        max_y = std::max(max_y, c.y);
    }
    if (missing_sprites != nullptr) {
        *missing_sprites = missing;
    }

    RgbaImage out;
    if (max_x <= min_x || max_y <= min_y) {
        return out;
    }
    out.width = static_cast<std::uint32_t>(max_x - min_x);
    out.height = static_cast<std::uint32_t>(max_y - min_y);
    out.pixels.resize(std::size_t{out.width} * out.height * 4);
    for (std::size_t i = 0; i < out.pixels.size(); i += 4) {
        out.pixels[i] = background.r;
        out.pixels[i + 1] = background.g;
        out.pixels[i + 2] = background.b;
        out.pixels[i + 3] = background.a;
    }

    for (const DrawCommand& c : commands) {
        const assets::IndexedImage* img = sprite(c.sprite);
        if (img == nullptr) {
            continue;
        }
        const std::int32_t left = c.x - min_x;
        const std::int32_t top = c.y - static_cast<std::int32_t>(img->height) - min_y;
        for (std::uint32_t y = 0; y < img->height; ++y) {
            const std::size_t dst_row = std::size_t(top + static_cast<std::int32_t>(y)) * out.width;
            for (std::uint32_t x = 0; x < img->width; ++x) {
                const std::size_t src = std::size_t{y} * img->width + x;
                if (img->opaque[src] == 0) {
                    continue;
                }
                const assets::Rgba& colour = palette[img->indices[src]];
                const std::size_t dst = (dst_row + std::size_t(left + static_cast<std::int32_t>(x))) * 4;
                out.pixels[dst] = colour.r;
                out.pixels[dst + 1] = colour.g;
                out.pixels[dst + 2] = colour.b;
                out.pixels[dst + 3] = 255;
            }
        }
    }
    return out;
}

}  // namespace o1602::render
