// SPDX-License-Identifier: GPL-3.0-or-later
//
// A fast 64-bit hash of bytes that every platform computes the same way, for fingerprints compared
// between computers (the multiplayer game's state, Simulation::state_hash()). Not for savegame
// formats: those keep the hashes their documents name (FNV-1a, CRC-32).
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace o1602::core {

/// XXH64 (xxHash, 64 bit) of `data`: the bytes are read as little-endian words on every platform,
/// so the result does not depend on the processor. Several gigabytes a second, against a few
/// hundred megabytes for FNV-1a, which takes the bytes one by one.
[[nodiscard]] std::uint64_t xxh64(std::span<const std::uint8_t> data, std::uint64_t seed = 0) noexcept;

/// SHA-256 (FIPS 180-4) of `data`. One way, unlike XXH64: for what may be shown without giving
/// away what it was made from (the verifiers of the multiplayer slots' tokens). Not fast.
[[nodiscard]] std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) noexcept;

}  // namespace o1602::core
