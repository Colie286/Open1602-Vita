// SPDX-License-Identifier: GPL-3.0-or-later
//
// Units drawn between two ticks (ADR 0004, review point 20): the simulation moves them 20 times a
// second; a screen shows 60 or 144 pictures. A trail keeps where each unit was before the last
// tick, and the picture puts it on the way from there to where it is now, by the share of the next
// tick that has passed. The simulation is not touched. Ships, soldiers and the walkers of the
// logistics each keep one, keyed by what names a unit of their kind.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace o1602::render {

/// The point `t` (clamped to 0..1) of the way from (was_x, was_y) to (now_x, now_y), written to
/// (x, y); (now_x, now_y) when the unit moved more than `max_jump` tiles on either axis in the
/// tick (set down, loaded, sent home), which is not smoothed.
void tick_lerp(double was_x, double was_y, double now_x, double now_y, double t, double max_jump, double& x,
               double& y) noexcept;

class TickTrail {
public:
    /// A unit that moved further than this (tiles) in one tick jumped.
    static constexpr double kMaxStep = 2.0;

    /// Before a tick: forgets the last positions and keeps those `each(add)` passes, as
    /// add(key, x, y) in any order and with distinct keys.
    template <class Each>
    void capture(Each&& each) {
        m_was.clear();
        each([this](std::uint64_t key, double x, double y) { m_was.push_back(Was{key, x, y}); });
        sort();
    }
    void clear() noexcept { m_was.clear(); }

    /// Share of the next tick that has passed (Simulation::tick_fraction).
    void set_fraction(double fraction) noexcept { m_fraction = fraction; }
    [[nodiscard]] double fraction() const noexcept { return m_fraction; }

    /// Where to draw the unit `key` that is at (x, y) now: on its way from where it was before the
    /// last tick (tick_lerp). One the trail has not seen is drawn where it is.
    void position(std::uint64_t key, double& x, double& y) const noexcept;
    /// Same, for callers that ask for the keys in rising order: `hint` (0 before the first call)
    /// keeps the place of the last key, so a pass over all units costs one walk over the trail
    /// instead of a search per unit (FB-2). Any order still gives the right answer.
    void position(std::uint64_t key, double& x, double& y, std::size_t& hint) const noexcept;

private:
    struct Was {
        std::uint64_t key;
        double x;
        double y;
    };
    void sort();

    std::vector<Was> m_was;  ///< sorted by key
    double m_fraction = 1.0;
};

}  // namespace o1602::render
