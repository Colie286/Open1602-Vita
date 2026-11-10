// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/cart_figures.hpp"

#include <algorithm>
#include <cmath>

#include "o1602/render/land_figures.hpp"

namespace o1602::render {

namespace {

constexpr std::uint8_t kStandAnim = 1;
constexpr std::uint8_t kDieAnim = 2;

/// Wegspeed of the tile under world point (x, y) for the figure's Speedtyp, 32 = road.
std::uint32_t walk_speed_at(const sim::World& world, const assets::BuildingTable& buildings,
                            const assets::FigureType& type, double x, double y) {
    if (x < 0.0 || y < 0.0) {
        return 32;
    }
    const auto wx = static_cast<std::uint32_t>(x);
    const auto wy = static_cast<std::uint32_t>(y);
    if (!world.contains(wx, wy)) {
        return 32;
    }
    const std::uint16_t island = world.cell(wx, wy).island;
    if (island == sim::World::kSea || island >= world.islands().size()) {
        return 32;
    }
    const sim::WorldIsland& isl = world.islands()[island];
    const sim::MapCell& c = isl.map.top(wx - isl.x, wy - isl.y);
    const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
    return b != nullptr ? std::max<std::uint32_t>(1, b->walk_speed[std::min<std::uint8_t>(type.speed_type, 3)]) : 32U;
}

}  // namespace

void CartFigures::update(const sim::World& world, const assets::BuildingTable& buildings,
                         const assets::FigureTable& figures, const ShipTrail& trail, std::uint32_t dt_ms) {
    m_instances.clear();
    m_ids.clear();
    for (auto& [id, slot] : m_slots) {
        slot.seen = false;
    }
    for (const sim::Ship& ship : world.ships()) {
        const assets::FigureType* type = figures.get(ship.figure);
        if (!ship.cart() || type == nullptr || (ship.sinking && type->anim(kDieAnim) == nullptr)) {
            continue;
        }
        double x = 0.0;
        double y = 0.0;
        trail.position(ship, x, y);
        // TRADER1: ANIM 0 drives, ANIM 1 stands (one picture per direction), ANIM 2 is destroyed.
        const bool driving = ship.motion.phase == sim::ShipMotion::Phase::Run;
        const std::uint8_t anim = ship.sinking                                    ? kDieAnim
                                  : !driving && type->anim(kStandAnim) != nullptr ? kStandAnim
                                                                                  : 0;
        auto [it, added] = m_slots.try_emplace(ship.id);
        Slot& slot = it->second;
        if (added || slot.requested != anim) {
            slot.state = start_animation(*type, anim, ship.id * 2654435761U);
            slot.requested = anim;
        } else if (ship.sinking) {
            advance_animation(slot.state, *type, dt_ms);
        } else if (driving) {
            // A frame lasts AnimSpeed * Wegspeed / 32 ms while driving, as the market carts'.
            advance_animation(slot.state, *type,
                              walking_pace(slot.pace_rest, dt_ms, walk_speed_at(world, buildings, *type, x, y)));
        }
        slot.seen = true;
        // Hidden on the tiles of a building (the Kontor it leaves, the market it stands at).
        if (!walker_visible_at(world, buildings, x, y)) {
            continue;
        }
        FigureInstance f;
        f.x = x;
        f.y = y;
        f.figure = ship.figure;
        // The original draws the cart from TRAEGER.BSH by its kind; TRADER1 names no Blocknr.
        f.sheet = assets::FigureSheet::Traeger;
        f.z = std::max(kLandHeight, spawn_height(world, buildings, x, y));
        f.direction = ship.direction;
        f.anim = slot.state.anim;
        f.frame = slot.state.frame;
        const auto wx = static_cast<std::uint32_t>(std::max(0.0, x));
        const auto wy = static_cast<std::uint32_t>(std::max(0.0, y));
        if (world.contains(wx, wy)) {
            f.island = world.cell(wx, wy).island;
        }
        m_instances.push_back(f);
        m_ids.push_back(ship.id);
    }
    std::erase_if(m_slots, [](const auto& entry) { return !entry.second.seen; });
}

const FigureInstance* CartFigures::find(std::uint32_t id) const noexcept {
    for (std::size_t i = 0; i < m_ids.size(); ++i) {
        if (m_ids[i] == id) {
            return &m_instances[i];
        }
    }
    return nullptr;
}

}  // namespace o1602::render
