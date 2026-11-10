// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ships and what they carry (docs/re/ships.md): position, course, cargo, trade route, the goods of
// the free trader. The rules that move them are in ships.hpp.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "o1602/assets/game_data.hpp"
#include "o1602/sim/player.hpp"
#include "o1602/sim/target.hpp"

namespace o1602::sim {

/// A hold of a ship: one ware, at most 50 t (docs/re/ships.md).
struct CargoSlot {
    std::uint8_t ware = 0;     ///< 0 = empty
    std::uint32_t amount = 0;  ///< 1/32 t
    std::uint16_t price = 0;   ///< average price paid per ton (what bought goods cost)
};

/// One ware a route stop loads or unloads.
struct RouteEntry {
    std::uint8_t ware = 0;  ///< 0 = unused
    bool unload = false;    ///< deliver up to `amount`; otherwise load until `amount` is on board
    std::uint32_t amount = 0;
    friend bool operator==(const RouteEntry&, const RouteEntry&) = default;
};

struct RouteStop {
    /// Wares per stop: the original has three; Open1602 takes as many as a ship has holds, at most
    /// eight (docs/re/ships.md, "Trade routes"), the first three as the original's.
    static constexpr std::size_t kEntries = 8;
    /// Wares of the original (SHIP4, the computer players' supply runs, the pirates' plunder).
    static constexpr std::size_t kOriginalEntries = 3;
    Target target;  ///< a city (Kontor), or a ship or position for computer players and pirates
    std::array<RouteEntry, kEntries> entries{};
    friend bool operator==(const RouteStop&, const RouteStop&) = default;
};

/// The automatic trade route of a ship: its stops, sailed in a loop. The original has four;
/// Open1602 takes eight (docs/re/ships.md, "Trade routes"), the first four as the original's.
struct TradeRoute {
    static constexpr std::size_t kStops = 8;
    /// Stops of the original (SHIP4, its route window, the computer players' supply runs).
    static constexpr std::size_t kOriginalStops = 4;
    std::array<RouteStop, kStops> stops{};
    std::uint8_t current = 0;  ///< stop sailed to or lying at
    bool active = false;
    bool one_way = false;  ///< switch off after the last stop (computer players)
    /// A transfer of this round was refused; three rounds in a row warn the player.
    bool problem = false;
    std::uint8_t problem_rounds = 0;

    [[nodiscard]] bool has_stops() const noexcept {
        for (const RouteStop& s : stops) {
            if (!s.target.empty()) {
                return true;
            }
        }
        return false;
    }
    /// A patrol: a route of positions only (`Ship_IsPatrolling 0x0044EA90`).
    [[nodiscard]] bool patrol() const noexcept {
        bool any = false;
        for (const RouteStop& s : stops) {
            if (!s.target.empty() && s.target.kind != Target::Kind::Position) {
                return false;
            }
            any = any || !s.target.empty();
        }
        return active && any;
    }
    /// Whether a stop beyond the original's four is set (such a route needs Open1602).
    [[nodiscard]] bool beyond_original_stops() const noexcept {
        for (std::size_t s = kOriginalStops; s < kStops; ++s) {
            if (!stops[s].target.empty()) {
                return true;
            }
        }
        return false;
    }
    /// Whether a stop has a ware beyond the original's three (such a route needs Open1602).
    [[nodiscard]] bool beyond_original_entries() const noexcept {
        for (const RouteStop& s : stops) {
            for (std::size_t e = RouteStop::kOriginalEntries; e < RouteStop::kEntries; ++e) {
                if (s.entries[e] != RouteEntry{}) {
                    return true;
                }
            }
        }
        return false;
    }
    [[nodiscard]] bool beyond_original() const noexcept { return beyond_original_stops() || beyond_original_entries(); }
};

/// A ware the free trader carries (the original's HANDLER record; not in the holds).
struct TraderGood {
    std::uint16_t price = 0;   ///< average purchase price per ton
    std::uint8_t age = 0;      ///< visits without a change; at 5 half of it is thrown away
    std::uint32_t amount = 0;  ///< 1/32 t
};

/// How a ship is moving right now. Ships sail from tile centre to tile centre in runs of one or two
/// tiles; between runs they decide what to do next (docs/re/ships.md, "Sailing").
struct ShipMotion {
    enum class Phase : std::uint8_t {
        Idle,     ///< nothing to do; decides again at the end of a short wait
        Run,      ///< sailing `count` tiles in `direction`
        Turn,     ///< turning in place by 45 degrees
        Wait,     ///< lying still (at a Kontor, after a failed plan)
        Sinking,  ///< going down for Stirbtime, then gone
    };
    Phase phase = Phase::Idle;
    std::uint8_t count = 0;  ///< tiles of the running run
    std::uint32_t elapsed_us = 0;
    std::uint32_t duration_us = 0;
    std::int32_t from_x = 0;  ///< position at the start of the run, 1/4096 tile
    std::int32_t from_y = 0;
    /// Planned course: runs as (direction << 4) | tiles, at most 99.
    std::vector<std::uint8_t> path;
    std::uint8_t path_index = 0;
    std::uint8_t radius = 80;          ///< half width of the next planning window
    std::uint8_t failures = 0;         ///< plans in a row that found no way
    std::uint8_t turn_to = 0xFF;       ///< direction a turn in place aims at
    std::uint64_t next_follow_us = 0;  ///< game time of the next re-plan towards a moving target
};

struct Ship {
    /// Stable number of the ship for commands and references; never reused within a game.
    std::uint32_t id = 0;
    std::string name;
    std::uint16_t x = 0;  ///< world tile the ship is on
    std::uint16_t y = 0;
    /// Position in 1/4096 tile (kShipUnit); x, y follow it.
    std::int32_t pos_x = 0;
    std::int32_t pos_y = 0;
    std::uint16_t figure = 0;  ///< FIGUR number (figuren.cod)
    PlayerId owner = kNoPlayer;
    std::uint8_t direction = 0;  ///< 0..7
    std::uint32_t health = 0;    ///< 1/32; 0 for ships of old files: full
    std::uint8_t cannons = 0;    ///< mounted cannons
    /// Holds; as many as the ship type has (8 in every original file). Empty holds have ware 0.
    std::vector<CargoSlot> cargo;
    TradeRoute route;
    Target target;    ///< what it sails to now
    Target order;     ///< what the player or computer last ordered
    Target previous;  ///< where it came from (the trader does not visit it again)
    /// Orders waiting behind `target` (Open1602: Shift + right click); each becomes the target when
    /// the one before it ends. Any other order clears them.
    std::vector<Target> queue;
    /// Open1602 (ADR 0010): attack move and hold position, as for land units.
    bool attack_move = false;
    bool hold = false;
    bool held = false;               ///< white flag: no decisions of its own
    bool sinking = false;            ///< hit fatally: turns, sinks and is removed
    std::uint64_t next_shot_ms = 0;  ///< game time of the next shot allowed (reload)
    bool repairing = false;          ///< ordered to a shipyard for repair
    bool for_sale = false;
    std::uint16_t sale_factor = 128;  ///< sale price = Preis * factor / 128
    /// Free trader only: goods by ware number, and the counters of what he gets from nowhere.
    std::vector<TraderGood> trader_goods;
    std::uint32_t trader_tools_ms = 0;
    std::uint32_t trader_ore_ms = 0;
    bool trader = false;
    /// The computer player's job of the ship in an original savegame (SHIP4 +0x24, +0x3E, +0x40),
    /// taken over once when the computer players start on it (docs/re/ai.md). Open1602 keeps the
    /// jobs with the computer players' state.
    struct OriginalJob {
        std::uint16_t mission = 0;
        std::uint16_t param = 0;
        std::uint8_t counter = 0;
    };
    std::optional<OriginalJob> original_job;
    /// Exploring an island (ship + 0x1A4): steps of 1.5 s left, 0 = not exploring; the next step
    /// at game time `explore_next_ms` (docs/re/ships.md#exploring).
    std::uint8_t explore_steps = 0;
    std::uint64_t explore_next_ms = 0;
    /// Island whose surroundings (rectangle + 5 tiles) the ship is in, for discovering islands;
    /// 0xFFFF = open sea.
    std::uint16_t near_island = 0xFFFF;
    ShipMotion motion;
    /// Slot of the ship in the file it was loaded from (SHIP4 +0x46), so the savegame writer keeps
    /// the fields Open1602 does not model; 0xFFFF for new ships.
    std::uint16_t source_slot = 0xFFFF;

    /// A player's trade cart ("fahrender Händler", TRADER1): a ship record that drives on land, on
    /// the island of its city (docs/re/ships.md#the-trade-cart). Nothing at sea sees it.
    [[nodiscard]] bool cart() const noexcept { return figure == assets::figure::kTradeCart; }
    /// Goods of a ware on board.
    [[nodiscard]] std::uint32_t load(std::uint8_t ware) const noexcept {
        std::uint32_t n = 0;
        for (const CargoSlot& c : cargo) {
            n += c.ware == ware ? c.amount : 0U;
        }
        return n;
    }
    /// Everything on board.
    [[nodiscard]] std::uint32_t total_load() const noexcept {
        std::uint32_t n = 0;
        for (const CargoSlot& c : cargo) {
            n += c.ware != 0 ? c.amount : 0U;
        }
        return n;
    }
    /// Places the ship on the centre of a world tile.
    void place(std::uint32_t tx, std::uint32_t ty) noexcept {
        pos_x = static_cast<std::int32_t>(tx) * kShipUnit + kShipUnit / 2;
        pos_y = static_cast<std::int32_t>(ty) * kShipUnit + kShipUnit / 2;
        x = static_cast<std::uint16_t>(tx);
        y = static_cast<std::uint16_t>(ty);
    }
};

/// A ship on the centre of a world tile.
[[nodiscard]] inline Ship make_ship(std::string name, std::uint32_t x, std::uint32_t y, std::uint16_t figure,
                                    PlayerId owner, std::uint8_t direction = 0) {
    Ship s;
    s.name = std::move(name);
    s.place(x, y);
    s.figure = figure;
    s.owner = owner;
    s.direction = static_cast<std::uint8_t>(direction & 7U);
    return s;
}

}  // namespace o1602::sim
