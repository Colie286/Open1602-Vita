// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window "Tastenbelegung" (key settings), Open1602's own: the keys of every action in the pages
// of the key overview (F1), and an editor page. A click on an action's keys waits for the next key
// pressed and gives the action that key in place of its keys, a right click adds it to them;
// Backspace takes every key from the action, Escape keeps what it had. A key another action of the
// same context had moves over, and the window says so. Opened from the window "Anzeige"; it only
// edits a KeysModel, the input applies and keeps what changed (keys.cfg).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "o1602/ui/form.hpp"
#include "o1602/ui/key_bindings.hpp"

namespace o1602::app {

struct KeysModel {
    ui::KeyBindings keys;      ///< as they are now
    ui::KeyBindings defaults;  ///< Open1602's
    /// A key as the window shows it (the key overview's names); empty: the names of keys.cfg.
    std::function<std::string(const ui::KeyChord&)> key_label;
};

/// An action of the window: its keys in one context.
struct KeyRow {
    ui::KeyContext context = ui::KeyContext::General;
    ui::KeyAction action = ui::KeyAction::Cancel;
    friend bool operator==(const KeyRow&, const KeyRow&) = default;
};

class KeysMenu {
public:
    enum class Result : std::uint8_t { None, Changed, Close };
    /// What a key pressed is besides its code, as the platform tells it.
    enum class KeyKind : std::uint8_t {
        Other,
        Modifier,   ///< Ctrl, Shift, Alt alone: waited for with the key they go with
        Escape,     ///< keeps the keys (or closes the window)
        Backspace,  ///< takes every key from the action waited for
    };
    /// Pages: the key overview's five, then the editor's.
    static constexpr std::size_t kPages = 6;
    /// The window's size in design units (it is centred over the map).
    static constexpr std::int32_t kWidth = 980;
    static constexpr std::int32_t kHeight = 748;
    /// Rows of one column; a page has two columns.
    static constexpr std::size_t kColumnRows = 18;

    explicit KeysMenu(ui::FormStyle style = {}) : m_form(style) {}

    void set_style(const ui::FormStyle& style) { m_form.set_style(style); }
    void set_scale(double scale) { m_form.set_scale(scale); }
    [[nodiscard]] double scale() const noexcept { return m_form.scale(); }

    /// Opens on the first page, waiting for nothing.
    void open() noexcept {
        m_page = 0;
        m_wait.reset();
        m_notice.clear();
    }
    [[nodiscard]] std::size_t page() const noexcept { return m_page; }
    /// The action waiting for its key, if any (the keys pressed meanwhile belong to the window).
    [[nodiscard]] std::optional<KeyRow> waiting() const noexcept {
        return m_wait ? std::optional<KeyRow>(m_wait->row) : std::nullopt;
    }
    /// What the last change said: which action lost the key.
    [[nodiscard]] const std::string& notice() const noexcept { return m_notice; }

    /// The actions of a page, in the order of ui::KeyAction (rows of `defaults` and of `keys`).
    [[nodiscard]] static std::vector<KeyRow> rows(const KeysModel& model, std::size_t page);
    /// The page an action of a context is on.
    [[nodiscard]] static std::size_t page_of(const KeyRow& row) noexcept;

    /// Everything to draw, relative to the window's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const KeysModel& model,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    /// The mouse in pixels relative to the window's top left; a click may change `model`.
    Result mouse(std::int32_t x, std::int32_t y, bool down, KeysModel& model,
                 ui::MouseButton button = ui::MouseButton::Left);
    /// A key pressed while the window is open.
    Result key(const ui::KeyPress& press, KeyKind kind, KeysModel& model);

    /// The text of a row's keys (for the buttons and the tests).
    [[nodiscard]] static std::string keys_text(const KeysModel& model, const KeyRow& row);
    /// The text of a row's action.
    [[nodiscard]] static std::string action_text(const KeysModel& model, const KeyRow& row, std::size_t page);

private:
    struct Wait {
        KeyRow row;
        bool add = false;  ///< the key goes with the action's keys instead of replacing them
    };
    void layout(const KeysModel& model);

    ui::Form m_form;
    std::size_t m_page = 0;
    std::optional<Wait> m_wait;
    std::string m_notice;
};

}  // namespace o1602::app
