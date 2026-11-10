// SPDX-License-Identifier: GPL-3.0-or-later
//
// Figures on land as the renderer sees them, with their animations running in real time:
// - wild animals graze (deer: ANIM 1, which continues randomly with the eating animations),
// - soldiers walk, stand, strike and die where the simulation has them, drawn on their way between
//   two ticks (TickTrail).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/render/ambient.hpp"
#include "o1602/render/figure_animation.hpp"
#include "o1602/render/tick_trail.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::render {

/// Height per Posoffs pixel of the tile a figure is made on (0x004963E8).
inline constexpr double kHeightPerPosoffs = 0.028;
/// Height of a figure made on land (Posoffs 20, HIGHBODEN): drawn 17 px higher at full zoom.
inline constexpr double kLandHeight = 0.56;

/// Height of a land figure made on world point (x, y): 0.028 * Posoffs of what stands on that tile
/// (callers of 0x00446CA0), 0.56 on land, 0 on beach, harbour tiles and sea. With `piers_as_land`
/// the rule for soldiers (0x00451180): piers and beach ruins count as land.
[[nodiscard]] double spawn_height(const sim::World& world, const assets::BuildingTable& buildings, double x, double y,
                                  bool piers_as_land = false);

/// How much of a worker or animal standing on world point (x, y) its tile hides, in 1/256 of the
/// figure's height, kCropHidden: all of it (Figure_CropCut, 0x00451250). Roads, ground, squares,
/// beach and the like hide nothing; crops hide the legs by growth stage and crop, ripe and tall
/// young trees the whole figure, so a woodcutter vanishes into the tree he fells; quarries hide
/// nothing, every other building all. Workers, hunters, grazing animals and deer only (kinds
/// 0x0C, 0x0E, 0x10, 0x11); the others are shown or hidden by the kind of tile alone.
[[nodiscard]] std::uint16_t crop_cut(const sim::World& world, const assets::BuildingTable& buildings, double x,
                                     double y);

/// Whether a townsman, carrier, cart, doctor, fireman or robber is drawn on a tile of building
/// type `b` (0x00451590): on roads, ground, ruins, squares, river corners, bridges, beach ruins
/// and piers. On the tiles of a building (leaving home, at the target) it is hidden.
[[nodiscard]] bool walker_visible_on(const assets::BuildingType* b) noexcept;

/// walker_visible_on for the tile under world point (x, y); never on open sea.
[[nodiscard]] bool walker_visible_at(const sim::World& world, const assets::BuildingTable& buildings, double x,
                                     double y);

/// A set of land figures with their animation states.
class LandFigures {
public:
    /// Runs the animations by `dt_ms`, but not those of figures marked as out of view.
    void advance(std::uint32_t dt_ms, const assets::FigureTable& figures);
    [[nodiscard]] const std::vector<FigureInstance>& instances() const noexcept { return m_instances; }

protected:
    void clear();
    void add(const FigureInstance& instance, const assets::FigureTable& figures, std::uint8_t anim, std::uint32_t seed);

    std::vector<FigureInstance> m_instances;
    std::vector<AnimState> m_states;
    /// Per instance, 1 if it is out of view: its animation waits where it is. Empty: all in view.
    std::vector<std::uint8_t> m_out_of_view;
};

class AnimalFigures : public LandFigures {
public:
    /// One figure per animal of `world`; direction and animation phase come from its position.
    void reset(const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures);

    /// Animation the animals start with: grazing.
    static constexpr std::uint8_t kGrazeAnim = 1;
};

class SoldierFigures : public LandFigures {
public:
    /// One figure per soldier of `world`, drawn from SOLDAT.BSH in the colour of its owner.
    void reset(const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures);
    /// Before a tick: the positions the soldiers leave.
    void capture(const sim::World& world);
    /// Follows the simulation: positions and directions of the soldiers, and a new animation when
    /// a soldier's changes (walk, stand, strike, die). Soldiers in a doctor's house are not drawn.
    /// `fraction` is the share of the next tick that has passed (Simulation::tick_fraction): a
    /// soldier is drawn that far on its way from before the last tick (capture) to where it is.
    /// Soldiers outside `view` keep their place in the list, but get no height and their
    /// animations wait until they come into view (FB-2).
    void sync(const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
              double fraction = 1.0, const TileBox& view = {});
    /// The figure of a soldier (Soldier::id), nothing if it is not drawn.
    [[nodiscard]] const FigureInstance* find(std::uint32_t id) const noexcept;

private:
    struct Entry {
        std::uint32_t id = 0;     ///< Soldier::id
        std::uint32_t index = 0;  ///< its instance
    };
    /// The instance of a soldier in `index` (sorted by id), kNone if there is none.
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    [[nodiscard]] static std::size_t lookup(const std::vector<Entry>& index, std::uint32_t id) noexcept;
    /// Same, for ids asked in rising order: `hint` keeps the place of the last one (TickTrail).
    [[nodiscard]] static std::size_t lookup(const std::vector<Entry>& index, std::uint32_t id,
                                            std::size_t& hint) noexcept;

    std::vector<std::uint8_t> m_sim_anims;  ///< Soldier::anim per instance, when its animation started
    std::vector<Entry> m_index;             ///< by Soldier::id
    TickTrail m_trail;                      ///< by Soldier::id
    // The last frame's, kept to reuse their memory.
    std::vector<AnimState> m_last_states;
    std::vector<std::uint8_t> m_last_sim_anims;
    std::vector<Entry> m_last_index;
};

}  // namespace o1602::render
