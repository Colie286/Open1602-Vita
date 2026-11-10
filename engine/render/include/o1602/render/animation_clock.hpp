// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/building_table.hpp"

namespace o1602::render {

/// Global animation frame per building type, advanced in real time like the original
/// (0x00468420, called every frame with the GetTickCount delta): an accumulator per type
/// collects milliseconds; when accumulator + dt/2 reaches AnimTime, the frame counter steps by one.
/// Animations are presentation only - they do not influence the simulation (ADR 0004).
/// At most one step per update is kept from the original (U-006).
class AnimationClock {
public:
    explicit AnimationClock(const assets::BuildingTable& buildings);

    /// `speed` is the game speed (1, 2, 4, 8); only figure_ms() runs with it.
    void advance(std::uint32_t delta_ms, std::uint32_t speed = 1);

    /// Current global frame of a type (to be taken modulo AnimAnz by the caller).
    [[nodiscard]] std::uint32_t frame(const assets::BuildingType& type) const;

    /// Real time advanced so far (effect figures run on it directly).
    [[nodiscard]] std::uint64_t elapsed_ms() const noexcept { return m_elapsed; }

    /// Game time of the figures on buildings and ships (flags, smoke, bow waves): it runs with the
    /// game speed like the original's figures. Measured in the original and the History Edition, a
    /// marketplace flag changes its image 11, 22 and 44 times a second at 1x, 2x and 4x. At most
    /// kMaxFigureStepMs per update, so a flag steps at most once per drawn frame (at 8x the
    /// original is held at the frame rate too).
    [[nodiscard]] std::uint64_t figure_ms() const noexcept { return m_figure; }

    static constexpr std::uint32_t kMaxFigureStepMs = 90;

private:
    const assets::BuildingTable* m_buildings;
    std::vector<std::uint32_t> m_frame;
    std::vector<std::uint32_t> m_accumulator;
    std::uint64_t m_elapsed = 0;
    std::uint64_t m_figure = 0;
};

}  // namespace o1602::render
