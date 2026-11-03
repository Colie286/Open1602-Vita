// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's islands (ADR 0013): new ones from the original's templates or of their own shape,
// and the operations that move, turn, copy and take them out. An island changes what many parts
// of a world name (cities, ships, growing fields), so these operations keep the whole world as it
// was before for undo (WorldEdit) instead of single tiles.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/core/file.hpp"
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

/// The crops an island's fertility names (assets::IslandHeader::fertility, bit ware - 45):
/// tobacco, spices, sugar cane, cotton, wine and cocoa.
inline constexpr std::array<std::uint8_t, 6> kCropWares = {46, 47, 48, 49, 50, 51};

/// An island's fertility (the crops it grows) changed: keeps it before and after.
class FertilityEdit final : public Operation {
public:
    FertilityEdit(std::size_t island, std::uint32_t before, std::uint32_t after) noexcept
        : m_island(island), m_before(before), m_after(after) {}
    void apply(sim::World& world) override;
    void revert(sim::World& world) override;
    [[nodiscard]] std::size_t memory() const noexcept override { return sizeof(*this); }
    [[nodiscard]] std::string_view kind() const noexcept override { return "island_fertility"; }

private:
    std::size_t m_island;
    std::uint32_t m_before;
    std::uint32_t m_after;
};

/// The island `index` fertile for exactly the wares of `fertility` (bit ware - 45); an error if
/// there is no such island or nothing changes.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> set_fertility(const sim::World& world, std::size_t index,
                                                                     std::uint32_t fertility);

// Placeholders: the islands of a world plan (sim::PlannedIslands), made when a game of the world
// starts. Only a world that has a plan has them (a scenario of the original that makes its islands).

/// The world plan changed: keeps it before and after.
class PlanEdit final : public Operation {
public:
    /// `kind` names the change (operation.hpp).
    PlanEdit(std::optional<sim::PlannedIslands> before, std::optional<sim::PlannedIslands> after, std::string_view kind)
        : m_before(std::move(before)), m_after(std::move(after)), m_kind(kind) {}
    void apply(sim::World& world) override { world.set_planned_islands(m_after); }
    void revert(sim::World& world) override { world.set_planned_islands(m_before); }
    [[nodiscard]] std::size_t memory() const noexcept override;
    [[nodiscard]] std::string_view kind() const noexcept override { return m_kind; }

private:
    std::optional<sim::PlannedIslands> m_before;
    std::optional<sim::PlannedIslands> m_after;
    std::string m_kind;
};

/// Whether the placeholder `index` of the plan is still made when a game starts: no island of the
/// world has its number (a stored island is not made again).
[[nodiscard]] bool placeholder_open(const sim::World& world, std::size_t index) noexcept;

/// The rectangle a placeholder takes: its size class's, as large as the templates of the class.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> placeholder_size(
    const assets::ScenarioPlan::Island& island) noexcept;

/// The placeholder whose rectangle holds the world tile (x, y), if any (the open ones only).
[[nodiscard]] std::optional<std::size_t> placeholder_at(const sim::World& world, std::uint32_t x,
                                                        std::uint32_t y) noexcept;

/// Whether a rectangle of `w` x `h` tiles at (x, y) lies in the map, on open sea and outside every
/// open placeholder other than `except`.
[[nodiscard]] bool free_for_placeholder(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w,
                                        std::uint32_t h, std::optional<std::size_t> except = std::nullopt);

/// A new placeholder of `climate` (0 north, 1 south, 2 by its place) and size class `size` at
/// (x, y), with a free island number and a template drawn when the game starts.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> add_placeholder(const sim::World& world, std::uint8_t climate,
                                                                       std::uint8_t size, std::uint32_t x,
                                                                       std::uint32_t y);
/// The placeholder `index` as `changed` (its place, climate or size).
[[nodiscard]] core::Result<std::unique_ptr<Operation>> change_placeholder(const sim::World& world, std::size_t index,
                                                                          const assets::ScenarioPlan::Island& changed);
[[nodiscard]] core::Result<std::unique_ptr<Operation>> remove_placeholder(const sim::World& world, std::size_t index);

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

/// Whether a ship lies in the rectangle of `w` x `h` tiles at (x, y), other than those in the
/// rectangle of the island `except` (being moved, they go with it).
[[nodiscard]] bool ship_in_way(const sim::World& world, std::int64_t x, std::int64_t y, std::uint32_t w,
                               std::uint32_t h, std::optional<std::size_t> except = std::nullopt);

// The operations: each makes the change on a copy of the world and gives it as a WorldEdit for
// Document::perform(); an error, and the world unchanged, where sim::World refuses it.

/// `island` added at (x, y) as the last island (a new number if its number is taken).
[[nodiscard]] core::Result<std::unique_ptr<Operation>> add_island(const sim::World& world, sim::WorldIsland island,
                                                                  std::uint32_t x, std::uint32_t y,
                                                                  const assets::BuildingTable& buildings);
/// The island `index` grown by tiles of open sea on each side, every tile keeping its place on the
/// map (sim::World::grow_island): where land painted at its edge goes on.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> grow_island(const sim::World& world, std::size_t index,
                                                                   std::uint32_t left, std::uint32_t top,
                                                                   std::uint32_t right, std::uint32_t bottom,
                                                                   const assets::BuildingTable& buildings);
/// A new island of only open sea, `width` x `height` tiles at (x, y), of `climate` (0 north, 1
/// south): where land painted on the open sea goes.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> add_sea_island(const sim::World& world, std::uint32_t x,
                                                                      std::uint32_t y, std::uint32_t width,
                                                                      std::uint32_t height, std::uint8_t climate,
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
/// The island `index` alone as a file: an Open1602 world as large as the island, which
/// load_island() reads back. Only an island no city, building or unit of a game is on.
[[nodiscard]] core::Result<core::Bytes> save_island(const sim::World& world, std::size_t index,
                                                    const assets::BuildingTable& buildings);

/// The island of an island file at (0, 0): one save_island() wrote (the first island of an
/// Open1602 world), or one of the original's island files (.scp), which takes the `climate` of
/// the folder it came from (0 north, 1 south): the file does not tell. Who knew it and a drought go.
[[nodiscard]] core::Result<sim::WorldIsland> load_island(std::span<const std::uint8_t> bytes,
                                                         const assets::BuildingTable& buildings,
                                                         const assets::GameDirectory* game,
                                                         std::vector<std::string>* warnings = nullptr,
                                                         std::uint8_t climate = 0);

/// The smallest map (width, height) that holds the islands, ships, land units and markers, at
/// least 1 x 1: how far resize_world() can cut.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> used_size(const sim::World& world) noexcept;

/// The map made `width` x `height` tiles large, growing or cut at the right and the bottom
/// (sim::World::resize()).
[[nodiscard]] core::Result<std::unique_ptr<Operation>> resize_world(const sim::World& world, std::uint32_t width,
                                                                    std::uint32_t height,
                                                                    const assets::BuildingTable& buildings);

}  // namespace o1602::editor
