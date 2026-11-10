// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finding games without typing addresses (ADR 0011). A host answers searches on a UDP port of its
// own (LanBeacon); a computer that looks for games asks the local network by broadcast, and any
// address it remembers directly (LanBrowser), and lists the games that answer with their round
// trip. Nothing of this is needed to join: an address alone does it too.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "o1602/core/result.hpp"
#include "o1602/net/enet_transport.hpp"
#include "o1602/net/protocol.hpp"

namespace o1602::net {

/// The port hosts answer searches on (the game itself runs on kDefaultPort or another).
inline constexpr std::uint16_t kDiscoveryPort = 16021;

/// What a host tells a computer that looks for games.
struct GameInfo {
    std::uint16_t protocol = kProtocolVersion;  ///< another version cannot join
    std::uint16_t port = kDefaultPort;          ///< where the game runs
    std::string title;                          ///< Lobby::title
    std::string host;                           ///< the host's name
    std::string game;                           ///< Lobby::game
    std::uint8_t players = 0;                   ///< human slots taken
    std::uint8_t slots = 0;                     ///< human slots in all (taken and open)
    bool started = false;
    bool password = false;
    bool join_running = true;
    bool spectators = true;
    friend bool operator==(const GameInfo&, const GameInfo&) = default;
};

/// The answer for a session: its lobby and who is in, on game port `port`.
[[nodiscard]] GameInfo game_info(const Lobby& lobby, const std::vector<Member>& roster, std::uint16_t port);

/// A search (`nonce` comes back in the answer) and an answer as bytes, and back (nothing for
/// what is neither: other programs may use the port).
[[nodiscard]] std::vector<std::uint8_t> encode_search(std::uint32_t nonce);
[[nodiscard]] std::optional<std::uint32_t> decode_search(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> encode_answer(std::uint32_t nonce, const GameInfo& info);
[[nodiscard]] std::optional<std::pair<std::uint32_t, GameInfo>> decode_answer(std::span<const std::uint8_t> bytes);

/// The host's end: answers searches with the game it describes.
class LanBeacon {
public:
    /// Listens on `port` of every address; an error if the port is taken (another host on this
    /// computer: its game can still be joined by address).
    [[nodiscard]] static core::Result<std::unique_ptr<LanBeacon>> open(std::uint16_t port = kDiscoveryPort);
    virtual ~LanBeacon() = default;

    virtual void set_info(GameInfo info) = 0;
    /// Answers the searches that came in since the last call.
    virtual void update() = 0;

protected:
    LanBeacon() = default;
};

/// A game that answered.
struct FoundGame {
    std::string address;  ///< of the host, as text ("192.168.1.20")
    GameInfo info;
    std::uint32_t ping_ms = 0;  ///< round trip of the last answer
    std::uint32_t age_ms = 0;   ///< since the last answer
};

/// The end of a computer looking for games.
class LanBrowser {
public:
    /// Asks hosts on their discovery port `port`.
    [[nodiscard]] static core::Result<std::unique_ptr<LanBrowser>> open(std::uint16_t port = kDiscoveryPort);
    virtual ~LanBrowser() = default;

    /// Asks the local network, and the hosts at `addresses` (names or IPv4 addresses).
    virtual void search(const std::vector<std::string>& addresses = {}) = 0;
    /// Collects the answers; games that have not answered for `forget_ms` are forgotten.
    virtual void update(std::uint32_t real_ms, std::uint32_t forget_ms = 6000) = 0;
    /// The games found, in the order they first answered.
    [[nodiscard]] virtual const std::vector<FoundGame>& games() const = 0;

protected:
    LanBrowser() = default;
};

}  // namespace o1602::net
