// SPDX-License-Identifier: GPL-3.0-or-later
//
// The start dialog of a new game (ADR 0008): Open1602's own screen, the original has none. The
// settings in groups (economy, limits, disasters, world, opponents), each a button with its value;
// a left click chooses the next value, a right click the previous one. Presets for the original's
// rules and Open1602's defaults. The victory conditions of an endless game, the bugs of the
// original a game can bring back ("Original settings"), the disasters, Open1602's balance changes
// and the islands of a world of its own have a page of their own each, which a button of the
// first opens. The page of the islands shows the world the settings make (sim::preview_world),
// drawn in the background.
//
// One dialog for every kind of game: the endless game after the name and the banner, a scenario
// after its mission screen, and the multiplayer menu, where the host chooses and the others look
// at the host's choice. What is played (SetupScope) decides which items it shows.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "endless_setup.hpp"

#include "o1602/core/result.hpp"
#include "o1602/sim/new_game.hpp"
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

    /// Makes the world of a setup for the preview, `level` the difficulty (0..3); called on a thread
    /// of its own, one call at a time.
    using PreviewMaker = std::function<core::Result<sim::WorldPreview>(const EndlessSetup& setup, std::uint8_t level)>;
    /// Palette colours of the preview: one per sim::WorldPreview::Tile, then the ships' starts.
    using PreviewColours = std::array<std::uint8_t, sim::WorldPreview::kTiles + 1>;

    explicit GameSettingsMenu(ui::FormStyle style = {}) : m_form(style), m_font(style.font) {}
    ~GameSettingsMenu();
    GameSettingsMenu(const GameSettingsMenu&) = delete;
    GameSettingsMenu& operator=(const GameSettingsMenu&) = delete;

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
        m_reroll = true;
        m_tip_key.clear();
    }
    /// Whether "Another world" may draw a new world number (after open(); a lobby's world keeps
    /// the number the host chose for it).
    void allow_reroll(bool allowed) noexcept { m_reroll = allowed; }
    /// What makes the preview of the page of the islands; without one the page has none.
    void set_preview_maker(PreviewMaker maker) { m_maker = std::move(maker); }
    void set_preview_colours(const PreviewColours& colours) noexcept { m_colours = colours; }
    /// The difficulty the game is started with (the original's maps differ by it).
    void set_level(std::uint8_t level) noexcept { m_level = level; }
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
    /// Whether the page of the islands is open.
    [[nodiscard]] bool islands_page() const noexcept { return m_page == Page::Islands; }
    /// Whether the page of the disasters is open.
    [[nodiscard]] bool disasters_page() const noexcept { return m_page == Page::Disasters; }
    /// Whether the page of Open1602's balance changes is open.
    [[nodiscard]] bool balance_page() const noexcept { return m_page == Page::Balance; }
    /// The world of the preview, when it is made (for the tests: wait_preview() waits for it).
    [[nodiscard]] const std::optional<sim::WorldPreview>& preview() const noexcept { return m_preview; }
    void wait_preview();
    /// How long the mouse rests on a setting or a preset before its explanation shows (0 for the
    /// tests).
    void set_tooltip_delay(std::chrono::milliseconds delay) noexcept { m_tip_delay = delay; }
    /// The localisation key of the explanation shown at the last draw (the key of the setting's
    /// label with ".tip"), "" when none is shown.
    [[nodiscard]] const std::string& tooltip() const noexcept { return m_tip_shown; }
    /// Escape or a right click: from the victory conditions or the original's bugs back to the
    /// settings; true if it went there, false if the dialog itself is to close.
    bool back() noexcept;

    /// Everything to draw, relative to the 1024 x 768 design's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    Result mouse(std::int32_t x, std::int32_t y, bool down, ui::MouseButton button = ui::MouseButton::Left);

private:
    enum class Page : std::uint8_t { Settings, Victory, Bugs, Islands, Disasters, Balance };
    /// A row of the preview's picture in one colour.
    struct Run {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t w = 0;
        std::uint8_t colour = 0;
    };
    /// What a setting or a preset is explained by: where the mouse rests on it, and its key.
    struct Tip {
        ui::Rect area;
        std::string key;
    };
    void layout();
    /// The explanation of what the mouse rests on, laid out over everything (after layout()).
    void layout_tooltip();
    void layout_preview(std::int32_t x, std::int32_t y);
    /// Starts making the preview of the setup shown, if it is not the one made or being made;
    /// takes a finished one.
    void update_preview();
    void picture(const sim::WorldPreview& p);
    /// A preset with what the dialog does not decide (the players of a multiplayer game) kept.
    [[nodiscard]] EndlessSetup keep_decided(EndlessSetup preset) const;
    /// A new world number: any 64 bits, in a multiplayer game one the host can type (as its
    /// "world number" field takes).
    [[nodiscard]] std::uint64_t new_seed() const;

    ui::Form m_form;
    std::int32_t m_font = 0;                 ///< the form's font slot
    const ui::Font* m_label_font = nullptr;  ///< of the last draw: the labels' column fits the longest
    EndlessSetup m_setup;
    SetupScope m_scope = SetupScope::Endless;
    Mode m_mode = Mode::Start;
    Page m_page = Page::Settings;
    std::string m_error;

    std::vector<Tip> m_tips;                                       ///< of the last layout
    std::optional<std::pair<std::int32_t, std::int32_t>> m_mouse;  ///< design units
    std::string m_tip_key;                                         ///< the tip under the mouse, "" for none
    std::chrono::steady_clock::time_point m_tip_since;             ///< since when the mouse rests on it
    std::chrono::milliseconds m_tip_delay{500};
    bool m_tip_dismissed = false;  ///< a click on it hides it until the mouse leaves it
    std::string m_tip_shown;

    PreviewMaker m_maker;
    PreviewColours m_colours{};
    std::uint8_t m_level = 0;
    bool m_reroll = true;
    std::string m_preview_key;  ///< the setup of the preview made or being made
    std::future<core::Result<sim::WorldPreview>> m_pending;
    std::optional<sim::WorldPreview> m_preview;
    std::string m_preview_error;
    std::vector<Run> m_runs;  ///< the picture, design units relative to its top left
    std::int32_t m_picture_w = 0;
    std::int32_t m_picture_h = 0;
};

}  // namespace o1602::app
