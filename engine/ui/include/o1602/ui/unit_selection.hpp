// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ships and land units the player has selected, their groups and the orders a click gives them
// (docs/re/ui.md#auswahl-von-einheiten, ADR 0010). Interface state only: nothing here changes the
// game, the orders are commands for the simulation.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::ui {

/// A ship or a land unit, by its stable id.
struct UnitRef {
    enum class Kind : std::uint8_t { Ship, Soldier };
    Kind kind = Kind::Ship;
    std::uint32_t id = 0;
    friend bool operator==(const UnitRef&, const UnitRef&) = default;
};

/// A unit under the selection rectangle: how much of its picture lies inside (pixels).
struct UnitCandidate {
    UnitRef ref;
    std::int64_t overlap = 0;
};

class UnitSelection {
public:
    /// The places of a formation of figuren.cod (the original's list, 0x004E878C). The original
    /// selects no more; Open1602 takes any number (LC-3) and puts the others into further rows of
    /// the same shape behind them.
    static constexpr std::size_t kFormationPlaces = 21;
    static constexpr std::size_t kGroups = 10;             ///< keys 1 .. 9 and 0
    using Offset = std::pair<std::int16_t, std::int16_t>;  ///< from the centre of the group, 1/8 tile

    [[nodiscard]] const std::vector<UnitRef>& units() const noexcept { return m_units; }
    [[nodiscard]] const std::vector<Offset>& offsets() const noexcept { return m_offsets; }
    [[nodiscard]] bool empty() const noexcept { return m_units.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_units.size(); }
    [[nodiscard]] bool contains(const UnitRef& ref) const noexcept;
    /// Ships or land units (a selection never mixes them).
    [[nodiscard]] std::optional<UnitRef::Kind> kind() const noexcept;
    /// Owner of the selected units (they all have the same), kNoPlayer if none.
    [[nodiscard]] sim::PlayerId owner(const sim::World& world) const;
    /// Changes since the last call (for the windows: refresh, back to the first page).
    [[nodiscard]] bool take_changed() noexcept { return std::exchange(m_changed, false); }

    void clear();
    /// Clears the selection and every group: unit ids belong to one game (after loading another).
    void reset();
    /// Only this unit.
    void select(const sim::World& world, const UnitRef& ref);
    /// Shift + click: a selected unit leaves the selection, another of the same owner and kind joins
    /// it (otherwise it is selected alone).
    void toggle(const sim::World& world, const UnitRef& ref);
    /// The rectangle (Selection_Box 0x0044CA30): the player's own units if there are any (else the
    /// foreign one with the largest part inside, alone); of the kind of the one with the largest part
    /// inside. `additive` (Shift) adds them to a selection of the same owner and kind.
    void box(const sim::World& world, std::vector<UnitCandidate> candidates, sim::PlayerId local, bool additive);
    /// All of `candidates` (the units on screen) of the same owner and type as `like`.
    void select_alike(const sim::World& world, const std::vector<UnitCandidate>& candidates, const UnitRef& like);
    void remove(const sim::World& world, const UnitRef& ref);
    /// Drops units that are gone, dying or sinking, or changed hands; true if any.
    bool prune(const sim::World& world);

    /// Mean position of the units, in tiles.
    [[nodiscard]] std::optional<std::pair<double, double>> centre(const sim::World& world) const;
    /// Whether any own unit is moving (the stop buttons, 0x0044D450).
    [[nodiscard]] bool any_moving(const sim::World& world) const;
    /// Whether any selected ship shows the white flag (0x0044D3A0).
    [[nodiscard]] bool any_white_flag(const sim::World& world) const;
    /// Bit k set: a unit of FIGUR Kind k is selected (0 ships, 1 infantry .. 4 cannoneers; 0x0044C9B0).
    [[nodiscard]] std::uint32_t kind_mask(const sim::World& world, const assets::FigureTable& figures) const;
    /// Alt+1 .. Alt+4 (History Edition): all of `local`'s land units among `candidates` (the units on
    /// screen) of FIGUR Kind `kind` (1 infantry, 2 cavalry, 3 musketeers, 4 cannoneers) become the
    /// selection; false (and the selection unchanged) if there is none.
    bool select_kind(const sim::World& world, const assets::FigureTable& figures,
                     const std::vector<UnitCandidate>& candidates, sim::PlayerId local, std::int32_t kind);
    /// Keeps (or removes) the units whose FIGUR Kind is in `mask` (0x0044C3D0).
    void filter_kinds(const sim::World& world, const assets::FigureTable& figures, std::uint32_t mask, bool keep);
    /// Keeps the units at half health or less (`keep_wounded`), or the others (0x0044C510).
    void filter_wounded(const sim::World& world, const assets::FigureTable& figures, bool keep_wounded);
    /// Formation `n` of figuren.cod (0 line, 1 column, 2 block) around the centre: new offsets, each
    /// unit in order to the nearest free place (0x0044C6D0); scale 3.5 tiles for ships, 1 for units.
    /// More units than places fill copies of the formation behind it, front to back.
    void formation(const sim::World& world, const assets::FigureTable& figures, std::size_t n);
    /// The group spreads out to 1.67 times its size (0x0044C8B0).
    void scatter(const sim::World& world);
    /// Tab (Open1602): narrows the selection to one type of unit after the other (FIGUR: infantry,
    /// cavalry, ...; kinds of ship), in the order of their numbers, then back to all of them. False
    /// if the selection has only one type.
    bool cycle_types(const sim::World& world);

    /// Ctrl + number: the group is the selection (with its offsets); Shift + number adds it.
    void store_group(std::size_t g);
    void add_to_group(const sim::World& world, std::size_t g);
    /// The number: the group becomes the selection; false if it has nobody left.
    bool recall_group(const sim::World& world, std::size_t g);
    /// The group a unit belongs to, for the number over it.
    [[nodiscard]] std::optional<std::size_t> group_of(const UnitRef& ref) const;
    /// The groups as the savegame keeps them (sim::World::unit_groups()), and back after loading.
    [[nodiscard]] sim::UnitGroups saved_groups() const;
    void restore_groups(const sim::UnitGroups& groups);

    /// Orders of the player's selected units: to world tile (`x`, `y`) keeping the shape of the group,
    /// onto an object (everyone to it), or to stop. `queue`: behind their current orders. `attack`:
    /// an object of another player means war (military mode or attack order). Land units go in
    /// orders of at most sim::kMaxCommandItems each.
    [[nodiscard]] std::vector<sim::Command> move_orders(const sim::World& world, sim::PlayerId local, double x,
                                                        double y, bool queue, bool attack_move = false) const;
    [[nodiscard]] std::vector<sim::Command> target_orders(const sim::World& world, sim::PlayerId local,
                                                          const sim::Target& target, bool queue, bool attack) const;
    [[nodiscard]] std::vector<sim::Command> stop_orders(const sim::World& world, sim::PlayerId local) const;
    /// Stop and stay, fighting only what comes in range (ADR 0010).
    [[nodiscard]] std::vector<sim::Command> hold_orders(const sim::World& world, sim::PlayerId local) const;

private:
    struct Member {
        UnitRef ref;
        Offset offset;
    };
    void recentre(const sim::World& world);
    /// formation() for more units than `slots` has places.
    void rows_behind(const sim::World& world, const std::vector<assets::FigureTable::FormationSlot>& slots,
                     double scale);
    void set(const sim::World& world, std::vector<UnitRef> units);

    std::vector<UnitRef> m_units;
    std::vector<Offset> m_offsets;
    std::array<std::vector<Member>, kGroups> m_groups;
    bool m_changed = false;
    std::vector<UnitRef> m_cycle_all;  ///< the whole selection while Tab narrows it
    std::size_t m_cycle_step = 0;      ///< 0 = all, n = the n-th type
    bool m_cycling = false;
};

/// Position of a unit in tiles; nothing if it is gone.
[[nodiscard]] std::optional<std::pair<double, double>> unit_position(const sim::World& world, const UnitRef& ref);
/// Owner of a unit, kNoPlayer if it is gone.
[[nodiscard]] sim::PlayerId unit_owner(const sim::World& world, const UnitRef& ref);
/// Health 0..1 of a unit (the bar over a selected unit), nothing if it is gone.
[[nodiscard]] std::optional<double> unit_health(const sim::World& world, const assets::FigureTable& figures,
                                                const UnitRef& ref);

}  // namespace o1602::ui
