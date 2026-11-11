// SPDX-License-Identifier: GPL-3.0-or-later
#include "cheat_console.hpp"

#include "o1602/assets/game_data.hpp"

namespace o1602::app {

namespace {

sim::CheatOrder order_for(sim::PlayerId player, sim::CheatOrder::Op op, const CheatConsole::Target& target) {
    sim::CheatOrder o;
    o.player = player;
    o.op = op;
    o.ship = target.ship;
    o.x = target.x;
    o.y = target.y;
    return o;
}

sim::CheatOrder goods(sim::PlayerId player, std::uint8_t ware, const CheatConsole::Target& target) {
    sim::CheatOrder o = order_for(player, sim::CheatOrder::Op::Goods, target);
    o.ware = ware;
    return o;
}

/// The number of the line; nothing if it is empty or too large for the codes and wares.
std::optional<std::uint32_t> number(const std::string& line) {
    if (line.empty() || line.size() > 9) {
        return std::nullopt;
    }
    std::uint32_t n = 0;
    for (const char c : line) {
        n = n * 10 + static_cast<std::uint32_t>(c - '0');
    }
    return n;
}

}  // namespace

CheatConsole::Result CheatConsole::key(Key key, char digit, bool cheats, sim::PlayerId player, const Target& target) {
    using Op = sim::CheatOrder::Op;
    if (key == Key::Open) {
        m_open = true;
        m_line.clear();
        return {true, std::nullopt};
    }
    if (m_open) {
        // The line takes every key while it is open.
        switch (key) {
            case Key::Digit:
                if (digit >= '0' && digit <= '9' && m_line.size() < kMaxDigits) {
                    m_line.push_back(digit);
                }
                break;
            case Key::Backspace:
                if (!m_line.empty()) {
                    m_line.pop_back();
                }
                break;
            case Key::Enter:
                m_open = false;  // the number stays for "a"
                break;
            case Key::Escape:
                m_open = false;
                m_line.clear();
                break;
            default:
                break;
        }
        return {true, std::nullopt};
    }
    switch (key) {
        case Key::A: {
            const auto n = number(m_line);
            if (!n) {
                return {};
            }
            if (*n == sim::CheatOrder::kCode || *n == sim::CheatOrder::kAlwaysCode) {
                sim::CheatOrder o = order_for(player, Op::Code, target);
                o.code = *n;
                return {true, o};
            }
            if (!cheats || *n > 255) {
                return {};
            }
            if (*n == 0) {
                return {true, order_for(player, Op::Gold, target)};
            }
            return {true, goods(player, static_cast<std::uint8_t>(*n), target)};
        }
        case Key::V:
            return cheats ? Result{true, order_for(player, Op::Volcano, target)} : Result{};
        case Key::ShiftM:
            return cheats ? Result{true, order_for(player, Op::Gold, target)} : Result{};
        case Key::ShiftZ:
            return cheats ? Result{true, goods(player, assets::ware::kBricks, target)} : Result{};
        case Key::ShiftT:
            return cheats ? Result{true, goods(player, assets::ware::kTools, target)} : Result{};
        case Key::ShiftH:
            return cheats ? Result{true, goods(player, assets::ware::kWood, target)} : Result{};
        case Key::ShiftK:
            if (!cheats) {
                return {};
            }
            // A cannon for the selected ship, else 5 t of cannons into the city.
            return {true, target.ship ? order_for(player, Op::Cannon, target)
                                      : goods(player, assets::ware::kCannons, target)};
        default:
            return {};
    }
}

}  // namespace o1602::app
