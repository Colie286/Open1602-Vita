// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/walker_figures.hpp"

#include <algorithm>

#include "o1602/render/land_figures.hpp"

namespace o1602::render {

namespace {

std::uint64_t trail_key(sim::Handle h) noexcept {
    return std::uint64_t{h.index} << 32U | h.generation;
}

/// Figure kinds 8, 9, 0x0A, 0x0B and 0x0F, which 0x0044FA90 draws only where 0x00451590 allows.
/// Workers, hunters, animals and fishing boats follow their own rules (crop_cut).
bool hidden_in_buildings(sim::WalkerKind kind) noexcept {
    switch (kind) {
        case sim::WalkerKind::Carrier:
        case sim::WalkerKind::Cart:
        case sim::WalkerKind::Doctor:
        case sim::WalkerKind::Firefighter:
        case sim::WalkerKind::Robber:
            return true;
        default:
            return false;
    }
}

}  // namespace

void WalkerFigures::capture(const sim::Logistics& logistics, const sim::World& world) {
    m_trail.capture([&](const auto& add) {
        logistics.walkers().for_each([&](sim::Handle h, const sim::Walker& w) {
            if (w.island < world.islands().size()) {
                const sim::WorldIsland& island = world.islands()[w.island];
                const sim::WalkerPosition p = sim::walker_position(w);
                add(trail_key(h), island.x + p.x, island.y + p.y);
            }
        });
    });
}

void WalkerFigures::update(const sim::Logistics& logistics, const sim::World& world,
                           const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                           std::uint32_t dt_ms, double fraction) {
    m_instances.clear();
    m_trail.set_fraction(fraction);
    for (Slot& s : m_slots) {
        s.used = false;
    }
    logistics.walkers().for_each([&](sim::Handle h, const sim::Walker& w) {
        const assets::FigureType* type = figures.get(w.figure);
        if (type == nullptr || w.island >= world.islands().size()) {
            return;
        }
        if (w.kind == sim::WalkerKind::Robber && w.working() && w.animation == 1) {
            return;  // waits unseen in his house (0x00451590)
        }
        if (h.index >= m_slots.size()) {
            m_slots.resize(std::size_t{h.index} + 1);
        }
        Slot& slot = m_slots[h.index];
        const std::uint8_t anim = type->anim(w.anim()) != nullptr ? w.anim() : 0;
        if (!slot.active || slot.generation != h.generation || slot.requested != anim) {
            const bool change = slot.active && slot.generation == h.generation;
            slot.generation = h.generation;
            slot.requested = anim;
            slot.state = start_animation(*type, anim, h.index * 2654435761U + h.generation);
            if (!change) {
                // The height of the tile it was made on, kept while it walks (0x00446CA0 callers):
                // fishers on the water, carriers never below land (0x0044AC28). Carts have no such
                // floor in the original and walk 17 px low from a fisher's hut (U-010); here they
                // get the carriers' floor.
                const sim::WalkerPosition p = sim::walker_position(w);
                const sim::WorldIsland& isl = world.islands()[w.island];
                slot.z =
                    w.kind == sim::WalkerKind::Fisher ? 0.0 : spawn_height(world, buildings, isl.x + p.x, isl.y + p.y);
                if (w.kind == sim::WalkerKind::Carrier || w.kind == sim::WalkerKind::Cart) {
                    slot.z = std::max(slot.z, kLandHeight);
                }
            }
            if (change) {
                // A new animation starts from its first frame (0x00446D90): a shot deer falls.
                slot.state.frame = 0;
                slot.state.elapsed_ms = 0;
            }
        } else if (w.moving()) {
            // A frame lasts AnimSpeed * Wegspeed / 32 ms while walking.
            advance_animation(slot.state, *type,
                              walking_pace(slot.pace_rest, dt_ms, std::max<std::uint32_t>(w.walk_speed, 1)));
        } else if (w.working()) {
            advance_animation(slot.state, *type, dt_ms);
        }
        slot.used = true;
        slot.active = true;
        const sim::WorldIsland& island = world.islands()[w.island];
        const sim::WalkerPosition p = sim::walker_position(w);
        FigureInstance f;
        f.x = island.x + p.x;
        f.y = island.y + p.y;
        m_trail.position(trail_key(h), f.x, f.y);
        f.figure = w.figure;
        f.z = slot.z;
        // Every walker but the deer shows only on an island the local player has discovered
        // (0x00451590: kinds 4 to 0x12 except 0x11).
        if (w.kind != sim::WalkerKind::Deer) {
            f.island = w.island;
        }
        // Workers, hunters, grazing animals and deer are cut by the crops and trees they stand in
        // (case 0x0C of 0x0044FA90): a woodcutter at work vanishes into the tree.
        if (w.kind == sim::WalkerKind::Harvester || w.kind == sim::WalkerKind::Hunter ||
            w.kind == sim::WalkerKind::Grazer || w.kind == sim::WalkerKind::Deer) {
            f.crop = crop_cut(world, buildings, f.x, f.y);
        }
        // Carriers, carts, doctors, firemen and robbers are drawn only on roads and open ground
        // (0x00451590): on the tiles of a building they are hidden, so a cart crossing the market
        // square into a producer next to it is not seen at all.
        if (hidden_in_buildings(w.kind) && !walker_visible_at(world, buildings, f.x, f.y)) {
            return;
        }
        f.direction = w.direction;
        f.anim = slot.state.anim;
        f.frame = slot.state.frame;
        m_instances.push_back(f);
    });
    for (Slot& s : m_slots) {
        s.active = s.used;
    }
}

}  // namespace o1602::render
