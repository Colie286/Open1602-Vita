// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's terrain (ADR 0013): the ground, forest, rock, rivers, beach, surf and sea an island
// is made of, painted tile by tile from a palette of the game's terrain. A stroke of the brush is
// one step of undo that keeps only the tiles it changed.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/core/result.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/editor/operations.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// The kinds of terrain the palette offers, each the records (haeuser.cod) of some kinds.
enum class TerrainGroup : std::uint8_t {
    Ground,  ///< meadow, grass, bare ground
    Forest,  ///< trees of the north and the south
    Rock,    ///< rocks and the ore mountains
    Slope,   ///< the slopes around a mountain, springs
    River,   ///< river pieces, bends and mouths
    Beach,   ///< beach, its corners and variants
    Coast,   ///< surf before the beach
    Sea,     ///< the sea by its depth
};
inline constexpr std::size_t kTerrainGroups = 8;

/// Whether a record is terrain (none of a game's buildings, roads, fields or ruins): what the
/// editor's terrain tools paint and paint over.
[[nodiscard]] bool is_terrain(const assets::BuildingTable& buildings, const assets::BuildingType& building) noexcept;
/// The group of a terrain record; nothing for any other.
[[nodiscard]] std::optional<TerrainGroup> terrain_group(const assets::BuildingTable& buildings,
                                                        const assets::BuildingType& building) noexcept;
/// The level of a terrain record in the map's picture: 0 water (sea, surf), 1 shore (beach), 2
/// land. Pieces of one level fit together; the coast between levels is laid by
/// TerrainStroke::shape().
[[nodiscard]] std::uint8_t terrain_level(const assets::BuildingType& building) noexcept;
/// The records of a group, by id.
[[nodiscard]] std::vector<const assets::BuildingType*> terrain_pieces(const assets::BuildingTable& buildings,
                                                                      TerrainGroup group);

/// A piece of terrain as a tool lays it.
struct TerrainPiece {
    std::uint32_t id = 0;  ///< haeuser.cod id
    std::uint8_t rotation = 0;
    friend bool operator==(const TerrainPiece&, const TerrainPiece&) = default;
};

/// Tiles of islands repainted, with the ore deposits and volcano craters of the mountains painted
/// over (they go with their mountain). One stroke of a terrain tool.
class TerrainEdit final : public Operation {
public:
    struct Features {
        std::uint16_t island = 0;
        std::vector<assets::IslandHeader::Deposit> deposits_before;
        std::vector<assets::IslandHeader::Deposit> deposits_after;
        std::vector<assets::IslandHeader::Crater> header_craters_before;
        std::vector<assets::IslandHeader::Crater> header_craters_after;
        std::vector<sim::Crater> craters_before;
        std::vector<sim::Crater> craters_after;
    };
    /// `kind` names the tool (operation.hpp).
    TerrainEdit(std::unique_ptr<TileEdit> tiles, std::vector<Features> features, std::string_view kind)
        : m_tiles(std::move(tiles)), m_features(std::move(features)), m_kind(kind) {}

    void apply(sim::World& world) override;
    void revert(sim::World& world) override;
    [[nodiscard]] std::size_t memory() const noexcept override;
    [[nodiscard]] std::string_view kind() const noexcept override { return m_kind; }
    /// The tiles changed (nothing if only features did, which does not happen).
    [[nodiscard]] const TileEdit* tiles() const noexcept { return m_tiles.get(); }

private:
    std::unique_ptr<TileEdit> m_tiles;
    std::vector<Features> m_features;
    std::string m_kind;
};

/// Terrain copied from an island (the editor's clipboard): which tiles are land, and the pieces
/// of the land inland (ground, forest, rocks, slopes, rivers) at their places. The coast is not
/// kept: paste() lays it anew about the land, so that it fits wherever the copy goes.
struct TerrainClip {
    struct Piece {
        std::uint32_t id = 0;  ///< haeuser.cod id
        std::uint8_t rotation = 0;
        std::uint32_t x = 0;  ///< the top left of its footprint in the copy
        std::uint32_t y = 0;
    };
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> land;  ///< width x height, 1 land (beach too), 0 water
    std::vector<Piece> pieces;
    [[nodiscard]] bool empty() const noexcept { return width == 0 || height == 0; }
};

/// One stroke of a terrain tool. It changes the world as it goes, so that the map shows the stroke
/// while it is drawn; finish() gives what it did as one step (to perform, which does it again
/// without harm), cancel() takes it back. Tiles of a city, a building, a ship or a land unit, of a
/// running game and of no island are left as they are.
class TerrainStroke {
public:
    TerrainStroke(sim::World& world, const assets::BuildingTable& buildings);
    TerrainStroke(const TerrainStroke&) = delete;
    TerrainStroke& operator=(const TerrainStroke&) = delete;
    ~TerrainStroke() = default;

    /// Why a tile cannot be painted; Free if it can.
    /// Level: a piece of another level than the tile's (water, shore, land), which would leave a gap
    /// in the map's picture; the coast changes with shape().
    /// Shape: the coast would take a form the game has no tiles for, and the land about it is too
    /// narrow to widen it there.
    enum class Refusal : std::uint8_t { Free, NoIsland, NotTerrain, City, Unit, RunningGame, Edge, Level, Shape };
    /// Whether the island tile (x, y) can be painted.
    [[nodiscard]] Refusal refusal(std::uint16_t island, std::int64_t x, std::int64_t y);

    /// Lays `piece` with the top left of its footprint at the island tile (x, y): the tiles become
    /// the piece's on both layers (the ground under them too), keeping whose land they are; a
    /// mountain, river bend or other piece of several tiles that it covers in part becomes meadow
    /// where it is not covered. False, and nothing changed, if the footprint leaves the island or
    /// one of its tiles cannot be painted, or is of another level than the piece (terrain_level()).
    bool stamp(std::uint16_t island, std::int64_t x, std::int64_t y, const assets::BuildingType& piece,
               std::uint8_t rotation, std::uint8_t phase = 0);
    /// The same at the world tile (x, y), on the island it belongs to.
    bool stamp_world(std::int64_t x, std::int64_t y, const assets::BuildingType& piece, std::uint8_t rotation,
                     std::uint8_t phase = 0);
    /// The automatic brushes: the island's tiles `tiles` become land (`land`) or sea, and the coast
    /// around them is laid anew as the game's islands have it: beach and its corners at the land's
    /// edge, surf before it, meadow inland where the coast was, light sea out from the surf. Where
    /// the coast would take a form the game has no tiles for (a strip one tile wide, land touching
    /// at a corner only), the tiles around follow the brush. Land keeps a tile of sea to the
    /// island's edge (Refusal::Edge). `seed` draws the beach's and surf's variants. False if
    /// nothing changed.
    bool shape(std::uint16_t island, std::span<const std::pair<std::uint32_t, std::uint32_t>> tiles, bool land,
               std::uint64_t seed);
    /// The coast clean-up: the island's tiles `tiles` keep whether they are land or water, and the
    /// coast on and about them is laid anew as shape() lays it: stray beach inland becomes meadow,
    /// a coast piece turned wrongly or of the wrong kind is laid right, and a form the game has no
    /// tiles for is widened on these tiles (land first). False if nothing changed.
    bool tidy_coast(std::uint16_t island, std::span<const std::pair<std::uint32_t, std::uint32_t>> tiles,
                    std::uint64_t seed);
    /// Lays `clip` with its top left at the island tile (x, y): its land and water, the coast about
    /// its land as shape() lays it, then its pieces. Tiles beyond the island or refused are left
    /// out. False, and nothing changed, if a coast would take a form without tiles (Refusal::Shape).
    bool paste(std::uint16_t island, std::int64_t x, std::int64_t y, const TerrainClip& clip, std::uint64_t seed);
    /// Keeps `refusal` as the first one met, unless one was met before (a tool that found nothing to paint).
    void note(Refusal refusal) noexcept;
    /// The first refusal met, for the tools' note (Free if none).
    [[nodiscard]] Refusal first_refusal() const noexcept { return m_first_refusal; }
    /// Whether the stroke changed anything so far.
    [[nodiscard]] bool changed() const noexcept { return m_changed; }

    /// The step the stroke made, named `kind`; nothing if it changed nothing. The world stays as
    /// the stroke left it, and the stroke is empty afterwards.
    [[nodiscard]] std::unique_ptr<Operation> finish(std::string_view kind);
    /// Takes the stroke back: the world is as it was before it.
    void cancel();

private:
    struct ShapeTile {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        bool land = false;
    };
    /// shape() for tiles each of their own kind; `land` is the kind that wins where a form must be
    /// widened. `tidy`: tidy_coast().
    bool shape_tiles(std::uint16_t island, std::span<const ShapeTile> tiles, bool land, std::uint64_t seed, bool tidy);
    void put(std::uint16_t island, std::uint32_t x, std::uint32_t y, const sim::MapCell& cell);
    void remove_object(std::uint16_t island, const sim::SubTile& object);
    void keep_features(std::uint16_t island);
    const std::unordered_set<std::uint64_t>& units_on(std::uint16_t island);

    sim::World* m_world;
    const assets::BuildingTable* m_buildings;
    TileEditBuilder m_builder;
    bool m_changed = false;
    bool m_shaping = false;  ///< shape() lays the coast: any level on any other
    std::vector<TerrainEdit::Features> m_features;
    /// Island tiles with a ship or a land unit, by island (made when first asked).
    std::vector<std::optional<std::unordered_set<std::uint64_t>>> m_units;
    Refusal m_first_refusal = Refusal::Free;
};

/// A tile of open sea to lay anew (sea_depth()).
struct SeaTile {
    std::uint32_t x = 0;  ///< island tile
    std::uint32_t y = 0;
    std::uint32_t id = 0;  ///< haeuser.cod id
    std::uint8_t rotation = 0;
    std::uint8_t phase = 0;
};

/// The open sea of the island's tiles x0..x1, y0..y1 by its depth, as the islands of their own
/// shape have it: light water past the surf, then middle, the dark open sea from about six tiles
/// out, deepening towards the island's edge. Only tiles of a depth too far from that and those
/// beside them are laid anew (a coast of the game's islands stays as it is); surf, land and other
/// water are left alone. `seed` turns the middle sea.
[[nodiscard]] std::vector<SeaTile> sea_depth(const sim::World& world, const assets::BuildingTable& buildings,
                                             std::uint16_t island, std::int64_t x0, std::int64_t y0, std::int64_t x1,
                                             std::int64_t y1, std::uint64_t seed);

/// The piece a terrain tile shows (the pipette): its record and rotation; nothing for a tile that
/// is not terrain or no island's.
[[nodiscard]] std::optional<TerrainPiece> terrain_at(const sim::World& world, const assets::BuildingTable& buildings,
                                                     std::uint32_t x, std::uint32_t y);

/// Fills from the world tile (x, y): every tile joined to it by sides that shows the same as it
/// (record and rotation) becomes `piece` (a piece of one tile). An error if nothing could change.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> fill_terrain(sim::World& world,
                                                                    const assets::BuildingTable& buildings,
                                                                    std::uint32_t x, std::uint32_t y,
                                                                    TerrainPiece piece);
/// Replaces on the island at the world tile (x, y) every tile of the record shown there by `piece`
/// (a piece of one tile), each keeping its rotation if the piece can turn. An error if nothing
/// could change.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> replace_terrain(sim::World& world,
                                                                       const assets::BuildingTable& buildings,
                                                                       std::uint32_t x, std::uint32_t y,
                                                                       TerrainPiece piece);

/// The island's tiles x0..x1, y0..y1 copied; a piece only if all of its footprint is within them.
/// Empty if the rectangle is not on the island.
[[nodiscard]] TerrainClip copy_terrain(const sim::World& world, const assets::BuildingTable& buildings,
                                       std::uint16_t island, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                                       std::uint32_t y1);
/// The copy turned a quarter clockwise, as World::rotate_island turns an island.
[[nodiscard]] TerrainClip turned(const TerrainClip& clip, const assets::BuildingTable& buildings);
/// The copy mirrored left to right. A piece that turns is mirrored by its turn; one that cannot
/// turn keeps its picture. Rivers, slopes and pieces of several tiles have no mirrored pictures and
/// are left out (meadow where they were).
[[nodiscard]] TerrainClip mirrored(const TerrainClip& clip, const assets::BuildingTable& buildings);

/// The sides of the brushes, in tiles.
inline constexpr std::array<std::uint8_t, 8> kBrushSizes = {1, 2, 3, 5, 7, 9, 13, 17};

/// The shapes of the brushes: the whole square, the round within it, the diamond within it.
enum class BrushShape : std::uint8_t { Square, Round, Diamond };
inline constexpr std::size_t kBrushShapes = 3;

/// The world tiles of a brush of side `size` about (x, y) (the tile under the mouse is its middle,
/// or the one right below it for an even side), within a map of `width` x `height`.
[[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>> brush_tiles(std::int64_t x, std::int64_t y,
                                                                               std::uint32_t size, std::uint32_t width,
                                                                               std::uint32_t height,
                                                                               BrushShape shape = BrushShape::Square);

/// The tiles of a line from (x0, y0) to (x1, y1), both ends included, without gaps (a stroke drawn
/// faster than the frames).
[[nodiscard]] std::vector<std::pair<std::int64_t, std::int64_t>> line_tiles(std::int64_t x0, std::int64_t y0,
                                                                            std::int64_t x1, std::int64_t y1);

/// A piece of one tile of `group` drawn by `random` (any 64-bit number, as a hash of the tile):
/// ground as often as the islands of the game have each kind, forest of the island's `climate`
/// (0 north, 1 south), any other of the group's pieces of one tile alike, each turned at random if
/// it can be. Nothing if the group has no piece of one tile.
[[nodiscard]] std::optional<TerrainPiece> random_piece(const assets::BuildingTable& buildings, TerrainGroup group,
                                                       std::uint8_t climate, std::uint64_t random);

}  // namespace o1602::editor
