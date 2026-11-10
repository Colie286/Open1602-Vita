// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/player.hpp"

namespace o1602::sim {

/// One tile of an island. Mirrors the original grid word (island + 0xAF4, 32 bit per tile):
/// bits 0-12 base sprite, 13-14 rotation, 15-18 animation phase; plus the owner (the original's
/// INSELHAUS attribute bits 22-25, translated to a PlayerId when loading). The sprite has 16 bits
/// here, not 13, so that buildings of mods may use sprites from 8192 on; savegames store the
/// buildings, not their sprites, and so are not limited by it.
struct MapCell {
    static constexpr std::uint16_t kEmpty = 0xFFFF;
    static constexpr std::uint8_t kNoCity = 0xFF;

    std::uint16_t sprite = kEmpty;  ///< base sprite = building gfx + index of the sub-tile
    std::uint8_t rotation = 0;      ///< 0..3
    std::uint8_t anim_phase = 0;    ///< 0..15, per-tile animation offset
    PlayerId owner = kNoPlayer;     ///< index into World::players()
    std::uint8_t city = kNoCity;    ///< city on this island (World::city()), attribute bits 14-16
    /// The value of INSELHAUS attribute bits 17-21, which the original passes to its placement
    /// handler and writes anew with every save; kept so a savegame comes back as it was.
    std::uint8_t random = 0;

    [[nodiscard]] bool empty() const noexcept { return sprite == kEmpty; }
};

/// Position of a tile relative to the building it belongs to.
struct SubTile {
    const assets::BuildingType* building = nullptr;
    std::int32_t origin_x = 0;  ///< tile where the building was placed (top-left of its footprint)
    std::int32_t origin_y = 0;
};

/// The fingerprint of a block of an island's tiles and the revision of the block it was taken at
/// (Simulation::state_hash()).
struct BlockHash {
    std::uint64_t revision = 0;  ///< 0: none taken yet; revisions start at 1
    std::uint64_t hash = 0;
};

/// Tile grid of one island with two layers: buildings/terrain on top and the ground below
/// (the original keeps the latter in a second grid at island + 0xAFC).
class IslandMap {
public:
    /// Tiles on a side of a block: the fingerprint of the game hashes an island's tiles block by
    /// block and takes again only the blocks that changed since the last one (TB-26).
    static constexpr std::uint32_t kBlockSide = 16;

    IslandMap(std::uint32_t width, std::uint32_t height);

    [[nodiscard]] std::uint32_t width() const noexcept { return m_width; }
    [[nodiscard]] std::uint32_t height() const noexcept { return m_height; }
    [[nodiscard]] const MapCell& top(std::uint32_t x, std::uint32_t y) const { return m_top[index(x, y)]; }
    [[nodiscard]] const MapCell& ground(std::uint32_t x, std::uint32_t y) const { return m_ground[index(x, y)]; }
    [[nodiscard]] bool contains(std::int32_t x, std::int32_t y) const noexcept {
        return x >= 0 && y >= 0 && static_cast<std::uint32_t>(x) < m_width && static_cast<std::uint32_t>(y) < m_height;
    }
    /// Blocks across and down; block k covers columns (k % blocks_x()) * kBlockSide on and rows
    /// (k / blocks_x()) * kBlockSide on, the last ones of a row or column fewer.
    [[nodiscard]] std::uint32_t blocks_x() const noexcept { return (m_width + kBlockSide - 1) / kBlockSide; }
    [[nodiscard]] std::uint32_t blocks_y() const noexcept { return (m_height + kBlockSide - 1) / kBlockSide; }
    /// A number that every change of a tile in the block (either layer) replaces by a new one,
    /// unique among all maps: equal revisions mean equal tiles. A copy of the map shares the
    /// revisions while it is the same.
    [[nodiscard]] std::uint64_t block_revision(std::size_t block) const { return m_revisions[block]; }

    /// Changes the picture of a tile in place (a field ripening or harvested: the original swaps
    /// the sprite bits of the grid word, 0x0047CA80 / 0x0047C830).
    void set_sprite(std::uint32_t x, std::uint32_t y, std::uint16_t sprite) {
        m_top[index(x, y)].sprite = sprite;
        touch(x, y);
    }
    void set_rotation(std::uint32_t x, std::uint32_t y, std::uint8_t rotation) {
        m_top[index(x, y)].rotation = static_cast<std::uint8_t>(rotation & 3U);
        touch(x, y);
    }
    void set_anim_phase(std::uint32_t x, std::uint32_t y, std::uint8_t phase) {
        m_top[index(x, y)].anim_phase = static_cast<std::uint8_t>(phase & 0x0FU);
        touch(x, y);
    }

    /// Places a building with its top-left footprint corner at (x, y), exactly like the original
    /// placement routine (0x00463B10): sub-tiles receive gfx, gfx+1, ... in a rotation dependent
    /// order. Previous content of the covered tiles moves to the ground layer (demolishing restores
    /// it from there, so former river tiles stay water - S-009).
    /// Fails (without changes) if the rotated footprint leaves the island.
    core::Status place(const assets::BuildingType& building, std::int32_t x, std::int32_t y, std::uint8_t rotation,
                       std::uint8_t anim_phase, PlayerId owner = kNoPlayer, std::uint8_t city = MapCell::kNoCity);

    /// A building the player builds (0x00463B10): like place(), but the ground layer stays as it is
    /// (it keeps the island's original terrain, which demolishing restores).
    core::Status build(const assets::BuildingType& building, std::int32_t x, std::int32_t y, std::uint8_t rotation,
                       std::uint8_t anim_phase, PlayerId owner, std::uint8_t city);

    /// A tile of a saved island (INSELHAUS, 0x00468550): written on top like the original's loader;
    /// with `to_ground` it also becomes the tile's ground (0x00463E10). Nothing moves down.
    core::Status load(const assets::BuildingType& building, std::int32_t x, std::int32_t y, std::uint8_t rotation,
                      std::uint8_t anim_phase, PlayerId owner, std::uint8_t city, bool to_ground,
                      std::uint8_t random = 0);

    /// Takes the current picture as the original terrain of every tile (the ground layer the
    /// original keeps at island + 0xAFC).
    void capture_ground() {
        m_ground = m_top;
        touch_all();
    }

    /// Demolishing: the tile shows its ground again (sprite, rotation, phase); city and owner stay.
    void restore_ground(std::uint32_t x, std::uint32_t y) {
        MapCell& t = m_top[index(x, y)];
        const MapCell& g = m_ground[index(x, y)];
        t.sprite = g.sprite;
        t.rotation = g.rotation;
        t.anim_phase = g.anim_phase;
        touch(x, y);
    }

    /// Sets a tile of either layer as it was saved (Open1602's own savegame chunk).
    void set_top_cell(std::uint32_t x, std::uint32_t y, const MapCell& cell) {
        m_top[index(x, y)] = cell;
        touch(x, y);
    }
    void set_ground_cell(std::uint32_t x, std::uint32_t y, const MapCell& cell) {
        m_ground[index(x, y)] = cell;
        touch(x, y);
    }

    /// Territory: the city (and its owner) a tile belongs to.
    void set_city(std::uint32_t x, std::uint32_t y, std::uint8_t city, PlayerId owner) {
        m_top[index(x, y)].city = city;
        m_top[index(x, y)].owner = owner;
        touch(x, y);
    }

    /// Replaces the building whose footprint lies at (x, y) by another one of the same size in
    /// place (a house changing class, ruins): owner, city and the ground below stay (0x00463EF0).
    core::Status replace(const assets::BuildingType& building, std::int32_t x, std::int32_t y, std::uint8_t rotation,
                         std::uint8_t anim_phase);

    /// Inverse of place() for one tile (original 0x00463250): building and placement origin.
    [[nodiscard]] SubTile sub_tile(const assets::BuildingTable& buildings, std::uint32_t x, std::uint32_t y) const;
    /// The placement origin of the tile (x, y) whose cell shows `building` (its by_sprite()
    /// record); inline, as the computers ask for it on every tile they look at (TB-11).
    [[nodiscard]] static std::pair<std::int32_t, std::int32_t> origin_of(const MapCell& cell,
                                                                         const assets::BuildingType& building,
                                                                         std::uint32_t x, std::uint32_t y) noexcept {
        const auto px = static_cast<std::int32_t>(x);
        const auto py = static_cast<std::int32_t>(y);
        const auto w = static_cast<std::int32_t>(building.size_x);
        const auto h = static_cast<std::int32_t>(building.size_y);
        const auto d = static_cast<std::int32_t>(cell.sprite - building.gfx);
        if (d == 0 && w == 1 && h == 1) {
            return {px, py};  // most of an island: ground, coast, trees
        }
        const std::int32_t row = d / w;
        const std::int32_t col = d % w;
        switch (cell.rotation & 3U) {  // original 0x00463250
            case 0:
                return {px - col, py - row};
            case 1:
                return {px + 1 + row - h, py - col};
            case 2:
                return {px + 1 + col - w, py + 1 + row - h};
            default:
                return {px - row, py + 1 + col - w};
        }
    }
    /// The same for the ground layer.
    [[nodiscard]] SubTile ground_sub_tile(const assets::BuildingTable& buildings, std::uint32_t x,
                                          std::uint32_t y) const;

    /// Builds the map from INSELHAUS entries (one entry per building at its origin).
    /// If `base` is given (the island template, see IslandHeader::template_path()), its tiles are
    /// placed first and the island's own entries on top - as the original loader does.
    /// Unknown ids and entries outside the island are skipped and reported in `warnings`.
    static IslandMap from_island(const assets::Island& island, const assets::BuildingTable& buildings,
                                 std::vector<std::string>* warnings = nullptr, const assets::Island* base = nullptr);

private:
    enum class Mode : std::uint8_t { Place, Replace, Build, Load, LoadGround };
    core::Status write(const assets::BuildingType& building, std::int32_t x, std::int32_t y, std::uint8_t rotation,
                       std::uint8_t anim_phase, PlayerId owner, std::uint8_t city, Mode mode, std::uint8_t random = 0);
    [[nodiscard]] std::size_t index(std::uint32_t x, std::uint32_t y) const { return std::size_t{y} * m_width + x; }
    /// A new revision for the block of tile (x, y) (block_revision()).
    void touch(std::uint32_t x, std::uint32_t y) {
        m_revisions[std::size_t{y / kBlockSide} * blocks_x() + x / kBlockSide] = next_revision();
    }
    void touch_all();
    [[nodiscard]] static std::uint64_t next_revision() noexcept;
    static SubTile sub_tile_of(const MapCell& cell, const assets::BuildingTable& buildings, std::uint32_t x,
                               std::uint32_t y);

    std::uint32_t m_width;
    std::uint32_t m_height;
    std::vector<MapCell> m_top;
    std::vector<MapCell> m_ground;
    std::vector<std::uint64_t> m_revisions;  ///< per block
};

/// Whether a building belongs to the land of a city (0x00480B70): roads, piers, crop fields, towers,
/// production and public buildings, Kontors. Grass, trees, fish, ground, mountains and ruins do
/// not. What does falls to ruins when its city loses the land; what does not is terrain the
/// original keeps in the ground layer.
[[nodiscard]] bool territory_building(const assets::BuildingTable& buildings, const assets::BuildingType& b) noexcept;

/// Footprint after rotation (original 0x00463830): rotations 1 and 3 swap width and height.
inline void rotated_size(const assets::BuildingType& b, std::uint8_t rotation, std::uint32_t& w, std::uint32_t& h) {
    const bool swap = (rotation & 1U) != 0;
    w = swap ? b.size_y : b.size_x;
    h = swap ? b.size_x : b.size_y;
}

}  // namespace o1602::sim
