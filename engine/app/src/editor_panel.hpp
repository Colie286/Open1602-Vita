// SPDX-License-Identifier: GPL-3.0-or-later
//
// The side panel of the editor (ADR 0013): below the head with the minimap, compass, rotation and
// zoom, where the game shows its modes. Its pages: what the document is (file, size, islands,
// players) with the buttons for the files and undo; a new map; the files to open; the name to
// save under. The panel only edits what is typed and tells what was clicked; the viewer keeps the
// document and fills the model every frame.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/ui/form.hpp"

namespace o1602::app {

/// The settings of a new map.
struct EditorNewMap {
    /// The sizes offered, in tiles: the original's first.
    static constexpr std::array<std::array<std::uint32_t, 2>, 5> kSizes = {
        {{500, 350}, {700, 500}, {1000, 1000}, {2000, 2000}, {4000, 4000}}};
    /// How many islands: none (only sea), few, as many as the original, many.
    enum class Islands : std::uint8_t { None, Few, Normal, Many };

    std::size_t size = 0;  ///< index into kSizes
    Islands islands = Islands::Normal;
    std::uint8_t players = 4;  ///< with a start (islands only), 1 .. sim::kMaxPlayers
    std::uint64_t seed = 0;    ///< the world the generator makes

    [[nodiscard]] std::uint32_t width() const noexcept { return kSizes[size % kSizes.size()][0]; }
    [[nodiscard]] std::uint32_t height() const noexcept { return kSizes[size % kSizes.size()][1]; }
    friend bool operator==(const EditorNewMap&, const EditorNewMap&) = default;
};

/// A file the editor can open, as the list shows it.
struct EditorFile {
    std::string name;  ///< Windows-1252
    /// The heading of the group the file belongs to (Windows-1252): own scenarios, the original's.
    std::string group;
};

/// What the panel shows; the viewer fills it every frame.
struct EditorModel {
    std::string file;  ///< the document's name (Windows-1252), empty: a new map not saved yet
    bool modified = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t islands = 0;
    /// Islands the world plan makes when a game starts (ADR 0013), 0 for a world without a plan.
    std::size_t planned_islands = 0;
    std::size_t players = 0;  ///< humans and computers
    bool can_undo = false;
    bool can_redo = false;
    /// The last thing that happened (saved, an error), Windows-1252.
    std::string notice;
    bool notice_is_error = false;
    /// The files to open (the page "Open"), in the order shown.
    std::vector<EditorFile> files;
    /// The names (Windows-1252, without ".o1602") of the own scenarios: a name typed that is
    /// among them replaces that file.
    std::vector<std::string> own_names;
};

/// What a click or a key asked for.
struct EditorRequest {
    enum class Kind : std::uint8_t {
        New,     ///< a new map with `map`
        Open,    ///< the file `index` of the model's list
        Save,    ///< under the document's file, or ask for a name (SaveAs) if it has none
        SaveAs,  ///< under `name` in the folder of own scenarios
        Undo,
        Redo,
        Close,  ///< out of the editor, back to the main menu
        /// The page "Open" opens: the model's list of files is due.
        ListFiles,
    };
    Kind kind = Kind::Close;
    std::size_t index = 0;
    std::string name{};  ///< Windows-1252
    EditorNewMap map{};
};

class EditorPanel {
public:
    /// The panel's width in design units, as the side panel's.
    static constexpr std::int32_t kWidth = 256;
    /// Rows of the list of files on one page.
    static constexpr std::size_t kRows = 12;
    /// The longest name a scenario can be saved under.
    static constexpr std::size_t kMaxName = 60;

    enum class Page : std::uint8_t { Document, New, Open, SaveAs };

    explicit EditorPanel(ui::FormStyle style = {}) : m_form(style) {}

    void set_style(const ui::FormStyle& style) { m_form.set_style(style); }
    void set_scale(double scale) { m_form.set_scale(scale); }
    [[nodiscard]] double scale() const noexcept { return m_form.scale(); }
    /// The panel's room in the side panel, design units from its top left: from `top` down,
    /// `height` high.
    void set_room(std::int32_t top, std::int32_t height) noexcept {
        m_top = top;
        m_height = height;
    }

    [[nodiscard]] Page page() const noexcept { return m_page; }
    /// Back to the document's page (a new document, or Escape).
    void show_document() noexcept;
    /// The page "Save as" with `name` offered.
    void ask_name(std::string_view name);
    /// The page "New map" with `map` offered.
    void ask_new(const EditorNewMap& map);
    /// A name is being typed: the keys go here.
    [[nodiscard]] bool typing() const noexcept { return m_page == Page::SaveAs; }
    [[nodiscard]] const std::string& text() const noexcept { return m_text; }
    [[nodiscard]] const EditorNewMap& new_map() const noexcept { return m_new; }

    /// Everything to draw, relative to the side panel's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const EditorModel& model,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    /// The mouse in pixels relative to the side panel's top left; the right button goes back
    /// through the values of the page "New map".
    std::optional<EditorRequest> mouse(std::int32_t x, std::int32_t y, bool down, const EditorModel& model,
                                       ui::MouseButton button = ui::MouseButton::Left);
    /// Text typed (Windows-1252).
    void type(std::string_view text);
    /// Ctrl+A 1, Backspace 8, Enter 13 saves, Escape 27 goes back to the document's page.
    std::optional<EditorRequest> key(int key);
    /// Escape outside a field: a page back to the document's; false if it showed already.
    bool back() noexcept;

    /// The widgets as laid out last (tests).
    [[nodiscard]] const ui::Form& form() const noexcept { return m_form; }
    /// Lays the page out for `model` (draw() and mouse() do it themselves).
    void layout(const EditorModel& model);

    /// A name as a file may carry it: what the file systems refuse goes, spaces at the ends too.
    [[nodiscard]] static std::string clean_name(std::string_view name);
    /// Whether `name` is one of the own scenarios' (letters in any case).
    [[nodiscard]] static bool name_taken(const EditorModel& model, std::string_view name);

private:
    void layout_document(const EditorModel& model);
    void layout_new();
    void layout_open(const EditorModel& model);
    void layout_save(const EditorModel& model);
    std::optional<EditorRequest> commit_name();

    ui::Form m_form;
    Page m_page = Page::Document;
    std::int32_t m_top = 0;
    std::int32_t m_height = 400;
    std::string m_text;  ///< the name being typed
    EditorNewMap m_new;
    std::size_t m_first = 0;  ///< the first file shown on the page "Open"
};

}  // namespace o1602::app
