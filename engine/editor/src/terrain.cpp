// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/terrain.hpp"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <utility>

#include "o1602/assets/game_data.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/island_shapes.hpp"

namespace o1602::editor {

namespace {

using core::ErrorCode;
using core::make_error;

constexpr std::uint64_t tile_key(std::uint32_t x, std::uint32_t y) noexcept {
    return std::uint64_t{x} << 32 | y;
}

/// The island a world tile belongs to and its tile there.
struct IslandTile {
    std::uint16_t island = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

std::optional<IslandTile> island_tile(const sim::World& world, std::int64_t x, std::int64_t y) {
    if (x < 0 || y < 0 || x >= world.width() || y >= world.height()) {
        return std::nullopt;
    }
    const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (c.island == sim::World::kSea) {
        return std::nullopt;
    }
    return IslandTile{c.island, c.local_x, c.local_y};
}

const assets::BuildingType* record_of(const assets::BuildingTable& buildings, const sim::MapCell& cell) {
    return cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
}

bool one_tile(const assets::BuildingType& b) noexcept {
    return b.size_x == 1 && b.size_y == 1;
}

/// A splitmix64 step: the next number of a sequence from `x`.
std::uint64_t mix(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/// Whether a tile is water for the coast: the sea, the surf and the surf before a river mouth;
/// an empty tile too.
bool water(const assets::BuildingTable& buildings, const sim::MapCell& cell) {
    const assets::BuildingType* b = record_of(buildings, cell);
    if (b == nullptr) {
        return true;
    }
    namespace kind = assets::kind;
    return b->kind == kind::kSea || b->kind == kind::kSurf || b->kind == kind::kSurfCorner ||
           b->kind == kind::kRiverMouth;
}

// Directions 0..7: N, NE, E, SE, S, SW, W, NW (sim::shapes::coast_tile's neighbour bits).
constexpr std::array<int, 8> kDirX = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr std::array<int, 8> kDirY = {-1, -1, 0, 1, 1, 1, 0, -1};

template <std::size_t N>
bool listed(const std::array<std::uint32_t, N>& ids, std::uint32_t id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

/// One of `ids` drawn by `random`, of those the table has (the next one if not).
template <std::size_t N>
std::uint32_t drawn(const assets::BuildingTable& buildings, const std::array<std::uint32_t, N>& ids,
                    std::uint64_t random) {
    for (std::size_t k = 0; k < N; ++k) {
        const std::uint32_t id = ids[(random + k) % N];
        if (buildings.by_id(id) != nullptr) {
            return id;
        }
    }
    return 0;
}

}  // namespace

std::optional<TerrainGroup> terrain_group(const assets::BuildingTable& buildings,
                                          const assets::BuildingType& building) noexcept {
    namespace kind = assets::kind;
    std::optional<TerrainGroup> group;
    switch (building.kind) {
        case kind::kGround:
            group = TerrainGroup::Ground;
            break;
        case kind::kForest:
            group = TerrainGroup::Forest;
            break;
        case kind::kRock:
            group = TerrainGroup::Rock;
            break;
        case kind::kSlope:
        case kind::kSlopeCorner:
        case kind::kSlopeSpring:
            group = TerrainGroup::Slope;
            break;
        case kind::kRiver:
        case kind::kRiverCorner:
        case kind::kRiverMouth:
        case kind::kBeachMouth:
            group = TerrainGroup::River;
            break;
        case kind::kBeach:
        case kind::kBeachOuter:
        case kind::kBeachInner:
        case kind::kBeachVariant:
            group = TerrainGroup::Beach;
            break;
        case kind::kSurf:
        case kind::kSurfCorner:
            group = TerrainGroup::Coast;
            break;
        case kind::kSea:
            group = TerrainGroup::Sea;
            break;
        default:
            return std::nullopt;
    }
    // A crop field, a road or a building of these kinds is a city's, not the island's.
    if (sim::territory_building(buildings, building)) {
        return std::nullopt;
    }
    return group;
}

std::uint8_t terrain_level(const assets::BuildingType& building) noexcept {
    namespace kind = assets::kind;
    switch (building.kind) {
        case kind::kSea:
        case kind::kSurf:
        case kind::kSurfCorner:
        case kind::kRiverMouth:
            return 0;
        case kind::kBeach:
        case kind::kBeachMouth:
        case kind::kBeachOuter:
        case kind::kBeachInner:
        case kind::kBeachVariant:
            return 1;
        default:
            return 2;
    }
}

bool is_terrain(const assets::BuildingTable& buildings, const assets::BuildingType& building) noexcept {
    return terrain_group(buildings, building).has_value();
}

std::vector<const assets::BuildingType*> terrain_pieces(const assets::BuildingTable& buildings, TerrainGroup group) {
    std::vector<const assets::BuildingType*> out;
    for (const assets::BuildingType& b : buildings.types()) {
        if (b.size_x > 0 && b.size_y > 0 && terrain_group(buildings, b) == group) {
            out.push_back(&b);
        }
    }
    std::sort(out.begin(), out.end(), [](const auto* a, const auto* b) { return a->id < b->id; });
    return out;
}

void TerrainEdit::apply(sim::World& world) {
    if (m_tiles) {
        m_tiles->apply(world);
    }
    for (const Features& f : m_features) {
        sim::WorldIsland& island = world.island(f.island);
        island.header.deposits = f.deposits_after;
        island.header.craters = f.header_craters_after;
        island.craters = f.craters_after;
    }
}

void TerrainEdit::revert(sim::World& world) {
    for (const Features& f : m_features) {
        sim::WorldIsland& island = world.island(f.island);
        island.header.deposits = f.deposits_before;
        island.header.craters = f.header_craters_before;
        island.craters = f.craters_before;
    }
    if (m_tiles) {
        m_tiles->revert(world);
    }
}

std::size_t TerrainEdit::memory() const noexcept {
    std::size_t n = sizeof(*this) + (m_tiles ? m_tiles->memory() : 0) + m_features.capacity() * sizeof(Features);
    for (const Features& f : m_features) {
        n += (f.deposits_before.capacity() + f.deposits_after.capacity()) * sizeof(assets::IslandHeader::Deposit) +
             (f.header_craters_before.capacity() + f.header_craters_after.capacity()) *
                 sizeof(assets::IslandHeader::Crater) +
             (f.craters_before.capacity() + f.craters_after.capacity()) * sizeof(sim::Crater);
    }
    return n;
}

TerrainStroke::TerrainStroke(sim::World& world, const assets::BuildingTable& buildings)
    : m_world(&world), m_buildings(&buildings), m_builder(world), m_units(world.islands().size()) {}

const std::unordered_set<std::uint64_t>& TerrainStroke::units_on(std::uint16_t island) {
    std::optional<std::unordered_set<std::uint64_t>>& set = m_units[island];
    if (!set) {
        set.emplace();
        const sim::WorldIsland& isl = m_world->islands()[island];
        const auto add = [&](std::int64_t wx, std::int64_t wy) {
            const std::int64_t lx = wx - isl.x;
            const std::int64_t ly = wy - isl.y;
            if (lx >= 0 && ly >= 0 && lx < isl.map.width() && ly < isl.map.height()) {
                set->insert(tile_key(static_cast<std::uint32_t>(lx), static_cast<std::uint32_t>(ly)));
            }
        };
        for (const sim::Ship& ship : m_world->ships()) {
            add(ship.x, ship.y);
        }
        for (const sim::Soldier& soldier : m_world->soldiers()) {
            if (soldier.pos_x >= 0 && soldier.pos_y >= 0) {
                add(soldier.pos_x / 4096, soldier.pos_y / 4096);
            }
        }
    }
    return *set;
}

TerrainStroke::Refusal TerrainStroke::refusal(std::uint16_t island, std::int64_t x, std::int64_t y) {
    if (!m_world->simulation_state().empty()) {
        return Refusal::RunningGame;
    }
    if (island >= m_world->islands().size()) {
        return Refusal::NoIsland;
    }
    const sim::WorldIsland& isl = m_world->islands()[island];
    if (x < 0 || y < 0 || x >= isl.map.width() || y >= isl.map.height()) {
        return Refusal::NoIsland;
    }
    const auto lx = static_cast<std::uint32_t>(x);
    const auto ly = static_cast<std::uint32_t>(y);
    // Where two islands' rectangles overlap, the tile is the one island's whose it is on the map.
    const sim::World::Cell& c = m_world->cell(isl.x + lx, isl.y + ly);
    if (c.island != island) {
        return Refusal::NoIsland;
    }
    const sim::MapCell& top = isl.map.top(lx, ly);
    if (!top.empty()) {
        const assets::BuildingType* b = record_of(*m_buildings, top);
        if (b == nullptr || !is_terrain(*m_buildings, *b)) {
            return Refusal::NotTerrain;
        }
    }
    if (top.city != sim::MapCell::kNoCity) {
        return Refusal::City;
    }
    if (units_on(island).contains(tile_key(lx, ly))) {
        return Refusal::Unit;
    }
    return Refusal::Free;
}

void TerrainStroke::note(Refusal refusal) noexcept {
    if (m_first_refusal == Refusal::Free) {
        m_first_refusal = refusal;
    }
}

void TerrainStroke::put(std::uint16_t island, std::uint32_t x, std::uint32_t y, const sim::MapCell& cell) {
    (void)m_builder.set(island, Layer::Top, x, y, cell);
    (void)m_builder.set(island, Layer::Ground, x, y, cell);
    sim::IslandMap& map = m_world->island(island).map;
    map.set_top_cell(x, y, cell);
    map.set_ground_cell(x, y, cell);
    m_changed = true;
}

void TerrainStroke::keep_features(std::uint16_t island) {
    if (std::any_of(m_features.begin(), m_features.end(),
                    [island](const TerrainEdit::Features& f) { return f.island == island; })) {
        return;
    }
    const sim::WorldIsland& isl = m_world->islands()[island];
    TerrainEdit::Features f;
    f.island = island;
    f.deposits_before = isl.header.deposits;
    f.header_craters_before = isl.header.craters;
    f.craters_before = isl.craters;
    m_features.push_back(std::move(f));
}

void TerrainStroke::remove_object(std::uint16_t island, const sim::SubTile& object) {
    sim::WorldIsland& isl = m_world->island(island);
    const assets::BuildingType* meadow = m_buildings->by_id(assets::id::kMeadow);
    const sim::MapCell origin_cell =
        isl.map.top(static_cast<std::uint32_t>(object.origin_x), static_cast<std::uint32_t>(object.origin_y));
    // The rotation the object was laid with is that of each of its tiles.
    sim::for_each_footprint(*object.building, object.origin_x, object.origin_y, origin_cell.rotation,
                            [&](std::int32_t tx, std::int32_t ty, std::uint32_t) {
                                if (!isl.map.contains(tx, ty)) {
                                    return;
                                }
                                const auto ux = static_cast<std::uint32_t>(tx);
                                const auto uy = static_cast<std::uint32_t>(ty);
                                const sim::MapCell& was = isl.map.top(ux, uy);
                                sim::MapCell cell;
                                cell.sprite =
                                    meadow != nullptr ? static_cast<std::uint16_t>(meadow->gfx) : sim::MapCell::kEmpty;
                                cell.owner = was.owner;
                                cell.city = was.city;
                                put(island, ux, uy, cell);
                            });
    // Its deposit and crater go with it.
    const auto at = [&](const auto& feature) { return feature.x == object.origin_x && feature.y == object.origin_y; };
    const bool deposit = std::any_of(isl.header.deposits.begin(), isl.header.deposits.end(), at);
    const bool crater = std::any_of(isl.craters.begin(), isl.craters.end(), at) ||
                        std::any_of(isl.header.craters.begin(), isl.header.craters.end(), at);
    if (deposit || crater) {
        keep_features(island);
        std::erase_if(isl.header.deposits, at);
        std::erase_if(isl.craters, at);
        std::erase_if(isl.header.craters, at);
    }
}

bool TerrainStroke::stamp(std::uint16_t island, std::int64_t x, std::int64_t y, const assets::BuildingType& piece,
                          std::uint8_t rotation, std::uint8_t phase) {
    rotation &= 3U;
    if (island >= m_world->islands().size() || x < 0 || y < 0) {
        note(Refusal::NoIsland);
        return false;
    }
    std::uint32_t fw = 0;
    std::uint32_t fh = 0;
    sim::rotated_size(piece, rotation, fw, fh);
    const sim::IslandMap& map = m_world->islands()[island].map;
    if (x + fw > map.width() || y + fh > map.height()) {
        note(Refusal::NoIsland);
        return false;
    }
    const std::uint8_t level = terrain_level(piece);
    for (std::uint32_t j = 0; j < fh; ++j) {
        for (std::uint32_t i = 0; i < fw; ++i) {
            if (const Refusal r = refusal(island, x + i, y + j); r != Refusal::Free) {
                note(r);
                return false;
            }
            const assets::BuildingType* now =
                record_of(*m_buildings, map.top(static_cast<std::uint32_t>(x + i), static_cast<std::uint32_t>(y + j)));
            if (!m_shaping && (now != nullptr ? terrain_level(*now) : 0) != level) {
                note(Refusal::Level);
                return false;
            }
        }
    }
    const auto ox = static_cast<std::int32_t>(x);
    const auto oy = static_cast<std::int32_t>(y);
    // The same piece laid where it lies already: nothing changes (its deposit stays).
    {
        const sim::SubTile t = map.sub_tile(*m_buildings, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        const sim::MapCell& top = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        if (t.building == &piece && t.origin_x == ox && t.origin_y == oy && (top.rotation & 3U) == rotation &&
            (top.anim_phase & 0xFU) == (phase & 0xFU)) {
            return true;
        }
    }
    // Pieces of several tiles that the footprint covers go whole, the rest of them as meadow.
    for (std::uint32_t j = 0; j < fh; ++j) {
        for (std::uint32_t i = 0; i < fw; ++i) {
            const sim::SubTile t = m_world->islands()[island].map.sub_tile(
                *m_buildings, static_cast<std::uint32_t>(x) + i, static_cast<std::uint32_t>(y) + j);
            if (t.building != nullptr && !one_tile(*t.building)) {
                remove_object(island, t);
            }
        }
    }
    sim::WorldIsland& isl = m_world->island(island);
    sim::for_each_footprint(piece, ox, oy, rotation, [&](std::int32_t tx, std::int32_t ty, std::uint32_t k) {
        const auto ux = static_cast<std::uint32_t>(tx);
        const auto uy = static_cast<std::uint32_t>(ty);
        const sim::MapCell& was = isl.map.top(ux, uy);
        sim::MapCell cell;
        cell.sprite = static_cast<std::uint16_t>(piece.gfx + k);
        cell.rotation = rotation;
        cell.anim_phase = static_cast<std::uint8_t>(phase & 0xFU);
        cell.owner = was.owner;
        cell.city = was.city;
        put(island, ux, uy, cell);
    });
    return true;
}

bool TerrainStroke::shape(std::uint16_t island, std::span<const std::pair<std::uint32_t, std::uint32_t>> tiles,
                          bool land, std::uint64_t seed) {
    namespace ids = assets::id::island_shape;
    using sim::shapes::Coast;
    if (island >= m_world->islands().size() || tiles.empty()) {
        return false;
    }
    const sim::IslandMap& map = m_world->islands()[island].map;
    const auto w = static_cast<std::int64_t>(map.width());
    const auto h = static_cast<std::int64_t>(map.height());
    // The rectangle worked on: the tiles and a margin, whose outer ring keeps its kind so that
    // the coast beyond it stays as it is.
    constexpr std::int64_t kMargin = 4;
    std::int64_t x0 = w;
    std::int64_t y0 = h;
    std::int64_t x1 = -1;
    std::int64_t y1 = -1;
    for (const auto& [tx, ty] : tiles) {
        x0 = std::min<std::int64_t>(x0, tx);
        y0 = std::min<std::int64_t>(y0, ty);
        x1 = std::max<std::int64_t>(x1, tx);
        y1 = std::max<std::int64_t>(y1, ty);
    }
    if (x1 < 0) {
        return false;
    }
    x0 = std::max<std::int64_t>(0, x0 - kMargin);
    y0 = std::max<std::int64_t>(0, y0 - kMargin);
    x1 = std::min<std::int64_t>(w - 1, x1 + kMargin);
    y1 = std::min<std::int64_t>(h - 1, y1 + kMargin);
    const std::int64_t rw = x1 - x0 + 1;
    const std::int64_t rh = y1 - y0 + 1;
    const auto at = [&](std::int64_t x, std::int64_t y) { return static_cast<std::size_t>((y - y0) * rw + (x - x0)); };
    // 1 land, 0 water; `fixed`: a tile that cannot change (a building, the ring, the edge).
    std::vector<std::uint8_t> grid(static_cast<std::size_t>(rw * rh));
    std::vector<std::uint8_t> fixed(grid.size());
    std::vector<std::uint8_t> brushed(grid.size());
    for (std::int64_t y = y0; y <= y1; ++y) {
        for (std::int64_t x = x0; x <= x1; ++x) {
            const auto ux = static_cast<std::uint32_t>(x);
            const auto uy = static_cast<std::uint32_t>(y);
            grid[at(x, y)] = water(*m_buildings, map.top(ux, uy)) ? 0 : 1;
            const bool ring =
                (x == x0 && x0 > 0) || (y == y0 && y0 > 0) || (x == x1 && x1 < w - 1) || (y == y1 && y1 < h - 1);
            fixed[at(x, y)] = ring || refusal(island, x, y) != Refusal::Free ? 1 : 0;
        }
    }
    // Land keeps a tile of water to the island's edge, for the surf.
    const auto may_be = [&](std::int64_t x, std::int64_t y, bool l) {
        if (fixed[at(x, y)] != 0) {
            return false;
        }
        return !l || (x > 0 && y > 0 && x < w - 1 && y < h - 1);
    };
    for (const auto& [tx, ty] : tiles) {
        if (tx < x0 || ty < y0 || tx > x1 || ty > y1) {
            continue;
        }
        if (!may_be(tx, ty, land)) {
            note(refusal(island, tx, ty) != Refusal::Free ? refusal(island, tx, ty) : Refusal::Edge);
            continue;
        }
        grid[at(tx, ty)] = land ? 1 : 0;
        brushed[at(tx, ty)] = 1;
    }
    const auto is_land = [&](std::int64_t x, std::int64_t y) -> bool {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return false;
        }
        if (x < x0 || y < y0 || x > x1 || y > y1) {
            return !water(*m_buildings, map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
        }
        return grid[at(x, y)] != 0;
    };
    const auto neighbours = [&](std::int64_t x, std::int64_t y) {
        std::uint8_t n = 0;
        for (std::size_t d = 0; d < 8; ++d) {
            if (is_land(x + kDirX[d], y + kDirY[d])) {
                n = static_cast<std::uint8_t>(n | (1U << d));
            }
        }
        return n;
    };
    const auto fits = [&](std::int64_t x, std::int64_t y) {
        return sim::shapes::coast_tile(is_land(x, y), neighbours(x, y)).has_value();
    };
    // Shapes the game has no tiles for: the tiles about them take the brush's kind; a tile of the
    // brush's kind not brushed gives way as a last resort.
    for (std::uint32_t pass = 0; pass < 16; ++pass) {
        bool changed = false;
        for (std::int64_t y = y0; y <= y1; ++y) {
            for (std::int64_t x = x0; x <= x1; ++x) {
                if (fits(x, y)) {
                    continue;
                }
                const bool own = is_land(x, y) == land;
                if (!own && may_be(x, y, land)) {
                    grid[at(x, y)] = land ? 1 : 0;
                    changed = true;
                    continue;
                }
                // The tile is of the brush's kind: its neighbours that are not take it.
                for (std::size_t d = 0; d < 8; ++d) {
                    const std::int64_t nx = x + kDirX[d];
                    const std::int64_t ny = y + kDirY[d];
                    if (nx >= x0 && ny >= y0 && nx <= x1 && ny <= y1 && is_land(nx, ny) != land &&
                        may_be(nx, ny, land)) {
                        grid[at(nx, ny)] = land ? 1 : 0;
                        changed = true;
                    }
                }
                if (!changed && own && brushed[at(x, y)] == 0 && may_be(x, y, !land)) {
                    grid[at(x, y)] = land ? 0 : 1;
                    changed = true;
                }
            }
        }
        if (!changed) {
            break;
        }
    }
    // The tiles laid anew where their kind or their coast changed.
    const bool before = m_changed;
    m_changed = false;
    m_shaping = true;
    for (std::int64_t y = y0; y <= y1; ++y) {
        for (std::int64_t x = x0; x <= x1; ++x) {
            if (fixed[at(x, y)] != 0) {
                continue;
            }
            const bool l = is_land(x, y);
            const auto coast = sim::shapes::coast_tile(l, neighbours(x, y));
            const auto ux = static_cast<std::uint32_t>(x);
            const auto uy = static_cast<std::uint32_t>(y);
            const sim::MapCell& cell = m_world->islands()[island].map.top(ux, uy);
            const assets::BuildingType* now = record_of(*m_buildings, cell);
            const std::uint32_t id = now != nullptr ? now->id : 0;
            const std::uint8_t turn = cell.rotation & 3U;
            const std::uint64_t r = mix(seed ^ (std::uint64_t{ux} << 32 | uy));
            const auto phase = static_cast<std::uint8_t>((r >> 20) % 6);
            std::uint32_t lay = 0;
            std::uint8_t rotation = 0;
            if (!coast) {
                // Still a shape without tiles (between fixed tiles): plain ground or sea.
                if (l == !water(*m_buildings, cell)) {
                    continue;
                }
                lay = l ? assets::id::kMeadow : ids::kLightSea;
            } else {
                rotation = coast->rotation;
                const auto keep = [&](bool same) { return same && turn == rotation; };
                switch (coast->coast) {
                    case Coast::Inland: {
                        const auto g = now != nullptr ? terrain_group(*m_buildings, *now) : std::nullopt;
                        const bool coastal = g == TerrainGroup::Beach || g == TerrainGroup::Coast ||
                                             g == TerrainGroup::Sea || (now != nullptr && water(*m_buildings, cell));
                        if (now != nullptr && !coastal) {
                            continue;
                        }
                        lay = assets::id::kMeadow;
                        rotation = 0;
                        break;
                    }
                    case Coast::Beach:
                        if (keep(listed(ids::kBeach, id) ||
                                 (now != nullptr && (now->kind == assets::kind::kBeachMouth ||
                                                     now->kind == assets::kind::kBeachVariant)))) {
                            continue;
                        }
                        lay = drawn(*m_buildings, ids::kBeach, r);
                        break;
                    case Coast::OuterCorner:
                        if (keep(listed(ids::kOuterCorner, id))) {
                            continue;
                        }
                        lay = drawn(*m_buildings, ids::kOuterCorner, r);
                        break;
                    case Coast::InnerCorner:
                        if (keep(listed(ids::kInnerCorner, id))) {
                            continue;
                        }
                        lay = drawn(*m_buildings, ids::kInnerCorner, r);
                        break;
                    case Coast::OpenSea:
                        if (now != nullptr && now->kind == assets::kind::kSea) {
                            continue;
                        }
                        lay = ids::kLightSea;
                        rotation = 0;
                        break;
                    case Coast::Surf:
                        if (keep(listed(ids::kSurf, id) ||
                                 (now != nullptr && now->kind == assets::kind::kRiverMouth))) {
                            continue;
                        }
                        lay = drawn(*m_buildings, ids::kSurf, r);
                        break;
                    case Coast::SurfCorner:
                        if (keep(listed(ids::kSurfCorner, id))) {
                            continue;
                        }
                        lay = drawn(*m_buildings, ids::kSurfCorner, r);
                        break;
                    case Coast::SurfBay:
                        if (keep(listed(ids::kSurfBay, id))) {
                            continue;
                        }
                        lay = drawn(*m_buildings, ids::kSurfBay, r);
                        break;
                }
            }
            if (const assets::BuildingType* b = m_buildings->by_id(lay)) {
                const bool sea = !l;
                (void)stamp(island, x, y, *b, rotation, sea ? phase : 0);
            }
        }
    }
    // The open sea about the new coast by its depth.
    constexpr std::int64_t kDepth = 6;
    for (const SeaTile& t :
         sea_depth(*m_world, *m_buildings, island, x0 - kDepth, y0 - kDepth, x1 + kDepth, y1 + kDepth, seed)) {
        if (const assets::BuildingType* b = m_buildings->by_id(t.id)) {
            (void)stamp(island, t.x, t.y, *b, t.rotation, t.phase);
        }
    }
    m_shaping = false;
    const bool now_changed = m_changed;
    m_changed = before || now_changed;
    return now_changed;
}

std::vector<SeaTile> sea_depth(const sim::World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                               std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1, std::uint64_t seed) {
    namespace ids = assets::id::island_shape;
    std::vector<SeaTile> out;
    if (island >= world.islands().size()) {
        return out;
    }
    const sim::WorldIsland& isl = world.islands()[island];
    const sim::IslandMap& map = isl.map;
    const auto w = static_cast<std::int64_t>(map.width());
    const auto h = static_cast<std::int64_t>(map.height());
    x0 = std::max<std::int64_t>(0, x0);
    y0 = std::max<std::int64_t>(0, y0);
    x1 = std::min<std::int64_t>(w - 1, x1);
    y1 = std::min<std::int64_t>(h - 1, y1);
    if (x0 > x1 || y0 > y1) {
        return out;
    }
    constexpr int kShore = 0;  // land, surf and any other tile that is not open sea
    constexpr int kLight = 1;
    constexpr int kMiddle = 2;
    constexpr int kDark = 3;
    constexpr std::int64_t kFar = 7;
    const auto inside = [&](std::int64_t x, std::int64_t y) { return x >= 0 && y >= 0 && x < w && y < h; };
    const auto cell = [&](std::int64_t x, std::int64_t y) -> const sim::MapCell& {
        return map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    };
    const auto land = [&](std::int64_t x, std::int64_t y) { return inside(x, y) && !water(buildings, cell(x, y)); };
    // The band a tile has now: by its record, the dark sea beyond the island.
    const auto band_now = [&](std::int64_t x, std::int64_t y) -> int {
        if (!inside(x, y)) {
            return kDark;
        }
        const assets::BuildingType* b = record_of(buildings, cell(x, y));
        if (b == nullptr) {
            return kDark;
        }
        switch (b->id) {
            case ids::kLightSea:
                return kLight;
            case ids::kLightEdge:
            case ids::kMiddleSea:
                return kMiddle;
            case ids::kDarkEdge:
            case ids::kDarkSea:
                return kDark;
            default:
                return kShore;
        }
    };
    const auto open = [&](std::int64_t x, std::int64_t y) {
        if (!inside(x, y) || band_now(x, y) == kShore) {
            return false;
        }
        for (std::size_t d = 0; d < 8; ++d) {
            if (land(x + kDirX[d], y + kDirY[d])) {
                return false;
            }
        }
        return true;
    };
    // The distance to the nearest land (Chebyshev, as the bands of the templates run).
    const auto distance = [&](std::int64_t x, std::int64_t y) {
        for (std::int64_t r = 1; r < kFar; ++r) {
            for (std::int64_t k = -r; k <= r; ++k) {
                if (land(x + k, y - r) || land(x + k, y + r) || land(x - r, y + k) || land(x + r, y + k)) {
                    return r;
                }
            }
        }
        return kFar;
    };
    // Value noise on a lattice of 5 tiles, 0..1023.
    const auto wobble = [](std::int64_t x, std::int64_t y) {
        constexpr std::int64_t kSpacing = 5;
        const auto floor_div = [](std::int64_t a) { return a >= 0 ? a / kSpacing : -((-a + kSpacing - 1) / kSpacing); };
        const std::int64_t gx = floor_div(x);
        const std::int64_t gy = floor_div(y);
        const std::int64_t fx = x - gx * kSpacing;
        const std::int64_t fy = y - gy * kSpacing;
        const auto corner = [](std::int64_t cx, std::int64_t cy) {
            return static_cast<std::int64_t>(
                mix(0x1602'5EA0ULL ^ tile_key(static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy))) % 1024U);
        };
        const std::int64_t top = corner(gx, gy) * (kSpacing - fx) + corner(gx + 1, gy) * fx;
        const std::int64_t bottom = corner(gx, gy + 1) * (kSpacing - fx) + corner(gx + 1, gy + 1) * fx;
        return static_cast<int>((top * (kSpacing - fy) + bottom * fy) / (kSpacing * kSpacing));
    };
    const auto band_of = [](std::int64_t level) { return level <= 2 ? kLight : level <= 4 ? kMiddle : kDark; };
    // The bands wanted over the window and a tile around it.
    const std::int64_t gx0 = x0 - 1;
    const std::int64_t gy0 = y0 - 1;
    const std::int64_t gw = x1 - x0 + 3;
    const std::int64_t gh = y1 - y0 + 3;
    const auto at = [&](std::int64_t x, std::int64_t y) {
        return static_cast<std::size_t>((y - gy0) * gw + (x - gx0));
    };
    std::vector<std::uint8_t> band(static_cast<std::size_t>(gw * gh), kShore);
    std::vector<std::uint8_t> moved(band.size(), 0);
    for (std::int64_t y = gy0; y < gy0 + gh; ++y) {
        for (std::int64_t x = gx0; x < gx0 + gw; ++x) {
            if (!open(x, y)) {
                band[at(x, y)] = inside(x, y) ? kShore : kDark;
                continue;
            }
            const int now = band_now(x, y);
            band[at(x, y)] = static_cast<std::uint8_t>(now);
            if (x < x0 || y < y0 || x > x1 || y > y1) {
                continue;
            }
            // As the islands of their own shape: light two tiles out, middle to four, then dark,
            // deepening towards the rectangle's edge. A band may come a tile closer to the land,
            // as the templates' bands wander, but never lie deeper than that.
            // The bands' borders wander by a tile with a coarse noise of the world's tiles, the
            // same for every stroke, so that they do not run along the brush's straight edges.
            const std::int64_t d = distance(x, y);
            const std::int64_t rim = std::min({x, y, w - 1 - x, h - 1 - y});
            const std::int64_t least = 5 - 2 * rim;
            const int v = wobble(static_cast<std::int64_t>(isl.x) + x, static_cast<std::int64_t>(isl.y) + y);
            const std::int64_t level = d + (v < 340 ? -1 : v > 683 ? 1 : 0);
            int want = band_of(std::max(level, least));
            if (d == 2) {
                want = std::min(want, kMiddle);  // never dark next to the surf
            }
            const int lo = std::min(want, band_of(std::max(d - 1, least)));
            const int hi = want;
            if (now < lo || now > hi) {
                band[at(x, y)] = static_cast<std::uint8_t>(want);
                moved[at(x, y)] = 1;
            }
        }
    }
    const auto depth_at = [&](std::int64_t x, std::int64_t y) -> int { return band[at(x, y)]; };
    // The edge of a band: the lighter water on two sides next to each other, turned towards them.
    const auto edge = [&](std::int64_t x, std::int64_t y, int b) -> std::optional<std::uint8_t> {
        const bool n = depth_at(x, y - 1) < b;
        const bool e = depth_at(x + 1, y) < b;
        const bool s = depth_at(x, y + 1) < b;
        const bool wv = depth_at(x - 1, y) < b;
        if (s && wv && !n && !e) {
            return std::uint8_t{0};
        }
        if (n && wv && !s && !e) {
            return std::uint8_t{1};
        }
        if (n && e && !s && !wv) {
            return std::uint8_t{2};
        }
        if (e && s && !n && !wv) {
            return std::uint8_t{3};
        }
        return std::nullopt;
    };
    for (std::int64_t y = y0; y <= y1; ++y) {
        for (std::int64_t x = x0; x <= x1; ++x) {
            const int b = band[at(x, y)];
            if (b == kShore) {
                continue;
            }
            if (moved[at(x, y)] == 0 && moved[at(x, y - 1)] == 0 && moved[at(x + 1, y)] == 0 &&
                moved[at(x, y + 1)] == 0 && moved[at(x - 1, y)] == 0) {
                continue;
            }
            const sim::MapCell& now = cell(x, y);
            const assets::BuildingType* was = record_of(buildings, now);
            const std::uint64_t r = mix(seed ^ tile_key(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
            std::uint32_t id = ids::kLightSea;
            std::uint8_t rotation = 0;
            if (b == kMiddle) {
                if (const auto turn = edge(x, y, b)) {
                    id = ids::kLightEdge;
                    rotation = *turn;
                } else {
                    id = ids::kMiddleSea;
                    // The middle sea is turned at random: one turned already stays.
                    rotation = was != nullptr && was->id == id ? static_cast<std::uint8_t>(now.rotation & 3U)
                                                               : static_cast<std::uint8_t>(r % 4);
                }
            } else if (b == kDark) {
                if (const auto turn = edge(x, y, b)) {
                    id = ids::kDarkEdge;
                    rotation = *turn;
                } else {
                    id = ids::kDarkSea;
                }
            }
            if (was != nullptr && was->id == id && (now.rotation & 3U) == rotation) {
                continue;
            }
            const assets::BuildingType* piece = buildings.by_id(id);
            if (piece == nullptr) {
                continue;
            }
            const std::uint32_t phases = std::max<std::uint32_t>(piece->anim_count, 1);
            // The dark sea in the phase of the world's open sea, so that no seam shows.
            const std::uint64_t phase = id == ids::kDarkSea ? 3U * (isl.x + static_cast<std::uint64_t>(x)) + isl.y +
                                                                  static_cast<std::uint64_t>(y)
                                                            : (r >> 8);
            out.push_back(SeaTile{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), id, rotation,
                                  static_cast<std::uint8_t>(phase % phases & 0xFU)});
        }
    }
    return out;
}

bool TerrainStroke::stamp_world(std::int64_t x, std::int64_t y, const assets::BuildingType& piece,
                                std::uint8_t rotation, std::uint8_t phase) {
    const auto at = island_tile(*m_world, x, y);
    if (!at) {
        note(Refusal::NoIsland);
        return false;
    }
    return stamp(at->island, at->x, at->y, piece, rotation, phase);
}

std::unique_ptr<Operation> TerrainStroke::finish(std::string_view kind) {
    std::unique_ptr<TileEdit> tiles = m_builder.finish();
    std::vector<TerrainEdit::Features> features;
    for (TerrainEdit::Features& f : m_features) {
        const sim::WorldIsland& isl = m_world->islands()[f.island];
        f.deposits_after = isl.header.deposits;
        f.header_craters_after = isl.header.craters;
        f.craters_after = isl.craters;
        features.push_back(std::move(f));
    }
    m_features.clear();
    m_changed = false;
    m_first_refusal = Refusal::Free;
    if (!tiles && features.empty()) {
        return nullptr;
    }
    return std::make_unique<TerrainEdit>(std::move(tiles), std::move(features), kind);
}

void TerrainStroke::cancel() {
    if (std::unique_ptr<TileEdit> tiles = m_builder.finish()) {
        tiles->revert(*m_world);
    }
    for (const TerrainEdit::Features& f : m_features) {
        sim::WorldIsland& isl = m_world->island(f.island);
        isl.header.deposits = f.deposits_before;
        isl.header.craters = f.header_craters_before;
        isl.craters = f.craters_before;
    }
    m_features.clear();
    m_changed = false;
    m_first_refusal = Refusal::Free;
}

std::optional<TerrainPiece> terrain_at(const sim::World& world, const assets::BuildingTable& buildings, std::uint32_t x,
                                       std::uint32_t y) {
    const auto at = island_tile(world, x, y);
    if (!at) {
        return std::nullopt;
    }
    const sim::MapCell& top = world.islands()[at->island].map.top(at->x, at->y);
    const assets::BuildingType* b = record_of(buildings, top);
    if (b == nullptr || !is_terrain(buildings, *b)) {
        return std::nullopt;
    }
    return TerrainPiece{b->id, static_cast<std::uint8_t>(top.rotation & 3U)};
}

namespace {

/// The record of a piece of one tile, or an error.
core::Result<const assets::BuildingType*> single_piece(const assets::BuildingTable& buildings, TerrainPiece piece) {
    const assets::BuildingType* b = buildings.by_id(piece.id);
    if (b == nullptr || !is_terrain(buildings, *b)) {
        return make_error(ErrorCode::NotFound, "no terrain " + std::to_string(piece.id));
    }
    if (!one_tile(*b)) {
        return make_error(ErrorCode::Unsupported, "only a piece of one tile fills");
    }
    return b;
}

core::Result<std::unique_ptr<Operation>> stroke_result(TerrainStroke& stroke, std::string_view kind) {
    const TerrainStroke::Refusal refused = stroke.first_refusal();
    if (std::unique_ptr<Operation> op = stroke.finish(kind)) {
        return op;
    }
    if (refused != TerrainStroke::Refusal::Free) {
        return make_error(ErrorCode::Unsupported, "the tiles cannot be painted");
    }
    return make_error(ErrorCode::InvalidFormat, "nothing changes");
}

}  // namespace

core::Result<std::unique_ptr<Operation>> fill_terrain(sim::World& world, const assets::BuildingTable& buildings,
                                                      std::uint32_t x, std::uint32_t y, TerrainPiece piece) {
    auto b = single_piece(buildings, piece);
    if (!b) {
        return b.error();
    }
    const auto at = island_tile(world, x, y);
    if (!at) {
        return make_error(ErrorCode::NotFound, "no island there");
    }
    const sim::IslandMap& map = world.islands()[at->island].map;
    const sim::MapCell target = map.top(at->x, at->y);
    TerrainStroke stroke(world, buildings);
    if (stroke.refusal(at->island, at->x, at->y) != TerrainStroke::Refusal::Free) {
        (void)stroke.stamp(at->island, at->x, at->y, **b, piece.rotation);
        return stroke_result(stroke, "terrain_fill");
    }
    const auto same = [&](std::uint32_t tx, std::uint32_t ty) {
        const sim::MapCell& c = map.top(tx, ty);
        return c.sprite == target.sprite && (c.rotation & 3U) == (target.rotation & 3U);
    };
    const std::uint32_t w = map.width();
    std::vector<bool> seen(std::size_t{w} * map.height(), false);
    std::deque<std::pair<std::uint32_t, std::uint32_t>> open;
    open.emplace_back(at->x, at->y);
    seen[std::size_t{at->y} * w + at->x] = true;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> region;
    while (!open.empty()) {
        const auto [cx, cy] = open.front();
        open.pop_front();
        region.emplace_back(cx, cy);
        constexpr std::array<std::pair<int, int>, 4> kSides = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        for (const auto& [dx, dy] : kSides) {
            const std::int64_t nx = std::int64_t{cx} + dx;
            const std::int64_t ny = std::int64_t{cy} + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= map.height()) {
                continue;
            }
            const auto ux = static_cast<std::uint32_t>(nx);
            const auto uy = static_cast<std::uint32_t>(ny);
            if (seen[std::size_t{uy} * w + ux] || !same(ux, uy) ||
                stroke.refusal(at->island, ux, uy) != TerrainStroke::Refusal::Free) {
                continue;
            }
            seen[std::size_t{uy} * w + ux] = true;
            open.emplace_back(ux, uy);
        }
    }
    for (const auto& [tx, ty] : region) {
        (void)stroke.stamp(at->island, tx, ty, **b, piece.rotation);
    }
    return stroke_result(stroke, "terrain_fill");
}

core::Result<std::unique_ptr<Operation>> replace_terrain(sim::World& world, const assets::BuildingTable& buildings,
                                                         std::uint32_t x, std::uint32_t y, TerrainPiece piece) {
    auto b = single_piece(buildings, piece);
    if (!b) {
        return b.error();
    }
    const auto at = island_tile(world, x, y);
    if (!at) {
        return make_error(ErrorCode::NotFound, "no island there");
    }
    const sim::IslandMap& map = world.islands()[at->island].map;
    const sim::MapCell target = map.top(at->x, at->y);
    const assets::BuildingType* old = record_of(buildings, target);
    if (old == nullptr || !is_terrain(buildings, *old)) {
        return make_error(ErrorCode::Unsupported, "the tiles cannot be painted");
    }
    if (!one_tile(*old)) {
        return make_error(ErrorCode::Unsupported, "only a piece of one tile is replaced");
    }
    const bool turns = (*b)->rotate > 0;
    TerrainStroke stroke(world, buildings);
    for (std::uint32_t ty = 0; ty < map.height(); ++ty) {
        for (std::uint32_t tx = 0; tx < map.width(); ++tx) {
            const sim::MapCell& c = map.top(tx, ty);
            if (c.sprite != target.sprite) {
                continue;
            }
            const auto rotation = static_cast<std::uint8_t>(turns ? c.rotation & 3U : piece.rotation);
            (void)stroke.stamp(at->island, tx, ty, **b, rotation, c.anim_phase);
        }
    }
    return stroke_result(stroke, "terrain_replace");
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> brush_tiles(std::int64_t x, std::int64_t y, std::uint32_t size,
                                                                 std::uint32_t width, std::uint32_t height) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
    size = std::max<std::uint32_t>(size, 1);
    const std::int64_t left = x - (std::int64_t{size} - 1) / 2;
    const std::int64_t top = y - (std::int64_t{size} - 1) / 2;
    for (std::int64_t ty = top; ty < top + size; ++ty) {
        for (std::int64_t tx = left; tx < left + size; ++tx) {
            if (tx >= 0 && ty >= 0 && tx < width && ty < height) {
                out.emplace_back(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
            }
        }
    }
    return out;
}

std::vector<std::pair<std::int64_t, std::int64_t>> line_tiles(std::int64_t x0, std::int64_t y0, std::int64_t x1,
                                                              std::int64_t y1) {
    std::vector<std::pair<std::int64_t, std::int64_t>> out;
    const std::int64_t dx = std::abs(x1 - x0);
    const std::int64_t dy = std::abs(y1 - y0);
    const std::int64_t sx = x0 < x1 ? 1 : -1;
    const std::int64_t sy = y0 < y1 ? 1 : -1;
    out.emplace_back(x0, y0);
    // One axis per step, so that the tiles touch by sides (a brush of one tile leaves no gap): the
    // step whose next half-way point the line reaches first.
    for (std::int64_t ix = 0, iy = 0; ix < dx || iy < dy;) {
        if ((1 + 2 * ix) * dy < (1 + 2 * iy) * dx) {
            x0 += sx;
            ++ix;
        } else {
            y0 += sy;
            ++iy;
        }
        out.emplace_back(x0, y0);
    }
    return out;
}

std::optional<TerrainPiece> random_piece(const assets::BuildingTable& buildings, TerrainGroup group,
                                         std::uint8_t climate, std::uint64_t random) {
    namespace ids = assets::id::island_shape;
    const std::uint64_t r = mix(random);
    const auto turned = [&](const assets::BuildingType& b) {
        return TerrainPiece{b.id, static_cast<std::uint8_t>(b.rotate > 0 ? (r >> 40) & 3U : 0U)};
    };
    std::vector<std::pair<const assets::BuildingType*, std::uint32_t>> choice;
    if (group == TerrainGroup::Ground) {
        for (const auto& w : ids::kGroundMix) {
            if (const auto* b = buildings.by_id(w.id); b != nullptr && one_tile(*b)) {
                choice.emplace_back(b, w.weight);
            }
        }
    } else if (group == TerrainGroup::Forest) {
        const auto& forest = (climate & 1U) == 0 ? ids::kForestNorth : ids::kForestSouth;
        for (const std::uint32_t id : forest) {
            if (const auto* b = buildings.by_id(id); b != nullptr && one_tile(*b)) {
                choice.emplace_back(b, 1);
            }
        }
    }
    if (choice.empty()) {
        for (const auto* b : terrain_pieces(buildings, group)) {
            if (one_tile(*b)) {
                choice.emplace_back(b, 1);
            }
        }
    }
    std::uint64_t total = 0;
    for (const auto& c : choice) {
        total += c.second;
    }
    if (total == 0) {
        return std::nullopt;
    }
    std::uint64_t pick = (r & 0xFFFFFFFFULL) % total;
    for (const auto& [b, weight] : choice) {
        if (pick < weight) {
            return turned(*b);
        }
        pick -= weight;
    }
    return turned(*choice.back().first);
}

}  // namespace o1602::editor
