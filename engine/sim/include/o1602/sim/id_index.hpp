// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finds the entry with an id in a list of ships or units in O(log n) instead of a search through
// all of them, which every unit did for its targets every tick (review point 16).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace o1602::sim {

/// The simulation changes the lists directly (erase_if, add), so the index does not follow every
/// change: it notices one by the list's address, length and next id, or by an entry that is no
/// longer where it was, and then builds itself anew. It answers as a search from the front did:
/// the first entry with the id.
class IdIndex {
public:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    template <typename T>
    [[nodiscard]] std::size_t find(const std::vector<T>& list, std::uint32_t id, std::uint32_t next_id) const {
        const bool fresh =
            m_data == static_cast<const void*>(list.data()) && m_size == list.size() && m_next_id == next_id;
        if (fresh) {
            const std::size_t slot = lookup(id);
            if (slot == kNone || list[slot].id == id) {
                return slot;
            }
        }
        m_entries.clear();
        m_entries.reserve(list.size());
        for (std::size_t i = 0; i < list.size(); ++i) {
            m_entries.push_back({list[i].id, static_cast<std::uint32_t>(i)});
        }
        std::stable_sort(m_entries.begin(), m_entries.end(),
                         [](const Entry& a, const Entry& b) { return a.id < b.id; });
        m_data = list.data();
        m_size = list.size();
        m_next_id = next_id;
        return lookup(id);
    }

    /// Before a change of the list through World: whether the index matches it now.
    template <typename T>
    [[nodiscard]] bool matches(const std::vector<T>& list, std::uint32_t next_id) const noexcept {
        return m_data == static_cast<const void*>(list.data()) && m_size == list.size() && m_next_id == next_id;
    }

    /// The list got one entry at its end and the index matched it before (TB-6): no new build when
    /// its id is the largest, as a new unit's is.
    template <typename T>
    void appended(const std::vector<T>& list, bool matched, std::uint32_t next_id) {
        if (!matched || list.empty() || (!m_entries.empty() && m_entries.back().id >= list.back().id)) {
            m_data = nullptr;  // built anew at the next find
            return;
        }
        m_entries.push_back({list.back().id, static_cast<std::uint32_t>(list.size() - 1)});
        m_data = list.data();
        m_size = list.size();
        m_next_id = next_id;
    }

    /// The entries at `removed` (ascending slots of the list before) left it, the others kept their
    /// order (TB-6): the index follows in one pass instead of sorting anew.
    template <typename T>
    void erased(const std::vector<T>& list, bool matched, const std::vector<std::uint32_t>& removed) {
        if (!matched) {
            m_data = nullptr;
            return;
        }
        std::size_t kept = 0;
        for (const Entry& e : m_entries) {
            const auto it = std::lower_bound(removed.begin(), removed.end(), e.slot);
            if (it != removed.end() && *it == e.slot) {
                continue;
            }
            m_entries[kept++] = {e.id, e.slot - static_cast<std::uint32_t>(it - removed.begin())};
        }
        m_entries.resize(kept);
        m_data = list.data();
        m_size = list.size();
    }

private:
    struct Entry {
        std::uint32_t id;
        std::uint32_t slot;
    };

    [[nodiscard]] std::size_t lookup(std::uint32_t id) const noexcept {
        const auto it = std::lower_bound(m_entries.begin(), m_entries.end(), id,
                                         [](const Entry& e, std::uint32_t v) { return e.id < v; });
        return it != m_entries.end() && it->id == id ? it->slot : kNone;
    }

    mutable std::vector<Entry> m_entries;
    mutable const void* m_data = nullptr;
    mutable std::size_t m_size = 0;
    mutable std::uint32_t m_next_id = 0;
};

}  // namespace o1602::sim
