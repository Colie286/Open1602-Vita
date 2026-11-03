// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's operations (operation.hpp): what a tool of the editor turns its input into.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "o1602/editor/operation.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/scenario_data.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// Which of an island's two layers of tiles: what stands on it, or the ground under it.
enum class Layer : std::uint8_t { Top, Ground };

/// Tiles of islands set to new cells. It keeps each tile's cell before and after, so that a
/// stroke of a brush over a few hundred tiles stays small even on a world of 4000 x 4000.
class TileEdit final : public Operation {
public:
    struct Change {
        std::uint16_t island = 0;  ///< index into World::islands()
        Layer layer = Layer::Top;
        std::uint16_t x = 0;  ///< island tile
        std::uint16_t y = 0;
        sim::MapCell before;
        sim::MapCell after;
    };
    explicit TileEdit(std::vector<Change> changes) noexcept : m_changes(std::move(changes)) {}

    void apply(sim::World& world) override;
    void revert(sim::World& world) override;
    [[nodiscard]] std::size_t memory() const noexcept override;
    [[nodiscard]] std::string_view kind() const noexcept override { return "tiles"; }
    [[nodiscard]] const std::vector<Change>& changes() const noexcept { return m_changes; }

private:
    std::vector<Change> m_changes;
};

/// Collects the tiles a tool sets into one TileEdit: the cell before is taken from the world the
/// first time a tile is set, the cell after is the last one set. The world is not changed until
/// the edit is performed.
class TileEditBuilder {
public:
    explicit TileEditBuilder(const sim::World& world) noexcept : m_world(&world) {}

    /// Sets the tile to `cell`; false (and nothing kept) for an island or tile that does not exist.
    bool set(std::uint16_t island, Layer layer, std::uint32_t x, std::uint32_t y, const sim::MapCell& cell);
    /// The cell a tile will have: the one set, else the world's. Nothing for a tile that does not exist.
    [[nodiscard]] std::optional<sim::MapCell> cell(std::uint16_t island, Layer layer, std::uint32_t x,
                                                   std::uint32_t y) const;
    /// The tiles that change, in the order they were first set; nothing if none does.
    [[nodiscard]] std::unique_ptr<TileEdit> finish();

private:
    [[nodiscard]] static std::uint64_t key(std::uint16_t island, Layer layer, std::uint32_t x,
                                           std::uint32_t y) noexcept;
    const sim::World* m_world;
    std::vector<TileEdit::Change> m_changes;
    std::unordered_map<std::uint64_t, std::size_t> m_index;
};

/// The scenario's description replaced (nothing: the file becomes an ordinary savegame).
class ScenarioInfoEdit final : public Operation {
public:
    ScenarioInfoEdit(std::optional<sim::ScenarioInfo> before, std::optional<sim::ScenarioInfo> after) noexcept
        : m_before(std::move(before)), m_after(std::move(after)) {}
    void apply(sim::World& world) override { world.set_scenario_info(m_after); }
    void revert(sim::World& world) override { world.set_scenario_info(m_before); }
    [[nodiscard]] std::size_t memory() const noexcept override;
    [[nodiscard]] std::string_view kind() const noexcept override { return "scenario"; }

private:
    std::optional<sim::ScenarioInfo> m_before;
    std::optional<sim::ScenarioInfo> m_after;
};

/// The scenario's markers replaced (all of them: a scenario has a few, at most a few thousand).
class MarkersEdit final : public Operation {
public:
    MarkersEdit(std::vector<sim::ScenarioMarker> before, std::vector<sim::ScenarioMarker> after) noexcept
        : m_before(std::move(before)), m_after(std::move(after)) {}
    void apply(sim::World& world) override { world.set_markers(m_after); }
    void revert(sim::World& world) override { world.set_markers(m_before); }
    [[nodiscard]] std::size_t memory() const noexcept override;
    [[nodiscard]] std::string_view kind() const noexcept override { return "markers"; }

private:
    std::vector<sim::ScenarioMarker> m_before;
    std::vector<sim::ScenarioMarker> m_after;
};

/// The scenario's script replaced.
class ScriptEdit final : public Operation {
public:
    ScriptEdit(std::string before, std::string after) noexcept
        : m_before(std::move(before)), m_after(std::move(after)) {}
    void apply(sim::World& world) override { world.set_scenario_script(m_after); }
    void revert(sim::World& world) override { world.set_scenario_script(m_before); }
    [[nodiscard]] std::size_t memory() const noexcept override {
        return sizeof(*this) + m_before.size() + m_after.size();
    }
    [[nodiscard]] std::string_view kind() const noexcept override { return "script"; }

private:
    std::string m_before;
    std::string m_after;
};

/// Several operations as one step: done in their order, taken back in the reverse one (a stroke
/// of the land brush that first made room for its land).
class Steps final : public Operation {
public:
    Steps(std::vector<std::unique_ptr<Operation>> steps, std::string_view kind)
        : m_steps(std::move(steps)), m_kind(kind) {}
    void apply(sim::World& world) override;
    void revert(sim::World& world) override;
    [[nodiscard]] std::size_t memory() const noexcept override;
    [[nodiscard]] std::string_view kind() const noexcept override { return m_kind; }

private:
    std::vector<std::unique_ptr<Operation>> m_steps;
    std::string m_kind;
};

}  // namespace o1602::editor
