// SPDX-License-Identifier: GPL-3.0-or-later
//
// The transport over the internet and local networks: ENet (third_party/enet), reliable and
// ordered messages over UDP (ADR 0011). Only the host needs a reachable port; the other computers
// connect out to it, which passes through home routers without opening anything (M-001).
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "o1602/core/result.hpp"
#include "o1602/net/transport.hpp"

namespace o1602::net {

/// The port Open1602 hosts on unless told otherwise.
inline constexpr std::uint16_t kDefaultPort = 16020;

/// The host's end: listens on `port` (all addresses) for up to `max_computers` computers.
[[nodiscard]] core::Result<std::unique_ptr<Transport>> enet_listen(std::uint16_t port = kDefaultPort,
                                                                   std::size_t max_computers = 32,
                                                                   std::size_t packet_limit = kMaxPacketBytes,
                                                                   std::size_t queue_limit = 0,
                                                                   const std::string& bind_address = {});
/// A computer's end: connects to the host at `address` (name or IPv4 address). The connection is
/// there with the Connected event, or failed with Disconnected.
[[nodiscard]] core::Result<std::unique_ptr<Transport>> enet_connect(const std::string& address,
                                                                    std::uint16_t port = kDefaultPort,
                                                                    std::size_t packet_limit = kMaxPacketBytes,
                                                                    std::size_t queue_limit = 0);

}  // namespace o1602::net
