// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's islands (ADR 0013): new ones from the original's templates or of their own shape,
// and the operations that move, turn, copy and take them out. An island changes what many parts
// of a world name (cities, ships, growing fields), so these operations keep the whole world as it
// was before for undo (WorldEdit) instead of single tiles.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/core/result.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// A change of the world kept as the whole other world: doing and taking it back both swap the
/// world with the one kept. For the changes of islands, which are rare and touch much.
class WorldEdit final : public Operation {
public:
    /// The world after the change; `kind` names it (operation.hpp).
    WorldEdit(sim::World after, std::string_view kind);
    void apply(sim::World& world) override;
    void revert(sim::World& world) override;
    [[nodiscard]] std::size_t memory() const noexcept override { return m_memory; }
    [[nodiscard]] std::string_view kind() const noexcept override { return m_kind; }

private:
    sim::World m_other;
    std::string m_kind;
    std::size_t m_memory = 0;
};

/// About how much memory a world holds: the map's cells and the islands' tiles.
[[nodiscard]] std::size_t world_memory(const sim::World& world) noexcept;

/// A new island.
struct NewIsland {
    /// From one of the original's templates, or of its own shape (sim/island_shapes.hpp).
    enum class Shape : std::uint8_t { Template, Own };
    Shape shape = Shape::Template;
    std::uint8_t climate = 0;  ///< 0 north, 1 south
    std::uint8_t size = 2;     ///< size class 0 little .. 4 large (sim/island_templates.hpp)
    /// Which template (taken modulo the templates of the climate and size), or the seed of the shape.
    std::uint64_t seed = 0;
};

/// The island of `request` at (0, 0), numbered `number`: a template's tiles (which need the
/// original's `game`), or one of its own shape as large as the size class's templates. It grows
/// grain, grass, trees and fish and has no deposits yet.
[[nodiscard]] core::Result<sim::WorldIsland> make_island(const NewIsland& request, std::uint16_t number,
                                                         const assets::BuildingTable& buildings,
                                                         const assets::GameDirectory* game,
                                                         std::vector<std::string>* warnings = nullptr);

/// Whether a rectangle of `w` x `h` tiles at (x, y) lies in the map on open sea (or on the island
/// `except`, the one being moved): where the editor shows a place as free.
[[nodiscard]] bool free_place(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w, std::uint32_t h,
                              std::optional<std::size_t> except = std::nullopt);

// The operations: each makes the change on a copy of the world and gives it as a WorldEdit for
// Document::perform(); an error, and the world unchanged, where sim::World refuses it.

/// `island` added at (x, y) as the last island (a new number if its number is taken).
[[nodiscard]] core::Result<std::unique_ptr<Operation>> add_island(const sim::World& world, sim::WorldIsland island,
                                                                  std::uint32_t x, std::uint32_t y,
                                                                  const assets::BuildingTable& buildings);
[[nodiscard]] core::Result<std::unique_ptr<Operation>> move_island(const sim::World& world, std::size_t index,
                                                                   std::uint32_t x, std::uint32_t y,
                                                                   const assets::BuildingTable& buildings);
[[nodiscard]] core::Result<std::unique_ptr<Operation>> rotate_island(const sim::World& world, std::size_t index,
                                                                     const assets::BuildingTable& buildings);
[[nodiscard]] core::Result<std::unique_ptr<Operation>> copy_island(const sim::World& world, std::size_t index,
                                                                   std::uint32_t x, std::uint32_t y,
                                                                   const assets::BuildingTable& buildings);
[[nodiscard]] core::Result<std::unique_ptr<Operation>> remove_island(const sim::World& world, std::size_t index,
                                                                     const assets::BuildingTable& buildings);

}  // namespace o1602::editor
