// SPDX-License-Identifier: GPL-3.0-or-later
//
// Entities with stable handles (carriers, carts, workers ...): no allocation per entity in the
// tick once the pool has grown, deterministic iteration in slot order, and handles that detect
// a reused slot (ADR 0004, S-002: a carrier belongs to its building by handle, not by position).
#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

namespace o1602::sim {

struct Handle {
    std::uint32_t index = 0xFFFFFFFFU;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != 0xFFFFFFFFU; }
    friend constexpr bool operator==(const Handle&, const Handle&) = default;
};

template <class T>
class Pool {
public:
    /// Adds an entity; reuses the lowest free slot so the order stays deterministic.
    Handle add(T value) {
        // Not inside for_each: a new slot could move the entity it is working on.
        assert(!m_iterating);
        std::uint32_t index = 0;
        if (!m_free.empty()) {
            // Lowest free slot (m_free is kept sorted descending, so the back is the lowest).
            index = m_free.back();
            m_free.pop_back();
            m_slots[index].value = std::move(value);
        } else {
            index = static_cast<std::uint32_t>(m_slots.size());
            m_slots.push_back(Slot{std::move(value), 0});
        }
        ++m_size;
        return Handle{index, m_slots[index].generation};
    }

    /// Removes the entity; its handle and all copies become invalid.
    void remove(Handle h) {
        if (!contains(h)) {
            return;
        }
        Slot& s = m_slots[h.index];
        s.value.reset();
        ++s.generation;
        --m_size;
        // Appended and rotated into place rather than insert(): GCC 13 and 14 at -O3 warn about the
        // insert into a vector (-Wstringop-overflow, a false positive).
        auto it = m_free.begin();
        while (it != m_free.end() && *it > h.index) {
            ++it;
        }
        const auto pos = it - m_free.begin();
        m_free.push_back(h.index);
        std::rotate(m_free.begin() + pos, m_free.end() - 1, m_free.end());
    }

    [[nodiscard]] bool contains(Handle h) const noexcept {
        return h.index < m_slots.size() && m_slots[h.index].generation == h.generation &&
               m_slots[h.index].value.has_value();
    }
    [[nodiscard]] T* get(Handle h) noexcept { return contains(h) ? &*m_slots[h.index].value : nullptr; }
    [[nodiscard]] const T* get(Handle h) const noexcept { return contains(h) ? &*m_slots[h.index].value : nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }

    /// Calls fn(handle, value) for every entity in slot order. fn may remove the current entity.
    template <class Fn>
    void for_each(Fn&& fn) {
        m_iterating = true;
        for (std::uint32_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].value) {
                fn(Handle{i, m_slots[i].generation}, *m_slots[i].value);
            }
        }
        m_iterating = false;
    }
    template <class Fn>
    void for_each(Fn&& fn) const {
        for (std::uint32_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].value) {
                fn(Handle{i, m_slots[i].generation}, *m_slots[i].value);
            }
        }
    }

    /// Slots for saving: their number, each one's generation and value (nullptr if free).
    [[nodiscard]] std::uint32_t slot_count() const noexcept { return static_cast<std::uint32_t>(m_slots.size()); }
    [[nodiscard]] std::uint32_t generation(std::uint32_t i) const noexcept { return m_slots[i].generation; }
    [[nodiscard]] const T* slot(std::uint32_t i) const noexcept {
        return m_slots[i].value ? &*m_slots[i].value : nullptr;
    }
    /// Restores saved slots: the same handles stay valid, free slots are taken again lowest first.
    void restore(std::vector<std::pair<std::optional<T>, std::uint32_t>> slots) {
        m_slots.clear();
        m_free.clear();
        m_size = 0;
        for (auto& [value, generation] : slots) {
            m_size += value ? 1U : 0U;
            m_slots.push_back(Slot{std::move(value), generation});
        }
        for (std::uint32_t i = static_cast<std::uint32_t>(m_slots.size()); i-- > 0;) {
            if (!m_slots[i].value) {
                m_free.push_back(i);
            }
        }
    }

private:
    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 0;
    };
    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_free;  ///< free slots, descending
    std::size_t m_size = 0;
    bool m_iterating = false;  ///< inside for_each (add() is not allowed then)
};

}  // namespace o1602::sim
