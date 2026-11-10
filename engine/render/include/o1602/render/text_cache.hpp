// SPDX-License-Identifier: GPL-3.0-or-later
//
// Values made from texts (the interface's text textures), by text, font and colour. When the cache
// is full, the least recently used texts go first, but never one used in the current frame: a
// frame with more texts than the cache holds lets it grow instead of making its texts anew on
// every draw. Lookups take a string_view and copy nothing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace o1602::render {

template <typename Value>
class TextCache {
public:
    explicit TextCache(std::size_t capacity) : m_capacity(capacity) {}

    /// The value of a text, nullptr if it is not cached. A hit counts as a use in this frame.
    [[nodiscard]] Value* find(std::string_view text, std::int32_t font, std::int32_t colour) {
        const auto it = m_index.find(Key{text, font, colour});
        if (it == m_index.end()) {
            return nullptr;
        }
        it->second->frame = m_frame;
        m_entries.splice(m_entries.begin(), m_entries, it->second);
        return &it->second->value;
    }

    /// Adds a text. While the cache is full, its least recently used text that was not used in this
    /// frame is handed to `evict` and removed. A text cached already has its old value handed to
    /// `evict` and replaced.
    template <typename Evict>
    Value& insert(std::string_view text, std::int32_t font, std::int32_t colour, Value value, Evict&& evict) {
        if (const auto it = m_index.find(Key{text, font, colour}); it != m_index.end()) {
            const auto entry = it->second;
            m_index.erase(it);
            std::invoke(evict, entry->value);
            m_entries.erase(entry);
        }
        while (m_entries.size() >= m_capacity && !m_entries.empty() && m_entries.back().frame != m_frame) {
            Entry& old = m_entries.back();
            m_index.erase(Key{old.text, old.font, old.colour});
            std::invoke(evict, old.value);
            m_entries.pop_back();
        }
        m_entries.push_front(Entry{std::string(text), font, colour, std::move(value), m_frame});
        Entry& e = m_entries.front();
        m_index.emplace(Key{e.text, e.font, e.colour}, m_entries.begin());
        return e.value;
    }

    /// Starts the next frame: what was used so far may be evicted again.
    void next_frame() noexcept { ++m_frame; }

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    /// Calls `f` with every cached value (to release them).
    template <typename F>
    void for_each(F&& f) {
        for (Entry& e : m_entries) {
            std::invoke(f, e.value);
        }
    }

private:
    struct Entry {
        std::string text;
        std::int32_t font = 0;
        std::int32_t colour = 0;
        Value value;
        std::uint64_t frame = 0;
    };
    /// Views the text of its entry (list nodes do not move) or the text looked up.
    struct Key {
        std::string_view text;
        std::int32_t font = 0;
        std::int32_t colour = 0;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            std::size_t h = std::hash<std::string_view>{}(k.text);
            h ^= std::hash<std::uint64_t>{}((std::uint64_t{static_cast<std::uint32_t>(k.font)} << 32U) |
                                            static_cast<std::uint32_t>(k.colour)) +
                 std::size_t{0x9E3779B9U} + (h << 6U) + (h >> 2U);
            return h;
        }
    };

    std::size_t m_capacity;
    std::uint64_t m_frame = 0;
    std::list<Entry> m_entries;  ///< most recently used first
    std::unordered_map<Key, typename std::list<Entry>::iterator, KeyHash> m_index;
};

}  // namespace o1602::render
