// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/animation_clock.hpp"

#include <algorithm>

namespace o1602::render {

AnimationClock::AnimationClock(const assets::BuildingTable& buildings)
    : m_buildings(&buildings), m_frame(buildings.types().size()), m_accumulator(buildings.types().size()) {
    for (std::size_t i = 0; i < m_frame.size(); ++i) {
        m_frame[i] = buildings.types()[i].anim_frame;
    }
}

void AnimationClock::advance(std::uint32_t delta_ms, std::uint32_t speed) {
    m_elapsed += delta_ms;
    m_figure += std::min<std::uint64_t>(static_cast<std::uint64_t>(delta_ms) * speed, kMaxFigureStepMs);
    const auto& types = m_buildings->types();
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (types[i].anim_time <= 0) {
            continue;  // TIMENEVER
        }
        m_accumulator[i] += delta_ms;
        if (m_accumulator[i] + delta_ms / 2 >= static_cast<std::uint32_t>(types[i].anim_time)) {
            m_accumulator[i] = 0;
            ++m_frame[i];
        }
    }
}

std::uint32_t AnimationClock::frame(const assets::BuildingType& type) const {
    return m_frame[m_buildings->index_of(type)];
}

}  // namespace o1602::render
