// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/island_map.hpp"

#include <algorithm>
#include <atomic>

#include "o1602/sim/game_data.hpp"

namespace o1602::sim {

using core::ErrorCode;
using core::make_error;

IslandMap::IslandMap(std::uint32_t width, std::uint32_t height)
    : m_width(width),
      m_height(height),
      m_top(std::size_t{width} * height),
      m_ground(std::size_t{width} * height),
      m_revisions(std::size_t{blocks_x()} * blocks_y()) {
    touch_all();
}

void IslandMap::touch_all() {
    for (auto& r : m_revisions) {
        r = next_revision();
    }
}

std::uint64_t IslandMap::next_revision() noexcept {
    // Shared by all maps, so that a revision never stands for two different contents; from any
    // thread (maps of other worlds are built on loader threads).
    static std::atomic<std::uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

namespace {

/// City of an island tile: only tiles with an owner belong to one.
std::uint8_t city_of(const assets::IslandTile& tile) {
    return player_from_original(tile.player()) == kNoPlayer ? MapCell::kNoCity : tile.city();
}

}  // namespace

core::Status IslandMap::place(const assets::BuildingType& b, std::int32_t x, std::int32_t y, std::uint8_t rotation,
                              std::uint8_t anim_phase, PlayerId owner, std::uint8_t city) {
    return write(b, x, y, rotation, anim_phase, owner, city, Mode::Place);
}

core::Status IslandMap::build(const assets::BuildingType& b, std::int32_t x, std::int32_t y, std::uint8_t rotation,
                              std::uint8_t anim_phase, PlayerId owner, std::uint8_t city) {
    return write(b, x, y, rotation, anim_phase, owner, city, Mode::Build);
}

core::Status IslandMap::load(const assets::BuildingType& b, std::int32_t x, std::int32_t y, std::uint8_t rotation,
                             std::uint8_t anim_phase, PlayerId owner, std::uint8_t city, bool to_ground,
                             std::uint8_t random) {
    return write(b, x, y, rotation, anim_phase, owner, city, to_ground ? Mode::LoadGround : Mode::Load, random);
}

core::Status IslandMap::replace(const assets::BuildingType& b, std::int32_t x, std::int32_t y, std::uint8_t rotation,
                                std::uint8_t anim_phase) {
    return write(b, x, y, rotation, anim_phase, kNoPlayer, MapCell::kNoCity, Mode::Replace);
}

core::Status IslandMap::write(const assets::BuildingType& b, std::int32_t x, std::int32_t y, std::uint8_t rotation,
                              std::uint8_t anim_phase, PlayerId owner, std::uint8_t city, Mode mode,
                              std::uint8_t random) {
    rotation &= 3U;
    std::uint32_t fw = 0;
    std::uint32_t fh = 0;
    rotated_size(b, rotation, fw, fh);
    if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) + fw > m_width ||
        static_cast<std::uint32_t>(y) + fh > m_height) {
        return make_error(ErrorCode::OutOfRange, "building " + std::to_string(b.id) + " does not fit at " +
                                                     std::to_string(x) + "," + std::to_string(y));
    }

    const auto w = static_cast<std::int32_t>(b.size_x);
    const auto h = static_cast<std::int32_t>(b.size_y);
    std::uint32_t sprite = b.gfx;
    const auto put = [&](std::int32_t tx, std::int32_t ty) {
        const std::size_t i = index(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
        PlayerId o = owner;
        std::uint8_t c = city;
        if (mode == Mode::Replace) {
            o = m_top[i].owner;  // a house changing class keeps its owner and city
            c = m_top[i].city;
        } else if (mode == Mode::Place && !m_top[i].empty()) {
            m_ground[i] = m_top[i];
        }
        const auto phase = static_cast<std::uint8_t>(anim_phase & 0xFU);
        const auto bits = static_cast<std::uint8_t>(random & 0x1FU);
        m_top[i] = MapCell{static_cast<std::uint16_t>(sprite), rotation, phase, o, c, bits};
        if (mode == Mode::LoadGround) {
            m_ground[i] = m_top[i];
        }
        ++sprite;
    };

    // Traversal order per rotation as in the original placement routine (0x00463B10).
    switch (rotation) {
        case 0:
            for (std::int32_t j = 0; j < h; ++j) {
                for (std::int32_t i = 0; i < w; ++i) {
                    put(x + i, y + j);
                }
            }
            break;
        case 1:
            for (std::int32_t j = h - 1; j >= 0; --j) {
                for (std::int32_t i = 0; i < w; ++i) {
                    put(x + j, y + i);
                }
            }
            break;
        case 2:
            for (std::int32_t j = h - 1; j >= 0; --j) {
                for (std::int32_t i = w - 1; i >= 0; --i) {
                    put(x + i, y + j);
                }
            }
            break;
        default:
            for (std::int32_t j = 0; j < h; ++j) {
                for (std::int32_t i = w - 1; i >= 0; --i) {
                    put(x + j, y + i);
                }
            }
            break;
    }
    // Every block the footprint covers (fw, fh >= 1: the building fit).
    const auto left = static_cast<std::uint32_t>(x);
    const auto top = static_cast<std::uint32_t>(y);
    for (std::uint32_t by = top / kBlockSide; fh > 0 && by <= (top + fh - 1) / kBlockSide; ++by) {
        for (std::uint32_t bx = left / kBlockSide; fw > 0 && bx <= (left + fw - 1) / kBlockSide; ++bx) {
            m_revisions[std::size_t{by} * blocks_x() + bx] = next_revision();
        }
    }
    return core::Ok{};
}

SubTile IslandMap::sub_tile(const assets::BuildingTable& buildings, std::uint32_t x, std::uint32_t y) const {
    return sub_tile_of(top(x, y), buildings, x, y);
}

SubTile IslandMap::ground_sub_tile(const assets::BuildingTable& buildings, std::uint32_t x, std::uint32_t y) const {
    return sub_tile_of(ground(x, y), buildings, x, y);
}

SubTile IslandMap::sub_tile_of(const MapCell& cell, const assets::BuildingTable& buildings, std::uint32_t x,
                               std::uint32_t y) {
    SubTile result;
    if (cell.empty()) {
        return result;
    }
    result.building = buildings.by_sprite(cell.sprite);
    if (result.building == nullptr) {
        return result;
    }
    const auto [ox, oy] = origin_of(cell, *result.building, x, y);
    result.origin_x = ox;
    result.origin_y = oy;
    return result;
}

IslandMap IslandMap::from_island(const assets::Island& island, const assets::BuildingTable& buildings,
                                 std::vector<std::string>* warnings, const assets::Island* base) {
    IslandMap map(island.header.width, island.header.height);
    const auto warn = [&](std::string message) {
        if (warnings != nullptr) {
            warnings->push_back(std::move(message));
        }
    };
    if (base != nullptr && (base->header.width != island.header.width || base->header.height != island.header.height)) {
        warn("template size " + std::to_string(base->header.width) + "x" + std::to_string(base->header.height) +
             " does not match island size (template ignored)");
        base = nullptr;
    }
    std::vector<const std::vector<assets::IslandTile>*> layers;
    if (base != nullptr) {
        for (const auto& layer : base->layers) {
            layers.push_back(&layer);
        }
    }
    for (const auto& layer : island.layers) {
        layers.push_back(&layer);
    }
    for (const auto* layer : layers) {
        for (const auto& tile : *layer) {
            const assets::BuildingType* b = buildings.by_id(tile.cod_id());
            if (b == nullptr) {
                warn("unknown building id " + std::to_string(tile.cod_id()) + " at " + std::to_string(tile.x) + "," +
                     std::to_string(tile.y));
                continue;
            }
            // The original's loader (0x00468550): every tile goes on top; terrain of no city also
            // becomes the ground (not ruins, 0x00463E10).
            constexpr std::uint8_t kNoCityBits = 7;
            const bool ground = tile.city() == kNoCityBits && b->kind != kind::kRuin && b->kind != kind::kBeachRuin &&
                                !territory_building(buildings, *b);
            if (auto status = map.load(*b, tile.x, tile.y, tile.rotation(), tile.anim_phase(),
                                       player_from_original(tile.player()), city_of(tile), ground, tile.random());
                !status) {
                warn(status.error().message);
            }
        }
    }
    return map;
}

bool territory_building(const assets::BuildingTable& buildings, const assets::BuildingType& b) noexcept {
    // HAUS_PRODTYP Kind (0x00480B70 -> 0x00480B40 -> 0x00480AF0).
    const auto crop = [](const assets::BuildingType* t) {
        return t != nullptr && t->ware != ware::kGrass && t->ware != ware::kTree && t->ware != ware::kFish;
    };
    const std::int32_t p = b.prod_kind;
    switch (p) {
        case 0:
            return b.kind == kind::kRoad || b.kind == kind::kPier;
        case prod::kRipe:  // a ripe field, unless grass, a tree or fish
            return crop(&b);
        case prod::kGrowing:  // by the ripe record that follows
            return crop(buildings.by_record(b.record + 1));
        case prod::kTower:
        case prod::kWall:
            return true;
        default:
            break;
    }
    // Production entries, houses, shipyards, military, public buildings, HAUPT and pirate houses.
    return (p >= prod::kWorkshop && p <= prod::kKontor) || (p >= prod::kHouse && p <= prod::kMilitary) ||
           (p >= prod::kPublicFirst && p <= prod::kPublicLast) || p == prod::kHaupt || p == prod::kPirateHouse;
}

}  // namespace o1602::sim
