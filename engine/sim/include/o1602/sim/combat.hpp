// SPDX-License-Identifier: GPL-3.0-or-later
//
// What ships, soldiers and towers share in a fight (docs/re/military.md, "Combat"): who sees whom,
// how good a target is, and shots on their way.
//
// Every figure step the original builds a list of hostile figures within 12 tiles for each ship,
// soldier and tower (0x0045CAD0, 0x0045CD20). A shooter scores the entries with five tables
// (0x00454250: a target it can kill, a big share of its health per shot, a threat to itself, one it
// outranges, the distance beyond its range) and takes the best one it can hit. Damage does not
// depend on chance or distance; a shot lands after the shooter's Shottime on the target if it still
// exists. There is no projectile that could miss.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/land_paths.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// A figure in another's list: who, and how far (1/8 tile, the original's distance of centres).
struct Neighbour {
    Target unit;
    std::int32_t distance8 = 0;
};

/// What a fighter is, for target choice and range: position, health, range, damage.
struct Fighter {
    Target unit;
    PlayerId owner = kNoPlayer;
    std::uint16_t island = 0xFFFF;  ///< soldiers, towers: the island; ships: the island they are near
    std::int32_t x8 = 0;            ///< centre in 1/8 tile
    std::int32_t y8 = 0;
    CellRect cells;                ///< the half-tile cells it covers (shots aim at the nearest)
    std::uint32_t health = 0;      ///< 1/32 HP
    std::uint32_t max_health = 1;  ///< 1/32 HP
    std::int32_t range8 = 0;       ///< Shotradius in 1/8 tile
    std::uint32_t damage = 0;      ///< damage of its next shot, 1/32 HP
    std::uint32_t threat = 0;      ///< what the scorer thinks it does to others: ships per shot, others per second
    bool cannon = false;           ///< full damage against walls and buildings
    bool ship = false;
};

/// A scored candidate (0x00454250).
struct Candidate {
    Target unit;
    std::int32_t distance8 = 0;
    std::int32_t score = 0;
    bool near = false;  ///< within range (and half a tile)
};

/// Whether units can shoot at a building of this kind (Haus_IsDestroyable 0x00464A80): gates,
/// walls, towers, buildings, beach houses, mines, Kontors, harbours and mills always; roads, trees
/// and fields, bridges and piers only with `ctrl`; terrain (ground, beach, river, rock, ...) and
/// ruins never, nor a destroyed Kontor or market.
[[nodiscard]] bool destroyable(const assets::BuildingType& building, bool ctrl) noexcept;

class Combat {
public:
    static constexpr std::size_t kMaxNeighbours = 32;
    /// Pairs within this many tiles in both axes see each other.
    static constexpr std::int32_t kSight = 12;

    Combat(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures);

    /// Rebuilds the hostile lists from the current positions (every tick).
    void update();
    /// The hostile figures a ship, soldier or tower (by its building) sees, at most kMaxNeighbours,
    /// nearest first (S-114). Valid until the next update().
    [[nodiscard]] std::span<const Neighbour> hostiles(const Target& unit) const;

    /// What a ship, soldier or tower is now; nothing if it is gone (or dying, or sinking).
    [[nodiscard]] std::optional<Fighter> fighter(const Target& unit) const;
    /// Whether a unit of `self` sent against this building target shoots it although nobody is at
    /// war: a tree or a field (forest kind) that is not its own, also one of nobody. In the original
    /// an order with Ctrl takes these as targets, and units shoot every target that is not their
    /// owner's (Haus_IsDestroyable 0x00464A80, 0x00456D00, 0x00452370).
    [[nodiscard]] bool fellable(const Target& unit, PlayerId self) const;
    /// Scores the entries within `max_distance8` and sorts them best first (stable). Valid until
    /// the next rank().
    [[nodiscard]] std::span<const Candidate> rank(const Fighter& me, std::span<const Neighbour> list,
                                                  std::int32_t max_distance8) const;

    /// Fires: the hit lands `delay_ms` later on `target` (and only if it still exists).
    void shoot(const Fighter& shooter, const Fighter& target, std::uint32_t delay_ms);

    /// The figure table (unit data).
    [[nodiscard]] const assets::FigureTable& figures() const noexcept { return *m_figures; }
    [[nodiscard]] const assets::BuildingTable& buildings() const noexcept { return *m_buildings; }
    [[nodiscard]] LandMap& land() noexcept { return m_land; }
    [[nodiscard]] const LandMap& land() const noexcept { return m_land; }
    /// Something was built or demolished on an island.
    void island_changed(std::uint16_t island);

    /// Damage of a shot of a soldier or tower with this health (Hitpoint scaled by health,
    /// 0x00454250): full at full health, a third near death.
    [[nodiscard]] static std::uint32_t scaled_damage(std::uint32_t hit, std::uint32_t health,
                                                     std::uint32_t max_health) noexcept;
    /// Damage per shot of a ship by its cannons: Hitpoint times 1, 2, 4 or 6.
    [[nodiscard]] static std::uint32_t cannon_factor(std::uint8_t cannons) noexcept;

private:
    struct Entry {
        Target unit;
        PlayerId owner;
        std::uint16_t island;
        std::int32_t x8;
        std::int32_t y8;
        bool ship;
        bool cart = false;  ///< a trade cart: a land figure that never fights back, out of the ships' sight
    };
    /// An entry and the bucket of kSight tiles it is in.
    struct Bucketed {
        std::int32_t bx;
        std::int32_t by;
        std::int32_t x8;  ///< the entry's position, here for the distance test
        std::int32_t y8;
        std::uint32_t entry;
        PlayerId owner;
    };
    /// A bucket in use: its owner groups in m_groups.
    struct Cell {
        std::int32_t bx;
        std::int32_t by;
        std::uint32_t first_group;
        std::uint32_t end_group;
    };
    /// The figures of one owner in a bucket: m_buckets[begin, end).
    struct Group {
        PlayerId owner;
        std::uint32_t begin;
        std::uint32_t end;
    };
    struct Seen {
        std::int32_t distance8;
        std::uint32_t entry;
    };
    [[nodiscard]] std::size_t list_index(const Target& unit) const;
    [[nodiscard]] std::optional<Fighter> tower_fighter(const Tower& t) const;

    World* m_world;
    const assets::BuildingTable* m_buildings;
    const assets::FigureTable* m_figures;
    LandMap m_land;
    // Rebuilt every tick; kept as members so that their memory is reused.
    std::vector<Entry> m_entries;
    std::vector<Neighbour> m_neighbours;                           ///< the lists of all entries, one after another
    std::vector<std::uint32_t> m_first;                            ///< per entry: its list in m_neighbours, and the end
    std::vector<std::pair<std::uint64_t, std::uint32_t>> m_index;  ///< unit key -> entry, sorted by key
    std::vector<std::uint8_t> m_hostile;                           ///< players x players
    std::vector<std::uint8_t> m_present;                           ///< per player: has figures
    std::vector<std::uint8_t> m_enemies;                           ///< per player: an enemy has figures
    std::vector<std::uint64_t> m_bucket_keys;                      ///< scratch of update(): the buckets' order
    std::vector<Bucketed> m_buckets;
    std::vector<Cell> m_cells;
    std::vector<Group> m_groups;
    std::vector<Seen> m_seen;
    std::vector<std::uint32_t> m_distances;   ///< scratch of update(): figures seen per distance
    mutable std::vector<Candidate> m_ranked;  ///< what rank() returns
};

}  // namespace o1602::sim
