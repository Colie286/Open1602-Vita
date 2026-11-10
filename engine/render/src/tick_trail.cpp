// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/tick_trail.hpp"

#include <algorithm>
#include <cmath>

namespace o1602::render {

void tick_lerp(double was_x, double was_y, double now_x, double now_y, double t, double max_jump, double& x,
               double& y) noexcept {
    x = now_x;
    y = now_y;
    if (std::abs(now_x - was_x) > max_jump || std::abs(now_y - was_y) > max_jump) {
        return;
    }
    t = std::clamp(t, 0.0, 1.0);
    x = was_x + (now_x - was_x) * t;
    y = was_y + (now_y - was_y) * t;
}

void TickTrail::sort() {
    // Units mostly come in the order of their keys already.
    if (!std::is_sorted(m_was.begin(), m_was.end(), [](const Was& a, const Was& b) { return a.key < b.key; })) {
        std::sort(m_was.begin(), m_was.end(), [](const Was& a, const Was& b) { return a.key < b.key; });
    }
}

void TickTrail::position(std::uint64_t key, double& x, double& y) const noexcept {
    std::size_t hint = 0;
    position(key, x, y, hint);
}

void TickTrail::position(std::uint64_t key, double& x, double& y, std::size_t& hint) const noexcept {
    const auto less = [](const Was& w, std::uint64_t k) { return w.key < k; };
    std::size_t i = hint <= m_was.size() && (hint == 0 || m_was[hint - 1].key < key) ? hint : 0;
    // A few steps on from the last key, else a search in what is left.
    for (int steps = 0; i < m_was.size() && m_was[i].key < key; ++i) {
        if (++steps == 8) {
            const auto first = m_was.begin() + static_cast<std::ptrdiff_t>(i);
            i = static_cast<std::size_t>(std::lower_bound(first, m_was.end(), key, less) - m_was.begin());
            break;
        }
    }
    hint = i;
    if (i == m_was.size() || m_was[i].key != key) {
        return;
    }
    tick_lerp(m_was[i].x, m_was[i].y, x, y, m_fraction, kMaxStep, x, y);
}

}  // namespace o1602::render
