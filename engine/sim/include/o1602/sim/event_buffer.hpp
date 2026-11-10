// SPDX-License-Identifier: GPL-3.0-or-later
//
// What happened in the game for the interface (messages, floating texts, sounds): the simulation
// collects events tick by tick, the interface takes them once per frame. A game without an
// interface (a server, a test, a tool) never takes them, so every buffer has a bound.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

namespace o1602::sim {

template <class T>
class EventBuffer {
public:
    /// Events nobody takes are dropped beyond this, the older half at once.
    static constexpr std::size_t kMaxPending = 8192;

    /// Adds an event; drops the older half first if the buffer is full.
    void push(T event) {
        trim();
        m_events.push_back(std::move(event));
    }
    /// Adds events in their order.
    void append(std::vector<T>&& events) {
        m_events.insert(m_events.end(), std::make_move_iterator(events.begin()), std::make_move_iterator(events.end()));
        trim();
    }
    /// For the functions of the subsystems that append to a vector. Indexes into it stay valid
    /// until the next push(), append() or trim().
    [[nodiscard]] std::vector<T>* sink() noexcept { return &m_events; }
    [[nodiscard]] const std::vector<T>& events() const noexcept { return m_events; }
    [[nodiscard]] std::size_t size() const noexcept { return m_events.size(); }

    /// Drops the older half once more than kMaxPending wait.
    void trim() {
        if (m_events.size() > kMaxPending) {
            const std::size_t dropped = m_events.size() - kMaxPending / 2;
            m_events.erase(m_events.begin(), m_events.begin() + static_cast<std::ptrdiff_t>(dropped));
            m_removed += dropped;
        }
    }
    /// The events since the last call, oldest first.
    [[nodiscard]] std::vector<T> take() noexcept {
        std::vector<T> events;
        events.swap(m_events);
        m_removed += events.size();
        return events;
    }

    /// How many events were ever added: before and after a tick, the difference is its events.
    [[nodiscard]] std::uint64_t total() const noexcept { return m_removed + m_events.size(); }
    /// The events added since total() was `before`, at most the latest kMaxPending / 2 of them:
    /// that many are always kept, so the result does not depend on when take() and trim() ran.
    [[nodiscard]] std::span<const T> since(std::uint64_t before) const noexcept {
        const std::uint64_t added = total() - before;
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(added, kMaxPending / 2));
        return std::span<const T>(m_events).subspan(m_events.size() - std::min(count, m_events.size()));
    }

private:
    std::vector<T> m_events;
    std::uint64_t m_removed = 0;  ///< taken or trimmed
};

}  // namespace o1602::sim
