// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor_panel.hpp"

#include <algorithm>
#include <cctype>

#include "o1602/assets/game_data.hpp"
#include "o1602/sim/limits.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

// The page of the document.
constexpr Id kNew = 1;
constexpr Id kOpen = 2;
constexpr Id kSave = 3;
constexpr Id kSaveAs = 4;
constexpr Id kUndo = 5;
constexpr Id kRedo = 6;
constexpr Id kClose = 7;
constexpr Id kView = 8;
constexpr Id kCheck = 9;
// The other pages.
constexpr Id kCancel = 10;
constexpr Id kCreate = 11;
constexpr Id kSize = 12;
constexpr Id kIslands = 13;
constexpr Id kPlayers = 14;
constexpr Id kReroll = 15;
constexpr Id kField = 16;
constexpr Id kStore = 17;
constexpr Id kPrevious = 18;
constexpr Id kNext = 19;
constexpr Id kBack = 20;
constexpr Id kIslandPage = 21;
constexpr Id kShape = 22;
constexpr Id kClimate = 23;
constexpr Id kIslandSize = 24;
constexpr Id kOtherIsland = 25;
constexpr Id kPlace = 26;
constexpr Id kMove = 40;
constexpr Id kRotate = 41;
constexpr Id kCopy = 42;
constexpr Id kRemove = 43;
constexpr Id kStopPlacing = 44;
constexpr Id kWorldSizePage = 45;
constexpr Id kWidthLess = 46;
constexpr Id kWidthMore = 47;
constexpr Id kHeightLess = 48;
constexpr Id kHeightMore = 49;
constexpr Id kFitSize = 50;
constexpr Id kApplySize = 51;
constexpr Id kFertilityPage = 52;
constexpr Id kSelectionPage = 53;
constexpr Id kSaveIsland = 54;
constexpr Id kLoadIsland = 55;
constexpr Id kPlaceholderClimate = 56;
constexpr Id kPlaceholderSize = 57;
constexpr Id kFirstCrop = 60;     ///< + the index into kCropWares
constexpr Id kFirstOverlay = 30;  ///< + EditorOverlay
constexpr Id kFirstRow = 100;     ///< + the row on the page

constexpr std::int32_t kX = 12;
constexpr std::int32_t kW = EditorPanel::kWidth - 2 * kX;
constexpr std::int32_t kButtonH = 30;
constexpr std::int32_t kGap = 6;
constexpr std::int32_t kLine = 24;
constexpr std::int32_t kRowH = 24;
constexpr std::int32_t kNote = 80;  ///< a note of up to four lines

constexpr std::array<const char*, kEditorOverlays> kOverlayTexts = {"editor.view.owners", "editor.view.island_numbers",
                                                                    "editor.view.coordinates"};

constexpr std::array<const char*, 4> kIslandTexts = {"editor.new.islands.none", "editor.new.islands.few",
                                                     "editor.new.islands.normal", "editor.new.islands.many"};

/// The crops of an island's fertility, as the page "Fertility" offers them (bit ware - 45).
constexpr std::array<std::uint8_t, 6> kCropWares = {assets::ware::kTobaccoField, assets::ware::kSpiceField,
                                                    assets::ware::kSugarField,   assets::ware::kCottonField,
                                                    assets::ware::kWineField,    assets::ware::kCocoaField};

constexpr std::array<const char*, 6> kCropTexts = {"editor.crop.tobacco", "editor.crop.spices", "editor.crop.sugar",
                                                   "editor.crop.cotton",  "editor.crop.wine",   "editor.crop.cocoa"};

/// The crops of each climate that grow half without the island's fertility for them.
constexpr std::array<std::array<bool, 6>, 2> kClimateCrops = {
    {{true, false, true, false, true, false}, {false, true, false, true, false, true}}};

constexpr std::array<const char*, 3> kPlaceholderClimateTexts = {"editor.prop.north", "editor.prop.south",
                                                                 "editor.placeholder.by_place"};

constexpr std::array<const char*, 5> kIslandSizeTexts = {"editor.islands.size.0", "editor.islands.size.1",
                                                         "editor.islands.size.2", "editor.islands.size.3",
                                                         "editor.islands.size.4"};

/// The next number of a simple generator (Knuth's MMIX constants): another world, another island.
std::uint64_t next_seed(std::uint64_t seed) {
    return seed * 6364136223846793005ULL + 1442695040888963407ULL;
}

/// The steps of the page "Map size", and the sea it leaves around what the map holds when fitted.
constexpr std::uint32_t kSizeStep = 50;
constexpr std::uint32_t kFitMargin = 8;

/// `value` a step larger or smaller, between `least` and the largest map.
std::uint32_t size_step(std::uint32_t value, std::uint32_t least, bool more) {
    const std::uint32_t v = more ? value + kSizeStep : (value > kSizeStep ? value - kSizeStep : 0);
    return std::clamp(v, std::max<std::uint32_t>(least, 1), sim::kMaxWorldSide);
}

/// A side as large as `used` with some sea, at a round number.
std::uint32_t fitted(std::uint32_t used) {
    const std::uint32_t v = (used + kFitMargin + 9) / 10 * 10;
    return std::min(v, sim::kMaxWorldSide);
}

std::string count(std::size_t n) {
    return std::to_string(n);
}

/// The next value of a cycle of `n` values, forward or back.
std::size_t step(std::size_t value, std::size_t n, bool forward) {
    return forward ? (value + 1) % n : (value + n - 1) % n;
}

}  // namespace

std::string EditorPanel::clean_name(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        // What Windows refuses in a file's name, and the control characters.
        if (u < 0x20 || u == 0x7F || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) {
            continue;
        }
        out.push_back(c);
    }
    const auto first = out.find_first_not_of(" .");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = out.find_last_not_of(" .");
    out = out.substr(first, last - first + 1);
    if (out.size() > kMaxName) {
        out.resize(kMaxName);
    }
    return out;
}

bool EditorPanel::name_taken(const EditorModel& model, std::string_view name) {
    const auto lower = [](std::string_view s) {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    };
    const std::string wanted = lower(name);
    return std::any_of(model.own_names.begin(), model.own_names.end(),
                       [&](const std::string& n) { return lower(n) == wanted; });
}

void EditorPanel::show_document() noexcept {
    m_page = Page::Document;
    m_form.set_focus(std::nullopt);
}

void EditorPanel::ask_name(std::string_view name) {
    m_page = Page::SaveAs;
    m_text = clean_name(name);
    m_form.set_focus(kField);
    m_form.select_all();  // what is typed replaces the name offered
}

bool EditorPanel::show_selection() noexcept {
    if (m_page == Page::New || m_page == Page::Open || m_page == Page::SaveAs) {
        return false;
    }
    m_page = Page::Selection;
    m_form.set_focus(std::nullopt);
    return true;
}

void EditorPanel::ask_new(const EditorNewMap& map) {
    m_page = Page::New;
    m_new = map;
    m_new.players = std::clamp<std::uint8_t>(m_new.players, 1, static_cast<std::uint8_t>(sim::kMaxPlayers));
    m_form.set_focus(std::nullopt);
}

bool EditorPanel::back() noexcept {
    if (m_page == Page::Document) {
        return false;
    }
    show_document();
    return true;
}

void EditorPanel::layout(const EditorModel& model) {
    m_form.clear();
    m_form.panel(Rect{kX - 6, m_top, kWidth - 2 * (kX - 6), m_height});
    switch (m_page) {
        case Page::Document:
            layout_document(model);
            break;
        case Page::New:
            layout_new();
            break;
        case Page::Open:
            layout_open(model);
            break;
        case Page::SaveAs:
            layout_save(model);
            break;
        case Page::Selection:
            layout_selection(model);
            break;
        case Page::View:
            layout_view(model);
            break;
        case Page::Check:
            layout_check(model);
            break;
        case Page::Islands:
            layout_islands(model);
            break;
        case Page::WorldSize:
            layout_world_size(model);
            break;
        case Page::Fertility:
            layout_fertility(model);
            break;
        case Page::IslandFiles:
            layout_open(model);
            break;
    }
}

void EditorPanel::layout_document(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 4;
    f.label(Rect{kX, y, kW, 24}, ui::tr("editor.title"), Align::Left, Tone::Heading);
    y += 26;
    std::string file = model.file.empty() ? ui::tr("editor.untitled") : model.file;
    if (model.modified) {
        file += " *";
    }
    f.label(Rect{kX, y, kW, kLine}, file);
    y += kLine;
    f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.map_size"),
            ui::tr("editor.size", {count(model.width), count(model.height)}), Tone::Dim);
    y += kLine;
    if (model.planned_islands > 0) {
        f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.islands"),
                ui::tr("editor.islands_planned", {count(model.planned_islands)}), Tone::Dim);
    } else {
        f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.islands"), count(model.islands), Tone::Dim);
    }
    y += kLine;
    f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.players"), count(model.players), Tone::Dim);
    y += kLine + 4;
    // The buttons share what is left above two lines of the notice: as high as the game's in a
    // large window, lower in a small one, so that the last stays in the panel.
    constexpr std::int32_t kButtons = 11;
    constexpr std::int32_t kNoticeRoom = 2 * 20 + 6 + kButtons;  // and what rounding the pitch down loses
    const std::int32_t bottom = m_top + m_height - 4;
    const std::int32_t pitch = std::clamp((bottom - kNoticeRoom - y) / kButtons, 22, kButtonH + kGap);
    const std::int32_t gap = std::min(kGap, std::max(2, pitch / 8));
    const auto full = [&](Id id, const char* text, bool enabled = true) {
        f.button(id, Rect{kX, y, kW, pitch - gap}, ui::tr(text), enabled);
        y += pitch;
    };
    full(kNew, "editor.new");
    full(kOpen, "editor.open");
    full(kSave, "editor.save");
    full(kSaveAs, "editor.save_as");
    full(kUndo, "editor.undo", model.can_undo);
    full(kRedo, "editor.redo", model.can_redo);
    full(kIslandPage, "editor.islands.edit");
    full(kWorldSizePage, "editor.world_size.edit");
    full(kView, "editor.view");
    full(kCheck, "editor.check");
    full(kClose, "editor.close");
    // The notice where two lines fit: in a small window the buttons take the room, and the status
    // bar shows it.
    if (!model.notice.empty() && bottom - y - 2 >= 2 * 20) {
        f.paragraph(Rect{kX, y + 2, kW, std::max(0, bottom - y - 2)}, model.notice,
                    model.notice_is_error ? Tone::Warning : Tone::Dim, 20);
    }
}

void EditorPanel::layout_new() {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.new.title"), Align::Left, Tone::Heading);
    y += 28;
    const auto value = [&](Id id, const char* label, std::string text, bool enabled = true) {
        f.label(Rect{kX, y, kW, kLine}, ui::tr(label), Align::Left, enabled ? Tone::Normal : Tone::Dim);
        y += kLine;
        f.cycle_button(id, Rect{kX, y, kW, kButtonH}, std::move(text), enabled);
        y += kButtonH + kGap;
    };
    std::string size = ui::tr("editor.size", {count(m_new.width()), count(m_new.height())});
    if (m_new.size == 0) {
        size = ui::tr("editor.new.size_original", {size});
    }
    value(kSize, "editor.new.size", std::move(size));
    value(kIslands, "editor.new.islands", ui::tr(kIslandTexts[static_cast<std::size_t>(m_new.islands)]));
    const bool islands = m_new.islands != EditorNewMap::Islands::None;
    value(kPlayers, "editor.new.players", count(m_new.players), islands);
    f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.new.seed"), std::to_string(m_new.seed),
            islands ? Tone::Normal : Tone::Dim, Tone::Dim);
    y += kLine + 2;
    f.button(kReroll, Rect{kX, y, kW, kButtonH}, ui::tr("editor.new.reroll"), islands);
    y += kButtonH + kGap;
    f.paragraph(Rect{kX, y, kW, kNote}, ui::tr(islands ? "editor.new.note_islands" : "editor.new.note_sea"), Tone::Dim,
                20);
    y += kNote + 4;
    const std::int32_t half = (kW - kGap) / 2;
    f.button(kCreate, Rect{kX, y, half, kButtonH}, ui::tr("editor.new.create"));
    f.button(kCancel, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_open(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    const bool islands = m_page == Page::IslandFiles;
    f.label(Rect{kX, y, kW, 28}, ui::tr(islands ? "editor.islands.load_title" : "editor.open.title"), Align::Left,
            Tone::Heading);
    y += 32;
    if (model.files.empty()) {
        f.paragraph(Rect{kX, y, kW, 60}, ui::tr(islands ? "editor.islands.no_files" : "editor.open.empty"), Tone::Dim,
                    20);
        y += 64;
    } else {
        const std::size_t n = model.files.size();
        m_first = std::min(m_first, n > kRows ? n - kRows : 0);
        std::string group;  // every page begins with the heading of its first file's group
        std::size_t shown = 0;
        for (std::size_t i = m_first; i < n && shown < kRows; ++i) {
            const EditorFile& file = model.files[i];
            if (file.group != group) {
                group = file.group;
                f.label(Rect{kX, y, kW, kRowH}, group, Align::Left, Tone::Heading);
                y += kRowH;
            }
            const auto row = static_cast<Id>(i - m_first);
            f.row(kFirstRow + row, Rect{kX, y, kW, kRowH - 2}, false);
            f.label(Rect{kX + 4, y, kW - 8, kRowH - 2}, file.name);
            y += kRowH;
            ++shown;
        }
        y += 4;
        const std::int32_t half = (kW - kGap) / 2;
        f.button(kPrevious, Rect{kX, y, half, kButtonH}, ui::tr("editor.previous"), m_first > 0);
        f.button(kNext, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.next"), m_first + kRows < n);
        y += kButtonH + kGap;
    }
    f.button(islands ? kIslandPage : kCancel, Rect{kX, y, kW, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_save(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.save.title"), Align::Left, Tone::Heading);
    y += 34;
    f.label(Rect{kX, y, kW, kLine}, ui::tr("editor.save.name"));
    y += kLine;
    f.field(kField, Rect{kX, y, kW, 32}, m_text);
    y += 38;
    const std::string name = clean_name(m_text);
    const bool taken = !name.empty() && name_taken(model, name);
    f.paragraph(Rect{kX, y, kW, kNote}, ui::tr(taken ? "editor.save.taken" : "editor.save.where"),
                taken ? Tone::Warning : Tone::Dim, 20);
    y += kNote + 4;
    const std::int32_t half = (kW - kGap) / 2;
    f.button(kStore, Rect{kX, y, half, kButtonH}, ui::tr(taken ? "editor.save.replace" : "editor.save"), !name.empty());
    f.button(kCancel, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_selection(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.selection.title"), Align::Left, Tone::Heading);
    y += 30;
    if (model.selection.empty()) {
        f.paragraph(Rect{kX, y, kW, 3 * 20}, ui::tr("editor.selection.none"), Tone::Dim, 20);
        y += 3 * 20 + 4;
    } else {
        f.paragraph(Rect{kX, y, kW, 2 * kLine}, model.selection, Tone::Normal, kLine);
        y += 2 * kLine + 4;
        // Room for the buttons below: back, and an island's three rows.
        const std::int32_t rows = model.island || model.placeholder ? 4 : 1;
        const std::int32_t bottom = m_top + m_height - rows * (kButtonH + kGap) - kGap;
        for (const EditorProperty& p : model.properties) {
            if (y + kLine > bottom) {
                break;
            }
            f.entry(Rect{kX, y, kW, kLine}, p.label, p.value, Tone::Dim);
            y += kLine;
        }
        y += kGap;
        if (model.island) {
            layout_island_actions(model, y);
        } else if (model.placeholder) {
            layout_placeholder_actions(model, y);
        }
    }
    f.button(kBack, Rect{kX, y, kW, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_island_actions(const EditorModel& model, std::int32_t& y) {
    ui::Form& f = m_form;
    const std::int32_t half = (kW - kGap) / 2;
    if (model.placing) {
        f.paragraph(Rect{kX, y, kW, 3 * 20}, ui::tr("editor.islands.placing"), Tone::Dim, 20);
        y += 3 * 20 + 4;
        f.button(kStopPlacing, Rect{kX, y, kW, kButtonH}, ui::tr("editor.cancel"));
        y += kButtonH + kGap;
        return;
    }
    f.button(kMove, Rect{kX, y, half, kButtonH}, ui::tr("editor.islands.move"));
    f.button(kRotate, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.islands.rotate"), model.island_free);
    y += kButtonH + kGap;
    f.button(kCopy, Rect{kX, y, half, kButtonH}, ui::tr("editor.islands.copy"), model.island_free);
    f.button(kRemove, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.islands.remove"), model.island_free);
    y += kButtonH + kGap;
    f.button(kFertilityPage, Rect{kX, y, half, kButtonH}, ui::tr("editor.fertility.edit"));
    f.button(kSaveIsland, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.islands.save"), model.island_free);
    y += kButtonH + kGap;
}

void EditorPanel::layout_placeholder_actions(const EditorModel& model, std::int32_t& y) {
    ui::Form& f = m_form;
    const std::int32_t half = (kW - kGap) / 2;
    if (model.placing) {
        f.paragraph(Rect{kX, y, kW, 3 * 20}, ui::tr("editor.placeholder.placing"), Tone::Dim, 20);
        y += 3 * 20 + 4;
        f.button(kStopPlacing, Rect{kX, y, kW, kButtonH}, ui::tr("editor.cancel"));
        y += kButtonH + kGap;
        return;
    }
    f.button(kMove, Rect{kX, y, half, kButtonH}, ui::tr("editor.islands.move"));
    f.button(kRemove, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.islands.remove"));
    y += kButtonH + kGap;
    f.cycle_button(kPlaceholderClimate, Rect{kX, y, kW, kButtonH},
                   ui::tr(kPlaceholderClimateTexts[model.placeholder_climate % kPlaceholderClimateTexts.size()]));
    y += kButtonH + kGap;
    f.cycle_button(kPlaceholderSize, Rect{kX, y, kW, kButtonH},
                   ui::tr(kIslandSizeTexts[model.placeholder_size % kIslandSizeTexts.size()]));
    y += kButtonH + kGap;
}

void EditorPanel::layout_fertility(const EditorModel& model) {
    if (!model.island) {
        layout_selection(model);  // the island is gone (undone, taken out)
        return;
    }
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.fertility.title"), Align::Left, Tone::Heading);
    y += 28;
    f.label(Rect{kX, y, kW, kLine}, model.selection);
    y += kLine + 4;
    for (std::size_t i = 0; i < kCropTexts.size(); ++i) {
        std::string text = ui::tr(kCropTexts[i]);
        const bool own = (model.island_fertility >> (kCropWares[i] - assets::ware::kCornField) & 1U) != 0;
        if (!own && kClimateCrops[model.island_climate & 1U][i]) {
            text = ui::tr("editor.fertility.half", {text});
        }
        f.check(kFirstCrop + static_cast<Id>(i), Rect{kX, y, kW, kButtonH}, std::move(text), own);
        y += kButtonH + kGap;
    }
    constexpr std::int32_t kFertilityNote = 4 * 20;
    f.paragraph(Rect{kX, y, kW, kFertilityNote}, ui::tr("editor.fertility.note"), Tone::Dim, 20);
    y += kFertilityNote + 4;
    f.button(kSelectionPage, Rect{kX, y, kW, kButtonH}, ui::tr("editor.fertility.back"));
}

void EditorPanel::layout_islands(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.islands.title"), Align::Left, Tone::Heading);
    y += 28;
    const auto value = [&](Id id, const char* label, std::string text) {
        f.label(Rect{kX, y, kW, kLine}, ui::tr(label));
        y += kLine;
        f.cycle_button(id, Rect{kX, y, kW, kButtonH}, std::move(text));
        y += kButtonH + kGap;
    };
    if (!model.has_plan) {
        m_island.placeholder = false;
    }
    value(kShape, "editor.islands.shape",
          ui::tr(m_island.placeholder ? "editor.islands.shape.placeholder"
                 : m_island.own_shape ? "editor.islands.shape.own"
                                      : "editor.islands.shape.template"));
    value(kClimate, "editor.islands.climate",
          ui::tr(m_island.climate == 0 ? "editor.prop.north" : "editor.prop.south"));
    value(kIslandSize, "editor.islands.size", ui::tr(kIslandSizeTexts[m_island.size % kIslandSizeTexts.size()]));
    f.button(kOtherIsland, Rect{kX, y, kW, kButtonH}, ui::tr("editor.islands.other"),
             !model.placing && !m_island.placeholder);
    y += kButtonH + kGap;
    f.button(kPlace, Rect{kX, y, kW, kButtonH}, ui::tr("editor.islands.place"), !model.placing);
    y += kButtonH + kGap;
    f.button(kLoadIsland, Rect{kX, y, kW, kButtonH}, ui::tr("editor.islands.load"), !model.placing);
    y += kButtonH + kGap;
    constexpr std::int32_t kIslandNote = 3 * 20;
    f.paragraph(Rect{kX, y, kW, kIslandNote},
                ui::tr(!model.placing         ? "editor.islands.note"
                       : m_island.placeholder ? "editor.placeholder.placing"
                                              : "editor.islands.placing"),
                Tone::Dim, 20);
    y += kIslandNote + 4;
    if (model.placing) {
        f.button(kStopPlacing, Rect{kX, y, kW, kButtonH}, ui::tr("editor.cancel"));
        y += kButtonH + kGap;
    }
    f.button(kBack, Rect{kX, y, kW, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_world_size(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.world_size.title"), Align::Left, Tone::Heading);
    y += 28;
    f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.world_size.now"),
            ui::tr("editor.size", {count(model.width), count(model.height)}), Tone::Dim);
    y += kLine;
    f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.world_size.new"),
            ui::tr("editor.size", {count(m_size_width), count(m_size_height)}));
    y += kLine + 4;
    const std::int32_t half = (kW - kGap) / 2;
    const auto side = [&](const char* label, Id less, Id more, std::uint32_t value, std::uint32_t used) {
        f.label(Rect{kX, y, kW, kLine}, ui::tr(label));
        y += kLine;
        f.button(less, Rect{kX, y, half, kButtonH}, ui::tr("editor.world_size.less", {count(kSizeStep)}),
                 value > std::max<std::uint32_t>(used, 1));
        f.button(more, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.world_size.more", {count(kSizeStep)}),
                 value < sim::kMaxWorldSide);
        y += kButtonH + kGap;
    };
    side("editor.world_size.width", kWidthLess, kWidthMore, m_size_width, model.used_width);
    side("editor.world_size.height", kHeightLess, kHeightMore, m_size_height, model.used_height);
    f.button(kFitSize, Rect{kX, y, kW, kButtonH}, ui::tr("editor.world_size.fit"));
    y += kButtonH + kGap;
    constexpr std::int32_t kSizeNote = 4 * 20;
    f.paragraph(Rect{kX, y, kW, kSizeNote}, ui::tr("editor.world_size.note"), Tone::Dim, 20);
    y += kSizeNote + 4;
    const bool changed = m_size_width != model.width || m_size_height != model.height;
    f.button(kApplySize, Rect{kX, y, kW, kButtonH}, ui::tr("editor.world_size.apply"), changed);
    y += kButtonH + kGap;
    f.button(kCancel, Rect{kX, y, kW, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_view(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.view.title"), Align::Left, Tone::Heading);
    y += 34;
    for (std::size_t i = 0; i < kEditorOverlays; ++i) {
        f.check(kFirstOverlay + static_cast<Id>(i), Rect{kX, y, kW, kButtonH}, ui::tr(kOverlayTexts[i]),
                model.overlays[i]);
        y += kButtonH + kGap;
    }
    y += kGap;
    f.button(kBack, Rect{kX, y, kW, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_check(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.check.title"), Align::Left, Tone::Heading);
    y += 32;
    // Each line gets three lines of text, a heading two; as many as the room holds above the
    // buttons, the rest on the next pages.
    constexpr std::int32_t kItem = 3 * 20;
    constexpr std::int32_t kHeading = 2 * 20 + 2;
    const std::int32_t bottom = m_top + m_height - 2 * (kButtonH + kGap) - 6;
    const std::size_t n = model.check.size();
    m_first = std::min(m_first, n == 0 ? 0 : n - 1);
    std::size_t i = m_first;
    for (; i < n; ++i) {
        const EditorCheckLine& line = model.check[i];
        using Kind = EditorCheckLine::Kind;
        const std::int32_t h = line.kind == Kind::Heading ? kHeading : kItem;
        if (y + h > bottom && i > m_first) {
            break;
        }
        if (line.kind == Kind::Heading) {
            f.paragraph(Rect{kX, y, kW, kHeading - 2}, line.text, Tone::Heading, 20);
        } else {
            const Tone tone = line.kind == Kind::Error || line.kind == Kind::Warning ? Tone::Warning
                              : line.kind == Kind::Fine                              ? Tone::Normal
                                                                                     : Tone::Dim;
            f.paragraph(Rect{kX + 8, y, kW - 8, kItem}, line.text, tone, 20);
        }
        y += h;
    }
    m_page_rows = std::max<std::size_t>(1, i - m_first);
    y += 4;
    if (m_first > 0 || i < n) {
        const std::int32_t half = (kW - kGap) / 2;
        f.button(kPrevious, Rect{kX, y, half, kButtonH}, ui::tr("editor.previous"), m_first > 0);
        f.button(kNext, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.next"), i < n);
        y += kButtonH + kGap;
    }
    f.button(kBack, Rect{kX, y, kW, kButtonH}, ui::tr("editor.back"));
}

std::vector<ui::DrawItem> EditorPanel::draw(const EditorModel& model,
                                            const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    layout(model);
    return m_form.draw(fonts);
}

std::optional<EditorRequest> EditorPanel::commit_name() {
    std::string name = clean_name(m_text);
    if (name.empty()) {
        return std::nullopt;
    }
    show_document();
    EditorRequest r{EditorRequest::Kind::SaveAs};
    r.name = std::move(name);
    return r;
}

std::optional<EditorRequest> EditorPanel::mouse(std::int32_t x, std::int32_t y, bool down, const EditorModel& model,
                                                ui::MouseButton button) {
    layout(model);
    const auto id = m_form.mouse(x, y, down, button);
    if (!id) {
        return std::nullopt;
    }
    const bool forward = m_form.clicked_with() == ui::MouseButton::Left;
    switch (*id) {
        case kNew:
            ask_new(m_new);
            return std::nullopt;
        case kOpen:
            m_page = Page::Open;
            m_first = 0;
            return EditorRequest{EditorRequest::Kind::ListFiles};
        case kSave:
            return EditorRequest{EditorRequest::Kind::Save};
        case kSaveAs:
            ask_name(model.file);
            return std::nullopt;
        case kUndo:
            return EditorRequest{EditorRequest::Kind::Undo};
        case kRedo:
            return EditorRequest{EditorRequest::Kind::Redo};
        case kClose:
            return EditorRequest{EditorRequest::Kind::Close};
        case kView:
            m_page = Page::View;
            return std::nullopt;
        case kCheck:
            m_page = Page::Check;
            m_first = 0;
            return EditorRequest{EditorRequest::Kind::Check};
        case kBack:
            show_document();
            return std::nullopt;
        case kCancel:
            show_document();
            return std::nullopt;
        case kSize:
            m_new.size = step(m_new.size, EditorNewMap::kSizes.size(), forward);
            return std::nullopt;
        case kIslands:
            m_new.islands = static_cast<EditorNewMap::Islands>(
                step(static_cast<std::size_t>(m_new.islands), kIslandTexts.size(), forward));
            return std::nullopt;
        case kPlayers:
            m_new.players = static_cast<std::uint8_t>(step(m_new.players - 1U, sim::kMaxPlayers, forward) + 1U);
            return std::nullopt;
        case kReroll:
            m_new.seed = next_seed(m_new.seed);
            return std::nullopt;
        case kIslandPage:
            m_page = Page::Islands;
            return std::nullopt;
        case kShape: {
            // Template, own shape, placeholder (with a plan): forward or back.
            const std::size_t shapes = model.has_plan ? 3 : 2;
            const std::size_t now = m_island.placeholder ? 2 : m_island.own_shape ? 1 : 0;
            const std::size_t next = step(std::min(now, shapes - 1), shapes, forward);
            m_island.own_shape = next == 1;
            m_island.placeholder = next == 2;
            return std::nullopt;
        }
        case kClimate:
            m_island.climate = static_cast<std::uint8_t>(m_island.climate == 0 ? 1 : 0);
            return std::nullopt;
        case kIslandSize:
            m_island.size = static_cast<std::uint8_t>(step(m_island.size, kIslandSizeTexts.size(), forward));
            return std::nullopt;
        case kOtherIsland:
            m_island.seed = next_seed(m_island.seed);
            return std::nullopt;
        case kPlace: {
            EditorRequest r{EditorRequest::Kind::PlaceIsland};
            r.island = m_island;
            return r;
        }
        case kPlaceholderClimate:
        case kPlaceholderSize: {
            if (!model.placeholder) {
                return std::nullopt;
            }
            EditorRequest r{EditorRequest::Kind::ChangePlaceholder, *model.placeholder};
            r.island.climate = model.placeholder_climate;
            r.island.size = model.placeholder_size;
            if (*id == kPlaceholderClimate) {
                r.island.climate =
                    static_cast<std::uint8_t>(step(r.island.climate, kPlaceholderClimateTexts.size(), forward));
            } else {
                r.island.size = static_cast<std::uint8_t>(step(r.island.size, kIslandSizeTexts.size(), forward));
            }
            return r;
        }
        case kMove:
        case kRemove:
            if (model.placeholder && !model.island) {
                return EditorRequest{
                    *id == kMove ? EditorRequest::Kind::MovePlaceholder : EditorRequest::Kind::RemovePlaceholder,
                    *model.placeholder};
            }
            [[fallthrough]];
        case kRotate:
        case kCopy: {
            if (!model.island) {
                return std::nullopt;
            }
            using Kind = EditorRequest::Kind;
            EditorRequest r{*id == kMove     ? Kind::MoveIsland
                            : *id == kRotate ? Kind::RotateIsland
                            : *id == kCopy   ? Kind::CopyIsland
                                             : Kind::RemoveIsland};
            r.index = *model.island;
            return r;
        }
        case kStopPlacing:
            return EditorRequest{EditorRequest::Kind::StopPlacing};
        case kSaveIsland:
            if (!model.island) {
                return std::nullopt;
            }
            return EditorRequest{EditorRequest::Kind::SaveIsland, *model.island};
        case kLoadIsland:
            m_page = Page::IslandFiles;
            m_first = 0;
            return EditorRequest{EditorRequest::Kind::ListIslandFiles};
        case kFertilityPage:
            if (model.island) {
                m_page = Page::Fertility;
            }
            return std::nullopt;
        case kSelectionPage:
            m_page = Page::Selection;
            return std::nullopt;
        case kWorldSizePage:
            m_page = Page::WorldSize;
            m_size_width = model.width;
            m_size_height = model.height;
            return std::nullopt;
        case kWidthLess:
        case kWidthMore:
            m_size_width = size_step(m_size_width, model.used_width, *id == kWidthMore);
            return std::nullopt;
        case kHeightLess:
        case kHeightMore:
            m_size_height = size_step(m_size_height, model.used_height, *id == kHeightMore);
            return std::nullopt;
        case kFitSize:
            m_size_width = fitted(model.used_width);
            m_size_height = fitted(model.used_height);
            return std::nullopt;
        case kApplySize: {
            EditorRequest r{EditorRequest::Kind::ResizeWorld};
            r.width = m_size_width;
            r.height = m_size_height;
            return r;
        }
        case kCreate: {
            show_document();
            EditorRequest r{EditorRequest::Kind::New};
            r.map = m_new;
            return r;
        }
        case kStore:
            return commit_name();
        case kPrevious: {
            // The check list goes back by the lines its page shows (they differ in height).
            const std::size_t rows = m_page == Page::Check ? m_page_rows : kRows;
            m_first = m_first > rows ? m_first - rows : 0;
            return std::nullopt;
        }
        case kNext:
            m_first += m_page == Page::Check ? m_page_rows : kRows;
            return std::nullopt;
        default:
            break;
    }
    if (*id >= kFirstCrop && *id < kFirstCrop + static_cast<Id>(kCropTexts.size())) {
        if (!model.island) {
            return std::nullopt;
        }
        const auto i = static_cast<std::size_t>(*id - kFirstCrop);
        EditorRequest r{EditorRequest::Kind::SetFertility};
        r.index = *model.island;
        r.fertility = model.island_fertility ^ (1U << (kCropWares[i] - assets::ware::kCornField));
        return r;
    }
    if (*id >= kFirstOverlay && *id < kFirstOverlay + static_cast<Id>(kEditorOverlays)) {
        EditorRequest r{EditorRequest::Kind::Overlay};
        r.index = static_cast<std::size_t>(*id - kFirstOverlay);
        return r;
    }
    if (*id >= kFirstRow && m_page == Page::IslandFiles) {
        const std::size_t index = m_first + static_cast<std::size_t>(*id - kFirstRow);
        if (index < model.files.size()) {
            m_page = Page::Islands;
            EditorRequest r{EditorRequest::Kind::LoadIsland, index};
            r.island = m_island;  // its climate, for an island file of the original
            return r;
        }
    }
    if (*id >= kFirstRow && m_page == Page::Open) {
        const std::size_t index = m_first + static_cast<std::size_t>(*id - kFirstRow);
        if (index < model.files.size()) {
            show_document();
            EditorRequest r{EditorRequest::Kind::Open};
            r.index = index;
            return r;
        }
    }
    return std::nullopt;
}

void EditorPanel::type(std::string_view text) {
    if (!typing()) {
        return;
    }
    if (!text.empty() && m_form.take_all_selected()) {
        m_text.clear();  // typed over the whole name (Ctrl+A, or the name offered)
    }
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F && m_text.size() < kMaxName) {
            m_text.push_back(c);
        }
    }
}

std::optional<EditorRequest> EditorPanel::key(int key) {
    if (!typing()) {
        if (key == 27) {
            (void)back();
        }
        return std::nullopt;
    }
    if (key == 1) {
        m_form.select_all();
        return std::nullopt;
    }
    const bool all = m_form.take_all_selected();
    switch (key) {
        case 8:
            if (all) {
                m_text.clear();
            } else if (!m_text.empty()) {
                m_text.pop_back();
            }
            return std::nullopt;
        case 13:
            return commit_name();
        case 27:
            show_document();
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

}  // namespace o1602::app
