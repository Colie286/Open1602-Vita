// SPDX-License-Identifier: GPL-3.0-or-later
#include "fertility_view.hpp"

#include "o1602/sim/construction.hpp"
#include "o1602/sim/growth.hpp"

namespace o1602::app {

namespace {

[[nodiscard]] bool checked_crop(std::uint8_t ware) noexcept {
    return ware >= assets::ware::kCornField && ware <= assets::ware::kCocoaField;
}

}  // namespace

std::uint8_t fertility_crop(const assets::BuildingTable& buildings, const assets::BuildingType& type) {
    const std::uint8_t own = sim::crop_ware(buildings, type);
    if (checked_crop(own)) {
        return own;
    }
    return own == 0 && type.radius > 0 && checked_crop(type.raw_ware) ? type.raw_ware : std::uint8_t{0};
}

void fertility_marks(const sim::World& world, const assets::BuildingTable& buildings, sim::PlayerId player,
                     std::uint16_t island, std::uint8_t crop, std::vector<render::TileMark>& out) {
    if (island >= world.islands().size() || !checked_crop(crop)) {
        return;
    }
    const sim::IslandMap& map = world.islands()[island].map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const sim::MapCell& cell = map.top(x, y);
            if (cell.empty() || (cell.owner != player && cell.owner != sim::kNoPlayer)) {
                continue;
            }
            const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
            if (b == nullptr) {
                continue;
            }
            const bool field =
                checked_crop(sim::crop_ware(buildings, *b)) || sim::ripe_of_withered(buildings, *b) != nullptr;
            if (b->kind != assets::kind::kGround && !field) {
                continue;
            }
            const auto tx = static_cast<std::int32_t>(x);
            const auto ty = static_cast<std::int32_t>(y);
            out.push_back(render::TileMark{
                island, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                sim::tile_fertile_at(world, buildings, island, crop, tx, ty, 0) ? kFertileTint : kInfertileTint});
        }
    }
}

}  // namespace o1602::app
