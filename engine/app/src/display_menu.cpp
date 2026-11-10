// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_menu.hpp"

#include <algorithm>
#include <string>
#include <string_view>

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

constexpr Id kItem = 1;  // + Item
constexpr Id kClose = 20;

constexpr std::int32_t kRowY = 64;
constexpr std::int32_t kRowH = 36;
constexpr std::int32_t kPercentW = 96;  ///< the value beside a slider
constexpr std::int32_t kLabelW = 260;
constexpr std::int32_t kValueW = 232;

constexpr std::array<std::string_view, DisplayMenu::kItems> kLabels = {
    "display.mode",       "display.size",         "display.ui_scale",   "display.zoom",       "display.zoom_drawing",
    "display.vsync",      "display.edge_scroll",  "display.edge_speed", "display.grab_mouse", "display.goods_trend",
    "display.rival_wars", "display.plant_fields", "display.graphics",  "vita.display.cpu", "vita.display.left_stick", "vita.display.right_stick", "vita.display.chat_button"};

std::string on_off(bool on) {
    return ui::tr(on ? "display.on" : "display.off");
}

}  // namespace

std::string DisplayMenu::value_text(const DisplayModel& model, Item item) {
    const DisplaySettings& s = model.settings;
    switch (item) {
        case Item::Mode:
            return ui::tr(s.fullscreen ? "display.fullscreen" : "display.window");
        case Item::WindowSize:
            if (s.fullscreen) {
                return ui::tr("display.size_desktop");
            }
            return ui::tr("display.size_value",
                          {std::to_string(model.window.width), std::to_string(model.window.height)});
        case Item::UiScale:
            if (s.ui_scale <= 0) {
                return ui::tr("display.ui_auto");
            }
            return ui::tr("display.percent", {std::to_string(s.ui_scale)});
        case Item::Zoom:
            return ui::tr(s.free_zoom ? "display.zoom_free" : "display.zoom_steps");
        case Item::ZoomDrawing:
            return ui::tr(s.smooth_zoom ? "display.zoom_smooth" : "display.zoom_pixels");
        case Item::LeftStick:
            return ui::tr("display.percent", {std::to_string(s.vita_left_stick)});
        case Item::RightStick:
            return ui::tr("display.percent", {std::to_string(s.vita_right_stick)});
        case Item::VitaCpu:
            return s.vita_cpu_mhz?std::to_string(s.vita_cpu_mhz)+" MHz":ui::tr("vita.display.system");
        case Item::Vsync:
            return on_off(s.vsync);
        case Item::EdgeScroll:
            return on_off(s.edge_scroll);
        case Item::EdgeSpeed:
            return ui::tr("display.percent", {std::to_string(s.edge_speed)});
        case Item::GrabMouse:
            return on_off(s.grab_mouse);
        case Item::ChatButton:
            return on_off(s.chat_button);
        case Item::GoodsTrend:
            return on_off(s.goods_trend);
        case Item::RivalWars:
            return on_off(s.rival_wars);
        case Item::PlantFields:
            return on_off(s.plant_fields);
        case Item::Graphics:
            return ui::tr(s.classic_graphics ? "display.graphics_classic" : "display.graphics_modern");
    }
    return {};
}

bool DisplayMenu::choose(DisplayModel& model, Item item, bool forward) {
    if (model.fixed_window && (item == Item::Mode || item == Item::WindowSize || item == Item::GrabMouse || item == Item::Graphics)) return false;
    DisplaySettings& s = model.settings;
    switch (item) {
        case Item::Mode:
            s.fullscreen = !s.fullscreen;
            return true;
        case Item::WindowSize: {
            if (s.fullscreen) {
                return false;
            }
            const WindowSize next =
                forward ? next_window_size(model.sizes, model.window) : previous_window_size(model.sizes, model.window);
            s.window_width = next.width;
            s.window_height = next.height;
            model.window = next;
            return true;
        }
        case Item::UiScale:
            s.ui_scale = forward ? next_ui_scale(s.ui_scale) : previous_ui_scale(s.ui_scale);
            return true;
        case Item::Zoom:
            s.free_zoom = !s.free_zoom;
            return true;
        case Item::ZoomDrawing:
            if (!s.free_zoom) {
                return false;  // the original's levels are always drawn at whole scales
            }
            s.smooth_zoom = !s.smooth_zoom;
            return true;
        case Item::LeftStick:
        case Item::RightStick: {
            if(!model.fixed_window)return false;
            auto& speed=item==Item::LeftStick?s.vita_left_stick:s.vita_right_stick;
            const auto before=speed;
            speed=std::clamp(speed+(forward?25:-25),25,300);
            return speed!=before;
        }
        case Item::VitaCpu:
            return false;
        case Item::Vsync:
            s.vsync = !s.vsync;
            return true;
        case Item::EdgeScroll:
            s.edge_scroll = !s.edge_scroll;
            return true;
        case Item::EdgeSpeed: {
            if (!s.edge_scroll) {
                return false;
            }
            const std::int32_t next =
                std::clamp(s.edge_speed + (forward ? kEdgeSpeedStep : -kEdgeSpeedStep), kEdgeSpeedMin, kEdgeSpeedMax);
            const bool changed = next != s.edge_speed;
            s.edge_speed = next;
            return changed;
        }
        case Item::GrabMouse:
            s.grab_mouse = !s.grab_mouse;
            return true;
        case Item::ChatButton:
            if(!model.fixed_window||!model.chat_available)return false;
            s.chat_button=!s.chat_button;return true;
        case Item::GoodsTrend:
            s.goods_trend = !s.goods_trend;
            return true;
        case Item::RivalWars:
            s.rival_wars = !s.rival_wars;
            return true;
        case Item::PlantFields:
            s.plant_fields = !s.plant_fields;
            return true;
        case Item::Graphics:
            s.classic_graphics = !s.classic_graphics;
            return true;
    }
    return false;
}

std::vector<ui::DrawItem> DisplayMenu::draw(const DisplayModel& model,
                                            const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    layout(model);
    return m_form.draw(fonts);
}

DisplayMenu::Result DisplayMenu::mouse(std::int32_t x, std::int32_t y, bool down, DisplayModel& model,
                                       ui::MouseButton button) {
    layout(model);
    const auto id = m_form.mouse(x, y, down, button);
    if (!id) {
        return Result::None;
    }
    if (*id == kClose) {
        return Result::Close;
    }
    if (*id == kItem + static_cast<Id>(Item::LeftStick) || *id == kItem + static_cast<Id>(Item::RightStick)) {
        auto& speed=*id==kItem+static_cast<Id>(Item::LeftStick)?model.settings.vita_left_stick:model.settings.vita_right_stick;
        const auto next=edge_speed_at(m_form.slid_to());
        const bool changed=next!=speed;speed=next;
        return changed?Result::Changed:Result::None;
    }
    if (*id == kItem + static_cast<Id>(Item::EdgeSpeed)) {
        // The slider, let go: the nearest step.
        const std::int32_t speed = edge_speed_at(m_form.slid_to());
        const bool changed = speed != model.settings.edge_speed;
        model.settings.edge_speed = speed;
        return changed ? Result::Changed : Result::None;
    }
    if (*id >= kItem && *id < kItem + static_cast<Id>(kItems)) {
        const bool forward = m_form.clicked_with() == ui::MouseButton::Left;
        return choose(model, static_cast<Item>(*id - kItem), forward) ? Result::Changed : Result::None;
    }
    return Result::None;
}

void DisplayMenu::layout(const DisplayModel& model) {
    ui::Form& f = m_form;
    f.clear();
    f.panel(Rect{0, 0, kWidth, kHeight});
    f.label(Rect{24, 14, kWidth - 48, 40}, ui::tr("display.title"), Align::Left, Tone::Heading);
    unsigned row=0;
    for (std::size_t i = 0; i < kItems; ++i) {
        const auto item = static_cast<Item>(i);
        if (model.fixed_window && (item == Item::Mode || item == Item::WindowSize || item == Item::GrabMouse || item == Item::Graphics)) continue;
        if(item==Item::VitaCpu)continue;
        if(!model.fixed_window&&(item==Item::LeftStick||item==Item::RightStick))continue;
        if(item==Item::ChatButton&&(!model.fixed_window||!model.chat_available))continue;
        const std::int32_t y = kRowY + static_cast<std::int32_t>(row++) * kRowH;
        const bool used = !(item == Item::WindowSize && model.settings.fullscreen) &&
                          !(item == Item::ZoomDrawing && !model.settings.free_zoom) &&
                          !(item == Item::EdgeSpeed && !model.settings.edge_scroll);
        f.label(Rect{24, y, kLabelW, kRowH - 4}, ui::tr(kLabels[i]), Align::Left, used ? Tone::Normal : Tone::Dim);
        const Id id = kItem + static_cast<Id>(i);
        const Rect value{kWidth - 24 - kValueW, y, kValueW, kRowH - 6};
        if (item == Item::EdgeSpeed || item == Item::LeftStick || item == Item::RightStick) {
            // A slider, with its value beside it; while it is dragged the value it would take.
            DisplayModel shown = model;
            auto& shown_speed=item==Item::EdgeSpeed?shown.settings.edge_speed:item==Item::LeftStick?shown.settings.vita_left_stick:shown.settings.vita_right_stick;
            if (const auto at = f.sliding(id)) {
                shown_speed = edge_speed_at(*at);
            }
            f.slider(id, Rect{value.x, value.y, value.w - kPercentW, value.h},
                     edge_speed_position(shown_speed), used);
            f.label(Rect{value.x + value.w - kPercentW, value.y, kPercentW, value.h}, value_text(shown, item),
                    Align::Right, used ? Tone::Normal : Tone::Dim);
            continue;
        }
        f.cycle_button(id, value, value_text(model, item), used);
    }
    const std::int32_t below = kRowY + static_cast<std::int32_t>(row) * kRowH + 4;
    if(!model.fixed_window)f.paragraph(Rect{24,below,kWidth-48,kHeight-below-60},ui::tr("display.hint"),Tone::Dim);
    f.button(kClose, Rect{(kWidth - 180) / 2, kHeight - 54, 180, 40}, ui::tr("display.close"));
}

}  // namespace o1602::app
