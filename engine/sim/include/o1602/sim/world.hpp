// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/goods.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/diplomacy.hpp"
#include "o1602/sim/goods_ledger.hpp"
#include "o1602/sim/id_index.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/sim/player.hpp"
#include "o1602/sim/production.hpp"
#include "o1602/sim/random.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/script_state.hpp"
#include "o1602/sim/ship.hpp"
#include "o1602/sim/soldier.hpp"
#include "o1602/sim/world_generator.hpp"

namespace o1602::sim {

/// A spot in a forest where deer live (island + 0x6C, HIRSCH2): the island keeps up to 8.
/// Coordinates are 16 bit like the islands; the original's 8 bits only bind the .GAM export.
struct DeerSpot {
    bool used = false;
    std::uint16_t x = 0;  ///< island tile
    std::uint16_t y = 0;
    std::uint64_t respawn_ms = 0;  ///< game time before which no new deer comes
};

/// A volcano crater: the origin tile of a volcano mountain (docs/re/volcanoes.md).
struct Crater {
    std::uint16_t x = 0;  ///< island tile (8 bit in the original's format, 16 here)
    std::uint16_t y = 0;
    std::uint16_t steps_left = 0;  ///< lava throws left of a running eruption (shown nowhere)
};

struct WorldIsland {
    assets::IslandHeader header;
    IslandMap map;
    std::uint32_t x = 0;  ///< position of the island's tile (0,0) on the world map
    std::uint32_t y = 0;
    std::array<DeerSpot, 8> deer_spots{};
    std::uint8_t deer_phase = 0;  ///< island epochs (30 s) since the last deer census, 0..3
    /// Phase of the island epochs (World::island_phase) the island was last ticked in (INSEL5 +0x0A).
    /// A new game takes it from the template, so islands whose template says otherwise than 0 get
    /// their first epoch at once (docs/re/newgame.md).
    std::uint8_t last_phase = 0;
    /// Drought on the island (docs/re/growth.md#drought).
    struct Drought {
        std::uint8_t strength = 0;   ///< 0 = none; lowers the fertility by strength/128
        std::uint64_t end_ms = 0;    ///< game time it ends; then the strength halves every epoch
        bool curse_pending = false;  ///< a curse of the natives, a drought from the next epoch
    };
    Drought drought{};
    /// Volcano craters, at most two (INSEL5 +0x17/+0x3C).
    std::vector<Crater> craters{};
    bool volcano_erupted = false;  ///< a volcano of the island has erupted (for the computer players)
    /// Players who know the island (island + 0x43): a ship of theirs came close (docs/re/ships.md).
    std::vector<bool> discovered{};

    /// Players who explored the island's crops with a ship (island + 0x42, docs/re/ships.md#erkunden).
    std::vector<bool> explored{};

    [[nodiscard]] bool discovered_by(PlayerId p) const noexcept { return p < discovered.size() && discovered[p]; }
    void discover(PlayerId p) { set_player_flag(discovered, p); }
    [[nodiscard]] bool explored_by(PlayerId p) const noexcept { return p < explored.size() && explored[p]; }
    void explore(PlayerId p) { set_player_flag(explored, p); }

private:
    /// Sets flags[p], growing the list as needed. Grows into a new list instead of resize(): GCC 13
    /// and 14 at -O3 warn about the resize of a vector<bool> (-Warray-bounds, a false positive).
    static void set_player_flag(std::vector<bool>& flags, PlayerId p) {
        if (p >= flags.size()) {
            std::vector<bool> grown(std::size_t{p} + 1, false);
            std::copy(flags.begin(), flags.end(), grown.begin());
            flags.swap(grown);
        }
        flags[p] = true;
    }
};

/// Whether player `p` knows the deposit (explored it; docs/re/ships.md#erkunden). Up to 32 players.
[[nodiscard]] inline bool deposit_known(const assets::IslandHeader::Deposit& d, PlayerId p) noexcept {
    return p < 32 && (d.mask >> p & 1U) != 0;
}

/// Whether `name` can name a player, a city or a ship (the renames of PlayerRename, CityOrder and
/// ShipCommand): 1 to `max_length` bytes of Windows-1252 and no control character, since names come
/// from the network too and must not break lines of lists and files.
[[nodiscard]] bool valid_name(std::string_view name, std::size_t max_length) noexcept;

/// Natives' goodwill towards a player who starts a game (0x0046A220).
inline constexpr std::uint32_t kNativeGoodwillStart = 200;
/// Goodwill never grows above this: far above what trade reaches against the decay (tens of
/// thousands), and low enough that no product with it leaves 32 bits.
inline constexpr std::uint32_t kMaxNativeGoodwill = 1'000'000;

/// A city: the settlement of one player on one island (STADT4 in the original).
struct City {
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint8_t index = 0;    ///< city number on the island (MapCell::city)
    PlayerId owner = kNoPlayer;
    std::string name;  ///< Windows-1252
    /// Goods in the city's warehouses in 1/32 t, indexed by ware number (haeuser.cod); the list
    /// grows with the wares a game knows (ADR 0007). KONTOR2 in the original.
    std::vector<std::uint32_t> goods;
    /// Warehouse the goods are booked at (KONTOR2 position; the original keeps one per city).
    std::optional<std::uint16_t> warehouse_x;
    std::optional<std::uint16_t> warehouse_y;
    /// Goods promised to carriers on their way to a market or warehouse, per ware (city ware
    /// entry +0; not saved, so 0 after loading).
    std::vector<std::uint32_t> reserved;
    /// Goods market carts are bringing, per ware. The original has no such counter and loses what
    /// does not fit when a cart unloads (S-024); Open1602 lets a cart load only what will fit.
    std::vector<std::uint32_t> incoming;
    /// Room per ware in 1/32 t, the same for every ware: the Maxlager of the warehouse plus 10 t
    /// for every further market or warehouse of the city (0x0047AB00). Computed by the logistics.
    std::uint32_t capacity = 0;

    /// Consumption accounting of one good (city + 0x150 + 12 * e, MARKT2): what the inhabitants
    /// asked for and what they got, both decaying by 15/16 every 10 s, in 1/8192 t.
    struct Supply {
        std::uint64_t demand = 0;               ///< A (the original keeps 32 bits and overflows, see consumption.md)
        std::uint64_t supplied = 0;             ///< B
        std::uint8_t ratio = 128;               ///< C = B * 128 / A, at most 128
        std::array<std::uint8_t, 3> history{};  ///< C 10, 20 and 30 s ago
    };
    std::vector<std::uint32_t> inhabitants;  ///< per class of inhabitants (BuildingTable::resident_classes)
    std::vector<std::uint8_t> satisfaction;  ///< per class, 128 = content
    std::vector<std::uint8_t> tax_rate;      ///< per class, 128 = normal (64..192)
    std::vector<Supply> supply;              ///< per ware
    std::uint8_t food_ratio = 128;           ///< supply of food (city + 0x255)
    /// Pride in a castle or cathedral (city + 0x200): each adds 800, the city tick takes 1/256 off.
    /// Every 32 lower the taxes the satisfaction is measured against by 1/128. Not in the .GAM.
    std::uint16_t satisfaction_bonus = 0;
    /// One house per target class may try out the next class (city + 0x234): its inhabitants
    /// count for that class in the demand for one tick, then it upgrades or gives up.
    struct UpgradeSlot {
        std::uint32_t inhabitants = 0;  ///< 0 = free
        std::uint16_t x = 0xFFFF;       ///< origin of the queued house
        std::uint16_t y = 0xFFFF;
    };
    std::vector<UpgradeSlot> upgrade_queue;  ///< by target class
    std::uint8_t epoch = 0;                  ///< counts the city's 10-s ticks, 0..7
    /// The player keeps building material from the people (city window): no house upgrades.
    bool material_lock = false;
    /// Game time the lock was switched on or the people last complained about it (city + 0x1E8).
    std::uint64_t material_reminder_ms = 0;
    std::uint8_t worst_good = 0;  ///< the scarcest good whose supply keeps falling, 0 = none
    /// The last population message of the city (city + 0x253, 0x11 = 100 inhabitants ... 0x1D =
    /// 9000; 0 = none yet): each comes once, as the city first passes its mark.
    std::uint8_t population_message = 0;
    /// Fires and plagues burning in the city now (city + 0x1FE, kept by recount_disasters): any
    /// of them stops the growth of every house.
    std::uint16_t disasters = 0;
    /// Game time of the next check for plague, robbers and fire (city + 0x1E0, every 60-67 s).
    std::uint64_t next_event_ms = 0;
    /// The growth of the house the growth pass looked at last (city + 0x1F8, > 0 grows) and the
    /// game time it last turned positive or the people last wanted a monument (city + 0x1E4). The
    /// original does not save either: after loading its savegames both are 0, here too.
    std::int32_t house_growth = 0;
    std::uint64_t growing_since_ms = 0;
    /// What the Kontor offers to other players' ships and the free trader, per ware (city ware
    /// slot +2, +4, +8; docs/re/ships.md). A ware is sold or bought, not both.
    struct Trade {
        bool sell = false;
        bool buy = false;
        std::uint16_t sell_price = 0;  ///< per ton, what a visiting ship pays
        std::uint16_t buy_price = 0;   ///< per ton, what the city pays
        std::uint32_t keep = 0;        ///< sells only above this stock (1/32 t)
        std::uint32_t limit = 0;       ///< buys only up to this stock (1/32 t)
    };
    std::vector<Trade> trade;  ///< per ware
    /// Natives: goodwill towards each player (city + 0x206); selling to them raises it, buying
    /// from them uses it up. Indexed by PlayerId, one entry per player (fit_goodwill); a player
    /// without an entry gets nothing.
    std::vector<std::uint32_t> goodwill;
    bool trader_inbound = false;  ///< a free trader sails here (city + 0x257 bit 1)
    /// Ships that gave up their way to the Kontor since the last route stop there (city + 0x252);
    /// at four, a computer owner blames the human ships near it (Fleet, docs/re/ships.md).
    std::uint8_t ships_gave_up = 0;
    /// Game time a building of the city was last hit (city + 0x1EC): the natives send more warriors
    /// for 10 minutes after.
    std::uint64_t last_attacked_ms = 0;
    /// Natives: the village has cursed its destroyer (once).
    bool cursed = false;
    /// Trade statistics of the city's Kontor (city + 0x1F0, + 0x1F4): every purchase and sale adds
    /// 32 times its value, the city tick takes 1/90 off every 10 s; divided by 480 they are a
    /// moving average per minute (the city window).
    std::int64_t purchases = 0;
    std::int64_t sales = 0;
    /// Where the goods came from and went to, by game minute (Open1602's goods statistics; the
    /// game never reads it). Every change of `goods` books its share here (book()).
    GoodsLedger ledger;
    /// The figures of the city window by game minute (Open1602's statistics; the game never reads
    /// it).
    CityHistory history;

    [[nodiscard]] const Trade* trade_of(std::uint8_t ware) const noexcept {
        return ware < trade.size() ? &trade[ware] : nullptr;
    }

    [[nodiscard]] std::uint32_t stock(std::uint8_t ware) const noexcept {
        return ware < goods.size() ? goods[ware] : 0;
    }
    [[nodiscard]] std::uint32_t reserved_of(std::uint8_t ware) const noexcept {
        return ware < reserved.size() ? reserved[ware] : 0;
    }
    [[nodiscard]] std::uint32_t incoming_of(std::uint8_t ware) const noexcept {
        return ware < incoming.size() ? incoming[ware] : 0;
    }
    /// Stock nobody has reserved.
    [[nodiscard]] std::uint32_t available(std::uint8_t ware) const noexcept {
        const std::uint32_t s = stock(ware);
        const std::uint32_t r = reserved_of(ware);
        return s > r ? s - r : 0;
    }
    /// Room left for a ware; less than a ton counts as none (0x0047AA00).
    [[nodiscard]] std::uint32_t free_space(std::uint8_t ware) const noexcept {
        const std::uint32_t s = stock(ware);
        const std::uint32_t room = capacity > s ? capacity - s : 0;
        return room < 32 ? 0 : room;
    }
    /// Makes sure the per-ware lists cover `ware`.
    /// Books a movement of goods in the city's statistics (GoodsLedger), in 1/32 t.
    void book(std::uint8_t ware, GoodsFlow flow, std::uint32_t amount) { ledger.add(ware, flow, amount); }

    void ensure_ware(std::uint8_t ware) {
        if (ware >= goods.size()) {
            goods.resize(std::size_t{ware} + 1, 0);
        }
        if (ware >= reserved.size()) {
            reserved.resize(goods.size(), 0);
        }
        if (ware >= incoming.size()) {
            incoming.resize(goods.size(), 0);
        }
    }
};

/// A fire or a plague on a building (the original's list 0x005A5100, docs/re/disasters.md).
struct Disaster {
    enum class Kind : std::uint8_t { Plague = 1, Fire = 2 };
    Kind kind = Kind::Fire;
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint16_t x = 0;       ///< origin tile of the building
    std::uint16_t y = 0;
    std::uint8_t elapsed = 0;   ///< epochs of 10 s it has lasted
    std::uint8_t duration = 0;  ///< epochs until it ends (plague) or burns the building down (fire)
    std::uint32_t id = 0;       ///< order of creation: the epochs handle the oldest first
};

/// A crater named by island and position (the original's object ref of type 0x33).
struct CraterRef {
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint16_t x = 0;       ///< origin of the volcano mountain
    std::uint16_t y = 0;
    friend bool operator==(const CraterRef&, const CraterRef&) = default;
};

/// A volcano erupting (figure VULKAN, kind 0x20 in the original): 141 lava throws over 28.4 s.
struct Eruption {
    CraterRef crater;
    std::uint16_t seed = 0;     ///< decides where the lava flies
    std::uint8_t step = 0;      ///< lava throws so far
    std::uint64_t next_us = 0;  ///< game time of the next step, in microseconds
};

/// A lump of lava in flight (figure VULKANFIRE1, kind 0x21). It flies in legs of half a second;
/// positions are world tiles and heights in half tile widths, both in 1/65536.
struct LavaLump {
    std::uint16_t island = 0;  ///< island of the volcano
    std::int64_t x = 0;        ///< position at the start of the leg
    std::int64_t y = 0;
    std::int64_t z = 0;
    std::int64_t dx = 0;  ///< way of the leg
    std::int64_t dy = 0;
    std::int64_t dz = 0;
    std::uint64_t launch_us = 0;     ///< game time of the throw
    std::uint64_t leg_start_us = 0;  ///< game time of the start and the end of the leg
    std::uint64_t leg_end_us = 0;
    std::int64_t speed = 0;    ///< launch speed in 1/65536 tile per second of the throw
    std::uint8_t heading = 0;  ///< 0..63, steps of 5.625 degrees
    std::uint8_t tilt = 0;     ///< 0..31, steepness class
    bool grounded = false;     ///< the leg ends on the ground
    std::uint8_t anim = 1;     ///< ANIM of the figure (1 or 2), for the display
};

/// Volcanoes of the world (TIMERS +0xC4..+0xD0 in the original).
struct VolcanoState {
    /// Game time from which the next eruption may come (TIMERS +0xCC; 70 min into a new game).
    std::uint64_t next_ms = 42'000ULL * 100;
    /// The crater that erupted last: the next roll may not pick it once (TIMERS +0xC8).
    std::optional<CraterRef> last;
    std::vector<Eruption> eruptions;
    std::vector<LavaLump> lava;
};

/// A wild animal standing on an island (HIRSCH2 in the original: up to 8 per island).
struct Animal {
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint16_t x = 0;       ///< island tile
    std::uint16_t y = 0;
    std::uint16_t figure = 0;  ///< FIGUR number (HIRSCH, ANTILOPE)
};

/// A house and its inhabitants (the original's house list at 0x005A77E8, SIEDLER).
struct House {
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint16_t x = 0;       ///< origin tile of the building
    std::uint16_t y = 0;
    std::uint8_t city = MapCell::kNoCity;
    std::uint8_t resident_class = 0;   ///< BGruppe of the building
    std::uint32_t inhabitants64 = 0;   ///< inhabitants * 64 (the fraction grows over time)
    std::uint8_t timer_group = 0;      ///< 0..15: when the house is ticked
    std::uint8_t epoch = 0;            ///< house tick epoch it was last ticked in
    std::uint8_t queue_epoch = 0;      ///< city epoch when it queued for the next class
    std::uint8_t market_distance = 6;  ///< distance class to the nearest market, 6 = none
    bool market = false;               ///< a market is in range
    bool services_complete = false;    ///< has the public buildings its class needs
    std::uint16_t services = 0;        ///< public buildings in range (bits as the original)

    [[nodiscard]] std::uint32_t inhabitants() const noexcept { return inhabitants64 >> 6; }
};

/// The 16 house timer groups: group g ticks every 15 s + 64 ms * g (docs/re/houses.md).
struct HouseClocks {
    static constexpr std::size_t kGroups = 16;
    std::array<std::uint32_t, kGroups> time_ms{};
    std::array<std::uint8_t, kGroups> epoch{};
};

/// A building with running costs that has no production state: shipyards (on/off) and castles.
struct CostBuilding {
    enum class Kind : std::uint8_t { Shipyard, Castle };
    Kind kind = Kind::Shipyard;
    std::uint16_t island = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    bool enabled = true;
    /// Shipyards (the original's WERFT table 0x005A6C18, docs/re/ships.md): the ship being built
    /// and the material in the yard.
    static constexpr std::uint8_t kNoOrder = 0xFF;
    std::uint8_t order = kNoOrder;  ///< ship type 0..3 (HANDEL1, HANDEL2, KRIEG1, KRIEG2)
    std::uint32_t progress = 0;     ///< build points, 32 per full cycle
    std::uint32_t wood = 0;         ///< Rohstoff in the yard, 1/32 t
    std::uint32_t cloth = 0;        ///< Workstoff in the yard, 1/32 t (the original does not save it)
    std::uint8_t rate = 0;          ///< of the running cycle, 128 = full
    std::uint16_t countdown = 0;    ///< game seconds to the end of the cycle
    std::uint32_t repair_ship = 0;  ///< Ship::id being repaired, 0 = none
    std::uint8_t idle_cycles = 0;   ///< cycles without material, at most 15
    bool fetching = false;          ///< its pack donkey is out
    /// Finished ships that found no free water to be launched on (WERFT + 0x10); at 100 a computer
    /// owner blames the human ships near the yard.
    std::uint8_t failed_launches = 0;
    /// Castles (MILITAR, 0x0055EB80): weapons in store (swords, muskets, cannons, 1/32 t) and the
    /// training slots (3, 5 or 8 used by castle size). `countdown` counts the seconds to the next
    /// training step.
    std::array<std::uint32_t, 3> weapons{};
    std::vector<CastleSlot> slots{};
};

/// A growing field, tree, meadow or fish shoal (ROHWACHS2 in the original). One growth stage of
/// class c lasts 40 s + 7 s * c; at AnimAnz stages the tile turns into its ripe record.
struct GrowthEntry {
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint16_t x = 0;       ///< island tile
    std::uint16_t y = 0;
    std::uint8_t growth_class = 0;  ///< 0..31
    std::uint8_t seen = 0;          ///< class counter when it last grew
    std::uint8_t stage = 0;
};

/// All growth of a world: one clock per growth class (TIMERS) and the growing tiles.
struct GrowthState {
    static constexpr std::size_t kClasses = 32;
    std::array<std::uint32_t, kClasses> time_ms{};  ///< game ms since the class last ticked
    std::array<std::uint8_t, kClasses> counter{};   ///< ticks of the class, counts 0..7
    std::vector<GrowthEntry> entries;
};

class World;

/// An object reference of the original files (SHIP4 targets and route stops, docs/re/ships.md):
/// byte 0 the type (1-3 a ship by its slot, whose id is slot + 1; 0x32-0x34 a building, 0x35 a city,
/// 0x37 a world position). Unknown types give no target.
[[nodiscard]] Target target_from_ref(const World& world, std::uint32_t ref);

/// A unit of a group the player stored (Ctrl + 1 .. 0), with its place in the group's shape.
struct GroupMember {
    bool ship = false;     ///< a ship, else a land unit
    std::uint32_t id = 0;  ///< Ship::id or Soldier::id
    std::int16_t dx = 0;   ///< from the centre of the group, 1/8 tile
    std::int16_t dy = 0;
    friend bool operator==(const GroupMember&, const GroupMember&) = default;
};
/// A player's stored groups, keys 1 .. 9 and 0.
using UnitGroups = std::array<std::vector<GroupMember>, 10>;

/// The world map: open sea with islands placed on it (the original keeps one 4-byte cell per
/// world tile: island number, local x, local y, colour table - see rendering.md). The size is a
/// property of the map; the original always uses 500 x 350 tiles and at most 255 islands
/// (L-003), Open1602 neither (ADR 0007).
class World {
public:
    static constexpr std::uint32_t kOriginalWidth = 500;
    static constexpr std::uint32_t kOriginalHeight = 350;
    /// Island number of open sea (Cell::island).
    static constexpr std::uint16_t kSea = 0xFFFF;
    /// Largest island side: Cell keeps the local position in 16 bits.
    static constexpr std::uint32_t kMaxIslandSide = 0xFFFF;
    /// Open1602's limit per side: 4096 x 4096 tiles take 100 MB. A file that names more is refused
    /// before anything is allocated (the world generator stays below, WorldSettings::kMaxSide).
    static constexpr std::uint32_t kMaxSide = kMaxWorldSide;

    explicit World(std::uint32_t width = kOriginalWidth, std::uint32_t height = kOriginalHeight);

    [[nodiscard]] std::uint32_t width() const noexcept { return m_width; }
    [[nodiscard]] std::uint32_t height() const noexcept { return m_height; }
    [[nodiscard]] bool contains(std::uint32_t x, std::uint32_t y) const noexcept { return x < m_width && y < m_height; }

    /// What lies at a world tile.
    struct Cell {
        std::uint16_t island = kSea;  ///< index into islands(), kSea for open sea
        std::uint16_t local_x = 0;
        std::uint16_t local_y = 0;
    };

    [[nodiscard]] const Cell& cell(std::uint32_t x, std::uint32_t y) const {
        return m_cells[std::size_t{y} * m_width + x];
    }
    [[nodiscard]] const std::vector<WorldIsland>& islands() const noexcept { return m_islands; }
    /// Mutable island for the simulation (fields growing, deposits running out).
    [[nodiscard]] WorldIsland& island(std::size_t index) { return m_islands[index]; }

    /// Adds an island at its header position. Fails if it leaves the world or overlaps another one.
    core::Status add_island(WorldIsland island);

    [[nodiscard]] const std::vector<Player>& players() const noexcept { return m_players; }
    [[nodiscard]] const std::vector<Ship>& ships() const noexcept { return m_ships; }
    [[nodiscard]] std::vector<Ship>& ships() noexcept { return m_ships; }
    /// Player of an owner number, nullptr for "nobody" or unknown slots.
    [[nodiscard]] const Player* player(PlayerId owner) const noexcept {
        return owner < m_players.size() ? &m_players[owner] : nullptr;
    }
    [[nodiscard]] Player* player(PlayerId owner) noexcept {
        return owner < m_players.size() ? &m_players[owner] : nullptr;
    }
    /// Replaces the players. A different number of players starts the diplomacy over with the
    /// relations of a new game (Diplomacy::reset).
    void set_players(std::vector<Player> players) {
        m_players = std::move(players);
        if (m_diplomacy.size() != m_players.size()) {
            m_diplomacy.reset(m_players, m_factions);
        }
    }

    /// The factions of this game (ADR 0007, section 3): the base game's (base_factions()) at
    /// their indices first, those of content packages after them.
    [[nodiscard]] const std::vector<Faction>& factions() const noexcept { return m_factions; }
    /// Replaces the factions; a player of a faction that is no longer there becomes a settler.
    void set_factions(std::vector<Faction> factions);
    /// The values of the balance this game plays with (balance.hpp): the original's unless a
    /// package changed them before the game started.
    [[nodiscard]] const Balance& balance() const noexcept { return m_balance; }
    void set_balance(const Balance& balance) noexcept { m_balance = balance; }
    /// The content packages this game uses, in their load order (ADR 0007 section 7): those
    /// installed when it started, kept in the savegame (section PACK). Nothing for a game from a
    /// savegame of a version before them, which plays with the packages installed now.
    [[nodiscard]] const std::optional<std::vector<assets::ContentPackage>>& packages() const noexcept {
        return m_packages;
    }
    void set_packages(std::vector<assets::ContentPackage> packages) { m_packages = std::move(packages); }
    /// The buildings the packages added and the Ids this game gave them (own_buildings.hpp), kept
    /// in the savegame (section OWNB): the islands' tiles name those Ids, so a game only loads
    /// where its packages give the same buildings the same Ids again.
    [[nodiscard]] const std::vector<assets::OwnBuilding>& own_buildings() const noexcept { return m_own_buildings; }
    void set_own_buildings(std::vector<assets::OwnBuilding> own) { m_own_buildings = std::move(own); }
    /// The goods this game trades: the original's and those its packages added (goods.hpp), kept
    /// in the savegame (section GOOD).
    [[nodiscard]] const assets::Goods& goods() const noexcept { return m_goods; }
    void set_goods(assets::Goods goods) { m_goods = std::move(goods); }
    /// The classes of inhabitants as the packages changed and added them (resident_classes.hpp),
    /// kept with the game; empty: haeuser.cod's. The game puts them into its building table
    /// (BuildingTable::set_resident_classes), which the simulation reads them from.
    [[nodiscard]] const std::vector<assets::ResidentClass>& resident_classes() const noexcept { return m_classes; }
    void set_resident_classes(std::vector<assets::ResidentClass> classes) { m_classes = std::move(classes); }
    /// The unlock levels as the packages changed and added them (unlock_levels.hpp), kept with the
    /// game; empty: haeuser.cod's. The game puts them into its building table
    /// (BuildingTable::set_infra_levels), which the simulation reads them from.
    [[nodiscard]] const std::vector<assets::InfraLevel>& infra_levels() const noexcept { return m_levels; }
    void set_infra_levels(std::vector<assets::InfraLevel> levels) { m_levels = std::move(levels); }
    /// The faction a player belongs to; the settlers' for a faction the world does not have.
    [[nodiscard]] const Faction& faction(const Player& player) const noexcept {
        return player.faction < m_factions.size() ? m_factions[player.faction] : m_factions[faction::kSettlers];
    }
    [[nodiscard]] FactionBehaviour behaviour(const Player& player) const noexcept { return faction(player).behaviour; }
    /// Whether the player `p` is one of a faction that acts as `behaviour`; false for nobody.
    [[nodiscard]] bool acts_as(PlayerId p, FactionBehaviour behaviour) const noexcept {
        const Player* player = this->player(p);
        return player != nullptr && this->behaviour(*player) == behaviour;
    }
    /// The first player of a faction that acts as `behaviour`, kNoPlayer if there is none.
    [[nodiscard]] PlayerId first_of(FactionBehaviour behaviour) const noexcept;
    /// Adds a ship; one without an id gets the next free one.
    void add_ship(Ship ship);
    /// Ship with this id, nullptr if it is gone.
    [[nodiscard]] const Ship* ship(std::uint32_t id) const noexcept;
    [[nodiscard]] Ship* ship(std::uint32_t id) noexcept;
    /// Removes the ships `gone` says so; the others keep their order.
    void erase_ships(const std::function<bool(const Ship&)>& gone);
    /// Id the next new ship gets.
    [[nodiscard]] std::uint32_t next_ship_id() const noexcept { return m_next_ship_id; }
    void set_next_ship_id(std::uint32_t id) noexcept { m_next_ship_id = id; }

    [[nodiscard]] const std::vector<City>& cities() const noexcept { return m_cities; }
    /// City `index` on island `island` (index into islands()), nullptr if there is none.
    [[nodiscard]] const City* city(std::uint16_t island, std::uint8_t index) const noexcept;
    void add_city(City city);
    /// Rules::reveal: discovers for every player each island with a city that `reveal` shows from
    /// the start, or only `island` with one (World::kSea: every island, which Reveal::Original
    /// leaves alone; it shows an island only when a city is founded there). No ship event: nobody
    /// sailed there.
    void reveal_islands(Reveal reveal, std::uint16_t island = kSea);
    /// Removes a city (it was dissolved); pointers to cities become invalid.
    void remove_city(std::uint16_t island, std::uint8_t index);
    /// Mutable lookup for the simulation.
    [[nodiscard]] City* city(std::uint16_t island, std::uint8_t index) noexcept;

    [[nodiscard]] const std::vector<Animal>& animals() const noexcept { return m_animals; }
    void add_animal(Animal animal) { m_animals.push_back(animal); }

    [[nodiscard]] const std::vector<Soldier>& soldiers() const noexcept { return m_soldiers; }
    [[nodiscard]] std::vector<Soldier>& soldiers() noexcept { return m_soldiers; }
    /// Adds a land unit; one without an id gets the next free one.
    void add_soldier(Soldier soldier);
    /// Unit with this id, nullptr if it is gone.
    [[nodiscard]] const Soldier* soldier(std::uint32_t id) const noexcept;
    [[nodiscard]] Soldier* soldier(std::uint32_t id) noexcept;
    /// Removes the units `gone` says so; the others keep their order.
    void erase_soldiers(const std::function<bool(const Soldier&)>& gone);
    [[nodiscard]] std::uint32_t next_soldier_id() const noexcept { return m_next_soldier_id; }
    void set_next_soldier_id(std::uint32_t id) noexcept { m_next_soldier_id = id; }

    /// Cannon towers of the Wachturm buildings.
    [[nodiscard]] const std::vector<Tower>& towers() const noexcept { return m_towers; }
    [[nodiscard]] std::vector<Tower>& towers() noexcept { return m_towers; }
    /// Damage of buildings under attack, and shots on their way.
    [[nodiscard]] const std::vector<BuildingDamage>& building_damage() const noexcept { return m_building_damage; }
    [[nodiscard]] std::vector<BuildingDamage>& building_damage() noexcept { return m_building_damage; }
    [[nodiscard]] const std::vector<PendingHit>& pending_hits() const noexcept { return m_hits; }
    [[nodiscard]] std::vector<PendingHit>& pending_hits() noexcept { return m_hits; }

    /// Peace and trade treaties and what players remember of each other.
    [[nodiscard]] const Diplomacy& diplomacy() const noexcept { return m_diplomacy; }
    [[nodiscard]] Diplomacy& diplomacy() noexcept { return m_diplomacy; }

    /// Replace whole lists in their order (restoring a saved game, savegame_extension.hpp).
    void set_cities(std::vector<City> cities);
    void set_animals(std::vector<Animal> animals) { m_animals = std::move(animals); }
    void set_soldiers(std::vector<Soldier> soldiers) { m_soldiers = std::move(soldiers); }
    void set_houses(std::vector<House> houses);
    /// All slots of the production list, used or free, and the free slots in the order they are
    /// taken again (last first).
    void set_productions(std::vector<Production> productions, std::vector<std::uint32_t> free);
    [[nodiscard]] const std::vector<std::uint32_t>& free_productions() const noexcept { return m_free_productions; }

    [[nodiscard]] const std::vector<House>& houses() const noexcept { return m_houses; }
    [[nodiscard]] std::vector<House>& houses() noexcept { return m_houses; }
    /// House whose building has its origin at (x, y), nullptr if none.
    [[nodiscard]] const House* house(std::uint16_t island, std::uint16_t x, std::uint16_t y) const;
    [[nodiscard]] House* house(std::uint16_t island, std::uint16_t x, std::uint16_t y);
    /// Adds a house (or replaces the one at its position).
    void add_house(const House& house);
    /// Removes the house at (x, y); the order of the others may change.
    void remove_house(std::uint16_t island, std::uint16_t x, std::uint16_t y);

    /// Next name of the city name list a new city gets (TIMERS +0xB0, round robin).
    [[nodiscard]] std::uint32_t city_name_index() const noexcept { return m_city_name_index; }
    void set_city_name_index(std::uint32_t i) noexcept { m_city_name_index = i; }

    /// Fires and plagues (not saved by the original, S-001).
    [[nodiscard]] const std::vector<Disaster>& disasters() const noexcept { return m_disasters; }
    [[nodiscard]] std::vector<Disaster>& disasters() noexcept { return m_disasters; }
    [[nodiscard]] std::uint32_t next_disaster_id() noexcept { return m_next_disaster_id++; }
    [[nodiscard]] std::uint32_t peek_disaster_id() const noexcept { return m_next_disaster_id; }
    void set_next_disaster_id(std::uint32_t id) noexcept { m_next_disaster_id = id; }

    /// Settings the original keeps with a game (TIMERS): the difficulty 0..3 (how often droughts
    /// come) and the settings word (bit 2: no droughts).
    /// TIMERS +0xA8 and its neighbours (docs/formats/gam.md).
    struct GameSettings {
        static constexpr std::uint32_t kNoPirates = 1U << 0;     ///< flags: no pirates
        static constexpr std::uint32_t kNoComputers = 1U << 1;   ///< flags: computer opponents idle
        static constexpr std::uint32_t kNoDroughts = 1U << 2;    ///< flags: no droughts
        static constexpr std::uint32_t kEndlessVeins = 1U << 3;  ///< flags: large iron veins never run out
        static constexpr std::uint32_t kIronMined = 1U << 4;     ///< flags: an iron mine was built (for good)
        static constexpr std::uint32_t kPirateGoal = 1U << 5;    ///< flags: defeat the pirates
        static constexpr std::uint32_t kNoTrader = 1U << 6;      ///< flags: no free trader
        static constexpr std::uint32_t kNoVolcanoes = 1U << 8;   ///< flags: no volcanoes
        static constexpr std::uint32_t kNoMilitary = 1U << 9;    ///< flags: no military buildings

        std::uint8_t difficulty = 2;
        std::uint32_t flags = 0;                 ///< the k... bits above
        std::uint32_t pirate_interval_s = 1500;  ///< TIMERS +0xB8: time between two new pirate ships
        /// TIMERS +0xA5 (0x005B62FC): the tutorial; set only in Einführungsspiel0-4. Its computer
        /// opponents keep their towns as they are and do not recruit, buy peace or order ships.
        bool tutorial = false;
    };
    [[nodiscard]] const GameSettings& game_settings() const noexcept { return m_game_settings; }
    void set_game_settings(const GameSettings& s) noexcept { m_game_settings = s; }
    /// Game time from which droughts may come again (TIMERS +0xB4; 90 min into a new game).
    [[nodiscard]] std::uint64_t next_drought_ms() const noexcept { return m_next_drought_ms; }
    void set_next_drought_ms(std::uint64_t t) noexcept { m_next_drought_ms = t; }
    /// Phase of the drought table (0..31, the original's 0x00548D30); drawn once per world. The
    /// original rolls it anew at every load and does not save it.
    [[nodiscard]] std::optional<std::uint8_t> drought_seed() const noexcept { return m_drought_seed; }
    void set_drought_seed(std::uint8_t s) noexcept { m_drought_seed = static_cast<std::uint8_t>(s & 31U); }

    /// The file the world was loaded from: the savegame writer starts from its records and keeps
    /// what Open1602 does not model yet (savegame_writer.hpp). Null for a world built in code.
    [[nodiscard]] const std::shared_ptr<const assets::ChunkFile>& source() const noexcept { return m_source; }
    void set_source(std::shared_ptr<const assets::ChunkFile> file) noexcept { m_source = std::move(file); }

    /// State of the running simulation (clocks, figures) from Open1602's own savegame chunk, kept
    /// until a Simulation takes it over (Simulation::Simulation).
    [[nodiscard]] const std::vector<std::uint8_t>& simulation_state() const noexcept { return m_simulation_state; }
    void set_simulation_state(std::vector<std::uint8_t> state) noexcept { m_simulation_state = std::move(state); }
    [[nodiscard]] std::vector<std::uint8_t> take_simulation_state() noexcept { return std::move(m_simulation_state); }
    /// The state of the packages' scripts the savegame kept (section SCRP), which the scripts take up
    /// when the game goes on (scripting.hpp). A package without an entry has not started its scripts
    /// in this game yet.
    [[nodiscard]] const std::vector<ScriptState>& script_states() const noexcept { return m_script_states; }
    void set_script_states(std::vector<ScriptState> states) noexcept { m_script_states = std::move(states); }
    /// The groups each player stored (by PlayerId), kept with the savegame (section UGRP) so that a
    /// loaded game has them again; the original forgets them. Only the interface reads them, and the
    /// game's fingerprint leaves them out: in a multiplayer game every computer knows only its own.
    [[nodiscard]] const std::vector<UnitGroups>& unit_groups() const noexcept { return m_unit_groups; }
    void set_unit_groups(std::vector<UnitGroups> groups) noexcept { m_unit_groups = std::move(groups); }
    void set_unit_groups(PlayerId player, UnitGroups groups) {
        if (player >= m_unit_groups.size()) {
            m_unit_groups.resize(std::size_t{player} + 1);
        }
        m_unit_groups[player] = std::move(groups);
    }
    /// Game settings saved with the game, which a Simulation of it uses (ADR 0008).
    [[nodiscard]] const std::optional<Rules>& saved_rules() const noexcept { return m_saved_rules; }
    void set_saved_rules(std::optional<Rules> rules) noexcept { m_saved_rules = rules; }
    /// The bugs of the original the running game plays with (Rules::bugs), for what sees only the
    /// world: construction and its preview in the interface, the windows. The Simulation sets them
    /// from its rules; they are not saved, the rules are.
    [[nodiscard]] const OriginalBugs& original_bugs() const noexcept { return m_original_bugs; }
    void set_original_bugs(const OriginalBugs& bugs) noexcept { m_original_bugs = bugs; }

    /// Eruptions, lava in flight and the time of the next eruption (volcanoes.hpp).
    [[nodiscard]] const VolcanoState& volcanoes() const noexcept { return m_volcanoes; }
    [[nodiscard]] VolcanoState& volcanoes() noexcept { return m_volcanoes; }

    /// Phase of the island epochs, 0..7, one step every 30 s (TIMERS +0x01): an island is ticked
    /// when its last phase differs.
    [[nodiscard]] std::uint8_t island_phase() const noexcept { return m_island_phase; }
    void set_island_phase(std::uint8_t p) noexcept { m_island_phase = static_cast<std::uint8_t>(p & 7U); }

    /// Game time since the start of the game (TIMERS +0xA0 of a savegame).
    [[nodiscard]] std::uint64_t time_ms() const noexcept { return m_time_ms; }
    void set_time_ms(std::uint64_t t) noexcept { m_time_ms = t; }

    [[nodiscard]] const HouseClocks& house_clocks() const noexcept { return m_house_clocks; }
    [[nodiscard]] HouseClocks& house_clocks() noexcept { return m_house_clocks; }
    [[nodiscard]] const std::vector<CostBuilding>& cost_buildings() const noexcept { return m_cost_buildings; }
    [[nodiscard]] std::vector<CostBuilding>& cost_buildings() noexcept { return m_cost_buildings; }
    void add_cost_building(const CostBuilding& b) { m_cost_buildings.push_back(b); }

    /// Random generators of the simulation, one per subsystem (random.hpp).
    [[nodiscard]] Random& random(RandomStream stream) noexcept { return m_random[stream]; }
    [[nodiscard]] const Random& random(RandomStream stream) const noexcept { return m_random[stream]; }
    void reseed(std::uint64_t seed) noexcept { m_random.reseed(seed); }

    [[nodiscard]] const GrowthState& growth() const noexcept { return m_growth; }
    [[nodiscard]] GrowthState& growth() noexcept { return m_growth; }

    [[nodiscard]] const std::vector<Production>& productions() const noexcept { return m_productions; }
    /// Mutable for the simulation; entries keep their index (other entries may refer to it).
    [[nodiscard]] std::vector<Production>& productions() noexcept { return m_productions; }
    /// Production state of the building whose origin tile is (x, y) on island `island` (index into
    /// islands()), nullptr if it has none. Constant time: the renderer asks for every visible
    /// production building in every frame.
    [[nodiscard]] const Production* production(std::uint16_t island, std::uint16_t x, std::uint16_t y) const;
    /// Index into productions() of that building.
    [[nodiscard]] std::optional<std::uint32_t> production_index(std::uint16_t island, std::uint16_t x,
                                                                std::uint16_t y) const;
    /// Adds or replaces the state of the building at (p.island, p.x, p.y). A new entry takes a
    /// free slot if there is one.
    void set_production(const Production& p);
    /// Frees the entry of the building at (x, y); returns its index.
    std::optional<std::uint32_t> remove_production(std::uint16_t island, std::uint16_t x, std::uint16_t y);

    /// Missions of the game (AUFTRAG4 in the original, which has 4 slots): each player names one
    /// or none (Player::mission).
    [[nodiscard]] const std::vector<Mission>& missions() const noexcept { return m_missions; }
    void set_missions(std::vector<Mission> missions) { m_missions = std::move(missions); }
    [[nodiscard]] const Mission* mission_of(const Player& player) const noexcept {
        return player.mission < m_missions.size() ? &m_missions[player.mission] : nullptr;
    }
    /// The end of the game: a player accomplished the mission (docs/re/missions.md). Missions are
    /// not checked any more afterwards; whether the game goes on is the frontend's choice.
    struct GameEnd {
        std::uint64_t time_ms = 0;
        PlayerId winner = kNoPlayer;
        friend bool operator==(const GameEnd&, const GameEnd&) = default;
    };
    [[nodiscard]] const std::optional<GameEnd>& game_end() const noexcept { return m_game_end; }
    void set_game_end(std::optional<GameEnd> end) noexcept { m_game_end = end; }

    /// Index into islands() of the island with this number (INSEL5 header), if it is placed.
    [[nodiscard]] std::optional<std::uint16_t> island_index(std::uint32_t number) const noexcept;

private:
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::vector<WorldIsland> m_islands;
    std::vector<Player> m_players;
    std::vector<Faction> m_factions = base_factions();
    Balance m_balance;
    std::optional<std::vector<assets::ContentPackage>> m_packages;
    std::vector<assets::OwnBuilding> m_own_buildings;
    assets::Goods m_goods;
    std::vector<assets::ResidentClass> m_classes;
    std::vector<assets::InfraLevel> m_levels;
    std::vector<Ship> m_ships;
    std::uint32_t m_next_ship_id = 1;
    IdIndex m_ship_index;
    std::vector<City> m_cities;
    /// (island << 8 | index) << 32 | slot of every city, sorted: the lookup of city() (TB-21).
    std::vector<std::uint64_t> m_city_keys;
    void index_cities();
    std::vector<Animal> m_animals;
    std::vector<Soldier> m_soldiers;
    std::uint32_t m_next_soldier_id = 1;
    IdIndex m_soldier_index;
    std::vector<std::uint32_t> m_removed;  ///< scratch of erase_ships() and erase_soldiers()
    std::vector<Tower> m_towers;
    std::vector<BuildingDamage> m_building_damage;
    std::vector<PendingHit> m_hits;
    Diplomacy m_diplomacy;
    std::vector<Production> m_productions;
    GrowthState m_growth;
    RandomStreams m_random;
    std::vector<House> m_houses;
    HouseClocks m_house_clocks;
    std::uint64_t m_time_ms = 0;
    std::uint8_t m_island_phase = 0;
    std::uint32_t m_city_name_index = 0;
    std::vector<Disaster> m_disasters;
    std::uint32_t m_next_disaster_id = 0;
    GameSettings m_game_settings;
    std::uint64_t m_next_drought_ms = 54'000ULL * 100;
    std::optional<std::uint8_t> m_drought_seed;
    VolcanoState m_volcanoes;
    std::shared_ptr<const assets::ChunkFile> m_source;
    std::vector<std::uint8_t> m_simulation_state;
    std::vector<ScriptState> m_script_states;
    std::vector<UnitGroups> m_unit_groups;
    std::optional<Rules> m_saved_rules;
    OriginalBugs m_original_bugs;
    std::vector<CostBuilding> m_cost_buildings;
    std::unordered_map<std::uint64_t, std::uint32_t> m_house_index;       ///< key: island, y, x
    std::unordered_map<std::uint64_t, std::uint32_t> m_production_index;  ///< key: island, y, x
    std::vector<std::uint32_t> m_free_productions;                        ///< free slots, reused last first
    std::vector<Cell> m_cells;
    std::vector<Mission> m_missions;
    std::optional<GameEnd> m_game_end;
};

/// The Kontor building of a city (at its warehouse position), nullptr if it has none.
[[nodiscard]] const assets::BuildingType* kontor_of(const World& world, const assets::BuildingTable& buildings,
                                                    const City& city);

/// A Kontor allows `Maxware` wares to sell and as many to buy as its second value (HAUS +0x45,
/// +0x46). When a Kontor of a city is built or loaded and the city sells or buys more wares than
/// it allows, all the city's trade settings are switched off (0x00481EE0). True if they were.
/// Data without Maxware (both 0) sets no limit.
bool trim_trade_settings(City& city, const assets::BuildingType& kontor);

/// Gives every natives' city one goodwill entry per player: the original's records have seven,
/// so players five on (ids 7 on) had none and bought without limit. A new entry is
/// kNativeGoodwillStart for a human or computer player, 0 for the others; every entry is at most
/// kMaxNativeGoodwill. Run when a game starts or is loaded.
void fit_goodwill(World& world);

/// The player at this computer until there is a lobby: the first human player, kNoPlayer if none.
[[nodiscard]] PlayerId first_human_player(const World& world) noexcept;

/// Loads a scenario (.SZS/.SZM) or savegame (.GAM): all islands, each on its island template and
/// placed on the world map, the players and the ships. Problems are collected in `warnings`; islands that cannot be
/// placed are skipped. A scenario that makes its islands at the start (the endless games) gets
/// them as `start` says (world_generator.hpp); the world's source is then that scenario with
/// the islands made.
core::Result<World> load_world(const assets::ChunkFile& file, const assets::BuildingTable& buildings,
                               const assets::GameDirectory& game, std::vector<std::string>* warnings = nullptr,
                               const WorldStart& start = {});

}  // namespace o1602::sim
