// SPDX-License-Identifier: GPL-3.0-or-later
#include "key_help.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using ui::KeyAction;

/// Pages and their sections, text keys of the localization.
constexpr std::array<const char*, kKeyHelpPages> kPageTitles = {
    "keys.page.map_game", "keys.page.windows", "keys.page.building", "keys.page.units", "keys.page.view"};
constexpr std::array<std::array<const char*, 2>, kKeyHelpPages> kSectionHeadings = {{
    {"keys.heading.map", "keys.heading.game"},
    {"keys.heading.windows", nullptr},
    {"keys.heading.building", nullptr},
    {"keys.heading.groups", "keys.heading.units"},
    {"keys.heading.view", nullptr},
}};

/// The page and section of the clicks of add_click_lines(): ships and units selected.
constexpr KeyHelpPlace kClickPlace{3, 1};
/// What each click does (keys.click.<name>), all with Shift.
constexpr std::array<const char*, 4> kShiftClicks = {"keys.click.castle_all", "keys.click.cannons",
                                                     "keys.click.cannons_unmount", "keys.click.trade_cart"};
/// The page and section of a building clicked twice: the map.
constexpr KeyHelpPlace kTwicePlace{0, 0};

/// The section at `place` of `pages`, added (with its heading) when it has no lines yet.
KeyHelpSection& section_at(std::array<KeyHelpPage, kKeyHelpPages>& pages, KeyHelpPlace place) {
    KeyHelpPage& page = pages[place.page];
    const std::string heading = ui::tr(kSectionHeadings[place.page][place.section]);
    auto section = std::find_if(page.sections.begin(), page.sections.end(),
                                [&](const KeyHelpSection& s) { return s.heading == heading; });
    if (section == page.sections.end()) {
        page.sections.push_back(KeyHelpSection{heading, {}});
        section = page.sections.end() - 1;
    }
    return *section;
}

}  // namespace

KeyHelpPlace key_help_place(KeyAction action) noexcept {
    switch (action) {
        case KeyAction::ScrollUp:
        case KeyAction::ScrollDown:
        case KeyAction::ScrollLeft:
        case KeyAction::ScrollRight:
        case KeyAction::RotateViewLeft:
        case KeyAction::RotateViewRight:
        case KeyAction::ZoomIn:
        case KeyAction::ZoomOut:
        case KeyAction::JumpToMessage:
        case KeyAction::CentreOnSelection:
        case KeyAction::NextCity:
            return {0, 0};
        case KeyAction::Pause:
        case KeyAction::PauseVideo:
        case KeyAction::SpeedUp:
        case KeyAction::SpeedDown:
        case KeyAction::QuickSave:
        case KeyAction::QuickLoad:
        case KeyAction::GameMenu:
        case KeyAction::Screenshot:
        case KeyAction::Help:
        case KeyAction::Cancel:
        case KeyAction::Quit:
            return {0, 1};
        case KeyAction::ModeBuild:
        case KeyAction::ModeMilitary:
        case KeyAction::ModeInfo:
        case KeyAction::ModeOptions:
        case KeyAction::ShipList:
        case KeyAction::CityList:
        case KeyAction::Diplomacy:
        case KeyAction::GoodsStatistics:
        case KeyAction::Chat:
            return {1, 0};
        case KeyAction::RotateBuilding:
        case KeyAction::RotateBuildingBack:
        case KeyAction::PickBuilding:
        case KeyAction::DemolishTool:
        case KeyAction::CopyTool:
        case KeyAction::BlueprintMode:
        case KeyAction::BuildBlueprints:
        case KeyAction::SaveTemplate:
        case KeyAction::PlantFields:
        case KeyAction::ToggleGates:
            return {2, 0};
        case KeyAction::GroupStore:
        case KeyAction::GroupAdd:
        case KeyAction::GroupRecall:
        case KeyAction::NextIdleShip:
        case KeyAction::NextIdleUnit:
        case KeyAction::SelectInfantry:
        case KeyAction::SelectMusketeers:
        case KeyAction::SelectCavalry:
        case KeyAction::SelectCannoneers:
            return {3, 0};
        case KeyAction::NextKindInSelection:
        case KeyAction::AttackMove:
        case KeyAction::Stop:
        case KeyAction::HoldPosition:
        case KeyAction::Patrol:
        case KeyAction::Formation:
        case KeyAction::Scatter:
        case KeyAction::WhiteFlag:
        case KeyAction::Explore:
        case KeyAction::Trade:
        case KeyAction::EditRoute:
        case KeyAction::Delete:
            return {3, 1};
        case KeyAction::PixelScaleUp:
        case KeyAction::PixelScaleDown:
        case KeyAction::ShowOwners:
        case KeyAction::ShowFertility:
        case KeyAction::ShowFps:
        case KeyAction::ShowCoordinates:
        case KeyAction::ShowUnitArms:
        case KeyAction::Fullscreen:
            return {4, 0};
    }
    return {0, 1};
}

std::array<KeyHelpPage, kKeyHelpPages> key_help_pages(const std::vector<KeyHelpEntry>& entries,
                                                      const std::function<std::string(ui::KeyAction)>& label) {
    // One line per action, its keys joined in the order they come.
    std::vector<std::pair<KeyAction, std::string>> actions;
    for (const KeyHelpEntry& e : entries) {
        const auto it =
            std::find_if(actions.begin(), actions.end(), [&](const auto& a) { return a.first == e.action; });
        if (it == actions.end()) {
            actions.emplace_back(e.action, e.keys);
            continue;
        }
        // Each key once ("Entf" in two contexts); a key's whole name, not a part of another's.
        std::size_t start = 0;
        bool known = false;
        while (start <= it->second.size()) {
            const std::size_t end = std::min(it->second.find(", ", start), it->second.size());
            known |= it->second.compare(start, end - start, e.keys) == 0;
            start = end + 2;
        }
        if (!known) {
            it->second += ", " + e.keys;
        }
    }
    std::stable_sort(actions.begin(), actions.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::array<KeyHelpPage, kKeyHelpPages> pages;
    std::array<std::array<std::vector<KeyHelpLine>, 2>, kKeyHelpPages> lines;
    for (const auto& [action, keys] : actions) {
        const KeyHelpPlace p = key_help_place(action);
        lines[p.page][p.section].push_back(KeyHelpLine{keys, label(action)});
    }
    for (std::size_t p = 0; p < kKeyHelpPages; ++p) {
        pages[p].title = ui::tr(kPageTitles[p]);
        for (std::size_t s = 0; s < 2; ++s) {
            if (kSectionHeadings[p][s] != nullptr && !lines[p][s].empty()) {
                pages[p].sections.push_back(KeyHelpSection{ui::tr(kSectionHeadings[p][s]), std::move(lines[p][s])});
            }
        }
    }
    return pages;
}

void add_click_lines(std::array<KeyHelpPage, kKeyHelpPages>& pages) {
    KeyHelpSection& units = section_at(pages, kClickPlace);
    const std::string keys = ui::tr("keys.modifier.shift") + ui::tr("keys.click");
    for (const char* text : kShiftClicks) {
        units.lines.push_back(KeyHelpLine{keys, ui::tr(text)});
    }
    section_at(pages, kTwicePlace)
        .lines.push_back(KeyHelpLine{ui::tr("keys.click.twice"), ui::tr("keys.click.catchments")});
}

namespace {

std::size_t line_rows(const KeyHelpHeight& height, std::size_t section, std::size_t line) {
    return height ? std::max<std::size_t>(1, height(section, line)) : 1;
}

/// The rows of the lines [first, last) of a section, one under the other.
std::size_t rows_of(std::size_t section, std::size_t first, std::size_t last, const KeyHelpHeight& height) {
    std::size_t rows = 0;
    for (std::size_t i = first; i < last; ++i) {
        rows += line_rows(height, section, i);
    }
    return rows;
}

/// How many lines of a page's single section go into the first column: the split with the lower longer
/// column, the first column the longer one on a tie.
std::size_t first_column_lines(const KeyHelpSection& section, const KeyHelpHeight& height) {
    const std::size_t n = section.lines.size();
    const std::size_t total = rows_of(0, 0, n, height);
    std::size_t best = 0;
    std::size_t best_rows = total;
    std::size_t prefix = 0;
    for (std::size_t k = 0; k <= n; ++k) {
        const std::size_t longer = std::max(prefix, total - prefix);
        if (longer <= best_rows) {  // on a tie the later split: the first column the longer one
            best = k;
            best_rows = longer;
        }
        if (k < n) {
            prefix += line_rows(height, 0, k);
        }
    }
    return best;
}

}  // namespace

std::size_t key_help_rows(const KeyHelpPage& page, const KeyHelpHeight& height) {
    if (page.sections.empty()) {
        return 0;
    }
    if (page.sections.size() == 1) {
        const KeyHelpSection& s = page.sections[0];
        const std::size_t split = first_column_lines(s, height);
        return 1 + std::max(rows_of(0, 0, split, height), rows_of(0, split, s.lines.size(), height));
    }
    std::size_t longest = 0;
    for (std::size_t i = 0; i < page.sections.size(); ++i) {
        longest = std::max(longest, rows_of(i, 0, page.sections[i].lines.size(), height));
    }
    return 1 + longest;
}

KeyHelpCell key_help_cell(const KeyHelpPage& page, std::size_t section, std::size_t line, const KeyHelpHeight& height) {
    if (line == SIZE_MAX) {
        return {section, 0};
    }
    const KeyHelpSection& s = page.sections[section];
    if (page.sections.size() == 1) {
        const std::size_t split = first_column_lines(s, height);
        return line < split ? KeyHelpCell{0, 1 + rows_of(0, 0, line, height)}
                            : KeyHelpCell{1, 1 + rows_of(0, split, line, height)};
    }
    return {section, 1 + rows_of(section, 0, line, height)};
}

}  // namespace o1602::app
