// SPDX-License-Identifier: GPL-3.0-or-later
#include "keys_menu.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "key_help.hpp"

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::KeyAction;
using ui::KeyContext;
using ui::Rect;

constexpr Id kTab = 1;  // + page
constexpr Id kDefaults = 10;
constexpr Id kClose = 11;
constexpr Id kRow = 100;  // + the row's place on the page

constexpr std::array<std::string_view, KeysMenu::kPages> kPageTitles = {"keys.page.map_game", "keys.page.windows",
                                                                        "keys.page.building", "keys.page.units",
                                                                        "keys.page.view",     "keys.page.editor"};
constexpr std::size_t kEditorPage = 5;

constexpr std::int32_t kMargin = 24;
constexpr std::int32_t kTabY = 60;
constexpr std::int32_t kRowY = 108;
constexpr std::int32_t kRowH = 30;
constexpr std::int32_t kColumnGap = 20;
constexpr std::int32_t kColumnW = (KeysMenu::kWidth - 2 * kMargin - kColumnGap) / 2;
constexpr std::int32_t kKeyW = 200;
constexpr std::int32_t kBottomY = KeysMenu::kHeight - 48;

/// Actions of a number (the groups' digits, the editor's bookmarks): the digits stay theirs here;
/// keys.cfg changes them.
bool numbered(KeyAction a) noexcept {
    return a == KeyAction::GroupStore || a == KeyAction::GroupAdd || a == KeyAction::GroupRecall ||
           a == KeyAction::EditorBookmarkSet || a == KeyAction::EditorBookmarkGo;
}

std::string chord_label(const KeysModel& model, const ui::KeyChord& chord) {
    if (model.key_label) {
        return model.key_label(chord);
    }
    return ui::KeyBindings::chord_text(chord, [](std::int32_t key, bool) { return std::to_string(key); });
}

std::string action_label(KeyAction action) {
    return ui::tr("keys.action." + std::string(ui::KeyBindings::name(action)));
}

}  // namespace

std::size_t KeysMenu::page_of(const KeyRow& row) noexcept {
    if (row.context == KeyContext::Editor) {
        return kEditorPage;
    }
    return key_help_place(row.action).page;
}

std::vector<KeyRow> KeysMenu::rows(const KeysModel& model, std::size_t page) {
    std::vector<KeyRow> out;
    for (const ui::KeyBindings* table : {&model.defaults, &model.keys}) {
        for (const ui::KeyBinding& b : table->all()) {
            const KeyRow row{b.context, b.action};
            if (b.param != 0 || numbered(b.action) || page_of(row) != page ||
                std::find(out.begin(), out.end(), row) != out.end()) {
                continue;
            }
            out.push_back(row);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const KeyRow& a, const KeyRow& b) {
        return a.action != b.action ? a.action < b.action : a.context < b.context;
    });
    return out;
}

std::string KeysMenu::keys_text(const KeysModel& model, const KeyRow& row) {
    std::string out;
    for (const ui::KeyChord& c : model.keys.keys(row.context, row.action)) {
        if (!out.empty()) {
            out += ", ";
        }
        out += chord_label(model, c);
    }
    return out.empty() ? ui::tr("keys.settings.none") : out;
}

std::string KeysMenu::action_text(const KeysModel& model, const KeyRow& row, std::size_t page) {
    std::string text = action_label(row.action);
    // An action with keys in two contexts names the context of each.
    const std::vector<KeyRow> all = rows(model, page);
    const auto same = std::count_if(all.begin(), all.end(), [&](const KeyRow& r) { return r.action == row.action; });
    if (same > 1) {
        text += " (" + ui::tr("keys.context." + std::string(ui::KeyBindings::context_name(row.context))) + ")";
    }
    return text;
}

std::vector<ui::DrawItem> KeysMenu::draw(const KeysModel& model,
                                         const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    layout(model);
    return m_form.draw(fonts);
}

KeysMenu::Result KeysMenu::mouse(std::int32_t x, std::int32_t y, bool down, KeysModel& model, ui::MouseButton button) {
    layout(model);
    const auto id = m_form.mouse(x, y, down, button);
    if (!id) {
        if (m_form.released()) {
            m_wait.reset();  // a click beside the keys: nothing is waited for
        }
        return Result::None;
    }
    if (*id == kClose) {
        m_wait.reset();
        return Result::Close;
    }
    if (*id == kDefaults) {
        model.keys = model.defaults;
        m_wait.reset();
        m_notice = ui::tr("keys.settings.reset");
        return Result::Changed;
    }
    if (*id >= kTab && *id < kTab + static_cast<Id>(kPages)) {
        m_page = static_cast<std::size_t>(*id - kTab);
        m_wait.reset();
        m_notice.clear();
        return Result::None;
    }
    const std::vector<KeyRow> page = rows(model, m_page);
    if (*id >= kRow && *id < kRow + static_cast<Id>(page.size())) {
        const KeyRow row = page[static_cast<std::size_t>(*id - kRow)];
        if (m_wait && m_wait->row == row) {
            m_wait.reset();  // clicked again: as it was
        } else {
            m_wait = Wait{row, m_form.clicked_with() == ui::MouseButton::Right};
        }
        m_notice.clear();
    }
    return Result::None;
}

KeysMenu::Result KeysMenu::key(const ui::KeyPress& press, KeyKind kind, KeysModel& model) {
    if (!m_wait) {
        return kind == KeyKind::Escape ? Result::Close : Result::None;
    }
    switch (kind) {
        case KeyKind::Modifier:
            return Result::None;  // the key it goes with comes next
        case KeyKind::Escape:
            m_wait.reset();
            return Result::None;
        case KeyKind::Backspace:
            model.keys.unbind(m_wait->row.context, m_wait->row.action);
            m_wait.reset();
            m_notice.clear();
            return Result::Changed;
        case KeyKind::Other:
            break;
    }
    const KeyRow row = m_wait->row;
    // By its place where Open1602's keys of the action go by their place (W A S D), else by what it
    // means in the keyboard's layout.
    const std::vector<ui::KeyChord> defaults = model.defaults.keys(row.context, row.action);
    const bool physical =
        std::any_of(defaults.begin(), defaults.end(), [](const ui::KeyChord& c) { return c.physical; });
    const ui::KeyChord chord{physical ? press.scancode : press.key, press.ctrl, press.shift, press.alt, physical};
    std::optional<ui::KeyBinding> taken;
    for (const ui::KeyBinding& b : model.keys.all()) {
        if (b.context == row.context && b.chord == chord && !(b.action == row.action && b.param == 0)) {
            taken = b;
        }
    }
    if (!m_wait->add) {
        model.keys.unbind(row.context, row.action);
    }
    model.keys.bind(row.context, chord, row.action);
    m_wait.reset();
    m_notice =
        taken ? ui::tr("keys.settings.moved", {chord_label(model, chord), action_label(taken->action)}) : std::string{};
    return Result::Changed;
}

void KeysMenu::layout(const KeysModel& model) {
    ui::Form& f = m_form;
    f.clear();
    f.panel(Rect{0, 0, kWidth, kHeight});
    f.label(Rect{kMargin, 14, kWidth - 2 * kMargin, 40}, ui::tr("keys.settings.title"), Align::Left, Tone::Heading);
    constexpr std::int32_t kTabGap = 8;
    constexpr auto kTabW =
        (kWidth - 2 * kMargin - static_cast<std::int32_t>(kPages - 1) * kTabGap) / static_cast<std::int32_t>(kPages);
    for (std::size_t p = 0; p < kPages; ++p) {
        f.button(kTab + static_cast<Id>(p),
                 Rect{kMargin + static_cast<std::int32_t>(p) * (kTabW + kTabGap), kTabY, kTabW, 36},
                 ui::tr(kPageTitles[p]), true, p == m_page);
    }
    const std::vector<KeyRow> page = rows(model, m_page);
    for (std::size_t i = 0; i < page.size() && i < 2 * kColumnRows; ++i) {
        const KeyRow& row = page[i];
        const std::int32_t x = kMargin + (i < kColumnRows ? 0 : kColumnW + kColumnGap);
        const std::int32_t y = kRowY + static_cast<std::int32_t>(i % kColumnRows) * kRowH;
        f.label(Rect{x, y, kColumnW - kKeyW - 4, kRowH - 4}, action_text(model, row, m_page));
        const Rect key{x + kColumnW - kKeyW, y, kKeyW, kRowH - 4};
        const Id id = kRow + static_cast<Id>(i);
        if (m_wait && m_wait->row == row) {
            f.button(id, key, ui::tr(m_wait->add ? "keys.settings.press_add" : "keys.settings.press"), true, true);
        } else {
            f.cycle_button(id, key, keys_text(model, row));
        }
    }
    const std::int32_t below = kRowY + static_cast<std::int32_t>(kColumnRows) * kRowH + 4;
    const Rect hint{kMargin, below, kWidth - 2 * kMargin, kBottomY - 4 - below};
    if (!m_notice.empty()) {
        f.paragraph(hint, m_notice, Tone::Warning);
    } else {
        f.paragraph(hint, ui::tr(m_wait ? "keys.settings.hint.wait" : "keys.settings.hint"), Tone::Dim);
    }
    f.button(kDefaults, Rect{kMargin, kBottomY, 240, 40}, ui::tr("keys.settings.defaults"));
    f.button(kClose, Rect{kWidth - kMargin - 180, kBottomY, 180, 40}, ui::tr("display.close"));
}

}  // namespace o1602::app
