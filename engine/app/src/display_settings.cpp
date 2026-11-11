// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_settings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <cstdlib>

#include "o1602/ui/panel.hpp"

namespace o1602::app {

namespace {

bool read_flag(std::string_view value, bool& out) {
    if (value == "on" || value == "1" || value == "true") {
        out = true;
        return true;
    }
    if (value == "off" || value == "0" || value == "false") {
        out = false;
        return true;
    }
    return false;
}

bool read_number(std::string_view value, std::int32_t lo, std::int32_t hi, std::int32_t& out) {
    std::int32_t n = 0;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
    if (ec != std::errc{} || end != value.data() + value.size()) {
        return false;
    }
    out = std::clamp(n, lo, hi);
    return true;
}

/// The values of "display.event_speed", in the order of EventSpeed.
constexpr std::array<std::string_view, 3> kEventSpeedNames = {"normal", "attacks", "keep"};

const char* on_off(bool on) {
    return on ? "on" : "off";
}

}  // namespace

std::vector<WindowSize> window_sizes_for(std::int32_t usable_width, std::int32_t usable_height) {
    std::vector<WindowSize> out;
    for (const WindowSize& s : kWindowSizes) {
        if (usable_width <= 0 || usable_height <= 0 || (s.width <= usable_width && s.height <= usable_height)) {
            out.push_back(s);
        }
    }
    if (out.empty()) {
        out.push_back(kWindowSizes.front());
    }
    return out;
}

WindowSize next_window_size(const std::vector<WindowSize>& sizes, WindowSize current) {
    if (sizes.empty()) {
        return current;
    }
    const std::int64_t area = std::int64_t{current.width} * current.height;
    const auto it = std::ranges::find(sizes, current);
    if (it != sizes.end()) {
        return std::next(it) == sizes.end() ? sizes.front() : *std::next(it);
    }
    // A size dragged by hand: the next larger one in the list.
    for (const WindowSize& s : sizes) {
        if (std::int64_t{s.width} * s.height > area) {
            return s;
        }
    }
    return sizes.front();
}

WindowSize previous_window_size(const std::vector<WindowSize>& sizes, WindowSize current) {
    if (sizes.empty()) {
        return current;
    }
    const std::int64_t area = std::int64_t{current.width} * current.height;
    const auto it = std::ranges::find(sizes, current);
    if (it != sizes.end()) {
        return it == sizes.begin() ? sizes.back() : *std::prev(it);
    }
    // A size dragged by hand: the next smaller one in the list.
    for (auto s = sizes.rbegin(); s != sizes.rend(); ++s) {
        if (std::int64_t{s->width} * s->height < area) {
            return *s;
        }
    }
    return sizes.back();
}

double ui_scale_for(std::int32_t ui_scale, std::int32_t width, std::int32_t height, float display_scale) noexcept {
    const double fit = std::min(static_cast<double>(std::max(width, 1)) / ui::kDesignWidth,
                                static_cast<double>(std::max(height, 1)) / ui::kDesignHeight);
    if (ui_scale <= 0) {
        return fit;
    }
    const double desktop =
        std::isfinite(display_scale) && display_scale > 0.0F ? static_cast<double>(display_scale) : 1.0;
    return std::min(fit, ui_scale / 100.0 * desktop);
}

std::int32_t edge_speed_at(double position) noexcept {
    if (!std::isfinite(position)) {
        return 100;
    }
    constexpr double kSteps = (kEdgeSpeedMax - kEdgeSpeedMin) / kEdgeSpeedStep;
    const auto step = static_cast<std::int32_t>(std::lround(std::clamp(position, 0.0, 1.0) * kSteps));
    return kEdgeSpeedMin + step * kEdgeSpeedStep;
}

double edge_speed_position(std::int32_t speed) noexcept {
    const std::int32_t s = std::clamp(speed, kEdgeSpeedMin, kEdgeSpeedMax);
    return static_cast<double>(s - kEdgeSpeedMin) / (kEdgeSpeedMax - kEdgeSpeedMin);
}

namespace {

/// The index in kAutosaveMinutes nearest to `minutes`.
std::size_t autosave_index(std::int32_t minutes) noexcept {
    std::size_t best = 0;
    for (std::size_t i = 1; i < kAutosaveMinutes.size(); ++i) {
        if (std::abs(kAutosaveMinutes[i] - minutes) < std::abs(kAutosaveMinutes[best] - minutes)) {
            best = i;
        }
    }
    return best;
}

}  // namespace

std::int32_t autosave_at(double position) noexcept {
    if (!std::isfinite(position)) {
        return kAutosaveDefault;
    }
    constexpr double kLast = static_cast<double>(kAutosaveMinutes.size() - 1);
    const auto i = static_cast<std::size_t>(std::lround(std::clamp(position, 0.0, 1.0) * kLast));
    return kAutosaveMinutes[i];
}

double autosave_position(std::int32_t minutes) noexcept {
    return static_cast<double>(autosave_index(minutes)) / static_cast<double>(kAutosaveMinutes.size() - 1);
}

std::int32_t next_autosave(std::int32_t minutes, bool forward) noexcept {
    const std::size_t i = autosave_index(minutes);
    if (forward) {
        return kAutosaveMinutes[std::min(i + 1, kAutosaveMinutes.size() - 1)];
    }
    return kAutosaveMinutes[i == 0 ? 0 : i - 1];
}

std::int32_t next_ui_scale(std::int32_t current) noexcept {
    const auto it = std::ranges::find(kUiScales, current);
    if (it == kUiScales.end() || std::next(it) == kUiScales.end()) {
        return kUiScales.front();
    }
    return *std::next(it);
}

std::int32_t previous_ui_scale(std::int32_t current) noexcept {
    const auto it = std::ranges::find(kUiScales, current);
    if (it == kUiScales.end() || it == kUiScales.begin()) {
        return kUiScales.back();
    }
    return *std::prev(it);
}

bool read_display_line(DisplaySettings& s, std::string_view key, std::string_view value) {
    if (key == "fullscreen") {
        return read_flag(value, s.fullscreen);
    }
    if (key == "window") {
        // "WxH"
        const std::size_t x = value.find('x');
        if (x == std::string_view::npos) {
            return false;
        }
        std::int32_t w = 0;
        std::int32_t h = 0;
        if (!read_number(value.substr(0, x), 320, 16384, w) || !read_number(value.substr(x + 1), 240, 16384, h)) {
            return false;
        }
        s.window_width = w;
        s.window_height = h;
        return true;
    }
    if (key == "ui_scale") {
        std::int32_t n = 0;
        if (!read_number(value, 0, 400, n)) {
            return false;
        }
        s.ui_scale = n < 50 ? 0 : n;  // anything smaller than half is automatic
        return true;
    }
    if (key == "free_zoom") {
        return read_flag(value, s.free_zoom);
    }
    if (key == "smooth_zoom") {
        return read_flag(value, s.smooth_zoom);
    }
    if (key == "vsync") {
        return read_flag(value, s.vsync);
    }
    if(key=="vita_fps" || key=="vita_cpu_mhz"){
        std::int32_t n=0;
        if(!read_number(value,std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max(),n))return false;
        if(key=="vita_fps"){
            if(n!=0&&n!=30&&n!=40&&n!=60)return false;
            s.vita_legacy_cap=true; // retire the old cap and restore normal VSync once
        }else{
            if(n!=0&&n!=400&&n!=444&&n!=500)return false;
            s.vita_cpu_mhz=n==500?444:n; // migrate the retired request without discarding other settings
        }
        return true;
    }
    if (key == "edge_scroll") {
        return read_flag(value, s.edge_scroll);
    }
    if (key == "edge_speed") {
        std::int32_t n = 0;
        if (!read_number(value, kEdgeSpeedMin, kEdgeSpeedMax, n)) {
            return false;
        }
        s.edge_speed = edge_speed_at(edge_speed_position(n));  // on a step
        return true;
    }
    if (key == "grab_mouse") {
        return read_flag(value, s.grab_mouse);
    }
    if (key == "vita_left_stick") return read_number(value,25,300,s.vita_left_stick);
    if (key == "vita_right_stick") return read_number(value,25,300,s.vita_right_stick);
    if (key == "chat_button") return read_flag(value,s.chat_button);
    if (key == "goods_trend") {
        return read_flag(value, s.goods_trend);
    }
    if (key == "rival_wars") {
        return read_flag(value, s.rival_wars);
    }
    if (key == "event_speed") {
        for (std::size_t i = 0; i < kEventSpeedNames.size(); ++i) {
            if (value == kEventSpeedNames[i]) {
                s.event_speed = static_cast<EventSpeed>(i);
                return true;
            }
        }
        return false;
    }
    if (key == "plant_fields") {
        return read_flag(value, s.plant_fields);
    }
    if (key == "autosave") {
        if (value == "off") {
            s.autosave_minutes = 0;
            return true;
        }
        std::int32_t n = 0;
        if (!read_number(value, 0, kAutosaveMinutes.back(), n)) {
            return false;
        }
        s.autosave_minutes = kAutosaveMinutes[autosave_index(n)];  // one of those offered
        return true;
    }
    if (key == "graphics") {
        if (value == "classic" || value == "modern") {
            s.classic_graphics = value == "classic";
            return true;
        }
        return false;
    }
    return false;
}

std::string write_display(const DisplaySettings& s) {
    std::string out = "# Display: full screen, the window's size, the interface in percent (0 = as large as fits)\n";
    out += std::string("display.fullscreen = ") + on_off(s.fullscreen) + "\n";
    if (s.window_width > 0 && s.window_height > 0) {
        out += "display.window = " + std::to_string(s.window_width) + "x" + std::to_string(s.window_height) + "\n";
    }
    out += "display.ui_scale = " + std::to_string(s.ui_scale) + "\n";
    out += "# The free zoom (off: the original's three levels), drawn smooth between whole scales\n";
    out += std::string("display.free_zoom = ") + on_off(s.free_zoom) + "\n";
    out += std::string("display.smooth_zoom = ") + on_off(s.smooth_zoom) + "\n";
    out += std::string("display.vsync = ") + on_off(s.vsync) + "\n";
    out += "display.vita_left_stick = " + std::to_string(s.vita_left_stick) + "\n";
    out += "display.vita_right_stick = " + std::to_string(s.vita_right_stick) + "\n";
    out += "display.vita_cpu_mhz = " + std::to_string(s.vita_cpu_mhz) + "\n";
    out += "# The map scrolls with the mouse at the screen's edge (full screen, or with grab_mouse)\n";
    out += std::string("display.edge_scroll = ") + on_off(s.edge_scroll) + "\n";
    out += "# Its speed in percent of the arrow keys' (25 to 300)\n";
    out += "display.edge_speed = " + std::to_string(s.edge_speed) + "\n";
    out += "# The mouse cannot leave the window while it has the focus\n";
    out += std::string("display.grab_mouse = ") + on_off(s.grab_mouse) + "\n";
    out += "# Under each good of a Kontor's stock how much more comes in or goes out per minute\n";
    out += std::string("display.chat_button = ") + on_off(s.chat_button) + "\n";
    out += std::string("display.goods_trend = ") + on_off(s.goods_trend) + "\n";
    out += "# Tell when two other players go to war or make peace\n";
    out += std::string("display.rival_wars = ") + on_off(s.rival_wars) + "\n";
    out += "# Which messages set the game back to normal speed: normal (all, original), attacks, keep (none)\n";
    out += "display.event_speed = " + std::string(kEventSpeedNames[static_cast<std::size_t>(s.event_speed)]) + "\n";
    out += "# A new farm plants its fields on the open ground around it at once\n";
    out += std::string("display.plant_fields = ") + on_off(s.plant_fields) + "\n";
    out += "# classic: shadows and smoke as the original draws them (every second pixel); modern: blended\n";
    out += std::string("display.graphics = ") + (s.classic_graphics ? "classic" : "modern") + "\n";
    out += "# Keep the game as lastgame every so many minutes of game time (off, 5, 10, 15, 20, 30, 60)\n";
    out += "display.autosave = " + (s.autosave_minutes > 0 ? std::to_string(s.autosave_minutes) : std::string("off")) +
           "\n";
    return out;
}

}  // namespace o1602::app
