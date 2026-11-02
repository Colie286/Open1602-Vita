// SPDX-License-Identifier: GPL-3.0-or-later
//
// The chat of a running multiplayer game (ADR 0011 point 17), Open1602's own: a window over the
// lower left of the map with the lines so far, the players to write to and the line being typed.
// While it is closed, new lines show over the map for a while. The chat key (Enter) opens it; Enter
// sends, Escape or Enter on an empty line closes it. "All" writes to everyone; any choice of the
// other persons in the game writes to them privately (the host passes such a line on to them only).
// The window only edits what is typed and to whom; the viewer sends the lines it gives back.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "multiplayer_menu.hpp"

#include "o1602/sim/player.hpp"
#include "o1602/ui/form.hpp"

namespace o1602::app {

/// Another person in the game, to write to privately.
struct ChatPartner {
    sim::PlayerId seat = sim::kNoPlayer;
    std::string name;
    sim::BannerId banner = 0;
};

/// What the chat shows besides its lines; the viewer keeps it up to date.
struct ChatModel {
    bool available = false;  ///< a multiplayer game runs
    bool may_write = true;   ///< off: a spectator in a game whose spectators only read
    /// The other persons in slots, by slot; empty for a spectator, who writes to everyone only.
    std::vector<ChatPartner> partners;
};

/// A line to send: to everyone (`to` empty) or privately to the players in `to`.
struct ChatSay {
    std::string text;
    std::vector<sim::PlayerId> to;
};

class ChatWindow {
public:
    enum class Result : std::uint8_t { None, Close };
    static constexpr std::int32_t kWidth = 600;
    /// Lines of the window's history.
    static constexpr std::int32_t kHistoryLines = 8;
    /// How long a line shows over the map while the window is closed (real time), and how many.
    static constexpr std::uint64_t kRecentMs = 10'000;
    static constexpr std::size_t kRecentLines = 5;

    explicit ChatWindow(ui::FormStyle style = {}) : m_style(style), m_form(style) {}

    void set_style(const ui::FormStyle& style) {
        m_style = style;
        m_form.set_style(style);
    }
    void set_scale(double scale) { m_form.set_scale(scale); }
    [[nodiscard]] double scale() const noexcept { return m_form.scale(); }
    /// The palette colours of the banners by id (render::BannerTable), `other` for any beyond them.
    void set_banners(std::vector<std::int32_t> colours, std::uint8_t other) {
        m_banner_colours = std::move(colours);
        m_other_banner = other;
    }

    void open();
    void close() noexcept { m_open = false; }
    [[nodiscard]] bool is_open() const noexcept { return m_open; }
    /// The window's height in design units: the more players to choose from, the taller.
    [[nodiscard]] std::int32_t height(const ChatModel& model) const;
    /// The players chosen, ascending; empty: everyone.
    [[nodiscard]] const std::vector<sim::PlayerId>& chosen() const noexcept { return m_to; }
    [[nodiscard]] const std::string& text() const noexcept { return m_text; }

    /// Everything to draw, relative to the window's top left (design units).
    [[nodiscard]] std::vector<ui::DrawItem> draw(const ChatModel& model, const std::deque<ChatLine>& lines,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    /// The lines of the last kRecentMs over the map, for when the window is closed: their bottom
    /// left at (0, 0), so they go upwards (design units). Nothing if none is that new.
    [[nodiscard]] std::vector<ui::DrawItem> draw_recent(const std::deque<ChatLine>& lines, std::uint64_t now_ms,
                                                        const std::array<const ui::Font*, ui::kFontSlots>& fonts) const;
    /// The mouse in pixels relative to the window's top left: "All", a player or "Close".
    Result mouse(std::int32_t x, std::int32_t y, bool down, const ChatModel& model);
    /// Text typed (Windows-1252), cut to net::kMaxChatLength.
    void type(std::string_view text);
    /// Backspace 8; Enter 13 sends the line (or closes the window if it is empty); Escape 27 closes.
    std::optional<ChatSay> key(const ChatModel& model, int key);

private:
    void layout(const ChatModel& model, const std::deque<ChatLine>* lines,
                const std::array<const ui::Font*, ui::kFontSlots>* fonts);
    /// Drops players from the choice who are no longer there.
    void prune(const ChatModel& model);
    [[nodiscard]] std::int32_t banner_colour(sim::BannerId banner) const noexcept {
        return banner < m_banner_colours.size() ? m_banner_colours[banner] : m_other_banner;
    }
    /// The top of the history: below the row(s) of players.
    [[nodiscard]] static std::int32_t history_top(const ChatModel& model);

    ui::FormStyle m_style;
    ui::Form m_form;
    std::vector<std::int32_t> m_banner_colours;  ///< palette colours or ui::rgb_ink
    std::uint8_t m_other_banner = 0;
    bool m_open = false;
    std::vector<sim::PlayerId> m_to;
    std::string m_text;
};

}  // namespace o1602::app
