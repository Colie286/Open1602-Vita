// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building and demolishing with the mouse: holds the building the build menu (or the keys) picked
// and its rotation, and turns a click or a drag into a build or demolish order of the local
// player. Roads and walls are dragged along a line, fields and squares
// over a rectangle, houses in rows and blocks and piers along a line (the History
// Edition's „Mehrfachplatzierung“), everything else is placed where the mouse is let go.
//
// The copy tool (Open1602, as in later Anno games) takes a piece of the player's own city as a
// layout: a drag over it (or a click on one building) picks up its buildings, roads, walls and
// fields, which then hang at the mouse, turn with the building keys and are built as one order
// wherever the player clicks, as often as wanted.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/blueprints.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/drag_path.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

class BuildTool {
public:
    enum class Mode : std::uint8_t { Off, Build, Demolish, Copy };

    explicit BuildTool(const assets::BuildingTable& buildings);

    [[nodiscard]] Mode mode() const noexcept { return m_mode; }
    void set_mode(Mode mode) noexcept;
    [[nodiscard]] bool active() const noexcept { return m_mode != Mode::Off; }

    /// The buildings the tool can place, in the build menu's order.
    [[nodiscard]] const std::vector<const assets::BuildingType*>& buildings() const noexcept { return m_list; }
    /// Turns the building (and a copied layout) by a quarter.
    void rotate(int step) noexcept;
    [[nodiscard]] std::uint8_t rotation() const noexcept { return m_rotation; }
    /// Picks a building by its Id (the build menu); false if it is none the tool can place.
    bool select(std::uint32_t id) noexcept;
    /// Buildings with a door turn towards the nearest road (the build menu's switch 31023; off at
    /// the start of a game as in the original).
    void set_orient_to_road(bool on) noexcept { m_orient_to_road = on; }
    /// The History Edition's „Mehrfachplatzierung“, always on: a drag with a house lays them side
    /// by side over the dragged rectangle, from where the drag started; a drag with a pier lays one
    /// on every tile of the straight line to the mouse. What does not fit is left out, and what
    /// cannot be paid for any more stays unbuilt (sim::plan_build).
    [[nodiscard]] bool multi_drag() const noexcept;
    /// Picks the first Kontor to found from a ship (the ship window's button 38017): its cargo
    /// pays instead of the nearest ship's. False if there is no Kontor to place.
    bool select_kontor(std::uint32_t ship) noexcept;

    /// Where the mouse is (island tile), nullopt over the sea.
    struct Spot {
        std::uint16_t island = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
    };
    void press(const Spot& spot) noexcept;
    /// The mouse button is let go (the order went out): the next trees get new variants.
    void release() noexcept {
        m_drag_start.reset();
        ++m_variant_seed;
    }
    [[nodiscard]] bool dragging() const noexcept { return m_drag_start.has_value(); }
    /// A field or forest being dragged out: its rectangle is shown hatched instead of the crops
    /// or trees (History Edition).
    [[nodiscard]] bool hatched_drag() const noexcept;

    [[nodiscard]] const assets::BuildingType* selected() const noexcept;

    /// The order the current drag (or a click at `spot`) would give.
    [[nodiscard]] std::optional<sim::BuildOrder> build_order(const sim::World& world, sim::PlayerId player,
                                                             const Spot& spot) const;
    [[nodiscard]] std::optional<sim::DemolishOrder> demolish_order(sim::PlayerId player, const Spot& spot) const;

    /// Every tile of the buildings the demolish order of the current drag (or a click at `spot`)
    /// would remove, for the preview (sim::plan_demolish, so a drag leaves out the big buildings
    /// as the order does).
    [[nodiscard]] std::vector<sim::TilePos> demolish_preview(const sim::World& world, sim::PlayerId player,
                                                             const Spot& spot) const;

    /// A copied piece of a city: what a player orders to build it again, relative to its
    /// smallest corner. Tiles of the layout run from (0, 0) to (width - 1, height - 1).
    struct Layout {
        struct Item {
            std::uint32_t building = 0;  ///< a record the player can order (a tree: its variant)
            std::int32_t x = 0;          ///< footprint origin in the layout
            std::int32_t y = 0;
            std::uint8_t rotation = 0;
            std::int32_t width = 1;  ///< footprint as rotated
            std::int32_t height = 1;
        };
        std::vector<Item> items;
        std::int32_t width = 0;
        std::int32_t height = 0;
        sim::TilePos from;  ///< where the layout's corner was taken from on its island

        /// Turns the layout by `step` quarters, the way the building rotation turns: each step takes
        /// east to south, the buildings turning with it.
        void rotate(int step);
    };

    /// The copy tool still waits for the piece to copy (no layout in hand).
    [[nodiscard]] bool copy_selecting() const noexcept { return m_mode == Mode::Copy && !m_layout; }
    /// Whether a click places buildings: the build tool, or the copy tool with a layout in hand.
    [[nodiscard]] bool places() const noexcept {
        return (m_mode == Mode::Build && selected() != nullptr) || (m_mode == Mode::Copy && m_layout.has_value());
    }
    [[nodiscard]] const std::optional<Layout>& layout() const noexcept { return m_layout; }
    /// The layout the drag of the copy tool (or a click at `spot`) covers: the player's own
    /// buildings that have a tile in the rectangle, as the build tool would order them. Houses
    /// come as the first house (they grow up again), fields and trees only with a building in the
    /// piece that harvests them, so that wild forest is not copied; Kontors, ruins and terrain never.
    [[nodiscard]] Layout copy_layout(const sim::World& world, sim::PlayerId player, const Spot& spot) const;
    /// Ends the drag of the copy tool: the piece goes into the hand. False (and nothing in hand)
    /// if it holds nothing to copy.
    bool take_layout(const sim::World& world, sim::PlayerId player, const Spot& spot);
    /// Puts the layout away: the copy tool waits for a new piece.
    void drop_layout() noexcept { m_layout.reset(); }
    /// Every tile of the buildings the drag of the copy tool covers, for the preview.
    [[nodiscard]] std::vector<sim::TilePos> copy_preview(const sim::World& world, sim::PlayerId player,
                                                         const Spot& spot) const;
    /// The record a player orders to build `placed` again (a road for its crossing, the growing
    /// field for a ripe one, the land wall for its beach piece, the first house for a grown one);
    /// nullptr if it is nothing the player can build.
    [[nodiscard]] const assets::BuildingType* orderable(const assets::BuildingType& placed) const noexcept;

    /// Blueprint mode (Open1602): what the build and copy tools place becomes the player's
    /// blueprints instead of buildings (sim::BlueprintOrder), to be built later. Stays as it is
    /// when the tool changes.
    [[nodiscard]] bool blueprint() const noexcept { return m_blueprint; }
    void set_blueprint(bool on) noexcept { m_blueprint = on; }
    /// The order of the current drag (or a click at `spot`) as blueprints: build_order() placed as
    /// sim::BlueprintOrder::Kind::Place. Nothing for a Kontor founded from a ship.
    [[nodiscard]] std::optional<sim::BlueprintOrder> blueprint_order(const sim::World& world, sim::PlayerId player,
                                                                     const Spot& spot) const;
    /// One line for the window title.
    [[nodiscard]] std::string describe() const;

private:
    const assets::BuildingTable* m_buildings;
    std::vector<const assets::BuildingType*> m_list;
    std::size_t m_selected = 0;
    std::uint8_t m_rotation = 0;
    Mode m_mode = Mode::Off;
    bool m_orient_to_road = false;
    bool m_blueprint = false;
    std::optional<std::uint32_t> m_ship;  ///< the ship that founds, chosen in its window
    std::optional<Spot> m_drag_start;
    std::optional<Layout> m_layout;                       ///< the copy tool's piece in hand
    const assets::BuildingType* m_first_house = nullptr;  ///< what a copied or picked house is built as
    std::uint32_t m_variant_seed = 0;  ///< picks the trees' variants, the same for every frame of a drag
};

}  // namespace o1602::app
