// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building and demolishing with the mouse: holds the building the build menu (or the keys) picked
// and its rotation, and turns a click or a drag into a build or demolish order of the local
// player (docs/re/construction.md). Roads and walls are dragged along a line, fields and squares
// over a rectangle, houses in rows and blocks and piers along a line (the History
// Edition's „Mehrfachplatzierung“), everything else is placed where the mouse is let go.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/drag_path.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

class BuildTool {
public:
    enum class Mode : std::uint8_t { Off, Build, Demolish };

    explicit BuildTool(const assets::BuildingTable& buildings);

    [[nodiscard]] Mode mode() const noexcept { return m_mode; }
    void set_mode(Mode mode) noexcept;
    [[nodiscard]] bool active() const noexcept { return m_mode != Mode::Off; }

    /// Next or previous building of the list.
    void cycle(int step) noexcept;
    /// Turns the building by a quarter.
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

    /// One line for the window title.
    [[nodiscard]] std::string describe() const;

private:
    const assets::BuildingTable* m_buildings;
    std::vector<const assets::BuildingType*> m_list;
    std::size_t m_selected = 0;
    std::uint8_t m_rotation = 0;
    Mode m_mode = Mode::Off;
    bool m_orient_to_road = false;
    std::optional<std::uint32_t> m_ship;  ///< the ship that founds, chosen in its window
    std::optional<Spot> m_drag_start;
    std::uint32_t m_variant_seed = 0;  ///< picks the trees' variants, the same for every frame of a drag
};

}  // namespace o1602::app
