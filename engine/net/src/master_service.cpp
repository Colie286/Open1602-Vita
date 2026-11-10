// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/net/master_service.hpp"

#include <algorithm>
namespace o1602::net::master {
namespace {
std::uint64_t pair(std::uint32_t a, std::uint32_t b) {
    return (std::uint64_t(std::min(a, b)) << 32) | std::max(a, b);
}
void send(std::vector<Delivery>& out, std::uint32_t id, Message m) {
    out.push_back({id, encode(m)});
}
void error(std::vector<Delivery>& out, std::uint32_t id, Error e, std::uint32_t request = 0) {
    Message m;
    m.kind = Kind::Error;
    m.peer = static_cast<std::uint32_t>(e);
    m.successor = request;
    send(out, id, std::move(m));
}
}  // namespace
bool Service::connect(std::uint32_t id, std::uint64_t now) {
    if (!id || m_peers.contains(id) || m_peers.size() >= m_limits.clients)
        return false;
    Peer p;
    p.window = now;
    p.born = now;
    m_peers.emplace(id, p);
    return true;
}
void Service::close(std::uint32_t a, std::uint32_t b, State& s, std::vector<Delivery>& out) {
    if (!s.links.erase(pair(a, b)))
        return;
    Message m;
    m.kind = Kind::Down;
    m.room = s.room.id;
    m.peer = b;
    send(out, a, m);
    m.peer = a;
    send(out, b, m);
}
void Service::leave(std::uint32_t id, std::uint64_t now, std::vector<Delivery>& out) {
    auto p = m_peers.find(id);
    if (p == m_peers.end() || !p->second.room)
        return;
    auto room = p->second.room;
    p->second.room = 0;
    auto it = m_rooms.find(room);
    if (it == m_rooms.end())
        return;
    auto& s = it->second;
    for (auto other : s.members)
        if (other != id)
            close(id, other, s, out);
    s.members.erase(id);
    if (s.room.host == id) {
        s.room.host = 0;
        s.orphaned = now ? now : 1;
    }
    if (s.members.empty() || (!s.room.host && (!s.next || !s.members.contains(s.next)))) {
        for (auto other : s.members) {
            m_peers[other].room = 0;
            error(out, other, Error::NotFound);
        }
        m_rooms.erase(it);
    }
}
std::vector<Delivery> Service::disconnect(std::uint32_t id, std::uint64_t now) {
    std::vector<Delivery> out;
    leave(id, now, out);
    m_peers.erase(id);
    return out;
}
std::vector<Delivery> Service::tick(std::uint64_t now) {
    std::vector<Delivery> out;
    for (auto it = m_rooms.begin(); it != m_rooms.end();) {
        auto& s = it->second;
        if (!s.room.host && now - s.orphaned >= 30000) {
            for (auto id : s.members) {
                m_peers[id].room = 0;
                error(out, id, Error::NotFound);
            }
            it = m_rooms.erase(it);
        } else
            ++it;
    }
    for (const auto& [id, p] : m_peers)
        if (!p.hello && now - p.born >= 10000)
            m_drops.push_back(id);
    return out;
}
std::vector<Delivery> Service::receive(std::uint32_t id, std::span<const std::uint8_t> bytes, std::uint64_t now) {
    std::vector<Delivery> out;
    auto it = m_peers.find(id);
    if (it == m_peers.end())
        return out;
    auto& p = it->second;
    if (now - p.window >= 1000) {
        p.window = now;
        p.bytes = 0;
        p.controls = 0;
        p.data = 0;
    }
    p.bytes += bytes.size();
    auto m = decode(bytes);
    if (!m || p.bytes > m_limits.bytes_per_second) {
        m_drops.push_back(id);
        return out;
    }
    const bool data = m->kind == Kind::Data || m->kind == Kind::Credit;
    if ((data ? ++p.data : ++p.controls) > (data ? m_limits.data_per_second : m_limits.controls_per_second)) {
        m_drops.push_back(id);
        return out;
    }
    if (m->kind == Kind::Hello) {
        p.hello = true;
        Message w;
        w.kind = Kind::Welcome;
        w.peer = id;
        send(out, id, w);
        return out;
    }
    if (!p.hello) {
        m_drops.push_back(id);
        return out;
    }
    if (m->kind == Kind::List) {
        Message list;
        list.kind = Kind::Rooms;
        for (const auto& [rid, s] : m_rooms)
            if (s.room.host && list.rooms.size() < kRooms)
                list.rooms.push_back(s.room);
        send(out, id, std::move(list));
        return out;
    }
    if (m->kind == Kind::Leave) {
        leave(id, now, out);
        return out;
    }
    if (m->kind == Kind::Create) {
        if (p.room) {
            error(out, id, Error::Forbidden, m->successor);
            return out;
        }
        if (m_rooms.size() >= m_limits.rooms) {
            error(out, id, Error::Full, m->successor);
            return out;
        }
        if (m->data.empty() || m->data.size() > kMetadata) {
            error(out, id, Error::Invalid, m->successor);
            return out;
        }
        State s;
        do {
            s.room.id = m_next_room++;
        } while (!s.room.id || m_rooms.contains(s.room.id));
        s.room.host = id;
        s.room.info = m->data;
        s.members.insert(id);
        p.room = s.room.id;
        m_rooms.emplace(p.room, std::move(s));
        Message ack;
        ack.kind = Kind::Entered;
        ack.room = p.room;
        ack.peer = id;
        ack.successor = m->successor;
        send(out, id, ack);
        return out;
    }
    if (m->kind == Kind::Join) {
        auto room = m_rooms.find(m->room);
        if (p.room || room == m_rooms.end() || !room->second.room.host) {
            error(out, id, Error::NotFound, m->successor);
            return out;
        }
        if (room->second.members.size() >= m_limits.members) {
            error(out, id, Error::Full, m->successor);
            return out;
        }
        p.room = m->room;
        room->second.members.insert(id);
        Message ack;
        ack.kind = Kind::Entered;
        ack.room = p.room;
        ack.peer = room->second.room.host;
        ack.successor = m->successor;
        send(out, id, ack);
        return out;
    }
    auto room = m_rooms.find(p.room);
    if (!p.room || room == m_rooms.end()) {
        error(out, id, Error::NotFound);
        return out;
    }
    auto& s = room->second;
    switch (m->kind) {
        case Kind::Publish:
            if (s.room.host != id || m->data.empty()) {
                error(out, id, Error::Forbidden);
                break;
            }
            s.room.info = m->data;
            s.next = m->successor != id && s.members.contains(m->successor) ? m->successor : 0;
            break;
        case Kind::Claim:
            if (s.room.host != id && s.next != id) {
                error(out, id, Error::Forbidden, m->successor);
                break;
            }
            // The original host-leaving message can arrive before its broker Leave.
            // An authorised successor retries without evicting a still connected owner.
            if (s.room.host != id && s.room.host != 0)
                break;
            s.room.host = id;
            s.orphaned = 0;
            s.next = 0;
            {
                Message ack;
                ack.kind = Kind::Entered;
                ack.room = p.room;
                ack.peer = id;
                ack.successor = m->successor;
                send(out, id, ack);
            }
            break;
        case Kind::Open:
            if (m->peer == id) {
                error(out, id, Error::Forbidden);
                break;
            }
            if (!s.members.contains(m->peer)) {
                Message down;
                down.kind = Kind::Down;
                down.room = p.room;
                down.peer = m->peer;
                send(out, id, down);
                break;
            }
            if (s.links.insert(pair(id, m->peer)).second) {
                Message up;
                up.kind = Kind::Up;
                up.room = p.room;
                up.peer = m->peer;
                up.successor = m_peers[m->peer].ping;
                send(out, id, up);
                up.peer = id;
                up.successor = p.ping;
                send(out, m->peer, up);
            }
            break;
        case Kind::Close:
            close(id, m->peer, s, out);
            break;
        case Kind::Credit:
            if (!s.members.contains(m->peer) || !s.links.contains(pair(id, m->peer)))
                break;
            if (!m->successor || m->successor > kCreditWindow || !m->data.empty()) {
                error(out, id, Error::Invalid);
                break;
            }
            {
                const auto to = m->peer;
                m->peer = id;
                m->room = p.room;
                send(out, to, std::move(*m));
            }
            break;
        case Kind::Data:
            if (!s.members.contains(m->peer) || !s.links.contains(pair(id, m->peer))) {
                // Reliable packets already in flight may outlive a virtual link. A stale
                // destination breaks that link, never the sender's whole broker connection.
                // No payload or notification is delivered to an unauthorised destination.
                Message down;
                down.kind = Kind::Down;
                down.room = p.room;
                down.peer = m->peer;
                send(out, id, down);
                break;
            }
            {
                auto to = m->peer;
                m->kind = Kind::Forward;
                m->peer = id;
                m->room = p.room;
                m->successor = p.ping;
                send(out, to, std::move(*m));
            }
            break;
        default:
            error(out, id, Error::Invalid);
            break;
    }
    return out;
}
}  // namespace o1602::net::master
