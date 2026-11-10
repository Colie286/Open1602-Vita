// SPDX-License-Identifier: GPL-3.0-or-later
#include "catchment_view.hpp"

#include "o1602/sim/production.hpp"

namespace o1602::app {

namespace {

/// The building of `type` and `player` with its origin at (x, y) of `map`, if there is one: its
/// rotation.
[[nodiscard]] std::optional<std::uint8_t> origin_of(const sim::IslandMap& map, const assets::BuildingTable& buildings,
                                                    const assets::BuildingType& type, sim::PlayerId player,
                                                    std::uint32_t x, std::uint32_t y) {
    if (!map.contains(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y))) {
        return std::nullopt;
    }
    const sim::MapCell& cell = map.top(x, y);
    if (cell.empty() || cell.owner != player) {
        return std::nullopt;
    }
    const sim::SubTile t = map.sub_tile(buildings, x, y);
    if (t.building == nullptr || t.building->id != type.id || t.origin_x != static_cast<std::int32_t>(x) ||
        t.origin_y != static_cast<std::int32_t>(y)) {
        return std::nullopt;
    }
    return cell.rotation;
}

}  // namespace

bool shows_catchment(const assets::BuildingType& type) noexcept {
    return type.radius > 0 && type.prod_kind != sim::prod::kHouse;
}

std::optional<CatchmentCircles> catchment_circles(const sim::World& world, const assets::BuildingTable& buildings,
                                                  sim::PlayerId player, std::uint16_t island, std::uint16_t x,
                                                  std::uint16_t y, bool same_type) {
    if (island >= world.islands().size()) {
        return std::nullopt;
    }
    const sim::IslandMap& map = world.islands()[island].map;
    if (!map.contains(x, y)) {
        return std::nullopt;
    }
    const sim::MapCell& cell = map.top(x, y);
    const sim::SubTile t = map.sub_tile(buildings, x, y);
    if (t.building == nullptr || cell.owner != player || !shows_catchment(*t.building)) {
        return std::nullopt;
    }
    const assets::BuildingType& type = *t.building;
    CatchmentCircles out;
    out.type = &type;
    out.circles.push_back(sim::catchment_of(type, cell.rotation, sim::TilePos{x, y}));
    out.origins.push_back(sim::TilePos{x, y});
    if (!same_type) {
        return out;
    }
    const auto add = [&](std::uint32_t ox, std::uint32_t oy) {
        if (ox == x && oy == y) {
            return;  // the clicked one, already first
        }
        if (const auto rotation = origin_of(map, buildings, type, player, ox, oy)) {
            const sim::TilePos origin{static_cast<std::int32_t>(ox), static_cast<std::int32_t>(oy)};
            out.circles.push_back(sim::catchment_of(type, *rotation, origin));
            out.origins.push_back(origin);
        }
    };
    if (world.production_index(island, x, y)) {
        // Production, public buildings, market places and Kontors are listed: no need to look at
        // every tile of the island.
        for (const sim::Production& p : world.productions()) {
            if (p.used && p.island == island) {
                add(p.x, p.y);
            }
        }
    } else {
        // Towers and the like: the island tile by tile.
        for (std::uint32_t oy = 0; oy < map.height(); ++oy) {
            for (std::uint32_t ox = 0; ox < map.width(); ++ox) {
                const sim::MapCell& c = map.top(ox, oy);
                if (!c.empty() && c.owner == player && buildings.by_sprite(c.sprite) == &type) {
                    add(ox, oy);
                }
            }
        }
    }
    return out;
}

}  // namespace o1602::app
