// SPDX-License-Identifier: GPL-3.0-or-later
//
// How messages travel (ADR 0011): reliable and in order, between the host and each other computer.
// The sessions know nothing else of the network, so that ENet (enet_transport.hpp), the in-memory
// network of the tests (loopback.hpp) or later another library can carry them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace o1602::net {

/// The largest message a session puts together: a whole game (protocol.hpp kMaxSnapshotBytes) and
/// the header of its message.
inline constexpr std::size_t kMaxMessageBytes = (128U << 20) + (64U << 10);
/// The largest message a transport carries in one piece, both ways (MS-7): larger ones travel in
/// parts (protocol.hpp Part), so that nobody can make the other side hold 128 MiB for a message that
/// never completes.
inline constexpr std::size_t kMaxPacketBytes = (1U << 20) + (64U << 10);

/// One connection of a transport; the host has one per computer, a computer one to the host.
using LinkId = std::uint32_t;

struct TransportEvent {
    enum class Kind : std::uint8_t {
        Connected,
        Disconnected,  ///< closed by the other side, timed out, or a connection attempt failed
        Received,
    };
    Kind kind = Kind::Received;
    LinkId link = 0;
    std::vector<std::uint8_t> data;  ///< Received: one whole message
};

class Transport {
public:
    virtual ~Transport() = default;

    /// Appends what happened since the last call; never blocks.
    virtual void poll(std::vector<TransportEvent>& events) = 0;
    /// Queues a message on a link: delivered whole, once, in the order sent, or the link breaks.
    virtual void send(LinkId link, std::span<const std::uint8_t> message) = 0;
    /// Sends what is queued now instead of at the next poll.
    virtual void flush() = 0;
    /// Closes a link after what is queued on it; no Disconnected event follows for it.
    virtual void disconnect(LinkId link) = 0;
    /// Time for a message there and back, as far as the transport knows (0 if not).
    [[nodiscard]] virtual std::uint32_t round_trip_ms(LinkId link) const = 0;
    /// Bytes still held by the reliable sender; optional backpressure for composed transports.
    [[nodiscard]] virtual std::size_t queued_bytes(LinkId /*link*/) const { return 0; }
    /// The other side's address as this end sees it (IPv4 in dots), empty if the transport has none.
    [[nodiscard]] virtual std::string address(LinkId /*link*/) const { return {}; }
};

}  // namespace o1602::net
