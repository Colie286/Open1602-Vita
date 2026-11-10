// SPDX-License-Identifier: GPL-3.0-or-later
//
// Goods on the move between the buildings of a city (docs/re/logistics.md):
//
// - Carriers: a workshop that runs short of raw material sends a carrier (0x0044AB60). He walks
//   over roads and open ground to the nearest building of his city that has the ware - a producer
//   of it, a market or the warehouse - takes up to what he can carry and brings it home.
// - Market carts: every market and warehouse sends carts (up to Figuranz, 0x0044AD50). A cart
//   drives on roads to the fullest producer of the city in its circle and brings the goods to the
//   city's warehouse. Finished goods are always collected; raw material and intermediate goods
//   only from a full store, the rest is left to the carriers of the next workshop.
//
// Goods are reserved when a figure sets out, so two figures never count on the same ton. A
// figure walks only inside the circle of its home building (pathfinding.hpp). Everything runs
// in fixed ticks with integer positions (ADR 0004); figures are not saved, like in the original.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/disasters.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/pool.hpp"
#include "o1602/sim/production_cycle.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/state_io.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// A building on an island as the logistics sees it.
struct BuildingAt {
    const assets::BuildingType* type = nullptr;
    TilePos origin;
    std::uint8_t rotation = 0;
    std::uint32_t width = 1;  ///< rotated footprint
    std::uint32_t height = 1;
    std::uint8_t city = MapCell::kNoCity;
};

/// The building covering `tile` of island `island`, if any.
[[nodiscard]] std::optional<BuildingAt> building_at(const World& world, const assets::BuildingTable& buildings,
                                                    std::uint16_t island, TilePos tile);

/// Door of a building (0x004634C0): its centre tile, or with Tuerflg the middle tile of one edge,
/// which edge depending on the rotation.
[[nodiscard]] TilePos door_tile(const BuildingAt& building) noexcept;
/// Centre tile: origin + (size - 1) / 2.
[[nodiscard]] TilePos centre_tile(const BuildingAt& building) noexcept;

/// Forest spots where deer may settle (0x0046B920): tiles of odd rows and the island's column
/// parity where at least 4 of the tile and its 4 neighbours carry trees.
[[nodiscard]] std::vector<TilePos> deer_census_spots(const World& world, const assets::BuildingTable& buildings,
                                                     std::uint16_t island);
/// How many deer a forest with `spots` such spots feeds: min(8, (spots + 5) / 8).
[[nodiscard]] std::uint32_t deer_allowed(std::size_t spots) noexcept;

enum class WalkerKind : std::uint8_t {
    Carrier,      ///< fetches raw material for a workshop (figure kind 8)
    Cart,         ///< brings goods of producers to the warehouse (figure kind 0x0B)
    Harvester,    ///< farm worker: harvests one ripe field, tree or quarry tile (kind 0x0C)
    Grazer,       ///< cattle or sheep: grazes ripe meadow tiles until full (kind 0x10)
    Fisher,       ///< fishing boat: fishes ripe shoals until full (kind 0x0D)
    Deer,         ///< deer or antelope: grazes around its spot in the forest (kind 0x11)
    Hunter,       ///< hunter of a hunting lodge, called by a grazing deer (kind 0x0E)
    Doctor,       ///< doctor of a Klinik: cures houses with the plague (kind 9)
    Firefighter,  ///< fire brigade ("Feuerwehr", HAUS_PRODTYP BRUNNEN): puts out fires (kind 10)
    Robber,       ///< robs carriers and carts on their way home (kind 0x0F)
};

/// Doctors and firemen: they walk to disasters of their city and end them.
[[nodiscard]] constexpr bool responds(WalkerKind kind) noexcept {
    return kind == WalkerKind::Doctor || kind == WalkerKind::Firefighter;
}

/// Workers that harvest tiles themselves instead of fetching from buildings.
[[nodiscard]] constexpr bool harvests(WalkerKind kind) noexcept {
    return kind == WalkerKind::Harvester || kind == WalkerKind::Grazer || kind == WalkerKind::Fisher;
}

/// Figures that walk at full speed on every ground, as in the original (S-026).
[[nodiscard]] constexpr bool full_speed(WalkerKind kind) noexcept {
    return harvests(kind) || kind == WalkerKind::Deer || kind == WalkerKind::Hunter;
}

/// A figure on its way.
struct Walker {
    WalkerKind kind = WalkerKind::Carrier;
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint16_t figure = 0;  ///< FIGUR number (TRAEGER, KARREN, ...)
    std::uint8_t city = 0;     ///< city on the island
    std::uint32_t home = 0;    ///< index into World::productions()
    TilePos home_tile;         ///< where it delivers: the door (carriers) or the centre (carts); deer: its place
    std::optional<std::uint32_t> source;  ///< producer it fetches from (index into productions)
    TilePos source_origin;                ///< origin of the building it fetches from (producer or warehouse)
    bool from_warehouse = false;          ///< fetches from a market or warehouse (city stock)
    std::uint8_t ware = 0;
    std::uint16_t reserved = 0;  ///< reserved at the source, not picked up yet
    std::uint16_t load = 0;      ///< carried, 1/32 t
    std::uint16_t expected = 0;  ///< carts: room reserved in the warehouse (City::incoming)
    bool returning = false;
    std::optional<TilePos> field;  ///< workers: the ripe tile they reserved
    std::uint32_t work_ms = 0;     ///< workers: game time left at the field
    /// ANIM of the figure: carriers and carts 1 empty, 0 loaded; farm workers 1 out, 2 working,
    /// 0 home; cattle and fishing boats 0 moving, 1 working.
    std::uint8_t animation = 1;
    // Deer and hunters.
    std::uint8_t grazes = 0;        ///< deer: grass stops since the last tree
    bool dying = false;             ///< deer: falls (shot, or no way left)
    bool shot = false;              ///< deer: hit by a hunter
    bool retargeted = false;        ///< hunter: followed the deer once
    Handle partner;                 ///< the deer a hunter hunts, the hunter after a deer; a robber's victim
    std::optional<TilePos> target;  ///< doctors and firemen: origin of the building they go to
    std::uint8_t patience = 0;      ///< robbers: seconds waited plus tiles chased
    /// Pack donkeys of shipyards: origin of the yard (home is then kNoHome; the yard is no
    /// production building).
    std::optional<TilePos> yard;

    // Movement: the figure walks from `tile` towards path[step]; `progress` of `length` (units of
    // 1/65536 tile) is done, at `speed` units per tick.
    TilePos tile;
    std::vector<TilePos> path;
    std::uint32_t step = 0;
    std::uint32_t progress = 0;
    std::uint32_t length = 0;
    std::uint32_t speed = 0;
    std::uint8_t direction = 0;
    std::uint8_t walk_speed = 32;  ///< Wegspeed of the tile the step started on (animation pace)

    [[nodiscard]] bool moving() const noexcept { return length != 0; }
    [[nodiscard]] bool working() const noexcept { return work_ms != 0; }
    [[nodiscard]] std::uint8_t anim() const noexcept { return animation; }
};

/// Island-tile position of a walker including the part of its step (tile centre = x + 0.5).
struct WalkerPosition {
    double x = 0.0;
    double y = 0.0;
};
[[nodiscard]] WalkerPosition walker_position(const Walker& walker) noexcept;

struct LogisticsStats {
    std::uint64_t carrier_trips = 0;    ///< carriers sent out
    std::uint64_t cart_trips = 0;       ///< carts sent out
    std::uint64_t raw_delivered = 0;    ///< raw material brought to workshops, 1/32 t
    std::uint64_t goods_collected = 0;  ///< goods brought to warehouses, 1/32 t
    std::uint64_t worker_trips = 0;     ///< farm workers, cattle and fishing boats sent out
    std::uint64_t harvested = 0;        ///< raw material harvested from tiles, 1/32 t
    std::uint64_t ore_mined = 0;        ///< raw material mines took from deposits, 1/32 t
    std::uint64_t deer_spawned = 0;     ///< deer that appeared at their spots
    std::uint64_t deer_hunted = 0;      ///< deer brought home by hunters
    std::uint64_t robbers = 0;          ///< robbers that set out
    std::uint64_t robberies = 0;        ///< carriers and carts robbed
    std::uint64_t stolen = 0;           ///< goods they carried, 1/32 t
    std::uint64_t cured = 0;            ///< houses cured by doctors
    std::uint64_t extinguished = 0;     ///< fires put out by firemen
};

class Logistics {
public:
    /// Units of a step: 1 tile = 65536.
    static constexpr std::uint32_t kTileUnits = 65536;
    /// Length of a diagonal step (sqrt 2 tiles).
    static constexpr std::uint32_t kDiagonalUnits = 92682;
    /// Game time per tick (Simulation::kTickMs).
    static constexpr std::uint32_t kTickMs = 50;
    /// The original's carrier (TRAEGER), sent for the second raw material of non-workshops.
    static constexpr std::uint16_t kCarrierFigure = figure::kCarrier;
    /// Walker::home of figures without a home building (deer).
    static constexpr std::uint32_t kNoHome = 0xFFFFFFFFU;

    /// `rules` (may be null: the original's) must outlive the logistics.
    Logistics(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
              const Rules* rules = nullptr);

    /// Recomputes the warehouse room of every city (after loading or building).
    void update_storage();

    /// Reacts to a production event: NeedsRawGoods sends a carrier or a cart.
    void handle(const ProductionEvent& event);

    /// One tick of game time: every figure walks; at the end of its path it picks up or delivers.
    void tick();

    /// Buildings with these production entries are gone: their figures end their trip (their load
    /// is lost, their reservations are released) - S-002, unless the game brings the bug back
    /// (OriginalBugs::goldsheep): then they walk on. One pass over the figures for all of
    /// them (TB-22); the figures end in the order of the buildings, as one call each would.
    void buildings_removed(std::span<const std::uint32_t> productions);

    /// Every used deer spot gets its deer (the original does this when it loads a game).
    void spawn_deer_at_spots();
    /// One island epoch (30 s, 0x0046B3E0): expired spots get their deer back or are given up, and
    /// every fourth epoch the island counts its forest spots and may settle new deer.
    void advance_islands(const std::vector<bool>* due = nullptr);
    /// Whether a worker is on the way to harvest this tile.
    [[nodiscard]] bool field_reserved(std::uint16_t island, TilePos tile) const;
    /// A robber the city tick sent out (0x0044B620): he waits in the house for a victim.
    void spawn_robber(const RobberSpawn& spawn);
    /// A shipyard's pack donkey fetches `ware` from a market or Kontor of its city within the
    /// yard's Radius (0x0044AB60 with PACKESEL). False if there is nothing to fetch.
    bool send_yard_carrier(std::uint16_t island, TilePos origin, std::uint8_t ware, std::uint16_t figure);
    /// What pack donkeys brought to shipyards (amount 0: the trip failed).
    struct YardDelivery {
        std::uint16_t island = 0;
        TilePos origin;
        std::uint8_t ware = 0;
        std::uint32_t amount = 0;
    };
    [[nodiscard]] std::vector<YardDelivery> take_yard_deliveries() {
        std::vector<YardDelivery> d;
        d.swap(m_yard_deliveries);
        return d;
    }
    /// Disasters doctors and firemen ended since the last call.
    [[nodiscard]] std::vector<DisasterEvent> take_disaster_events() {
        std::vector<DisasterEvent> events;
        events.swap(m_disaster_events);
        return events;
    }
    /// Game time per island epoch.
    static constexpr std::uint32_t kIslandEpochMs = 30'000;

    [[nodiscard]] const Pool<Walker>& walkers() const noexcept { return m_walkers; }

    /// The version save() writes, the section LOGI of the running game's state (DS-3).
    static constexpr std::uint16_t kStateVersion = 2;
    /// The figures, their reservations and the statistics, for a savegame of Open1602.
    void save(core::ByteWriter& out) const;
    /// Restores what save() of `version` wrote (1: before shipyards had pack donkeys); an error if
    /// the data is damaged, and then nothing changed.
    core::Status load(core::ByteReader& in, std::uint16_t version = kStateVersion);
    [[nodiscard]] const LogisticsStats& stats() const noexcept { return m_stats; }

    /// Tiles per tick of a figure on a tile of Wegspeed class `walk_speed` (units of 1/65536).
    [[nodiscard]] static std::uint32_t step_speed(const assets::FigureType& figure, std::uint8_t walk_speed) noexcept;

private:
    void send_carrier(std::uint32_t home, std::uint8_t ware, std::uint16_t figure);
    [[nodiscard]] std::optional<BuildingAt> home_building(const Walker& walker) const;
    std::vector<YardDelivery> m_yard_deliveries;
    void send_cart(std::uint32_t home);
    /// Farm worker, cattle or fishing boat for `ware` (0x0044B7E0, 0x0044BB40, 0x0044B9A0).
    void send_worker(std::uint32_t home, std::uint8_t ware, WalkerKind kind);
    /// A mine takes ore straight from its deposit (0x0047DFD7; mines have no figures).
    void mine_ore(std::uint32_t home);
    /// Prepares the search area of a walker's trip: the catchment of its home with what its kind
    /// walks on, its home opened; with `targets` the ripe tiles it may harvest are goals.
    void prepare_area(const Walker& walker, const BuildingAt& home, const assets::FigureType& figure, bool targets);
    /// Finds and reserves the next ripe tile for a worker and plans the way there.
    bool find_crop(Walker& walker, const BuildingAt& home, const assets::FigureType& figure);
    /// A worker is done at its field: harvest, then the next tile or home.
    void finish_work(Handle handle, Walker& walker);

    /// A deer at forest place `spot` (0x0044BD00), unless one lives there; with nothing to eat it
    /// falls at once.
    bool spawn_deer(std::uint16_t island, TilePos spot);
    [[nodiscard]] bool deer_at(std::uint16_t island, TilePos spot) const;
    /// The slot of `spot` waits a minute (0x0046B2A0).
    void touch_spot(std::uint16_t island, TilePos spot);
    /// The slot of `spot` is given up, the last slot moves into the hole (0x0046B310).
    void remove_spot(std::uint16_t island, TilePos spot);
    /// A deer that found no way falls for 5 s and gives its slot up.
    void fall(Walker& deer);
    /// Next grazing place of a deer: a ripe tile of its ware in the 13 x 13 square around its
    /// spot, preferably about 4 steps away, chosen and reached like the original (0x0045C270).
    bool plan_deer(Walker& deer);
    void deer_done(Handle handle, Walker& deer);
    void remove_deer(Handle handle, Walker& deer);
    void call_hunter(Handle deer);
    void hunter_arrive(Handle handle, Walker& hunter);
    /// A hunter close to its grazing deer shoots (0x0045C450).
    void check_shot(Walker& hunter);
    void census(std::uint16_t island);
    /// A Klinik or fire brigade sends its figure if the city has a disaster of its kind
    /// (0x0044AF90, 0x0044B470).
    void send_responder(std::uint32_t home, WalkerKind kind);
    /// The nearest disaster of the walker's kind in its city and circle; plans the way there.
    bool find_disaster(Walker& walker, const BuildingAt& home, const assets::FigureType& figure);
    /// A doctor or fireman has worked at a building: it is cured or put out; then the next one.
    void responder_done(Handle handle, Walker& walker);
    /// Robbers (0x0045A780): waiting, chasing, fleeing.
    void robber_wait(Handle handle, Walker& robber);
    void robber_chase(Handle handle, Walker& robber);
    void robber_flee(Handle handle, Walker& robber);
    [[nodiscard]] bool find_victim(Walker& robber);
    /// The square of 19 tiles around a robber's house, what carriers walk on.
    void prepare_robber_area(const Walker& robber, const assets::FigureType& figure);
    /// Marks the tiles of the city's buildings with HAUS_PRODTYP kind 1..last_kind as goals
    /// (inside the current search area).
    void mark_goals(const IslandMap& map, std::uint8_t city, std::int32_t last_kind, std::uint8_t speed_type);
    /// Starts the walker if `path` is not empty; an invalid handle if there is nothing to walk.
    Handle launch(Walker walker);
    void arrive(Handle handle, Walker& walker);
    void pick_up(Walker& walker);
    void deliver(Walker& walker);
    bool plan_home(Walker& walker);
    /// S-002 brought back (OriginalBugs::goldsheep): a figure whose building is gone belongs to the
    /// building that stands on its door or middle tile now, if any.
    void rehome(Walker& walker) const;
    /// Removes the walker and gives back what it reserved.
    void finish(Handle handle, Walker& walker);
    void begin_step(Walker& walker);

    [[nodiscard]] std::uint8_t& jobs(std::uint32_t production);
    [[nodiscard]] std::optional<BuildingAt> building_of(std::uint32_t production) const;
    /// Reserves up to `max` of `ware` at the building `at` (a producer or a market/warehouse);
    /// nothing if less than `min` is free (0x0047D810).
    std::uint16_t reserve(const BuildingAt& at, std::uint16_t island, std::uint8_t ware, std::uint16_t min,
                          std::uint16_t max, bool& from_warehouse, std::optional<std::uint32_t>& production);

    World* m_world;
    const assets::BuildingTable* m_buildings;
    const assets::FigureTable* m_figures;
    const Rules* m_rules;
    Pool<Walker> m_walkers;
    std::vector<std::uint8_t> m_jobs;  ///< walkers out per production building
    PathFinder m_finder;
    LogisticsStats m_stats;
    std::vector<DisasterEvent> m_disaster_events;
    /// Ripe tiles a worker is on its way to (the original's grid bit 29), key: island, y, x.
    std::unordered_set<std::uint64_t> m_reserved_tiles;
    /// Deer that started grazing and call a hunter (handled after the figures moved).
    std::vector<Handle> m_hunt_calls;
    /// Scratch grids of the deer planner (cell state and goal flag of the 13 x 13 square).
    std::vector<std::uint8_t> m_deer_state;
    std::vector<std::uint8_t> m_deer_goal;
};

}  // namespace o1602::sim
