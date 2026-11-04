// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's own frame around the map (ADR 0013): a menu bar above it, a tool bar on its left,
// a status bar below it and the column on its right that holds the minimap and the side panel's
// pages (EditorPanel). The frame only tells what was clicked; the panel and the viewer do it.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "editor_panel.hpp"

#include "o1602/ui/form.hpp"

namespace o1602::app {

/// What the frame shows besides the panel's model: the keys of the tools and the commands (as
/// keys.cfg has them, empty: none) and what lies under the mouse on the map.
struct EditorChromeModel {
    std::array<std::string, kEditorTools> tool_keys{};
    std::array<std::string, 21> command_keys{};  ///< by EditorCommand
    std::string status;                          ///< the status line's text (Windows-1252), the notice first
    bool status_error = false;
    std::string place;  ///< where the mouse is on the map (Windows-1252), empty: off the map
    /// Why the tool in hand would not place where the mouse is (Windows-1252), empty: it would.
    std::string hover;
};

class EditorChrome {
public:
    /// The column right of the map, design units wide (the side panel's room).
    static constexpr std::int32_t kColumn = EditorPanel::kWidth;
    static constexpr std::int32_t kMenuH = 26;
    static constexpr std::int32_t kToolW = 42;
    static constexpr std::int32_t kStatusH = 24;
    static constexpr std::size_t kMenus = 8;

    /// What a click asked for.
    struct Result {
        std::optional<EditorTool> tool;
        std::optional<EditorCommand> command;
    };

    explicit EditorChrome(ui::FormStyle style = {}) : m_form(style), m_back(style) {}

    void set_style(const ui::FormStyle& style) {
        m_form.set_style(style);
        m_back.set_style(style);
    }
    /// The window in pixels and the interface's scale (design units to pixels).
    void set_screen(std::int32_t width, std::int32_t height, double scale) noexcept;
    [[nodiscard]] double scale() const noexcept { return m_scale; }

    /// The column's ground, under the minimap and the panel, with a frame about the minimap at
    /// `minimap` (pixels; none if empty).
    [[nodiscard]] std::vector<ui::DrawItem> draw_back(const std::array<const ui::Font*, ui::kFontSlots>& fonts,
                                                      const ui::Rect& minimap = {});
    /// The bars and an open menu, over the map.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const EditorModel& model, const EditorChromeModel& chrome,
                                                 std::optional<EditorTool> active,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    /// The mouse in pixels of the window. `taken`: the frame had it (on a bar, or an open menu
    /// that the click closed), so the map and the panel must not.
    Result mouse(std::int32_t x, std::int32_t y, bool down, const EditorModel& model, const EditorChromeModel& chrome,
                 std::optional<EditorTool> active, bool& taken);
    /// Whether (x, y) in pixels is on a bar or an open menu.
    [[nodiscard]] bool covers(std::int32_t x, std::int32_t y) const noexcept;
    /// Escape: an open menu closes; false if none was open.
    bool close_menu() noexcept;
    [[nodiscard]] bool menu_open() const noexcept { return m_open.has_value(); }
    /// The tool or menu entry under the mouse: its name and key, for the status line.
    [[nodiscard]] std::string hint(const EditorChromeModel& chrome) const;

    /// The tool bar's tools in their order, with the gaps between the groups after them.
    struct ToolEntry {
        EditorTool tool;
        const char* text;  ///< localisation key of its name
        bool gap_after;
    };
    [[nodiscard]] static const std::array<ToolEntry, kEditorTools>& tools() noexcept;
    /// The localisation key of a command's name.
    [[nodiscard]] static const char* command_text(EditorCommand command) noexcept;

    /// The widgets as laid out last (tests).
    [[nodiscard]] const ui::Form& form() const noexcept { return m_form; }
    void layout(const EditorModel& model, const EditorChromeModel& chrome, std::optional<EditorTool> active);

private:
    struct MenuEntry {
        EditorCommand command;
        bool enabled;
    };
    [[nodiscard]] std::vector<MenuEntry> entries(std::size_t menu, const EditorModel& model) const;
    [[nodiscard]] ui::Rect menu_rect() const noexcept;

    ui::Form m_form;
    ui::Form m_back;
    double m_scale = 1.0;
    std::int32_t m_width = 1024;  ///< design units
    std::int32_t m_height = 768;
    std::optional<std::size_t> m_open;  ///< the menu open
    std::int32_t m_menu_w = 0;          ///< the menu open's size, as laid out
    std::int32_t m_menu_h = 0;
    const ui::Font* m_font = nullptr;                      ///< the panels' font, as drawn last
    std::array<std::int32_t, kMenus + 1> m_menu_x{};       ///< where the menu bar's entries begin, as laid out
    std::vector<std::pair<ui::Rect, EditorTool>> m_icons;  ///< the tool buttons as laid out, for their icons
};

}  // namespace o1602::app
