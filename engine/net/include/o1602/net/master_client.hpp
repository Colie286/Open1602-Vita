// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "o1602/net/lan.hpp"
#include "o1602/net/master_protocol.hpp"
namespace o1602::net {
// Virtual names are room/member IDs, never IP addresses or candidates for DNS.
std::string relay_address(std::uint64_t room, std::uint32_t member);
std::optional<std::pair<std::uint64_t, std::uint32_t>> parse_relay_address(std::string_view);
std::optional<std::pair<std::string, std::uint16_t>> parse_master_endpoint(std::string_view);
class MasterClient : public std::enable_shared_from_this<MasterClient> {
public:
    static core::Result<std::shared_ptr<MasterClient>> open(const std::string& endpoint);
    ~MasterClient();
    void update();
    void refresh();
    void leave();
    bool ready() const;
    bool failed() const;
    bool hosting() const;
    const std::string& error() const;
    const std::vector<FoundGame>& games() const;
    std::uint64_t room() const;
    std::uint32_t member() const;
    void publish(const GameInfo&, std::string_view successor = {});
    core::Result<std::unique_ptr<Transport>> listen();
    /// Migration must claim the original member's room, never create a replacement room.
    core::Result<std::unique_ptr<Transport>> take_over(std::string_view successor_address);
    core::Result<std::unique_ptr<Transport>> connect(const std::string& virtual_address);

private:
    class Relay;
    struct Impl;
    explicit MasterClient(std::unique_ptr<Transport>);
    std::unique_ptr<Impl> m;
};
}  // namespace o1602::net
