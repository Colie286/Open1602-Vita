// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a fight shows besides the units themselves (docs/re/military.md#effects): the hit where a
// shot lands (TREFFKAN1, TREFFKAN2, TREFFMUS), the smoke of a ship's broadside and of a tower's
// cannon (KANONSHOT*), and the wreck a sunk ship leaves on the water (SHIPSUNK).
//
// The original keeps these as figures of its list with a timer, freed when the timer runs out.
// They change nothing in the game, so Open1602 makes them from the events of the simulation, on
// the side of the picture only, and saves nothing. They follow game time: a pause stops them.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/military.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::render {

inline constexpr std::uint16_t kFigureShipSunk = assets::figure::kShipSunk;

class CombatEffects {
public:
    /// Forgets everything, e.g. when another game is loaded.
    void reset() noexcept;

    /// Hits and tower smoke from the events of the ticks since the last call. Call with the world
    /// as those ticks left it.
    void military(const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                  std::span<const sim::MilitaryEvent> events);
    /// Smoke of the ships' broadsides, from the same ticks.
    void ships(const sim::World& world, const assets::FigureTable& figures, std::span<const sim::ShipEvent> events);

    /// The pictures at the world's time: call once per displayed frame. `tick_fraction` is the
    /// share of the next tick that has passed (Simulation::tick_fraction); the effects run up to a
    /// tick behind, like the units they belong to.
    void frame(const sim::World& world, const assets::FigureTable& figures, double tick_fraction = 1.0);

    /// Hits, tower smoke and wrecks, each with its frame.
    [[nodiscard]] const std::vector<EffectFigure>& effects() const noexcept { return m_effects; }
    /// The smoke of each ship that fired, by Ship::id (drawn with the ship).
    [[nodiscard]] const ShipSmokeMap& ship_smoke() const noexcept { return m_ship_smoke; }

private:
    /// An effect that plays its first animation once, for the figure's Worktime.
    struct Burst {
        EffectFigure figure;
        std::uint64_t start_ms = 0;
        std::uint32_t life_ms = 0;
    };
    struct Smoke {
        std::uint16_t figure = 0;
        std::uint8_t side = 0;
        std::uint64_t start_ms = 0;
        std::uint32_t life_ms = 0;
    };
    struct Wreck {
        double x = 0.0;
        double y = 0.0;
        std::uint64_t start_ms = 0;
    };
    struct Sinking {
        double x = 0.0;
        double y = 0.0;
        bool seen = false;
    };

    std::vector<Burst> m_bursts;
    std::unordered_map<std::uint32_t, Smoke> m_smoke;
    std::vector<Wreck> m_wrecks;
    std::unordered_map<std::uint32_t, Sinking> m_sinking;  ///< sinking ships by id, where they are
    std::vector<EffectFigure> m_effects;
    ShipSmokeMap m_ship_smoke;
};

/// Animation and frame of the wreck `elapsed_ms` after its ship went down, or nothing once it is
/// gone (exposed for tests). ANIM 0 runs for the Worktime, ANIM 1 for its frames times AnimRept,
/// then ANIM 2 and the last image of ANIM 3 (0x00452300); about 22 s in all.
struct WreckFrame {
    std::uint8_t anim = 0;
    std::uint8_t frame = 0;
};
[[nodiscard]] std::optional<WreckFrame> wreck_frame(const assets::FigureType& wreck, std::uint64_t elapsed_ms);

}  // namespace o1602::render
