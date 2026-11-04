// SPDX-License-Identifier: GPL-3.0-or-later
// The window of the keys (engine/app/src/keys_menu.hpp) and the key settings file it keeps.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "keys_menu.hpp"

#include "o1602/ui/localization.hpp"

using namespace o1602;
using app::KeyRow;
using app::KeysMenu;
using ui::KeyAction;
using ui::KeyContext;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts = {nullptr, nullptr, nullptr};

// Keycodes are the letters themselves; physical keys (scancodes) count from 4 = A.
std::string name(std::int32_t key, bool physical) {
    if (physical) {
        return std::string(1, static_cast<char>('A' + key - 4));  // chord_text adds the brackets
    }
    return std::string(1, static_cast<char>(std::toupper(key)));
}

std::optional<std::int32_t> from_name(std::string_view n, bool physical) {
    if (n.size() == 3 && n.front() == '[' && n.back() == ']') {
        n = n.substr(1, 1);
    }
    if (n.size() != 1 || std::isalpha(static_cast<unsigned char>(n[0])) == 0) {
        return std::nullopt;
    }
    const int upper = std::toupper(static_cast<unsigned char>(n[0]));
    return physical ? upper - 'A' + 4 : std::tolower(upper);
}

app::KeysModel model() {
    ui::KeyBindings d;
    d.bind(KeyContext::General, {26, false, false, false, true}, KeyAction::ScrollUp);  // [W]
    d.bind(KeyContext::General, {'p'}, KeyAction::Pause);
    d.bind(KeyContext::General, {'h'}, KeyAction::Help);
    d.bind(KeyContext::General, {'b'}, KeyAction::ModeBuild);
    d.bind(KeyContext::Building, {'r'}, KeyAction::RotateBuilding);
    d.bind(KeyContext::Units, {'s'}, KeyAction::Stop);
    d.bind(KeyContext::Units, {'h'}, KeyAction::HoldPosition);
    d.bind(KeyContext::General, {'1'}, KeyAction::GroupRecall, 0);
    d.bind(KeyContext::Editor, {'z', true}, KeyAction::EditorUndo);
    app::KeysModel m;
    m.keys = d;
    m.defaults = d;
    m.key_label = [](const ui::KeyChord& c) { return ui::KeyBindings::chord_text(c, name); };
    return m;
}

std::optional<std::pair<std::int32_t, std::int32_t>> find_text(KeysMenu& menu, const app::KeysModel& m,
                                                               const std::string& text) {
    for (const ui::DrawItem& d : menu.draw(m, kNoFonts)) {
        if (d.kind == ui::DrawItem::Kind::Text && d.text == text) {
            return std::pair{d.x, d.y - 4};
        }
    }
    return std::nullopt;
}

KeysMenu::Result click(KeysMenu& menu, app::KeysModel& m, const std::string& text,
                       ui::MouseButton button = ui::MouseButton::Left) {
    const auto at = find_text(menu, m, text);
    REQUIRE(at.has_value());
    (void)menu.mouse(at->first, at->second, true, m, button);
    return menu.mouse(at->first, at->second, false, m, button);
}

ui::KeyPress press(std::int32_t key, std::int32_t scancode, bool ctrl = false) {
    return ui::KeyPress{key, scancode, ctrl, false, false};
}

}  // namespace

TEST_CASE("key bindings: what differs from the defaults is written, and read over them makes the table") {
    const app::KeysModel m = model();
    ui::KeyBindings keys = m.defaults;
    CHECK(keys.write_changes(m.defaults, name).empty());
    keys.unbind(KeyContext::General, KeyAction::Pause);
    keys.bind(KeyContext::General, {'q'}, KeyAction::Pause);
    keys.bind(KeyContext::General, {'x', true}, KeyAction::Pause);
    keys.unbind(KeyContext::Units, KeyAction::Stop);
    keys.bind(KeyContext::Units, {'s'}, KeyAction::HoldPosition);  // Stop's key moves over
    const std::string text = keys.write_changes(m.defaults, name);
    CHECK(text.find("general pause = Q\n") != std::string::npos);
    CHECK(text.find("general pause = Ctrl+X\n") != std::string::npos);
    CHECK(text.find("units stop = none\n") != std::string::npos);
    CHECK(text.find("units hold_position = S\n") != std::string::npos);
    CHECK(text.find("help") == std::string::npos);  // unchanged
    ui::KeyBindings read = m.defaults;
    CHECK(read.read_text(text, from_name).empty());
    CHECK(read.find(KeyContext::General, {'q'})->action == KeyAction::Pause);
    CHECK(!read.find(KeyContext::General, {'p'}));
    CHECK(read.keys(KeyContext::Units, KeyAction::Stop).empty());
    CHECK(read.find(KeyContext::Units, {'s'})->action == KeyAction::HoldPosition);
    CHECK(read.keys(KeyContext::Units, KeyAction::HoldPosition).size() == 2);  // H and S
    CHECK(read.write_changes(m.defaults, name) == keys.write_changes(m.defaults, name));
}

TEST_CASE("keys window: the actions of each page, the editor's on a page of their own, no group digits") {
    const app::KeysModel m = model();
    const auto has = [&](std::size_t page, KeyRow row) {
        const std::vector<KeyRow> rows = KeysMenu::rows(m, page);
        return std::find(rows.begin(), rows.end(), row) != rows.end();
    };
    CHECK(has(0, KeyRow{KeyContext::General, KeyAction::ScrollUp}));
    CHECK(has(0, KeyRow{KeyContext::General, KeyAction::Pause}));
    CHECK(has(1, KeyRow{KeyContext::General, KeyAction::ModeBuild}));
    CHECK(has(2, KeyRow{KeyContext::Building, KeyAction::RotateBuilding}));
    CHECK(has(3, KeyRow{KeyContext::Units, KeyAction::Stop}));
    CHECK(has(5, KeyRow{KeyContext::Editor, KeyAction::EditorUndo}));
    for (std::size_t p = 0; p < KeysMenu::kPages; ++p) {
        for (const KeyRow& r : KeysMenu::rows(m, p)) {
            CHECK(r.action != KeyAction::GroupRecall);
        }
    }
    CHECK(KeysMenu::keys_text(m, KeyRow{KeyContext::General, KeyAction::ScrollUp}) == "[W]");
}

TEST_CASE("keys window: a click waits for the key, which replaces the action's keys or joins them") {
    ui::set_language(ui::kFallbackLanguage);
    app::KeysModel m = model();
    KeysMenu menu;
    menu.open();
    // Pause: P becomes Q.
    CHECK(click(menu, m, "P") == KeysMenu::Result::None);
    CHECK(menu.waiting() == (std::optional<KeyRow>(KeyRow{KeyContext::General, KeyAction::Pause})));
    CHECK(find_text(menu, m, ui::tr("keys.settings.press")).has_value());
    // A modifier alone waits on; the key with it is taken.
    CHECK(menu.key(press(0, 0), KeysMenu::KeyKind::Modifier, m) == KeysMenu::Result::None);
    CHECK(menu.waiting().has_value());
    CHECK(menu.key(press('q', 20, true), KeysMenu::KeyKind::Other, m) == KeysMenu::Result::Changed);
    CHECK(!menu.waiting());
    CHECK(m.keys.keys(KeyContext::General, KeyAction::Pause) == (std::vector<ui::KeyChord>{{'q', true}}));
    CHECK(menu.notice().empty());
    // A right click adds a key: Pause on Ctrl+Q and Y.
    CHECK(click(menu, m, "Ctrl+Q", ui::MouseButton::Right) == KeysMenu::Result::None);
    CHECK(find_text(menu, m, ui::tr("keys.settings.press_add")).has_value());
    CHECK(menu.key(press('y', 28), KeysMenu::KeyKind::Other, m) == KeysMenu::Result::Changed);
    CHECK(m.keys.keys(KeyContext::General, KeyAction::Pause).size() == 2);
    CHECK(find_text(menu, m, "Ctrl+Q, Y").has_value());
    // Escape keeps the keys; Backspace takes them all.
    (void)click(menu, m, "Ctrl+Q, Y");
    CHECK(menu.key(press(27, 41), KeysMenu::KeyKind::Escape, m) == KeysMenu::Result::None);
    CHECK(m.keys.keys(KeyContext::General, KeyAction::Pause).size() == 2);
    (void)click(menu, m, "Ctrl+Q, Y");
    CHECK(menu.key(press(8, 42), KeysMenu::KeyKind::Backspace, m) == KeysMenu::Result::Changed);
    CHECK(m.keys.keys(KeyContext::General, KeyAction::Pause).empty());
    CHECK(find_text(menu, m, ui::tr("keys.settings.none")).has_value());
    // Escape with nothing waited for closes the window.
    CHECK(menu.key(press(27, 41), KeysMenu::KeyKind::Escape, m) == KeysMenu::Result::Close);
}

TEST_CASE("keys window: a key another action had moves over and the window says so; W A S D stay by place") {
    ui::set_language(ui::kFallbackLanguage);
    app::KeysModel m = model();
    KeysMenu menu;
    menu.open();
    // Pause gets H, which Help had.
    (void)click(menu, m, "P");
    CHECK(menu.key(press('h', 11), KeysMenu::KeyKind::Other, m) == KeysMenu::Result::Changed);
    CHECK(m.keys.find(KeyContext::General, {'h'})->action == KeyAction::Pause);
    CHECK(m.keys.keys(KeyContext::General, KeyAction::Help).empty());
    CHECK(menu.notice() == ui::tr("keys.settings.moved", {"H", ui::tr("keys.action.help")}));
    // Holding a position on H with units selected is another context: it keeps its key.
    CHECK(m.keys.find(KeyContext::Units, {'h'})->action == KeyAction::HoldPosition);
    // The map's keys go by their place, as Open1602's do: the scancode is taken.
    (void)click(menu, m, "[W]");
    CHECK(menu.key(press('z', 29), KeysMenu::KeyKind::Other, m) == KeysMenu::Result::Changed);
    CHECK(m.keys.keys(KeyContext::General, KeyAction::ScrollUp) ==
          (std::vector<ui::KeyChord>{{29, false, false, false, true}}));
    // "Defaults" brings every key back.
    CHECK(click(menu, m, ui::tr("keys.settings.defaults")) == KeysMenu::Result::Changed);
    CHECK(m.keys.write_changes(m.defaults, name).empty());
    CHECK(menu.notice() == ui::tr("keys.settings.reset"));
    // The tabs turn the pages; "Close" closes.
    (void)click(menu, m, ui::tr("keys.page.units"));
    CHECK(menu.page() == 3);
    CHECK(find_text(menu, m, ui::tr("keys.action.stop")).has_value());
    CHECK(click(menu, m, ui::tr("display.close")) == KeysMenu::Result::Close);
}

TEST_CASE("keys window: every page fits the window with Open1602's keys, in English and German") {
    // The real defaults are the platform's (map_input.cpp); every action in its context is at most
    // what they bind, so a table of all of them shows the most rows a page can have.
    app::KeysModel m;
    for (std::uint16_t i = 0; !ui::KeyBindings::name(static_cast<KeyAction>(i)).empty(); ++i) {
        const auto a = static_cast<KeyAction>(i);
        const bool editor = ui::KeyBindings::name(a).starts_with("editor");
        m.defaults.bind(editor ? KeyContext::Editor : KeyContext::General, {static_cast<std::int32_t>(1000 + i)}, a);
    }
    m.keys = m.defaults;
    for (std::size_t p = 0; p < KeysMenu::kPages; ++p) {
        CHECK(KeysMenu::rows(m, p).size() <= 2 * KeysMenu::kColumnRows);
    }
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        for (const char* key : {"keys.page.editor", "keys.settings.title", "keys.settings.hint", "keys.settings.moved",
                                "keys.context.units", "display.keys"}) {
            CHECK(ui::tr(key) != key);
        }
    }
    ui::set_language(ui::kFallbackLanguage);
}
