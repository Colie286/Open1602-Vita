// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ships at sea: sailing to a target along a planned course, trading at Kontors,
// trade routes, the free trader and shipyards.
//
// Like the original, a ship decides what to do only at the end of each run of one or
// two tiles, after a turn step or a wait: whether it has arrived, what it trades there, where it
// sails next, whether another ship lies ahead. The original makes these decisions only on the
// owner's computer and sends the course to the others; Open1602 makes them on every computer from
// the same state and the simulation's own random numbers (ADR 0004).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// Ships the pirates may have: 0 to 4, by the highest level a human
/// or computer player reached.
[[nodiscard]] std::uint32_t pirate_max_ships(const World& world) noexcept;

/// Whether pirates come in this game (settings bit 0: no pirates; Rules::pirates can override it).
[[nodiscard]] bool pirates_enabled(const World& world, const Rules& rules) noexcept;

/// Nests the pirates aim for (Rules::pirate_nests): one, or as the pirates' faction grows them
/// with the human and computer players (Faction::nests; by default one for every four begun).
/// Each nest brings the original's ships, so the fleet grows with them.
[[nodiscard]] std::uint32_t pirate_nests(const World& world, const Rules& rules) noexcept;

/// The part of the map a world tile belongs to when the pirates aim for `nests` nests: as many
/// strips across the longer side of the map, numbered from its top or left edge.
[[nodiscard]] std::uint32_t pirate_region(const World& world, std::uint32_t x, std::uint32_t y,
                                          std::uint32_t nests) noexcept;

/// The pirate nest's offer: its N most valuable wares (N = Maxware of its Kontor) for
/// sale at 40 % of the base price, everything else off. The pirates' task 5 runs it every 42 s,
/// and the pirate step again whenever the nest eats 10 t of its food.
void pirate_nest_offer(World& world, const assets::BuildingTable& buildings, City& nest);

/// The human and computer players when Rules::trader_reach scales the free trader to them (more
/// than the trader's Faction::reach_players), else 0.
[[nodiscard]] std::size_t trader_reach_players(const World& world, const Rules& rules) noexcept;

/// How many of the nearest Kontors the free trader weighs when `kontors` are open to him
/// (Rules::trader_reach): the original's 12, or with more human and computer players than his
/// faction's reach_players the share a game of that many would have, at least 3.
[[nodiscard]] std::size_t trader_candidates(const World& world, const Rules& rules, std::size_t kontors) noexcept;

/// Room of one hold of a ship.
inline constexpr std::uint32_t kHoldCapacity = 50 * 32;
/// Room of one of the trade cart's three holds (History Edition: "10t Holz" in each).
inline constexpr std::uint32_t kCartHoldCapacity = 10 * 32;
/// Room of one hold of this ship or trade cart.
[[nodiscard]] inline std::uint32_t hold_capacity(const Ship& ship) noexcept {
    return ship.cart() ? kCartHoldCapacity : kHoldCapacity;
}

/// How near a trade cart must be to a market or Kontor to trade there: 2 tiles from
/// any tile of it.
inline constexpr std::int32_t kCartReach = 2;
/// Stops of a trade cart's route (SHIP4 +0x052 holds four for a cart).
inline constexpr std::uint8_t kCartStops = 4;

/// The cities whose market or Kontor a trade cart on the world tile (wx, wy) reaches, as indices
/// into World::cities(), in the order of that list.
[[nodiscard]] std::vector<std::size_t> cities_near_cart(const World& world, const assets::BuildingTable& buildings,
                                                        std::int32_t wx, std::int32_t wy);

/// The first trade cart of `player` on `island` (one per player and island as in the original; more
/// with Rules::more_carts), nullptr if it has none.
[[nodiscard]] const Ship* trade_cart_of(const World& world, PlayerId player, std::uint16_t island);
/// All trade carts of `player` on `island`, in the order of the world's ships.
[[nodiscard]] std::vector<const Ship*> trade_carts_of(const World& world, PlayerId player, std::uint16_t island);
/// Open1602 (Rules::more_carts): how many trade carts `player` may have on `island`, one for each of
/// its Kontors and markets there.
[[nodiscard]] std::uint32_t cart_places(const World& world, const assets::BuildingTable& buildings, PlayerId player,
                                        std::uint16_t island);

/// Whether a world tile lies within reach of a building: the integer circle of radius
/// 5 around its centre. Ships trade with a Kontor, are repaired by a shipyard from there.
[[nodiscard]] bool building_in_reach(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                                     std::uint32_t bx, std::uint32_t by, std::int32_t wx, std::int32_t wy);

/// The first city whose Kontor a ship at (wx, wy) can trade with, if any.
[[nodiscard]] std::optional<std::size_t> kontor_in_reach(const World& world, const assets::BuildingTable& buildings,
                                                         std::int32_t wx, std::int32_t wy);

/// Where a ship could found a Kontor: the island and the island tile the search reached.
struct KontorSite {
    std::uint16_t island = 0;
    std::uint32_t x = 0;  ///< island tile
    std::uint32_t y = 0;
};

/// The land a ship at (wx, wy) lies at for founding a Kontor (radius 4): only from
/// within an island's rectangle, a search through the sea, surf and beach of the disc of radius 4
/// round the ship to the first land tile outside the circles of every market and Kontor of that
/// island. The ship's window offers „Kontor errichten“ when the owner has no city
/// on that island yet.
[[nodiscard]] std::optional<KontorSite> kontor_site_near(const World& world, const assets::BuildingTable& buildings,
                                                         std::int32_t wx, std::int32_t wy);

/// Whether ships can load, unload or trade at the city's Kontor: a destroyed Kontor
/// ("Kontor zerstört") only while its owner is a human or computer player still in the game, not
/// one of the natives, pirates or a defeated player.
[[nodiscard]] bool kontor_open(const World& world, const assets::BuildingTable& buildings, const City& city);

/// Moving a ship.
struct ShipOrder {
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;  ///< Ship::id
    Target target;           ///< none = stop
    /// Given in the military mode (or as an attack): an object of another player (not the free
    /// trader) means war with him.
    bool attack = false;
    /// Open1602 (ADR 0010): on the way the ship fights every enemy it meets, then sails on.
    bool attack_move = false;
    /// Waits behind the orders the ship has (Shift + right click); a ship without an order (or on
    /// its trade route) takes it at once.
    bool queue = false;
};

/// Goods between a ship and the Kontor it lies at: the player's own (no money) or another's at the
/// Kontor's prices (with a trade treaty).
struct CargoTransfer {
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;
    std::uint8_t ware = 0;
    bool to_ship = true;       ///< load / buy; otherwise unload / sell
    std::uint32_t amount = 0;  ///< 1/32 t; the original offers 1, 5, 10 and 50 t
    std::uint8_t hold = 0xFF;  ///< unloading from one hold, 0xFF = any
    /// The Kontor (island, city) whose window sent it. Only with S-012 brought back
    /// (OriginalBugs::remote_trade) does it count, when the ship lies at no Kontor any more.
    std::optional<std::uint16_t> kontor_island = std::nullopt;
    std::uint8_t kontor_city = 0;
};

/// Editing the trade route of a ship.
struct RouteEdit {
    enum class Op : std::uint8_t {
        SetStop,    ///< stop `stop` sails to `target` (its entries are cleared)
        ClearStop,  ///< `stop` = 0xFF clears all
        SetEntry,   ///< entry `entry` of stop `stop`
        Start,      ///< switches the route on
        Stop,       ///< and off
        OneWay,     ///< `on`: the route ends after its last stop (computer players' supply runs)
        /// Open1602: the stops of the ship in `target` (one of the same owner) replace this ship's.
        CopyFrom,
    };
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;
    Op op = Op::SetStop;
    std::uint8_t stop = 0;
    std::uint8_t entry = 0;
    Target target{};
    RouteEntry value{};
    bool on = true;
};

/// Kontor trade settings of one ware (city ops 0x14/0x1C-0x22).
struct TradeSetting {
    PlayerId player = kNoPlayer;
    std::uint16_t island = 0;
    std::uint8_t city = 0;
    std::uint8_t ware = 0;
    enum class Mode : std::uint8_t { None, Sell, Buy };
    Mode mode = Mode::None;
    std::optional<std::uint16_t> price;   ///< none = the default for the mode
    std::optional<std::uint32_t> amount;  ///< keep (selling) or limit (buying); none = default
};

/// "Insel erkunden": a ship standing in the rectangle of an island explores its crops and
/// deposits (button 0x9487).
struct ExploreOrder {
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;
};

/// Further orders of the ship panel: cannons, cargo overboard, the
/// name, offering the ship for sale and buying a ship another player offers.
struct ShipCommand {
    enum class Op : std::uint8_t {
        /// 1 t of cannons from a hold becomes a mounted cannon (op 0x19 +1), `count` times while
        /// a hold has 1 t and the ship room for another (Open1602: more than one at once).
        MountCannon,
        /// "Kanonenbestückung reduzieren": a mounted cannon back into a hold as 1 t, `count` times
        /// while there is room (Open1602: more than one at once).
        UnmountCannon,
        Dump,          ///< "Waren über Bord werfen": the goods of `hold` (0xFF = all holds) are lost
        Rename,        ///< `name`, 1 to kMaxNameLength bytes (Windows-1252)
        OfferForSale,  ///< at Preis * `factor` / 128 (factor 0..384; op 0x20/0x21)
        WithdrawSale,
        Buy,  ///< `player` buys the ship at its price from its owner (0x84D)
        /// The white flag up (`on`: the ship stops and decides nothing more, never fires) or down
        /// (op 0x23).
        WhiteFlag,
        Scuttle,  ///< "Schiff versenken": it sinks without an attacker (op 0x0C, attacker 7)
        /// Back and forth between where it lies and the world tile `x`, `y`, fighting what it
        /// meets (two position stops and the route on).
        Patrol,
        /// Open1602: stop and stay; fire at what comes in range, chase nothing (until the next order).
        Hold,
        /// A computer that finds no island for its city gives its ship to the pirates (the root
        /// task: the ship stops, then ship op 5 with value 5). Only a computer's ship.
        ToPirates,
    };
    static constexpr std::size_t kMaxNameLength = 40;
    static constexpr std::uint16_t kMaxSaleFactor = 384;
    /// MountCannon: every free place the cannons on board fill; UnmountCannon: every mounted cannon
    /// the holds have room for.
    static constexpr std::uint8_t kAllCannons = 0xFF;
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;
    Op op = Op::MountCannon;
    std::uint8_t hold = 0xFF;  ///< MountCannon and Dump: the hold, 0xFF = the first fitting / all
    std::uint16_t factor = 128;
    std::string name;
    bool on = true;       ///< WhiteFlag
    std::uint16_t x = 0;  ///< Patrol: world tile
    std::uint16_t y = 0;
    std::uint8_t count = 1;  ///< Mount-/UnmountCannon: cannons at most, kAllCannons = as many as fit
};

/// "Fahrenden Händler anfordern" (city panel button 43026): a city's owner without a trade cart on
/// its island gets one, which comes out of the city's Kontor, or of the market whose window asked
/// for it, and drives to the world tile `x`, `y` at once, as in the original. It costs nothing; one
/// per player and island.
struct CartRequest {
    PlayerId player = kNoPlayer;
    std::uint16_t island = 0;
    std::uint8_t city = 0;
    std::uint16_t x = 0;  ///< world tile
    std::uint16_t y = 0;
    /// World tile of the city's market it comes out of; 0, 0 or anything else: the Kontor.
    std::uint16_t from_x = 0;
    std::uint16_t from_y = 0;
    /// Open1602 (Rules::more_carts): another cart where the player has one on the island already, up
    /// to one for each of its Kontors and markets there.
    bool another = false;
};

/// Whether and why a ship can explore an island now (the button of the ship panel).
enum class ExploreState : std::uint8_t {
    Possible,    ///< standing in an island's rectangle with something left to find
    Running,     ///< exploring now
    Done,        ///< the island is fully explored by the owner
    Moving,      ///< the ship is sailing or turning
    NoIsland,    ///< not inside the rectangle of an island
    NotAllowed,  ///< not the player's ship, or sinking
};

/// Orders to a shipyard (building ops 0x1A, 0x1B, 0x26) and to a ship to go there for repair.
struct ShipyardOrder {
    enum class Op : std::uint8_t {
        Build,  ///< `ship_type` 0..3: HANDEL1, HANDEL2, KRIEG1, KRIEG2
        Cancel,
        Enable,  ///< switch on or off with `on`
        Repair,  ///< ship `ship` (lying in reach) is repaired, or released with on = false
    };
    PlayerId player = kNoPlayer;
    std::uint16_t island = 0;
    std::uint16_t x = 0;  ///< origin of the shipyard
    std::uint16_t y = 0;
    Op op = Op::Build;
    std::uint8_t ship_type = 0;
    bool on = true;
    std::uint32_t ship = 0;
};

/// What happened to ships, for messages and the display.
struct ShipEvent {
    enum class Kind : std::uint8_t {
        Arrived,           ///< reached its target
        Traded,            ///< goods changed hands at a Kontor
        Refused,           ///< a transfer found nothing to move
        IslandDiscovered,  ///< `player` sees `island` for the first time
        NativesFound,      ///< MELDUNG 0x2F: a native village on it
        PiratesFound,      ///< 0x30: a pirate hideout on it
        RouteWarning,      ///< 0x5D: three rounds of a route with refused transfers
        GaveUp,            ///< found no way to its target
        TraderArrived,     ///< a free trader appeared on the map
        ShipLaunched,      ///< 6 / 11: a shipyard finished a ship
        ShipRepaired,      ///< 7
        FleetFull,         ///< 0x47: the player has too many ships for another order
        Attacked,          ///< 8, or 5 when `amount` is 1 (pirates); `city` holds the attacker
        Sunk,              ///< 9: `player` lost the ship; `city` holds the attacker
        WhiteFlag,         ///< a computer's ship surrendered to the pirates
        Fired,             ///< a broadside; `ware` holds the bearing
        PirateAppeared,    ///< a new pirate ship at the edge of the map
        Plundered,         ///< pirates took a surrendered ship's goods (`amount` 1) or found nothing
        CropsExplored,     ///< 0x2A: the crops of `island` are explored (centre of the island in x, y)
        DepositFound,      ///< 0x2B iron ore / 0x2C gold (`ware`) at the mountain x, y of `island`
        ExploreFinished,   ///< the exploring of `island` has ended (for the progress display)
        OfferedForSale,    ///< 10: `player` offers the ship for `money`
        Sold,              ///< the ship went from `player` to the player in `city` for `money`
    };
    Kind kind = Kind::Arrived;
    std::uint32_t ship = 0;
    PlayerId player = kNoPlayer;
    std::uint16_t island = 0;
    std::uint8_t city = 0;
    std::uint8_t ware = 0;
    std::int64_t amount = 0;
    std::int64_t money = 0;
    std::uint16_t x = 0;  ///< island tile for messages with a place
    std::uint16_t y = 0;
};

class Combat;

/// The holds a ship uses: as many as its type has (Maxware), the first of `ship.cargo`. Ships from
/// the original's files keep its eight slots; the ones beyond the type's stay empty. Everything
/// that puts goods or soldiers into a hold looks only at these.
[[nodiscard]] std::size_t usable_holds(const Ship& ship, const assets::FigureTable& figures);
/// The hold a goods exchange window selects when it opens, as in the original: the one with the most
/// goods. An empty ship gets the first hold; of equally full holds the last one is taken.
[[nodiscard]] std::size_t fullest_hold(const Ship& ship, const assets::FigureTable& figures);
/// The wares a stop of the ship's route moves: one per hold, at least the original's three, at
/// most RouteStop::kEntries. The ones beyond stay stored (a route copied from a larger ship).
[[nodiscard]] std::size_t usable_route_entries(const Ship& ship, const assets::FigureTable& figures);

class Fleet {
public:
    /// Cheats (CheatOrder): goods into the holds of the ship with `id` as far as they hold them
    /// (what went in, 1/32 t), a cannon more up to its type's (whether one came).
    std::uint32_t cheat_cargo(std::uint32_t id, std::uint8_t ware, std::uint32_t amount);
    bool cheat_cannon(std::uint32_t id);
    /// Course searches of all ships in one tick (TB-1). A search costs up to a few ms; ships over
    /// the budget wait for the next tick, in the order of the ship list, so every peer does the same.
    static constexpr std::uint32_t kPlansPerTick = 8;

    Fleet(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures, const Rules& rules);

    /// One tick of game time: every ship moves; at the end of a run it decides what to do.
    void tick(std::uint32_t dt_ms);
    /// Something was built or demolished on an island: its water may have changed.
    void island_changed(std::uint16_t island);
    /// The same for the island tiles x0..x1, y0..y1 only (inclusive).
    void island_changed(std::uint16_t island, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1);

    bool order(const ShipOrder& order);
    /// Goods moved, or nothing (then a Refused event).
    std::uint32_t transfer(const CargoTransfer& transfer);
    bool edit_route(const RouteEdit& edit);
    bool set_trade(const TradeSetting& setting);
    bool shipyard(const ShipyardOrder& order);
    /// Starts exploring the island the ship stands in (ExploreState::Possible only).
    bool explore(const ExploreOrder& order);
    /// A new trade cart (carts.cpp); its id, or 0 when the request is refused.
    std::uint32_t request_cart(const CartRequest& request);

    /// The trade cart of `player` on `island`, if it has one.
    [[nodiscard]] const Ship* cart_of(PlayerId player, std::uint16_t island) const;
    /// Cannons, cargo overboard, name, sale (ship_orders.cpp).
    bool command(const ShipCommand& command);
    /// What buying the ship costs (Preis * sale factor / 128).
    [[nodiscard]] std::int32_t sale_price(const Ship& ship) const;
    [[nodiscard]] ExploreState explore_state(const Ship& ship, PlayerId player) const;
    /// The island whose rectangle holds the ship's tile, if any.
    [[nodiscard]] std::optional<std::uint16_t> island_at_ship(const Ship& ship) const;
    /// A shot hit ship `ship`: it loses health and sails slower, sinks at 0. A
    /// computer's ship hit by pirates surrenders.
    void hit(std::uint32_t ship, std::uint32_t damage, PlayerId attacker, bool by_pirates);
    /// The ship goes down: it turns to an odd heading, sinks for Stirbtime and is gone with its cargo.
    void sink(Ship& ship, PlayerId attacker);
    /// Who sees whom and target choice; without it ships do not fight.
    void set_combat(Combat* combat) noexcept { m_combat = combat; }
    /// Ships the AI steers itself (ComputerPlayers::directs): idle pirates of these choose nothing.
    void set_directed(std::function<bool(std::uint32_t)> directed) { m_directed = std::move(directed); }

    /// Ships and shipyards a player has or has ordered (the original's limit of 33 counts both).
    [[nodiscard]] std::uint32_t fleet_size(PlayerId player) const;
    [[nodiscard]] const SeaMap& sea() const noexcept { return m_sea; }
    /// Speed of a ship in tiles per second times 1000 (for the display).
    [[nodiscard]] std::uint32_t speed_milli(const Ship& ship) const;

    [[nodiscard]] std::vector<ShipEvent> take_events() {
        std::vector<ShipEvent> e;
        e.swap(m_events);
        return e;
    }

    /// Names for new ships (Text.cod [SHIPS]), handed out in turn.
    void set_ship_names(std::vector<std::string> names) { m_ship_names = std::move(names); }
    /// Gives the players' ships without a name the next names of the list, in the order of the
    /// ships (the start ships of the players five on, which are copies of a level's ship).
    void name_unnamed_ships();
    [[nodiscard]] const std::vector<std::string>& ship_names() const noexcept { return m_ship_names; }
    /// A pack donkey brought material to a shipyard (the logistics call this).
    void yard_delivery(std::uint16_t island, std::uint16_t x, std::uint16_t y, std::uint8_t ware, std::uint32_t amount);
    /// Asks the logistics for a pack donkey: shipyard (island, x, y) needs `ware`.
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

    /// The highest version load() reads, the section FLET of the running game's state (DS-3).
    static constexpr std::uint16_t kMaxStateVersion = 5;
    /// The version save() writes: 5 with the free traders' choices of Rules::trader_reach, else 4,
    /// so that a game of up to four players keeps the bytes of the versions before them.
    [[nodiscard]] std::uint16_t state_version() const noexcept;
    /// State that is not in the world (clocks, the trader's tools), for Open1602's savegame.
    void save(core::ByteWriter& out) const;
    /// Restores what save() of `version` wrote; an error if the data is damaged (nothing changed).
    core::Status load(core::ByteReader& in, std::uint16_t version);

private:
    struct Decision;
    void advance(Ship& ship, std::uint32_t budget_us);
    void decide(Ship& ship);
    /// Arrival at the target (unloading, trading, the stop of a route); how long the ship stays.
    std::optional<std::uint32_t> arrive_at_target(Ship& ship);
    /// The next target of a ship without an order: its route, the trader's or the pirates' choice.
    void choose_next_target(Ship& ship);
    void build_squares();
    /// Calls visit(ship) for the ships within `range` tiles (Chebyshev) of (x, y) and maybe a few
    /// more, in the order of World::ships(); stops when visit returns true.
    template <class Visit>
    void near_ships(std::int32_t x, std::int32_t y, std::int32_t range, Visit&& visit) const;
    [[nodiscard]] bool ship_ahead(const Ship& ship) const;
    void start_wait(Ship& ship, std::uint32_t ms);
    bool start_next_run(Ship& ship);
    /// What planning a course found: a way (or the ship is there), none, or none only because of
    /// the ships nearby.
    enum class Planned : std::uint8_t { Way, NoWay, ShipsInWay };
    Planned plan(Ship& ship, std::int32_t radius);
    bool local_detour(Ship& ship);
    [[nodiscard]] bool at_target(const Ship& ship, const Target& target) const;
    [[nodiscard]] bool target_valid(const Target& target) const;
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> target_point(const Target& target) const;
    void update_island(Ship& ship);
    void explore_tick();
    void explore_step(Ship& ship);
    void stop_exploring(Ship& ship);
    /// The ship goes to `owner` as it lies (bought, or given to the pirates).
    void change_owner(Ship& ship, PlayerId owner);
    void finish_order(Ship& ship);
    /// A ship gave up its way to the Kontor of `target` (with 1).
    void gave_up_city(const Target& target);
    /// A computer's complaint: the first human player at peace with
    /// `owner` that has ships within `reach` tiles of the world tile (x, y) gets event 10.
    void complain_of_ships(PlayerId owner, std::int32_t x, std::int32_t y, std::int32_t reach);
    void route_next_stop(Ship& ship);
    bool route_city_stop(Ship& ship, const RouteStop& stop);
    bool route_ship_stop(Ship& ship, const RouteStop& stop);
    std::uint32_t move_goods(Ship& ship, City& city, std::uint8_t ware, bool to_ship, std::uint32_t amount,
                             std::uint8_t hold, bool limits, std::uint16_t price);
    [[nodiscard]] bool may_trade(PlayerId ship_owner, PlayerId city_owner) const;
    [[nodiscard]] const assets::FigureType* type_of(const Ship& ship) const;
    [[nodiscard]] std::size_t holds(const Ship& ship) const;
    [[nodiscard]] std::uint32_t space_for(const Ship& ship, std::uint8_t ware, std::uint8_t hold = 0xFF) const;
    [[nodiscard]] std::uint32_t run_us(const Ship& ship, std::uint8_t direction, std::uint8_t tiles) const;
    void add_cargo(Ship& ship, std::uint8_t ware, std::uint32_t amount, std::uint16_t price);
    std::uint32_t remove_cargo(Ship& ship, std::uint8_t ware, std::uint32_t amount, std::uint8_t hold);

    // Trade carts (carts.cpp)
    void decide_cart(Ship& ship);
    bool plan_cart(Ship& ship);
    [[nodiscard]] bool cart_at(const Ship& ship, const Target& target) const;
    [[nodiscard]] std::uint32_t cart_run_us(const Ship& ship, std::uint8_t direction) const;
    [[nodiscard]] bool cart_target_allowed(const Ship& ship, const Target& target) const;

    // Naval combat (naval.cpp)
    bool naval_combat(Ship& ship);
    [[nodiscard]] std::optional<std::uint8_t> bearing_to(const Ship& ship, const Target& target) const;

    // Pirates (pirates.cpp)
    [[nodiscard]] PlayerId pirates() const;
    [[nodiscard]] std::vector<std::size_t> pirate_bases() const;
    void pirates_tick(std::uint32_t dt_ms);
    void pirates_step();
    void pirate_choose(Ship& ship);
    /// The base a pirate ship sails to: one of `bases` (not empty).
    std::size_t pirate_base_for(const Ship& ship, const std::vector<std::size_t>& bases);
    bool plunder(Ship& pirate, Ship& victim);

    // Free trader (trader.cpp)
    void trader_tick(std::uint32_t dt_ms);
    void trader_choose(Ship& ship);
    void trader_leave(Ship& ship);
    bool trader_trade(Ship& ship, City& city);
    void trader_restock(Ship& ship, std::uint32_t dt_ms);
    [[nodiscard]] std::uint16_t trader_max_price(std::uint8_t ware) const;
    [[nodiscard]] std::uint32_t trader_buy_amount(const Ship& ship, const City& city, std::uint8_t ware) const;
    [[nodiscard]] std::uint32_t trader_sell_amount(const Ship& ship, const City& city, std::uint8_t ware) const;
    void spawn_trader();

    // Shipyards (shipyards.cpp)
    void yards_tick(std::uint32_t dt_ms);
    void yard_cycle(CostBuilding& yard);
    bool launch(CostBuilding& yard);
    CostBuilding* yard_at(std::uint16_t island, std::uint16_t x, std::uint16_t y);
    /// The next name of Text.cod's [SHIPS], round robin; empty without names.
    std::string next_ship_name();

    World* m_world;
    Combat* m_combat = nullptr;
    std::function<bool(std::uint32_t)> m_directed;
    const assets::BuildingTable* m_buildings;
    const assets::FigureTable* m_figures;
    const Rules* m_rules;
    SeaMap m_sea;
    SeaPlanner m_planner;
    PathFinder m_cart_paths;
    // Ships by squares of kSquareTiles tiles, built at the start of a tick (TB-4): index into
    // World::ships(). Ships move less than kSquareSlack tiles in a tick; ships added since are
    // looked at one by one.
    static constexpr std::int32_t kSquareTiles = 8;
    static constexpr std::int32_t kSquareSlack = 4;
    std::vector<std::vector<std::uint32_t>> m_squares;
    std::uint32_t m_square_cols = 0;
    std::uint32_t m_square_rows = 0;
    std::size_t m_squared_ships = 0;
    bool m_squares_valid = false;
    mutable std::vector<std::uint32_t> m_near;  ///< scratch of near_ships()
    std::uint32_t m_plans_left = 0;             ///< course searches still allowed in this tick (TB-1)
    std::uint32_t m_tick_ms = 0;                ///< game time of the running tick
    std::vector<ShipEvent> m_events;
    std::vector<std::uint32_t> m_ids;  ///< scratch of tick(): the ships in their order
    std::vector<FetchRequest> m_fetch;
    std::vector<std::string> m_ship_names;
    std::uint32_t m_name_index = 0;
    std::uint32_t m_trader_ms = 0;        ///< game time since the last check for a new free trader
    std::uint32_t m_yard_ms = 0;          ///< game time since the last shipyard second
    std::uint32_t m_pirate_ms = 0;        ///< game time since the pirates' last step
    std::uint32_t m_pirate_spawn_ms = 0;  ///< time until the next pirate ship may appear
    bool m_pirate_armed = false;          ///< the last new pirate ship has its cannons
    /// Tools the free trader got from nowhere (TIMERS +0x98) and the quota after which he gets
    /// only one ton at a time (+0x9C, 400 t).
    std::uint32_t m_tools_given = 0;
    std::uint32_t m_tools_quota = 400 * 32;
    /// Market statistics of the last full pass over the cities, per ware: demand and
    /// offers of all Kontors with the price sums.
    struct Market {
        std::uint64_t demand = 0;
        std::uint64_t demand_value = 0;
        std::uint64_t offer = 0;
        std::uint64_t offer_value = 0;
    };
    std::vector<Market> m_market;
    /// With the reach of Rules::trader_reach: which Kontor (island << 8 | city) a free trader last
    /// chose at which of the choices counted in m_trader_choices, so that he goes where no one has
    /// been for longest when nothing promises profit.
    std::uint32_t m_trader_choices = 0;
    std::map<std::uint32_t, std::uint32_t> m_trader_chosen;
};

}  // namespace o1602::sim
