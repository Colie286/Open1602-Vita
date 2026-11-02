// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <limits>
#include <random>
#include <utility>

#include "o1602/assets/game_content.hpp"
#include "o1602/net/session.hpp"
#include "o1602/sim/command_io.hpp"
#include "o1602/sim/save_game.hpp"

namespace o1602::net {

namespace {

constexpr std::size_t kKeptHashes = 32;  ///< the host's own fingerprints, for computers behind it
constexpr std::size_t kPeerHashes = 16;  ///< fingerprints of a computer ahead of the host
constexpr std::uint32_t kLeadShrinkMs = 500;
constexpr std::uint32_t kJitterMarginMs = 30;

std::uint64_t random_token() {
    std::random_device device;
    std::uint64_t t = 0;
    while (t == 0) {
        t = (std::uint64_t{device()} << 32) ^ device();
    }
    return t;
}

/// Frames' worth of commands a player may have waiting for room in a frame.
constexpr std::uint32_t kWaitingFrames = 4;
/// Pauses, resumes and renames a computer may send per second.
constexpr std::uint32_t kMaxGameOrdersPerSecond = 4;
/// Slot changes a computer may ask for per second, and how often the host sends the lobby at most
/// (MS-16): each change goes to everyone, up to 64 KiB of setup.
constexpr std::uint32_t kMaxSeatChangesPerSecond = 4;
constexpr std::uint32_t kLobbyEveryMs = 100;
/// How often the roster goes out at most when only the progress of games on their way changed.
constexpr std::uint32_t kLoadingEveryMs = 250;
/// Time a new connection has to send its Hello (MS-12: short, so that connections that never say
/// who they are do not hold the few there are).
constexpr std::uint32_t kGreetingMs = 3'000;
/// Games written for computers that join or resync: a few at once, then one a second. A computer
/// that leaves and comes back in a loop cannot make the host write the whole game more often.
constexpr std::uint32_t kGameCostMs = 1'000;
constexpr std::uint32_t kGameCreditMs = 4 * kGameCostMs;

/// A name or chat line from the network without control characters (line breaks would split the
/// lists and files they end up in).
std::string printable(std::string text) {
    std::replace_if(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }, ' ');
    return text;
}

/// Someone (or nobody) takes a slot; its banner stays with the slot.
void occupy(LobbySeat& seat, LobbySeat::Kind kind, std::string name = {}, bool ready = false) {
    seat.kind = kind;
    seat.name = std::move(name);
    seat.ready = ready;
}

}  // namespace

struct HostSession::Peer {
    enum class State : std::uint8_t {
        Greeting,  ///< connected, no Hello yet (or welcomed and waiting for its game)
        Loading,   ///< its game is on the way or loading
        Playing,
    };
    LinkId link = 0;
    std::string address;  ///< as the transport sees it, for the others if this one hosts next
    State state = State::Greeting;
    bool gone = false;  ///< dropped; removed at the end of the update
    bool welcomed = false;
    bool game_due = false;  ///< send it the game at the end of this update
    bool packing = false;   ///< its game is being packed (m_packing)
    Seat seat = sim::kNoPlayer;
    std::string name;
    std::uint32_t epoch = 0;   ///< of the last game sent
    std::uint64_t frames = 0;  ///< run, as last reported
    bool lagging = false;
    std::uint32_t stalls = 0;         ///< times the others stopped waiting for it
    std::uint64_t token = 0;          ///< to come back to its slot (0: it watches)
    std::uint64_t owner = 0;          ///< player_verifier of its player ID (0: none)
    std::uint32_t chat_lines = 0;     ///< in the current second
    std::uint32_t game_orders = 0;    ///< pauses and renames in the current second
    std::uint32_t lobby_orders = 0;   ///< slot changes in the current second
    std::uint32_t greeting_ms = 0;    ///< connected without a Hello this long
    std::uint32_t resyncs = 0;        ///< games sent after desyncs without a matching fingerprint since
    std::uint8_t loaded = 0;          ///< of the game on its way, in percent (LoadProgress)
    bool start_wait = false;          ///< the game's first frame waits for it (SessionConfig::start_wait_ms)
    std::uint32_t start_idle_ms = 0;  ///< since its game last made progress, while the start waits for it
    std::deque<FrameHash> hashes;     ///< reported ahead of the host, not compared yet
};

HostSession::HostSession(std::unique_ptr<Transport> transport, sim::Simulation& simulation,
                         const assets::BuildingTable& buildings, Seat seat, std::string name, std::uint64_t content,
                         SessionConfig config)
    : Session(std::move(transport), std::move(config)),
      m_simulation(&simulation),
      m_buildings(&buildings),
      m_name(std::move(name)),
      m_content(content),
      m_lead(m_config.min_lead),
      m_salt(random_token()) {
    m_seat = seat;
    // The lobby of a running game: its human slots are open, and so are the computer players'
    // (the computer plays one until someone comes and again when he leaves).
    m_lobby.started = true;
    const auto& players = simulation.world().players();
    for (std::size_t s = 0; s < players.size() && s < kMaxLobbySeats; ++s) {
        LobbySeat lobby_seat;
        lobby_seat.banner = players[s].banner;
        if (s == seat) {
            occupy(lobby_seat, LobbySeat::Kind::Human, m_name, true);
        } else if (players[s].role != sim::PlayerRole::Human && players[s].role != sim::PlayerRole::Computer) {
            lobby_seat.kind = LobbySeat::Kind::Reserved;
        } else if (!players[s].active()) {
            lobby_seat.kind = LobbySeat::Kind::Closed;
        } else if (players[s].role == sim::PlayerRole::Computer) {
            m_computer_held.push_back(static_cast<Seat>(s));
        }
        m_lobby.seats.push_back(std::move(lobby_seat));
    }
    // The game keeps who plays the host's slot.
    if (const std::uint64_t own = player_verifier(m_config.player);
        own != 0 && seat < players.size() && players[seat].owner != own) {
        insert(sim::PlayerControl{seat, false, own});
    }
    init_lobby();
}

HostSession::HostSession(std::unique_ptr<Transport> transport, Lobby lobby, Seat seat, std::string name,
                         std::uint64_t content, SessionConfig config)
    : Session(std::move(transport), std::move(config)),
      m_simulation(nullptr),
      m_buildings(nullptr),
      m_name(std::move(name)),
      m_content(content),
      m_lead(m_config.min_lead),
      m_salt(random_token()) {
    m_lobby = std::move(lobby);
    if (m_lobby.seats.size() > kMaxLobbySeats) {
        m_lobby.seats.resize(kMaxLobbySeats);
    }
    m_lobby.started = false;
    for (LobbySeat& s : m_lobby.seats) {
        if (s.kind == LobbySeat::Kind::Human) {
            occupy(s, LobbySeat::Kind::Open);  // only computers that come in are human players
        }
    }
    if (m_lobby.banners.size() > kMaxLobbyBanners) {
        m_lobby.banners.resize(kMaxLobbyBanners);
    }
    sort_out_banners();
    m_seat = seat < m_lobby.seats.size() ? seat : sim::kNoPlayer;
    if (m_seat != sim::kNoPlayer) {
        occupy(m_lobby.seats[m_seat], LobbySeat::Kind::Human, m_name, true);
    }
    init_lobby();
}

HostSession::HostSession(std::unique_ptr<Transport> transport, ClientSession& client,
                         const assets::BuildingTable& buildings)
    : Session(std::move(transport), client.m_config),
      m_simulation(client.m_simulation),
      m_buildings(&buildings),
      m_name(client.m_hello.name),
      m_content(client.m_hello.content),
      m_lead(m_config.min_lead),
      m_salt(random_token()) {
    m_config.password = client.m_hello.password;
    m_config.join_running = client.m_lobby.join_running;
    m_config.spectators = client.m_lobby.spectators;
    m_config.computer_takes_over = client.m_lobby.computer_takes_over;
    m_config.host_controls_speed = client.m_lobby.host_controls_speed;
    m_config.spectators_chat = client.m_lobby.spectators_chat;
    m_seat = client.m_seat;
    // The game goes on from this computer's frame. The frames the old host closed beyond it are
    // as good as any: every computer gets them with this game. What the others had beyond them
    // goes with the game they load from here.
    m_frames = client.m_frames;
    m_log_start = m_frames;
    for (const std::vector<sim::Command>& commands : client.m_frames_ahead) {
        m_log.push_back(Frame{commands});
    }
    m_closed = client.m_closed;
    m_broadcast = m_closed;
    m_lobby = client.m_lobby;
    if (client.m_standby) {
        // What the old host sent is checked like anything from the network: a slot outside the
        // lobby would be written outside it (MS-8).
        const auto good = [&](Seat s) {
            return s < m_lobby.seats.size() && s != m_seat && m_lobby.seats[s].kind != LobbySeat::Kind::Closed;
        };
        for (const SeatToken& t : client.m_standby->tokens) {
            if (good(t.seat) && t.verifier != 0) {
                m_verifiers.push_back(t);
            }
        }
        for (const Seat s : client.m_standby->computer_held) {
            if (good(s) && std::find(m_computer_held.begin(), m_computer_held.end(), s) == m_computer_held.end()) {
                m_computer_held.push_back(s);
            }
        }
    }
    const auto old_host =
        std::find_if(client.m_roster.begin(), client.m_roster.end(), [](const Member& m) { return m.host; });
    const Seat left = old_host != client.m_roster.end() ? old_host->seat : sim::kNoPlayer;
    m_lobby.started = true;
    for (std::size_t s = 0; s < m_lobby.seats.size(); ++s) {
        if (s == m_seat) {
            occupy(m_lobby.seats[s], LobbySeat::Kind::Human, m_name, true);
        } else if (m_lobby.seats[s].kind == LobbySeat::Kind::Human) {
            occupy(m_lobby.seats[s], LobbySeat::Kind::Open);  // until its player is back
            vacate(static_cast<Seat>(s));                     // or the computer takes it over
            // Kept for its player, who may notice the host's leaving seconds after a newcomer (MS-14).
            reserve(static_cast<Seat>(s), std::max(m_config.take_over_ms, m_config.migration_ms));
        }
    }
    // The old host's slot: the computer plays it until someone takes it, as a slot left open.
    if (left != sim::kNoPlayer && left < m_lobby.seats.size() && left != m_seat) {
        forget_token(left);
        insert(sim::PlayerControl{left, true});
        if (std::find(m_computer_held.begin(), m_computer_held.end(), left) == m_computer_held.end()) {
            m_computer_held.push_back(left);
        }
    }
    forget_token(m_seat);
    init_lobby();
}

void HostSession::init_lobby() {
    m_lobby.join_running = m_config.join_running;
    m_lobby.computer_takes_over = m_config.computer_takes_over;
    m_lobby.host_controls_speed = m_config.host_controls_speed;
    m_lobby.spectators = m_config.spectators;
    m_lobby.spectators_chat = m_config.spectators_chat;
    m_lobby.password = !m_config.password.empty();
    m_lobby.title.resize(std::min(m_lobby.title.size(), kMaxTitleLength));
    m_lobby.game.resize(std::min(m_lobby.game.size(), kMaxTitleLength));
    if (m_lobby.setup.size() > kMaxSetupBytes) {
        m_lobby.setup.clear();
    }
    refresh_roster();
}

HostSession::~HostSession() {
    // Leaving on purpose: the others move on to the successor at once (MS-2).
    broadcast(HostLeaving{m_successor_address});
    for (const auto& p : m_peers) {
        m_transport->disconnect(p->link);
    }
    m_transport->flush();
}

void HostSession::submit(sim::Command command) {
    queue(m_seat, std::move(command));
}

void HostSession::queue(Seat from, sim::Command command) {
    if (!started() || from == sim::kNoPlayer || command_player(command) != from) {
        return;  // a player gives orders for himself only (review point 6)
    }
    if (from != m_seat && std::none_of(m_peers.begin(), m_peers.end(),
                                       [from](const auto& p) { return !p->gone && p->welcomed && p->seat == from; })) {
        return;
    }
    if (from >= m_allowance.size()) {
        m_allowance.resize(std::size_t{from} + 1);
    }
    Allowance& a = m_allowance[from];
    if (a.waiting >= kWaitingFrames * m_share) {
        ++a.dropped;  // more than a few frames' worth: dropped at once, before any more work (MS-9)
        return;
    }
    if (auto* order = std::get_if<sim::SoldierOrder>(&command)) {
        // The formations may differ between computers (data fixes, DS-5); the interface never names
        // one, the places travel in `offsets`.
        order->formation = 0xFF;
    }
    const auto bytes = sim::command_size(command);
    if (!bytes) {
        ++a.dropped;  // the others could not read it and would leave the game (MS-10)
        return;
    }
    admit(Waiting{from, *bytes, std::move(command)});
}

void HostSession::admit(Waiting w) {
    // Per player and for the frame as a whole: one frame must fit into one message and be quick to
    // run (MS-3). Each player has a share of it, so that a few cannot crowd out the others; what
    // does not fit waits for the next frames, in the order it came.
    Allowance& a = m_allowance[w.from];
    if (a.waiting > 0 || a.commands >= m_share || a.owed >= m_work_share || m_open_work >= kFrameWork ||
        m_open_bytes >= kMaxFrameBytes || m_open.size() >= kMaxCommandsPerMessage - kReservedCommands) {
        ++a.waiting;
        m_overflow.push_back(std::move(w));
        return;
    }
    const std::uint64_t work = command_work(m_simulation->world(), w.command);
    ++a.commands;
    a.owed += work;
    m_open_work += work;
    m_open_bytes += w.bytes;
    m_open.push_back(std::move(w.command));
}

void HostSession::insert(sim::Command command) {
    if (m_open.size() < kMaxCommandsPerMessage) {
        m_open.push_back(std::move(command));
    } else {
        m_inserted.push_back(std::move(command));
    }
}

void HostSession::refresh_shares() {
    std::uint32_t players = m_seat != sim::kNoPlayer ? 1U : 0U;
    for (const auto& p : m_peers) {
        players += !p->gone && p->welcomed && p->seat != sim::kNoPlayer ? 1U : 0U;
    }
    m_share = command_share(players);
    m_work_share = work_share(players);
}

void HostSession::tell_dropped() {
    for (std::size_t seat = 0; seat < m_allowance.size(); ++seat) {
        Allowance& a = m_allowance[seat];
        if (a.dropped == 0) {
            continue;
        }
        if (seat == m_seat) {
            SessionEvent e;
            e.kind = SessionEvent::Kind::Dropped;
            e.seat = m_seat;
            e.count = a.dropped;
            event(std::move(e));
        } else {
            for (const auto& p : m_peers) {
                if (!p->gone && p->welcomed && p->seat == seat) {
                    send(p->link, Dropped{a.dropped});
                }
            }
        }
        a.dropped = 0;
    }
}

void HostSession::update(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick) {
    m_incoming.clear();
    m_transport->poll(m_incoming);
    for (TransportEvent& e : m_incoming) {
        switch (e.kind) {
            case TransportEvent::Kind::Connected: {
                auto peer = std::make_unique<Peer>();
                peer->link = e.link;
                peer->address = m_transport->address(e.link);
                m_peers.push_back(std::move(peer));
                break;
            }
            case TransportEvent::Kind::Disconnected:
                if (Peer* p = peer_of(e.link)) {
                    drop(*p, std::nullopt);
                }
                break;
            case TransportEvent::Kind::Received:
                receive(e.link, e.data);
                break;
        }
    }
    if (m_chat_ms += real_ms; m_chat_ms >= 1000) {
        m_chat_ms = 0;
        for (const auto& p : m_peers) {
            p->chat_lines = 0;
            p->game_orders = 0;
            p->lobby_orders = 0;
        }
    }
    // A connection that never says who it is would hold one of the few connections for good.
    for (const auto& p : m_peers) {
        if (p->gone || p->welcomed) {
            continue;
        }
        p->greeting_ms += real_ms;
        if (p->greeting_ms > kGreetingMs) {
            drop(*p, std::nullopt);
        }
    }
    m_lobby_wait_ms -= std::min(m_lobby_wait_ms, real_ms);
    if (m_lobby_changed && m_lobby_wait_ms == 0) {
        m_lobby_changed = false;
        m_lobby_wait_ms = kLobbyEveryMs;
        broadcast(m_lobby);
        lobby_event();
    }
    if (started()) {
        refresh_shares();
        std::erase_if(m_reserved, [real_ms](auto& r) {
            r.second -= std::min(r.second, real_ms);
            return r.second == 0;
        });
        take_over_vacant(real_ms);
        update_lead(real_ms);
        if (wait_for_start(real_ms)) {
            m_waiting = true;  // the first frame waits until every player has the game
        } else {
            run(real_ms, before_tick);
        }
        broadcast_frames();
        deliver_packed(real_ms);
        m_game_credit_ms = std::min(kGameCreditMs, m_game_credit_ms + std::min(real_ms, kGameCreditMs));
        // Without credit, or while a game is being packed, who waits for a game gets the next one
        // (game_due stays set).
        const bool due = std::any_of(m_peers.begin(), m_peers.end(), [](const auto& p) { return p->game_due; });
        if (due && !m_packing && m_game_credit_ms >= kGameCostMs) {
            m_game_credit_ms -= kGameCostMs;  // one game written for all it is due to now
            start_packing();
            deliver_packed(0);  // at once if the host does not let it pack while the game goes on
        }
        forget_frames();
    }
    std::erase_if(m_peers, [](const auto& p) { return p->gone; });
    m_roster_wait_ms -= std::min(m_roster_wait_ms, real_ms);
    if (m_roster_changed || (m_loading_changed && m_roster_wait_ms == 0)) {
        refresh_roster();
    }
    if (started()) {
        hand_over();
    }
    tell_dropped();
    m_transport->flush();
}

HostSession::Peer* HostSession::peer_of(LinkId link) {
    const auto it =
        std::find_if(m_peers.begin(), m_peers.end(), [link](const auto& p) { return !p->gone && p->link == link; });
    return it == m_peers.end() ? nullptr : it->get();
}

void HostSession::receive(LinkId link, std::span<const std::uint8_t> bytes) {
    Peer* peer = peer_of(link);
    if (peer == nullptr) {
        return;
    }
    if (bytes.size() > kMaxMessageToHost) {
        drop(*peer, RefuseReason::Invalid);  // a computer sends commands and reports, never games
        return;
    }
    if (!peer->welcomed && bytes.size() >= 3 && bytes[0] == 0 &&
        static_cast<std::uint16_t>(bytes[1] | (bytes[2] << 8)) != kProtocolVersion) {
        drop(*peer, RefuseReason::Version);  // a Hello of another version, whatever follows (MS-22)
        return;
    }
    auto message = decode(bytes);
    if (!message) {
        drop(*peer, RefuseReason::Invalid);
        return;
    }
    if (auto* hello_message = std::get_if<Hello>(&*message); hello_message != nullptr && !peer->welcomed) {
        hello(*peer, *hello_message);
    } else if (!peer->welcomed) {
        drop(*peer, RefuseReason::Invalid);  // anything before Hello, or a message only the host sends
    } else if (auto* batch = std::get_if<CommandBatch>(&*message)) {
        commands(*peer, *batch);
    } else if (const auto* report = std::get_if<Progress>(&*message)) {
        progress(*peer, *report);
    } else if (const auto* loading = std::get_if<LoadProgress>(&*message)) {
        load_progress(*peer, *loading);
    } else if (const auto* seat = std::get_if<TakeSeat>(&*message)) {
        // Each move costs a Welcome and a new token; ready and banner only change the lobby, which
        // goes out at most every kLobbyEveryMs (MS-16).
        if (peer->lobby_orders < kMaxSeatChangesPerSecond) {
            ++peer->lobby_orders;
            take_seat(*peer, seat->seat);
        }
    } else if (const auto* r = std::get_if<SetReady>(&*message)) {
        ready(*peer, r->ready);
    } else if (const auto* b = std::get_if<SetBanner>(&*message)) {
        set_banner(*peer, b->banner);
    } else if (const auto* line = std::get_if<Chat>(&*message)) {
        chat(*peer, *line);
    } else {
        drop(*peer, RefuseReason::Invalid);
    }
}

void HostSession::hello(Peer& peer, const Hello& m) {
    if (m.protocol != kProtocolVersion) {
        drop(peer, RefuseReason::Version);
        return;
    }
    if (m.content != m_content) {
        drop(peer, RefuseReason::Content);
        return;
    }
    if (m.password != m_config.password) {
        drop(peer, RefuseReason::Password);
        return;
    }
    if (auto missing = assets::missing_packages(packages(), m.packages); !missing.empty()) {
        // Sent only to a computer with the password, and only once its person agreed to them.
        const std::uint64_t bytes = share_bytes(missing);
        if (!m.fetch_packages || bytes == 0 || !send_packages(peer, missing)) {
            drop(peer, RefuseReason::Packages, std::move(missing), bytes);
            return;
        }
    }
    Seat seat = seat_of_token(m);
    if (seat != sim::kNoPlayer) {
        // A token never takes a slot from a connection that is still there (MS-1): whoever holds
        // it plays on, and the token is good again once that connection has ended.
        const auto old = std::find_if(m_peers.begin(), m_peers.end(), [&](const auto& p) {
            return p.get() != &peer && !p->gone && p->welcomed && p->seat == seat;
        });
        if (old != m_peers.end()) {
            drop(peer, RefuseReason::Taken);
            return;
        }
        if (const auto v = std::find_if(m_verifiers.begin(), m_verifiers.end(),
                                        [&](const SeatToken& t) { return t.seat == seat; });
            v != m_verifiers.end()) {
            // Back after the host changed: from now on this host knows the token itself.
            m_verifiers.erase(v);
            forget_token(seat);
            m_tokens.emplace_back(seat, m.token);
            m_standby_changed = true;
        }
    }
    if (seat == sim::kNoPlayer && !peer.address.empty() &&
        std::find(m_banned.begin(), m_banned.end(), peer.address) != m_banned.end()) {
        // A computer sent away in this session does not come straight back (MS-12); others from the
        // same address still return to their slots with their tokens.
        drop(peer, RefuseReason::Kicked);
        return;
    }
    peer.owner = player_verifier(m.player);
    // A person kicked in this session has no claim to their slot any more.
    const std::uint64_t claim =
        std::find(m_kicked.begin(), m_kicked.end(), peer.owner) != m_kicked.end() ? 0 : peer.owner;
    if (seat == sim::kNoPlayer) {
        // Back into the slot this person played (the game keeps who it was), also after a restart
        // of the program or a change of host, and also when newcomers may no longer join.
        seat = seat_of_owner(claim);
        if (seat != sim::kNoPlayer) {
            forget_token(seat);  // whoever had it in the meantime cannot come back with the old token
        }
    }
    if (seat == sim::kNoPlayer && m.returning && m_config.spectators) {
        // After the host changed, without its slot (it watched, or someone took the slot): it
        // watches, rather than taking a slot it did not have.
    } else if (seat == sim::kNoPlayer) {
        if (started() && !m_config.join_running) {
            drop(peer, RefuseReason::Closed);
            return;
        }
        seat = free_seat(m.seat, claim);
        if (seat == sim::kNoPlayer && !m_config.spectators) {
            drop(peer, RefuseReason::Full);
            return;
        }
        // Someone new in the slot: whoever had it before cannot come back with the old token.
        forget_token(seat);
    }
    if (seat == sim::kNoPlayer) {
        const auto watching = std::count_if(m_peers.begin(), m_peers.end(), [&](const auto& p) {
            return p.get() != &peer && !p->gone && p->welcomed && p->seat == sim::kNoPlayer;
        });
        if (static_cast<std::uint64_t>(watching) >= m_config.max_spectators) {
            drop(peer, RefuseReason::Full);  // each spectator holds a connection and gets the game (MS-12)
            return;
        }
    }
    const auto members = std::count_if(m_peers.begin(), m_peers.end(),
                                       [&](const auto& p) { return p.get() != &peer && !p->gone && p->welcomed; });
    if (static_cast<std::uint64_t>(members) >= kMaxMembers) {
        drop(peer, RefuseReason::Full);  // the roster would not decode any more (LC-1)
        return;
    }
    peer.welcomed = true;
    peer.game_due = started();
    peer.seat = seat;
    peer.name = printable(m.name);
    if (peer.name.empty()) {
        peer.name = "Player";  // replaced by the player's own name (PlayerRename) once the game runs
    }
    if (seat != sim::kNoPlayer) {
        peer.token = token_of(seat);
        occupy(m_lobby.seats[seat], LobbySeat::Kind::Human, peer.name, false);
        std::erase_if(m_vacant, [seat](const auto& v) { return v.first == seat; });
        std::erase_if(m_reserved, [seat](const auto& r) { return r.first == seat; });
        if (started()) {
            // The player takes the slot over from the computer, under the player's name, as
            // commands of that player like any other.
            insert(sim::PlayerControl{seat, false, peer.owner});
            insert(sim::PlayerRename{seat, peer.name});
        }
    }
    send(peer.link, Welcome{kProtocolVersion, seat, peer.token});
    m_lobby_changed = true;
    SessionEvent e;
    e.kind = SessionEvent::Kind::Joined;
    e.seat = seat;
    e.name = peer.name;
    e.frame = m_frames;
    event(std::move(e));
    m_roster_changed = true;
}

void HostSession::commands(Peer& peer, CommandBatch& m) {
    if (peer.seat == sim::kNoPlayer || peer.state == Peer::State::Greeting) {
        return;  // spectators give no orders, and nobody without a game
    }
    for (sim::Command& c : m.commands) {
        // Only the host hands slots to the computer (ADR 0011); everyone may pause, resume, set the
        // speed and name himself, a few times a second, unless only the host controls the speed.
        if (std::holds_alternative<sim::PlayerControl>(c)) {
            continue;
        }
        if (std::holds_alternative<sim::GameSpeedOrder>(c) && m_config.host_controls_speed) {
            continue;  // MS-20
        }
        if (std::holds_alternative<sim::GameSpeedOrder>(c) || std::holds_alternative<sim::PlayerRename>(c)) {
            if (peer.game_orders >= kMaxGameOrdersPerSecond) {
                continue;
            }
            ++peer.game_orders;
        }
        queue(peer.seat, std::move(c));
    }
}

void HostSession::progress(Peer& peer, const Progress& m) {
    if (m.epoch != peer.epoch || peer.state == Peer::State::Greeting) {
        return;  // about a game it had before the last one sent
    }
    if (m.frames < peer.frames || m.frames > m_closed) {
        drop(peer, RefuseReason::Invalid);
        return;
    }
    peer.frames = m.frames;
    if (peer.state == Peer::State::Loading) {
        peer.state = Peer::State::Playing;
        peer.loaded = 100;
        // Far behind after loading: it catches up without the others waiting for it.
        peer.lagging = m_frames > peer.frames + ahead_frames();
        m_roster_changed = true;
    } else if (peer.lagging && m_frames <= peer.frames + ahead_frames()) {
        peer.lagging = false;
        m_roster_changed = true;
    }
    if (m.hash) {
        compare(peer, *m.hash);
    }
}

void HostSession::load_progress(Peer& peer, const LoadProgress& m) {
    // Only of a game on its way: a computer that plays has it.
    if (peer.state != Peer::State::Loading || peer.packing || m.percent == peer.loaded) {
        return;
    }
    peer.loaded = m.percent;
    peer.start_idle_ms = 0;
    m_loading_changed = true;
}

bool HostSession::wait_for_start(std::uint32_t real_ms) {
    if (!m_starting) {
        return false;
    }
    bool waiting = false;
    for (const auto& p : m_peers) {
        if (p->gone || !p->start_wait) {
            continue;
        }
        if (p->state == Peer::State::Playing && !p->packing && !p->game_due) {
            p->start_wait = false;  // it has the game
            continue;
        }
        // Writing and packing its game is the host's part: only a game on its way that does not
        // come on counts against the computer.
        if (p->state == Peer::State::Loading && !p->packing && !p->game_due) {
            p->start_idle_ms += real_ms;
            if (p->start_idle_ms >= m_config.start_wait_ms) {
                p->start_wait = false;  // it catches up once it has the game
                continue;
            }
        }
        waiting = true;
    }
    if (!waiting) {
        stop_starting();
    }
    return waiting;
}

void HostSession::stop_starting() {
    if (!m_starting) {
        return;
    }
    m_starting = false;
    for (const auto& p : m_peers) {
        p->start_wait = false;
    }
    m_roster_changed = true;
    SessionEvent e;
    e.kind = SessionEvent::Kind::Started;
    e.seat = m_seat;
    event(std::move(e));
}

void HostSession::start_now() {
    stop_starting();
}

void HostSession::compare(Peer& peer, const FrameHash& theirs) {
    if (theirs.frame > peer.frames) {
        drop(peer, RefuseReason::Invalid);
        return;
    }
    if (theirs.frame > m_frames) {
        // Ahead of the host: compared once the host has run that frame.
        if (peer.hashes.size() == kPeerHashes) {
            peer.hashes.pop_front();
        }
        peer.hashes.push_back(theirs);
        return;
    }
    const auto mine =
        std::find_if(m_hashes.begin(), m_hashes.end(), [&](const FrameHash& h) { return h.frame == theirs.frame; });
    if (mine == m_hashes.end()) {
        return;  // too old, or a frame whose game could not be written
    }
    ++m_compared;
    if (mine->hash == theirs.hash) {
        ++m_matched;
        peer.resyncs = 0;
    } else {
        desync(peer, theirs.frame);
    }
}

void HostSession::desync(Peer& peer, std::uint64_t frame) {
    SessionEvent e;
    e.kind = SessionEvent::Kind::Desync;
    e.seat = peer.seat;
    e.name = peer.name;
    e.frame = frame;
    event(std::move(e));
    if (peer.resyncs >= m_config.max_resyncs) {
        drop(peer, RefuseReason::Desync);
        return;
    }
    ++peer.resyncs;
    peer.game_due = true;
    broadcast(Desync{frame, {peer.seat}});
}

void HostSession::close_frame() {
    m_log.push_back(Frame{std::move(m_open)});
    m_open.clear();
    ++m_closed;
    m_open_work = 0;
    m_open_bytes = 0;
    refresh_shares();
    for (Allowance& a : m_allowance) {
        a.commands = 0;
        a.owed = a.owed > m_work_share ? a.owed - m_work_share : 0;
        a.waiting = 0;
    }
    // What waited goes first into the new frame, in the order it came: the host's own, then the
    // players' commands (those that do not fit again wait on).
    std::deque<sim::Command> inserted;
    inserted.swap(m_inserted);
    for (sim::Command& command : inserted) {
        insert(std::move(command));
    }
    std::deque<Waiting> waiting;
    waiting.swap(m_overflow);
    for (Waiting& w : waiting) {
        admit(std::move(w));
    }
}

std::uint64_t HostSession::ahead_frames() const {
    const std::uint32_t speed = m_simulation->paused() ? 1 : std::max<std::uint32_t>(m_simulation->speed(), 1);
    return m_lead + std::uint64_t{m_config.max_ahead_ms} * speed / sim::Simulation::kTickMs;
}

std::uint64_t HostSession::resync_frames() const {
    // Game time, like ahead_frames: at speed 8 the frames pass eight times as fast (MS-19).
    const std::uint32_t speed = m_simulation->paused() ? 1 : std::max<std::uint32_t>(m_simulation->speed(), 1);
    return std::uint64_t{m_config.resync_behind} * speed;
}

std::uint64_t HostSession::slowest() const {
    std::uint64_t frames = std::numeric_limits<std::uint64_t>::max();
    for (const auto& p : m_peers) {
        if (!p->gone && p->state == Peer::State::Playing && !p->lagging && !p->packing) {
            frames = std::min(frames, p->frames);
        }
    }
    return frames;
}

void HostSession::run(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick) {
    const auto close_ahead = [this] {
        while (m_closed < m_frames + m_lead) {
            close_frame();
        }
    };
    close_ahead();
    const std::uint32_t due = frames_due(real_ms, *m_simulation);
    const std::uint64_t slow = slowest();
    const std::uint64_t limit = slow == std::numeric_limits<std::uint64_t>::max() ? slow : slow + ahead_frames();
    std::uint32_t done = 0;
    bool held = false;
    while (done < due) {
        if (m_frames >= limit) {
            held = true;
            break;
        }
        if (before_tick && !before_tick()) {
            break;
        }
        const Frame& frame = m_log[static_cast<std::size_t>(m_frames - m_log_start)];
        if (const auto hash = run_frame(*m_simulation, frame.commands)) {
            if (m_hashes.size() == kKeptHashes) {
                m_hashes.pop_front();
            }
            m_hashes.push_back(*hash);
            for (const auto& peer : m_peers) {
                Peer& p = *peer;
                const auto theirs = std::find_if(p.hashes.begin(), p.hashes.end(),
                                                 [&](const FrameHash& h) { return h.frame == hash->frame; });
                if (theirs != p.hashes.end()) {
                    const FrameHash h = *theirs;
                    p.hashes.erase(p.hashes.begin(), theirs + 1);
                    compare(p, h);
                }
            }
        }
        ++done;
        close_ahead();
    }
    ran(done);
    m_waiting = held;
    watch_stalls(real_ms, held && done == 0);
}

void HostSession::watch_stalls(std::uint32_t real_ms, bool stalled) {
    const auto holding = [&](const Peer& p) {
        return !p.gone && p.state == Peer::State::Playing && !p.lagging && !p.packing &&
               p.frames + ahead_frames() <= m_frames;
    };
    // A computer that has held everyone up before gets a quarter of the time: one that stalls
    // again and again would otherwise stop the game for the full time each time.
    const bool again =
        std::any_of(m_peers.begin(), m_peers.end(), [&](const auto& p) { return holding(*p) && p->stalls > 0; });
    const std::uint32_t limit = again ? m_config.stall_ms / 4 : m_config.stall_ms;
    // Only updates in which no frame could run count: a computer that is slow but keeps up some
    // frames sets the pace of the game, and is not lagging (MS-15).
    if (!stalled) {
        m_held_ms = 0;
    } else if (m_held_ms += real_ms; m_held_ms >= limit) {
        m_held_ms = 0;
        for (const auto& p : m_peers) {
            if (holding(*p)) {
                p->lagging = true;
                ++p->stalls;
                m_roster_changed = true;
                SessionEvent e;
                e.kind = SessionEvent::Kind::Lagging;
                e.seat = p->seat;
                e.name = p->name;
                e.frame = m_frames;
                event(std::move(e));
            }
        }
    }
    for (const auto& p : m_peers) {
        if (!p->gone && p->lagging && !p->game_due && p->frames + resync_frames() < m_frames) {
            p->game_due = true;  // quicker to load the game as it is now than to run all those frames
        }
    }
}

void HostSession::broadcast_frames() {
    if (m_broadcast == m_closed) {
        return;
    }
    Turn turn;
    turn.first = m_broadcast;
    turn.closed = m_closed;
    turn.lead = m_lead;
    for (std::uint64_t f = m_broadcast; f < m_closed; ++f) {
        const Frame& frame = m_log[static_cast<std::size_t>(f - m_log_start)];
        if (!frame.commands.empty()) {
            turn.frames.push_back(FrameCommands{f, frame.commands});
        }
    }
    const std::vector<std::uint8_t> bytes = encode(turn);
    for (const auto& p : m_peers) {
        if (!p->gone && p->state != Peer::State::Greeting && !p->packing) {  // its game is on the way
            send_bytes(p->link, bytes);
        }
    }
    m_broadcast = m_closed;
}

void HostSession::start_packing() {
    // Written once for all computers it is due to now, on the game thread: the game as it is after
    // m_frames frames. Only the packing, which reads nothing but these bytes, goes to a worker.
    auto sections = sim::encode_game(m_simulation->world(), *m_buildings, m_simulation);
    for (const auto& p : m_peers) {
        if (!p->game_due) {
            continue;
        }
        p->game_due = false;
        if (!sections) {
            drop(*p, RefuseReason::Closed);  // the game cannot be written: nobody can come in
            continue;
        }
        // From now on what it says about the game it had is no longer of interest (progress()
        // takes the epoch of the game on its way), nobody waits for it, and it gets no frames
        // until its game comes with those run since.
        p->packing = true;
        p->loaded = 0;
        ++p->epoch;
        p->lagging = false;
        p->hashes.clear();
        m_roster_changed = true;
    }
    if (!sections) {
        return;
    }
    m_packing =
        Packing{std::async(std::launch::async, [bytes = std::move(sections).value()] { return sim::pack_game(bytes); }),
                m_frames, m_simulation->speed(), m_simulation->paused()};
}

void HostSession::deliver_packed(std::uint32_t real_ms) {
    if (!m_packing) {
        return;
    }
    m_packing->waited_ms += real_ms;
    if (m_packing->waited_ms >= m_config.pack_wait_ms) {
        m_packing->file.wait();
    } else if (m_packing->file.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    auto file = m_packing->file.get();
    const Packing packing = {{}, m_packing->frame, m_packing->speed, m_packing->paused, 0};
    m_packing.reset();
    if (!file || file->size() > kMaxSnapshotBytes) {
        for (const auto& p : m_peers) {
            if (std::exchange(p->packing, false)) {
                drop(*p, RefuseReason::Closed);
            }
        }
        return;
    }
    m_game = encode(Snapshot{0, packing.frame, packing.speed, packing.paused, std::move(file).value()});
    for (const auto& p : m_peers) {
        if (std::exchange(p->packing, false) && !p->gone) {
            send_game(*p, packing.frame);
        }
    }
    m_game.reset();
}

void HostSession::send_game(Peer& peer, std::uint64_t first) {
    // The epoch is the first field after the type (protocol.cpp), little endian.
    for (std::size_t i = 0; i < 4; ++i) {
        (*m_game)[1 + i] = static_cast<std::uint8_t>(peer.epoch >> (8 * i));
    }
    send_bytes(peer.link, *m_game);
    // The frames closed beyond the game sent: those the host ran while it was packed, and those
    // closed ahead; later ones follow with everyone's.
    Turn turn;
    turn.first = first;
    turn.closed = m_closed;
    turn.lead = m_lead;
    for (std::uint64_t f = first; f < m_closed; ++f) {
        const Frame& frame = m_log[static_cast<std::size_t>(f - m_log_start)];
        if (!frame.commands.empty()) {
            turn.frames.push_back(FrameCommands{f, frame.commands});
        }
    }
    send(peer.link, turn);
    peer.state = Peer::State::Loading;
    peer.loaded = 0;
    peer.start_idle_ms = 0;  // the game is on its way now
    peer.frames = first;
    peer.lagging = false;
    peer.hashes.clear();
    m_roster_changed = true;
}

void HostSession::forget_frames() {
    // Kept: what has not gone out yet, and what the host has not run (for a game sent now).
    std::uint64_t keep = std::min(m_frames, m_broadcast);
    if (m_packing) {
        keep = std::min(keep, m_packing->frame);  // for the game on its way
    }
    while (m_log_start < keep) {
        m_log.pop_front();
        ++m_log_start;
    }
}

void HostSession::update_lead(std::uint32_t real_ms) {
    std::uint32_t round_trip = 0;
    for (const auto& p : m_peers) {
        if (!p->gone && p->state != Peer::State::Greeting) {
            round_trip = std::max(round_trip, m_transport->round_trip_ms(p->link));
        }
    }
    const std::uint32_t speed = m_simulation->paused() ? 1 : std::max<std::uint32_t>(m_simulation->speed(), 1);
    // Frames that pass while a frame travels one way, and one more.
    const std::uint64_t one_way = (std::uint64_t{round_trip} / 2 + kJitterMarginMs) * speed;
    const std::uint64_t wanted = (one_way + sim::Simulation::kTickMs - 1) / sim::Simulation::kTickMs + 1;
    const auto lead =
        static_cast<std::uint16_t>(std::clamp<std::uint64_t>(wanted, m_config.min_lead, m_config.max_lead));
    if (lead >= m_lead) {
        m_lead = lead;
        m_lead_ms = 0;
    } else if (m_lead_ms += real_ms; m_lead_ms >= kLeadShrinkMs) {
        --m_lead;  // slowly: a short good spell must not empty the computers' buffers
        m_lead_ms = 0;
    }
}

std::span<const assets::ContentPackage> HostSession::packages() const noexcept {
    if (m_simulation != nullptr && m_simulation->world().packages()) {
        return *m_simulation->world().packages();
    }
    return m_packages;
}

std::uint64_t HostSession::share_bytes(std::span<const assets::ContentPackage> missing) const {
    std::uint64_t total = 0;
    for (const assets::ContentPackage& p : missing) {
        const auto shared =
            std::find_if(m_shared.begin(), m_shared.end(), [&](const SharedPackage& s) { return s.package == p; });
        if (shared == m_shared.end()) {
            return 0;
        }
        total += shared->bytes;
    }
    // Packages without files count 1 byte: 0 says the host does not send them.
    return total <= m_config.max_share_bytes ? std::max<std::uint64_t>(total, 1) : 0;
}

bool HostSession::send_packages(Peer& peer, std::span<const assets::ContentPackage> missing) {
    for (const assets::ContentPackage& p : missing) {
        const auto shared =
            std::find_if(m_shared.begin(), m_shared.end(), [&](const SharedPackage& s) { return s.package == p; });
        auto files = assets::read_package_files(shared->folder);
        if (!files || assets::package_hash(shared->folder) != p.hash) {
            return false;  // changed since the game started
        }
        // The files in messages of at most kMaxPackageFilesBytes, in their order.
        std::vector<PackageFiles> messages(1, PackageFiles{p, 0, 1, {}});
        std::size_t bytes = 0;
        for (assets::PackageFile& f : *files) {
            if (f.bytes.size() > kMaxPackageFilesBytes) {
                return false;
            }
            if ((bytes + f.bytes.size() > kMaxPackageFilesBytes || messages.back().files.size() == kMaxPackageFiles) &&
                !messages.back().files.empty()) {
                messages.push_back(PackageFiles{p, static_cast<std::uint32_t>(messages.size()), 1, {}});
                bytes = 0;
            }
            bytes += f.bytes.size();
            messages.back().files.push_back(std::move(f));
        }
        for (PackageFiles& message : messages) {
            message.count = static_cast<std::uint32_t>(messages.size());
            send(peer.link, message);
        }
    }
    return true;
}

void HostSession::drop(Peer& peer, std::optional<RefuseReason> tell, std::vector<assets::ContentPackage> packages,
                       std::uint64_t share_bytes) {
    if (peer.gone) {
        return;
    }
    peer.gone = true;
    peer.game_due = false;
    peer.packing = false;
    if (tell) {
        send(peer.link, Refused{*tell, std::move(packages), share_bytes});
    }
    m_transport->disconnect(peer.link);
    if (peer.welcomed) {
        if (peer.seat != sim::kNoPlayer) {
            occupy(m_lobby.seats[peer.seat], LobbySeat::Kind::Open);
            m_lobby_changed = true;
            // A slot the computer held before the player came: the computer goes on with it.
            // The host gives it in the player's stead (queue() takes orders only from those here).
            if (std::find(m_computer_held.begin(), m_computer_held.end(), peer.seat) != m_computer_held.end()) {
                if (started()) {
                    insert(sim::PlayerControl{peer.seat, true});
                }
            } else if (started()) {
                vacate(peer.seat);                          // a person's own slot: the computer after a while
                reserve(peer.seat, m_config.take_over_ms);  // and nobody new until then (MS-5)
            }
        }
        SessionEvent e;
        e.kind = SessionEvent::Kind::Left;
        e.seat = peer.seat;
        e.name = peer.name;
        e.frame = m_frames;
        event(std::move(e));
        m_roster_changed = true;
    }
}

void HostSession::vacate(Seat seat) {
    if (m_config.computer_takes_over &&
        std::none_of(m_vacant.begin(), m_vacant.end(), [seat](const auto& v) { return v.first == seat; })) {
        m_vacant.emplace_back(seat, 0);
    }
}

void HostSession::take_over_vacant(std::uint32_t real_ms) {
    // A person's slot that has stood empty long enough: the computer plays it on with what it
    // finds there (ai::Engine), until its player or someone else comes. The host gives the order
    // like the speed, so every computer runs it in the same frame.
    std::erase_if(m_vacant, [&](auto& v) {
        if (seat_taken(v.first)) {
            return true;
        }
        v.second = std::min(v.second + real_ms, m_config.take_over_ms);
        if (v.second < m_config.take_over_ms) {
            return false;
        }
        const sim::Player* p = m_simulation->world().player(v.first);
        if (p != nullptr && p->active() && p->role == sim::PlayerRole::Human) {
            insert(sim::PlayerControl{v.first, true});
        }
        return true;
    });
}

bool HostSession::seat_taken(Seat seat) const {
    return seat == m_seat || std::any_of(m_peers.begin(), m_peers.end(),
                                         [seat](const auto& p) { return !p->gone && p->welcomed && p->seat == seat; });
}

Seat HostSession::free_seat(Seat wanted, std::uint64_t owner) const {
    const auto free = [&](std::size_t s) {
        if (s >= m_lobby.seats.size() || seat_taken(static_cast<Seat>(s))) {
            return false;
        }
        if (reserved(static_cast<Seat>(s))) {
            return false;  // its player may come back with the token (MS-5)
        }
        if (!started()) {
            return m_lobby.seats[s].kind == LobbySeat::Kind::Open;
        }
        // A slot nobody plays: open in the lobby, still in the game, and steered by a person who
        // left or by the computer that holds it for whoever comes.
        const auto& players = m_simulation->world().players();
        return m_lobby.seats[s].kind == LobbySeat::Kind::Open && s < players.size() && players[s].active() &&
               (players[s].role == sim::PlayerRole::Human || players[s].role == sim::PlayerRole::Computer);
    };
    if (free(wanted)) {
        return wanted;
    }
    // Slots nobody owns (or this person) first: another person's slot waits for its player.
    for (std::size_t s = 0; s < m_lobby.seats.size(); ++s) {
        const std::uint64_t o = owner_of(static_cast<Seat>(s));
        if ((o == 0 || o == owner) && free(s)) {
            return static_cast<Seat>(s);
        }
    }
    for (std::size_t s = 0; s < m_lobby.seats.size(); ++s) {
        if (free(s)) {
            return static_cast<Seat>(s);
        }
    }
    return sim::kNoPlayer;
}

std::uint64_t HostSession::owner_of(Seat seat) const {
    if (!started()) {
        return seat < m_lobby.seats.size() ? m_lobby.seats[seat].owner : 0;
    }
    const auto& players = m_simulation->world().players();
    return seat < players.size() ? players[seat].owner : 0;
}

Seat HostSession::seat_of_owner(std::uint64_t owner) const {
    if (owner == 0) {
        return sim::kNoPlayer;
    }
    for (std::size_t s = 0; s < m_lobby.seats.size(); ++s) {
        const auto seat = static_cast<Seat>(s);
        if (owner_of(seat) != owner || seat == m_seat || seat_taken(seat)) {
            continue;
        }
        // Free as for anyone, except that a slot kept for its player (reserved) is this one's.
        if (!started()) {
            if (m_lobby.seats[s].kind == LobbySeat::Kind::Open) {
                return seat;
            }
            continue;
        }
        const auto& players = m_simulation->world().players();
        if (m_lobby.seats[s].kind == LobbySeat::Kind::Open && s < players.size() && players[s].active() &&
            (players[s].role == sim::PlayerRole::Human || players[s].role == sim::PlayerRole::Computer)) {
            return seat;
        }
    }
    return sim::kNoPlayer;
}

void HostSession::refresh_roster() {
    m_roster_changed = false;
    m_loading_changed = false;
    m_roster_wait_ms = kLoadingEveryMs;
    m_roster.clear();
    m_roster_links.clear();
    m_roster.push_back(Member{m_seat, m_name, Member::State::Playing, true, 100});
    m_roster_links.push_back(LinkId{});  // the host itself: never sent away
    for (const auto& p : m_peers) {
        if (p->gone || !p->welcomed) {
            continue;
        }
        const Member::State state = p->lagging                                        ? Member::State::Lagging
                                    : p->state == Peer::State::Playing && !p->packing ? Member::State::Playing
                                                                                      : Member::State::Loading;
        const std::uint8_t loaded = state == Member::State::Loading ? p->loaded : std::uint8_t{100};
        m_roster.push_back(Member{p->seat, p->name, state, false, loaded});
        m_roster_links.push_back(p->link);
    }
    // The successor: of the computers that play a slot and run the game, the one with the lowest
    // slot, so that it does not change while the same computers play.
    m_successor = sim::kNoPlayer;
    m_successor_address.clear();
    for (const auto& p : m_peers) {
        if (!p->gone && p->welcomed && p->seat != sim::kNoPlayer && p->seat < m_successor &&
            p->state == Peer::State::Playing && !p->lagging && !p->packing && !p->address.empty()) {
            m_successor = p->seat;
            m_successor_address = p->address;
        }
    }
    m_standby_changed = true;
    // The successor's address only to those who play: spectators get it when the host leaves (MS-25).
    const std::vector<std::uint8_t> players = encode(Roster{m_roster, m_successor, m_successor_address, m_starting});
    const std::vector<std::uint8_t> spectators = encode(Roster{m_roster, m_successor, {}, m_starting});
    for (const auto& p : m_peers) {
        if (!p->gone && p->welcomed) {
            m_transport->send(p->link, p->seat != sim::kNoPlayer ? players : spectators);
        }
    }
}

void HostSession::hand_over() {
    if (!m_standby_changed) {
        return;
    }
    m_standby_changed = false;
    const auto successor = std::find_if(m_peers.begin(), m_peers.end(), [&](const auto& p) {
        return !p->gone && p->welcomed && p->seat != sim::kNoPlayer && p->seat == m_successor;
    });
    if (successor == m_peers.end()) {
        m_standby_link.reset();
        return;
    }
    // Verifiers, not the tokens: the successor must not be able to take others' slots on this
    // host while it still runs (MS-1). Slots whose players have not come back since a migration
    // pass on the earlier host's verifiers as they are.
    Standby standby;
    for (const auto& [seat, token] : m_tokens) {
        standby.tokens.push_back(SeatToken{seat, m_salt, token_verifier(token, m_salt)});
    }
    standby.tokens.insert(standby.tokens.end(), m_verifiers.begin(), m_verifiers.end());
    standby.computer_held = m_computer_held;
    if (m_standby_link == (*successor)->link && standby == m_standby) {
        return;
    }
    send((*successor)->link, standby);
    m_standby_link = (*successor)->link;
    m_standby = std::move(standby);
}

void HostSession::broadcast(const Message& message) {
    const std::vector<std::uint8_t> bytes = encode(message);
    for (const auto& p : m_peers) {
        if (!p->gone && p->welcomed) {
            send_bytes(p->link, bytes);
        }
    }
}

std::uint64_t HostSession::token_of(Seat seat) {
    const auto it = std::find_if(m_tokens.begin(), m_tokens.end(), [&](const auto& t) { return t.first == seat; });
    if (it != m_tokens.end()) {
        return it->second;
    }
    // Drawn at random, so that one token tells nothing about the others.
    std::uint64_t token = random_token();
    while (std::any_of(m_tokens.begin(), m_tokens.end(), [&](const auto& t) { return t.second == token; })) {
        token = random_token();
    }
    m_tokens.emplace_back(seat, token);
    m_standby_changed = true;
    return token;
}

void HostSession::forget_token(Seat seat) {
    std::erase_if(m_reserved, [seat](const auto& r) { return r.first == seat; });
    m_standby_changed |= std::erase_if(m_tokens, [&](const auto& t) { return t.first == seat; }) > 0;
    m_standby_changed |= std::erase_if(m_verifiers, [&](const SeatToken& t) { return t.seat == seat; }) > 0;
}

void HostSession::reserve(Seat seat, std::uint32_t ms) {
    std::erase_if(m_reserved, [seat](const auto& r) { return r.first == seat; });
    m_reserved.emplace_back(seat, ms);
}

bool HostSession::reserved(Seat seat) const {
    // Only while someone can come back: the slot has a token (or a verifier from the old host).
    return std::any_of(m_reserved.begin(), m_reserved.end(), [seat](const auto& r) { return r.first == seat; }) &&
           (std::any_of(m_tokens.begin(), m_tokens.end(), [seat](const auto& t) { return t.first == seat; }) ||
            std::any_of(m_verifiers.begin(), m_verifiers.end(), [seat](const SeatToken& t) { return t.seat == seat; }));
}

Seat HostSession::seat_of_token(const Hello& m) {
    if (m.token == 0) {
        return sim::kNoPlayer;
    }
    // In the lobby a slot is only kept while the computer is in.
    // Never the host's own slot (it has no token, but one handed out before the host took it).
    const auto good = [&](Seat s) {
        return s < m_lobby.seats.size() && s != m_seat && (started() || free_seat(s) == s);
    };
    const auto it = std::find_if(m_tokens.begin(), m_tokens.end(), [&](const auto& t) { return t.second == m.token; });
    if (it != m_tokens.end()) {
        return good(it->first) ? it->first : sim::kNoPlayer;
    }
    // The tokens of the old host count only for those that follow it here (returning).
    if (m.returning) {
        const auto found = std::find_if(m_verifiers.begin(), m_verifiers.end(), [&](const SeatToken& t) {
            return t.verifier == token_verifier(m.token, t.salt);
        });
        if (found != m_verifiers.end() && good(found->seat)) {
            return found->seat;
        }
    }
    return sim::kNoPlayer;
}

// --- The lobby ---

void HostSession::move(Peer* peer, Seat seat) {
    Seat& current = peer != nullptr ? peer->seat : m_seat;
    if (current != sim::kNoPlayer) {
        occupy(m_lobby.seats[current], LobbySeat::Kind::Open);
        // Whoever takes the slot later gets a new token, so that nobody collects the tokens of
        // the slots he passes through.
        forget_token(current);
    }
    current = seat;
    if (seat != sim::kNoPlayer) {
        const std::string& name = peer != nullptr ? peer->name : m_name;
        occupy(m_lobby.seats[seat], LobbySeat::Kind::Human, name, peer == nullptr);
    }
    if (peer != nullptr) {
        peer->token = seat != sim::kNoPlayer ? token_of(seat) : 0;
        send(peer->link, Welcome{kProtocolVersion, seat, peer->token});
    }
    m_lobby_changed = true;
    m_roster_changed = true;
}

void HostSession::take_seat(Peer& peer, Seat seat) {
    if (started() || seat == peer.seat) {
        return;
    }
    if (seat == sim::kNoPlayer ? m_config.spectators : free_seat(seat) == seat) {
        move(&peer, seat);
    }
}

bool HostSession::take_seat(Seat seat) {
    if (started() || seat == m_seat) {
        return !started();
    }
    if (seat != sim::kNoPlayer && free_seat(seat) != seat) {
        return false;
    }
    move(nullptr, seat);
    return true;
}

void HostSession::ready(Peer& peer, bool ready) {
    if (started() || peer.seat == sim::kNoPlayer || m_lobby.seats[peer.seat].ready == ready) {
        return;
    }
    m_lobby.seats[peer.seat].ready = ready;
    m_lobby_changed = true;
}

void HostSession::chat(Peer& peer, const Chat& m) {
    if (m.text.empty() || peer.chat_lines >= m_config.max_chat_per_second) {
        return;
    }
    ++peer.chat_lines;
    deliver(Chat{peer.seat, peer.name, printable(m.text), m.everyone, m.to, {}}, &peer);
}

void HostSession::say(std::string text, std::vector<Seat> to) {
    text.resize(std::min(text.size(), kMaxChatLength));
    if (text.empty()) {
        return;
    }
    const bool everyone = to.empty();
    deliver(Chat{m_seat, m_name, std::move(text), everyone, std::move(to), {}}, nullptr);
}

void HostSession::deliver(Chat line, Peer* from) {
    const bool watching = line.seat == sim::kNoPlayer;
    if (line.everyone) {
        if (watching && from != nullptr && !m_config.spectators_chat) {
            return;
        }
        line.to.clear();
        line.missed.clear();
        broadcast(line);
        chat_event(line);
        return;
    }
    if (watching) {
        return;  // spectators write only to everyone
    }
    std::vector<Seat> wanted = std::move(line.to);
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    line.to.clear();
    line.missed.clear();
    for (const Seat s : wanted) {
        if (s != line.seat && s != sim::kNoPlayer) {
            (played_by_person(s) ? line.to : line.missed).push_back(s);
        }
    }
    if (line.to.empty() && line.missed.empty()) {
        return;
    }
    // Only the computers of the players it is for get it; they do not learn whom it missed.
    Chat shown = line;
    shown.missed.clear();
    const auto is_for = [&](Seat s) { return std::binary_search(line.to.begin(), line.to.end(), s); };
    if (!line.to.empty()) {
        const std::vector<std::uint8_t> bytes = encode(shown);
        for (const auto& p : m_peers) {
            if (!p->gone && p->welcomed && p.get() != from && p->seat != sim::kNoPlayer && is_for(p->seat)) {
                send_bytes(p->link, bytes);
            }
        }
        if (m_seat != sim::kNoPlayer && is_for(m_seat)) {
            chat_event(shown);
        }
    }
    // Back to who said it, with the players it missed.
    if (from != nullptr) {
        send(from->link, line);
    } else {
        chat_event(line);
    }
}

bool HostSession::played_by_person(Seat seat) const {
    if (seat == sim::kNoPlayer) {
        return false;
    }
    return seat == m_seat || std::any_of(m_peers.begin(), m_peers.end(),
                                         [&](const auto& p) { return !p->gone && p->welcomed && p->seat == seat; });
}

void HostSession::set_lobby_info(std::string title, std::string game, std::vector<std::uint8_t> setup) {
    m_lobby.title = std::move(title);
    m_lobby.game = std::move(game);
    m_lobby.setup = std::move(setup);
    m_lobby.title.resize(std::min(m_lobby.title.size(), kMaxTitleLength));
    m_lobby.game.resize(std::min(m_lobby.game.size(), kMaxTitleLength));
    if (m_lobby.setup.size() > kMaxSetupBytes) {
        m_lobby.setup.clear();
    }
    m_lobby_changed = true;
}

void HostSession::set_seat(Seat seat, LobbySeat::Kind kind) {
    if (started() || seat >= m_lobby.seats.size() || seat == m_seat || kind == LobbySeat::Kind::Human ||
        kind == LobbySeat::Kind::Reserved || m_lobby.seats[seat].kind == LobbySeat::Kind::Reserved) {
        return;
    }
    const auto holder =
        std::find_if(m_peers.begin(), m_peers.end(), [&](const auto& p) { return !p->gone && p->seat == seat; });
    if (holder != m_peers.end()) {
        if (m_config.spectators) {
            move(holder->get(), sim::kNoPlayer);
        } else {
            drop(**holder, RefuseReason::Full);
        }
    }
    occupy(m_lobby.seats[seat], kind);
    m_lobby_changed = true;
}

bool HostSession::set_banner(Seat seat, sim::BannerId banner) {
    // The host speaks for itself and for the slots no other person plays.
    if (seat >= m_lobby.seats.size() || (seat != m_seat && m_lobby.seats[seat].kind == LobbySeat::Kind::Human)) {
        return false;
    }
    return change_banner(seat, banner);
}

void HostSession::set_banner(Peer& peer, sim::BannerId banner) {
    if (peer.seat != sim::kNoPlayer) {
        (void)change_banner(peer.seat, banner);
    }
}

bool HostSession::change_banner(Seat seat, sim::BannerId banner) {
    if (started() || seat >= m_lobby.seats.size() || m_lobby.seats[seat].kind == LobbySeat::Kind::Reserved ||
        std::find(m_lobby.banners.begin(), m_lobby.banners.end(), banner) == m_lobby.banners.end()) {
        return false;
    }
    LobbySeat& mine = m_lobby.seats[seat];
    if (mine.banner == banner) {
        return true;
    }
    const auto other = std::find_if(m_lobby.seats.begin(), m_lobby.seats.end(), [&](const LobbySeat& s) {
        return &s != &mine && s.kind != LobbySeat::Kind::Reserved && s.banner == banner;
    });
    if (other != m_lobby.seats.end()) {
        if (other->kind == LobbySeat::Kind::Human) {
            return false;  // another person's: first come, first served
        }
        other->banner = mine.banner;  // the computer's or nobody's: they swap
    }
    mine.banner = banner;
    m_lobby_changed = true;
    return true;
}

void HostSession::sort_out_banners() {
    if (m_lobby.banners.empty()) {
        return;
    }
    std::vector<sim::BannerId> taken;
    std::vector<LobbySeat*> unsorted;
    for (LobbySeat& s : m_lobby.seats) {
        if (s.kind == LobbySeat::Kind::Reserved) {
            continue;
        }
        const bool offered =
            std::find(m_lobby.banners.begin(), m_lobby.banners.end(), s.banner) != m_lobby.banners.end();
        if (offered && std::find(taken.begin(), taken.end(), s.banner) == taken.end()) {
            taken.push_back(s.banner);
        } else {
            unsorted.push_back(&s);
        }
    }
    // More slots than banners: the last ones share (the lobby cannot tell them apart anyway).
    std::size_t next = 0;
    for (LobbySeat* s : unsorted) {
        while (next < m_lobby.banners.size() &&
               std::find(taken.begin(), taken.end(), m_lobby.banners[next]) != taken.end()) {
            ++next;
        }
        s->banner = next < m_lobby.banners.size() ? m_lobby.banners[next] : m_lobby.banners.back();
        taken.push_back(s->banner);
    }
}

void HostSession::kick(Seat seat) {
    const auto holder = std::find_if(m_peers.begin(), m_peers.end(),
                                     [&](const auto& p) { return !p->gone && p->welcomed && p->seat == seat; });
    if (seat != sim::kNoPlayer && holder != m_peers.end()) {
        ban((*holder)->address);
        ban_owner((*holder)->owner);
        drop(**holder, RefuseReason::Kicked);
        forget_token(seat);  // and it cannot come back with it
    }
}

void HostSession::kick_member(std::size_t member, Seat seat, std::string_view name) {
    if (member == 0 || member >= m_roster_links.size() || member >= m_roster.size() || m_roster[member].seat != seat ||
        m_roster[member].name != name) {
        return;  // someone else by now: the roster changed since it was shown (MS-18)
    }
    Peer* peer = peer_of(m_roster_links[member]);
    if (peer == nullptr || peer->gone || !peer->welcomed || peer->seat != seat) {
        return;
    }
    ban(peer->address);
    ban_owner(peer->owner);
    drop(*peer, RefuseReason::Kicked);
    if (seat != sim::kNoPlayer) {
        forget_token(seat);  // and it cannot come back with it
    }
}

void HostSession::ban(const std::string& address) {
    if (!address.empty() && std::find(m_banned.begin(), m_banned.end(), address) == m_banned.end()) {
        m_banned.push_back(address);
    }
}

void HostSession::ban_owner(std::uint64_t owner) {
    if (owner != 0 && std::find(m_kicked.begin(), m_kicked.end(), owner) == m_kicked.end()) {
        m_kicked.push_back(owner);
    }
}

bool HostSession::everyone_ready() const {
    return std::all_of(m_peers.begin(), m_peers.end(), [&](const auto& p) {
        return p->gone || !p->welcomed || p->seat == sim::kNoPlayer || m_lobby.seats[p->seat].ready;
    });
}

void HostSession::start(sim::Simulation& simulation, const assets::BuildingTable& buildings) {
    if (started()) {
        return;
    }
    m_simulation = &simulation;
    m_buildings = &buildings;
    m_lobby.started = true;
    m_lobby_changed = true;
    m_standby_changed = true;
    const auto& players = simulation.world().players();
    for (std::size_t s = 0; s < m_lobby.seats.size() && s < players.size(); ++s) {
        if (m_lobby.seats[s].kind == LobbySeat::Kind::Open && players[s].role == sim::PlayerRole::Computer) {
            m_computer_held.push_back(static_cast<Seat>(s));
        }
    }
    // All begin together: the first frame waits for the players in the lobby to have the game.
    m_starting = m_config.start_wait_ms > 0;
    m_roster_changed = true;
    for (const auto& p : m_peers) {
        if (p->gone || !p->welcomed) {
            continue;
        }
        p->game_due = true;
        p->start_wait = m_starting && p->seat != sim::kNoPlayer;
        p->start_idle_ms = 0;
        if (p->seat != sim::kNoPlayer) {
            insert(sim::PlayerRename{p->seat, p->name});
            if (p->owner != 0) {
                insert(sim::PlayerControl{p->seat, false, p->owner});  // the game keeps who plays it
            }
        }
    }
    if (const std::uint64_t own = player_verifier(m_config.player); own != 0 && m_seat != sim::kNoPlayer) {
        insert(sim::PlayerControl{m_seat, false, own});
    }
}

}  // namespace o1602::net
