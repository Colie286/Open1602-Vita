// SPDX-License-Identifier: GPL-3.0-or-later
//
// The players' score sheets (Screen_PlayerStatus; ENDE.GAD, docs/re/ui.md "Endbildschirm"): after
// a game is decided, and from the info panel's "Bewertung" while it runs. The labels are part of
// the background picture; the screen fills in a player's figures and points, the total, the name
// and the coat of arms, and offers the coats of arms of the other players to look at theirs.
//
// The original has a coat of arms for each of its four colours. A game with more players, or with
// players in further colours, gets a strip instead (Open1602): the gadgets of those coats of arms
// show the players in the order of their points, the rank under each, with the scroll arrows of
// TRANSFER.GAD on both sides when there are more players than gadgets, and the shown player's rank
// under the name.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "high_score_file.hpp"

#include "o1602/assets/gad.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/sim/score.hpp"
#include "o1602/ui/panel.hpp"

namespace o1602::app {

class EndScreen {
public:
    /// What the player chose.
    enum class Choice : std::uint8_t {
        MainMenu,  ///< "Hauptmenü"
        NewGame,   ///< "Neues Spiel": the same scenario's mission screen (single player)
        Resume,    ///< Escape: back to the game (the sheet while it runs; Open1602 after a win)
    };

    /// `ende`: the layout ENDE.GAD, nothing if the installation lacks it (the screen is black).
    /// `transfer`: TRANSFER.GAD, whose scroll arrows page through the strip of players; without
    /// it the strip pages with the keys only.
    explicit EndScreen(std::optional<assets::GadgetLayout> ende, const assets::GadgetLayout* transfer = nullptr);

    /// Shows a decided game (`view_only` false: the buttons, no way back) or the sheets of a
    /// running one (Escape goes back). Starts with the local player's sheet.
    void open(sim::EndScreenSummary summary, bool view_only);
    void set_scale(double scale);
    /// How players are shown (their coats of arms); kept, not copied. Without: the white arms.
    void set_banners(const render::BannerTable* banners) noexcept { m_banners = banners; }

    [[nodiscard]] double scale() const noexcept { return m_scale; }

    /// Everything to draw over the whole 1024 x 768 design, relative to its top left, in pixels
    /// at scale().
    [[nodiscard]] std::vector<ui::DrawItem> draw(const std::array<const ui::Font*, ui::kFontSlots>& fonts) const;
    /// The mouse relative to the design's top left, in pixels.
    std::optional<Choice> mouse(std::int32_t x, std::int32_t y, bool down);
    /// Escape: back to a running game, or into a won one (Open1602: the original has no keys on
    /// its final screen and ends the game).
    [[nodiscard]] std::optional<Choice> back() const noexcept;
    /// Left and right: the sheet of the previous or next player (Open1602: ENDE.GAD has coats of
    /// arms for four colours, a game may have twelve players). In the strip, the next by points.
    void step(int direction);

    [[nodiscard]] const sim::EndScreenSummary& summary() const noexcept { return m_summary; }
    [[nodiscard]] bool view_only() const noexcept { return m_view_only; }
    /// The row whose sheet is shown.
    [[nodiscard]] std::size_t shown_row() const noexcept { return m_row; }
    /// Whether the coats of arms are the strip of all players rather than one per colour.
    [[nodiscard]] bool strip() const noexcept { return m_strip; }
    /// The strip: the rows in the order of their points (most first, ties in the order of the rows),
    /// and the place of the first one its gadgets show.
    [[nodiscard]] const std::vector<std::size_t>& strip_order() const noexcept { return m_order; }
    [[nodiscard]] std::size_t strip_first() const noexcept { return m_first; }
    /// The rank of a row by its points, from 1; rows with equal points share one.
    [[nodiscard]] std::size_t rank(std::size_t row) const noexcept { return row < m_rank.size() ? m_rank[row] : 0; }

private:
    void show(std::size_t row);
    /// Scrolls the strip so that its first gadget shows place `first` (clamped).
    void scroll_strip(std::size_t first);
    /// Fills the strip's gadgets and arrows from m_first.
    void update_strip();
    /// The rank texts the strip and the name get (the panel has no gadgets for them).
    void draw_ranks(std::vector<ui::DrawItem>& out) const;

    std::optional<ui::Panel> m_panel;
    double m_scale = 1.0;
    std::int32_t m_arms = 0;          ///< sprite of the coat of arms of colour 0 (gadget 36005)
    std::int32_t m_button_arms = 0;   ///< sprite of the first coat of arms to choose (gadget 36051)
    std::int32_t m_button_press = 0;  ///< its step to the chosen one
    std::size_t m_slots = 0;          ///< coats of arms to choose a player in the layout
    bool m_arrows = false;            ///< the strip's scroll arrows are in the panel
    const render::BannerTable* m_banners = nullptr;
    sim::EndScreenSummary m_summary;
    std::size_t m_row = 0;  ///< the row shown
    bool m_view_only = false;
    /// The row of each colour's button, nothing for a colour nobody plays.
    std::array<std::optional<std::size_t>, 4> m_by_colour{};
    bool m_strip = false;
    std::vector<std::size_t> m_order;  ///< the strip: rows by points
    std::vector<std::size_t> m_rank;   ///< per row
    std::size_t m_first = 0;           ///< the strip's place its first gadget shows
};

}  // namespace o1602::app
