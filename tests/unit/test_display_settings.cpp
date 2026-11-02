// SPDX-License-Identifier: GPL-3.0-or-later
// How the game is shown (engine/app/src/display_settings.hpp, display_menu.hpp): the display.*
// lines of settings.cfg, the window sizes and interface sizes offered, the window "Anzeige".

#include <string>

#include "../test_framework.hpp"
#include "app_settings.hpp"
#include "autosave.hpp"
#include "display_menu.hpp"
#include "display_settings.hpp"

#include "o1602/ui/localization.hpp"

using namespace o1602;

TEST_CASE("display settings: written to settings.cfg and read back") {
    app::AppSettings s;
    s.display.fullscreen = true;
    s.display.window_width = 1600;
    s.display.window_height = 900;
    s.display.ui_scale = 150;
    s.display.free_zoom = false;
    s.display.smooth_zoom = false;
    s.display.vsync = false;
    s.display.edge_scroll = false;
    s.display.edge_speed = 175;
    s.display.grab_mouse = true;
    s.display.goods_trend = false;
    s.display.rival_wars = false;
    s.display.event_speed = app::EventSpeed::Attacks;
    s.display.plant_fields = true;
    s.display.classic_graphics = true;
    s.display.autosave_minutes = 30;
    const std::string text = app::write_settings(s);
    CHECK(text.find("display.window = 1600x900\n") != std::string::npos);
    CHECK(text.find("display.graphics = classic\n") != std::string::npos);
    CHECK(text.find("display.event_speed = attacks\n") != std::string::npos);
    const app::AppSettings back = app::parse_settings(text, app::AppSettings{});
    CHECK(back.display == s.display);
    // The defaults: a window, the interface as large as fits, the free zoom drawn smooth.
    const app::AppSettings none = app::parse_settings("", app::AppSettings{});
    CHECK(!none.display.fullscreen);
    CHECK(none.display.window_width == 0);
    CHECK(none.display.ui_scale == 0);
    CHECK(none.display.free_zoom);
    CHECK(none.display.smooth_zoom);
    CHECK(none.display.vsync);
    CHECK(none.display.edge_scroll);  // as in the original
    CHECK(none.display.edge_speed == 100);
    CHECK(!none.display.grab_mouse);
    CHECK(none.display.goods_trend);
    CHECK(none.display.rival_wars);
    CHECK(none.display.event_speed == app::EventSpeed::Normal);  // back to 1x, as the original
    CHECK(!none.display.plant_fields);                           // the original plants no fields by itself
    CHECK(!none.display.classic_graphics);                       // Open1602's look
    CHECK(none.display.autosave_minutes == app::kAutosaveDefault);
}

TEST_CASE("display settings: damaged lines keep what was there") {
    app::DisplaySettings d;
    CHECK(!app::read_display_line(d, "window", "big"));
    CHECK(!app::read_display_line(d, "window", "1600x"));
    CHECK(!app::read_display_line(d, "fullscreen", "maybe"));
    CHECK(!app::read_display_line(d, "unknown", "on"));
    CHECK(!app::read_display_line(d, "graphics", "on"));
    CHECK(!app::read_display_line(d, "event_speed", "fast"));
    CHECK(d.event_speed == app::EventSpeed::Normal);
    CHECK(d == app::DisplaySettings{});
    CHECK(app::read_display_line(d, "window", "100x50"));  // kept within limits
    CHECK(d.window_width == 320);
    CHECK(d.window_height == 240);
    CHECK(app::read_display_line(d, "ui_scale", "30"));  // below half: automatic
    CHECK(d.ui_scale == 0);
    CHECK(app::read_display_line(d, "ui_scale", "900"));
    CHECK(d.ui_scale == 400);
}

TEST_CASE("display settings: the window sizes that fit the screen") {
    const auto all = app::window_sizes_for(0, 0);
    CHECK(all.size() == app::kWindowSizes.size());
    const auto hd = app::window_sizes_for(1920, 1040);  // a task bar takes 40 rows
    CHECK(!hd.empty());
    for (const app::WindowSize& s : hd) {
        CHECK(s.width <= 1920);
        CHECK(s.height <= 1040);
    }
    CHECK(app::window_sizes_for(800, 600).size() == 1);  // the smallest always
    // The next size: the following one in the list, a dragged size the next larger.
    CHECK(app::next_window_size(all, {1280, 720}) == (app::WindowSize{1280, 800}));
    CHECK(app::next_window_size(all, {1300, 900}) == (app::WindowSize{1600, 900}));
    CHECK(app::next_window_size(all, app::kWindowSizes.back()) == app::kWindowSizes.front());
    // The previous size: the one before in the list, a dragged size the next smaller.
    CHECK(app::previous_window_size(all, {1280, 800}) == (app::WindowSize{1280, 720}));
    CHECK(app::previous_window_size(all, {1300, 900}) == (app::WindowSize{1366, 768}));
    CHECK(app::previous_window_size(all, app::kWindowSizes.front()) == app::kWindowSizes.back());
    CHECK(app::previous_window_size(all, {200, 100}) == app::kWindowSizes.back());
    for (const app::WindowSize& s : all) {
        CHECK(app::previous_window_size(all, app::next_window_size(all, s)) == s);
    }
}

TEST_CASE("display settings: the interface's scale") {
    // Automatic: as large as the 1024 x 768 design fits.
    CHECK(app::ui_scale_for(0, 1024, 768, 1.0F) == 1.0);
    CHECK(app::ui_scale_for(0, 2560, 1440, 1.0F) == 1440.0 / 768.0);
    // A size: in percent of the desktop's scale, never larger than fits.
    CHECK(app::ui_scale_for(100, 2560, 1440, 1.0F) == 1.0);
    CHECK(app::ui_scale_for(100, 3840, 2160, 2.0F) == 2.0);
    CHECK(app::ui_scale_for(150, 1280, 800, 1.0F) == 800.0 / 768.0);
    CHECK(app::ui_scale_for(75, 1920, 1080, 1.0F) == 0.75);
    CHECK(app::next_ui_scale(0) == 75);
    CHECK(app::next_ui_scale(300) == 0);
    CHECK(app::next_ui_scale(33) == 0);
    CHECK(app::previous_ui_scale(75) == 0);
    CHECK(app::previous_ui_scale(0) == 300);
    CHECK(app::previous_ui_scale(33) == 300);
    for (const std::int32_t s : app::kUiScales) {
        CHECK(app::previous_ui_scale(app::next_ui_scale(s)) == s);
    }
}

TEST_CASE("display menu: each row chooses its next value") {
    ui::set_language(ui::kFallbackLanguage);
    app::DisplayModel model;
    model.window = {1280, 800};
    model.sizes = app::window_sizes_for(0, 0);
    using Item = app::DisplayMenu::Item;
    CHECK(app::DisplayMenu::value_text(model, Item::WindowSize) == "1280 \xd7 800");  // Windows-1252
    CHECK(app::DisplayMenu::choose_next(model, Item::WindowSize));
    CHECK(model.window == (app::WindowSize{1366, 768}));
    CHECK(model.settings.window_width == 1366);
    CHECK(app::DisplayMenu::choose_next(model, Item::Mode));
    CHECK(model.settings.fullscreen);
    CHECK(!app::DisplayMenu::choose_next(model, Item::WindowSize));  // full screen: the screen's size
    CHECK(app::DisplayMenu::value_text(model, Item::UiScale) == "as large as fits");
    CHECK(app::DisplayMenu::choose_next(model, Item::UiScale));
    CHECK(app::DisplayMenu::value_text(model, Item::UiScale) == "75 %");
    CHECK(app::DisplayMenu::choose_next(model, Item::Zoom));
    CHECK(!model.settings.free_zoom);
    CHECK(!app::DisplayMenu::choose_next(model, Item::ZoomDrawing));  // the original's levels: whole scales
    CHECK(app::DisplayMenu::choose_next(model, Item::Vsync));
    CHECK(!model.settings.vsync);
    CHECK(app::DisplayMenu::value_text(model, Item::EdgeSpeed) == "100 %");
    CHECK(app::DisplayMenu::choose_next(model, Item::EdgeSpeed));
    CHECK(model.settings.edge_speed == 125);
    CHECK(app::DisplayMenu::choose_previous(model, Item::EdgeSpeed));
    CHECK(app::DisplayMenu::choose_previous(model, Item::EdgeSpeed));
    CHECK(model.settings.edge_speed == 75);
    CHECK(app::DisplayMenu::value_text(model, Item::EdgeScroll) == "on");
    CHECK(app::DisplayMenu::choose_next(model, Item::EdgeScroll));
    CHECK(!model.settings.edge_scroll);
    CHECK(!app::DisplayMenu::choose_next(model, Item::EdgeSpeed));  // no edge scrolling: no speed
    CHECK(app::DisplayMenu::value_text(model, Item::GrabMouse) == "off");
    CHECK(app::DisplayMenu::choose_previous(model, Item::GrabMouse));
    CHECK(model.settings.grab_mouse);
    CHECK(app::DisplayMenu::value_text(model, Item::GoodsTrend) == "on");
    CHECK(app::DisplayMenu::choose_next(model, Item::GoodsTrend));
    CHECK(!model.settings.goods_trend);
    CHECK(app::DisplayMenu::choose_next(model, Item::RivalWars));
    CHECK(!model.settings.rival_wars);
    CHECK(app::DisplayMenu::value_text(model, Item::RivalWars) == "off");
    CHECK(app::DisplayMenu::value_text(model, Item::EventSpeed) == "Always (original)");
    CHECK(app::DisplayMenu::choose_next(model, Item::EventSpeed));
    CHECK(model.settings.event_speed == app::EventSpeed::Attacks);
    CHECK(app::DisplayMenu::value_text(model, Item::EventSpeed) == "Attacks only");
    CHECK(app::DisplayMenu::choose_previous(model, Item::EventSpeed));
    CHECK(app::DisplayMenu::choose_previous(model, Item::EventSpeed));
    CHECK(model.settings.event_speed == app::EventSpeed::Keep);
    CHECK(app::DisplayMenu::value_text(model, Item::EventSpeed) == "Keep the speed");
    CHECK(app::DisplayMenu::value_text(model, Item::PlantFields) == "off");
    CHECK(app::DisplayMenu::choose_next(model, Item::PlantFields));
    CHECK(model.settings.plant_fields);
    CHECK(app::DisplayMenu::value_text(model, Item::Graphics) == "modern");
    CHECK(app::DisplayMenu::choose_next(model, Item::Graphics));
    CHECK(model.settings.classic_graphics);
    CHECK(app::DisplayMenu::value_text(model, Item::Graphics) == "as in the original");
}

TEST_CASE("display menu: a click on a row changes it, the button closes the window") {
    ui::set_language(ui::kFallbackLanguage);
    using Item = app::DisplayMenu::Item;
    app::DisplayMenu menu;
    menu.set_scale(1.0);
    app::DisplayModel model;
    model.window = {1280, 800};
    model.sizes = app::window_sizes_for(0, 0);
    (void)menu.draw(model, {});
    // The first row's button (the mode) on the right of the window.
    const std::int32_t x = app::DisplayMenu::kWidth - 60;
    const std::int32_t y = 64 + 15;
    CHECK(menu.mouse(x, y, true, model) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(x, y, false, model) == app::DisplayMenu::Result::Changed);
    CHECK(model.settings.fullscreen);
    CHECK(app::DisplayMenu::choose_next(model, Item::Mode));  // back to the window
    // A right click on the window's size takes the previous one.
    const std::int32_t size_y = 64 + 36 + 15;
    CHECK(menu.mouse(x, size_y, true, model, ui::MouseButton::Right) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(x, size_y, false, model, ui::MouseButton::Right) == app::DisplayMenu::Result::Changed);
    CHECK(model.window == (app::WindowSize{1280, 720}));
    CHECK(model.settings.window_width == 1280);
    CHECK(model.settings.window_height == 720);
    CHECK(app::DisplayMenu::choose_previous(model, Item::UiScale));
    CHECK(model.settings.ui_scale == 300);
    const std::int32_t cx = app::DisplayMenu::kWidth / 2;
    const std::int32_t cy = app::DisplayMenu::kHeight - 34;
    CHECK(menu.mouse(cx, cy, true, model) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(cx, cy, false, model) == app::DisplayMenu::Result::Close);
}

TEST_CASE("display settings: edge scrolling's speed in steps of 25 %") {
    CHECK(app::edge_speed_at(0.0) == app::kEdgeSpeedMin);
    CHECK(app::edge_speed_at(1.0) == app::kEdgeSpeedMax);
    CHECK(app::edge_speed_at(-3.0) == app::kEdgeSpeedMin);
    CHECK(app::edge_speed_at(7.0) == app::kEdgeSpeedMax);
    for (std::int32_t s = app::kEdgeSpeedMin; s <= app::kEdgeSpeedMax; s += app::kEdgeSpeedStep) {
        CHECK(app::edge_speed_at(app::edge_speed_position(s)) == s);
    }
    app::DisplaySettings d;
    CHECK(app::read_display_line(d, "edge_speed", "110"));  // the nearest step
    CHECK(d.edge_speed == 100);
    CHECK(app::read_display_line(d, "edge_speed", "1000"));  // kept within limits
    CHECK(d.edge_speed == app::kEdgeSpeedMax);
    CHECK(!app::read_display_line(d, "edge_speed", "fast"));
    CHECK(d.edge_speed == app::kEdgeSpeedMax);
}

TEST_CASE("display menu: the slider of edge scrolling's speed follows the mouse") {
    ui::set_language(ui::kFallbackLanguage);
    using Item = app::DisplayMenu::Item;
    app::DisplayMenu menu;
    menu.set_scale(1.0);
    app::DisplayModel model;
    model.window = {1280, 800};
    model.sizes = app::window_sizes_for(0, 0);
    (void)menu.draw(model, {});
    // The slider's row; it runs from the value column's left edge.
    const std::int32_t y = 64 + static_cast<std::int32_t>(Item::EdgeSpeed) * 36 + 15;
    const std::int32_t left = app::DisplayMenu::kWidth - 24 - 266;
    CHECK(menu.mouse(left + 2, y, true, model) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(left + 400, y, true, model) == app::DisplayMenu::Result::None);  // dragged past the end
    CHECK(model.settings.edge_speed == 100);                                          // only on letting go
    CHECK(menu.mouse(left + 400, y + 80, false, model) == app::DisplayMenu::Result::Changed);
    CHECK(model.settings.edge_speed == app::kEdgeSpeedMax);
    CHECK(menu.mouse(left, y, true, model) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(left, y, false, model) == app::DisplayMenu::Result::Changed);
    CHECK(model.settings.edge_speed == app::kEdgeSpeedMin);
    // Off without edge scrolling.
    model.settings.edge_scroll = false;
    (void)menu.draw(model, {});
    CHECK(menu.mouse(left + 100, y, true, model) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(left + 100, y, false, model) == app::DisplayMenu::Result::None);
    CHECK(model.settings.edge_speed == app::kEdgeSpeedMin);
}

TEST_CASE("display settings: the autosave intervals offered") {
    ui::set_language(ui::kFallbackLanguage);
    app::DisplaySettings d;
    CHECK(app::read_display_line(d, "autosave", "off"));
    CHECK(d.autosave_minutes == 0);
    CHECK(app::read_display_line(d, "autosave", "60"));
    CHECK(d.autosave_minutes == 60);
    CHECK(app::read_display_line(d, "autosave", "12"));  // the nearest offered
    CHECK(d.autosave_minutes == 10);
    CHECK(app::read_display_line(d, "autosave", "500"));  // kept within limits
    CHECK(d.autosave_minutes == 60);
    CHECK(!app::read_display_line(d, "autosave", "often"));
    CHECK(d.autosave_minutes == 60);
    d.autosave_minutes = 0;
    CHECK(app::write_display(d).find("display.autosave = off\n") != std::string::npos);
    // The slider: each step on its own place, the ends at the ends.
    for (const std::int32_t m : app::kAutosaveMinutes) {
        CHECK(app::autosave_at(app::autosave_position(m)) == m);
    }
    CHECK(app::autosave_at(0.0) == 0);
    CHECK(app::autosave_at(1.0) == 60);
    CHECK(app::autosave_at(-1.0) == 0);
    CHECK(app::autosave_position(0) == 0.0);
    CHECK(app::autosave_position(60) == 1.0);
    CHECK(app::next_autosave(0, true) == 5);
    CHECK(app::next_autosave(30, true) == 60);
    CHECK(app::next_autosave(60, true) == 60);
    CHECK(app::next_autosave(5, false) == 0);
    CHECK(app::next_autosave(0, false) == 0);
    // The window's row.
    app::DisplayModel model;
    using Item = app::DisplayMenu::Item;
    CHECK(app::DisplayMenu::value_text(model, Item::Autosave) == "10 min");
    CHECK(app::DisplayMenu::choose_next(model, Item::Autosave));
    CHECK(model.settings.autosave_minutes == 15);
    model.settings.autosave_minutes = 5;
    CHECK(app::DisplayMenu::choose_previous(model, Item::Autosave));
    CHECK(app::DisplayMenu::value_text(model, Item::Autosave) == "off");
    CHECK(!app::DisplayMenu::choose_previous(model, Item::Autosave));  // already off
}

TEST_CASE("autosave: every so many minutes of game time") {
    constexpr std::uint64_t kMinute = 60'000;
    app::AutosaveTimer t;
    t.restart(0);
    CHECK(!t.due(0, 5));
    CHECK(!t.due(5 * kMinute - 1, 5));
    CHECK(t.due(5 * kMinute, 5));
    CHECK(!t.due(5 * kMinute + 100, 5));  // once per interval
    CHECK(!t.due(10 * kMinute - 1, 5));
    CHECK(t.due(10 * kMinute + 50, 5));
    // Off: never.
    CHECK(!t.due(100 * kMinute, 0));
    CHECK(!t.due(200 * kMinute, 0));
    // Switched on again: it counts from then, not from the last save.
    CHECK(!t.due(200 * kMinute, 10));
    CHECK(!t.due(209 * kMinute, 10));
    CHECK(t.due(210 * kMinute, 10));
    // A loaded game counts from its own time.
    t.restart(3 * kMinute);
    CHECK(!t.due(12 * kMinute, 10));
    CHECK(t.due(13 * kMinute, 10));
    // A game whose time went back (another came without restart): counts from there.
    CHECK(!t.due(1 * kMinute, 10));
    CHECK(!t.due(10 * kMinute, 10));
    CHECK(t.due(11 * kMinute, 10));
}

TEST_CASE("display menu: the autosave slider stops on the intervals offered") {
    ui::set_language(ui::kFallbackLanguage);
    using Item = app::DisplayMenu::Item;
    app::DisplayMenu menu;
    menu.set_scale(1.0);
    app::DisplayModel model;
    model.window = {1280, 800};
    model.sizes = app::window_sizes_for(0, 0);
    (void)menu.draw(model, {});
    const std::int32_t y = 64 + static_cast<std::int32_t>(Item::Autosave) * 36 + 15;
    const std::int32_t left = app::DisplayMenu::kWidth - 24 - 266;
    CHECK(menu.mouse(left, y, true, model) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(left, y, false, model) == app::DisplayMenu::Result::Changed);
    CHECK(model.settings.autosave_minutes == 0);
    CHECK(menu.mouse(left + 2, y, true, model) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(left + 400, y, true, model) == app::DisplayMenu::Result::None);  // dragged past the end
    CHECK(menu.mouse(left + 400, y, false, model) == app::DisplayMenu::Result::Changed);
    CHECK(model.settings.autosave_minutes == 60);
}
