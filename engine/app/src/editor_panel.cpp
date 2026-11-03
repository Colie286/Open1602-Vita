// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor_panel.hpp"

#include <algorithm>
#include <cctype>

#include "o1602/assets/game_data.hpp"
#include "o1602/sim/limits.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"
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
constexpr Id kTerrainPage = 58;
constexpr Id kFirstCrop = 60;  ///< + the index into kCropWares
constexpr Id kTerrainTool = 70;
constexpr Id kTerrainGroup = 71;
constexpr Id kTerrainPiece = 72;
constexpr Id kTerrainRotation = 73;
constexpr Id kTerrainBrush = 74;
constexpr Id kTerrainCopy = 75;
constexpr Id kTerrainTurn = 76;
constexpr Id kTerrainMirror = 77;
constexpr Id kObjectsPage = 59;
constexpr Id kObjectsTool = 80;
constexpr Id kObjectsGroup = 81;
constexpr Id kObjectsPiece = 82;
constexpr Id kObjectsRotation = 83;
constexpr Id kObjectsOwner = 84;
constexpr Id kFirstOverlay = 30;  ///< + EditorOverlay
constexpr Id kLandscape = 85;
constexpr Id kCityNamePage = 86;
constexpr Id kCityGoodsPage = 87;
constexpr Id kCityOwner = 88;
constexpr Id kCitySelection = 89;      ///< back from a city's page to the selection
constexpr Id kFirstGoodLess = 120;     ///< + the row on the page "Stock"
constexpr Id kFirstGoodMore = 140;     ///< + the row on the page "Stock"
constexpr std::size_t kGoodRows = 20;  ///< rows of the page "Stock" at most
constexpr Id kFirstRow = 100;          ///< + the row on the page

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
constexpr std::array<const char*, EditorTerrain::kTools> kTerrainToolTexts = {
    "editor.terrain.tool.brush",   "editor.terrain.tool.land",    "editor.terrain.tool.water",
    "editor.terrain.tool.coast",   "editor.terrain.tool.random",  "editor.terrain.tool.fill",
    "editor.terrain.tool.replace", "editor.terrain.tool.pipette", "editor.terrain.tool.select",
    "editor.terrain.tool.paste"};
constexpr std::array<const char*, EditorTerrain::kTools> kTerrainToolNotes = {
    "editor.terrain.note.brush",   "editor.terrain.note.land",    "editor.terrain.note.water",
    "editor.terrain.note.coast",   "editor.terrain.note.random",  "editor.terrain.note.fill",
    "editor.terrain.note.replace", "editor.terrain.note.pipette", "editor.terrain.note.select",
    "editor.terrain.note.paste"};
/// By editor::TerrainGroup.
constexpr std::array<const char*, 8> kTerrainGroupTexts = {"editor.terrain.group.ground", "editor.terrain.group.forest",
                                                           "editor.terrain.group.rock",   "editor.terrain.group.slope",
                                                           "editor.terrain.group.river",  "editor.terrain.group.beach",
                                                           "editor.terrain.group.coast",  "editor.terrain.group.sea"};
constexpr std::array<const char*, 4> kTerrainRotationTexts = {"editor.terrain.rotation.0", "editor.terrain.rotation.1",
                                                              "editor.terrain.rotation.2", "editor.terrain.rotation.3"};

constexpr std::array<const char*, EditorObjects::kTools> kObjectToolTexts = {
    "editor.objects.tool.build", "editor.objects.tool.ship", "editor.objects.tool.unit", "editor.objects.tool.resource",
    "editor.objects.tool.remove"};
constexpr std::array<const char*, EditorObjects::kTools> kObjectToolNotes = {
    "editor.objects.note.build", "editor.objects.note.ship", "editor.objects.note.unit", "editor.objects.note.resource",
    "editor.objects.note.remove"};
constexpr std::array<const char*, EditorObjects::kTools> kObjectPieceLabels = {
    "editor.objects.piece", "editor.objects.ship", "editor.objects.unit", "editor.objects.resource",
    "editor.objects.piece"};

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

/// The climates a new island may take: north, south and the packages' (not a placeholder's,
/// which the world plan makes north or south).
std::size_t climate_choices(const EditorModel& model, bool placeholder) {
    return 2 + (placeholder ? 0 : model.climates.size());
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
    if (m_page == Page::New || m_page == Page::Open || m_page == Page::SaveAs || m_page == Page::CityName) {
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
            layout_new(model);
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
        case Page::Terrain:
            layout_terrain(model);
            break;
        case Page::Objects:
            layout_objects(model);
            break;
        case Page::IslandFiles:
            layout_open(model);
            break;
        case Page::CityName:
            layout_city_name(model);
            break;
        case Page::CityGoods:
            layout_city_goods(model);
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
    constexpr std::int32_t kButtons = 12;                        // rows
    constexpr std::int32_t kNoticeRoom = 2 * 20 + 6 + kButtons;  // and what rounding the pitch down loses
    const std::int32_t bottom = m_top + m_height - 4;
    const std::int32_t pitch = std::clamp((bottom - kNoticeRoom - y) / kButtons, 22, kButtonH + kGap);
    const std::int32_t gap = std::min(kGap, std::max(2, pitch / 8));
    const auto full = [&](Id id, const char* text, bool enabled = true) {
        f.button(id, Rect{kX, y, kW, pitch - gap}, ui::tr(text), enabled);
        y += pitch;
    };
    // Two buttons of short names side by side in one row: open and check.
    const auto pair = [&](Id left, const char* left_text, bool left_on, Id right, const char* right_text,
                          bool right_on) {
        const std::int32_t half = (kW - kGap) / 2;
        f.button(left, Rect{kX, y, half, pitch - gap}, ui::tr(left_text), left_on);
        f.button(right, Rect{kX + half + kGap, y, kW - half - kGap, pitch - gap}, ui::tr(right_text), right_on);
        y += pitch;
    };
    full(kNew, "editor.new");
    pair(kOpen, "editor.open", true, kCheck, "editor.check", true);
    full(kSave, "editor.save");
    full(kSaveAs, "editor.save_as");
    full(kUndo, "editor.undo", model.can_undo);
    full(kRedo, "editor.redo", model.can_redo);
    full(kIslandPage, "editor.islands.edit");
    full(kTerrainPage, "editor.terrain.edit");
    full(kObjectsPage, "editor.objects.edit");
    full(kWorldSizePage, "editor.world_size.edit");
    full(kView, "editor.view");
    full(kClose, "editor.close");
    // The notice where two lines fit: in a small window the buttons take the room, and the status
    // bar shows it.
    if (!model.notice.empty() && bottom - y - 2 >= 2 * 20) {
        f.paragraph(Rect{kX, y + 2, kW, std::max(0, bottom - y - 2)}, model.notice,
                    model.notice_is_error ? Tone::Warning : Tone::Dim, 20);
    }
}

void EditorPanel::layout_new(const EditorModel& model) {
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
    if (!model.landscapes.empty()) {
        m_new.landscape = std::min(m_new.landscape, model.landscapes.size() - 1);
        value(kLandscape, "editor.new.landscape", model.landscapes[m_new.landscape], islands);
    }
    value(kPlayers, "editor.new.players", count(m_new.players), islands);
    f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.new.seed"), std::to_string(m_new.seed),
            islands ? Tone::Normal : Tone::Dim, Tone::Dim);
    y += kLine + 2;
    f.button(kReroll, Rect{kX, y, kW, kButtonH}, ui::tr("editor.new.reroll"), islands);
    y += kButtonH + kGap + 4;
    const std::int32_t half = (kW - kGap) / 2;
    f.button(kCreate, Rect{kX, y, half, kButtonH}, ui::tr("editor.new.create"));
    f.button(kCancel, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.cancel"));
    y += kButtonH + kGap;
    // The note where it fits: in a small window the settings and the buttons take the room.
    const std::int32_t room = std::min(kNote, m_top + m_height - 4 - y);
    if (room >= 2 * 20) {
        f.paragraph(Rect{kX, y, kW, room}, ui::tr(islands ? "editor.new.note_islands" : "editor.new.note_sea"),
                    Tone::Dim, 20);
    }
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
        // Room for the buttons below: back, and an island's three rows or a city's two.
        const std::int32_t rows = model.island || model.placeholder ? 4 : model.city ? 3 : 1;
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
        } else if (model.city) {
            const std::int32_t half = (kW - kGap) / 2;
            f.button(kCityNamePage, Rect{kX, y, half, kButtonH}, ui::tr("editor.city.rename"));
            f.button(kCityGoodsPage, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.city.stock"),
                     model.city->room > 0);
            y += kButtonH + kGap;
            f.cycle_button(kCityOwner, Rect{kX, y, kW, kButtonH}, ui::tr("editor.city.owner", {model.city->owner}));
            y += kButtonH + kGap;
        }
    }
    f.button(kBack, Rect{kX, y, kW, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_city_name(const EditorModel& model) {
    if (!model.city) {
        m_page = Page::Selection;  // the city is gone (undone)
        layout_selection(model);
        return;
    }
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.city.rename_title"), Align::Left, Tone::Heading);
    y += 34;
    f.label(Rect{kX, y, kW, kLine}, ui::tr("editor.city.name"));
    y += kLine;
    f.field(kField, Rect{kX, y, kW, 32}, m_text);
    y += 38;
    const bool valid = sim::valid_name(m_text, sim::CityOrder::kMaxNameLength);
    const std::int32_t half = (kW - kGap) / 2;
    f.button(kStore, Rect{kX, y, half, kButtonH}, ui::tr("editor.city.rename_ok"), valid);
    f.button(kCitySelection, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_city_goods(const EditorModel& model) {
    if (!model.city || model.city->room == 0) {
        m_page = Page::Selection;
        layout_selection(model);
        return;
    }
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.city.stock_title", {model.city->name}), Align::Left, Tone::Heading);
    y += 28;
    f.paragraph(Rect{kX, y, kW, 3 * 20}, ui::tr("editor.city.room", {count(model.city->room / 32)}), Tone::Dim, 20);
    y += 3 * 20 + 4;
    // As many rows as fit above the buttons below: back, and the pages.
    constexpr std::int32_t kGoodH = 26;
    const std::int32_t bottom = m_top + m_height - 2 * (kButtonH + kGap) - kGap;
    const std::size_t n = model.city->goods.size();
    const auto fit = static_cast<std::size_t>(std::max<std::int32_t>(1, (bottom - y) / kGoodH));
    m_page_rows = std::min({fit, kGoodRows, std::max<std::size_t>(n, 1)});
    m_first = std::min(m_first, n > m_page_rows ? n - m_page_rows : 0);
    constexpr std::int32_t kStep = 28;
    for (std::size_t row = 0; row < m_page_rows && m_first + row < n; ++row) {
        const EditorGood& g = model.city->goods[m_first + row];
        const auto id = static_cast<Id>(row);
        constexpr std::int32_t kAmount = 40;  // "999" (tons, as the hint says) in the panel's font
        f.label(Rect{kX, y, kW - 2 * kStep - kAmount - 4, kGoodH - 2}, g.name);
        f.label(Rect{kX + kW - 2 * kStep - kAmount - 4, y, kAmount, kGoodH - 2},
                ui::tr("editor.city.tons", {count(g.amount / 32)}), Align::Right);
        // Cycle buttons: the right button takes a ton.
        f.cycle_button(kFirstGoodLess + id, Rect{kX + kW - 2 * kStep - 2, y, kStep, kGoodH - 2}, "-", g.amount > 0);
        f.cycle_button(kFirstGoodMore + id, Rect{kX + kW - kStep, y, kStep, kGoodH - 2}, "+",
                       g.amount < model.city->room);
        y += kGoodH;
    }
    y += 4;
    const std::int32_t half = (kW - kGap) / 2;
    f.button(kPrevious, Rect{kX, y, half, kButtonH}, ui::tr("editor.previous"), m_first > 0);
    f.button(kNext, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.next"), m_first + m_page_rows < n);
    y += kButtonH + kGap;
    f.button(kCitySelection, Rect{kX, y, kW, kButtonH}, ui::tr("editor.city.back"));
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
        if (!own && (model.island_crops >> (kCropWares[i] - assets::ware::kCornField) & 1U) != 0) {
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

void EditorPanel::layout_terrain(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.terrain.title"), Align::Left, Tone::Heading);
    y += 28;
    const EditorTerrain& t = model.terrain;
    using Tool = EditorTerrain::Tool;
    // Each setting's name above its value, which takes the panel's width.
    const auto value = [&](Id id, const char* label, std::string text, bool enabled) {
        f.label(Rect{kX, y, kW, 18}, ui::tr(label), Align::Left, enabled ? Tone::Normal : Tone::Dim);
        y += 18;
        f.cycle_button(id, Rect{kX, y, kW, kButtonH}, std::move(text), enabled);
        y += kButtonH + 2;
    };
    value(kTerrainTool, "editor.terrain.tool", ui::tr(kTerrainToolTexts[static_cast<std::size_t>(t.tool)]), true);
    std::string note = ui::tr(kTerrainToolNotes[static_cast<std::size_t>(t.tool)]);
    if (t.tool == Tool::Select || t.tool == Tool::Paste) {
        // The copy: what it holds, and copying, turning and mirroring it, in the place of the
        // four settings of the other tools.
        const bool copied = model.clip_width > 0 && model.clip_height > 0;
        f.label(Rect{kX, y, kW, 18},
                copied ? ui::tr("editor.terrain.copy_size", {count(model.clip_width), count(model.clip_height)})
                       : ui::tr("editor.terrain.no_copy"),
                Align::Left, copied ? Tone::Normal : Tone::Dim);
        y += 18;
        f.button(kTerrainCopy, Rect{kX, y, kW, kButtonH}, ui::tr("editor.terrain.copy"), model.terrain_selected);
        y += kButtonH + 4;
        f.button(kTerrainTurn, Rect{kX, y, kW, kButtonH}, ui::tr("editor.terrain.turn"), copied);
        y += kButtonH + 4;
        f.button(kTerrainMirror, Rect{kX, y, kW, kButtonH}, ui::tr("editor.terrain.mirror"), copied);
        y += kButtonH + 4;
        y += 4 * (18 + kButtonH + 2) - (18 + 3 * (kButtonH + 4));  // as tall as the four settings
    } else {
        const bool shaping = t.tool == Tool::Land || t.tool == Tool::Water || t.tool == Tool::Coast;
        value(kTerrainGroup, "editor.terrain.group", ui::tr(kTerrainGroupTexts[t.group % kTerrainGroupTexts.size()]),
              t.tool != Tool::Pipette && !shaping);
        std::string piece = ui::tr("editor.terrain.no_piece");
        if (model.terrain_pieces > 0) {
            piece = ui::tr("editor.terrain.piece_value",
                           {count(model.terrain_id), count(t.piece + 1), count(model.terrain_pieces)});
        }
        const bool pieces = model.terrain_pieces > 0 && t.tool != Tool::Random && t.tool != Tool::Pipette && !shaping;
        value(kTerrainPiece, "editor.terrain.piece", std::move(piece), pieces);
        value(kTerrainRotation, "editor.terrain.rotation", ui::tr(kTerrainRotationTexts[t.rotation & 3U]),
              pieces && model.terrain_turns);
        const std::uint32_t side = kTerrainBrushes[t.brush % kTerrainBrushes.size()];
        const bool single = model.terrain_width == 1 && model.terrain_height == 1;
        value(kTerrainBrush, "editor.terrain.brush", ui::tr("editor.size", {count(side), count(side)}),
              t.tool == Tool::Random || shaping || (t.tool == Tool::Brush && single));
        if (pieces && !single) {
            note = ui::tr("editor.terrain.note.large", {count(model.terrain_width), count(model.terrain_height)});
        }
    }
    // The tool's note, or in its place the notice of the last stroke (as tall as the panel is at
    // 1024 x 768: the back button stays below it).
    constexpr std::int32_t kTerrainNote = 4 * 20;
    if (!model.notice.empty()) {
        f.paragraph(Rect{kX, y, kW, kTerrainNote}, model.notice, model.notice_is_error ? Tone::Warning : Tone::Dim, 20);
    } else {
        f.paragraph(Rect{kX, y, kW, kTerrainNote}, note, Tone::Dim, 20);
    }
    y += kTerrainNote + 4;
    f.button(kBack, Rect{kX, y, kW, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_objects(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.objects.title"), Align::Left, Tone::Heading);
    y += 28;
    const EditorObjects& o = model.objects;
    using Tool = EditorObjects::Tool;
    const bool build = o.tool == Tool::Build;
    // Ships and land units: the piece is their type, the rotation the way they face.
    const bool figure = o.tool == Tool::Ship || o.tool == Tool::Unit || o.tool == Tool::Resource;
    const std::size_t chosen = o.tool == Tool::Ship       ? o.ship
                               : o.tool == Tool::Unit     ? o.unit
                               : o.tool == Tool::Resource ? o.resource
                                                          : o.piece;
    const auto value = [&](Id id, const std::string& label, std::string text, bool enabled) {
        f.label(Rect{kX, y, kW, 18}, label, Align::Left, enabled ? Tone::Normal : Tone::Dim);
        y += 18;
        f.cycle_button(id, Rect{kX, y, kW, kButtonH}, std::move(text), enabled);
        y += kButtonH + 2;
    };
    value(kObjectsTool, ui::tr("editor.objects.tool"), ui::tr(kObjectToolTexts[static_cast<std::size_t>(o.tool)]),
          true);
    value(kObjectsGroup, ui::tr("editor.objects.group"), model.object_group, build && model.object_groups > 0);
    // A ship's or unit's name is long: its number goes on the label above it.
    const char* piece_label = kObjectPieceLabels[static_cast<std::size_t>(o.tool)];
    std::string piece = ui::tr("editor.terrain.no_piece");
    std::string label = ui::tr(piece_label);
    if (model.object_pieces > 0) {
        const std::string n = count(std::min(chosen, model.object_pieces - 1) + 1);
        piece = figure ? model.object_name
                       : ui::tr("editor.objects.piece_value", {model.object_name, n, count(model.object_pieces)});
        if (figure) {
            label = ui::tr(piece_label, {n, count(model.object_pieces)});
        }
    }
    value(kObjectsPiece, label, std::move(piece), (build || figure) && model.object_pieces > 0);
    value(kObjectsRotation, ui::tr("editor.objects.rotation"), ui::tr(kTerrainRotationTexts[o.rotation & 3U]),
          (build && model.object_turns) || o.tool == Tool::Ship || o.tool == Tool::Unit);
    value(kObjectsOwner, ui::tr("editor.objects.owner"),
          model.players > 0 ? model.object_owner : ui::tr("editor.objects.no_owner"),
          (build || o.tool == Tool::Ship || o.tool == Tool::Unit) && model.players > 0);
    std::string note = ui::tr(kObjectToolNotes[static_cast<std::size_t>(o.tool)]);
    if ((build || o.tool == Tool::Ship || o.tool == Tool::Unit) && model.players == 0) {
        note = ui::tr("editor.objects.note.no_players");
    }
    constexpr std::int32_t kObjectsNote = 4 * 20;
    if (!model.notice.empty()) {
        f.paragraph(Rect{kX, y, kW, kObjectsNote}, model.notice, model.notice_is_error ? Tone::Warning : Tone::Dim, 20);
    } else {
        f.paragraph(Rect{kX, y, kW, kObjectsNote}, note, Tone::Dim, 20);
    }
    y += kObjectsNote + 4;
    f.button(kBack, Rect{kX, y, kW, kButtonH}, ui::tr("editor.back"));
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
    if (m_island.climate >= climate_choices(model, m_island.placeholder)) {
        m_island.climate = 0;  // a package's climate the world does not know (any more)
    }
    value(kClimate, "editor.islands.climate",
          m_island.climate >= 2   ? model.climates[m_island.climate - 2U]
          : m_island.climate == 0 ? ui::tr("editor.prop.north")
                                  : ui::tr("editor.prop.south"));
    value(kIslandSize, "editor.islands.size", ui::tr(kIslandSizeTexts[m_island.size % kIslandSizeTexts.size()]));
    f.button(kOtherIsland, Rect{kX, y, kW, kButtonH}, ui::tr("editor.islands.other"),
             !model.placing && !m_island.placeholder);
    y += kButtonH + kGap;
    if (!m_island.own_shape && !m_island.placeholder && !model.island_template.empty()) {
        f.label(Rect{kX, y, kW, kLine}, model.island_template, Align::Left, Tone::Dim);
        y += kLine + 2;
    }
    f.button(kPlace, Rect{kX, y, kW, kButtonH}, ui::tr("editor.islands.place"), !model.placing);
    y += kButtonH + kGap;
    f.button(kLoadIsland, Rect{kX, y, kW, kButtonH}, ui::tr("editor.islands.load"), !model.placing);
    y += kButtonH + kGap;
    // The note where its three lines fit above the buttons below: a sentence cut reads wrong.
    const std::int32_t below = (model.placing ? 2 : 1) * (kButtonH + kGap) + 4;
    constexpr std::int32_t note = 3 * 20;
    if (m_top + m_height - 4 - below - y >= note) {
        f.paragraph(Rect{kX, y, kW, note},
                    ui::tr(!model.placing         ? "editor.islands.note"
                           : m_island.placeholder ? "editor.placeholder.placing"
                                                  : "editor.islands.placing"),
                    Tone::Dim, 20);
        y += note + 4;
    }
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
    if (m_page == Page::CityName) {
        if (!sim::valid_name(m_text, sim::CityOrder::kMaxNameLength)) {
            return std::nullopt;
        }
        m_page = Page::Selection;
        m_form.set_focus(std::nullopt);
        EditorRequest r{EditorRequest::Kind::RenameCity};
        r.name = m_text;
        return r;
    }
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
        case kLandscape:
            m_new.landscape = step(m_new.landscape, std::max<std::size_t>(model.landscapes.size(), 1), forward);
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
            m_island.climate = static_cast<std::uint8_t>(
                step(m_island.climate, climate_choices(model, m_island.placeholder), forward));
            return std::nullopt;
        case kIslandSize:
            m_island.size = static_cast<std::uint8_t>(step(m_island.size, kIslandSizeTexts.size(), forward));
            return std::nullopt;
        case kOtherIsland:
            // The next template in turn (EditorModel::island_template says which), or another shape.
            m_island.seed = m_island.own_shape ? next_seed(m_island.seed) : m_island.seed + 1;
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
        case kTerrainPage:
            m_page = Page::Terrain;
            return std::nullopt;
        case kObjectsPage:
            m_page = Page::Objects;
            return std::nullopt;
        case kObjectsTool:
        case kObjectsGroup:
        case kObjectsPiece:
        case kObjectsRotation:
        case kObjectsOwner: {
            EditorRequest r{EditorRequest::Kind::Objects};
            EditorObjects& o = r.objects;
            o = model.objects;
            if (*id == kObjectsTool) {
                o.tool = static_cast<EditorObjects::Tool>(
                    step(static_cast<std::size_t>(o.tool), EditorObjects::kTools, forward));
            } else if (*id == kObjectsGroup) {
                o.group = model.object_groups > 0 ? step(o.group, model.object_groups, forward) : 0;
                o.piece = 0;
                o.rotation = 0;
            } else if (*id == kObjectsPiece) {
                using Tool = EditorObjects::Tool;
                std::size_t& chosen = o.tool == Tool::Ship       ? o.ship
                                      : o.tool == Tool::Unit     ? o.unit
                                      : o.tool == Tool::Resource ? o.resource
                                                                 : o.piece;
                chosen = model.object_pieces > 0 ? step(chosen, model.object_pieces, forward) : 0;
                if (o.tool == Tool::Build) {
                    o.rotation = 0;
                }
            } else if (*id == kObjectsRotation) {
                o.rotation = static_cast<std::uint8_t>(step(o.rotation, 4, forward));
            } else {
                o.owner = model.players > 0 ? step(o.owner, model.players, forward) : 0;
            }
            return r;
        }
        case kTerrainCopy:
            return EditorRequest{EditorRequest::Kind::TerrainCopy};
        case kTerrainTurn:
            return EditorRequest{EditorRequest::Kind::TerrainTurn};
        case kTerrainMirror:
            return EditorRequest{EditorRequest::Kind::TerrainMirror};
        case kTerrainTool:
        case kTerrainGroup:
        case kTerrainPiece:
        case kTerrainRotation:
        case kTerrainBrush: {
            EditorRequest r{EditorRequest::Kind::Terrain};
            EditorTerrain& t = r.terrain;
            t = model.terrain;
            if (*id == kTerrainTool) {
                t.tool = static_cast<EditorTerrain::Tool>(
                    step(static_cast<std::size_t>(t.tool), EditorTerrain::kTools, forward));
            } else if (*id == kTerrainGroup) {
                t.group = static_cast<std::uint8_t>(step(t.group, kTerrainGroupTexts.size(), forward));
                t.piece = 0;
                t.rotation = 0;
            } else if (*id == kTerrainPiece) {
                t.piece = model.terrain_pieces > 0 ? step(t.piece, model.terrain_pieces, forward) : 0;
                t.rotation = 0;
            } else if (*id == kTerrainRotation) {
                t.rotation = static_cast<std::uint8_t>(step(t.rotation, 4, forward));
            } else {
                t.brush = static_cast<std::uint8_t>(step(t.brush, kTerrainBrushes.size(), forward));
            }
            return r;
        }
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
        case kCityNamePage:
            if (model.city) {
                m_page = Page::CityName;
                m_text = model.city->name;
                m_form.set_focus(kField);
                m_form.select_all();
            }
            return std::nullopt;
        case kCityGoodsPage:
            m_page = Page::CityGoods;
            m_first = 0;
            return std::nullopt;
        case kCitySelection:
            m_page = Page::Selection;
            m_form.set_focus(std::nullopt);
            return std::nullopt;
        case kCityOwner: {
            EditorRequest r{EditorRequest::Kind::CityOwner};
            r.index = forward ? 1 : 0;
            return r;
        }
        case kPrevious: {
            // The check list and the stock go back by the lines their page shows.
            const std::size_t rows = m_page == Page::Check || m_page == Page::CityGoods ? m_page_rows : kRows;
            m_first = m_first > rows ? m_first - rows : 0;
            return std::nullopt;
        }
        case kNext:
            m_first += m_page == Page::Check || m_page == Page::CityGoods ? m_page_rows : kRows;
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
    if (*id >= kFirstGoodLess && *id < kFirstGoodMore + static_cast<Id>(kGoodRows) && model.city) {
        // Left 10 t, right 1 t; within the city's room.
        const bool more = *id >= kFirstGoodMore;
        const std::size_t i = m_first + static_cast<std::size_t>(*id - (more ? kFirstGoodMore : kFirstGoodLess));
        if (i >= model.city->goods.size()) {
            return std::nullopt;
        }
        const EditorGood& g = model.city->goods[i];
        const std::uint32_t step = forward ? 10 * 32 : 32;
        EditorRequest r{EditorRequest::Kind::CityGoods};
        r.index = g.ware;
        // Down to whole tons, then by steps.
        r.amount = more ? std::min(model.city->room, (g.amount / step + 1) * step)
                        : (g.amount % step != 0 ? g.amount / step * step : g.amount - std::min(g.amount, step));
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
            if (m_page == Page::CityName) {
                m_page = Page::Selection;
                m_form.set_focus(std::nullopt);
            } else {
                show_document();
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

}  // namespace o1602::app
