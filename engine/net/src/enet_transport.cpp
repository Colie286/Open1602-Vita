// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/net/enet_transport.hpp"

#include <cstdint>
#include <enet/enet.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "enet_library.hpp"

namespace o1602::net {

namespace {

using detail::EnetLibrary;

constexpr std::size_t kChannels = 1;
/// ENet applies its packet limit to what a host sends and receives alike, and holds what it receives
/// of a packet until the packet is whole: so the limit is small, and games travel in parts (MS-7).
/// (protocol.hpp is not included here: on Windows ENet brings windows.h, whose macros near and far
/// break the game's headers.)

/// Connections from one address at most (ENet's default is 4095): a few computers behind one router.
constexpr std::size_t kDuplicatePeers = 4;
constexpr enet_uint32 kTimeoutMinMs = 5000;
constexpr enet_uint32 kTimeoutMaxMs = 15000;

class EnetTransport final : public Transport {
public:
    /// `host_link`: a computer's end, whose one link (to the host) is 0.
    EnetTransport(ENetHost* host, bool host_link, std::size_t queue_limit = 0)
        : m_host(host), m_next(host_link ? 0 : 1), m_queue_limit(queue_limit) {}
    ~EnetTransport() override {
        for (const auto& [link, peer] : m_links) {
            enet_peer_disconnect_now(peer, 0);
        }
        enet_host_flush(m_host);
        enet_host_destroy(m_host);
        EnetLibrary::release();
    }
    EnetTransport(const EnetTransport&) = delete;
    EnetTransport& operator=(const EnetTransport&) = delete;

    /// The link of a connection being made (a computer's end).
    void expect(ENetPeer* peer) { add(peer); }

    void poll(std::vector<TransportEvent>& events) override {
        for (const LinkId link : m_broken) {
            events.push_back({TransportEvent::Kind::Disconnected, link, {}});
        }
        m_broken.clear();
        ENetEvent e;
        std::size_t received_bytes = 0;
        // Keep a flooded radio/UDP socket from monopolising the application frame.
        // A service failure is a broken link, not an endless silent wait for a snapshot.
        for (unsigned budget = 0; budget < 256; ++budget) {
            const int rc = enet_host_service(m_host, &e, 0);
            if (rc < 0) {
                for (const auto& [link, peer] : m_links) {
                    peer->data = nullptr;
                    events.push_back({TransportEvent::Kind::Disconnected, link, {}});
                }
                m_links.clear();
                break;
            }
            if (rc == 0)
                break;
            switch (e.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    const LinkId link = e.peer->data != nullptr ? id(e.peer) : add(e.peer);
                    enet_peer_timeout(e.peer, 0, kTimeoutMinMs, kTimeoutMaxMs);
                    events.push_back({TransportEvent::Kind::Connected, link, {}});
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT:
                    if (e.peer->data != nullptr) {
                        const LinkId link = id(e.peer);
                        m_links.erase(link);
                        e.peer->data = nullptr;
                        events.push_back({TransportEvent::Kind::Disconnected, link, {}});
                    }
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    if (e.peer->data != nullptr) {
                        events.push_back({TransportEvent::Kind::Received,
                                          id(e.peer),
                                          {e.packet->data, e.packet->data + e.packet->dataLength}});
                    }
                    received_bytes += e.packet->dataLength;
                    enet_packet_destroy(e.packet);
                    break;
                case ENET_EVENT_TYPE_NONE:
                    break;
            }
            // Only bounded relay endpoints opt in. Direct/LAN/Adhoc defaults stay unchanged.
            // Bound temporary receive-event copies as well as ENet's outgoing queue.
            if (m_queue_limit && received_bytes >= m_queue_limit)
                break;
        }
    }

    void send(LinkId link, std::span<const std::uint8_t> message) override {
        const auto it = m_links.find(link);
        if (it == m_links.end()) {
            return;
        }
        if (m_queue_limit && queued_bytes(link) + message.size() > m_queue_limit) {
            auto peer = it->second;
            peer->data = nullptr;
            enet_peer_disconnect_now(peer, 0);
            m_links.erase(it);
            m_broken.push_back(link);
            return;
        }
        ENetPacket* packet = enet_packet_create(message.data(), message.size(), ENET_PACKET_FLAG_RELIABLE);
        if (packet == nullptr || enet_peer_send(it->second, 0, packet) != 0) {
            if (packet != nullptr) {
                enet_packet_destroy(packet);
            }
            // Delivered whole or the link breaks (Transport::send): a message lost here would
            // leave the other side waiting for it forever.
            const LinkId broken = it->first;
            it->second->data = nullptr;
            enet_peer_disconnect_later(it->second, 0);
            m_links.erase(it);
            m_broken.push_back(broken);
        }
    }

    void flush() override { enet_host_flush(m_host); }

    void disconnect(LinkId link) override {
        const auto it = m_links.find(link);
        if (it == m_links.end()) {
            return;
        }
        // After what is queued; ENet's own DISCONNECT for it is no longer reported.
        it->second->data = nullptr;
        enet_peer_disconnect_later(it->second, 0);
        m_links.erase(it);
    }

    [[nodiscard]] std::uint32_t round_trip_ms(LinkId link) const override {
        const auto it = m_links.find(link);
        return it == m_links.end() ? 0 : it->second->roundTripTime;
    }

    [[nodiscard]] std::size_t queued_bytes(LinkId link) const override {
        const auto it = m_links.find(link);
        if (it == m_links.end())
            return 0;
        std::size_t queued = 0;
        for (auto* list : {&it->second->outgoingCommands, &it->second->outgoingSendReliableCommands,
                           &it->second->sentReliableCommands})
            for (auto node = enet_list_begin(list); node != enet_list_end(list); node = enet_list_next(node))
                queued += reinterpret_cast<ENetOutgoingCommand*>(node)->fragmentLength;
        return queued;
    }

    [[nodiscard]] std::string address(LinkId link) const override {
        const auto it = m_links.find(link);
        char text[64] = {};
        if (it == m_links.end() || enet_address_get_host_ip(&it->second->address, text, sizeof text) != 0) {
            return {};
        }
        return text;
    }

private:
    // The link id lives in the peer's data pointer (ids start at 1 there, so null means none).
    static LinkId id(const ENetPeer* peer) {
        return static_cast<LinkId>(reinterpret_cast<std::uintptr_t>(peer->data) - 1);
    }
    LinkId add(ENetPeer* peer) {
        const LinkId link = m_next++;
        // ENet keeps a pointer per peer; it holds the link's number, never dereferenced.
        peer->data =
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(link) + 1);  // NOLINT(performance-no-int-to-ptr)
        m_links.emplace(link, peer);
        return link;
    }

    ENetHost* m_host;
    LinkId m_next;
    std::size_t m_queue_limit = 0;
    std::unordered_map<LinkId, ENetPeer*> m_links;  // lookups only, never walked for the game
    std::vector<LinkId> m_broken;                   ///< failed to send: reported at the next poll
};

}  // namespace

core::Result<std::unique_ptr<Transport>> enet_listen(std::uint16_t port, std::size_t max_computers,
                                                     std::size_t packet_limit, std::size_t queue_limit,
                                                     const std::string& bind_address) {
    if (!EnetLibrary::acquire()) {
        return core::make_error(core::ErrorCode::PlatformError, "ENet could not be initialised");
    }
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    if (!bind_address.empty() && enet_address_set_host_ip(&address, bind_address.c_str()) != 0) {
        EnetLibrary::release();
        return core::make_error(core::ErrorCode::InvalidFormat, "invalid bind IPv4 address");
    }
    ENetHost* host = enet_host_create(&address, max_computers, kChannels, 0, 0);
    if (host == nullptr) {
        EnetLibrary::release();
        return core::make_error(core::ErrorCode::PlatformError,
                                "cannot listen on UDP port " + std::to_string(port) + " (in use?)");
    }
    host->maximumPacketSize = packet_limit;
    host->maximumWaitingData = packet_limit;
    host->duplicatePeers = queue_limit ? 32 : kDuplicatePeers;
    return std::unique_ptr<Transport>(std::make_unique<EnetTransport>(host, false, queue_limit));
}

core::Result<std::unique_ptr<Transport>> enet_connect(const std::string& address, std::uint16_t port,
                                                      std::size_t packet_limit, std::size_t queue_limit) {
    if (!EnetLibrary::acquire()) {
        return core::make_error(core::ErrorCode::PlatformError, "ENet could not be initialised");
    }
    ENetAddress to{};
    // Numeric endpoints never enter the platform DNS resolver; MasterClient resolves names beside the UI.
    if (enet_address_set_host_ip(&to, address.c_str()) != 0 && enet_address_set_host(&to, address.c_str()) != 0) {
        EnetLibrary::release();
        return core::make_error(core::ErrorCode::NotFound, "unknown host: " + address);
    }
    to.port = port;
    ENetHost* host = enet_host_create(nullptr, 1, kChannels, 0, 0);
    if (host == nullptr) {
        EnetLibrary::release();
        return core::make_error(core::ErrorCode::PlatformError, "cannot open a UDP socket");
    }
    host->maximumPacketSize = packet_limit;  // the same on both ends (MS-11)
    host->maximumWaitingData = packet_limit;
    ENetPeer* peer = enet_host_connect(host, &to, kChannels, 0);
    if (peer == nullptr) {
        enet_host_destroy(host);
        EnetLibrary::release();
        return core::make_error(core::ErrorCode::PlatformError, "cannot connect to " + address);
    }
    auto transport = std::make_unique<EnetTransport>(host, true, queue_limit);
    transport->expect(peer);
    return std::unique_ptr<Transport>(std::move(transport));
}

}  // namespace o1602::net
