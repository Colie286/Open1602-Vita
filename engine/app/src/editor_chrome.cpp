// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor_chrome.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

constexpr Id kFirstMenu = 1;    ///< + the menu
constexpr Id kFirstEntry = 20;  ///< + the entry of the open menu
constexpr Id kFirstTool = 50;   ///< + EditorTool

constexpr std::int32_t kToolButton = 34;  ///< a tool's button, square
constexpr std::int32_t kToolGap = 2;
constexpr std::int32_t kGroupGap = 8;
constexpr std::int32_t kEntryH = 26;
constexpr std::int32_t kMenuW = 250;

constexpr std::array<const char*, EditorChrome::kMenus> kMenuTexts = {
    "editor.menu.file",    "editor.menu.edit",  "editor.menu.view",  "editor.menu.map",
    "editor.menu.players", "editor.menu.rules", "editor.menu.check", "editor.menu.help"};

constexpr std::array<EditorChrome::ToolEntry, kEditorTools> kTools = {{
    {EditorTool::Select, "editor.tool.select", true},
    {EditorTool::Island, "editor.tool.island", false},
    {EditorTool::Placeholder, "editor.tool.placeholder", true},
    {EditorTool::Land, "editor.tool.land", false},
    {EditorTool::Water, "editor.tool.water", false},
    {EditorTool::Coast, "editor.tool.coast", true},
    {EditorTool::Brush, "editor.tool.brush", false},
    {EditorTool::Random, "editor.tool.random", false},
    {EditorTool::Fill, "editor.tool.fill", false},
    {EditorTool::Replace, "editor.tool.replace", false},
    {EditorTool::Pipette, "editor.tool.pipette", false},
    {EditorTool::Area, "editor.tool.area", false},
    {EditorTool::Paste, "editor.tool.paste", true},
    {EditorTool::Build, "editor.tool.build", false},
    {EditorTool::Template, "editor.tool.template", false},
    {EditorTool::Ship, "editor.tool.ship", false},
    {EditorTool::Unit, "editor.tool.unit", false},
    {EditorTool::Resource, "editor.tool.resource", false},
    {EditorTool::Remove, "editor.tool.remove", false},
}};

/// The tools' symbols: 12 x 12 cells, '#' drawn, by EditorTool.
using Icon = std::array<std::string_view, 12>;
constexpr std::array<Icon, kEditorTools> kIcons = {{
    // Select: an arrow
    {"#...........", "##..........", "#.#.........", "#..#........", "#...#.......", "#....#......", "#.....#.....",
     "#..####.....", "#.#..#......", "##...#......", "#.....#.....", "......#....."},
    // Island: land on the sea line
    {"............", "....####....", "..########..", ".##########.", ".##########.", "..########..", "....####....",
     "............", "##..##..##..", ".##..##..##.", "............", "............"},
    // Placeholder: a dashed frame
    {"##.##.##.##.", "#..........#", "............", "#..........#", "#..........#", "............", "#..........#",
     "#..........#", "............", "#..........#", "##.##.##.##.", "............"},
    // Land: a filled shape
    {"............", "...######...", ".##########.", "############", "############", "############", "###########.",
     ".#########..", "..#######...", "....###.....", "............", "............"},
    // Water: waves
    {"............", "............", ".##....##...", "#..#..#..#..", "....##....##", "............", ".##....##...",
     "#..#..#..#..", "....##....##", "............", "............", "............"},
    // Coast: land over waves
    {"######......", "########....", "#########...", "#######.....", "#####.......", "............", ".##....##...",
     "#..#..#..#..", "....##....##", "............", ".##....##...", "#..#..#..#.."},
    // Brush: a paint brush, its bristles down
    {"....##......", "....##......", "....##......", "....##......", "...####.....", "...####.....", "..######....",
     ".########...", ".########...", ".########...", "..#.##.#....", "............"},
    // Random: a die
    {"############", "#..........#", "#.##....##.#", "#.##....##.#", "#..........#", "#....##....#", "#....##....#",
     "#..........#", "#.##....##.#", "#.##....##.#", "#..........#", "############"},
    // Fill: a bucket
    {"....##......", "...#..#.....", "..#....#....", ".########...", ".#......#.#.", ".#......#.#.", ".#......#.##",
     "..#....#..##", "...####.....", "............", "............", "............"},
    // Replace: two arrows
    {"............", "..#.........", ".##.........", "##########..", ".##.......#.", "..#.......#.", ".#.......#..",
     ".#.......##.", "..##########", ".........##.", ".........#..", "............"},
    // Pipette: a dropper, its bulb up, a drop below
    {".........###", "........####", ".......#####", "......####..", ".....#.##...", "....#.#.....", "...#.#......",
     "..#.#.......", ".#.#........", ".##.........", "............", "#..........."},
    // Area: the corners of a rectangle
    {"####....####", "#..........#", "#..........#", "#..........#", "............", "............", "............",
     "............", "#..........#", "#..........#", "#..........#", "####....####"},
    // Paste: a sheet on a board
    {"....####....", ".###....###.", ".#..####..#.", ".#........#.", ".#.######.#.", ".#........#.", ".#.######.#.",
     ".#........#.", ".#.####...#.", ".#........#.", ".##########.", "............"},
    // Build: a house
    {".....##.....", "....####....", "...######...", "..########..", ".##########.", "############", "..#......#..",
     "..#......#..", "..#..##..#..", "..#..##..#..", "..########..", "............"},
    // Template: two houses
    {"..#.........", ".###........", "#####.......", ".#.#..#.....", ".###.###....", "....#####...", ".....#.#.#..",
     ".....###.##.", "........####", "........#..#", "........####", "............"},
    // Ship
    {".....#......", ".....##.....", ".....###....", ".....####...", ".....#####..", ".....#......", "############",
     ".##########.", "..########..", "............", ".##....##...", "#..#..#..#.."},
    // Unit: a figure
    {".....##.....", "....####....", ".....##.....", "...######...", "..#.####.#..", "..#.####.#..", ".....##.....",
     "....#..#....", "....#..#....", "....#..#....", "...##..##...", "............"},
    // Resource: a gem
    {"............", "...######...", "..#.#..#.#..", ".#..#..#..#.", "############", ".#...##...#.", "..#..##..#..",
     "...#.##.#...", "....####....", ".....##.....", "............", "............"},
    // Remove: a cross
    {"##........##", "###......###", ".###....###.", "..###..###..", "...######...", "....####....", "....####....",
     "...######...", "..###..###..", ".###....###.", "###......###", "##........##"},
}};

std::int32_t px(std::int32_t v, double scale) {
    return static_cast<std::int32_t>(std::floor(v * scale));
}

/// The symbol in the button `r` (design units) as fills of palette colour `colour`.
void draw_icon(std::vector<ui::DrawItem>& out, const Icon& icon, const Rect& r, std::int32_t colour, double scale) {
    constexpr std::int32_t kCell = 2;
    const std::int32_t x0 = r.x + (r.w - 12 * kCell) / 2;
    const std::int32_t y0 = r.y + (r.h - 12 * kCell) / 2;
    for (std::size_t row = 0; row < icon.size(); ++row) {
        const std::string_view line = icon[row];
        std::size_t c = 0;
        while (c < line.size()) {
            if (line[c] != '#') {
                ++c;
                continue;
            }
            const std::size_t start = c;
            while (c < line.size() && line[c] == '#') {
                ++c;
            }
            ui::DrawItem d;
            d.kind = ui::DrawItem::Kind::Fill;
            const auto cx = x0 + static_cast<std::int32_t>(start) * kCell;
            const auto cy = y0 + static_cast<std::int32_t>(row) * kCell;
            d.x = px(cx, scale);
            d.y = px(cy, scale);
            d.w = std::max(1, px(cx + static_cast<std::int32_t>(c - start) * kCell, scale) - d.x);
            d.h = std::max(1, px(cy + kCell, scale) - d.y);
            d.colour = colour;
            out.push_back(d);
        }
    }
}

/// The width of `text` in design units in `font` (without one every byte is 9 units wide, more
/// than any letter of the panels' font).
std::int32_t text_width(const ui::Font* font, const std::string& text) {
    return font != nullptr ? static_cast<std::int32_t>(std::ceil(font->width(text) / font->design_scale()))
                           : static_cast<std::int32_t>(text.size()) * 9;
}

}  // namespace

const std::array<EditorChrome::ToolEntry, kEditorTools>& EditorChrome::tools() noexcept {
    return kTools;
}

const char* EditorChrome::command_text(EditorCommand command) noexcept {
    switch (command) {
        case EditorCommand::New:
            return "editor.new";
        case EditorCommand::Open:
            return "editor.open";
        case EditorCommand::Save:
            return "editor.save";
        case EditorCommand::SaveAs:
            return "editor.save_as";
        case EditorCommand::Close:
            return "editor.close";
        case EditorCommand::Undo:
            return "editor.undo";
        case EditorCommand::Redo:
            return "editor.redo";
        case EditorCommand::View:
            return "editor.view";
        case EditorCommand::Islands:
            return "editor.islands.edit";
        case EditorCommand::WorldSize:
            return "editor.world_size.edit";
        case EditorCommand::Random:
            return "editor.random.edit";
        case EditorCommand::Preview:
            return "editor.random.roll";
        case EditorCommand::Players:
            return "editor.players.edit";
        case EditorCommand::Rules:
            return "editor.rules.edit";
        case EditorCommand::Goals:
            return "editor.goals.edit";
        case EditorCommand::Scenario:
            return "editor.scenario.edit";
        case EditorCommand::Check:
            return "editor.check";
        case EditorCommand::Keys:
            return "editor.menu.keys";
    }
    return "editor.title";
}

void EditorChrome::set_screen(std::int32_t width, std::int32_t height, double scale) noexcept {
    m_scale = scale > 0.0 ? scale : 1.0;
    m_width = static_cast<std::int32_t>(width / m_scale);
    m_height = static_cast<std::int32_t>(height / m_scale);
    m_form.set_scale(m_scale);
    m_back.set_scale(m_scale);
}

std::vector<EditorChrome::MenuEntry> EditorChrome::entries(std::size_t menu, const EditorModel& model) const {
    const bool plan = model.has_plan && !model.previewing;
    switch (menu) {
        case 0:
            return {{EditorCommand::New, true},
                    {EditorCommand::Open, true},
                    {EditorCommand::Save, true},
                    {EditorCommand::SaveAs, true},
                    {EditorCommand::Close, true}};
        case 1:
            return {{EditorCommand::Undo, model.can_undo}, {EditorCommand::Redo, model.can_redo}};
        case 2:
            return {{EditorCommand::View, true}};
        case 3:
            return {{EditorCommand::Islands, true},
                    {EditorCommand::WorldSize, true},
                    {EditorCommand::Random, plan},
                    {EditorCommand::Preview, plan}};
        case 4:
            return {{EditorCommand::Players, true}};
        case 5:
            return {{EditorCommand::Scenario, true}, {EditorCommand::Rules, true}, {EditorCommand::Goals, true}};
        case 6:
            return {{EditorCommand::Check, true}};
        default:
            return {{EditorCommand::Keys, true}};
    }
}

Rect EditorChrome::menu_rect() const noexcept {
    if (!m_open) {
        return {};
    }
    // Below its entry, within the map's width.
    const std::int32_t right = m_width - kColumn;
    const std::int32_t x = std::min(m_menu_x[*m_open], std::max(0, right - m_menu_w));
    return {x, kMenuH, m_menu_w, m_menu_h};
}

void EditorChrome::layout(const EditorModel& model, const EditorChromeModel& chrome, std::optional<EditorTool> active) {
    ui::Form& f = m_form;
    f.clear();
    m_icons.clear();
    const std::int32_t map_w = std::max(0, m_width - kColumn);
    // The menu bar, and the map's name at its right end.
    f.panel(Rect{0, 0, map_w, kMenuH});
    std::int32_t x = 2;
    for (std::size_t m = 0; m < kMenus; ++m) {
        const std::string text = ui::tr(kMenuTexts[m]);
        const std::int32_t w = text_width(m_font, text) + 2 * ui::kTextPadding + 6;
        m_menu_x[m] = x;
        f.button(kFirstMenu + static_cast<Id>(m), Rect{x, 2, w, kMenuH - 4}, text, true, m_open == m);
        x += w + 2;
    }
    m_menu_x[kMenus] = x;
    std::string name = model.file.empty() ? ui::tr("editor.untitled") : model.file;
    if (model.modified) {
        name += " *";
    }
    if (map_w - x - 8 > 40) {
        f.label(Rect{x + 4, 0, map_w - x - 12, kMenuH}, name, Align::Right, Tone::Heading);
    }
    // The tool bar down the map's left edge.
    const std::int32_t bottom = m_height - kStatusH;
    f.panel(Rect{0, kMenuH, kToolW, std::max(0, bottom - kMenuH)});
    std::int32_t y = kMenuH + 4;
    for (const ToolEntry& t : kTools) {
        if (y + kToolButton > bottom - 2) {
            break;  // a window too low for all of them: the last go (their keys still work)
        }
        const bool usable = t.tool != EditorTool::Placeholder || model.has_plan;
        const Rect r{(kToolW - kToolButton) / 2, y, kToolButton, kToolButton};
        f.button(kFirstTool + static_cast<Id>(t.tool), r, {}, usable && !model.previewing, active == t.tool);
        m_icons.emplace_back(r, t.tool);
        y += kToolButton + (t.gap_after ? kGroupGap : kToolGap);
    }
    // The status line: what the mouse points at or what happened, and where on the map it is.
    f.panel(Rect{0, bottom, map_w, kStatusH});
    const std::string hint_text = hint(chrome);
    // Over the map, why the tool in hand would not place there comes before the last notice.
    const bool hover = hint_text.empty() && !chrome.hover.empty();
    const std::string& text = !hint_text.empty() ? hint_text : hover ? chrome.hover : chrome.status;
    const std::int32_t place_w = chrome.place.empty() ? 0 : std::min(map_w / 2, 260);
    f.label(Rect{6, bottom, std::max(0, map_w - place_w - 12), kStatusH}, text, Align::Left,
            hover || (hint_text.empty() && chrome.status_error) ? Tone::Warning : Tone::Normal);
    if (place_w > 0) {
        f.label(Rect{map_w - place_w - 6, bottom, place_w, kStatusH}, chrome.place, Align::Right, Tone::Dim);
    }
    // The menu open, over the map.
    if (m_open) {
        // As wide as its longest command and key, the command on the left, its key on the right.
        const std::vector<MenuEntry> list = entries(*m_open, model);
        const auto key_of = [&](EditorCommand command) {
            const auto c = static_cast<std::size_t>(command);
            return c < chrome.command_keys.size() ? chrome.command_keys[c] : std::string{};
        };
        m_menu_w = kMenuW;
        for (const MenuEntry& e : list) {
            const std::string key = key_of(e.command);
            const std::int32_t w = text_width(m_font, ui::tr(command_text(e.command))) +
                                   (key.empty() ? 0 : 24 + text_width(m_font, key)) + 2 * ui::kTextPadding + 16;
            m_menu_w = std::max(m_menu_w, w);
        }
        m_menu_h = static_cast<std::int32_t>(list.size()) * kEntryH + 8;
        const Rect r = menu_rect();
        f.panel(r);
        for (std::size_t i = 0; i < list.size(); ++i) {
            const Rect e{r.x + 4, r.y + 4 + static_cast<std::int32_t>(i) * kEntryH, r.w - 8, kEntryH - 2};
            const Tone tone = list[i].enabled ? Tone::Normal : Tone::Dim;
            f.button(kFirstEntry + static_cast<Id>(i), e, {}, list[i].enabled);
            f.label(e, ui::tr(command_text(list[i].command)), Align::Left, tone);
            if (const std::string key = key_of(list[i].command); !key.empty()) {
                f.label(e, key, Align::Right, Tone::Dim);
            }
        }
    }
}

std::string EditorChrome::hint(const EditorChromeModel& chrome) const {
    const auto id = m_form.hovered();
    if (!id || *id < kFirstTool || *id >= kFirstTool + static_cast<Id>(kEditorTools)) {
        return {};
    }
    const auto t = static_cast<std::size_t>(*id - kFirstTool);
    for (const ToolEntry& e : kTools) {
        if (static_cast<std::size_t>(e.tool) == t) {
            std::string text = ui::tr(e.text);
            if (!chrome.tool_keys[t].empty()) {
                text += " (" + chrome.tool_keys[t] + ")";
            }
            return text;
        }
    }
    return {};
}

std::vector<ui::DrawItem> EditorChrome::draw_back(const std::array<const ui::Font*, ui::kFontSlots>& fonts,
                                                  const ui::Rect& minimap) {
    m_back.clear();
    m_back.panel(Rect{m_width - kColumn, 0, kColumn, m_height});
    if (minimap.w > 0 && minimap.h > 0) {
        // The minimap's frame, close about it.
        constexpr std::int32_t kMargin = 6;
        const auto left = static_cast<std::int32_t>(std::floor(minimap.x / m_scale));
        const auto top = static_cast<std::int32_t>(std::floor(minimap.y / m_scale));
        const auto width = static_cast<std::int32_t>(std::ceil((minimap.x + minimap.w) / m_scale)) - left;
        const auto height = static_cast<std::int32_t>(std::ceil((minimap.y + minimap.h) / m_scale)) - top;
        m_back.panel(Rect{left - kMargin, top - kMargin, width + 2 * kMargin, height + 2 * kMargin});
    }
    return m_back.draw(fonts);
}

std::vector<ui::DrawItem> EditorChrome::draw(const EditorModel& model, const EditorChromeModel& chrome,
                                             std::optional<EditorTool> active,
                                             const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    m_font = m_form.font(fonts);
    layout(model, chrome, active);
    std::vector<ui::DrawItem> items = m_form.draw(fonts);
    const ui::FormStyle style{};
    // The symbols over their buttons: gold, the chosen one's light, a tool that cannot be used grey.
    for (const auto& [r, tool] : m_icons) {
        const bool usable = (tool != EditorTool::Placeholder || model.has_plan) && !model.previewing;
        const std::int32_t colour = !usable ? 1 : active == tool ? 15 : style.frame;
        // The menu open lies over the tool bar's top: no symbol through it.
        if (m_open) {
            const Rect m = menu_rect();
            if (r.x < m.x + m.w && r.x + r.w > m.x && r.y < m.y + m.h && r.y + r.h > m.y) {
                continue;
            }
        }
        draw_icon(items, kIcons[static_cast<std::size_t>(tool)], r, colour, m_scale);
    }
    return items;
}

EditorChrome::Result EditorChrome::mouse(std::int32_t x, std::int32_t y, bool down, const EditorModel& model,
                                         const EditorChromeModel& chrome, std::optional<EditorTool> active,
                                         bool& taken) {
    layout(model, chrome, active);
    Result result;
    const bool was_open = m_open.has_value();
    const bool over = covers(x, y);
    const auto id = m_form.mouse(x, y, down);
    taken = over;
    if (!id) {
        // A press beside an open menu closes it, and does nothing else (not on the map, the tool
        // bar or the panel).
        const auto dx = static_cast<std::int32_t>(x / m_scale);
        const auto dy = static_cast<std::int32_t>(y / m_scale);
        if (down && was_open && dy >= kMenuH && !menu_rect().contains(dx, dy)) {
            m_open.reset();
            taken = true;
        }
        return result;
    }
    if (*id >= kFirstMenu && *id < kFirstMenu + static_cast<Id>(kMenus)) {
        const auto m = static_cast<std::size_t>(*id - kFirstMenu);
        const std::vector<MenuEntry> list = entries(m, model);
        if (list.size() == 1 && m >= 4 && m <= 6) {
            // Players, rules, check: a page at once.
            m_open.reset();
            result.command = list.front().command;
        } else {
            m_open = m_open == m ? std::nullopt : std::optional<std::size_t>(m);
        }
        return result;
    }
    if (*id >= kFirstEntry && *id < kFirstEntry + 10 && m_open) {
        const std::vector<MenuEntry> list = entries(*m_open, model);
        const auto i = static_cast<std::size_t>(*id - kFirstEntry);
        m_open.reset();
        if (i < list.size() && list[i].enabled) {
            result.command = list[i].command;
        }
        return result;
    }
    if (*id >= kFirstTool && *id < kFirstTool + static_cast<Id>(kEditorTools)) {
        m_open.reset();
        result.tool = static_cast<EditorTool>(*id - kFirstTool);
    }
    return result;
}

bool EditorChrome::covers(std::int32_t x, std::int32_t y) const noexcept {
    const auto dx = static_cast<std::int32_t>(x / m_scale);
    const auto dy = static_cast<std::int32_t>(y / m_scale);
    if (m_open) {
        return true;  // a menu open: a click anywhere else only closes it
    }
    const std::int32_t map_w = m_width - kColumn;
    if (dx >= map_w) {
        return false;  // the column: the panel's
    }
    return dy < kMenuH || dy >= m_height - kStatusH || dx < kToolW;
}

bool EditorChrome::close_menu() noexcept {
    return std::exchange(m_open, std::nullopt).has_value();
}

}  // namespace o1602::app
