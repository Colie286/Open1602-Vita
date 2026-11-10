// SPDX-License-Identifier: GPL-3.0-or-later
//
// The messages of a multiplayer game (ADR 0011). Every computer talks only to the host; the host
// decides which commands belong to which frame and sends every frame, complete, to everyone. A
// frame is one call of Simulation::tick(), counted from the start of the session; the game time
// stands still in the frames of a pause, the frames go on.
//
// Little-endian with fixed widths, like the commands (sim/command_io.hpp). A message starts with
// its type. New types and new fields at the end of a message come with a new kProtocolVersion;
// the host refuses a computer of another version.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "o1602/net/transport.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::net {

/// Raised whenever the messages or the simulation change in a way older builds would not follow.
/// The first three bytes of a Hello (its type and this version) never change, so that a computer of
/// another version is told so (RefuseReason::Version) rather than refused as nonsense (MS-22).
inline constexpr std::uint16_t kProtocolVersion = 19;

/// Upper limits a decoded message must keep; more comes from damaged or hostile data.
inline constexpr std::size_t kMaxNameLength = sim::PlayerRename::kMaxNameLength;
inline constexpr std::uint32_t kMaxCommandsPerMessage = 1024;
inline constexpr std::uint32_t kMaxFramesPerTurn = 1U << 16;
inline constexpr std::uint32_t kMaxRosterMembers = 256;
/// Computers a host takes besides itself: one fewer than a Roster holds, so that every roster it
/// sends decodes (LC-1).
inline constexpr std::uint32_t kMaxMembers = kMaxRosterMembers - 1;
inline constexpr std::uint32_t kMaxSnapshotBytes = 32U << 20;
static_assert(kMaxSnapshotBytes + (64U << 10) <= kMaxMessageBytes, "a game must fit into one message");
/// The largest message a computer sends the host (its commands and reports; the host refuses more).
inline constexpr std::size_t kMaxMessageToHost = 1U << 20;
static_assert(kMaxMessageToHost < kMaxPacketBytes, "a computer's messages travel whole");
/// The bytes of a message in one Part.
inline constexpr std::size_t kPartBytes = 256U << 10;
static_assert(kPartBytes + 64 < kMaxPacketBytes);
/// Spectators a host takes at most (MS-12), beyond the players.
inline constexpr std::uint32_t kDefaultMaxSpectators = 8;
inline constexpr std::size_t kMaxPasswordLength = 64;
inline constexpr std::size_t kMaxTitleLength = 64;
inline constexpr std::size_t kMaxChatLength = 200;
/// Slots of a lobby: every PlayerId but kNoPlayer, so a lobby holds the players with a start and the
/// factions' slots between them.
inline constexpr std::uint32_t kMaxLobbySeats = 255;
static_assert(kMaxLobbySeats == sim::kMaxPlayerCount && kMaxLobbySeats >= sim::kMaxPlayerSlots,
              "a lobby has a slot for every player and the factions");
inline constexpr std::uint32_t kMaxSetupBytes = 64U << 10;
inline constexpr std::size_t kMaxAddressLength = 64;
inline constexpr std::uint32_t kMaxLobbyBanners = 1024;
/// Content packages a Hello names or a Refused lists (ADR 0007 section 7).
inline constexpr std::uint32_t kMaxPackages = 1024;
/// The bytes of a package's files in one PackageFiles message: a package travels in as many as it
/// needs, and a single file larger than this not at all.
inline constexpr std::uint32_t kMaxPackageFilesBytes = 16U << 20;
static_assert(kMaxPackageFilesBytes + (64U << 10) <= kMaxMessageBytes);
/// Files of a package in one PackageFiles message.
inline constexpr std::uint32_t kMaxPackageFiles = 4096;

/// Which player of the world a computer plays: kNoPlayer watches only.
using Seat = sim::PlayerId;

// --- From a computer to the host ---

/// The first message after connecting.
struct Hello {
    std::uint16_t protocol = kProtocolVersion;
    /// Fingerprint of the game data the simulation depends on (content_fingerprint()): computers
    /// with other data would compute another game.
    std::uint64_t content = 0;
    std::string name;            ///< the player's name, 1 to kMaxNameLength bytes (Windows-1252)
    Seat seat = sim::kNoPlayer;  ///< the player slot wanted, kNoPlayer for any
    std::uint64_t token = 0;     ///< from an earlier Welcome: takes the same slot again
    std::string password;        ///< the game's password, if it has one
    /// Comes after its host left, to the computer that carries the game on (host migration): back
    /// into its slot with `token`, else it watches.
    bool returning = false;
    /// The content packages installed on the computer, each with its version and hash: it joins
    /// only with every package the game uses; those it has beyond them it does not load for the game.
    std::vector<assets::ContentPackage> packages;
    /// The person agreed to download the packages it lacks (Refused::share_bytes): the host sends
    /// them (PackageFiles) before it takes the computer in.
    bool fetch_packages = false;
};

/// Commands of the computer's player, in the order given; the host puts them into the next frame
/// it closes.
struct CommandBatch {
    std::vector<sim::Command> commands;
};

/// The fingerprint of the game after a frame (Simulation::state_hash()).
struct FrameHash {
    std::uint64_t frame = 0;  ///< frames run when it was taken
    std::uint64_t hash = 0;
    friend bool operator==(const FrameHash&, const FrameHash&) = default;
};

/// How far a computer is: the host waits for the slowest, compares fingerprints and knows which
/// frames it may forget.
struct Progress {
    std::uint32_t epoch = 0;   ///< of the game it runs (Snapshot::epoch): older reports are stale
    std::uint64_t frames = 0;  ///< frames run so far
    std::optional<FrameHash> hash;
};

// --- From the host to a computer ---

/// The computer is in: the slot it plays and the token to come back to it.
struct Welcome {
    std::uint16_t protocol = kProtocolVersion;
    Seat seat = sim::kNoPlayer;
    std::uint64_t token = 0;
};

/// Why the host does not take a computer (it disconnects right after).
enum class RefuseReason : std::uint8_t {
    Version,   ///< another kProtocolVersion
    Content,   ///< other game data
    Full,      ///< no free slot and no spectators
    Closed,    ///< the host takes nobody in a running game
    Desync,    ///< the computer kept diverging from the host's game
    Invalid,   ///< the computer sent what makes no sense
    Password,  ///< a wrong password
    Kicked,    ///< the host sent it away
    Taken,     ///< the slot of the token is played from another computer that is still there
    Packages,  ///< content packages of the game are missing on the computer or differ (Refused::packages)
};

struct Refused {
    RefuseReason reason = RefuseReason::Version;
    /// (Packages) The packages of the game the computer lacks, as the host has them.
    std::vector<assets::ContentPackage> packages;
    /// (Packages) The bytes of these packages, when the host sends them to a computer that asks for
    /// them (Hello::fetch_packages); 0 when it does not.
    std::uint64_t share_bytes = 0;
};

/// The whole game as the host has it before frame `frame`: at the start, for a computer that
/// comes in later and after a desync. The frames from `frame` on follow in Turns.
struct Snapshot {
    std::uint32_t epoch = 0;  ///< counts the games sent to this computer
    std::uint64_t frame = 0;
    // Speed and pause: the savegame holds them too (state version 6); these win, as older saves lack them.
    std::uint32_t speed = 1;
    bool paused = false;
    std::vector<std::uint8_t> save;  ///< Open1602 savegame (sim/save_game.hpp)
};

/// The commands of one frame, in the order they are carried out.
struct FrameCommands {
    std::uint64_t frame = 0;
    std::vector<sim::Command> commands;
};

/// Frames `first` to `closed` - 1 are complete: those not listed have no commands. A computer may
/// run every frame below `closed`.
struct Turn {
    std::uint64_t first = 0;
    std::uint64_t closed = 0;
    /// How many frames the host closes ahead of its own game: a computer with more frames in hand
    /// than this runs faster to catch up.
    std::uint16_t lead = 0;
    std::vector<FrameCommands> frames;  ///< ascending, within [first, closed)
};

/// A computer of the session as everyone sees it.
struct Member {
    enum class State : std::uint8_t {
        Playing,
        Loading,  ///< receiving or loading the game
        Lagging,  ///< so far behind that the others no longer wait for it
    };
    Seat seat = sim::kNoPlayer;
    std::string name;
    State state = State::Playing;
    bool host = false;
    friend bool operator==(const Member&, const Member&) = default;
};

struct Roster {
    std::vector<Member> members;
    /// Who carries the game on if the host leaves (host migration): a member's slot, kNoPlayer for
    /// nobody, and its address as the host sees it, where the others then connect.
    Seat successor = sim::kNoPlayer;
    std::string successor_address;
};

/// The fingerprints of these players differed from the host's after frame `frame`; they get the
/// game anew (Snapshot).
struct Desync {
    std::uint64_t frame = 0;
    std::vector<Seat> seats;
};

// --- Before the game: the lobby ---

/// One player slot of the game to come, as the lobby shows it.
struct LobbySeat {
    enum class Kind : std::uint8_t {
        Open,      ///< free for a computer that comes in
        Human,     ///< `name` plays it
        Computer,  ///< a computer player of the game
        Closed,    ///< not in the game
        Reserved,  ///< not a player's slot (free trader, pirates, natives); the lobby leaves it out
    };
    Kind kind = Kind::Open;
    std::string name;
    bool ready = false;
    /// The banner the slot's player gets (colour, flag, coat of arms). It stays with the slot when
    /// players come and go; no two slots of a lobby with Lobby::banners share one.
    sim::BannerId banner = 0;
    friend bool operator==(const LobbySeat&, const LobbySeat&) = default;
};

/// (Host) The game being set up; sent to everyone whenever it changes. Who watches is in the
/// Roster.
struct Lobby {
    std::string title;             ///< the game's name, as the host gave it
    std::string game;              ///< what is played, for display ("Endlosspiel, Stufe 2")
    std::vector<LobbySeat> seats;  ///< by player slot of the world (Seat)
    bool started = false;          ///< the game runs: a computer that comes in gets it at once
    bool join_running = true;      ///< computers may come into the running game
    bool spectators = true;        ///< computers without a slot may watch
    bool password = false;         ///< joining takes a password
    /// The program's own description of the game (map, level, preview): the session passes it on
    /// without reading it.
    std::vector<std::uint8_t> setup;
    /// The banners the players may choose from (SetBanner), in the order the lobby offers them;
    /// empty: every slot keeps its banner (a savegame, a running game).
    std::vector<sim::BannerId> banners;
    /// The computer plays the slots of persons who left the running game (SessionConfig).
    bool computer_takes_over = true;
    /// Only the host pauses and resumes the game; the others' pauses are refused (MS-20).
    bool host_pauses_only = false;
    /// Spectators may write in the chat, to everyone (they read the lines to everyone in any case).
    bool spectators_chat = true;
    friend bool operator==(const Lobby&, const Lobby&) = default;
};

/// (Computer) Wants slot `seat` of the lobby, an open one; kNoPlayer: to watch.
struct TakeSeat {
    Seat seat = sim::kNoPlayer;
};

/// (Computer) Ready to start, or not any more.
struct SetReady {
    bool ready = false;
};

/// (Computer) Wants `banner` (one of Lobby::banners) for its slot. Refused when another person's
/// slot has it; a slot the computer plays or nobody plays gives it up and takes the old one.
struct SetBanner {
    sim::BannerId banner = 0;
};

/// A line of chat, to everyone or privately to some players. From a computer only `text`,
/// `everyone` and `to` count: the host fills in who said it. A line to everyone goes to every
/// computer, the spectators' too. A private line goes only to the computers of the players in `to`
/// and back to the one who said it: the others never get it.
struct Chat {
    Seat seat = sim::kNoPlayer;
    std::string name;
    std::string text;  ///< 1 to kMaxChatLength bytes (Windows-1252)
    bool everyone = true;
    /// (Private) The players it is for, ascending; from the host: those it reached.
    std::vector<Seat> to;
    /// (Private, from the host to the one who said it only) The players in `to` it did not reach:
    /// no person plays them (any more).
    std::vector<Seat> missed;
};

// --- Host migration ---

/// A slot and what its token is checked against: token_verifier(token, salt), not the token, so
/// that the successor cannot use it to take the slot while the old host still runs.
struct SeatToken {
    Seat seat = sim::kNoPlayer;
    std::uint64_t salt = 0;  ///< drawn by the host that gave the token out
    std::uint64_t verifier = 0;
    friend bool operator==(const SeatToken&, const SeatToken&) = default;
};

/// What a token is checked against after the host changed: the first 8 bytes of
/// SHA-256(salt, token), both little endian. One way: the verifier does not give the token away.
[[nodiscard]] std::uint64_t token_verifier(std::uint64_t token, std::uint64_t salt) noexcept;

/// (Host, to its successor only) What the successor needs to carry the game on when the host leaves:
/// the slots' token verifiers, so that the others get their slots back, and the slots the computer
/// plays until someone comes. Sent again whenever it changes.
struct Standby {
    std::vector<SeatToken> tokens;
    std::vector<Seat> computer_held;
    friend bool operator==(const Standby&, const Standby&) = default;
};

/// (Host) Commands of this computer's player the host did not carry out since the last notice: more
/// than a few frames' share at once, or one other computers could not read (MS-10, MS-17).
struct Dropped {
    std::uint32_t commands = 0;
};

/// (Host) A piece of a message larger than kMaxPacketBytes (MS-7, MS-11): a game, or many frames at
/// once. `total` is the size of the whole message, the same in every piece; the pieces of one
/// message come one after another, and the computer reads the message once it has them all.
struct Part {
    std::uint32_t total = 0;
    std::vector<std::uint8_t> bytes;
};

/// (Host) The host ends the session on purpose: the computers move on to the successor at once
/// (`successor_address`, which spectators get only now). A connection that just breaks is not that:
/// a computer first tries to get back to its host (MS-2).
struct HostLeaving {
    std::string successor_address;
};

/// (Host) Files of a package of the game the computer asked for (Hello::fetch_packages): the
/// `index`th of `count` messages of the package, together its files in package_hash order. The
/// computer keeps the package only when the files have its hash (assets::store_package).
struct PackageFiles {
    assets::ContentPackage package;
    std::uint32_t index = 0;
    std::uint32_t count = 1;
    std::vector<assets::PackageFile> files;
};

using Message = std::variant<Hello, CommandBatch, Progress, Welcome, Refused, Snapshot, Turn, Roster, Desync, Lobby,
                             TakeSeat, SetReady, Chat, Standby, SetBanner, Dropped, HostLeaving, Part, PackageFiles>;

[[nodiscard]] std::vector<std::uint8_t> encode(const Message& message);
/// What goes over the transport for an encoded message: the message itself, or the encoded Parts of
/// one larger than kMaxPacketBytes.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> packets(std::span<const std::uint8_t> message);
/// Nothing if the bytes are cut off, too long, of an unknown type or out of range.
[[nodiscard]] std::optional<Message> decode(std::span<const std::uint8_t> bytes);

/// The player a command acts for (every command names one).
[[nodiscard]] sim::PlayerId command_player(const sim::Command& command) noexcept;

}  // namespace o1602::net
