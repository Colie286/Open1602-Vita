// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <map>
#include <set>

#include "o1602/net/master_protocol.hpp"
namespace o1602::net::master {
struct Limits {
    std::size_t clients = 64, rooms = 32, members = 32;
    std::uint64_t bytes_per_second = 8U << 20;
    unsigned controls_per_second = 32, data_per_second = 2048;
};
struct Delivery {
    std::uint32_t peer;
    std::vector<std::uint8_t> bytes;
};
// Peer IDs come from the ENet transport, not from message payloads.
class Service {
public:
    explicit Service(Limits limits = {}, std::uint64_t first_room = 1)
        : m_limits(limits), m_next_room(first_room ? first_room : 1) {}
    bool connect(std::uint32_t, std::uint64_t now);
    std::vector<Delivery> receive(std::uint32_t, std::span<const std::uint8_t>, std::uint64_t now);
    std::vector<Delivery> disconnect(std::uint32_t, std::uint64_t now);
    std::vector<Delivery> tick(std::uint64_t now);
    std::vector<std::uint32_t> take_drops() {
        auto out = std::move(m_drops);
        m_drops.clear();
        return out;
    }
    void ping(std::uint32_t id, std::uint32_t ms) {
        if (m_peers.contains(id))
            m_peers[id].ping = ms;
    }
    std::size_t clients() const { return m_peers.size(); }
    std::size_t rooms() const { return m_rooms.size(); }

private:
    struct Peer {
        bool hello = false;
        std::uint64_t room = 0, window = 0, born = 0, bytes = 0;
        unsigned controls = 0, data = 0;
        std::uint32_t ping = 0;
    };
    struct State {
        Room room;
        std::uint32_t next = 0;
        std::uint64_t orphaned = 0;
        std::set<std::uint32_t> members;
        std::set<std::uint64_t> links;
    };
    void leave(std::uint32_t, std::uint64_t, std::vector<Delivery>&);
    void close(std::uint32_t, std::uint32_t, State&, std::vector<Delivery>&);
    Limits m_limits;
    std::uint64_t m_next_room;
    std::map<std::uint32_t, Peer> m_peers;
    std::map<std::uint64_t, State> m_rooms;
    std::vector<std::uint32_t> m_drops;
};
}  // namespace o1602::net::master
