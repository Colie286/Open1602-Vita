// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window "Anzeige" (display) over the map, Open1602's own: window or full screen, the window's
// size, the interface's size, the zoom and how it draws, vertical sync. Opened from the settings of
// the side panel, and in the main menu by the History Edition's "Options"; each row is a button
// with its value, a left click chooses the next one, a right click the previous one. The window
// only edits a DisplayModel; the viewer applies what changed.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "display_settings.hpp"

#include "o1602/ui/form.hpp"

namespace o1602::app {

struct DisplayModel {
    bool chat_available = false;
    bool fixed_window = false;  ///< handheld: no desktop mode/size/grab controls
    DisplaySettings settings;
    WindowSize window;              ///< the window's size now (window coordinates)
    std::vector<WindowSize> sizes;  ///< the sizes offered (window_sizes_for)
};

class DisplayMenu {
public:
    /// Keys: the button "Tastenbelegung" opens the window of the keys (keys_menu.hpp).
    enum class Result : std::uint8_t { None, Changed, Close, Keys };
    /// The rows, top to bottom.
    enum class Item : std::uint8_t {
        Mode,
        WindowSize,
        UiScale,
        Zoom,
        ZoomDrawing,
        Vsync,
        EdgeScroll,
        EdgeSpeed,
        GrabMouse,
        GoodsTrend,
        RivalWars,
        EventSpeed,
        PlantFields,
        Graphics,
        Autosave,
        VitaCpu,
        LeftStick,
        RightStick,
        ChatButton,
        BlueprintMode,
        TouchMoveBuildings
    };
    static constexpr std::size_t kItems = 21;
    /// The window's size in design units (it is centred over the map).
    static constexpr std::int32_t kWidth = 640;
    static constexpr std::int32_t kHeight = 756;  // fifteen rows, the hint and the button within 768

    explicit DisplayMenu(ui::FormStyle style = {}) : m_form(style) {}

    void set_style(const ui::FormStyle& style) { m_form.set_style(style); }
    void set_scale(double scale) { m_form.set_scale(scale); }
    [[nodiscard]] double scale() const noexcept { return m_form.scale(); }

    /// Everything to draw, relative to the window's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const DisplayModel& model,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    /// The mouse in pixels relative to the window's top left; a click changes `model`.
    Result mouse(std::int32_t x, std::int32_t y, bool down, DisplayModel& model,
                 ui::MouseButton button = ui::MouseButton::Left);

    /// The text of a row's value (for the buttons and the tests).
    [[nodiscard]] static std::string value_text(const DisplayModel& model, Item item);
    /// What a left click on a row does to the model; false if the row cannot be changed now.
    static bool choose_next(DisplayModel& model, Item item) { return choose(model, item, true); }
    /// What a right click does: the previous value.
    static bool choose_previous(DisplayModel& model, Item item) { return choose(model, item, false); }

private:
    static bool choose(DisplayModel& model, Item item, bool forward);
    void layout(const DisplayModel& model);

    ui::Form m_form;
};

}  // namespace o1602::app
