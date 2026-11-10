// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>
#include "o1602/sim/world.hpp"

namespace o1602::render {

/// Tracks exactly the inputs of the minimap's island pixels. A changed palette,
/// discovery state, local player or map block invalidates its affected pixels.
/// The caller keeps the existing round-robin/update schedule and colour rules.
class MinimapTracker {
public:
    using Colour = std::array<std::uint8_t, 4>;
    using Palette = std::vector<std::optional<Colour>>;
    void reset() { m_stamps.clear(); m_palette.clear(); }
    template<class Update>
    bool refresh(const sim::World& world, std::size_t island, sim::PlayerId local,
                 const Palette& palette, Update update) {
        if (palette != m_palette) {
            m_palette = palette;
            m_stamps.clear();
        }
        m_stamps.resize(world.islands().size());
        const auto& is = world.islands()[island];
        const auto& map = is.map;
        const Key key{is.x, is.y, map.width(), map.height(), local,
                      local == sim::kNoPlayer || is.discovered_by(local)};
        auto& stamp = m_stamps[island];
        const bool all = !stamp.valid || key != stamp.key;
        stamp.revisions.resize(std::size_t{map.blocks_x()} * map.blocks_y());
        bool changed = false;
        for (std::uint32_t by = 0; by < map.blocks_y(); ++by) {
            for (std::uint32_t bx = 0; bx < map.blocks_x(); ++bx) {
                const auto block = std::size_t{by} * map.blocks_x() + bx;
                const auto revision = map.block_revision(block);
                if (!all && revision == stamp.revisions[block])
                    continue;
                const auto x = bx * sim::IslandMap::kBlockSide;
                const auto y = by * sim::IslandMap::kBlockSide;
                changed = update(x, y, std::min(map.width(), x + sim::IslandMap::kBlockSide),
                                  std::min(map.height(), y + sim::IslandMap::kBlockSide)) || changed;
                stamp.revisions[block] = revision;
            }
        }
        stamp.key = key;
        stamp.valid = true;
        return changed;
    }
private:
    using Key = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, sim::PlayerId, bool>;
    struct Stamp { Key key{}; bool valid = false; std::vector<std::uint64_t> revisions; };
    Palette m_palette;
    std::vector<Stamp> m_stamps;
};

}  // namespace o1602::render
