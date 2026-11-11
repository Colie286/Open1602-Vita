// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the game is shown (the window "Anzeige" of the settings): window or full screen, the
// window's size, the size of the interface, the free zoom and how it draws. Kept in settings.cfg
// as display.* lines. Pure logic: the viewer applies it to the window.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace o1602::app {

/// Which messages set the game back to normal speed. In the original those of disasters and
/// attacks do.
enum class EventSpeed : std::uint8_t {
    Normal,   ///< every one of them (original)
    Attacks,  ///< attacks only: fires, plagues, droughts and eruptions let the game run on at its speed
    Keep,     ///< none: the game keeps its speed
};

/// The autosave intervals offered, in minutes of game time (0: off).
inline constexpr std::array<std::int32_t, 7> kAutosaveMinutes = {0, 5, 10, 15, 20, 30, 60};
inline constexpr std::int32_t kAutosaveDefault = 10;

struct DisplaySettings {
    bool fullscreen = false;  ///< the whole screen at the desktop's resolution (no mode change)
    /// The window's size in window coordinates when it was last left; 0: the default (1280 x 800).
    std::int32_t window_width = 0;
    std::int32_t window_height = 0;
    /// The interface's size in percent of the desktop's scale (100: the original's pixels on a
    /// normal screen, twice that on a HiDPI one); 0: as large as fits (the original's behaviour).
    std::int32_t ui_scale = 0;
    /// Free zoom (any magnification, the wheel zooms finely); off: the original's three levels.
    bool free_zoom = true;
    /// A magnification between whole scales is drawn sharp and smooth (render::SdlMapCanvas);
    /// off: straight with uneven pixels (cheapest).
    bool smooth_zoom = true;
    std::int32_t vita_left_stick = 100; ///< speed percent, 25..300; original response at 100
    std::int32_t vita_right_stick = 100;
    bool chat_button = true; ///< handheld multiplayer chat shortcut; presentation only
    bool vsync = true;  ///< frames wait for the screen
    // Handheld presentation preferences. Desktop behavior stays unchanged.
    bool vita_legacy_cap = false; ///< migration only; never persisted
    std::int32_t vita_cpu_mhz = 0; ///< system / optional request of 400 or 444 MHz
    /// The map scrolls while the mouse rests at the edge of the screen, as in the original. Only
    /// where the mouse cannot leave the window: in full screen (which then keeps the mouse) or
    /// with grab_mouse.
    bool edge_scroll = true;
    /// How fast edge scrolling is, in percent of the arrow keys' speed (kEdgeSpeedMin to
    /// kEdgeSpeedMax in steps of kEdgeSpeedStep).
    std::int32_t edge_speed = 100;
    /// The mouse stays in the window (also in a window, not only in full screen) while it has the
    /// focus; switching to another program lets it go.
    bool grab_mouse = false;
    /// The goods trend at the Kontor: under each good of its stock what comes in more than goes
    /// out per minute, green, or the other way round, red (docs/architecture/0012-goods-statistics.md).
    bool goods_trend = true;
    /// The status bar tells when two other players go to war or make peace (the original tells
    /// only the player concerned).
    bool rival_wars = true;
    /// Which messages set the game back to normal speed. In a multiplayer game only the host's
    /// computer changes the speed, so the host's choice counts for everyone.
    EventSpeed event_speed = EventSpeed::Normal;
    /// A farm, plantation, pasture or forester the player builds plants its fields on the open
    /// ground of its catchment at once, as key F does (the original plants none by itself).
    bool plant_fields = false;
    /// The original's graphics instead of Open1602's: shadows and chimney smoke dithered (every
    /// second pixel) instead of blended (render::Dither), and no dotted border around other players'
    /// land while building. One switch for everything where the two differ in look, not in what is
    /// shown.
    bool classic_graphics = false;
    /// Every so many minutes of game time the game is kept as lastgame by itself (one of
    /// kAutosaveMinutes); 0: never.
    std::int32_t autosave_minutes = kAutosaveDefault;

    friend bool operator==(const DisplaySettings&, const DisplaySettings&) = default;
};

inline void normalize_vita_display(DisplaySettings& s) noexcept {
    s.classic_graphics=false; // Vita fixes the modern profile, including old saved settings.
    if(s.vita_legacy_cap){s.vsync=true;s.vita_legacy_cap=false;}
    s.vita_cpu_mhz=444; // Fixed Vita baseline, including older system/400 settings.
}

/// Edge scrolling's speeds offered, in percent of the arrow keys' speed.
inline constexpr std::int32_t kEdgeSpeedMin = 25;
inline constexpr std::int32_t kEdgeSpeedMax = 300;
inline constexpr std::int32_t kEdgeSpeedStep = 25;

/// The interface sizes offered, in percent (0: automatic).
inline constexpr std::array<std::int32_t, 9> kUiScales = {0, 75, 100, 125, 150, 175, 200, 250, 300};

struct WindowSize {
    std::int32_t width = 0;
    std::int32_t height = 0;
    friend bool operator==(const WindowSize&, const WindowSize&) = default;
};
/// The window sizes offered (any other comes from dragging the window's edge).
inline constexpr std::array<WindowSize, 10> kWindowSizes = {{{1024, 768},
                                                             {1280, 720},
                                                             {1280, 800},
                                                             {1366, 768},
                                                             {1600, 900},
                                                             {1680, 1050},
                                                             {1920, 1080},
                                                             {1920, 1200},
                                                             {2560, 1440},
                                                             {3840, 2160}}};
/// The offered sizes that fit into the desktop's usable area (window coordinates); at least the
/// smallest. Unknown area (0): all.
[[nodiscard]] std::vector<WindowSize> window_sizes_for(std::int32_t usable_width, std::int32_t usable_height);
/// The size after `current` in `sizes` (the first that is larger in area; after the last the first).
[[nodiscard]] WindowSize next_window_size(const std::vector<WindowSize>& sizes, WindowSize current);
/// The size before `current` in `sizes` (the last that is smaller in area; before the first the last).
[[nodiscard]] WindowSize previous_window_size(const std::vector<WindowSize>& sizes, WindowSize current);

/// The interface's scale (screen pixels per pixel of the 1024 x 768 design) in a window of
/// `width` x `height` physical pixels: automatic, the largest that fits; else the setting times the
/// desktop's scale, never larger than fits.
[[nodiscard]] double ui_scale_for(std::int32_t ui_scale, std::int32_t width, std::int32_t height,
                                  float display_scale) noexcept;
/// The setting after `current` in kUiScales (after the last the first).
[[nodiscard]] std::int32_t next_ui_scale(std::int32_t current) noexcept;
/// The setting before `current` in kUiScales (before the first the last).
[[nodiscard]] std::int32_t previous_ui_scale(std::int32_t current) noexcept;

/// Edge scrolling's speed for a slider at `position` (0..1): the nearest step.
[[nodiscard]] std::int32_t edge_speed_at(double position) noexcept;
/// Where the slider of edge scrolling's `speed` stands (0..1).
[[nodiscard]] double edge_speed_position(std::int32_t speed) noexcept;

/// The autosave interval for a slider at `position` (0..1): the nearest of kAutosaveMinutes.
[[nodiscard]] std::int32_t autosave_at(double position) noexcept;
/// Where the slider of the autosave interval `minutes` stands (0..1); a value not offered stands
/// at the nearest one offered.
[[nodiscard]] double autosave_position(std::int32_t minutes) noexcept;
/// The interval offered after (`forward`) or before `minutes`; it stops at the ends.
[[nodiscard]] std::int32_t next_autosave(std::int32_t minutes, bool forward) noexcept;

/// A "display.*" line of settings.cfg (`key` without the prefix); false if the key is unknown.
bool read_display_line(DisplaySettings& s, std::string_view key, std::string_view value);
/// The display.* lines of settings.cfg.
[[nodiscard]] std::string write_display(const DisplaySettings& s);

}  // namespace o1602::app
