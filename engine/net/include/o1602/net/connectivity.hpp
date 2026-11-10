// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "o1602/core/result.hpp"
namespace o1602::net {
enum class NetworkMode : std::uint8_t { Internet, Adhoc };
struct ConnectivityState {
    bool supported = false, pending = false, ready = true;
    NetworkMode mode = NetworkMode::Internet;
    std::string own_ip;
    std::vector<std::string> candidates;
    std::string status_key = "vita.net.wlan";
    std::string error;
    unsigned sockets = 0;
    std::uint64_t sent = 0, received = 0;
};
// Optional platform transport control. No SDK, UI or simulation dependency.
// Install once before the viewer; the platform owns the backend until after it dies.
class Connectivity {
public:
    virtual ~Connectivity() = default;
    virtual ConnectivityState state() const = 0;
    virtual core::Status select(NetworkMode) = 0;
    virtual void update() = 0;
    virtual void refresh() = 0;
    virtual bool input_blocked() const { return false; }
    virtual bool valid_peer(const std::string&) const = 0;
};
void install_connectivity(Connectivity* backend);
Connectivity* connectivity();
// Includes DNS workers holding the original ENet library; never switch modes underneath one.
bool network_in_use();
} // namespace o1602::net
