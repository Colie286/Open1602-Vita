// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "o1602/net/transport.hpp"
namespace o1602::net::master {
inline constexpr std::uint16_t kPort = 16022, kVersion = 2;
inline constexpr std::size_t kMetadata = 1024, kRooms = 64, kWireLimit = kMaxPacketBytes + 64;
// Receiver credit bounds the data in flight through the broker. It fits any legal packet.
inline constexpr std::size_t kCreditWindow = 2 * kMaxPacketBytes;
enum class Kind : std::uint8_t {
    Hello = 1,
    Welcome,
    List,
    Rooms,
    Create,
    Join,
    Entered,
    Publish,
    Claim,
    Leave,
    Open,
    Close,
    Data,
    Forward,
    Up,
    Down,
    Error,
    Credit
};
enum class Error : std::uint32_t { Invalid = 1, Full, NotFound, Forbidden, Rate, Version };
struct Room {
    std::uint64_t id = 0;
    std::uint32_t host = 0;
    std::vector<std::uint8_t> info;
};
struct Message {
    Kind kind = Kind::Hello;
    std::uint64_t room = 0;
    std::uint32_t peer = 0, successor = 0;
    std::vector<std::uint8_t> data;
    std::vector<Room> rooms;
};
std::vector<std::uint8_t> encode(const Message&);
std::optional<Message> decode(std::span<const std::uint8_t>);
}  // namespace o1602::net::master
