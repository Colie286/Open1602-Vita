// SPDX-License-Identifier: GPL-3.0-or-later
//
// Soldiers, cannon towers and what their shots do (docs/re/military.md).
//
// Like ships, a soldier decides what to do only at the end of each run of one to three half-tile
// cells, after an attack or a short wait (0x00456D00): whether a hostile figure is in range, whether
// to chase one, whether it has arrived. Towers look for a target once a second (0x00458AC0). Shots
// land after the shooter's Shottime (combat.hpp); a soldier dies at 0 health, a building falls to
// ruins at its Maxenergy. The original decides kills and destruction only on the attacker's computer;
// Open1602 computes everything on every computer from the same state (ADR 0004).
#pragma once

#include <cstdint>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/combat.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/land_paths.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

class Fleet;

/// Sends soldiers somewhere (Order_Move 0x0044D540): to a place, to a building, against a unit.
struct SoldierOrder {
    PlayerId player = kNoPlayer;
    std::vector<std::uint32_t> soldiers;  ///< Soldier::id
    Target target;                        ///< a Spot, a building, a soldier or ship; none = stop
    /// With the attack cursor: an object of another player (not the free trader) means war with him.
    bool attack = false;
    /// Open1602 (ADR 0010): on the way to the target the units fight every enemy they meet, then go on.
    bool attack_move = false;
    /// FORMATION of figuren.cod to take up around a Spot, 0xFF = none (everyone walks to the spot).
    std::uint8_t formation = 0xFF;
    /// With a Spot: where each unit goes relative to it, in 1/8 tile, in the order of `soldiers`
    /// (the shape of the group kept, 0x0044D540). Ignored unless there is one per unit.
    std::vector<std::pair<std::int16_t, std::int16_t>> offsets{};
    /// Waits behind the unit's orders (Shift + right click).
    bool queue = false;
};

/// Further commands to one unit (soldier ops 0x0C, 0x28, 0x29, 0x2B).
struct SoldierCommand {
    enum class Op : std::uint8_t {
        Stop,
        Disband,      ///< "Einheit auflösen": dies without fight; costs the third value of its Soldat: line
        SetWaypoint,  ///< `index` 0 or 1 becomes `point`
        Patrol,       ///< `on`: walk between the waypoints (once with `one_way`), or stop doing so
        Hold,         ///< Open1602: stop and stay; fight only what comes in range (until the next order)
    };
    PlayerId player = kNoPlayer;
    std::uint32_t soldier = 0;
    Op op = Op::Stop;
    std::uint8_t index = 0;
    Target point;
    bool on = true;
    bool one_way = false;
};

/// Orders to a castle (building ops 0x10, 0x4C; docs/re/military.md, "Training").
struct CastleOrder {
    enum class Op : std::uint8_t {
        Recruit,  ///< train a unit of `figure` (SOLDAT1, KAVALERIE1, MUSKETIER1, KANONIER1)
        Release,  ///< send the trained unit of `slot` out, walking to `spot`
        Cancel,   ///< give up the unit of `slot`; paid weapons come back as far as it is healthy
        /// Open1602: every trained unit leaves at once and the group lines up in a block around
        /// `spot` (the units in training stay)
        ReleaseAll,
    };
    PlayerId player = kNoPlayer;
    std::uint16_t island = 0;
    std::uint16_t x = 0;  ///< origin of the castle
    std::uint16_t y = 0;
    Op op = Op::Recruit;
    std::uint16_t figure = 0;
    std::uint8_t slot = 0;
    Target spot;
};

/// Soldiers from a ship's holds onto land within 6 tiles (0x00448F90).
struct UnloadOrder {
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;
    std::uint8_t hold = 0xFF;  ///< one hold, 0xFF = all holds with soldiers
};

/// What happened in a fight, for messages, sounds and effects.
struct MilitaryEvent {
    enum class Kind : std::uint8_t {
        Shot,               ///< `unit` fired at `target`
        Hit,                ///< a shot of `unit` (the shooter) landed on `target` at (x, y)
        SoldierKilled,      ///< `target` was killed by `player`'s unit
        BuildingDestroyed,  ///< `target` fell to ruins
        SettlementShelled,  ///< MELDUNG 15: ships attack a building of `owner`
        ShipAttacked,       ///< 8, or 5 when `pirates`
        ShipSunk,           ///< 9
        SoldierHealed,      ///< 13
        WarDeclared,        ///< the attack cursor declared war: `player` on `owner`
        UnitTrained,        ///< MELDUNG 12: a castle finished training a unit
        ArmyFull,           ///< 0x45: the player has 100 units
        TroopsLanded,       ///< 16: soldiers of `player` landed on an island where `owner` has a city
        Boarded,            ///< `unit` went on board ship `target`
        NativesCurse,       ///< MELDUNG 4: the natives curse `player` (a drought on their island if `pirates`)
    };
    Kind kind = Kind::Shot;
    Target unit;
    Target target;
    PlayerId player = kNoPlayer;  ///< the acting player (attacker)
    PlayerId owner = kNoPlayer;   ///< the player it happened to
    std::int32_t x = 0;           ///< position, 1/4096 tile (hits)
    std::int32_t y = 0;
    std::uint16_t figure = 0;  ///< effect figure (Hitfignr) or the shooter's figure
    bool pirates = false;
};

class Military {
public:
    /// Way searches of all soldiers in one tick (TB-1), about 1.6 ms each. Soldiers over the
    /// budget decide again in the next tick, in the order of the unit list.
    static constexpr std::uint32_t kPlansPerTick = 8;

    Military(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
             const Rules& rules, Combat& combat);

    /// The fleet gets hit too: ships slow down when hit and sink at 0 health.
    void set_fleet(Fleet* fleet) noexcept { m_fleet = fleet; }

    /// One tick: soldiers walk and fight, towers fire, shots land, buildings heal.
    void tick(std::uint32_t dt_ms, std::vector<ConstructionEvent>* construction);

    bool order(const SoldierOrder& order);
    /// An attack by `self` on `target` (military mode, 0x0044D540): war with its owner unless that
    /// is `self`, nobody or the free trader; pending peace offers end too. Returns whether the
    /// relation changed.
    bool declare_war_by_attack(PlayerId self, const Target& target);
    bool command(const SoldierCommand& command);
    bool castle(const CastleOrder& order);
    bool unload(const UnloadOrder& order);

    /// Units a player has: on land, in castles and on board ships (Player_ArmySize 0x00475B60).
    [[nodiscard]] std::uint32_t army_size(PlayerId player) const;
    /// A castle's pack donkey brought weapons (the logistics call this; amount 0: the trip failed).
    void castle_delivery(std::uint16_t island, std::uint16_t x, std::uint16_t y, std::uint8_t ware,
                         std::uint32_t amount);
    /// Castles that need a pack donkey to fetch weapons from their city.
    struct FetchRequest {
        std::uint16_t island = 0;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint8_t ware = 0;
    };
    [[nodiscard]] std::vector<FetchRequest> take_fetch_requests() {
        std::vector<FetchRequest> r;
        r.swap(m_fetch);
        return r;
    }

    /// The natives' city tick (every 10 s, 0x0047EB30): warriors come out of their huts, three
    /// every 30 s, seven every 10 s for ten minutes after their village was attacked. Each goes to
    /// the first road or bridge of the natives' owner that a search within 7 tiles of the hut
    /// reaches (warrior_target()); a hut without one sends nobody.
    void natives_tick();
    /// Target of a warrior leaving `hut` (0x00467820): a half-tile spot, nothing without a goal.
    [[nodiscard]] std::optional<Target> warrior_target(std::uint16_t island, TilePos origin, std::uint32_t width,
                                                       std::uint32_t height, PlayerId owner);

    /// A tower building was built or a city's territory changed: towers come and go with it.
    void update_towers(std::uint16_t island);

    [[nodiscard]] std::vector<MilitaryEvent> take_events() {
        std::vector<MilitaryEvent> e;
        e.swap(m_events);
        return e;
    }

    /// Damage of a building at its origin, 1/32 HP.
    [[nodiscard]] std::uint32_t building_damage(std::uint16_t island, std::uint16_t x, std::uint16_t y) const;
    /// Applies damage to a building (0x0047A650): destroyed at its Maxenergy.
    void damage_building(std::uint16_t island, std::uint16_t x, std::uint16_t y, std::uint32_t damage, bool cannon,
                         PlayerId attacker, std::vector<ConstructionEvent>* construction);

    /// The version save() writes, the section MILI of the running game's state (DS-3): 1 the
    /// regeneration clock, 2 and the castles' clock, 3 and the natives' ticks.
    static constexpr std::uint16_t kStateVersion = 3;
    /// State that is not in the world (clocks), for Open1602's savegame.
    void save(core::ByteWriter& out) const;
    /// Restores what save() of `version` wrote; an error if the data is damaged (nothing changed).
    core::Status load(core::ByteReader& in, std::uint16_t version);

private:
    /// A unit or building on another island than the unit stands on (a melee unit cannot reach it).
    [[nodiscard]] bool on_other_island(const Soldier& s, const Target& t) const;
    // Soldiers (soldiers.cpp)
    void advance(Soldier& s, std::uint32_t budget_us);
    void decide(Soldier& s);
    [[nodiscard]] static std::uint32_t idle_ms(const Soldier& s) noexcept;
    void start_wait(Soldier& s, std::uint32_t ms);
    /// Waits standing (idle_ms), the walking or attack animation ended.
    void rest(Soldier& s);
    bool start_next_run(Soldier& s);
    bool plan(Soldier& s, std::int32_t radius, bool hostile_goal);
    bool plan_to(Soldier& s, const Target& target, std::int32_t radius, bool hostile_goal);
    [[nodiscard]] bool target_valid(const Target& t) const;
    [[nodiscard]] std::optional<CellRect> target_cells(const Target& t) const;
    [[nodiscard]] PlayerId owner_of(const Target& t) const;
    /// The target is a doctor of the soldier's owner: it walks right up to it and goes in to be healed.
    [[nodiscard]] bool heals_at(const Soldier& s, const Target& t) const;
    /// How near (in cells) the soldier walks to a friendly target: onto a free place itself, next
    /// to an own doctor, within a few cells of anything else.
    [[nodiscard]] std::int32_t friendly_goal(const Soldier& s, const Target& t) const;
    /// The cells where a unit stands without an order or is headed to as its place, the units in
    /// `leaving` left out (they are given a new place).
    [[nodiscard]] std::unordered_set<std::uint64_t> occupied_cells(
        const std::unordered_set<std::uint32_t>& leaving) const;
    /// The cell nearest (x, y) that a soldier can stand on and that is not in `occupied`, among
    /// equally near ones the one nearest (from_x, from_y); (x, y) itself when there is none near.
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> free_cell(
        std::int32_t x, std::int32_t y, std::int32_t from_x, std::int32_t from_y,
        const std::unordered_set<std::uint64_t>& occupied) const;
    [[nodiscard]] bool attackable(const Soldier& s, const Target& t) const;
    [[nodiscard]] std::optional<std::uint8_t> fire_direction(const Soldier& s, const assets::FigureType& type,
                                                             const Target& t) const;
    bool attack(Soldier& s, const assets::FigureType& type, const Target& t);
    bool arrived(Soldier& s);
    bool wander(Soldier& s);
    /// The next of the queued orders that is still valid becomes the target (or none).
    void next_queued(Soldier& s);
    [[nodiscard]] const assets::FigureType* type_of(const Soldier& s) const;
    [[nodiscard]] std::uint32_t run_us(const Soldier& s, std::uint8_t direction, std::uint8_t cells) const;
    void kill(Soldier& s, PlayerId killer, bool disband);
    /// The units list in squares (review AU7), for the units' loop of tick(): built before it and
    /// kept up to date as each unit moves. Outside the loop, or once the list has changed, the
    /// queries look at all units. Never saved.
    void grid_build();
    void grid_moved(const Soldier& s);
    [[nodiscard]] bool grid_valid() const noexcept;
    /// Calls `visit` for every unit whose cell may be within `r` cells of (cx, cy) in both axes
    /// (and maybe others), until it returns true. The order is not that of the list.
    template <typename Visit>
    void near_units(std::int32_t cx, std::int32_t cy, std::int32_t r, Visit&& visit) const;

    // Castles and ships (castles.cpp)
    void castles_tick();
    void castle_second(CostBuilding& castle);
    [[nodiscard]] CostBuilding* castle_at(std::uint16_t island, std::uint16_t x, std::uint16_t y);
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> exit_cell(std::uint16_t island, std::uint16_t x,
                                                                                 std::uint16_t y) const;
    /// The unit of `slot` (trained) out of castle `c` at `cell`, walking to `spot`; its id.
    std::uint32_t release(CostBuilding& c, std::size_t slot, std::pair<std::int32_t, std::int32_t> cell, PlayerId owner,
                          const Target& spot);
    bool board(Soldier& s, Ship& ship);

    // Towers (towers.cpp)
    void towers_tick(std::uint32_t dt_ms);
    void tower_think(Tower& t);

    /// The natives' curse (0x0047EAA0) after their village fell.
    void curse(std::uint16_t island, std::uint8_t city, bool dissolved);

    // Hits and buildings (combat side)
    void resolve_hits(std::vector<ConstructionEvent>* construction);
    void hit(const PendingHit& h, std::vector<ConstructionEvent>* construction);
    void regenerate();

    World* m_world;
    const assets::BuildingTable* m_buildings;
    const assets::FigureTable* m_figures;
    const Rules* m_rules;
    Combat* m_combat;
    Fleet* m_fleet = nullptr;
    LandPlanner m_planner;
    std::uint32_t m_plans_left = 0;  ///< way searches still allowed in this tick (TB-1)
    std::uint32_t m_tick_ms = 0;     ///< game time of the running tick
    PathFinder m_finder;             ///< scratch of warrior_target()
    std::vector<MilitaryEvent> m_events;
    std::vector<std::uint32_t> m_ids;  ///< scratch of tick(): the units in their order
    // The squares of grid_build(): the units of each by their place in World::soldiers().
    std::vector<std::vector<std::uint32_t>> m_grid;
    std::vector<std::uint32_t> m_square_of;  ///< per unit: its square
    const Soldier* m_grid_data = nullptr;    ///< the list the squares were built for
    std::int32_t m_grid_w = 0;
    std::int32_t m_grid_h = 0;
    std::vector<PendingHit> m_due;     ///< scratch of resolve_hits()
    std::uint32_t m_regen_ms = 0;      ///< game time since the building damage last healed
    std::uint32_t m_castle_ms = 0;     ///< game time since the castles' last second
    std::uint32_t m_native_ticks = 0;  ///< natives' city ticks (warriors every third)
    std::vector<FetchRequest> m_fetch;
};

}  // namespace o1602::sim
