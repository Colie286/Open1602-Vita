// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cheats as in the original: Ctrl+Shift+Alt+W opens a line for a number (up to 16 digits),
// Enter closes it and keeps the number. Then "a" takes the number: 2061 switches the cheats on or
// off, 512 on also in a game of more than one person; with the cheats on, 0 gives 500 gold and a
// ware's number 5 t of that ware. With the cheats on also Shift+M (500 gold), Shift+Z, Shift+T and
// Shift+H (5 t of bricks, tools, wood), Shift+K (a cannon for the selected ship, else 5 t of
// cannons) and "v" (the volcano under the mouse erupts). Everything goes to the selected ship,
// else to the city under the mouse, whoever's it is. The orders are commands (sim::CheatOrder):
// every computer of a game carries them out alike, and Rules::cheats decides whether they may.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "o1602/sim/simulation.hpp"

namespace o1602::app {

class CheatConsole {
public:
    static constexpr std::size_t kMaxDigits = 16;

    /// The keys the console knows; the input translates the platform's.
    enum class Key : std::uint8_t {
        Open,   ///< Ctrl+Shift+Alt+W
        Digit,  ///< 0..9, `digit`
        Backspace,
        Enter,
        Escape,
        A,  ///< "a" without Shift, Ctrl or Alt
        V,  ///< "v" without Shift, Ctrl or Alt
        ShiftM,
        ShiftZ,
        ShiftT,
        ShiftH,
        ShiftK,
        Other,  ///< any other key
    };

    /// Where the cheats go: the selected ship, else the city of the tile under the mouse.
    struct Target {
        std::optional<std::uint32_t> ship;
        std::uint32_t x = 0;
        std::uint32_t y = 0;
    };

    /// A key: whether the console took it (it is then nobody else's), and the order it gives, if
    /// any. `cheats` says whether the player may cheat now (sim::Simulation::cheats_allowed).
    struct Result {
        bool taken = false;
        std::optional<sim::CheatOrder> order;
    };
    Result key(Key key, char digit, bool cheats, sim::PlayerId player, const Target& target);

    /// Whether the line is open for a number (drawn at the bottom left of the map, keys go to it).
    [[nodiscard]] bool open() const noexcept { return m_open; }
    /// The number typed.
    [[nodiscard]] const std::string& line() const noexcept { return m_line; }

private:
    bool m_open = false;
    std::string m_line;
};

}  // namespace o1602::app
