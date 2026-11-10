// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/combat_effects.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

#include "o1602/render/land_figures.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::render {

namespace {

constexpr std::uint16_t kNoFigure = 0xFFFF;  // NOOBJEKT: swordsmen leave no hit
/// Height of the hit on a building (Projectile_Hit 0x00445930); ships are hit at 0.
constexpr double kBuildingHitHeight = 1.0;

std::uint64_t tile_key(std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    return static_cast<std::uint64_t>(island) << 32U | static_cast<std::uint64_t>(x) << 16U | y;
}

const assets::FigureType* effect_type(const assets::FigureTable& figures, std::uint16_t figure) {
    return figure != 0 && figure != kNoFigure ? figures.get(figure) : nullptr;
}

/// Frame of the first animation `elapsed_ms` after the start; it does not wrap, as no effect
/// lives longer than one run of it.
std::uint8_t first_anim_frame(const assets::FigureType& type, std::uint64_t elapsed_ms) {
    const assets::FigureAnim* a = type.anim(0);
    if (a == nullptr || a->count == 0) {
        return 0;
    }
    const std::uint64_t speed = a->speed_ms != 0 ? a->speed_ms : 1;
    return static_cast<std::uint8_t>(std::min<std::uint64_t>(elapsed_ms / speed, a->count - 1U));
}

}  // namespace

std::optional<WreckFrame> wreck_frame(const assets::FigureType& wreck, std::uint64_t elapsed_ms) {
    // 0x00452300: when the timer runs out the next ANIM starts with a timer of its whole run,
    // AnimSpeed * AnimRept * AnimAnz; from ANIM 3 on the figure is freed when it runs out.
    std::uint64_t t = elapsed_ms;
    for (std::uint8_t anim = 0; anim <= 3; ++anim) {
        const assets::FigureAnim* a = wreck.anim(anim);
        if (a == nullptr || a->count == 0) {
            return std::nullopt;
        }
        const std::uint64_t speed = a->speed_ms != 0 ? a->speed_ms : 1;
        const std::uint64_t length =
            anim == 0 ? wreck.work_time_ms : speed * std::max<std::uint32_t>(a->repeat, 1U) * a->count;
        if (t < length) {
            return WreckFrame{anim, static_cast<std::uint8_t>(t / speed % a->count)};
        }
        t -= length;
    }
    return std::nullopt;
}

void CombatEffects::reset() noexcept {
    m_bursts.clear();
    m_smoke.clear();
    m_wrecks.clear();
    m_sinking.clear();
    m_effects.clear();
    m_ship_smoke.clear();
}

void CombatEffects::military(const sim::World& world, const assets::BuildingTable& buildings,
                             const assets::FigureTable& figures, std::span<const sim::MilitaryEvent> events) {
    const std::uint64_t now = world.time_ms();
    std::unordered_map<std::uint64_t, const sim::Tower*> tower_index;
    for (const sim::MilitaryEvent& e : events) {
        if (e.kind == sim::MilitaryEvent::Kind::Hit) {
            // Projectile_Hit 0x00445930: the shooter's Hitfignr where the shot landed, at the height
            // of the target plus the effect's Fahnoffs[2], for its Worktime.
            const assets::FigureType* type = effect_type(figures, e.figure);
            if (type == nullptr) {
                continue;
            }
            Burst b;
            b.figure.x = e.x / static_cast<double>(sim::kShipUnit);
            b.figure.y = e.y / static_cast<double>(sim::kShipUnit);
            double ground = 0.0;
            switch (e.target.kind) {
                case sim::Target::Kind::Building:
                    ground = kBuildingHitHeight;
                    break;
                case sim::Target::Kind::Soldier:
                    ground = spawn_height(world, buildings, b.figure.x, b.figure.y, true);
                    break;
                default:
                    break;
            }
            b.figure.z = ground + type->fahnoffs[2];
            b.figure.ground = ground;
            b.figure.figure = e.figure;
            b.figure.sheet = assets::FigureSheet::Effekte;
            b.start_ms = now;
            b.life_ms = type->work_time_ms;
            m_bursts.push_back(b);
        } else if (e.kind == sim::MilitaryEvent::Kind::Shot && e.unit.kind == sim::Target::Kind::Building) {
            // Fx_SpawnTowerShot 0x00447F00: the tower's Shotfignr Fahnoffs[0] out from its centre
            // towards where it aims, Fahnoffs[2] high, turned like the tower.
            // Towers by origin, made at the first shot of the call (FB-17); the first tower at a
            // tile wins, as a linear search would find it.
            if (tower_index.empty()) {
                const auto& towers = world.towers();
                tower_index.reserve(towers.size());
                for (const sim::Tower& t : towers) {
                    tower_index.emplace(tile_key(t.island, t.x, t.y), &t);
                }
            }
            const sim::Tower* tower = nullptr;
            if (e.unit.x <= 0xFFFFU && e.unit.y <= 0xFFFFU) {
                const auto found = tower_index.find(tile_key(e.unit.island, static_cast<std::uint16_t>(e.unit.x),
                                                             static_cast<std::uint16_t>(e.unit.y)));
                tower = found != tower_index.end() ? found->second : nullptr;
            }
            const assets::FigureType* tower_type = tower != nullptr ? figures.get(tower->figure) : nullptr;
            const assets::FigureType* type =
                tower_type != nullptr ? effect_type(figures, tower_type->combat.shot_figure) : nullptr;
            if (type == nullptr) {
                continue;
            }
            const double angle = static_cast<double>(tower->direction & 7U) * std::numbers::pi / 4.0;
            Burst b;
            b.figure.x = e.x / static_cast<double>(sim::kShipUnit) + type->fahnoffs[0] * std::sin(angle);
            b.figure.y = e.y / static_cast<double>(sim::kShipUnit) - type->fahnoffs[0] * std::cos(angle);
            b.figure.z = type->fahnoffs[2];
            b.figure.figure = tower_type->combat.shot_figure;
            b.figure.sheet = assets::FigureSheet::Ship;
            b.figure.direction = tower->direction;
            b.start_ms = now;
            b.life_ms = type->work_time_ms;
            m_bursts.push_back(b);
        }
    }
}

void CombatEffects::ships(const sim::World& world, const assets::FigureTable& figures,
                          std::span<const sim::ShipEvent> events) {
    const std::uint64_t now = world.time_ms();
    for (const sim::ShipEvent& e : events) {
        if (e.kind != sim::ShipEvent::Kind::Fired) {
            continue;
        }
        // Net_ApplyShot 0x00447880: the ship type's Shotfignr goes with the ship and replaces the
        // smoke of its last shot. A ship fires with its side to the bearing: heading = bearing + 2
        // is the right side, bearing + 6 the left.
        const sim::Ship* ship = world.ship(e.ship);
        const assets::FigureType* ship_type = ship != nullptr ? figures.get(ship->figure) : nullptr;
        const assets::FigureType* type =
            ship_type != nullptr ? effect_type(figures, ship_type->combat.shot_figure) : nullptr;
        if (type == nullptr) {
            continue;
        }
        Smoke s;
        s.figure = ship_type->combat.shot_figure;
        s.side = ((ship->direction - e.ware) & 7U) == 2U ? 0 : 1;
        s.start_ms = now;
        s.life_ms = type->work_time_ms;
        m_smoke[e.ship] = s;
    }
}

void CombatEffects::frame(const sim::World& world, const assets::FigureTable& figures, double tick_fraction) {
    const std::uint64_t now = world.time_ms();
    constexpr double kTickMs = sim::Logistics::kTickMs;
    const double drawn_ms =
        std::max(0.0, static_cast<double>(now) - kTickMs + std::clamp(tick_fraction, 0.0, 1.0) * kTickMs);
    const auto drawn = static_cast<std::uint64_t>(drawn_ms);
    const auto elapsed = [&](std::uint64_t start) { return drawn > start ? drawn - start : 0U; };

    // A sunk ship leaves its wreck where it went down when its figure is freed (ShipWreck_Spawn
    // 0x00446E10).
    for (auto& [id, s] : m_sinking) {
        s.seen = false;
    }
    for (const sim::Ship& ship : world.ships()) {
        if (!ship.sinking) {
            continue;
        }
        const bool placed = ship.pos_x != 0 || ship.pos_y != 0;
        Sinking& s = m_sinking[ship.id];
        s.x = placed ? static_cast<double>(ship.pos_x) / sim::kShipUnit : ship.x + 0.5;
        s.y = placed ? static_cast<double>(ship.pos_y) / sim::kShipUnit : ship.y + 0.5;
        s.seen = true;
    }
    for (auto it = m_sinking.begin(); it != m_sinking.end();) {
        if (it->second.seen) {
            ++it;
            continue;
        }
        m_wrecks.push_back(Wreck{it->second.x, it->second.y, now});
        it = m_sinking.erase(it);
    }

    m_effects.clear();
    std::erase_if(m_bursts, [&](const Burst& b) { return elapsed(b.start_ms) >= b.life_ms; });
    for (const Burst& b : m_bursts) {
        if (const assets::FigureType* type = figures.get(b.figure.figure)) {
            EffectFigure f = b.figure;
            f.frame = first_anim_frame(*type, elapsed(b.start_ms));
            m_effects.push_back(f);
        }
    }
    const assets::FigureType* wreck = figures.get(kFigureShipSunk);
    std::erase_if(m_wrecks,
                  [&](const Wreck& w) { return wreck == nullptr || !wreck_frame(*wreck, elapsed(w.start_ms)); });
    for (const Wreck& w : m_wrecks) {
        const WreckFrame wf = *wreck_frame(*wreck, elapsed(w.start_ms));
        EffectFigure f;
        f.x = w.x;
        f.y = w.y;
        f.figure = kFigureShipSunk;
        f.anim = wf.anim;
        f.frame = wf.frame;
        f.sheet = assets::FigureSheet::Effekte;
        m_effects.push_back(f);
    }

    std::erase_if(m_smoke, [&](const auto& entry) {
        return elapsed(entry.second.start_ms) >= entry.second.life_ms || world.ship(entry.first) == nullptr;
    });
    // Updated in place rather than cleared and refilled every frame (FB-17).
    std::erase_if(m_ship_smoke, [&](const auto& entry) { return !m_smoke.contains(entry.first); });
    for (const auto& [id, s] : m_smoke) {
        if (const assets::FigureType* type = figures.get(s.figure)) {
            m_ship_smoke[id] = ShipSmoke{s.figure, s.side, first_anim_frame(*type, elapsed(s.start_ms))};
        } else {
            m_ship_smoke.erase(id);
        }
    }
}

}  // namespace o1602::render
