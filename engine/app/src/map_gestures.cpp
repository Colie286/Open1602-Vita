// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_gestures.hpp"

#include <algorithm>
#include <cmath>

namespace o1602::app {

void TouchPress::press(double x, double y, std::uint64_t ms) noexcept {
    m_origin = {x, y};
    m_started = ms;
    m_kind = Kind::Pending;
}

TouchPress::Kind TouchPress::move(double x, double y, std::uint64_t ms) noexcept {
    if (m_kind == Kind::Pending) {
        const double dx = x - m_origin.first, dy = y - m_origin.second;
        if (dx * dx + dy * dy >= 64.0) {
            m_kind = ms - m_started >= kHoldMs ? Kind::Hold : Kind::Pan;
        }
    }
    return m_kind;
}

void TouchScroll::begin(double x, double y, std::uint64_t ms) noexcept {
    reset();
    m_x = x; m_y = y; m_ms = ms;
    m_tracking = true;
}

void TouchScroll::sample(double x, double y, std::uint64_t ms) noexcept {
    if (!m_tracking || ms <= m_ms || (x == m_x && y == m_y)) return;
    const double dt = static_cast<double>(ms - m_ms);
    double vx = (m_x - x) * 1000.0 / dt, vy = (m_y - y) * 1000.0 / dt;
    const double speed = std::hypot(vx, vy);
    if (speed > 1800.0) { vx *= 1800.0 / speed; vy *= 1800.0 / speed; }
    // A reversal must not leave the camera gliding against the latest swipe.
    if (dt > 150.0 || vx * m_vx + vy * m_vy < 0.0) m_vx = m_vy = 0;
    const double weight = 1.0 - std::exp(-dt / 35.0);
    m_vx += (vx - m_vx) * weight;
    m_vy += (vy - m_vy) * weight;
    m_x = x; m_y = y; m_ms = ms;
}

void TouchScroll::release(std::uint64_t ms) noexcept {
    if (!m_tracking || ms < m_ms || ms - m_ms > 100) { reset(); return; }
    // Holding still before lifting the finger deliberately ends the swipe.
    const double decay = std::exp(-static_cast<double>(ms - m_ms) / 40.0);
    m_vx *= decay; m_vy *= decay;
    m_tracking = false;
    m_active = std::hypot(m_vx, m_vy) >= 60.0;
}

std::pair<double, double> TouchScroll::advance(std::uint32_t delta_ms) noexcept {
    if (!m_active || delta_ms == 0) return {};
    // Resume or a long loading frame must not throw the camera across the map.
    if (delta_ms > 150) { reset(); return {}; }
    constexpr double tau = 0.35, stop_speed = 8.0;
    const double remaining = tau * std::log(std::hypot(m_vx, m_vy) / stop_speed);
    const double dt = std::min(static_cast<double>(delta_ms) / 1000.0, remaining);
    const double decay = std::exp(-dt / tau);
    const std::pair<double, double> distance{m_vx * tau * (1.0 - decay), m_vy * tau * (1.0 - decay)};
    m_vx *= decay; m_vy *= decay;
    if (dt >= remaining) reset();
    return distance;
}

void LeftPress::move(double x, double y) {
    if (!m_at) {
        return;
    }
    const double dx = x - m_at->first;
    const double dy = y - m_at->second;
    m_rubber_band = m_rubber_band || dx * dx + dy * dy >= 25.0;  // 5 px
}

bool MapDrag::move(double dx, double dy) {
    if (m_dragging) {
        m_distance += std::abs(dx) + std::abs(dy);
    }
    return m_dragging;
}

EdgeScroll edge_scroll_at(double x, double y, std::int32_t width, std::int32_t height, double margin) noexcept {
    EdgeScroll e;
    if (width <= 0 || height <= 0) {
        return e;
    }
    const auto axis = [margin](double at, std::int32_t size) {
        if (at < margin) {
            return -1;
        }
        return at >= static_cast<double>(size) - margin ? 1 : 0;
    };
    e.dx = axis(x, width);
    e.dy = axis(y, height);
    return e;
}

bool GroupKey::press(std::size_t group, std::uint64_t now_ms) {
    const bool twice = m_last == group && now_ms - m_last_ms < kTwiceMs;
    m_last = group;
    m_last_ms = now_ms;
    return twice;
}

std::optional<std::size_t> next_idle(const std::vector<ui::UnitRef>& idle, std::optional<ui::UnitRef> selected) {
    if (idle.empty()) {
        return std::nullopt;
    }
    if (!selected) {
        return 0;
    }
    const auto it = std::find(idle.begin(), idle.end(), *selected);
    return it == idle.end() ? 0 : static_cast<std::size_t>(it - idle.begin() + 1) % idle.size();
}

std::optional<std::size_t> next_in_cycle(const std::vector<std::size_t>& items, std::optional<std::size_t> current) {
    if (items.empty()) {
        return std::nullopt;
    }
    if (!current) {
        return 0;
    }
    const auto it = std::find(items.begin(), items.end(), *current);
    return it == items.end() ? 0 : static_cast<std::size_t>(it - items.begin() + 1) % items.size();
}

}  // namespace o1602::app
