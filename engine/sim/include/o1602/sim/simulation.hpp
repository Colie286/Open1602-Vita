// SPDX-License-Identifier: GPL-3.0-or-later
//
// The running game: advances the world in fixed ticks of game time (ADR 0004). The original
// advances its subsystems with the frame time times the game speed in steps of at most 200 ms
// (0x00489670): production (0x0047DAF0) once per game second, then settlers, figures, ships ...
// Open1602 uses fixed ticks so that every computer of a multiplayer game computes the same.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/ai.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/disasters.hpp"
#include "o1602/sim/event_buffer.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/island_events.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/military.hpp"
#include "o1602/sim/production_cycle.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/scripting.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/volcanoes.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

namespace ai {
class Engine;
}

/// The two switches of the production windows (docs/re/production.md, docs/re/ui.md): working on
/// or off (Prod_SetEnabled 0x0047CFA0, also building op 0x26), and whether market carts may collect
/// the goods (Prod_SetNoPickup 0x0047CD90). The original's windows change both only on the
/// clicking computer; here they are commands like everything else.
struct ProductionSwitch {
    enum class Op : std::uint8_t {
        Enable,  ///< `on`: the building works
        Pickup,  ///< `on`: market carts may collect here
    };
    PlayerId player = kNoPlayer;
    std::uint16_t island = 0;
    std::uint16_t x = 0;  ///< origin of the building
    std::uint16_t y = 0;
    bool on = true;
    Op op = Op::Enable;
};

/// Settings of the city window (docs/re/consumption.md): the tax of a class of inhabitants, the
/// name, and keeping the building material from the people (no house upgrades).
struct CityOrder {
    enum class Op : std::uint8_t {
        TaxRate,       ///< `resident_class` pays `rate` (64..192, 128 = normal; op 0x31 sub 0x33)
        Rename,        ///< `name`, 1 to kMaxNameLength bytes (Windows-1252)
        MaterialLock,  ///< `on`: the houses do not take the material to rise
    };
    static constexpr std::uint8_t kMinTax = 64;
    static constexpr std::uint8_t kMaxTax = 192;
    static constexpr std::size_t kMaxNameLength = 40;
    PlayerId player = kNoPlayer;
    std::uint16_t island = 0;
    std::uint8_t city = 0;
    Op op = Op::TaxRate;
    std::uint8_t resident_class = 0;
    std::uint8_t rate = 128;
    bool on = false;
    std::string name;
};

/// Game speed and pause (ADR 0004): commands like every other action, so that all computers of a
/// game and a replay run at the same pace and pause at the same tick.
struct GameSpeedOrder {
    enum class Op : std::uint8_t {
        SetSpeed,  ///< `speed`, 1 to Rules::max_game_speed
        Pause,     ///< the game stands still from this tick on; commands are still carried out
        Resume,
    };
    PlayerId player = kNoPlayer;
    Op op = Op::SetSpeed;
    std::uint32_t speed = 1;
};

/// The name of a player (the original asks for it when a game starts, 0x0048A680).
struct PlayerRename {
    static constexpr std::size_t kMaxNameLength = 40;
    PlayerId player = kNoPlayer;
    std::string name;  ///< 1 to kMaxNameLength bytes (Windows-1252)
};

/// Who steers a player: a person or the computer. The multiplayer host gives it for a slot when
/// someone comes into the running game and takes it over from the computer.
struct PlayerControl {
    PlayerId player = kNoPlayer;
    bool computer = false;
};

/// Closes or opens all the gates of a player's cities (Player::gates_closed). Closed, they let only
/// the player's own soldiers through; Open1602's own, the original's gates have no state.
struct GateOrder {
    PlayerId player = kNoPlayer;
    bool closed = false;
};

/// A player's action (ADR 0004): applied at the start of a tick, the same on every computer.
using Command =
    std::variant<BuildOrder, DemolishOrder, ShipOrder, CargoTransfer, RouteEdit, TradeSetting, ShipyardOrder,
                 ExploreOrder, DiplomacyOrder, SoldierOrder, SoldierCommand, CastleOrder, UnloadOrder, ProductionSwitch,
                 CityOrder, ShipCommand, GameSpeedOrder, PlayerRename, PlayerControl, GateOrder, CartRequest>;

/// What became of a command.
struct CommandResult {
    std::uint64_t sequence = 0;      ///< as returned by Simulation::submit() or schedule()
    bool late = false;               ///< refused: its tick had already run when it arrived
    std::optional<BuildPlan> build;  ///< build orders: what was built and why the rest was not
    std::size_t demolished = 0;      ///< demolish orders: buildings removed
    BuildBill refund;                ///< demolish orders: what came back (Rules::demolish_refund)
    bool accepted = false;           ///< all other orders: carried out
    std::uint32_t moved = 0;         ///< cargo transfers: goods moved, 1/32 t
};

class Simulation {
public:
    /// Game time per tick.
    static constexpr std::uint32_t kTickMs = 50;
    /// Real time beyond this is dropped instead of caught up (the original: frames longer than
    /// 3 s count as 50 ms, so a stalled computer does not fast-forward the game).
    static constexpr std::uint32_t kMaxCatchUpMs = 3000;
    /// At most this many ticks per advance(); a computer too slow for the speed lets the game run
    /// slower instead of spending ever longer frames on catching up (review point 20).
    static constexpr std::uint32_t kMaxTicksPerAdvance = 32;
    /// Called before each tick advance() runs (to remember positions for drawing between ticks);
    /// false ends this advance() early, e.g. when the frame's time is up.
    using BeforeTick = std::function<bool()>;

    Simulation(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
               Rules rules = Rules::open1602());

    /// Whether the state of the running game a world carries (a game saved by Open1602) can be
    /// read whole (the error names the part that is not); Ok for a world without one. Builds the parts of a game on the
    /// world to try, without running a tick, so that a loader can refuse a damaged file before the game starts.
    /// Building the parts may normalise the world as a load does (ships of older files get their
    /// health), so it must leave a world of a running game as it is.
    [[nodiscard]] static core::Status state_readable(World& world, const assets::BuildingTable& buildings,
                                                     const assets::FigureTable& figures);

    /// Advances by `real_ms` of real time at the current speed; runs whole ticks and keeps the rest,
    /// at most kMaxTicksPerAdvance and only while `before_tick` agrees. Time owed beyond one more
    /// such step is dropped. Commands are applied even while the game is paused (the player may
    /// build then).
    void advance(std::uint32_t real_ms, const BeforeTick& before_tick = {});
    /// How far the game time is between the last tick and the next, 0 to below 1: for drawing
    /// moving figures between their positions of two ticks (ADR 0004).
    [[nodiscard]] double tick_fraction() const noexcept {
        return std::min(0.999, static_cast<double>(m_pending_ms) / kTickMs);
    }
    /// The last advance() could not run all the ticks its time asked for: the game runs slower
    /// than its speed on this computer.
    [[nodiscard]] bool behind() const noexcept { return m_behind; }

    /// Queues a command for the start of the next tick; returns its sequence number. This is the
    /// way in for the players (interface, later the network): a command must name the player who
    /// gives it, one of the world's players, or it is refused without effect. kNoPlayer, which the
    /// handlers take as "any owner", is left to execute() for the game's own actors.
    std::uint64_t submit(Command command) { return schedule(next_tick(), std::move(command)); }
    /// Queues a command for the start of tick `tick` (ADR 0004): in a multiplayer game every computer
    /// schedules the commands of all players for the same tick, a few ticks ahead so that they
    /// arrive in time. Commands of one tick are carried out by player, and those of one player in
    /// the order they were scheduled, so the order does not depend on the network. A command for a
    /// tick that has already run is refused (CommandResult::late): the game would have diverged.
    std::uint64_t schedule(std::uint64_t tick, Command command);
    /// The number of the tick that runs next; ticks are counted from the start of the game.
    [[nodiscard]] std::uint64_t next_tick() const noexcept { return m_time_ms / kTickMs; }
    /// Applies the queued commands that are due (the start of a tick does this).
    void apply_commands();
    /// Carries out one command at once, as if it had been queued (the computer players act this way
    /// within a tick). The result is returned, not kept for take_command_results(). Unlike submit()
    /// it takes kNoPlayer as acting for every owner (tools, tests).
    CommandResult execute(const Command& command);
    /// Results of the applied commands since the last call.
    [[nodiscard]] std::vector<CommandResult> take_command_results() { return m_command_results.take(); }
    /// What building and demolishing changed since the last call.
    [[nodiscard]] std::vector<ConstructionEvent> take_construction_events() { return m_construction_events.take(); }
    /// Names for new cities (Text.cod [STAEDTE]).
    void set_city_names(std::vector<std::string> names) { m_city_names = std::move(names); }
    /// The names for new cities and ships of a game that has none yet. A game keeps the names it
    /// began with: they are saved with it, so that every computer of a multiplayer game names new
    /// cities and ships alike, whatever the language of its installation.
    void offer_names(std::vector<std::string> cities, std::vector<std::string> ships);
    [[nodiscard]] const std::vector<std::string>& city_names() const noexcept { return m_city_names; }
    /// One tick of game time: the commands that are due, then the steps of tick_step_names().
    void tick();
    /// tick() that also measures its steps (benchmarks): adds the real time step i of
    /// tick_step_names() took, in nanoseconds, to `step_ns[i]`; steps beyond the span are not
    /// timed. The game goes on exactly as with tick().
    void tick_timed(std::span<std::uint64_t> step_ns);
    /// The steps of a tick in the order they run (documentation and tests).
    [[nodiscard]] static std::vector<std::string_view> tick_step_names();
    /// Called at the end of every tick: for what has to follow the game tick by tick without
    /// being part of it, such as the townsfolk of render::Ambient. It must not change the world.
    void set_tick_observer(std::function<void(const World&)> observer) { m_tick_observer = std::move(observer); }

    /// The scripts of the game's packages (scripting.hpp): they run at the end of every tick and are
    /// saved with the game. Null for none; the host must outlive the simulation or be unset first.
    void set_scripts(ScriptHost* scripts) noexcept { m_scripts = scripts; }
    [[nodiscard]] ScriptHost* scripts() const noexcept { return m_scripts; }

    /// Game speed factor (1 = normal, up to Rules::max_game_speed); 0 pauses. For tools and tests;
    /// a running game changes the speed with a GameSpeedOrder.
    void set_speed(std::uint32_t speed) noexcept;
    [[nodiscard]] std::uint32_t speed() const noexcept { return m_speed; }
    /// Paused by a GameSpeedOrder: no ticks run, commands are still carried out.
    [[nodiscard]] bool paused() const noexcept { return m_paused; }

    [[nodiscard]] std::uint64_t game_time_ms() const noexcept { return m_time_ms; }
    [[nodiscard]] const World& world() const noexcept { return *m_world; }
    [[nodiscard]] const Rules& rules() const noexcept { return m_rules; }
    /// Carriers and carts on their way.
    [[nodiscard]] const Logistics& logistics() const noexcept { return m_logistics; }
    /// Ships, the free trader and shipyards.
    [[nodiscard]] const Fleet& fleet() const noexcept { return m_fleet; }
    /// Soldiers, towers and the hostile lists.
    [[nodiscard]] const Military& military() const noexcept { return m_military; }
    [[nodiscard]] const Combat& combat() const noexcept { return m_combat; }
    /// The computer players.
    [[nodiscard]] const ComputerPlayers& computers() const noexcept { return m_ai; }
    /// Shots, hits, deaths and destroyed buildings since the last call.
    [[nodiscard]] std::vector<MilitaryEvent> take_military_events() { return m_military_events.take(); }
    /// Treaties offered, signed or broken since the last call.
    [[nodiscard]] std::vector<DiplomacyNotice> take_diplomacy_notices() { return m_world->diplomacy().take_notices(); }
    /// What happened to ships since the last call.
    [[nodiscard]] std::vector<ShipEvent> take_ship_events();
    /// Names for new ships (Text.cod [SHIPS]).
    void set_ship_names(std::vector<std::string> names) { m_fleet.set_ship_names(std::move(names)); }

    /// Production events since the last call (floating texts, animation phases, later logistics).
    [[nodiscard]] std::vector<ProductionEvent> take_production_events() { return m_production_events.take(); }
    /// Shortages and hunger since the last call (messages for the player).
    [[nodiscard]] std::vector<CityEvent> take_city_events() { return m_city_events.take(); }
    /// Houses that rose, fell back or were abandoned since the last call.
    [[nodiscard]] std::vector<HouseEvent> take_house_events() { return m_house_events.take(); }
    /// Droughts and withered fields since the last call.
    [[nodiscard]] std::vector<IslandEvent> take_island_events() { return m_island_events.take(); }
    /// Fires and plagues that broke out, spread, ended or burned a building down since the last call.
    [[nodiscard]] std::vector<DisasterEvent> take_disaster_events() { return m_disaster_events.take(); }
    /// Clocks and figures for Open1602's own savegame chunk (savegame_extension.hpp). A Simulation
    /// built on a world that carries such state continues from it.
    void save_state(core::ByteWriter& out) const;
    /// The world carried a state of the running game that could not be read (damaged, or of a
    /// newer Open1602): the game starts from the world alone, as after loading an original
    /// savegame, and so does not continue exactly as it was saved. Worth telling the player.
    [[nodiscard]] bool state_damaged() const noexcept { return m_state_damaged; }
    /// A fingerprint of the whole game (XXH64 over the sections of the savegame, unpacked, without
    /// the original's file the game came from, and with the map's tiles as the hashes of their
    /// blocks): equal on every computer of a multiplayer game after the same tick, or the games have
    /// diverged. Exchanged every few seconds, not every tick, as it costs writing the game in
    /// memory but for the tiles: of these only the blocks that changed since the last fingerprint
    /// are hashed again (TB-26).
    [[nodiscard]] std::uint64_t state_hash() const;
    /// The same fingerprint taken anew from every tile, without the blocks' hashes kept: what
    /// state_hash() must give (a test of the blocks' revisions).
    [[nodiscard]] std::uint64_t state_hash_anew() const;

    /// Eruptions and what the lava hit since the last call.
    [[nodiscard]] std::vector<VolcanoEvent> take_volcano_events() { return m_volcano_events.take(); }
    /// Missions accomplished, players defeated and the end of the game since the last call.
    [[nodiscard]] std::vector<GameEvent> take_game_events() { return m_game_events.take(); }

private:
    friend class ai::Engine;
    /// Only the parts, joined, and the clocks of the world: what state_readable needs.
    struct Parts {};
    Simulation(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures, Rules rules,
               Parts);
    void pay_upkeep();
    void players_tick();
    void accomplish(PlayerId winner);
    /// `winner` won with `message`; with `co_winners` also everybody else who meets his mission
    /// or the victory conditions now.
    void accomplish(PlayerId winner, std::uint16_t message, bool co_winners);
    void defeat(PlayerId player, GameEvent::Reason reason, bool blame);
    /// Inhabitants of the third class and up in the player's largest city (triumphal arch).
    [[nodiscard]] std::uint64_t upper_inhabitants_of_largest_city(PlayerId player) const;
    bool switch_production(const ProductionSwitch& s);
    bool city_order(const CityOrder& order);
    /// „Kontor gründen“ (0x0043CE00 button 0x9481): the island by the ship is asked (island order 0x4C).
    bool ask_founding(const DiplomacyOrder& order);
    bool game_speed(const GameSpeedOrder& order);
    bool rename_player(const PlayerRename& order);
    bool control_player(const PlayerControl& order);
    bool gate_order(const GateOrder& order);
    core::Status restore_state(const std::vector<std::uint8_t>& state);
    core::Status restore_legacy_state(core::ByteReader& reader, std::uint16_t version);
    void handle(const std::vector<ConstructionEvent>& events, std::size_t first);

    /// A step of tick(); see tick_steps() for the order and why.
    struct TickStep {
        std::string_view name;
        void (Simulation::*run)();
    };
    [[nodiscard]] static std::span<const TickStep> tick_steps() noexcept;
    /// Adds a tick to `clock`; true once per `period`, which `clock` then starts anew.
    static bool elapsed(std::uint32_t& clock, std::uint32_t period) noexcept;
    void diplomacy_step();
    void growth_step();
    void production_step();
    void cities_step();
    void city_events_step();
    void houses_step();
    void upkeep_step();
    void disasters_step();
    void islands_step();
    void volcanoes_step();
    void computers_step();
    void combat_step();
    void ships_step();
    void soldiers_step();
    void logistics_step();
    void statistics_step();
    /// Sends a carrier with `ware` to the shipyard or castle at (x, y); false if it cannot go.
    bool send_yard_carrier(std::uint16_t island, std::uint16_t x, std::uint16_t y, std::uint8_t ware);
    /// Keeps every event buffer within EventBuffer::kMaxPending.
    void trim_events();

    World* m_world;
    const assets::BuildingTable* m_buildings;
    Rules m_rules;
    std::uint32_t m_speed = 1;
    bool m_paused = false;
    bool m_state_damaged = false;
    /// What state_hash() wrote last time, kept for its memory, and the hashes of the blocks of each
    /// island's tiles (so not from two threads at once).
    mutable std::vector<std::uint8_t> m_hash_buffer;
    mutable std::vector<std::vector<BlockHash>> m_block_hashes;
    bool m_behind = false;
    std::uint64_t m_time_ms = 0;
    std::uint32_t m_pending_ms = 0;     ///< game time not yet run as a tick
    std::uint32_t m_production_ms = 0;  ///< game time since the last production second
    std::uint32_t m_city_ms = 0;        ///< game time since the cities last consumed
    std::vector<bool> m_islands_due;    ///< scratch of the island epochs
    EventBuffer<ProductionEvent> m_production_events;
    EventBuffer<CityEvent> m_city_events;
    EventBuffer<HouseEvent> m_house_events;
    EventBuffer<IslandEvent> m_island_events;
    EventBuffer<DisasterEvent> m_disaster_events;
    EventBuffer<VolcanoEvent> m_volcano_events;
    EventBuffer<GameEvent> m_game_events;
    std::vector<RobberSpawn> m_robbers;
    std::uint32_t m_disaster_ms = 0;  ///< game time since the last disaster epoch
    const assets::FigureTable* m_figures;
    std::uint32_t m_upkeep_ms = 0;  ///< game time since the military upkeep was last paid
    std::uint32_t m_island_ms = 0;  ///< game time since the last island epoch
    Logistics m_logistics;
    Fleet m_fleet;
    EventBuffer<ShipEvent> m_ship_events;
    Combat m_combat;
    Military m_military;
    EventBuffer<MilitaryEvent> m_military_events;
    ComputerPlayers m_ai;
    struct Queued {
        std::uint64_t tick;
        std::uint64_t sequence;
        Command command;
    };
    std::vector<Queued> m_commands;  ///< sorted by tick, then player, then as scheduled
    std::uint64_t m_next_command = 1;
    EventBuffer<CommandResult> m_command_results;
    EventBuffer<ConstructionEvent> m_construction_events;
    /// What a batch of construction events touched (handle): the tiles of an island built on or
    /// cleared, as one box per island; kept for their capacity.
    struct CoastChange {
        std::uint16_t island = 0;
        std::int32_t x0 = 0;
        std::int32_t y0 = 0;
        std::int32_t x1 = 0;
        std::int32_t y1 = 0;
    };
    std::vector<CoastChange> m_coast_islands;
    std::vector<std::uint16_t> m_tower_islands;
    std::vector<std::string> m_city_names;
    std::function<void(const World&)> m_tick_observer;
    ScriptHost* m_scripts = nullptr;
};

}  // namespace o1602::sim
