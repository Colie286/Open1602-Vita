// SPDX-License-Identifier: GPL-3.0-or-later
//
// The map: MAPW (own savegame) with the world's size and both layers of every island tile, TILE
// (chunk in a .GAM) with the tiles loading the original's chunks would not give back.
#include <algorithm>
#include <array>
#include <map>
#include <string>

#include "section_io.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/core/hash.hpp"

namespace o1602::sim::save {

using core::ErrorCode;
using core::make_error;

namespace {

/// A tile's picture in the file: the building's id and the part of it, 0 and 0 for none.
struct CellPicture {
    std::uint32_t id = 0;
    std::uint16_t sub = 0;
};

CellPicture picture_of(const assets::BuildingTable& buildings, const MapCell& c) {
    const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
    return b != nullptr ? CellPicture{b->id, static_cast<std::uint16_t>(c.sprite - b->gfx)} : CellPicture{};
}

/// A tile of TILE: the picture, then the other fields of the cell.
template <class A, class C>
void io_cell(A& a, C& c, const assets::BuildingTable& buildings) {
    CellPicture p;
    if constexpr (!A::kReading) {
        p = picture_of(buildings, c);
    }
    a.u32(p.id);
    a.u16(p.sub);
    a.u8(c.rotation);
    a.u8(c.anim_phase);
    a.u8(c.owner);
    a.u8(c.city);
    a.u8(c.random);
    if constexpr (A::kReading) {
        if (p.id != 0) {
            const assets::BuildingType* b = buildings.by_id(p.id);
            if (b == nullptr || p.sub >= b->tile_count()) {
                a.fail();  // the game's data changed, or the part lies outside the building
                return;
            }
            c.sprite = static_cast<std::uint16_t>(b->gfx + p.sub);
        }
    }
}

template <class A, class T>
void io_tile(A& a, T& t, const assets::BuildingTable& buildings) {
    a.u8(t.island);  // TILE is only in the chunk of a .GAM, whose islands are numbered below 256
    a.u16(t.x);
    a.u16(t.y);
    a.boolean(t.ground);
    io_cell(a, t.cell, buildings);
}

bool same_cell(const MapCell& a, const MapCell& b) noexcept {
    return a.sprite == b.sprite && a.rotation == b.rotation && a.anim_phase == b.anim_phase && a.owner == b.owner &&
           a.city == b.city && a.random == b.random;
}

/// What of an island header the game uses after loading. The rest of the header mirrors state the
/// island itself keeps (drought, craters, discovered by, ...), or fields of the original's file the
/// game does not use (`raw`); neither goes into Open1602's own savegame.
template <class A, class H>
void io_header(A& a, H& h, std::uint16_t version) {
    a.string(h.chunk_name);
    if (version >= kCompactMap) {
        a.u16(h.number);
        a.u16(h.width);
        a.u16(h.height);
    } else {
        a.u8(h.number);
        a.u8(h.width);
        a.u8(h.height);
    }
    a.u8(h.flags);
    a.u16(h.pos_x);
    a.u16(h.pos_y);
    a.u16(h.template_number);
    a.u16(h.template_size);
    a.u8(h.climate);
    a.boolean(h.stored_complete);
}

/// One layer of an island in MAPW, field by field over the island (all ids, then all parts, ...),
/// which zlib packs well. Rotation and animation phase share a byte.
struct Layer {
    std::vector<std::uint32_t> ids;
    std::vector<std::uint16_t> subs;
    std::vector<std::uint8_t> rotation_anim;
    std::vector<std::uint8_t> owner;
    std::vector<std::uint8_t> city;
    std::vector<std::uint8_t> random;

    explicit Layer(std::size_t n) : ids(n), subs(n), rotation_anim(n), owner(n), city(n), random(n) {}
};

/// A column of MAPW version 2: a byte 0 and the values as they are, or a byte 1, the number of
/// runs (u32) and each run as its length (u32) and value. The writer takes the shorter.
template <class T>
void put_column(core::ByteWriter& out, const std::vector<T>& v) {
    std::size_t runs = 0;
    for (std::size_t k = 0; k < v.size(); ++k) {
        runs += k == 0 || v[k] != v[k - 1] ? 1U : 0U;
    }
    if (runs * (4 + sizeof(T)) >= v.size() * sizeof(T)) {
        out.u8(0);
        if constexpr (sizeof(T) == 4) {
            out.u32s(v);
        } else if constexpr (sizeof(T) == 2) {
            out.u16s(v);
        } else {
            out.bytes(v);
        }
        return;
    }
    out.u8(1);
    out.u32(static_cast<std::uint32_t>(runs));
    for (std::size_t k = 0; k < v.size();) {
        std::size_t end = k + 1;
        while (end < v.size() && v[end] == v[k]) {
            ++end;
        }
        out.u32(static_cast<std::uint32_t>(end - k));
        if constexpr (sizeof(T) == 4) {
            out.u32(v[k]);
        } else if constexpr (sizeof(T) == 2) {
            out.u16(v[k]);
        } else {
            out.u8(v[k]);
        }
        k = end;
    }
}

template <class T>
void get_column(Reader& a, std::vector<T>& v) {
    std::uint8_t mode = 0;
    a.u8(mode);
    if (mode == 0) {
        for (auto& e : v) {
            if constexpr (sizeof(T) == 4) {
                a.u32(e);
            } else if constexpr (sizeof(T) == 2) {
                a.u16(e);
            } else {
                a.u8(e);
            }
        }
        return;
    }
    // Runs: as many as the bytes can hold, together exactly as long as the column.
    const std::uint32_t runs = mode == 1 ? a.in().count(4 + sizeof(T)) : 0;
    if (mode != 1 || runs > v.size()) {
        a.fail();
        return;
    }
    std::size_t at = 0;
    for (std::uint32_t r = 0; r < runs && a.ok(); ++r) {
        std::uint32_t length = 0;
        T value{};
        a.u32(length);
        if constexpr (sizeof(T) == 4) {
            a.u32(value);
        } else if constexpr (sizeof(T) == 2) {
            a.u16(value);
        } else {
            a.u8(value);
        }
        if (length == 0 || length > v.size() - at) {
            a.fail();
            return;
        }
        std::fill_n(v.begin() + static_cast<std::ptrdiff_t>(at), length, value);
        at += length;
    }
    if (at != v.size()) {
        a.fail();
    }
}

/// The columns of a layer in MAPW version 2 (kCompactMap): each as runs where that is shorter, as a
/// map is mostly long stretches of the same ground, owner and city.
template <class A, class L>
void io_compact_layer(A& a, L& layer) {
    if constexpr (!A::kReading) {
        core::ByteWriter& out = a.out();
        put_column(out, layer.ids);
        put_column(out, layer.subs);
        for (const auto* column : {&layer.rotation_anim, &layer.owner, &layer.city, &layer.random}) {
            put_column(out, *column);
        }
    } else {
        get_column(a, layer.ids);
        get_column(a, layer.subs);
        for (auto* column : {&layer.rotation_anim, &layer.owner, &layer.city, &layer.random}) {
            get_column(a, *column);
        }
    }
}

/// The columns of a layer in MAPW version 1, without a length: the island's size gives it.
template <class A, class L>
void io_layer(A& a, L& layer) {
    if constexpr (!A::kReading) {
        // Column by column in one go: the map is most of a savegame, and of the fingerprint of the
        // game that multiplayer takes every few seconds (Simulation::state_hash()).
        core::ByteWriter& out = a.out();
        out.u32s(layer.ids);
        out.u16s(layer.subs);
        for (const auto* column : {&layer.rotation_anim, &layer.owner, &layer.city, &layer.random}) {
            out.bytes(*column);
        }
    } else {
        for (auto& v : layer.ids) {
            a.u32(v);
        }
        for (auto& v : layer.subs) {
            a.u16(v);
        }
        for (auto* column : {&layer.rotation_anim, &layer.owner, &layer.city, &layer.random}) {
            for (auto& v : *column) {
                a.u8(v);
            }
        }
    }
}

Layer layer_of(const IslandMap& map, bool ground, const assets::BuildingTable& buildings) {
    Layer layer(std::size_t{map.width()} * map.height());
    std::size_t k = 0;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x, ++k) {
            const MapCell& c = ground ? map.ground(x, y) : map.top(x, y);
            const CellPicture p = picture_of(buildings, c);
            layer.ids[k] = p.id;
            layer.subs[k] = p.sub;
            layer.rotation_anim[k] = static_cast<std::uint8_t>(c.rotation | c.anim_phase << 2);
            layer.owner[k] = c.owner;
            layer.city[k] = c.city;
            layer.random[k] = c.random;
        }
    }
    return layer;
}

/// Puts a layer read into the map; an error names a building the game's data does not have.
core::Status set_layer(IslandMap& map, bool ground, const Layer& layer, const assets::BuildingTable& buildings) {
    const std::uint32_t w = map.width();
    for (std::size_t k = 0; k < layer.ids.size(); ++k) {
        MapCell c;
        const std::uint32_t id = layer.ids[k];
        if (id != 0) {
            const assets::BuildingType* b = buildings.by_id(id);
            if (b == nullptr) {
                return make_error(ErrorCode::InvalidFormat,
                                  "save: building " + std::to_string(id) + " is not in the game's data");
            }
            if (layer.subs[k] >= b->tile_count()) {
                return make_error(ErrorCode::InvalidFormat, "save: part " + std::to_string(layer.subs[k]) +
                                                                " outside building " + std::to_string(id));
            }
            c.sprite = static_cast<std::uint16_t>(b->gfx + layer.subs[k]);
        }
        c.rotation = static_cast<std::uint8_t>(layer.rotation_anim[k] & 3U);
        c.anim_phase = static_cast<std::uint8_t>(layer.rotation_anim[k] >> 2 & 0x0FU);
        c.owner = layer.owner[k];
        c.city = layer.city[k];
        c.random = layer.random[k];
        const auto x = static_cast<std::uint32_t>(k % w);
        const auto y = static_cast<std::uint32_t>(k / w);
        if (ground) {
            map.set_ground_cell(x, y, c);
        } else {
            map.set_top_cell(x, y, c);
        }
    }
    return core::Ok{};
}

}  // namespace

/// The whole map: the world's size and every island with both layers of all its tiles.
void write_map(Writer& out, const World& w, const assets::BuildingTable& buildings) {
    out.u32(w.width());
    out.u32(w.height());
    out.list(w.islands(), 64, [&](const WorldIsland& i) {
        io_header(out, i.header, kCompactMap);
        out.u32(i.x);
        out.u32(i.y);
        out.u32(i.map.width());
        out.u32(i.map.height());
        for (const bool ground : {false, true}) {
            const Layer layer = layer_of(i.map, ground, buildings);
            io_compact_layer(out, layer);
        }
    });
}

namespace {

/// The bytes of a block's tiles for its fingerprint, little endian: per tile of both layers the
/// building id (u32), sub-tile (u16), rotation and phase, owner, city and random value.
constexpr std::size_t kBlockBytes = std::size_t{IslandMap::kBlockSide} * IslandMap::kBlockSide * 2 * 10;

std::uint64_t block_hash(const IslandMap& map, std::size_t block, const assets::BuildingTable& buildings) {
    const auto left = static_cast<std::uint32_t>(block % map.blocks_x()) * IslandMap::kBlockSide;
    const auto top = static_cast<std::uint32_t>(block / map.blocks_x()) * IslandMap::kBlockSide;
    const std::uint32_t right = std::min(left + IslandMap::kBlockSide, map.width());
    const std::uint32_t bottom = std::min(top + IslandMap::kBlockSide, map.height());
    std::array<std::uint8_t, kBlockBytes> bytes;
    std::size_t n = 0;
    for (const bool ground : {false, true}) {
        for (std::uint32_t y = top; y < bottom; ++y) {
            for (std::uint32_t x = left; x < right; ++x) {
                const MapCell& c = ground ? map.ground(x, y) : map.top(x, y);
                const CellPicture p = picture_of(buildings, c);
                for (std::size_t k = 0; k < 4; ++k) {
                    bytes[n++] = static_cast<std::uint8_t>(p.id >> (8 * k));
                }
                bytes[n++] = static_cast<std::uint8_t>(p.sub);
                bytes[n++] = static_cast<std::uint8_t>(p.sub >> 8U);
                bytes[n++] = static_cast<std::uint8_t>(c.rotation | c.anim_phase << 2);
                bytes[n++] = c.owner;
                bytes[n++] = c.city;
                bytes[n++] = c.random;
            }
        }
    }
    return core::xxh64(std::span<const std::uint8_t>(bytes.data(), n));
}

}  // namespace

/// MAPW for the fingerprint of the game: as write_map, but each island's tiles as the XXH64 of each
/// block (IslandMap::kBlockSide tiles on a side), taken again only for the blocks that changed (TB-26).
void write_map_fingerprint(Writer& out, const World& w, const assets::BuildingTable& buildings,
                           std::vector<std::vector<BlockHash>>& hashes) {
    out.u32(w.width());
    out.u32(w.height());
    hashes.resize(w.islands().size());
    std::size_t index = 0;
    out.list(w.islands(), 64, [&](const WorldIsland& i) {
        io_header(out, i.header, kCompactMap);
        out.u32(i.x);
        out.u32(i.y);
        out.u32(i.map.width());
        out.u32(i.map.height());
        std::vector<BlockHash>& blocks = hashes[index++];
        blocks.resize(std::size_t{i.map.blocks_x()} * i.map.blocks_y());
        for (std::size_t b = 0; b < blocks.size(); ++b) {
            if (blocks[b].revision != i.map.block_revision(b)) {
                blocks[b] = {i.map.block_revision(b), block_hash(i.map, b, buildings)};
            }
            out.u64(blocks[b].hash);
        }
    });
}

/// A world with the size and islands of the MAPW section, nothing else yet. Not through
/// Reader::list: each island is checked and placed as it is read.
core::Result<World> read_map(Reader& in, const assets::BuildingTable& buildings, std::uint16_t version) {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    in.u32(width);
    in.u32(height);
    if (!in.ok() || width == 0 || height == 0 || width > World::kMaxSide || height > World::kMaxSide) {
        return make_error(ErrorCode::InvalidFormat, "save: impossible world size");
    }
    World world(width, height);
    // The smallest island record: a header with an empty name (18 bytes, 21 in version 2), position
    // and size (16) and both layers: one tile of 10 bytes each in version 1, in version 2 six columns
    // of a mode byte and at least one byte each. A larger minimum would refuse 1x1 islands.
    const bool compact = version >= kCompactMap;
    const std::uint32_t islands = in.in().count(compact ? 49 : 54);
    // In version 2 a few bytes can describe a large island: the islands together may not cover
    // more than the world before any of them is allocated (add_island refuses overlaps after).
    std::uint64_t area = 0;
    for (std::uint32_t i = 0; i < islands && in.ok(); ++i) {
        assets::IslandHeader header;
        io_header(in, header, version);
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        in.u32(x);
        in.u32(y);
        in.u32(w);
        in.u32(h);
        // Version 1: two layers of 10 bytes per tile must be there before the island is allocated.
        area += std::uint64_t{w} * h;
        if (!in.ok() || w == 0 || h == 0 || w > width || h > height || area > std::uint64_t{width} * height ||
            (!compact && std::uint64_t{w} * h * 2 * 10 > in.in().remaining())) {
            return make_error(ErrorCode::InvalidFormat, "save: damaged island");
        }
        IslandMap map(w, h);
        for (const bool ground : {false, true}) {
            Layer layer(std::size_t{w} * h);
            if (compact) {
                io_compact_layer(in, layer);
            } else {
                io_layer(in, layer);
            }
            if (!in.ok()) {
                return make_error(ErrorCode::InvalidFormat, "save: damaged island");
            }
            if (auto status = set_layer(map, ground, layer, buildings); !status) {
                return status.error();
            }
        }
        if (!in.ok()) {
            break;
        }
        WorldIsland island{std::move(header), std::move(map), x, y};
        if (auto status = world.add_island(std::move(island)); !status) {
            return status.error();
        }
    }
    if (!in.ok()) {
        return make_error(ErrorCode::InvalidFormat, "save: damaged map");
    }
    return world;
}

/// Tiles that loading the original's chunks would not give back.
void write_tiles(Writer& out, const World& w, const assets::BuildingTable& buildings,
                 std::span<const std::uint8_t> before) {
    std::map<std::uint16_t, IslandMap> reloaded;
    if (auto file = assets::ChunkFile::from_bytes(core::Bytes(before.begin(), before.end()))) {
        if (auto islands = assets::load_islands(*file)) {
            for (const assets::Island& island : *islands) {
                reloaded.emplace(island.header.number, IslandMap::from_island(island, buildings));
            }
        }
    }
    std::vector<Decoded::TileOverride> list;
    for (const WorldIsland& i : w.islands()) {
        const auto it = reloaded.find(i.header.number);
        const IslandMap* old =
            it != reloaded.end() && it->second.width() == i.map.width() && it->second.height() == i.map.height()
                ? &it->second
                : nullptr;
        for (std::uint16_t y = 0; y < i.map.height(); ++y) {
            for (std::uint16_t x = 0; x < i.map.width(); ++x) {
                if (old == nullptr || !same_cell(old->top(x, y), i.map.top(x, y))) {
                    list.push_back({i.header.number, x, y, false, i.map.top(x, y)});
                }
                if (old == nullptr || !same_cell(old->ground(x, y), i.map.ground(x, y))) {
                    list.push_back({i.header.number, x, y, true, i.map.ground(x, y)});
                }
            }
        }
    }
    out.list(list, 17, [&](const Decoded::TileOverride& t) { io_tile(out, t, buildings); });
}

std::vector<Decoded::TileOverride> read_tiles(Reader& in, const assets::BuildingTable& buildings) {
    std::vector<Decoded::TileOverride> list;
    in.list(list, 17, [&](Decoded::TileOverride& t) { io_tile(in, t, buildings); });
    return list;
}

/// FNV-1a over what makes the game's buildings (id, picture, size) for the META section.
std::uint64_t buildings_fingerprint(const assets::BuildingTable& buildings) {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    const auto mix = [&](std::uint32_t v) {
        for (int k = 0; k < 4; ++k) {
            h ^= (v >> (8 * k)) & 0xFFU;
            h *= 0x100000001B3ULL;
        }
    };
    for (const assets::BuildingType& b : buildings.types()) {
        mix(b.id);
        mix(b.gfx);
        mix(b.size_x | b.size_y << 16);
    }
    return h;
}

}  // namespace o1602::sim::save
