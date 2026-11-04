// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's frame (editor_chrome.hpp): its menu bar, tool bar and status line.
#include <array>
#include <optional>
#include <string>

#include "../test_framework.hpp"
#include "editor_chrome.hpp"

#include "o1602/ui/localization.hpp"

using namespace o1602;
using app::EditorChrome;
using app::EditorChromeModel;
using app::EditorCommand;
using app::EditorModel;
using app::EditorTool;

namespace {

/// The middle of the widget that says `text`, in pixels at `scale`.
std::optional<std::array<std::int32_t, 2>> find(const EditorChrome& chrome, const std::string& text, double scale) {
    for (const ui::Form::Widget& w : chrome.form().widgets()) {
        if (w.text == text) {
            return std::array<std::int32_t, 2>{static_cast<std::int32_t>((w.rect.x + w.rect.w / 2) * scale),
                                               static_cast<std::int32_t>((w.rect.y + w.rect.h / 2) * scale)};
        }
    }
    return std::nullopt;
}

EditorChrome::Result click_at(EditorChrome& chrome, const EditorModel& m, const EditorChromeModel& c, std::int32_t x,
                              std::int32_t y, bool& taken) {
    (void)chrome.mouse(x, y, true, m, c, std::nullopt, taken);
    return chrome.mouse(x, y, false, m, c, std::nullopt, taken);
}

}  // namespace

TEST_CASE("editor chrome: the menus give their commands, players, rules and check at once") {
    ui::set_language("en");
    EditorChrome chrome;
    chrome.set_screen(1024, 768, 1.0);
    EditorModel m;
    EditorChromeModel c;
    c.command_keys[static_cast<std::size_t>(EditorCommand::Save)] = "Ctrl+S";
    chrome.layout(m, c, std::nullopt);
    auto at = find(chrome, "File", 1.0);
    REQUIRE(at.has_value());
    bool taken = false;
    auto r = click_at(chrome, m, c, (*at)[0], (*at)[1], taken);
    CHECK(taken);
    CHECK(!r.command.has_value());
    CHECK(chrome.menu_open());
    chrome.layout(m, c, std::nullopt);
    bool key_shown = false;
    for (const ui::Form::Widget& w : chrome.form().widgets()) {
        key_shown = key_shown || w.text == "Ctrl+S";
    }
    CHECK(key_shown);
    at = find(chrome, "Save as", 1.0);
    REQUIRE(at.has_value());
    r = click_at(chrome, m, c, (*at)[0], (*at)[1], taken);
    REQUIRE(r.command.has_value());
    CHECK(*r.command == EditorCommand::SaveAs);
    CHECK(!chrome.menu_open());
    // Nothing to undo: the entry cannot be chosen.
    chrome.layout(m, c, std::nullopt);
    at = find(chrome, "Edit", 1.0);
    REQUIRE(at.has_value());
    (void)click_at(chrome, m, c, (*at)[0], (*at)[1], taken);
    chrome.layout(m, c, std::nullopt);
    at = find(chrome, "Undo", 1.0);
    REQUIRE(at.has_value());
    r = click_at(chrome, m, c, (*at)[0], (*at)[1], taken);
    CHECK(!r.command.has_value());
    // Players opens its page at once.
    chrome.layout(m, c, std::nullopt);
    at = find(chrome, "Players", 1.0);
    REQUIRE(at.has_value());
    r = click_at(chrome, m, c, (*at)[0], (*at)[1], taken);
    REQUIRE(r.command.has_value());
    CHECK(*r.command == EditorCommand::Players);
    // A press beside an open menu closes it and goes nowhere else.
    chrome.layout(m, c, std::nullopt);
    at = find(chrome, "Map", 1.0);
    REQUIRE(at.has_value());
    (void)click_at(chrome, m, c, (*at)[0], (*at)[1], taken);
    REQUIRE(chrome.menu_open());
    // Open, it has every click: the map's and the column's.
    CHECK(chrome.covers(500, 500));
    CHECK(chrome.covers(1000, 400));
    (void)chrome.mouse(500, 500, true, m, c, std::nullopt, taken);
    CHECK(taken);
    CHECK(!chrome.menu_open());
}

TEST_CASE("editor chrome: a menu is as wide as its longest command and key") {
    ui::set_language("en");
    EditorChrome chrome;
    chrome.set_screen(1024, 768, 1.0);
    EditorModel m;
    EditorChromeModel c;
    c.command_keys[static_cast<std::size_t>(EditorCommand::SaveAs)] = "Ctrl+Shift+S";
    chrome.layout(m, c, std::nullopt);
    const auto at = find(chrome, "File", 1.0);
    REQUIRE(at.has_value());
    bool taken = false;
    (void)click_at(chrome, m, c, (*at)[0], (*at)[1], taken);
    chrome.layout(m, c, std::nullopt);
    // Without fonts every byte is 9 units wide: the command and its key side by side.
    std::optional<ui::Rect> command;
    std::optional<ui::Rect> key;
    for (const ui::Form::Widget& w : chrome.form().widgets()) {
        if (w.text == "Save as") {
            command = w.rect;
        }
        if (w.text == "Ctrl+Shift+S") {
            key = w.rect;
        }
    }
    REQUIRE(command.has_value());
    REQUIRE(key.has_value());
    CHECK(command->w >=
          static_cast<std::int32_t>(std::string("Save as").size() + std::string("Ctrl+Shift+S").size()) * 9 +
              2 * ui::kTextPadding);
}

TEST_CASE("editor chrome: the tool bar chooses tools and names them with their keys") {
    ui::set_language("en");
    for (const auto& [w, h] : {std::pair{1024, 768}, std::pair{800, 600}}) {
        const double scale = std::min(w / 1024.0, h / 768.0);
        EditorChrome chrome;
        chrome.set_screen(w, h, scale);
        EditorModel m;
        EditorChromeModel c;
        c.tool_keys[static_cast<std::size_t>(EditorTool::Land)] = "L";
        chrome.layout(m, c, EditorTool::Land);
        // Every tool has its button, all of them on the bar between the menu and the status line.
        std::size_t tools = 0;
        std::optional<ui::Rect> land;
        for (const ui::Form::Widget& wd : chrome.form().widgets()) {
            if (wd.kind == ui::Form::Widget::Kind::Button && wd.id >= 50 &&
                wd.id < 50 + static_cast<int>(app::kEditorTools)) {
                ++tools;
                CHECK(wd.rect.y >= EditorChrome::kMenuH);
                CHECK(wd.rect.y + wd.rect.h <= 768 - EditorChrome::kStatusH);
                CHECK(wd.rect.x + wd.rect.w <= EditorChrome::kToolW);
                if (wd.id == 50 + static_cast<int>(EditorTool::Land)) {
                    land = wd.rect;
                    CHECK(wd.on);  // the tool chosen
                }
                // A placeholder needs a world plan.
                if (wd.id == 50 + static_cast<int>(EditorTool::Placeholder)) {
                    CHECK(!wd.enabled);
                }
            }
            // The menu bar's entries all fit beside the column.
            if (wd.kind == ui::Form::Widget::Kind::Button && wd.id >= 1 && wd.id < 9) {
                CHECK(wd.rect.x + wd.rect.w <= 1024 - EditorChrome::kColumn);
            }
        }
        CHECK(tools == app::kEditorTools);
        REQUIRE(land.has_value());
        const auto x = static_cast<std::int32_t>((land->x + land->w / 2) * scale);
        const auto y = static_cast<std::int32_t>((land->y + land->h / 2) * scale);
        bool taken = false;
        (void)chrome.mouse(x, y, false, m, c, EditorTool::Land, taken);  // the mouse over it
        CHECK(chrome.hint(c) == "Draw land (L)");
        const auto r = click_at(chrome, m, c, x, y, taken);
        REQUIRE(r.tool.has_value());
        CHECK(*r.tool == EditorTool::Land);
        CHECK(chrome.covers(x, y));
        // The map between the bars is not the frame's, the column neither.
        CHECK(!chrome.covers(static_cast<std::int32_t>(400 * scale), static_cast<std::int32_t>(300 * scale)));
        CHECK(!chrome.covers(w - 10, static_cast<std::int32_t>(300 * scale)));
    }
}

TEST_CASE("editor chrome: the status line shows the notice and where the mouse is") {
    ui::set_language("en");
    EditorChrome chrome;
    chrome.set_screen(1024, 768, 1.0);
    EditorModel m;
    m.file = "Zwei Kuesten";
    m.modified = true;
    EditorChromeModel c;
    c.status = "Saved";
    c.place = "Tile 12, 34 \xB7 island 3";
    chrome.layout(m, c, std::nullopt);
    bool notice = false;
    bool place = false;
    bool name = false;
    for (const ui::Form::Widget& w : chrome.form().widgets()) {
        notice = notice || w.text == "Saved";
        place = place || w.text == c.place;
        name = name || w.text == "Zwei Kuesten *";
    }
    CHECK(notice);
    CHECK(place);
    CHECK(name);
}
