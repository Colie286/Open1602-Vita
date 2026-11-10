// SPDX-License-Identifier: GPL-3.0-or-later
//
// XXH64 as specified in xxHash's doc/xxhash_spec.md (Yann Collet, BSD-2-Clause); SHA-256 as in
// FIPS 180-4.
#include "o1602/core/hash.hpp"

#include <bit>
#include <cstddef>
#include <cstring>

namespace o1602::core {

namespace {

constexpr std::uint64_t kPrime1 = 0x9E3779B185EBCA87ULL;
constexpr std::uint64_t kPrime2 = 0xC2B2AE3D27D4EB4FULL;
constexpr std::uint64_t kPrime3 = 0x165667B19E3779F9ULL;
constexpr std::uint64_t kPrime4 = 0x85EBCA77C2B2AE63ULL;
constexpr std::uint64_t kPrime5 = 0x27D4EB2F165667C5ULL;

/// The little-endian number of `sizeof(T)` bytes at `p`: one load on a little-endian processor.
template <class T>
T read_le(const std::uint8_t* p) noexcept {
    T v = 0;
    if constexpr (std::endian::native == std::endian::little) {
        std::memcpy(&v, p, sizeof v);
    } else {
        for (std::size_t k = 0; k < sizeof v; ++k) {
            v |= static_cast<T>(T{p[k]} << (8 * k));
        }
    }
    return v;
}

std::uint64_t read64(const std::uint8_t* p) noexcept {
    return read_le<std::uint64_t>(p);
}
std::uint32_t read32(const std::uint8_t* p) noexcept {
    return read_le<std::uint32_t>(p);
}

std::uint64_t mix_lane(std::uint64_t acc, std::uint64_t input) noexcept {
    acc += input * kPrime2;
    acc = std::rotl(acc, 31);
    return acc * kPrime1;
}

std::uint64_t merge(std::uint64_t acc, std::uint64_t lane) noexcept {
    acc ^= mix_lane(0, lane);
    return acc * kPrime1 + kPrime4;
}

}  // namespace

std::uint64_t xxh64(std::span<const std::uint8_t> data, std::uint64_t seed) noexcept {
    const std::uint8_t* p = data.data();
    std::size_t n = data.size();
    std::uint64_t h = 0;
    if (n >= 32) {
        // Four lanes side by side, 32 bytes a step.
        std::uint64_t v1 = seed + kPrime1 + kPrime2;
        std::uint64_t v2 = seed + kPrime2;
        std::uint64_t v3 = seed;
        std::uint64_t v4 = seed - kPrime1;
        do {
            v1 = mix_lane(v1, read64(p));
            v2 = mix_lane(v2, read64(p + 8));
            v3 = mix_lane(v3, read64(p + 16));
            v4 = mix_lane(v4, read64(p + 24));
            p += 32;
            n -= 32;
        } while (n >= 32);
        h = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
        h = merge(h, v1);
        h = merge(h, v2);
        h = merge(h, v3);
        h = merge(h, v4);
    } else {
        h = seed + kPrime5;
    }
    h += data.size();
    for (; n >= 8; p += 8, n -= 8) {
        h ^= mix_lane(0, read64(p));
        h = std::rotl(h, 27) * kPrime1 + kPrime4;
    }
    if (n >= 4) {
        h ^= std::uint64_t{read32(p)} * kPrime1;
        h = std::rotl(h, 23) * kPrime2 + kPrime3;
        p += 4;
        n -= 4;
    }
    for (; n > 0; ++p, --n) {
        h ^= std::uint64_t{*p} * kPrime5;
        h = std::rotl(h, 11) * kPrime1;
    }
    h ^= h >> 33;
    h *= kPrime2;
    h ^= h >> 29;
    h *= kPrime3;
    h ^= h >> 32;
    return h;
}

namespace {

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void sha256_block(std::array<std::uint32_t, 8>& h, const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = std::uint32_t{block[4 * i]} << 24 | std::uint32_t{block[4 * i + 1]} << 16 |
               std::uint32_t{block[4 * i + 2]} << 8 | std::uint32_t{block[4 * i + 3]};
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::array<std::uint32_t, 8> v = h;
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = std::rotr(v[4], 6) ^ std::rotr(v[4], 11) ^ std::rotr(v[4], 25);
        const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const std::uint32_t t1 = v[7] + s1 + ch + kSha256K[i] + w[i];
        const std::uint32_t s0 = std::rotr(v[0], 2) ^ std::rotr(v[0], 13) ^ std::rotr(v[0], 22);
        const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const std::uint32_t t2 = s0 + maj;
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (std::size_t i = 0; i < 8; ++i) {
        h[i] += v[i];
    }
}

}  // namespace

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) noexcept {
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::size_t i = 0;
    for (; i + 64 <= data.size(); i += 64) {
        sha256_block(h, data.data() + i);
    }
    // The rest, a 1 bit, zeros and the length in bits: one block or two.
    std::array<std::uint8_t, 128> tail{};
    const std::size_t rest = data.size() - i;
    if (rest != 0) {
        std::memcpy(tail.data(), data.data() + i, rest);
    }
    tail[rest] = 0x80;
    const std::size_t blocks = rest < 56 ? 1 : 2;
    const std::uint64_t bits = std::uint64_t{data.size()} * 8;
    for (std::size_t k = 0; k < 8; ++k) {
        tail[blocks * 64 - 1 - k] = static_cast<std::uint8_t>(bits >> (8 * k));
    }
    for (std::size_t b = 0; b < blocks; ++b) {
        sha256_block(h, tail.data() + 64 * b);
    }
    std::array<std::uint8_t, 32> out{};
    for (std::size_t k = 0; k < 8; ++k) {
        out[4 * k] = static_cast<std::uint8_t>(h[k] >> 24);
        out[4 * k + 1] = static_cast<std::uint8_t>(h[k] >> 16);
        out[4 * k + 2] = static_cast<std::uint8_t>(h[k] >> 8);
        out[4 * k + 3] = static_cast<std::uint8_t>(h[k]);
    }
    return out;
}

}  // namespace o1602::core
