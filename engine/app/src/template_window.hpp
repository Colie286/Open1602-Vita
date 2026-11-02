// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window of the templates (Open1602, as in later Anno games; templates.hpp), beside the side
// panel. Its list shows each template with a small view from above, its name, size, number of
// buildings and costs, and takes one into the hand of the copy tool, renames it (typed in its row,
// as a ship's name in the ship's window) or deletes it (after a second click). Saving a piece the
// copy tool holds (Ctrl+S) asks for the name in the same window. The window only edits what is typed
// and tells what was clicked; the viewer keeps the templates.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/sim/construction.hpp"
#include "o1602/ui/form.hpp"

namespace o1602::app {

/// One template as the list shows it.
struct TemplateEntry {
    std::string name;  ///< Windows-1252
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::size_t buildings = 0;
    sim::BuildBill cost;
    /// The view from above: the footprints of the items, each in the colour of its kind.
    struct Piece {
        enum class Kind : std::uint8_t { Building, House, Road, Field, Forest };
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t w = 1;
        std::int32_t h = 1;
        Kind kind = Kind::Building;
    };
    std::vector<Piece> view;
};

/// The palette colours of the views from above: the ground and each kind of piece.
struct TemplateColours {
    std::uint8_t ground = 0;
    std::array<std::uint8_t, 5> pieces{};  ///< by TemplateEntry::Piece::Kind
};

/// What the window shows; the viewer fills it while the window is open.
struct TemplatesModel {
    std::vector<TemplateEntry> entries;
    /// Saving: the piece in hand (its size and buildings), to show beside the name.
    TemplateEntry piece;
};

/// What a click or a key asked for.
struct TemplateRequest {
    enum class Kind : std::uint8_t { Take, Rename, Remove, Save };
    Kind kind = Kind::Take;
    std::size_t index = 0;  ///< Take, Rename, Remove: the template in the model's list
    std::string name;       ///< Rename, Save: the name typed
};

class TemplateWindow {
public:
    static constexpr std::int32_t kWidth = 460;
    /// Templates on one page of the list.
    static constexpr std::size_t kRows = 4;

    explicit TemplateWindow(ui::FormStyle style = {}) : m_form(style) {}

    void set_style(const ui::FormStyle& style) { m_form.set_style(style); }
    void set_scale(double scale) { m_form.set_scale(scale); }
    [[nodiscard]] double scale() const noexcept { return m_form.scale(); }
    void set_colours(const TemplateColours& colours) noexcept { m_colours = colours; }

    /// Opens the list.
    void open_list();
    /// Asks for the name of a new template, `name` typed in already.
    void open_save(std::string_view name);
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept { return m_mode != Mode::Closed; }
    [[nodiscard]] bool saving() const noexcept { return m_mode == Mode::Save; }
    /// A name is being typed: the keys go here.
    [[nodiscard]] bool typing() const noexcept { return m_mode == Mode::Save || m_renaming.has_value(); }
    [[nodiscard]] const std::string& text() const noexcept { return m_text; }
    /// The window's height in design units.
    [[nodiscard]] std::int32_t height(const TemplatesModel& model) const;

    /// Everything to draw, relative to the window's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const TemplatesModel& model,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    /// The mouse in pixels relative to the window's top left.
    std::optional<TemplateRequest> mouse(std::int32_t x, std::int32_t y, bool down, const TemplatesModel& model);
    /// Text typed (Windows-1252).
    void type(std::string_view text);
    /// Ctrl+A 1, Backspace 8, Enter 13 keeps the name, Escape 27 takes it back (and closes the
    /// question for a new template's name).
    std::optional<TemplateRequest> key(int key);

    /// "6 x 5, 12 buildings, 340 gold, 4 t tools": what a template is, on one line.
    [[nodiscard]] static std::string summary(const TemplateEntry& entry);

private:
    enum class Mode : std::uint8_t { Closed, List, Save };
    void layout(const TemplatesModel& model);
    void view(const TemplateEntry& entry, ui::Rect box);
    std::optional<TemplateRequest> commit();

    ui::Form m_form;
    Mode m_mode = Mode::Closed;
    std::string m_text;                     ///< the name being typed
    std::optional<std::size_t> m_renaming;  ///< the template whose name is typed
    std::optional<std::size_t> m_removing;  ///< the template whose delete button asks again
    std::size_t m_page = 0;
    TemplateColours m_colours;
};

}  // namespace o1602::app
