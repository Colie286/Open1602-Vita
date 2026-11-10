// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the mouse and the keys mean on the map before anything is ordered (ADR 0010): a press of
// the left button that becomes a selection rectangle, the map dragged with the right button
// (released almost in place it is a click), a group key pressed twice, the next idle unit. Needs
// no platform, so that the tests have it; MapInput acts on it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "o1602/ui/unit_selection.hpp"

namespace o1602::app {

/// The left button pressed on the map: a click, or a selection rectangle once the mouse moved
/// 5 pixels (0x0040BA70).
class LeftPress {
public:
    void press(double x, double y) {
        m_at = std::make_pair(x, y);
        m_rubber_band = false;
    }
    /// The mouse moved to (x, y) with the left button down.
    void move(double x, double y);
    void reset() {
        m_at.reset();
        m_rubber_band = false;
    }
    /// Where the press began (screen); nothing if the left button was not pressed on the map.
    [[nodiscard]] const std::optional<std::pair<double, double>>& at() const noexcept { return m_at; }
    /// The press became a selection rectangle.
    [[nodiscard]] bool rubber_band() const noexcept { return m_rubber_band; }

private:
    std::optional<std::pair<double, double>> m_at;
    bool m_rubber_band = false;
};

/// The right (or middle) button drags the map; released after less than 4 pixels of travel it is
/// a click.
class MapDrag {
public:
    void press() {
        m_dragging = true;
        m_distance = 0.0;
    }
    /// The mouse moved by (dx, dy); true while the map follows it.
    bool move(double dx, double dy);
    /// The button went up: true if that was a click. A release without a press on the map (it went
    /// down on the panel, where the minimap already gave its order) is none, so that it does not
    /// send the selection to the tile under the mouse as well.
    bool release() {
        const bool click = m_dragging && m_distance < 4.0;
        m_dragging = false;
        return click;
    }
    [[nodiscard]] bool dragging() const noexcept { return m_dragging; }

private:
    bool m_dragging = false;
    double m_distance = 0.0;  ///< mouse travel while the button is held
};

/// Which way the map scrolls with the mouse at (x, y) on a screen of `width` x `height` pixels:
/// -1, 0 or 1 on each axis, towards the edges the mouse is within `margin` pixels of (both in a
/// corner).
struct EdgeScroll {
    int dx = 0;
    int dy = 0;
    friend bool operator==(const EdgeScroll&, const EdgeScroll&) = default;
};
[[nodiscard]] EdgeScroll edge_scroll_at(double x, double y, std::int32_t width, std::int32_t height,
                                        double margin) noexcept;

/// A group key pressed twice within kTwiceMs goes to the group.
class GroupKey {
public:
    static constexpr std::uint64_t kTwiceMs = 500;
    /// The key of `group` was pressed at `now_ms`: true if it was the same group's key just before.
    bool press(std::size_t group, std::uint64_t now_ms);
    void reset() noexcept { m_last = kNone; }

private:
    static constexpr std::size_t kNone = 0xFF;
    std::size_t m_last = kNone;  ///< the group key pressed last
    std::uint64_t m_last_ms = 0;
};

/// The next of `idle` after the single unit selected (`selected`), the first if none of them is
/// selected; nothing if `idle` is empty.
[[nodiscard]] std::optional<std::size_t> next_idle(const std::vector<ui::UnitRef>& idle,
                                                   std::optional<ui::UnitRef> selected);

/// The entry of `items` after `current` (round), the first if `current` is not among them;
/// nothing if `items` is empty. The position in `items` (the next city: indices of World::cities()).
[[nodiscard]] std::optional<std::size_t> next_in_cycle(const std::vector<std::size_t>& items,
                                                       std::optional<std::size_t> current);

}  // namespace o1602::app
