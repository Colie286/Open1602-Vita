// SPDX-License-Identifier: GPL-3.0-or-later
//
// Land units, cannon towers and what combat leaves behind (docs/re/military.md). The rules that
// move them and let them fight are in military.hpp.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "o1602/sim/player.hpp"
#include "o1602/sim/target.hpp"

namespace o1602::sim {

/// Soldiers walk on a grid of half tiles: every island tile is 2 x 2 cells (docs/re/military.md).
inline constexpr std::int32_t kCellUnit = kPosUnit / 2;

/// How a soldier is moving right now. Like ships, soldiers walk from cell centre to cell centre in
/// runs and decide what to do at the end of each run.
struct SoldierMotion {
    enum class Phase : std::uint8_t {
        Idle,     ///< standing; decides again after a short wait
        Run,      ///< walking `count` cells in `direction`
        Wait,     ///< standing still for `duration_us` (after an attack, a failed plan)
        Dying,    ///< the death animation (or the short wait of a disbanded unit), then removed
        Healing,  ///< inside a doctor's house: gone for 10 s, then back at full health
    };
    Phase phase = Phase::Idle;
    std::uint8_t direction = 0;
    std::uint8_t count = 0;  ///< cells of the running run
    std::uint32_t elapsed_us = 0;
    std::uint32_t duration_us = 0;
    std::int32_t from_x = 0;  ///< position at the start of the run, 1/4096 tile
    std::int32_t from_y = 0;
    /// Planned way: runs as (direction << 4) | cells, at most 128.
    std::vector<std::uint8_t> path;
    std::uint8_t path_index = 0;
};

/// Patrol between two points (soldier +0x126 .. +0x12F).
struct Patrol {
    bool active = false;
    bool one_way = false;  ///< stop after the last point
    std::uint8_t index = 0;
    std::array<Target, 2> points{};
    friend bool operator==(const Patrol&, const Patrol&) = default;
};

/// A land unit (soldier table 0x0051C678 and SOLDAT3 in the original).
struct Soldier {
    /// Stable number for commands and references; never reused within a game.
    std::uint32_t id = 0;
    /// Position in 1/4096 tile, world coordinates. A standing soldier is on the centre of a half
    /// tile cell.
    std::int32_t pos_x = 0;
    std::int32_t pos_y = 0;
    /// FIGUR number of the unit type. For units drawn per player colour this is the first figure
    /// of the family (SOLDAT1, KAVALERIE1, ...): the colour follows the owner's banner, so any number
    /// of players works (ADR 0007).
    std::uint16_t figure = 0;
    PlayerId owner = kNoPlayer;
    std::uint8_t direction = 0;        ///< 0..7
    std::uint8_t anim = 0;             ///< ANIM index: 0 walk, 1 stand, 2 attack, 3 die
    std::uint16_t island = 0xFFFF;     ///< index into World::islands() it stands on
    std::uint32_t health = 0;          ///< 1/32 HP; 0 for units of old files: full
    Target home;                       ///< where it came from; natives wander around it
    Target target;                     ///< what it walks to or attacks
    std::uint8_t radius = 45;          ///< half width of the next planning window, cells
    std::uint8_t failures = 0;         ///< plans in a row that found no way
    std::uint64_t next_attack_ms = 0;  ///< game time of the next attack allowed (Worktime after the last)
    std::uint64_t next_follow_ms = 0;  ///< next re-plan towards a moving target
    std::uint64_t last_attack_ms = 0;  ///< idle units wander or patrol 2 s after their last attack
    Patrol patrol;
    /// Orders waiting behind `target` (Open1602: Shift + right click); any other order clears them.
    std::vector<Target> queue;
    /// Open1602 (ADR 0010): fights every enemy it meets on its way (attack move), or stays where it
    /// is and only fights what is in range (hold position). Set by orders, ended by the next one.
    bool attack_move = false;
    bool hold = false;
    bool disbanded = false;  ///< dying without the death animation
    SoldierMotion motion;
    /// Slot of the unit in the file it was loaded from (SOLDAT3 +0x08), so the savegame writer keeps
    /// what Open1602 does not model; 0xFFFF for new units.
    std::uint16_t source_slot = 0xFFFF;

    [[nodiscard]] double x() const noexcept { return pos_x / static_cast<double>(kPosUnit); }
    [[nodiscard]] double y() const noexcept { return pos_y / static_cast<double>(kPosUnit); }
    /// Half-tile cell it stands on.
    [[nodiscard]] std::int32_t cell_x() const noexcept { return pos_x / kCellUnit; }
    [[nodiscard]] std::int32_t cell_y() const noexcept { return pos_y / kCellUnit; }
    [[nodiscard]] bool dying() const noexcept { return motion.phase == SoldierMotion::Phase::Dying; }
    /// Not on the map: dying or healing; no one fights it.
    [[nodiscard]] bool away() const noexcept {
        return motion.phase == SoldierMotion::Phase::Dying || motion.phase == SoldierMotion::Phase::Healing;
    }
    /// Places the unit on the centre of a half-tile cell.
    void place_cell(std::int32_t cx, std::int32_t cy) noexcept {
        pos_x = cx * kCellUnit + kCellUnit / 2;
        pos_y = cy * kCellUnit + kCellUnit / 2;
    }
};

/// A unit standing on the upper left cell of world tile (tx, ty).
[[nodiscard]] inline Soldier make_soldier(std::uint32_t tx, std::uint32_t ty, std::uint16_t figure, PlayerId owner,
                                          std::uint16_t island = 0xFFFF) {
    Soldier s;
    s.place_cell(static_cast<std::int32_t>(2 * tx), static_cast<std::int32_t>(2 * ty));
    s.figure = figure;
    s.owner = owner;
    s.island = island;
    return s;
}

/// A cannon tower: the gun of a Wachturm building (tower table 0x004EDE60, TURM in the files).
/// It belongs to the owner of the city it stands in and fires at hostile ships and soldiers.
struct Tower {
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint16_t x = 0;       ///< origin of the building, island tile
    std::uint16_t y = 0;
    PlayerId owner = kNoPlayer;
    std::uint16_t figure = 0;  ///< KANONTURM or PIRATTURM
    std::uint8_t direction = 0;
    std::uint64_t next_shot_ms = 0;
    std::uint32_t next_think_us = 0;  ///< time to its next decision (once per second)
    std::uint16_t source_slot = 0xFFFF;
};

/// Damage a building took (table 0x005624A8). It heals by 1 HP every 10 s; at the building's
/// Maxenergy it falls to ruins. The original does not save it (S-087); Open1602 does.
struct BuildingDamage {
    std::uint16_t island = 0;
    std::uint16_t x = 0;  ///< origin of the building
    std::uint16_t y = 0;
    std::uint32_t damage = 0;  ///< 1/32 HP
};

/// A shot on its way: the hit lands `Shottime` after the shot, on the target if it still exists
/// (event type 1 of the queue 0x005A5A00). There is no projectile that could miss.
struct PendingHit {
    std::uint64_t due_ms = 0;
    Target shooter;  ///< ship, soldier or the tower's building
    PlayerId attacker = kNoPlayer;
    bool cannon = false;       ///< full damage against walls and buildings (ships, towers, gunners)
    std::uint32_t damage = 0;  ///< 1/32 HP
    Target target;
    std::int32_t from_x = 0;  ///< shooter position (1/4096 tile), for the impact effect
    std::int32_t from_y = 0;
};

/// A slot of a castle (MILITAR): a unit in training or waiting to be sent out.
struct CastleSlot {
    std::uint16_t figure = 0;    ///< 0 = empty
    bool training = false;       ///< still training (bit 8)
    bool armed = false;          ///< its weapons are paid (bit 9)
    std::uint32_t health = 0;    ///< at release, 1/32 HP
    std::uint16_t progress = 0;  ///< training points, done at 320
    friend bool operator==(const CastleSlot&, const CastleSlot&) = default;
};

}  // namespace o1602::sim
