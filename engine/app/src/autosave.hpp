// SPDX-License-Identifier: GPL-3.0-or-later
//
// When the game is kept by itself (DisplaySettings::autosave_minutes): every so many minutes of
// game time since the game began, was loaded, was last kept or the interval was changed. Game
// time, not real time: a paused game is not saved again and again, a fast one more often. Pure
// logic; the viewer saves.
#pragma once

#include <cstdint>

namespace o1602::app {

class AutosaveTimer {
public:
    /// Another game (started or loaded) at `game_ms`: the interval counts from there.
    void restart(std::uint64_t game_ms) noexcept { m_since = game_ms; }

    /// Whether the game is due to be kept at `game_ms` with an interval of `minutes` (0: never). A
    /// changed interval counts from now; so does a game whose time went back (another game came
    /// without restart()). True only once per interval: the next counts from `game_ms`.
    [[nodiscard]] bool due(std::uint64_t game_ms, std::int32_t minutes) noexcept {
        if (minutes != m_minutes || game_ms < m_since) {
            m_minutes = minutes;
            m_since = game_ms;
            return false;
        }
        if (minutes <= 0 || game_ms - m_since < static_cast<std::uint64_t>(minutes) * 60'000U) {
            return false;
        }
        m_since = game_ms;
        return true;
    }

private:
    std::uint64_t m_since = 0;
    std::int32_t m_minutes = -1;  ///< the interval last asked for (-1: none yet)
};

}  // namespace o1602::app
