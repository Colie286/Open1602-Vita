// SPDX-License-Identifier: GPL-3.0-or-later
//
// The start dialog of a new game (ADR 0008): Open1602's own screen, the original has none. The
// settings in groups (economy, limits, disasters, world, opponents), each a button with its value;
// a left click chooses the next value, a right click the previous one. Presets for the original's
// rules and Open1602's defaults. The victory conditions of an endless game and the bugs of the
// original a game can bring back ("Original settings") have a page of their own each, which a
// button of the first opens.
//
// One dialog for every kind of game: the endless game after the name and the banner, a scenario
// after its mission screen, and the multiplayer menu, where the host chooses and the others look
// at the host's choice. What is played (SetupScope) decides which items it shows.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "endless_setup.hpp"

#include "o1602/ui/form.hpp"

namespace o1602::app {

class GameSettingsMenu {
public:
    enum class Result : std::uint8_t { None, Start, Back };
    /// What the button at the bottom right does.
    enum class Mode : std::uint8_t {
        Start,  ///< starts the game ("Spiel starten")
        Apply,  ///< takes the settings for a game being set up ("Übernehmen"); Result::Start
        View,   ///< the settings can only be looked at: no presets, no button but "Back"
    };

    explicit GameSettingsMenu(ui::FormStyle style = {}) : m_form(style), m_font(style.font) {}

    void set_style(const ui::FormStyle& style) {
        m_form.set_style(style);
        m_font = style.font;
    }
    void set_scale(double scale) { m_form.set_scale(scale); }
    void open(const EndlessSetup& setup, SetupScope scope = SetupScope::Endless, Mode mode = Mode::Start) {
        m_setup = setup;
        m_scope = scope;
        m_mode = scope == SetupScope::Savegame ? Mode::View : mode;
        m_page = Page::Settings;
        m_error.clear();
    }
    /// Shows `text` in place of the hint: why the game did not start. A click on "Start" takes it
    /// away again.
    void set_error(std::string text) { m_error = std::move(text); }
    [[nodiscard]] const std::string& error() const noexcept { return m_error; }
    [[nodiscard]] const EndlessSetup& setup() const noexcept { return m_setup; }
    [[nodiscard]] SetupScope scope() const noexcept { return m_scope; }
    [[nodiscard]] Mode mode() const noexcept { return m_mode; }
    /// Whether the page of the victory conditions is open.
    [[nodiscard]] bool victory_page() const noexcept { return m_page == Page::Victory; }
    /// Whether the page of the original's bugs ("Original settings") is open.
    [[nodiscard]] bool bugs_page() const noexcept { return m_page == Page::Bugs; }
    /// Escape or a right click: from the victory conditions or the original's bugs back to the
    /// settings; true if it went there, false if the dialog itself is to close.
    bool back() noexcept;

    /// Everything to draw, relative to the 1024 x 768 design's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    Result mouse(std::int32_t x, std::int32_t y, bool down, ui::MouseButton button = ui::MouseButton::Left);

private:
    enum class Page : std::uint8_t { Settings, Victory, Bugs };
    void layout();
    /// A preset with what the dialog does not decide (the players of a multiplayer game) kept.
    [[nodiscard]] EndlessSetup keep_decided(EndlessSetup preset) const;

    ui::Form m_form;
    std::int32_t m_font = 0;                 ///< the form's font slot
    const ui::Font* m_label_font = nullptr;  ///< of the last draw: the labels' column fits the longest
    EndlessSetup m_setup;
    SetupScope m_scope = SetupScope::Endless;
    Mode m_mode = Mode::Start;
    Page m_page = Page::Settings;
    std::string m_error;
};

}  // namespace o1602::app
