// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/net/protocol.hpp"

#include <algorithm>
#include <utility>

#include "wire.hpp"

#include "o1602/core/byte_writer.hpp"
#include "o1602/core/hash.hpp"
#include "o1602/sim/command_io.hpp"

namespace o1602::net {

namespace {

using detail::In;

// A message starts with its type, the index in the Message variant (appended, never reordered).

void put_commands(core::ByteWriter& out, const std::vector<sim::Command>& commands) {
    out.u32(static_cast<std::uint32_t>(commands.size()));
    for (const sim::Command& c : commands) {
        sim::write_command(out, c);
    }
}

void put_packages(core::ByteWriter& out, const std::vector<assets::ContentPackage>& packages) {
    out.u32(static_cast<std::uint32_t>(packages.size()));
    for (const assets::ContentPackage& p : packages) {
        out.string(p.id);
        out.string(p.version);
        out.u64(p.hash);
    }
}

void put(core::ByteWriter& out, const Hello& m) {
    out.u16(m.protocol);
    out.u64(m.content);
    out.string(m.name);
    out.u8(m.seat);
    out.u64(m.token);
    out.string(m.password);
    out.boolean(m.returning);
    put_packages(out, m.packages);
    out.boolean(m.fetch_packages);
}

void put(core::ByteWriter& out, const CommandBatch& m) {
    put_commands(out, m.commands);
}

void put(core::ByteWriter& out, const Progress& m) {
    out.u32(m.epoch);
    out.u64(m.frames);
    out.boolean(m.hash.has_value());
    if (m.hash) {
        out.u64(m.hash->frame);
        out.u64(m.hash->hash);
    }
}

void put(core::ByteWriter& out, const Welcome& m) {
    out.u16(m.protocol);
    out.u8(m.seat);
    out.u64(m.token);
}

void put(core::ByteWriter& out, const Refused& m) {
    out.u8(static_cast<std::uint8_t>(m.reason));
    put_packages(out, m.packages);
    out.u64(m.share_bytes);
}

void put(core::ByteWriter& out, const Snapshot& m) {
    out.u32(m.epoch);
    out.u64(m.frame);
    out.u32(m.speed);
    out.boolean(m.paused);
    out.u32(static_cast<std::uint32_t>(m.save.size()));
    out.bytes(m.save);
}

void put(core::ByteWriter& out, const Turn& m) {
    out.u64(m.first);
    out.u64(m.closed);
    out.u16(m.lead);
    out.u32(static_cast<std::uint32_t>(m.frames.size()));
    for (const FrameCommands& f : m.frames) {
        out.u64(f.frame);
        put_commands(out, f.commands);
    }
}

void put(core::ByteWriter& out, const Roster& m) {
    out.u32(static_cast<std::uint32_t>(m.members.size()));
    for (const Member& member : m.members) {
        out.u8(member.seat);
        out.string(member.name);
        out.u8(static_cast<std::uint8_t>(member.state));
        out.boolean(member.host);
    }
    out.u8(m.successor);
    out.string(m.successor_address);
}

void put(core::ByteWriter& out, const Desync& m) {
    out.u64(m.frame);
    out.u32(static_cast<std::uint32_t>(m.seats.size()));
    for (const Seat s : m.seats) {
        out.u8(s);
    }
}

void put(core::ByteWriter& out, const Lobby& m) {
    out.string(m.title);
    out.string(m.game);
    out.u32(static_cast<std::uint32_t>(m.seats.size()));
    for (const LobbySeat& seat : m.seats) {
        out.u8(static_cast<std::uint8_t>(seat.kind));
        out.string(seat.name);
        out.boolean(seat.ready);
        out.u16(seat.banner);
    }
    out.boolean(m.started);
    out.boolean(m.join_running);
    out.boolean(m.spectators);
    out.boolean(m.password);
    out.u32(static_cast<std::uint32_t>(m.setup.size()));
    out.bytes(m.setup);
    out.u32(static_cast<std::uint32_t>(m.banners.size()));
    for (const sim::BannerId b : m.banners) {
        out.u16(b);
    }
    out.boolean(m.computer_takes_over);
    out.boolean(m.host_pauses_only);
    out.boolean(m.spectators_chat);
}

void put(core::ByteWriter& out, const TakeSeat& m) {
    out.u8(m.seat);
}

void put(core::ByteWriter& out, const SetReady& m) {
    out.boolean(m.ready);
}

void put_seats(core::ByteWriter& out, const std::vector<Seat>& seats) {
    out.u32(static_cast<std::uint32_t>(seats.size()));
    for (const Seat s : seats) {
        out.u8(s);
    }
}

void put(core::ByteWriter& out, const Chat& m) {
    out.u8(m.seat);
    out.string(m.name);
    out.string(m.text);
    out.boolean(m.everyone);
    put_seats(out, m.to);
    put_seats(out, m.missed);
}

void put(core::ByteWriter& out, const SetBanner& m) {
    out.u16(m.banner);
}

void put(core::ByteWriter& out, const Dropped& m) {
    out.u32(m.commands);
}

void put(core::ByteWriter& out, const HostLeaving& m) {
    out.string(m.successor_address);
}

void put(core::ByteWriter& out, const Part& m) {
    out.u32(m.total);
    out.u32(static_cast<std::uint32_t>(m.bytes.size()));
    out.bytes(m.bytes);
}

void put(core::ByteWriter& out, const PackageFiles& m) {
    put_packages(out, {m.package});
    out.u32(m.index);
    out.u32(m.count);
    out.u32(static_cast<std::uint32_t>(m.files.size()));
    for (const assets::PackageFile& f : m.files) {
        out.string(f.path);
        out.u32(static_cast<std::uint32_t>(f.bytes.size()));
        out.bytes(f.bytes);
    }
}

void put(core::ByteWriter& out, const Standby& m) {
    out.u32(static_cast<std::uint32_t>(m.tokens.size()));
    for (const SeatToken& t : m.tokens) {
        out.u8(t.seat);
        out.u64(t.salt);
        out.u64(t.verifier);
    }
    out.u32(static_cast<std::uint32_t>(m.computer_held.size()));
    for (const Seat s : m.computer_held) {
        out.u8(s);
    }
}

std::vector<assets::ContentPackage> get_packages(In& in) {
    const std::uint32_t n = in.count(kMaxPackages);
    std::vector<assets::ContentPackage> out;
    out.reserve(n);
    for (std::uint32_t i = 0; i < n && in.ok(); ++i) {
        assets::ContentPackage p;
        p.id = in.text(assets::kMaxPackageIdLength);
        p.version = in.text(assets::kMaxPackageVersionLength);
        p.hash = in.u64();
        out.push_back(std::move(p));
    }
    return out;
}

Hello get_hello(In& in) {
    Hello m;
    m.protocol = in.u16();
    m.content = in.u64();
    m.name = in.text(kMaxNameLength);
    m.seat = in.u8();
    m.token = in.u64();
    m.password = in.text(kMaxPasswordLength);
    m.returning = in.boolean();
    m.packages = get_packages(in);
    m.fetch_packages = in.boolean();
    return m;
}

Progress get_progress(In& in) {
    Progress m;
    m.epoch = in.u32();
    m.frames = in.u64();
    if (in.boolean()) {
        FrameHash h;
        h.frame = in.u64();
        h.hash = in.u64();
        m.hash = h;
    }
    return m;
}

Welcome get_welcome(In& in) {
    Welcome m;
    m.protocol = in.u16();
    m.seat = in.u8();
    m.token = in.u64();
    return m;
}

Refused get_refused(In& in) {
    Refused m;
    const std::uint8_t reason = in.u8();
    if (reason > static_cast<std::uint8_t>(RefuseReason::Packages)) {
        in.fail();
    }
    m.reason = static_cast<RefuseReason>(reason);
    m.packages = get_packages(in);
    m.share_bytes = in.u64();
    return m;
}

Snapshot get_snapshot(In& in) {
    Snapshot m;
    m.epoch = in.u32();
    m.frame = in.u64();
    m.speed = in.u32();
    m.paused = in.boolean();
    const std::uint32_t n = in.count(kMaxSnapshotBytes);
    if (const auto bytes = in.ok() ? in.reader().bytes(n) : std::nullopt) {
        m.save.assign(bytes->begin(), bytes->end());
    } else {
        in.fail();
    }
    return m;
}

Turn get_turn(In& in) {
    Turn m;
    m.first = in.u64();
    m.closed = in.u64();
    m.lead = in.u16();
    if (m.closed < m.first) {
        in.fail();
    }
    const std::uint32_t n = in.count(kMaxFramesPerTurn);
    m.frames.reserve(n);
    for (std::uint32_t i = 0; i < n && in.ok(); ++i) {
        FrameCommands f;
        f.frame = in.u64();
        // Ascending and within the range, so that a computer can walk them once.
        const std::uint64_t floor = m.frames.empty() ? m.first : m.frames.back().frame + 1;
        if (f.frame < floor || f.frame >= m.closed) {
            in.fail();
        }
        f.commands = in.commands();
        m.frames.push_back(std::move(f));
    }
    return m;
}

Roster get_roster(In& in) {
    Roster m;
    const std::uint32_t n = in.count(kMaxRosterMembers);
    m.members.reserve(n);
    for (std::uint32_t i = 0; i < n && in.ok(); ++i) {
        Member member;
        member.seat = in.u8();
        member.name = in.text(kMaxNameLength);
        const std::uint8_t state = in.u8();
        if (state > static_cast<std::uint8_t>(Member::State::Lagging)) {
            in.fail();
        }
        member.state = static_cast<Member::State>(state);
        member.host = in.boolean();
        m.members.push_back(std::move(member));
    }
    m.successor = in.u8();
    m.successor_address = in.text(kMaxAddressLength);
    return m;
}

Desync get_desync(In& in) {
    Desync m;
    m.frame = in.u64();
    const std::uint32_t n = in.count(kMaxLobbySeats);  // slots, not members
    m.seats.reserve(n);
    for (std::uint32_t i = 0; i < n && in.ok(); ++i) {
        m.seats.push_back(in.u8());
    }
    return m;
}

Lobby get_lobby(In& in) {
    Lobby m;
    m.title = in.text(kMaxTitleLength);
    m.game = in.text(kMaxTitleLength);
    const std::uint32_t n = in.count(kMaxLobbySeats);
    m.seats.reserve(n);
    for (std::uint32_t i = 0; i < n && in.ok(); ++i) {
        LobbySeat seat;
        const std::uint8_t kind = in.u8();
        if (kind > static_cast<std::uint8_t>(LobbySeat::Kind::Reserved)) {
            in.fail();
        }
        seat.kind = static_cast<LobbySeat::Kind>(kind);
        seat.name = in.text(kMaxNameLength);
        seat.ready = in.boolean();
        seat.banner = in.u16();
        m.seats.push_back(std::move(seat));
    }
    m.started = in.boolean();
    m.join_running = in.boolean();
    m.spectators = in.boolean();
    m.password = in.boolean();
    const std::uint32_t size = in.count(kMaxSetupBytes);
    if (const auto bytes = in.ok() ? in.reader().bytes(size) : std::nullopt) {
        m.setup.assign(bytes->begin(), bytes->end());
    } else {
        in.fail();
    }
    const std::uint32_t banners = in.count(kMaxLobbyBanners);
    m.banners.reserve(banners);
    for (std::uint32_t i = 0; i < banners && in.ok(); ++i) {
        m.banners.push_back(in.u16());
    }
    m.computer_takes_over = in.boolean();
    m.host_pauses_only = in.boolean();
    m.spectators_chat = in.boolean();
    return m;
}

Standby get_standby(In& in) {
    Standby m;
    const std::uint32_t n = in.count(kMaxLobbySeats);
    m.tokens.reserve(n);
    for (std::uint32_t i = 0; i < n && in.ok(); ++i) {
        SeatToken t;
        t.seat = in.u8();
        t.salt = in.u64();
        t.verifier = in.u64();
        m.tokens.push_back(t);
    }
    const std::uint32_t held = in.count(kMaxLobbySeats);
    m.computer_held.reserve(held);
    for (std::uint32_t i = 0; i < held && in.ok(); ++i) {
        m.computer_held.push_back(in.u8());
    }
    return m;
}

Part get_part(In& in) {
    Part m;
    m.total = in.u32();
    if (m.total > kMaxMessageBytes) {
        in.fail();
    }
    const std::uint32_t n = in.count(static_cast<std::uint32_t>(kPartBytes));
    if (const auto bytes = in.ok() ? in.reader().bytes(n) : std::nullopt) {
        m.bytes.assign(bytes->begin(), bytes->end());
    } else {
        in.fail();
    }
    return m;
}

PackageFiles get_package_files(In& in) {
    PackageFiles m;
    const auto package = get_packages(in);
    if (package.size() != 1) {
        in.fail();
        return m;
    }
    m.package = package[0];
    m.index = in.u32();
    m.count = in.u32();
    if (m.count == 0 || m.index >= m.count) {
        in.fail();
    }
    const std::uint32_t n = in.count(kMaxPackageFiles);
    std::uint64_t total = 0;
    for (std::uint32_t i = 0; i < n && in.ok(); ++i) {
        assets::PackageFile f;
        f.path = in.text(assets::kMaxPackagePathLength);
        const std::uint32_t size = in.count(kMaxPackageFilesBytes);
        total += size;
        if (total > kMaxPackageFilesBytes || !assets::safe_package_path(f.path)) {
            in.fail();
            break;
        }
        if (const auto bytes = in.ok() ? in.reader().bytes(size) : std::nullopt) {
            f.bytes.assign(bytes->begin(), bytes->end());
        } else {
            in.fail();
        }
        m.files.push_back(std::move(f));
    }
    return m;
}

std::vector<Seat> get_seats(In& in) {
    std::vector<Seat> seats;
    const std::uint32_t n = in.count(kMaxLobbySeats);
    seats.reserve(n);
    for (std::uint32_t i = 0; i < n && in.ok(); ++i) {
        seats.push_back(in.u8());
    }
    return seats;
}

Chat get_chat(In& in) {
    Chat m;
    m.seat = in.u8();
    m.name = in.text(kMaxNameLength);
    m.text = in.text(kMaxChatLength);
    m.everyone = in.boolean();
    m.to = get_seats(in);
    m.missed = get_seats(in);
    if (m.everyone && !(m.to.empty() && m.missed.empty())) {
        in.fail();  // a line to everyone names nobody
    }
    return m;
}

std::optional<Message> get(In& in, std::uint8_t type) {
    switch (type) {
        case 0:
            return get_hello(in);
        case 1:
            return CommandBatch{in.commands()};
        case 2:
            return get_progress(in);
        case 3:
            return get_welcome(in);
        case 4:
            return get_refused(in);
        case 5:
            return get_snapshot(in);
        case 6:
            return get_turn(in);
        case 7:
            return get_roster(in);
        case 8:
            return get_desync(in);
        case 9:
            return get_lobby(in);
        case 10:
            return TakeSeat{in.u8()};
        case 11:
            return SetReady{in.boolean()};
        case 12:
            return get_chat(in);
        case 13:
            return get_standby(in);
        case 14:
            return SetBanner{in.u16()};
        case 15:
            return Dropped{in.u32()};
        case 16:
            return HostLeaving{in.text(kMaxAddressLength)};
        case 17:
            return get_part(in);
        case 18:
            return get_package_files(in);
        default:
            return std::nullopt;
    }
}

static_assert(std::variant_size_v<Message> == 19, "a new message needs its case in get()");

}  // namespace

std::vector<std::uint8_t> encode(const Message& message) {
    core::ByteWriter out;
    out.u8(static_cast<std::uint8_t>(message.index()));
    std::visit([&out](const auto& m) { put(out, m); }, message);
    return out.take();
}

std::vector<std::vector<std::uint8_t>> packets(std::span<const std::uint8_t> message) {
    std::vector<std::vector<std::uint8_t>> out;
    if (message.size() <= kMaxPacketBytes) {
        out.emplace_back(message.begin(), message.end());
        return out;
    }
    Part part;
    part.total = static_cast<std::uint32_t>(message.size());
    for (std::size_t at = 0; at < message.size(); at += kPartBytes) {
        const auto piece = message.subspan(at, std::min(kPartBytes, message.size() - at));
        part.bytes.assign(piece.begin(), piece.end());
        out.push_back(encode(part));
    }
    return out;
}

std::optional<Message> decode(std::span<const std::uint8_t> bytes) {
    In in(bytes);
    const std::uint8_t type = in.u8();
    if (!in.ok()) {
        return std::nullopt;
    }
    auto message = get(in, type);
    if (!message || !in.ok() || !in.at_end()) {
        return std::nullopt;
    }
    return message;
}

// NOLINTNEXTLINE(bugprone-exception-escape): a Command is never valueless, so std::visit cannot throw
sim::PlayerId command_player(const sim::Command& command) noexcept {
    return std::visit([](const auto& c) { return c.player; }, command);
}

std::uint64_t token_verifier(std::uint64_t token, std::uint64_t salt) noexcept {
    core::ByteWriter in;
    in.u64(salt);
    in.u64(token);
    const auto digest = core::sha256(in.data());
    std::uint64_t v = 0;
    for (std::size_t k = 0; k < 8; ++k) {
        v |= std::uint64_t{digest[k]} << (8 * k);
    }
    return v;
}

}  // namespace o1602::net
