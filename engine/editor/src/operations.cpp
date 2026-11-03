// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/operations.hpp"

namespace o1602::editor {

namespace {

const sim::MapCell& read_cell(const sim::IslandMap& map, Layer layer, std::uint32_t x, std::uint32_t y) {
    return layer == Layer::Top ? map.top(x, y) : map.ground(x, y);
}

void write_cell(sim::IslandMap& map, Layer layer, std::uint32_t x, std::uint32_t y, const sim::MapCell& cell) {
    if (layer == Layer::Top) {
        map.set_top_cell(x, y, cell);
    } else {
        map.set_ground_cell(x, y, cell);
    }
}

bool same(const sim::MapCell& a, const sim::MapCell& b) noexcept {
    return a.sprite == b.sprite && a.rotation == b.rotation && a.anim_phase == b.anim_phase && a.owner == b.owner &&
           a.city == b.city && a.random == b.random;
}

std::size_t text_memory(const std::vector<sim::ScenarioText>& texts) {
    std::size_t n = texts.capacity() * sizeof(sim::ScenarioText);
    for (const auto& t : texts) {
        n += t.language.size() + t.text.size();
    }
    return n;
}

std::size_t info_memory(const std::optional<sim::ScenarioInfo>& info) {
    return info ? text_memory(info->names) + text_memory(info->briefings) + info->author.size() + info->version.size()
                : 0;
}

std::size_t markers_memory(const std::vector<sim::ScenarioMarker>& markers) {
    std::size_t n = markers.capacity() * sizeof(sim::ScenarioMarker);
    for (const auto& m : markers) {
        n += m.name.size();
    }
    return n;
}

}  // namespace

void TileEdit::apply(sim::World& world) {
    for (const Change& c : m_changes) {
        write_cell(world.island(c.island).map, c.layer, c.x, c.y, c.after);
    }
}

void TileEdit::revert(sim::World& world) {
    for (auto it = m_changes.rbegin(); it != m_changes.rend(); ++it) {
        write_cell(world.island(it->island).map, it->layer, it->x, it->y, it->before);
    }
}

std::size_t TileEdit::memory() const noexcept {
    return sizeof(*this) + m_changes.capacity() * sizeof(Change);
}

std::uint64_t TileEditBuilder::key(std::uint16_t island, Layer layer, std::uint32_t x, std::uint32_t y) noexcept {
    return std::uint64_t{island} << 40 | std::uint64_t{layer == Layer::Ground ? 1U : 0U} << 32 |
           std::uint64_t{x & 0xFFFFU} << 16 | (y & 0xFFFFU);
}

bool TileEditBuilder::set(std::uint16_t island, Layer layer, std::uint32_t x, std::uint32_t y,
                          const sim::MapCell& cell) {
    if (island >= m_world->islands().size()) {
        return false;
    }
    const sim::IslandMap& map = m_world->islands()[island].map;
    if (x >= map.width() || y >= map.height()) {
        return false;
    }
    const std::uint64_t k = key(island, layer, x, y);
    if (const auto it = m_index.find(k); it != m_index.end()) {
        m_changes[it->second].after = cell;
        return true;
    }
    m_index.emplace(k, m_changes.size());
    m_changes.push_back(TileEdit::Change{island, layer, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                                         read_cell(map, layer, x, y), cell});
    return true;
}

std::optional<sim::MapCell> TileEditBuilder::cell(std::uint16_t island, Layer layer, std::uint32_t x,
                                                  std::uint32_t y) const {
    if (island >= m_world->islands().size()) {
        return std::nullopt;
    }
    const sim::IslandMap& map = m_world->islands()[island].map;
    if (x >= map.width() || y >= map.height()) {
        return std::nullopt;
    }
    if (const auto it = m_index.find(key(island, layer, x, y)); it != m_index.end()) {
        return m_changes[it->second].after;
    }
    return read_cell(map, layer, x, y);
}

std::unique_ptr<TileEdit> TileEditBuilder::finish() {
    std::vector<TileEdit::Change> changes;
    changes.reserve(m_changes.size());
    for (const TileEdit::Change& c : m_changes) {
        if (!same(c.before, c.after)) {
            changes.push_back(c);
        }
    }
    m_changes.clear();
    m_index.clear();
    if (changes.empty()) {
        return nullptr;
    }
    return std::make_unique<TileEdit>(std::move(changes));
}

std::size_t ScenarioInfoEdit::memory() const noexcept {
    return sizeof(*this) + info_memory(m_before) + info_memory(m_after);
}

std::size_t MarkersEdit::memory() const noexcept {
    return sizeof(*this) + markers_memory(m_before) + markers_memory(m_after);
}

void Steps::apply(sim::World& world) {
    for (const std::unique_ptr<Operation>& op : m_steps) {
        op->apply(world);
    }
}

void Steps::revert(sim::World& world) {
    for (auto it = m_steps.rbegin(); it != m_steps.rend(); ++it) {
        (*it)->revert(world);
    }
}

std::size_t Steps::memory() const noexcept {
    std::size_t total = sizeof(*this) + m_kind.size();
    for (const std::unique_ptr<Operation>& op : m_steps) {
        total += op->memory();
    }
    return total;
}

}  // namespace o1602::editor
