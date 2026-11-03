// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/terrain.hpp"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <utility>

#include "o1602/assets/game_data.hpp"
#include "o1602/sim/island_map.hpp"

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
    for (std::uint32_t j = 0; j < fh; ++j) {
        for (std::uint32_t i = 0; i < fw; ++i) {
            if (const Refusal r = refusal(island, x + i, y + j); r != Refusal::Free) {
                note(r);
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
