// SPDX-License-Identifier: GPL-3.0-or-later
//
// Random numbers of the simulation (ADR 0004). The original draws everything from the C library's
// rand(), shared with presentation code that only runs for what is on screen (M-006), so two
// computers diverge. Open1602 gives each subsystem its own generator, kept in the world state:
// the same world gives the same game on every computer, and drawing for one subsystem never
// changes another one.
#pragma once

#include <array>
#include <cstdint>

namespace o1602::sim {

/// PCG32 (O'Neill): small, fast, good statistics, and fully specified, so results are the same
/// with every compiler and standard library.
class Random {
public:
    constexpr Random() noexcept : Random(0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL) {}
    constexpr Random(std::uint64_t seed, std::uint64_t stream) noexcept : m_increment((stream << 1U) | 1U) {
        next();
        m_state += seed;
        next();
    }

    constexpr std::uint32_t next() noexcept {
        const std::uint64_t old = m_state;
        m_state = old * 6364136223846793005ULL + m_increment;
        const auto shifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
        const auto rot = static_cast<std::uint32_t>(old >> 59U);
        return (shifted >> rot) | (shifted << ((32U - rot) & 31U));
    }

    /// Uniform in [0, n) without modulo bias; 0 for n = 0.
    constexpr std::uint32_t below(std::uint32_t n) noexcept {
        if (n == 0) {
            return 0;
        }
        const std::uint32_t limit = (0U - n) % n;  // values below this would favour small results
        for (;;) {
            const std::uint32_t r = next();
            if (r >= limit) {
                return r % n;
            }
        }
    }

    /// A generator continuing from a saved state.
    [[nodiscard]] static constexpr Random from_state(std::uint64_t state, std::uint64_t increment) noexcept {
        Random r;
        r.m_state = state;
        r.m_increment = increment | 1U;
        return r;
    }

    [[nodiscard]] constexpr std::uint64_t state() const noexcept { return m_state; }
    [[nodiscard]] constexpr std::uint64_t increment() const noexcept { return m_increment; }
    friend constexpr bool operator==(const Random&, const Random&) = default;

private:
    std::uint64_t m_state = 0;
    std::uint64_t m_increment = 1;
};

/// One generator per subsystem.
enum class RandomStream : std::uint8_t {
    Houses,        ///< house variants on upgrades
    Events,        ///< fires, plague, robbers
    Animals,       ///< deer
    Pirates,       ///< what pirates take
    Construction,  ///< random bits of new buildings (house timer group, growth class), ruins
    Islands,       ///< droughts and the fields they wither
    Ships,         ///< courses, the free trader, ship names
    Military,      ///< soldiers wandering, plans after a failure
    Ai,            ///< the computer players (house and field variants, ship types, patrols)
    Count,
};

/// Seed of the simulation's generators when a game names none.
inline constexpr std::uint64_t kDefaultSimulationSeed = 1602;

/// The generators of a world, all derived from one seed.
class RandomStreams {
public:
    explicit constexpr RandomStreams(std::uint64_t seed = kDefaultSimulationSeed) noexcept { reseed(seed); }
    constexpr void reseed(std::uint64_t seed) noexcept {
        for (std::size_t i = 0; i < m_streams.size(); ++i) {
            m_streams[i] = Random(seed, 0x5DEECE66DULL + i);
        }
    }
    [[nodiscard]] constexpr Random& operator[](RandomStream s) noexcept {
        return m_streams[static_cast<std::size_t>(s)];
    }
    [[nodiscard]] constexpr const Random& operator[](RandomStream s) const noexcept {
        return m_streams[static_cast<std::size_t>(s)];
    }

private:
    std::array<Random, static_cast<std::size_t>(RandomStream::Count)> m_streams;
};

}  // namespace o1602::sim
