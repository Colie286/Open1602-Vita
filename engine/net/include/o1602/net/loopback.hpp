// SPDX-License-Identifier: GPL-3.0-or-later
//
// A network in memory, with a clock of its own: messages arrive after a delay (plus jitter), in
// order per link, when the clock has advanced that far. For tests of whole sessions on one computer
// without sockets, under latency and with connections that break.
#pragma once

#include <cstdint>
#include <memory>

#include "o1602/net/transport.hpp"

namespace o1602::net {

class LoopbackNetwork {
public:
    /// Each message takes `latency_ms` plus up to `jitter_ms` (from `seed`) one way.
    explicit LoopbackNetwork(std::uint32_t latency_ms = 0, std::uint32_t jitter_ms = 0, std::uint64_t seed = 1);
    ~LoopbackNetwork();
    LoopbackNetwork(const LoopbackNetwork&) = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;

    /// The host's end; there is one host per network.
    [[nodiscard]] std::unique_ptr<Transport> listen();
    /// A computer's end, connecting to the host (its link to the host is 0).
    [[nodiscard]] std::unique_ptr<Transport> connect();
    /// Time passes: messages due by then can be polled.
    void advance(std::uint32_t ms);
    /// Breaks the connection of a computer at once, without a word: both ends see Disconnected.
    void cut(const Transport& computer);
    void set_latency(std::uint32_t latency_ms, std::uint32_t jitter_ms);
    /// Messages sent so far, and their bytes (all ends).
    [[nodiscard]] std::uint64_t messages_sent() const noexcept;
    [[nodiscard]] std::uint64_t bytes_sent() const noexcept;

    struct Impl;

private:
    std::shared_ptr<Impl> m_impl;
};

}  // namespace o1602::net
