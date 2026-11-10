// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace o1602::core {

/// Monotone integer priorities: a push is between the last popped priority and
/// that priority + MaxStep. Equal priorities leave in insertion order, exactly
/// like a min-heap ordered by (priority, insertion sequence).
/// Reusable intrusive FIFO buckets avoid per-node heap comparisons/allocations.
template <std::uint32_t MaxStep>
class BoundedPriorityQueue {
public:
    BoundedPriorityQueue() { clear(); }
    void clear() noexcept {
        m_heads.fill(none);
        m_tails.fill(none);
        m_nodes.clear();
        m_free = none;
        m_priority = 0;
        m_count = 0;
    }
    [[nodiscard]] bool empty() const noexcept { return m_count == 0; }
    void push(std::uint32_t priority, std::uint32_t value) {
        assert(priority >= m_priority && priority - m_priority <= MaxStep);
        std::uint32_t slot = m_free;
        if (slot == none) {
            assert(m_nodes.size() < none);
            slot = static_cast<std::uint32_t>(m_nodes.size());
            m_nodes.push_back({value, none});
        } else {
            m_free = m_nodes[slot].next;
            m_nodes[slot] = {value, none};
        }
        const auto bucket = priority % buckets;
        if (m_heads[bucket] == none)
            m_heads[bucket] = slot;
        else
            m_nodes[m_tails[bucket]].next = slot;
        m_tails[bucket] = slot;
        ++m_count;
    }
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> pop() noexcept {
        assert(!empty());
        while (m_heads[m_priority % buckets] == none)
            ++m_priority;
        const auto bucket = m_priority % buckets;
        const auto slot = m_heads[bucket];
        const auto node = m_nodes[slot];
        m_heads[bucket] = node.next;
        if (node.next == none)
            m_tails[bucket] = none;
        m_nodes[slot].next = m_free;
        m_free = slot;
        --m_count;
        return {m_priority, node.value};
    }
private:
    // Any ring larger than the largest step preserves priority/FIFO order.
    // Power-of-two indexing avoids repeated integer remainder in the hot path
    // (especially expensive on ARMv7). No search budget or tie changes.
    static_assert(MaxStep < (1U<<31));
    static constexpr std::uint32_t buckets = std::bit_ceil(MaxStep + 1);
    static constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
    struct Node { std::uint32_t value, next; };
    std::array<std::uint32_t, buckets> m_heads{}, m_tails{};
    std::vector<Node> m_nodes;
    std::uint32_t m_free = none, m_priority = 0, m_count = 0;
};

}  // namespace o1602::core
