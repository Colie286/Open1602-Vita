// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/net/session.hpp"
#include "o1602/core/profile.hpp"
#include <cstdio>

#include <algorithm>
#include <bit>
#include <utility>
#include <variant>

namespace o1602::net {

namespace {

constexpr std::uint32_t kTickMs = sim::Simulation::kTickMs;
constexpr std::uint32_t kMaxOwedMs = kTickMs * sim::Simulation::kMaxTicksPerAdvance;
/// command_work() of one item of a build order and one unit of a soldier order, in tiles.
constexpr std::uint64_t kBuildItemWork = 16;
constexpr std::uint64_t kSoldierWork = 4;
/// The most units a castle holds (its places in the castle window).
constexpr std::uint64_t kCastleUnits = 9;

}  // namespace

std::uint64_t command_work(const sim::World& world, const sim::Command& command) {
    if (const auto* demolish = std::get_if<sim::DemolishOrder>(&command)) {
        // The tiles of the rectangle on the island (plan_demolish looks at each).
        if (demolish->island >= world.islands().size()) {
            return 1;
        }
        const sim::IslandMap& map = world.islands()[demolish->island].map;
        const std::int64_t x0 = std::max(0, std::min(demolish->from.x, demolish->to.x));
        const std::int64_t y0 = std::max(0, std::min(demolish->from.y, demolish->to.y));
        const std::int64_t x1 =
            std::min(std::int64_t{map.width()} - 1, std::int64_t{std::max(demolish->from.x, demolish->to.x)});
        const std::int64_t y1 =
            std::min(std::int64_t{map.height()} - 1, std::int64_t{std::max(demolish->from.y, demolish->to.y)});
        if (x1 < x0 || y1 < y0) {
            return 1;
        }
        return static_cast<std::uint64_t>((x1 - x0 + 1) * (y1 - y0 + 1));
    }
    if (const auto* build = std::get_if<sim::BuildOrder>(&command)) {
        return 1 + build->items.size() * kBuildItemWork;
    }
    if (const auto* soldiers = std::get_if<sim::SoldierOrder>(&command)) {
        return 1 + soldiers->soldiers.size() * kSoldierWork;
    }
    if (const auto* castle = std::get_if<sim::CastleOrder>(&command);
        castle != nullptr && castle->op == sim::CastleOrder::Op::ReleaseAll) {
        return 1 + kCastleUnits * kSoldierWork;  // every unit of a full castle sent as a group
    }
    return 1;
}

const char* describe(EndReason reason) noexcept {
    switch (reason) {
        case EndReason::None:
            return "";
        case EndReason::ConnectFailed:
            return "the host cannot be reached";
        case EndReason::HostLeft:
            return "the connection to the host ended";
        case EndReason::Refused:
            return "the host refused";
        case EndReason::ProtocolError:
            return "a message made no sense";
        case EndReason::LoadFailed:
            return "the host's game could not be loaded";
        case EndReason::Desync:
            return "this computer kept diverging from the host";
    }
    return "";
}

const char* describe(RefuseReason reason) noexcept {
    switch (reason) {
        case RefuseReason::Version:
            return "another version of Open1602";
        case RefuseReason::Content:
            return "other game data";
        case RefuseReason::Full:
            return "no free slot";
        case RefuseReason::Closed:
            return "the host takes nobody into the running game";
        case RefuseReason::Desync:
            return "this computer kept diverging from the host";
        case RefuseReason::Invalid:
            return "this computer sent what makes no sense";
        case RefuseReason::Password:
            return "wrong password";
        case RefuseReason::Kicked:
            return "sent away by the host";
        case RefuseReason::Taken:
            return "the slot is played from another computer";
        case RefuseReason::Packages:
            return "content packages of the game are missing or differ";
    }
    return "";
}

Session::Session(std::unique_ptr<Transport> transport, SessionConfig config)
    : m_transport(std::move(transport)), m_config(std::move(config)) {
    m_config.hash_interval = std::max<std::uint32_t>(m_config.hash_interval, 1);
    m_config.min_lead = std::max<std::uint16_t>(m_config.min_lead, 1);
    m_config.max_lead = std::max(m_config.max_lead, m_config.min_lead);
}

double Session::tick_fraction() const noexcept {
    return std::min(0.999, static_cast<double>(m_pending_ms) / kTickMs);
}

std::vector<SessionEvent> Session::take_events() {
    std::vector<SessionEvent> events;
    events.swap(m_events);
    return events;
}

std::uint32_t Session::frames_due(std::uint32_t real_ms, const sim::Simulation& simulation) {
    // As Simulation::advance(): a stall counts as one tick, at most one more step is owed. The
    // frames of a pause run at normal speed: they carry the commands given meanwhile.
    const std::uint32_t speed = simulation.paused() ? 1 : std::max<std::uint32_t>(simulation.speed(), 1);
    const std::uint64_t game_ms = real_ms > sim::Simulation::kMaxCatchUpMs ? kTickMs : std::uint64_t{real_ms} * speed;
    m_pending_ms = static_cast<std::uint32_t>(std::min<std::uint64_t>(m_pending_ms + game_ms, 2 * kMaxOwedMs));
    return std::min(m_pending_ms / kTickMs, sim::Simulation::kMaxTicksPerAdvance);
}

void Session::ran(std::uint32_t frames) {
    const std::uint64_t spent = std::uint64_t{frames} * kTickMs;
    m_pending_ms = spent >= m_pending_ms ? 0 : static_cast<std::uint32_t>(m_pending_ms - spent);
    m_pending_ms = std::min(m_pending_ms, kMaxOwedMs);
}

std::optional<FrameHash> Session::run_frame(sim::Simulation& simulation, std::span<const sim::Command> commands) {
    // Scheduled for the tick that runs now: tick() carries them out first, by player and then in
    // this order, also in the frames of a pause, when the game time does not move on.
    const std::uint64_t now = simulation.next_tick();
    for (const sim::Command& c : commands) {
        simulation.schedule(now, c);
    }
    {core::profile::Scope timer(core::profile::Simulation);simulation.tick();}
    ++m_frames;
    // Fewer fingerprints at higher speed, so that their cost stays the same in real time. Every
    // computer runs the same speed at the same frame, and the speed is rounded up to a power of
    // two so that the frames of a higher speed are among those of a lower one.
    if (m_frames %
            (std::uint64_t{m_config.hash_interval} * std::bit_ceil(std::max<std::uint32_t>(simulation.speed(), 1))) !=
        0) {
        return std::nullopt;
    }
    core::profile::Scope timer(core::profile::NetworkHash);
    const FrameHash hash{m_frames, simulation.state_hash()};
    // Reuse the existing lockstep fingerprint; never compute a second hash for logging.
    std::fprintf(stderr, "LOCKSTEP frame=%llu tick=%llu hash=%llu\n",
                 static_cast<unsigned long long>(hash.frame),
                 static_cast<unsigned long long>(simulation.next_tick()), static_cast<unsigned long long>(hash.hash));
    return hash;
}

void Session::end(EndReason reason) {
    if (m_ended) {
        return;
    }
    m_ended = true;
    m_end_reason = reason;
    SessionEvent e;
    e.kind = SessionEvent::Kind::Ended;
    e.reason = reason;
    event(std::move(e));
}

void Session::lobby_event() {
    SessionEvent e;
    e.kind = SessionEvent::Kind::LobbyChanged;
    event(std::move(e));
}

void Session::chat_event(const Chat& chat) {
    SessionEvent e;
    e.kind = SessionEvent::Kind::Chat;
    e.seat = chat.seat;
    e.name = chat.name;
    e.text = chat.text;
    e.everyone = chat.everyone;
    e.to = chat.to;
    e.missed = chat.missed;
    event(std::move(e));
}

std::uint32_t Session::command_share(std::uint32_t players) const noexcept {
    const std::uint32_t fair = (kMaxCommandsPerMessage - kReservedCommands) / std::max(players, 1U);
    return std::max(1U, std::min(m_config.max_commands_per_frame, fair));
}

std::uint64_t Session::work_share(std::uint32_t players) noexcept {
    return std::max<std::uint64_t>(1, kFrameWork / std::max(players, 1U));
}

void Session::send(LinkId link, const Message& message) {
    send_bytes(link, encode(message));
}

void Session::send_bytes(LinkId link, std::span<const std::uint8_t> bytes) {
    if (bytes.size() <= kMaxPacketBytes) {
        m_transport->send(link, bytes);
        return;
    }
    for (const std::vector<std::uint8_t>& packet : packets(bytes)) {
        m_transport->send(link, packet);
    }
}

}  // namespace o1602::net
