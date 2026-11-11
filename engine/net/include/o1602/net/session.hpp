// SPDX-License-Identifier: GPL-3.0-or-later
//
// A multiplayer game (ADR 0011): lockstep through the host. Every computer runs the whole game;
// only commands travel. The host collects the commands of all players, puts each into a frame
// and sends the frames, complete, to everyone; a computer runs a frame only once it has it whole.
// So all run the same commands at the same frame, and the deterministic simulation (ADR 0004)
// does the rest.
//
// Every few seconds each computer sends the host a fingerprint of its game (Simulation::
// state_hash()). One that differs gets the host's game anew, as a savegame (M-003): the same
// way a computer comes in at the start, comes into a running game or comes back after it lost
// the connection.
//
// A session drives the simulation in place of Simulation::advance(): it calls tick() for each
// frame. Nothing else may submit commands to the simulation or advance it while a session runs.
//
// Before the game there may be a lobby: the host has no game yet, only the slots of the game to
// come (Lobby). Computers come in, take slots, say they are ready and chat; when the host starts,
// it makes the game from the lobby and every computer gets it as it would get a running one.
//
// When the host leaves a running game, the game moves on (host migration): the host names one of
// the computers as its successor in the Roster and hands it the slots' tokens (Standby). That
// computer hosts the game from where its own is (a HostSession made from its ClientSession); the
// others follow it (a ClientSession made from theirs), come back into their slots with their tokens
// and get its game, as after a lost connection.
#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/net/protocol.hpp"
#include "o1602/net/transport.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::net {

/// What the commands of one frame may cost every computer, in units of about one map tile looked
/// at (MS-3): a few milliseconds. Each player has an equal share of it per frame. A command that
/// costs more than a player has left still goes in, and the player's next commands wait until the
/// frames have paid it off: a large order takes frames, it does not make one frame long.
inline constexpr std::uint64_t kFrameWork = 16'384;
/// The bytes of one frame's commands beyond which no further player command goes into it, so that
/// the frames a computer is sent at once stay well within a message.
inline constexpr std::size_t kMaxFrameBytes = 64U << 10;
/// Room in every frame for the commands the host gives itself (a slot handed to the computer, a
/// player's name), which players' commands cannot take (MS-17).
inline constexpr std::uint32_t kReservedCommands = 64;
static_assert(kReservedCommands < kMaxCommandsPerMessage);

/// What `command` costs every computer that carries it out, in the units of kFrameWork: a rectangle
/// to demolish its tiles on the island, a build order its items, a soldier order its units, any
/// other command 1. The host admits by it, and a computer paces what it sends by it.
[[nodiscard]] std::uint64_t command_work(const sim::World& world, const sim::Command& command);

struct SessionConfig {
    /// Frames between two fingerprints at normal speed (100 = 5 s), times the speed rounded up to a
    /// power of two at higher speeds. Each costs a savegame in memory.
    std::uint32_t hash_interval = 100;
    /// Frames the host closes ahead of its own game, at least and at most: more than the slowest
    /// connection needs one way, so that the frames are there when a computer wants to run them.
    /// Within these bounds the host follows the measured round trips.
    std::uint16_t min_lead = 2;
    std::uint16_t max_lead = 50;
    /// Game time the host may run ahead of the slowest computer, beyond the lead, before it waits
    /// for it (real time at normal speed; at speed 4 four times the frames).
    std::uint32_t max_ahead_ms = 1000;
    /// After waiting this long for a computer the others go on without it; it catches up or
    /// gets the game anew.
    std::uint32_t stall_ms = 10'000;
    /// A computer the others no longer wait for gets the game anew once it is this many frames
    /// behind the host at normal speed (30 s), as many times more at a higher speed, instead of
    /// running them all.
    std::uint32_t resync_behind = 600;
    /// Commands one player may give per frame; more wait for the next frames, and beyond a few
    /// frames' worth they are dropped (a flood would stall everyone).
    std::uint32_t max_commands_per_frame = 256;
    /// How often a computer tells the host how far it is (a fingerprint goes at once).
    std::uint32_t progress_interval_ms = 100;
    /// Computers without a free player slot may watch, this many at most (MS-12).
    bool spectators = true;
    std::uint32_t max_spectators = kDefaultMaxSpectators;
    /// Computers may come into a running game (else only back into their own slot).
    bool join_running = true;
    /// (Host) The computer plays the slot of a person who left the running game, once it has stood
    /// empty for take_over_ms (time to come back after a broken connection), until someone takes
    /// it again. Without: the slot gives no orders until its player is back.
    bool computer_takes_over = true;
    std::uint32_t take_over_ms = 60'000;
    /// (Host, also after a host migration) Only the host's player pauses, resumes and sets the
    /// speed of the game; the others' speed orders are not carried out (MS-20: one player could
    /// keep the game paused). Without it, every player may do all three.
    bool host_controls_speed = false;
    /// (Host, also after a host migration) How long, in real time, a game for a computer that
    /// joins or resyncs may pack on a worker while the game goes on before the host waits for it
    /// (TB-26). 0 waits at once, as a test that steps time faster than it passes needs; the game
    /// gives a few seconds.
    std::uint32_t pack_wait_ms = 0;
    /// (Host) After start(), the game's first frame waits until every player of the lobby has the
    /// game loaded, so that all begin together; meanwhile everyone sees how far each one is
    /// (Member::loaded). A player whose game made no progress for this long is no longer waited
    /// for: it catches up as one that joins a running game. 0: the game runs at once and the
    /// others catch up.
    std::uint32_t start_wait_ms = 0;
    /// Desyncs of one computer within this many fingerprints after it got the game anew: it
    /// cannot follow the host (another build?) and is sent away.
    std::uint32_t max_resyncs = 2;
    /// (Host) Computers must give it to come in; empty: anyone may.
    std::string password;
    /// (Host, also after a host migration) The player ID of this computer's person (Hello::player):
    /// the game keeps who plays the host's slot (sim::Player::owner) as it does for the others.
    std::uint64_t player = 0;
    /// Chat lines one computer may send per second; more are dropped.
    std::uint32_t max_chat_per_second = 5;
    /// (Host, also after a host migration) Spectators may write in the chat, to everyone; without,
    /// they only read it.
    bool spectators_chat = true;
    /// (Host migration) How long a computer whose host left tries to get into the successor's game;
    /// the successor may notice the host's leaving a few seconds later than the others.
    std::uint32_t migration_ms = 30'000;
    /// (After a broken connection, not a host that left on purpose) How long a computer tries to get
    /// back into its host's game, with its token, before it takes the host for gone and moves on to
    /// the successor (MS-2). Needs ClientSession::reconnect_with().
    std::uint32_t reconnect_ms = 10'000;
    /// (Host) The bytes of packages it sends at most to a computer that lacks them and asks for them
    /// (HostSession::share_packages); a computer that lacks more is refused as without sharing.
    std::uint64_t max_share_bytes = 256U << 20;
};

/// A package of the game a host may send to computers that lack it: what the game names, and the
/// folder its files are read from when one asks.
struct SharedPackage {
    assets::ContentPackage package;
    std::filesystem::path folder;
    std::uint64_t bytes = 0;  ///< of its files together (assets::package_size)
};

enum class EndReason : std::uint8_t {
    None,
    ConnectFailed,  ///< the host could not be reached
    HostLeft,       ///< the connection to the host broke or the host ended the game
    Refused,        ///< the host did not take this computer (SessionEvent::refused says why)
    ProtocolError,  ///< a message made no sense
    LoadFailed,     ///< the game from the host could not be loaded
    Desync,         ///< this computer kept diverging from the host and was sent away
};

/// A short English text for logs: why the session ended or a computer was refused (what the
/// player reads comes from the program's texts in the game's language).
[[nodiscard]] const char* describe(EndReason reason) noexcept;
[[nodiscard]] const char* describe(RefuseReason reason) noexcept;

struct SessionEvent {
    enum class Kind : std::uint8_t {
        Welcomed,      ///< (computer) the host took this computer in as `seat`
        Loaded,        ///< (computer) a game from the host was loaded; `frame` is its first frame
        Joined,        ///< `seat` (`name`) came in, or back
        Left,          ///< `seat` (`name`) went
        Lagging,       ///< the others no longer wait for `seat`
        Desync,        ///< `seat` diverged from the host after `frame`; it gets the game anew
        Ended,         ///< the session is over (`reason`)
        LobbyChanged,  ///< the lobby (slots, settings) changed, or it started
        Chat,          ///< `seat` (`name`) said `text`, to everyone or privately (`everyone`, `to`, `missed`)
        Dropped,       ///< `count` commands of this computer's player were not carried out (too many at once)
        Reconnecting,  ///< (computer) the connection to the host broke; it tries to get back in
        SpeedRefused,  ///< (computer) only the host pauses, resumes and sets the speed of this game
                       ///< (Lobby::host_controls_speed)
        PackageKept,   ///< (computer) a package the host sent was kept (packages[0])
        Started,       ///< the first frame of the game runs: every player has it loaded, or the host
                       ///< stopped waiting (Session::starting())
    };
    Kind kind = Kind::Joined;
    Seat seat = sim::kNoPlayer;
    std::string name;
    std::uint64_t frame = 0;
    EndReason reason = EndReason::None;
    RefuseReason refused = RefuseReason::Version;
    /// (Ended, refused for Packages) The packages of the game this computer lacks; (PackageKept)
    /// the package kept.
    std::vector<assets::ContentPackage> packages;
    /// (Ended, refused for Packages) Their bytes, when the host sends them (Refused::share_bytes).
    std::uint64_t share_bytes = 0;
    std::string text;
    std::uint32_t count = 0;
    /// (Chat) To everyone, or privately to the players in `to`; on the computer that said it, the
    /// players it did not reach are in `missed` (Chat).
    bool everyone = true;
    std::vector<Seat> to;
    std::vector<Seat> missed;
};

/// What the host and the other computers have in common: pacing the frames, fingerprints, events.
class Session {
public:
    virtual ~Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /// A command of the local player; it is carried out when its frame comes, on every computer.
    /// One that names another player, or any from a spectator, is dropped.
    virtual void submit(sim::Command command) = 0;
    /// Network and game for `real_ms` of real time: receives, runs the frames due at the game's
    /// speed (as Simulation::advance(), calling `before_tick` before each) as far as they are
    /// complete, and sends.
    virtual void update(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick = {}) = 0;
    /// A line of chat (cut to kMaxChatLength; empty lines are not sent): to everyone, or privately
    /// to the players in `to` (only players say something privately, to other players). A
    /// spectator's line goes out only if the game lets spectators write (Lobby::spectators_chat).
    virtual void say(std::string text, std::vector<Seat> to = {}) = 0;
    /// This computer's player may pause, resume and set the speed of the game (the host always may).
    [[nodiscard]] virtual bool may_change_speed() const noexcept { return true; }

    /// The player this computer plays, kNoPlayer while it only watches or is not in yet.
    [[nodiscard]] Seat local_seat() const noexcept { return m_seat; }
    /// Frames run so far.
    [[nodiscard]] std::uint64_t frames() const noexcept { return m_frames; }
    /// As Simulation::tick_fraction(), for drawing between frames.
    [[nodiscard]] double tick_fraction() const noexcept;
    /// The last update could not run all the frames its time asked for: the frames were not
    /// complete yet, or the host waits for a slower computer.
    [[nodiscard]] bool waiting() const noexcept { return m_waiting; }
    /// Who is in the game, the host first.
    [[nodiscard]] const std::vector<Member>& roster() const noexcept { return m_roster; }
    /// The game as set up before the start (Lobby::started tells whether it runs).
    [[nodiscard]] const Lobby& lobby() const noexcept { return m_lobby; }
    /// Who carries the game on if the host leaves (kNoPlayer: nobody), and where the others then
    /// connect (Roster::successor).
    [[nodiscard]] Seat successor() const noexcept { return m_successor; }
    /// The game has started, but its first frame waits until every player has it loaded
    /// (SessionConfig::start_wait_ms): the players see how far each one is (Member::loaded).
    [[nodiscard]] bool starting() const noexcept { return m_starting; }
    [[nodiscard]] const std::string& successor_address() const noexcept { return m_successor_address; }
    [[nodiscard]] bool ended() const noexcept { return m_ended; }
    [[nodiscard]] EndReason end_reason() const noexcept { return m_end_reason; }
    /// What happened since the last call.
    [[nodiscard]] std::vector<SessionEvent> take_events();

protected:
    Session(std::unique_ptr<Transport> transport, SessionConfig config);

    /// Frames the time asks for now (at most Simulation::kMaxTicksPerAdvance); ran() books those run.
    std::uint32_t frames_due(std::uint32_t real_ms, const sim::Simulation& simulation);
    void ran(std::uint32_t frames);
    /// One frame: its commands, in order, then a tick. Returns the fingerprint if one is due.
    std::optional<FrameHash> run_frame(sim::Simulation& simulation, std::span<const sim::Command> commands);
    void event(SessionEvent e) { m_events.push_back(std::move(e)); }
    void end(EndReason reason);
    void send(LinkId link, const Message& message);
    /// An encoded message: whole, or in Parts if it is larger than a transport carries (MS-7).
    void send_bytes(LinkId link, std::span<const std::uint8_t> bytes);
    void chat_event(const Chat& chat);
    void lobby_event();
    /// Commands each of `players` players may put into one frame: the configured limit or an equal
    /// share of a message (less the host's reserve), whichever is less.
    [[nodiscard]] std::uint32_t command_share(std::uint32_t players) const noexcept;
    /// Work each of `players` players may put into one frame: an equal share of kFrameWork.
    [[nodiscard]] static std::uint64_t work_share(std::uint32_t players) noexcept;

    std::unique_ptr<Transport> m_transport;
    SessionConfig m_config;
    std::vector<TransportEvent> m_incoming;  ///< reused by every poll
    Seat m_seat = sim::kNoPlayer;
    std::uint64_t m_frames = 0;
    std::uint32_t m_pending_ms = 0;
    bool m_waiting = false;
    bool m_ended = false;
    EndReason m_end_reason = EndReason::None;
    std::vector<Member> m_roster;
    Seat m_successor = sim::kNoPlayer;
    std::string m_successor_address;
    bool m_starting = false;
    Lobby m_lobby;
    std::vector<SessionEvent> m_events;
};

class ClientSession;

/// The host: runs its own game, takes the other computers in and decides the frames. Its player is
/// `seat` (kNoPlayer: a host without a player of its own).
class HostSession final : public Session {
public:
    /// A game that runs at once on the world the host has; the computers get its free human slots.
    HostSession(std::unique_ptr<Transport> transport, sim::Simulation& simulation,
                const assets::BuildingTable& buildings, Seat seat, std::string name, std::uint64_t content,
                SessionConfig config = {});
    /// A lobby first: `lobby` has the slots of the game to come (the host's own `seat` becomes
    /// Human); start() begins the game.
    HostSession(std::unique_ptr<Transport> transport, Lobby lobby, Seat seat, std::string name, std::uint64_t content,
                SessionConfig config = {});
    /// Host migration: carries on the game of `client`, whose host left and named this computer its
    /// successor (client.hosts_next()). The game goes on from where this computer's is, with the
    /// frames the old host closed beyond it, the slots and tokens it handed over, the same password
    /// and options; the old host's slot goes to the computer. The others come back with their tokens.
    HostSession(std::unique_ptr<Transport> transport, ClientSession& client, const assets::BuildingTable& buildings);
    ~HostSession() override;

    void submit(sim::Command command) override;
    void update(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick = {}) override;
    void say(std::string text, std::vector<Seat> to = {}) override;

    // --- The lobby (before start()) ---

    /// The game has started (start(), or the session began with a running game).
    [[nodiscard]] bool started() const noexcept { return m_simulation != nullptr; }
    /// The content packages the game to come uses (ADR 0007 section 7): a computer that lacks one of
    /// them, or has it with other files, is refused (RefuseReason::Packages). Once the game runs,
    /// those its world names count.
    void set_packages(std::vector<assets::ContentPackage> packages) { m_packages = std::move(packages); }
    /// The content packages a computer needs to join.
    [[nodiscard]] std::span<const assets::ContentPackage> packages() const noexcept;
    /// The packages this computer has that it sends to a computer that lacks them, when that asks
    /// for them (Hello::fetch_packages) and they are within SessionConfig::max_share_bytes. Without,
    /// such a computer is refused (RefuseReason::Packages).
    void share_packages(std::vector<SharedPackage> packages) { m_shared = std::move(packages); }
    /// Title, description and the program's setup of the game to come; the slots stay.
    void set_lobby_info(std::string title, std::string game, std::vector<std::uint8_t> setup);
    /// Who plays slot `seat`: Open, Computer or Closed. A computer that had it watches from then
    /// on; the host's own slot and Human are not set this way.
    void set_seat(Seat seat, LobbySeat::Kind kind);
    /// The host's own player: an open slot, or kNoPlayer to only host.
    bool take_seat(Seat seat);
    /// The banner of slot `seat`: the host's own, or one no other person plays (the computer's, an
    /// open or a closed one). `banner` must be one of Lobby::banners; a slot the computer plays or
    /// nobody plays that has it takes the old one in exchange, another person's refuses.
    bool set_banner(Seat seat, sim::BannerId banner);
    /// Sends the computer playing `seat` away.
    void kick(Seat seat);
    /// Sends the computer of roster()[`member`] away, a spectator too (0, the host itself: nothing),
    /// if that entry still has `seat` and `name` as shown: the roster may have changed since (MS-18).
    void kick_member(std::size_t member, Seat seat, std::string_view name);
    /// Every human slot of another computer is ready (the host starts when it wants).
    [[nodiscard]] bool everyone_ready() const;
    /// Begins the game made from the lobby: every computer in the lobby gets it. With
    /// SessionConfig::start_wait_ms, its first frame waits for the players to have it (starting()).
    void start(sim::Simulation& simulation, const assets::BuildingTable& buildings);
    /// Stops waiting for the players still loading the game at the start: it runs now, they catch up.
    void start_now();

    /// Frames complete so far (all frames below this one).
    [[nodiscard]] std::uint64_t closed() const noexcept { return m_closed; }
    /// Frames the host closes ahead of its game now.
    [[nodiscard]] std::uint16_t lead() const noexcept { return m_lead; }
    /// Frames kept: closed but not yet sent, or not yet run by the host.
    [[nodiscard]] std::size_t kept_frames() const noexcept { return m_log.size(); }
    /// Fingerprints of other computers compared with the host's so far (and found equal).
    [[nodiscard]] std::uint64_t fingerprints_compared() const noexcept { return m_compared; }
    [[nodiscard]] std::uint64_t fingerprints_matched() const noexcept { return m_matched; }

private:
    struct Peer;
    struct Frame {
        std::vector<sim::Command> commands;
    };

    void receive(LinkId link, std::span<const std::uint8_t> bytes);
    void hello(Peer& peer, const Hello& m);
    void commands(Peer& peer, CommandBatch& m);
    void progress(Peer& peer, const Progress& m);
    void load_progress(Peer& peer, const LoadProgress& m);
    /// While the game starts (m_starting): whether its first frame still waits for a player; once
    /// it no longer does, the game runs (Started).
    bool wait_for_start(std::uint32_t real_ms);
    void stop_starting();
    void take_seat(Peer& peer, Seat seat);
    void ready(Peer& peer, bool ready);
    void chat(Peer& peer, const Chat& m);
    /// A line of `from` (nullptr: the host's own player), to everyone or only to the players in
    /// `line.to` that a person plays, and back to `from` with those it missed.
    void deliver(Chat line, Peer* from);
    /// A person plays `seat`: the host's player or a computer's that is still there.
    [[nodiscard]] bool played_by_person(Seat seat) const;
    /// Gives slot `seat` banner `banner` (set_banner()), whoever asks.
    bool change_banner(Seat seat, sim::BannerId banner);
    void set_banner(Peer& peer, sim::BannerId banner);
    /// Makes the slots' banners those on offer and every one different: a slot with a banner taken
    /// by an earlier slot, or not on offer, gets the first free one.
    void sort_out_banners();
    void broadcast(const Message& message);
    /// Moves a computer (or the host, `peer` nullptr) to `seat`, or to watching.
    void move(Peer* peer, Seat seat);
    /// A command waiting for room in a frame (MS-9), with its size as bytes.
    struct Waiting {
        Seat from = sim::kNoPlayer;
        std::size_t bytes = 0;
        sim::Command command;
    };
    /// What a player may still put into frames (MS-3, MS-9).
    struct Allowance {
        std::uint32_t commands = 0;  ///< in the open frame
        std::uint64_t owed = 0;      ///< work admitted beyond the shares of the frames so far
        std::uint32_t waiting = 0;   ///< in m_overflow
        std::uint32_t dropped = 0;   ///< not carried out since the player was last told (Dropped)
    };

    /// A command of player `from` (checked to be one of the player's and readable by the others):
    /// into the open frame if the player's share has room, else to wait for the next frames.
    void queue(Seat from, sim::Command command);
    void admit(Waiting w);
    /// A command the host gives itself: into the open frame, or first into the next (never lost).
    void insert(sim::Command command);
    /// The players' shares of a frame, for the players there now.
    void refresh_shares();
    /// Tells the players whose commands were dropped (Dropped, or an event for the host's own).
    void tell_dropped();
    void close_frame();
    void run(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick);
    void compare(Peer& peer, const FrameHash& theirs);
    void desync(Peer& peer, std::uint64_t frame);
    /// Writes the game for the computers it is due to and starts packing it on a worker (TB-26).
    void start_packing();
    /// Sends a game packed by now to those it was packed for; after `real_ms` more of packing in
    /// all than SessionConfig::pack_wait_ms it waits for it.
    void deliver_packed(std::uint32_t real_ms);
    void send_game(Peer& peer, std::uint64_t frame);
    void broadcast_frames();
    void forget_frames();
    void update_lead(std::uint32_t real_ms);
    void watch_stalls(std::uint32_t real_ms, bool stalled);
    /// Ends the computer's connection, telling it why if `tell` (with `packages` for Packages).
    void drop(Peer& peer, std::optional<RefuseReason> tell, std::vector<assets::ContentPackage> packages = {},
              std::uint64_t share_bytes = 0);
    /// The bytes of `missing` if this host sends them all (share_packages), else 0.
    [[nodiscard]] std::uint64_t share_bytes(std::span<const assets::ContentPackage> missing) const;
    /// Sends the computer the files of `missing` (PackageFiles); false if one could not be read.
    bool send_packages(Peer& peer, std::span<const assets::ContentPackage> missing);
    void refresh_roster();
    void init_lobby();
    /// The token of a slot (made when it is first needed).
    [[nodiscard]] std::uint64_t token_of(Seat seat);
    /// The slot's token is no longer good: the slot changed hands.
    void forget_token(Seat seat);
    /// A kicked computer's address: no newcomer from there for the rest of the session (MS-12).
    void ban(const std::string& address);
    /// A kicked person: not back into their slot by their player ID (seat_of_owner).
    void ban_owner(std::uint64_t owner);
    /// Keeps a person's slot from newcomers for `ms`, for its player to come back with the token.
    void reserve(Seat seat, std::uint32_t ms);
    [[nodiscard]] bool reserved(Seat seat) const;
    /// The slot a Hello's token is good for, kNoPlayer if none (MS-1, MS-8).
    [[nodiscard]] Seat seat_of_token(const Hello& m);
    /// Tells the successor what it needs to carry the game on, if that changed (Standby).
    void hand_over();
    [[nodiscard]] Peer* peer_of(LinkId link);
    /// A slot for a computer that comes in: `wanted` if it is free, else the first free one,
    /// leaving those of other people (owner_of) to them while another is free.
    [[nodiscard]] Seat free_seat(Seat wanted, std::uint64_t owner = 0) const;
    /// The person who played a slot (sim::Player::owner; in the lobby, the savegame's).
    [[nodiscard]] std::uint64_t owner_of(Seat seat) const;
    /// The free slot the person `owner` played before, kNoPlayer if none.
    [[nodiscard]] Seat seat_of_owner(std::uint64_t owner) const;
    [[nodiscard]] bool seat_taken(Seat seat) const;
    /// A person's slot was left: the computer takes it over after a while (m_vacant).
    void vacate(Seat seat);
    void take_over_vacant(std::uint32_t real_ms);
    [[nodiscard]] std::uint64_t slowest() const;
    /// Frames the host may be ahead of a computer at the current speed.
    [[nodiscard]] std::uint64_t ahead_frames() const;
    /// Frames behind the host after which a lagging computer gets the game anew (resync_behind,
    /// scaled by the speed).
    [[nodiscard]] std::uint64_t resync_frames() const;

    sim::Simulation* m_simulation;
    const assets::BuildingTable* m_buildings;
    std::string m_name;
    std::uint64_t m_content;
    std::vector<assets::ContentPackage> m_packages;  ///< before the game runs (set_packages)
    std::vector<SharedPackage> m_shared;             ///< share_packages()
    std::vector<std::unique_ptr<Peer>> m_peers;
    std::deque<Frame> m_log;  ///< frames m_log_start to m_closed - 1
    std::uint64_t m_log_start = 0;
    std::uint64_t m_closed = 0;           ///< frames below are complete
    std::uint64_t m_broadcast = 0;        ///< frames below went out to every computer
    std::vector<sim::Command> m_open;     ///< for frame m_closed
    std::uint64_t m_open_work = 0;        ///< command_work() of the players' commands in m_open
    std::size_t m_open_bytes = 0;         ///< their size as bytes
    std::deque<Waiting> m_overflow;       ///< no room in m_open: for the next frames, in order
    std::deque<sim::Command> m_inserted;  ///< the host's own that found m_open full: first in the next
    std::vector<Allowance> m_allowance;   ///< by seat
    /// Commands and work each player may put into one frame: the configured limit or an equal share
    /// of a message, whichever is less, and an equal share of kFrameWork.
    std::uint32_t m_share = 1;
    std::uint64_t m_work_share = kFrameWork;
    std::uint16_t m_lead = 0;
    std::uint32_t m_lead_ms = 0;     ///< time since the lead last shrank
    std::deque<FrameHash> m_hashes;  ///< the host's latest fingerprints
    std::uint32_t m_held_ms = 0;     ///< how long the host has been waiting for a computer
    /// A game being packed for computers that join or resync: its sections were written on the
    /// game thread after `frame` frames, with that speed and pause; zlib, most of the cost of a
    /// large game, runs on a worker while the game goes on (TB-26). The computers it is for wait
    /// with Peer::packing set; the frames from `frame` on are kept for them.
    struct Packing {
        std::future<core::Result<std::vector<std::uint8_t>>> file;
        std::uint64_t frame = 0;
        std::uint32_t speed = 1;
        bool paused = false;
        std::uint32_t waited_ms = 0;
    };
    std::optional<Packing> m_packing;
    /// The game as sent in this update: a Snapshot message, encoded once for every computer that
    /// gets it (MS-23); each gets its own epoch written in.
    std::optional<std::vector<std::uint8_t>> m_game;
    /// Games the host may still write for computers that join or resync, in milliseconds of credit
    /// (kGameCostMs a game, kGameCreditMs at most; it grows with real time).
    std::uint32_t m_game_credit_ms = 4'000;
    std::vector<std::pair<Seat, std::uint64_t>> m_tokens;  ///< seat and token, to come back to it
    std::uint64_t m_salt = 0;  ///< for the verifiers of m_tokens the successor gets (Standby)
    /// After a host migration: the slots an earlier host handed out tokens for, by verifier, until
    /// their players are back (then m_tokens has their tokens).
    std::vector<SeatToken> m_verifiers;
    std::vector<Seat> m_computer_held;  ///< open at the start: the computer plays it while nobody does
    /// A person's slot left in the running game, and how long it has stood empty: the computer
    /// takes it over after SessionConfig::take_over_ms.
    std::vector<std::pair<Seat, std::uint32_t>> m_vacant;
    /// A person's slot left in the running game, and how much longer newcomers do not get it (MS-5).
    std::vector<std::pair<Seat, std::uint32_t>> m_reserved;
    std::uint64_t m_compared = 0;
    std::uint64_t m_matched = 0;
    std::vector<LinkId> m_roster_links;    ///< the link of each roster() entry (unused for the host's own)
    std::vector<std::string> m_banned;     ///< addresses of computers kicked: not taken in again (MS-12)
    std::vector<std::uint64_t> m_kicked;   ///< owners of slots kicked: not back into them by their ID
    std::optional<LinkId> m_standby_link;  ///< the successor, as last told
    Standby m_standby;                     ///< what it was told
    bool m_standby_changed = true;
    bool m_roster_changed = true;
    /// Only how far computers are with the game on its way changed: the roster goes out with the
    /// next change, or after m_roster_wait_ms.
    bool m_loading_changed = false;
    std::uint32_t m_roster_wait_ms = 0;
    bool m_lobby_changed = true;
    std::uint32_t m_chat_ms = 0;        ///< into the current second of the chat limit
    std::uint32_t m_lobby_wait_ms = 0;  ///< before the lobby may be sent again (MS-16)
};

/// A computer that joins a host. It has no game until the host sends one: `load` turns the
/// savegame into a running simulation (the caller keeps it) or gives nullptr if it cannot.
class ClientSession final : public Session {
public:
    using Loader = std::function<sim::Simulation*(std::span<const std::uint8_t> save)>;
    /// Loading a game in two steps (load_in_background()): `Prepare` gets the savegame on a worker
    /// thread and does what takes long and touches nothing of the program (reading and unpacking
    /// it); the `Finish` it returns makes the running game of it on the game thread, as a Loader.
    using Finish = std::function<sim::Simulation*()>;
    using Prepare = std::function<Finish(std::vector<std::uint8_t> save)>;
    /// A connection to the successor (nullptr if none can be made now).
    using Connect = std::function<std::unique_ptr<Transport>()>;
    /// Keeps a package the host sent (its files, in package_hash order): false if it cannot, which
    /// ends the session (LoadFailed).
    using KeepPackage =
        std::function<bool(const assets::ContentPackage& package, std::span<const assets::PackageFile> files)>;

    ClientSession(std::unique_ptr<Transport> transport, Hello hello, Loader load, SessionConfig config = {});
    /// Host migration: follows `old`, whose host left, to the successor: the same name and slot (its
    /// token), the game anew from there. Connects with `connect`, again and again until it is taken
    /// in, for SessionConfig::migration_ms; then it ends with ConnectFailed.
    ClientSession(const ClientSession& old, Connect connect);

    void submit(sim::Command command) override;
    void update(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick = {}) override;
    void say(std::string text, std::vector<Seat> to = {}) override;
    [[nodiscard]] bool may_change_speed() const noexcept override { return !m_lobby.host_controls_speed; }
    /// In the lobby: asks for an open slot, or kNoPlayer to watch.
    void take_seat(Seat seat);
    /// In the lobby: ready to start, or not.
    void set_ready(bool ready);
    /// In the lobby: asks for `banner` (one of Lobby::banners) for this computer's slot.
    void set_banner(sim::BannerId banner);
    /// How to connect to the host again: when the connection breaks in the running game (and the
    /// host did not say it leaves), the computer tries to get back into its slot for
    /// SessionConfig::reconnect_ms before it moves on to the successor. Without, it only moves on
    /// when the host said it leaves.
    void reconnect_with(Connect connect) { m_reconnect = std::move(connect); }
    /// Where the packages go that the host sends when the Hello asks for them (Hello::fetch_packages),
    /// before the game that uses them is loaded. Without, a computer that gets one ends the session.
    void keep_packages_with(KeepPackage keep) { m_keep_package = std::move(keep); }
    /// Loads the games from the host in two steps, the long one on a worker thread (in place of the
    /// Loader): the program goes on meanwhile and the connection stays alive, also for a large
    /// world on a slow computer; what comes from the host meanwhile waits until the game is there.
    void load_in_background(Prepare prepare) { m_prepare = std::move(prepare); }

    /// Taken in by the host (in the lobby or the game).
    [[nodiscard]] bool welcomed() const noexcept { return m_welcomed; }
    /// The game runs (a game came from the host and was loaded).
    [[nodiscard]] bool playing() const noexcept { return m_simulation != nullptr; }
    /// Token to come back into the same slot after a lost connection (Hello::token).
    [[nodiscard]] std::uint64_t token() const noexcept { return m_token; }
    /// Frames received but not run yet.
    [[nodiscard]] std::uint64_t frames_in_hand() const noexcept { return m_closed - m_frames; }

    /// How far the game on its way from the host has come, for a progress display while joining.
    enum class JoinStage : std::uint8_t {
        None,       ///< not waiting for a game: in the lobby before the start, or playing
        Waiting,    ///< the game has started; the host is still writing it for this computer
        Receiving,  ///< its pieces are coming in (received_bytes() of total_bytes())
        Loading,    ///< it is here and is loaded with the next update()
    };
    [[nodiscard]] JoinStage join_stage() const noexcept;
    /// Bytes of the large message coming in pieces so far, and its size (0 if none comes now).
    [[nodiscard]] std::uint64_t received_bytes() const noexcept {
        return m_whole.empty() ? m_part.size() : m_whole.size();
    }
    [[nodiscard]] std::uint64_t total_bytes() const noexcept {
        return m_whole.empty() ? (m_part.empty() ? 0 : m_part_total) : m_whole.size();
    }
    /// The host left the running game and it can move on: this computer hosts it next
    /// (hosts_next()), or follows the successor at successor_address().
    [[nodiscard]] bool can_migrate() const noexcept;
    /// The host left and named this computer its successor, with what it needs (Standby). Only
    /// when the host said it leaves, or it could not be reached again (MS-2): a computer whose own
    /// connection broke would host a second game beside the host's.
    [[nodiscard]] bool hosts_next() const noexcept;
    /// The name of the successor as the roster has it.
    [[nodiscard]] std::string successor_name() const;

private:
    friend class HostSession;  // made from a ClientSession when the host leaves

    void receive(std::span<const std::uint8_t> bytes);
    /// One event of the transport, as update() takes them in turn.
    void handle(TransportEvent& e);
    void load(Snapshot& m);
    /// The game of `head` (Snapshot without its bytes) is loaded as `simulation` (nullptr: it could not be).
    void loaded(sim::Simulation* simulation, const Snapshot& head);
    /// While a game is prepared on a worker (m_pending): keeps the connection alive; once it is
    /// ready, finishes it. False while it is still being prepared.
    bool finish_pending();
    void turn(Turn& m);
    void package_files(PackageFiles& m);
    void run(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick);
    void report(std::uint32_t real_ms);
    /// Tells the host how much of the game on its way this computer has (LoadProgress).
    void report_loading(std::uint32_t real_ms);
    /// Sends what the host would admit now of m_outgoing.
    void send_commands();
    /// The connection to the host broke: back into the slot with the token (m_reconnect).
    void reconnect();
    /// Players in the game as the roster has them (the host's own too).
    [[nodiscard]] std::uint32_t players() const noexcept;

    static constexpr LinkId kHost = 0;
    Hello m_hello;
    Loader m_load;
    Prepare m_prepare;  ///< load_in_background()
    /// A game being prepared on a worker (load_in_background()), and its snapshot without the bytes.
    struct Pending {
        std::future<Finish> finish;
        Snapshot head;
    };
    std::optional<Pending> m_pending;
    sim::Simulation* m_simulation = nullptr;
    bool m_connected = false;
    bool m_welcomed = false;
    std::uint64_t m_token = 0;
    std::uint32_t m_epoch = 0;   ///< of the game last loaded (Snapshot::epoch)
    std::uint64_t m_closed = 0;  ///< frames below have come from the host
    std::uint16_t m_lead = 0;
    std::deque<std::vector<sim::Command>> m_frames_ahead;  ///< frames m_frames to m_closed - 1
    std::deque<sim::Command> m_outgoing;
    std::vector<std::uint8_t> m_part;  ///< the pieces so far of a message that comes in Parts
    std::uint32_t m_part_total = 0;    ///< its size
    /// A message whose last piece came, read at the next update(): the frame between can show that
    /// it is being loaded. The transport events after it wait in m_incoming from m_next on.
    std::vector<std::uint8_t> m_whole;
    std::size_t m_next = 0;
    KeepPackage m_keep_package;
    /// The files so far of a package that comes in several PackageFiles messages.
    std::optional<PackageFiles> m_package;
    /// Pacing what goes to the host as it would admit it (MS-17): commands and work in the frames
    /// run since the last sending, so that the host does not have to drop any.
    std::uint32_t m_send_commands = 0;
    std::uint64_t m_send_owed = 0;
    std::uint64_t m_reported = 0;                  ///< frames last told the host
    std::uint32_t m_since_report = 0;              ///< ms
    std::uint8_t m_load_reported = kNoLoadReport;  ///< LoadProgress::percent last told the host
    std::uint32_t m_since_load_report = 0;         ///< ms
    static constexpr std::uint8_t kNoLoadReport = 0xff;
    std::optional<Standby> m_standby;  ///< from the host, if it named this computer its successor
    Connect m_connect;                 ///< following a successor: connects again after a failure
    Connect m_reconnect;               ///< to the host, after a broken connection (reconnect_with())
    bool m_host_leaving = false;       ///< the host said it leaves (HostLeaving)
    bool m_reconnecting = false;       ///< trying to get back to the host with m_connect
    bool m_host_gone = false;          ///< it could not be reached again: as good as left
    std::uint32_t m_migration_ms = 0;  ///< since it began to follow
    std::uint32_t m_retry_ms = 0;      ///< since the last connection failed
};

}  // namespace o1602::net
