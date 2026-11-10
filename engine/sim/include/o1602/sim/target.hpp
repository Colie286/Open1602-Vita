// SPDX-License-Identifier: GPL-3.0-or-later
//
// References to objects of the world: what a ship sails to, what a soldier attacks, a route stop
// (the original's 4-byte object references, docs/re/ships.md and docs/re/military.md).
#pragma once

#include <cstddef>
#include <cstdint>

namespace o1602::sim {

/// Positions of ships and soldiers in 1/4096 tile; a tile centre is tile * 4096 + 2048.
inline constexpr std::int32_t kShipUnit = 4096;
inline constexpr std::int32_t kPosUnit = kShipUnit;

/// Most orders a ship or a unit keeps waiting behind its current one (Shift + right click, ADR 0010).
inline constexpr std::size_t kMaxQueuedOrders = 16;

struct Target {
    enum class Kind : std::uint8_t {
        None,
        Position,  ///< a world tile (type 0x37)
        City,      ///< the Kontor of a city (type 0x35): island index and city number on it
        Ship,      ///< a ship, by id (types 1-3)
        Building,  ///< a building by a tile of it, e.g. a shipyard (types 0x32-0x34)
        Soldier,   ///< a land unit, by id (type 4)
        Spot,      ///< a world position in half tiles (type 0x38): where soldiers are sent
    };
    Kind kind = Kind::None;
    std::uint16_t island = 0;  ///< City, Building: index into World::islands()
    std::uint8_t city = 0;     ///< City: city number on the island
    std::uint32_t x = 0;       ///< Position: world tile; Building: island tile; Spot: world half tile
    std::uint32_t y = 0;
    std::uint32_t ship = 0;  ///< Ship: Ship::id; Soldier: Soldier::id
    /// The reference as a file of the original had it: kept for writing it back (bytes Open1602
    /// does not read, references of kinds it does not know). Not part of the comparison.
    std::uint32_t raw = 0;

    [[nodiscard]] static Target position(std::uint32_t x, std::uint32_t y) noexcept {
        return {Kind::Position, 0, 0, x, y, 0, 0};
    }
    [[nodiscard]] static Target city_of(std::uint16_t island, std::uint8_t city) noexcept {
        return {Kind::City, island, city, 0, 0, 0, 0};
    }
    [[nodiscard]] static Target other_ship(std::uint32_t id) noexcept { return {Kind::Ship, 0, 0, 0, 0, id, 0}; }
    [[nodiscard]] static Target building(std::uint16_t island, std::uint32_t x, std::uint32_t y) noexcept {
        return {Kind::Building, island, 0, x, y, 0, 0};
    }
    [[nodiscard]] static Target soldier(std::uint32_t id) noexcept { return {Kind::Soldier, 0, 0, 0, 0, id, 0}; }
    [[nodiscard]] static Target spot(std::uint32_t x2, std::uint32_t y2) noexcept {
        return {Kind::Spot, 0, 0, x2, y2, 0, 0};
    }
    /// The spot at the top corner of world tile `x`, `y` (spots are half tiles).
    [[nodiscard]] static Target tile_spot(std::uint32_t x, std::uint32_t y) noexcept { return spot(2 * x, 2 * y); }
    [[nodiscard]] bool empty() const noexcept { return kind == Kind::None; }
    /// A unit that moves (its position is not fixed).
    [[nodiscard]] bool moving() const noexcept { return kind == Kind::Ship || kind == Kind::Soldier; }
    friend bool operator==(const Target& a, const Target& b) noexcept {
        return a.kind == b.kind && a.island == b.island && a.city == b.city && a.x == b.x && a.y == b.y &&
               a.ship == b.ship;
    }
};

}  // namespace o1602::sim
