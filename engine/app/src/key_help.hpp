// SPDX-License-Identifier: GPL-3.0-or-later
//
// The key overview (F1, ADR 0010) in pages, as the History Edition shows its shortcuts: map and
// game, windows, building, units and ships, view. Which page and section each action is listed in,
// and the pages made from the keys bound. Needs no platform, so that the tests have it.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "o1602/ui/key_bindings.hpp"

namespace o1602::app {

/// A line of the overview: the keys and what they do.
struct KeyHelpLine {
    std::string keys;
    std::string text;
};

/// A section of a page: its heading and lines. The overview shows the sections of a page side by
/// side; a page of one section splits it into two columns.
struct KeyHelpSection {
    std::string heading;
    std::vector<KeyHelpLine> lines;
};

struct KeyHelpPage {
    std::string title;
    std::vector<KeyHelpSection> sections;
};

inline constexpr std::size_t kKeyHelpPages = 5;

/// Where an action is listed: page 0 map and game, 1 windows, 2 building, 3 units and ships, 4 view.
struct KeyHelpPlace {
    std::uint8_t page = 0;
    std::uint8_t section = 0;
    friend bool operator==(const KeyHelpPlace&, const KeyHelpPlace&) = default;
};
[[nodiscard]] KeyHelpPlace key_help_place(ui::KeyAction action) noexcept;

/// A key bound to an action, as the overview names it.
struct KeyHelpEntry {
    ui::KeyAction action = ui::KeyAction::Cancel;
    std::string keys;
};

/// The pages, titles and headings in the player's language, `label` the text of an action. The
/// keys of an action are joined (each once), the actions keep the order of ui::KeyAction; a
/// section without keys is left out, a page without sections stays (its tab is still there).
[[nodiscard]] std::array<KeyHelpPage, kKeyHelpPages> key_help_pages(
    const std::vector<KeyHelpEntry>& entries, const std::function<std::string(ui::KeyAction)>& label);

/// Adds the clicks with a modifier that windows know and no key binding names (Shift on the castle's
/// send-out arrow, on cannons in a ship's hold) to the units of the units and ships page, and the
/// building clicked twice (the circles of all of its type on the island) to the map.
void add_click_lines(std::array<KeyHelpPage, kKeyHelpPages>& pages);

/// The rows line `line` of section `section` takes: its text wrapped to the width of its column.
/// Without one every line takes one row.
using KeyHelpHeight = std::function<std::size_t(std::size_t section, std::size_t line)>;

/// The rows a page takes: a heading row and the lines below it, of the longer section when there
/// are two side by side, of the longer column when one section is split into two.
[[nodiscard]] std::size_t key_help_rows(const KeyHelpPage& page, const KeyHelpHeight& height = {});

/// Where a line of a page goes: its column (0 or 1) and first row (0 the heading row). A heading is
/// line SIZE_MAX of its section. One section is split where the two columns come out the most even.
struct KeyHelpCell {
    std::size_t column = 0;
    std::size_t row = 0;
    friend bool operator==(const KeyHelpCell&, const KeyHelpCell&) = default;
};
[[nodiscard]] KeyHelpCell key_help_cell(const KeyHelpPage& page, std::size_t section, std::size_t line,
                                        const KeyHelpHeight& height = {});

}  // namespace o1602::app
