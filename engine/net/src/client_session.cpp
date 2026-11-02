// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <exception>
#include <iterator>
#include <utility>

#include "o1602/net/session.hpp"
#include "o1602/sim/command_io.hpp"

namespace o1602::net {

namespace {

/// Following a successor: the wait before connecting again after an attempt failed.
constexpr std::uint32_t kRetryMs = 500;

}  // namespace

ClientSession::ClientSession(std::unique_ptr<Transport> transport, Hello hello, Loader load, SessionConfig config)
    : Session(std::move(transport), std::move(config)), m_hello(std::move(hello)), m_load(std::move(load)) {
    m_hello.protocol = kProtocolVersion;
    m_hello.name.resize(std::min(m_hello.name.size(), kMaxNameLength));
    m_hello.password.resize(std::min(m_hello.password.size(), kMaxPasswordLength));
}

ClientSession::ClientSession(const ClientSession& old, Connect connect)
    : Session(connect(), old.m_config),
      m_hello(old.m_hello),
      m_load(old.m_load),
      m_prepare(old.m_prepare),
      m_keep_package(old.m_keep_package),
      m_connect(connect),
      m_reconnect(std::move(connect)) {
    m_hello.seat = old.m_seat;
    m_hello.token = old.m_token;
    m_hello.returning = true;
}

bool ClientSession::hosts_next() const noexcept {
    return m_ended && m_end_reason == EndReason::HostLeft && (m_host_leaving || m_host_gone) &&
           m_simulation != nullptr && m_lobby.started && m_seat != sim::kNoPlayer && m_successor == m_seat &&
           m_standby.has_value();
}

bool ClientSession::can_migrate() const noexcept {
    if (!m_ended || m_end_reason != EndReason::HostLeft || !(m_host_leaving || m_host_gone) ||
        m_simulation == nullptr || !m_lobby.started || m_successor == sim::kNoPlayer) {
        return false;
    }
    return m_successor == m_seat ? hosts_next() : !m_successor_address.empty();
}

std::string ClientSession::successor_name() const {
    const auto it = std::find_if(m_roster.begin(), m_roster.end(),
                                 [&](const Member& m) { return !m.host && m.seat == m_successor; });
    return it != m_roster.end() ? it->name : std::string{};
}

void ClientSession::say(std::string text, std::vector<Seat> to) {
    text.resize(std::min(text.size(), kMaxChatLength));
    if (!m_welcomed || m_ended || text.empty()) {
        return;
    }
    if (m_seat == sim::kNoPlayer && (!to.empty() || !m_lobby.spectators_chat)) {
        return;  // the host would not pass it on
    }
    const bool everyone = to.empty();
    send(kHost, Chat{m_seat, m_hello.name, std::move(text), everyone, std::move(to), {}});
}

void ClientSession::take_seat(Seat seat) {
    if (m_welcomed && !m_ended && !m_lobby.started) {
        send(kHost, TakeSeat{seat});
    }
}

void ClientSession::set_ready(bool ready) {
    if (m_welcomed && !m_ended && !m_lobby.started) {
        send(kHost, SetReady{ready});
    }
}

void ClientSession::set_banner(sim::BannerId banner) {
    if (m_welcomed && !m_ended && !m_lobby.started && m_seat != sim::kNoPlayer) {
        send(kHost, SetBanner{banner});
    }
}

void ClientSession::submit(sim::Command command) {
    // Only the host decides when a command is carried out, also those of this computer's player.
    if (m_ended || m_simulation == nullptr || m_seat == sim::kNoPlayer || command_player(command) != m_seat) {
        return;
    }
    if (!sim::command_size(command)) {
        // The others could not read it: the host would drop it (MS-10).
        SessionEvent e;
        e.kind = SessionEvent::Kind::Dropped;
        e.seat = m_seat;
        e.count = 1;
        event(std::move(e));
        return;
    }
    if (std::holds_alternative<sim::GameSpeedOrder>(command) && !may_change_speed()) {
        // The host would not carry it out (MS-20); the player is told why nothing happens.
        SessionEvent e;
        e.kind = SessionEvent::Kind::SpeedRefused;
        e.seat = m_seat;
        event(std::move(e));
        return;
    }
    m_outgoing.push_back(std::move(command));
}

void ClientSession::update(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick) {
    if (m_ended) {
        return;
    }
    if (m_connect && !m_welcomed) {
        // Following a successor that may not host yet: connect again until it takes this computer in.
        m_migration_ms += real_ms;
        if (m_migration_ms >= (m_reconnecting ? m_config.reconnect_ms : m_config.migration_ms)) {
            m_transport.reset();
            if (m_reconnecting) {
                // The host cannot be reached again: gone, as if it had left (it may still run, but
                // nobody can tell from here). Now the successor may carry the game on.
                m_reconnecting = false;
                m_host_gone = true;
                end(EndReason::HostLeft);
            } else {
                end(EndReason::ConnectFailed);
            }
            return;
        }
        if (!m_transport) {
            m_retry_ms += real_ms;
            if (m_retry_ms >= kRetryMs) {
                m_retry_ms = 0;
                m_transport = m_connect();
            }
        }
    }
    if (!m_transport) {
        return;
    }
    if (m_pending && !finish_pending()) {
        return;
    }
    if (!m_whole.empty() && !m_ended) {
        const std::vector<std::uint8_t> whole = std::exchange(m_whole, {});
        receive(whole);
    }
    // The events held back behind a whole message (or a game being prepared) first, then those
    // that came since.
    for (bool polled = false;;) {
        while (m_next < m_incoming.size() && m_whole.empty() && !m_pending && !m_ended && m_transport) {
            handle(m_incoming[m_next++]);
        }
        if (polled || m_next < m_incoming.size() || !m_whole.empty() || m_pending || m_ended || !m_transport) {
            break;
        }
        m_incoming.clear();
        m_next = 0;
        m_transport->poll(m_incoming);
        polled = true;
    }
    if (!m_transport || m_ended) {
        // Whatever was held belongs to a connection that is over.
        m_incoming.clear();
        m_next = 0;
        m_whole.clear();
    }
    if (!m_transport) {
        return;
    }
    if (m_ended) {
        m_transport->disconnect(kHost);
        m_transport->flush();
        return;
    }
    if (m_pending) {
        report_loading(real_ms);
        m_transport->flush();
        return;  // the game comes with a later update
    }
    if (m_simulation != nullptr) {
        run(real_ms, before_tick);
    }
    send_commands();
    report(real_ms);
    report_loading(real_ms);
    m_transport->flush();
}

bool ClientSession::finish_pending() {
    if (m_pending->finish.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        // Heard, so that the host's messages are acknowledged and the link does not time out;
        // they are read once the game is there.
        m_transport->poll(m_incoming);
        report_loading(0);
        m_transport->flush();
        return false;
    }
    Finish finish;
    try {
        finish = m_pending->finish.get();
    } catch (const std::exception&) {
        finish = nullptr;  // as a game that cannot be read
    }
    const Snapshot head = std::move(m_pending->head);
    m_pending.reset();
    loaded(finish ? finish() : nullptr, head);
    return !m_ended;
}

void ClientSession::handle(TransportEvent& e) {
    switch (e.kind) {
        case TransportEvent::Kind::Connected:
            m_connected = true;
            send(kHost, m_hello);
            break;
        case TransportEvent::Kind::Disconnected:
            if (m_connect && !m_welcomed) {
                m_connected = false;
                m_transport.reset();  // and again in a moment
                break;
            }
            if (m_welcomed && !m_host_leaving && m_reconnect && m_simulation != nullptr && m_lobby.started) {
                reconnect();  // only this computer's connection may have broken (MS-2)
                break;
            }
            end(m_welcomed ? EndReason::HostLeft : EndReason::ConnectFailed);
            break;
        case TransportEvent::Kind::Received:
            receive(e.data);
            break;
    }
}

ClientSession::JoinStage ClientSession::join_stage() const noexcept {
    if (!m_welcomed || m_ended || !m_lobby.started || m_simulation != nullptr) {
        return JoinStage::None;
    }
    if (!m_whole.empty() || m_pending) {
        return JoinStage::Loading;
    }
    return m_part.empty() ? JoinStage::Waiting : JoinStage::Receiving;
}

void ClientSession::receive(std::span<const std::uint8_t> bytes) {
    auto message = decode(bytes);
    if (!message) {
        end(EndReason::ProtocolError);
        return;
    }
    if (auto* part = std::get_if<Part>(&*message)) {
        // A large message in pieces, one after another (MS-7): read once it is whole.
        if ((!m_part.empty() && part->total != m_part_total) || part->total <= kMaxPacketBytes ||
            m_part.size() + part->bytes.size() > part->total) {
            end(EndReason::ProtocolError);
            return;
        }
        m_part_total = part->total;
        m_part.insert(m_part.end(), part->bytes.begin(), part->bytes.end());
        if (m_part.size() == m_part_total) {
            std::vector<std::uint8_t> whole = std::move(m_part);
            m_part.clear();
            if (whole[0] == static_cast<std::uint8_t>(Message(Part{}).index())) {
                end(EndReason::ProtocolError);  // parts of parts
                return;
            }
            m_whole = std::move(whole);  // read with the next update()
        }
        return;
    }
    if (!m_part.empty()) {
        end(EndReason::ProtocolError);  // another message between the pieces of one
        return;
    }
    if (auto* files = std::get_if<PackageFiles>(&*message)) {
        package_files(*files);
    } else if (m_package) {
        end(EndReason::ProtocolError);  // another message between the files of a package
    } else if (const auto* welcome = std::get_if<Welcome>(&*message)) {
        // Again in the lobby when this computer moves to another slot.
        if ((m_welcomed && m_lobby.started) || welcome->protocol != kProtocolVersion) {
            end(EndReason::ProtocolError);
            return;
        }
        m_welcomed = true;
        m_reconnecting = false;
        m_seat = welcome->seat;
        m_token = welcome->token;
        SessionEvent e;
        e.kind = SessionEvent::Kind::Welcomed;
        e.seat = m_seat;
        event(std::move(e));
    } else if (const auto* refused = std::get_if<Refused>(&*message);
               refused != nullptr && m_reconnecting && refused->reason == RefuseReason::Taken) {
        // The host has not noticed yet that the old connection broke: again in a moment.
        m_connected = false;
        m_transport.reset();
    } else if (refused != nullptr) {
        SessionEvent e;
        e.kind = SessionEvent::Kind::Ended;
        e.reason = EndReason::Refused;
        e.refused = refused->reason;
        e.packages = refused->packages;
        e.share_bytes = refused->share_bytes;
        m_ended = true;
        m_end_reason = refused->reason == RefuseReason::Desync ? EndReason::Desync : EndReason::Refused;
        e.reason = m_end_reason;
        event(std::move(e));
    } else if (!m_welcomed) {
        end(EndReason::ProtocolError);
    } else if (auto* snapshot = std::get_if<Snapshot>(&*message)) {
        load(*snapshot);
    } else if (auto* frames = std::get_if<Turn>(&*message)) {
        turn(*frames);
    } else if (auto* roster = std::get_if<Roster>(&*message)) {
        m_roster = std::move(roster->members);
        m_successor = roster->successor;
        m_successor_address = std::move(roster->successor_address);
        if (m_successor != m_seat) {
            m_standby.reset();
        }
        if (std::exchange(m_starting, roster->starting) && !m_starting) {
            SessionEvent e;
            e.kind = SessionEvent::Kind::Started;
            e.seat = m_seat;
            event(std::move(e));
        }
    } else if (auto* standby = std::get_if<Standby>(&*message)) {
        m_standby = std::move(*standby);
    } else if (auto* lobby = std::get_if<Lobby>(&*message)) {
        m_lobby = std::move(*lobby);
        lobby_event();
    } else if (const auto* line = std::get_if<Chat>(&*message)) {
        chat_event(*line);
    } else if (const auto* desync = std::get_if<Desync>(&*message)) {
        for (const Seat s : desync->seats) {
            SessionEvent e;
            e.kind = SessionEvent::Kind::Desync;
            e.seat = s;
            e.frame = desync->frame;
            event(std::move(e));
        }
    } else if (auto* leaving = std::get_if<HostLeaving>(&*message)) {
        m_host_leaving = true;
        if (!leaving->successor_address.empty()) {
            m_successor_address = std::move(leaving->successor_address);
        }
    } else if (const auto* dropped = std::get_if<Dropped>(&*message)) {
        SessionEvent e;
        e.kind = SessionEvent::Kind::Dropped;
        e.seat = m_seat;
        e.count = dropped->commands;
        event(std::move(e));
    } else {
        end(EndReason::ProtocolError);  // a message only computers send
    }
}

void ClientSession::package_files(PackageFiles& m) {
    if (!m_hello.fetch_packages || !m_keep_package || m_welcomed) {
        end(EndReason::ProtocolError);  // only before it is taken in, when it asked for them
        return;
    }
    if (!m_package) {
        if (m.index != 0) {
            end(EndReason::ProtocolError);
            return;
        }
        m_package = std::move(m);
    } else if (m.package != m_package->package || m.count != m_package->count || m.index != m_package->index + 1) {
        end(EndReason::ProtocolError);
        return;
    } else {
        m_package->index = m.index;
        std::move(m.files.begin(), m.files.end(), std::back_inserter(m_package->files));
    }
    if (m_package->index + 1 < m_package->count) {
        return;
    }
    const PackageFiles whole = std::move(*m_package);
    m_package.reset();
    if (!m_keep_package(whole.package, whole.files)) {
        end(EndReason::LoadFailed);
        return;
    }
    SessionEvent e;
    e.kind = SessionEvent::Kind::PackageKept;
    e.packages.push_back(whole.package);
    event(std::move(e));
}

void ClientSession::load(Snapshot& m) {
    if (m_prepare) {
        // The long part on a worker; update() keeps the connection and finishes it (finish_pending).
        Snapshot head{m.epoch, m.frame, m.speed, m.paused, {}};
        m_pending = Pending{
            std::async(std::launch::async,
                       [prepare = m_prepare, save = std::move(m.save)]() mutable { return prepare(std::move(save)); }),
            std::move(head)};
        return;
    }
    loaded(m_load ? m_load(m.save) : nullptr, m);
}

void ClientSession::loaded(sim::Simulation* simulation, const Snapshot& m) {
    if (simulation == nullptr) {
        m_simulation = nullptr;
        end(EndReason::LoadFailed);
        return;
    }
    m_simulation = simulation;
    // The savegame carries speed and pause too; the snapshot's values win (older saves lack them).
    m_simulation->set_speed(std::max<std::uint32_t>(m.speed, 1));
    if (m.paused != m_simulation->paused()) {
        m_simulation->execute(sim::GameSpeedOrder{
            sim::kNoPlayer, m.paused ? sim::GameSpeedOrder::Op::Pause : sim::GameSpeedOrder::Op::Resume, m.speed});
    }
    m_epoch = m.epoch;
    m_frames = m.frame;
    m_closed = m.frame;
    m_frames_ahead.clear();
    m_pending_ms = 0;
    m_outgoing.clear();  // given in the old game: they would act on the new one
    m_send_commands = 0;
    m_send_owed = 0;
    SessionEvent e;
    e.kind = SessionEvent::Kind::Loaded;
    e.seat = m_seat;
    e.frame = m_frames;
    event(std::move(e));
    // At once: the host takes this computer as playing from now on.
    send(kHost, Progress{m_epoch, m_frames, std::nullopt});
    m_reported = m_frames;
    m_since_report = 0;
    m_load_reported = kNoLoadReport;  // the next game on its way starts from nothing
}

void ClientSession::turn(Turn& m) {
    if (m_simulation == nullptr) {
        return;  // frames of a game this computer does not have (yet)
    }
    if (m.first > m_closed || m.closed - m_frames > (std::uint64_t{1} << 20)) {
        end(EndReason::ProtocolError);  // a gap, or more frames than any game runs ahead
        return;
    }
    m_lead = m.lead;
    auto listed = m.frames.begin();
    for (std::uint64_t f = m.first; f < m.closed; ++f) {
        const bool has = listed != m.frames.end() && listed->frame == f;
        if (f >= m_closed) {
            m_frames_ahead.emplace_back();
            if (has) {
                m_frames_ahead.back() = std::move(listed->commands);
            }
        }
        if (has) {
            ++listed;
        }
    }
    m_closed = std::max(m_closed, m.closed);
}

void ClientSession::run(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick) {
    std::uint32_t due = frames_due(real_ms, *m_simulation);
    // More in hand than the host keeps ahead: this computer is behind and catches up.
    const std::uint64_t in_hand = m_closed - m_frames;
    if (in_hand > std::uint64_t{m_lead} + 2) {
        due = std::max<std::uint32_t>(due, static_cast<std::uint32_t>(std::min<std::uint64_t>(
                                               in_hand - m_lead, sim::Simulation::kMaxTicksPerAdvance)));
    }
    std::uint32_t done = 0;
    while (done < due && m_frames < m_closed) {
        if (before_tick && !before_tick()) {
            break;
        }
        const std::vector<sim::Command> commands = std::move(m_frames_ahead.front());
        m_frames_ahead.pop_front();
        if (const auto hash = run_frame(*m_simulation, commands)) {
            send(kHost, Progress{m_epoch, m_frames, hash});
            m_reported = m_frames;
            m_since_report = 0;
        }
        ++done;
        // A frame gone by: the host closed one more too, with a share for this computer's player.
        m_send_commands = 0;
        m_send_owed -= std::min(m_send_owed, work_share(players()));
    }
    m_waiting = done < due && m_frames == m_closed;
    ran(done);
}

std::uint32_t ClientSession::players() const noexcept {
    return static_cast<std::uint32_t>(
        std::count_if(m_roster.begin(), m_roster.end(), [](const Member& m) { return m.seat != sim::kNoPlayer; }));
}

void ClientSession::send_commands() {
    // Paced as the host admits them (MS-17): a player's share of commands and work per frame run,
    // so that a burst waits here, not at the host, which drops what waits too long.
    if (m_simulation == nullptr) {
        return;
    }
    const std::uint32_t share = command_share(players());
    const std::uint64_t work = work_share(players());
    std::size_t n = 0;
    while (n < m_outgoing.size() && m_send_commands < share && m_send_owed < work) {
        ++m_send_commands;
        m_send_owed += command_work(m_simulation->world(), m_outgoing[n]);
        ++n;
    }
    while (n > 0) {
        // As many as fit into one message the host takes (large build orders take room).
        std::size_t take = n;
        std::vector<std::uint8_t> bytes;
        for (;;) {
            CommandBatch batch;
            batch.commands.assign(m_outgoing.begin(), m_outgoing.begin() + static_cast<std::ptrdiff_t>(take));
            bytes = encode(batch);
            if (bytes.size() <= kMaxMessageToHost || take == 1) {
                break;
            }
            take /= 2;
        }
        m_outgoing.erase(m_outgoing.begin(), m_outgoing.begin() + static_cast<std::ptrdiff_t>(take));
        n -= take;
        if (bytes.size() <= kMaxMessageToHost) {
            m_transport->send(kHost, bytes);
        }
    }
}

void ClientSession::reconnect() {
    m_connect = m_reconnect;
    m_reconnecting = true;
    m_welcomed = false;
    m_connected = false;
    m_transport.reset();
    m_migration_ms = 0;
    m_retry_ms = kRetryMs;  // at once
    m_hello.seat = m_seat;
    m_hello.token = m_token;
    m_hello.returning = false;
    // The frames in hand are of the old connection; the host sends the game anew. Until then
    // nothing this computer reports may be taken for the new game (epoch 0 is none).
    m_frames_ahead.clear();
    m_closed = m_frames;
    m_epoch = 0;
    m_part.clear();
    m_load_reported = kNoLoadReport;
    SessionEvent e;
    e.kind = SessionEvent::Kind::Reconnecting;
    e.seat = m_seat;
    event(std::move(e));
}

void ClientSession::report(std::uint32_t real_ms) {
    if (m_simulation == nullptr) {
        return;
    }
    m_since_report += real_ms;
    if (m_frames != m_reported && m_since_report >= m_config.progress_interval_ms) {
        send(kHost, Progress{m_epoch, m_frames, std::nullopt});
        m_reported = m_frames;
        m_since_report = 0;
    }
}

void ClientSession::report_loading(std::uint32_t real_ms) {
    m_since_load_report = std::min(m_since_load_report + real_ms, m_config.progress_interval_ms);
    std::uint8_t percent = 0;
    if (!m_whole.empty() || m_pending) {
        percent = 100;  // here, and being loaded
    } else if (!m_part.empty() && m_part_total > 0) {
        percent =
            static_cast<std::uint8_t>(std::min<std::uint64_t>(99, std::uint64_t{m_part.size()} * 100 / m_part_total));
    } else {
        return;  // no game on its way (one that comes whole is loaded at once)
    }
    // A few times a second, but the end at once: the host may be waiting for it to start.
    if (percent == m_load_reported || (percent < 100 && m_since_load_report < m_config.progress_interval_ms)) {
        return;
    }
    send(kHost, LoadProgress{percent});
    m_load_reported = percent;
    m_since_load_report = 0;
}

}  // namespace o1602::net
