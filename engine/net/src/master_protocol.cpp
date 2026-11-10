// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/net/master_protocol.hpp"

#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
namespace o1602::net::master {
std::vector<std::uint8_t> encode(const Message& m) {
    if (m.rooms.size() > kRooms || m.data.size() > kMaxPacketBytes)
        return {};
    core::ByteWriter w;
    w.u32(0x524d324f);
    w.u16(kVersion);
    w.u8(static_cast<std::uint8_t>(m.kind));
    w.u64(m.room);
    w.u32(m.peer);
    w.u32(m.successor);
    w.u32(static_cast<std::uint32_t>(m.data.size()));
    w.bytes(m.data);
    w.u16(static_cast<std::uint16_t>(m.rooms.size()));
    for (const auto& r : m.rooms) {
        if (r.info.size() > kMetadata)
            return {};
        w.u64(r.id);
        w.u32(r.host);
        w.u16(static_cast<std::uint16_t>(r.info.size()));
        w.bytes(r.info);
    }
    auto out = w.take();
    return out.size() <= kWireLimit ? out : std::vector<std::uint8_t>{};
}
std::optional<Message> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kWireLimit)
        return {};
    core::ByteReader r(bytes);
    if (r.u32() != 0x524d324f || r.u16() != kVersion)
        return {};
    auto kind = r.u8();
    auto room = r.u64();
    auto peer = r.u32();
    auto successor = r.u32();
    auto size = r.u32();
    if (!kind || *kind < 1 || *kind > static_cast<unsigned>(Kind::Credit) || !room || !peer || !successor || !size ||
        *size > kMaxPacketBytes)
        return {};
    auto data = r.bytes(*size);
    auto count = r.u16();
    if (!data || !count || *count > kRooms)
        return {};
    Message m;
    m.kind = static_cast<Kind>(*kind);
    m.room = *room;
    m.peer = *peer;
    m.successor = *successor;
    m.data.assign(data->begin(), data->end());
    if (m.kind != Kind::Data && m.kind != Kind::Forward && m.data.size() > kMetadata)
        return {};
    if (m.kind != Kind::Rooms && *count != 0)
        return {};
    for (unsigned i = 0; i < *count; ++i) {
        auto id = r.u64();
        auto host = r.u32();
        auto n = r.u16();
        if (!id || !*id || !host || !*host || !n || *n > kMetadata)
            return {};
        auto info = r.bytes(*n);
        if (!info)
            return {};
        m.rooms.push_back({*id, *host, {info->begin(), info->end()}});
    }
    if (!r.at_end())
        return {};
    return m;
}
}  // namespace o1602::net::master
