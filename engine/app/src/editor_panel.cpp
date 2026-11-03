// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor_panel.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

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
constexpr Id kTerrainSmaller = 78;  ///< the brush a size smaller
constexpr Id kTerrainBigger = 79;   ///< the brush a size bigger
constexpr Id kTerrainShape = 173;   ///< the brush's shape
constexpr Id kTerrainSlider = 174;  ///< the brush's size slid
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
constexpr Id kCitySelection = 89;  ///< back from a city's page to the selection
constexpr Id kPlayersPage = 90;
constexpr Id kPlayerSlot = 91;
constexpr Id kPlayerRole = 92;
constexpr Id kPlayerColour = 93;
constexpr Id kPlayerMoney = 94;
constexpr Id kPlayerMonuments = 95;
constexpr Id kPlayerArches = 96;
constexpr Id kPlayerNamePage = 97;
constexpr Id kPlayerAdd = 98;
constexpr Id kPlayerRemove = 99;
constexpr Id kPlayersBack = 160;  ///< back from a player's name to the page "Players"
constexpr Id kRelationsPage = 161;
constexpr Id kRelationPartner = 162;
constexpr Id kRelationPeace = 163;
constexpr Id kRelationTrade = 164;
constexpr Id kRulesPage = 165;
constexpr Id kRulesOwn = 166;       ///< the scenario's own settings, or the player's
constexpr Id kRulesOriginal = 167;  ///< all as in the original
constexpr Id kRulesOpen1602 = 168;  ///< all as Open1602 has them
constexpr Id kRandomPage = 169;
constexpr Id kRandomBack = 170;        ///< from the random elements to the rules
constexpr Id kRandomRoll = 171;        ///< the dice preview, or another roll of it
constexpr Id kPreviewEnd = 172;        ///< from the dice preview back to the plan
constexpr Id kFirstGoodLess = 120;     ///< + the row on the page "Stock"
constexpr Id kFirstGoodMore = 140;     ///< + the row on the page "Stock"
constexpr std::size_t kGoodRows = 20;  ///< rows of the page "Stock" at most
constexpr Id kFirstRow = 100;          ///< + the row on the page
constexpr Id kTabPlayers = 180;        ///< the players dialog's tab of the players
constexpr Id kTabRelations = 181;      ///< the players dialog's tab of the diplomacy
constexpr Id kColumnsBack = 182;       ///< the diplomacy table's columns before
constexpr Id kColumnsOn = 183;         ///< the diplomacy table's columns after
constexpr Id kRecent = 190;            ///< + the object placed lately (the page "Objects")
constexpr Id kPlayerTable = 300;       ///< + 8 * the row + the column of the players' table
constexpr std::size_t kTableRows = 16;
constexpr Id kRelationTable = 500;  ///< + 16 * the row + the column of the diplomacy table
constexpr std::size_t kTableSide = 16;
/// The dialogs' widths at most: wide for tables, narrow for the other pages.
constexpr std::int32_t kWideDialog = 980;
constexpr std::int32_t kNarrowDialog = 560;
/// Below this width the dialogs keep the column's forms.
constexpr std::int32_t kWideForm = 600;

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
/// The tools' names in the tool bar, the page's heading.
constexpr std::array<const char*, EditorTerrain::kTools> kTerrainToolTitles = {
    "editor.tool.brush", "editor.tool.land",    "editor.tool.water",   "editor.tool.coast", "editor.tool.random",
    "editor.tool.fill",  "editor.tool.replace", "editor.tool.pipette", "editor.tool.area",  "editor.tool.paste"};
/// By editor::BrushShape.
constexpr std::array<const char*, 3> kBrushShapeTexts = {"editor.terrain.shape.square", "editor.terrain.shape.round",
                                                         "editor.terrain.shape.diamond"};
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

constexpr std::array<const char*, EditorObjects::kTools> kObjectToolTitles = {
    "editor.tool.build", "editor.tool.template", "editor.tool.ship",
    "editor.tool.unit",  "editor.tool.resource", "editor.tool.remove"};
constexpr std::array<const char*, EditorObjects::kTools> kObjectToolNotes = {
    "editor.objects.note.build", "editor.objects.note.template", "editor.objects.note.ship",
    "editor.objects.note.unit",  "editor.objects.note.resource", "editor.objects.note.remove"};
constexpr std::array<const char*, EditorObjects::kTools> kObjectPieceLabels = {
    "editor.objects.building", "editor.objects.template", "editor.objects.ship",
    "editor.objects.unit",     "editor.objects.resource", "editor.objects.piece"};

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

/// Whether a page goes back and on by the rows it shows (else by kRows).
bool paged_by_rows(EditorPanel::Page page) noexcept {
    using P = EditorPanel::Page;
    return page == P::Check || page == P::CityGoods || page == P::Rules || page == P::Random;
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
    if (m_page == Page::New || m_page == Page::Open || m_page == Page::SaveAs || m_page == Page::CityName ||
        m_page == Page::PlayerName) {
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

std::optional<EditorRequest> EditorPanel::command(EditorCommand command, const EditorModel& model) {
    m_form.set_focus(std::nullopt);
    switch (command) {
        case EditorCommand::New:
            return press(kNew, model, true);
        case EditorCommand::Open:
            return press(kOpen, model, true);
        case EditorCommand::Save:
            return press(kSave, model, true);
        case EditorCommand::SaveAs:
            return press(kSaveAs, model, true);
        case EditorCommand::Close:
            return press(kClose, model, true);
        case EditorCommand::Undo:
            return model.can_undo ? press(kUndo, model, true) : std::nullopt;
        case EditorCommand::Redo:
            return model.can_redo ? press(kRedo, model, true) : std::nullopt;
        case EditorCommand::View:
            return press(kView, model, true);
        case EditorCommand::Islands:
            m_island.placeholder = false;
            return press(kIslandPage, model, true);
        case EditorCommand::WorldSize:
            return press(kWorldSizePage, model, true);
        case EditorCommand::Random:
            return press(kRandomPage, model, true);
        case EditorCommand::Preview:
            if (!model.has_plan && !model.previewing) {
                return std::nullopt;
            }
            m_page = Page::Random;
            m_first = 0;
            return EditorRequest{EditorRequest::Kind::RandomRoll};
        case EditorCommand::Players:
            return press(kPlayersPage, model, true);
        case EditorCommand::Rules:
            return press(kRulesPage, model, true);
        case EditorCommand::Check:
            return press(kCheck, model, true);
        case EditorCommand::Keys:
            return EditorRequest{EditorRequest::Kind::ShowKeys};
    }
    return std::nullopt;
}

namespace {

constexpr std::array<std::pair<EditorTool, EditorTerrain::Tool>, 10> kTerrainTools = {{
    {EditorTool::Land, EditorTerrain::Tool::Land},
    {EditorTool::Water, EditorTerrain::Tool::Water},
    {EditorTool::Coast, EditorTerrain::Tool::Coast},
    {EditorTool::Brush, EditorTerrain::Tool::Brush},
    {EditorTool::Random, EditorTerrain::Tool::Random},
    {EditorTool::Fill, EditorTerrain::Tool::Fill},
    {EditorTool::Replace, EditorTerrain::Tool::Replace},
    {EditorTool::Pipette, EditorTerrain::Tool::Pipette},
    {EditorTool::Area, EditorTerrain::Tool::Select},
    {EditorTool::Paste, EditorTerrain::Tool::Paste},
}};

constexpr std::array<std::pair<EditorTool, EditorObjects::Tool>, 6> kObjectTools = {{
    {EditorTool::Build, EditorObjects::Tool::Build},
    {EditorTool::Template, EditorObjects::Tool::Template},
    {EditorTool::Ship, EditorObjects::Tool::Ship},
    {EditorTool::Unit, EditorObjects::Tool::Unit},
    {EditorTool::Resource, EditorObjects::Tool::Resource},
    {EditorTool::Remove, EditorObjects::Tool::Remove},
}};

}  // namespace

std::optional<EditorRequest> EditorPanel::choose_tool(EditorTool tool, const EditorModel& model) {
    m_form.set_focus(std::nullopt);
    switch (tool) {
        case EditorTool::Select:
            m_page = Page::Selection;
            return std::nullopt;
        case EditorTool::Island:
            m_page = Page::Islands;
            m_island.placeholder = false;
            return std::nullopt;
        case EditorTool::Placeholder:
            if (model.has_plan) {
                m_page = Page::Islands;
                m_island.placeholder = true;
            }
            return std::nullopt;
        default:
            break;
    }
    for (const auto& [t, terrain] : kTerrainTools) {
        if (t == tool) {
            m_page = Page::Terrain;
            EditorRequest r{EditorRequest::Kind::Terrain};
            r.terrain = model.terrain;
            r.terrain.tool = terrain;
            return r;
        }
    }
    for (const auto& [t, objects] : kObjectTools) {
        if (t == tool) {
            m_page = Page::Objects;
            EditorRequest r{EditorRequest::Kind::Objects};
            r.objects = model.objects;
            r.objects.tool = objects;
            return r;
        }
    }
    return std::nullopt;
}

std::optional<EditorTool> EditorPanel::active_tool(const EditorModel& model) const noexcept {
    switch (m_page) {
        case Page::Selection:
        case Page::CityName:
        case Page::CityGoods:
        case Page::Fertility:
            return EditorTool::Select;
        case Page::Islands:
        case Page::IslandFiles:
            return m_island.placeholder && model.has_plan ? EditorTool::Placeholder : EditorTool::Island;
        case Page::Terrain:
            for (const auto& [t, terrain] : kTerrainTools) {
                if (terrain == model.terrain.tool) {
                    return t;
                }
            }
            return std::nullopt;
        case Page::Objects:
            for (const auto& [t, objects] : kObjectTools) {
                if (objects == model.objects.tool) {
                    return t;
                }
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

bool EditorPanel::dialog() const noexcept {
    using P = Page;
    return m_screen_w > 0 && !m_previewing &&
           (m_page == P::Players || m_page == P::PlayerName || m_page == P::Relations || m_page == P::Rules ||
            m_page == P::Random || m_page == P::WorldSize || m_page == P::Check);
}

ui::Rect EditorPanel::dialog_rect() const noexcept {
    using P = Page;
    const bool wide = m_page == P::Players || m_page == P::Relations || m_page == P::Rules || m_page == P::Random;
    // Right of the tool bar, between the menu bar and the status line; as tall as the page needs.
    constexpr std::int32_t kToolBar = 42;
    constexpr std::int32_t kBars = 26 + 24;
    const std::int32_t room_w = m_screen_w - kToolBar - 16;
    const std::int32_t room_h = m_screen_h - kBars - 16;
    const std::int32_t want = m_page == P::PlayerName  ? 230
                              : m_page == P::WorldSize ? 440
                              : m_page == P::Check     ? m_check_h
                              : wide                   ? 620
                                                       : 580;
    const std::int32_t w = std::max(kWidth, std::min(room_w, wide ? kWideDialog : kNarrowDialog));
    const std::int32_t h = std::max(200, std::min(room_h, want));
    return Rect{kToolBar + 8 + (room_w - w) / 2 - m_column_x, 26 + 8 + (room_h - h) / 2, w, h};
}

void EditorPanel::layout(const EditorModel& model) {
    m_form.clear();
    if (m_page != m_laid_page) {
        // Another page: a button held on the last one is not this one's.
        m_form.cancel_press();
        m_laid_page = m_page;
    }
    m_previewing = model.previewing;
    if (model.previewing) {
        // The plan waits: nothing but another roll or the way back to it.
        m_form.panel(Rect{m_x - 6, m_top, kWidth - 2 * (m_x - 6), m_height});
        layout_preview(model);
        return;
    }
    if (m_page == Page::Check) {
        // The check list's dialog as tall as its lines (up to its room; then it has pages).
        std::int32_t h = 16 + 6 + 32 + (kButtonH + kGap) + 6 + 8;
        for (const EditorCheckLine& line : model.check) {
            h += line.kind == EditorCheckLine::Kind::Heading ? 2 * 20 + 2 : 3 * 20;
        }
        m_check_h = h;
    }
    if (dialog()) {
        // A page of the whole map: a dialog in the window's middle, laid out inside its frame.
        const Rect d = dialog_rect();
        m_form.panel(d);
        const std::array<std::int32_t, 4> column = {m_x, m_w, m_top, m_height};
        m_x = d.x + 18;
        m_w = d.w - 36;
        m_top = d.y + 8;
        m_height = d.h - 16;
        layout_page(model);
        m_x = column[0];
        m_w = column[1];
        m_top = column[2];
        m_height = column[3];
        return;
    }
    m_form.panel(Rect{m_x - 6, m_top, kWidth - 2 * (m_x - 6), m_height});
    layout_page(model);
}

void EditorPanel::layout_page(const EditorModel& model) {
    const bool wide = m_w >= kWideForm;
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
        case Page::Players:
            wide ? layout_players_table(model) : layout_players(model);
            break;
        case Page::PlayerName:
            layout_player_name(model);
            break;
        case Page::Relations:
            wide ? layout_relations_table(model) : layout_relations(model);
            break;
        case Page::Rules:
            wide ? layout_settings_grid(model, true) : layout_rules(model);
            break;
        case Page::Random:
            wide ? layout_settings_grid(model, false) : layout_random(model);
            break;
    }
}

void EditorPanel::layout_document(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 4;
    f.label(Rect{m_x, y, m_w, 24}, ui::tr("editor.title"), Align::Left, Tone::Heading);
    y += 26;
    std::string file = model.file.empty() ? ui::tr("editor.untitled") : model.file;
    if (model.modified) {
        file += " *";
    }
    f.label(Rect{m_x, y, m_w, kLine}, file);
    y += kLine;
    f.entry(Rect{m_x, y, m_w, kLine}, ui::tr("editor.map_size"),
            ui::tr("editor.size", {count(model.width), count(model.height)}), Tone::Dim);
    y += kLine;
    if (model.planned_islands > 0) {
        f.entry(Rect{m_x, y, m_w, kLine}, ui::tr("editor.islands"),
                ui::tr("editor.islands_planned", {count(model.planned_islands)}), Tone::Dim);
    } else {
        f.entry(Rect{m_x, y, m_w, kLine}, ui::tr("editor.islands"), count(model.islands), Tone::Dim);
    }
    y += kLine;
    f.entry(Rect{m_x, y, m_w, kLine}, ui::tr("editor.players"), count(model.players), Tone::Dim);
    y += kLine + 4;
    // What the files, the pages of the whole map and the tools are: the menu bar above the map and
    // the tool bar left of it (EditorChrome) have them, and each its key.
    const std::int32_t bottom = m_top + m_height - 4;
    f.paragraph(Rect{m_x, y, m_w, std::max(0, std::min(5 * 20, bottom - y))}, ui::tr("editor.document.hint"), Tone::Dim,
                20, ui::tr("editor.document.hint_short"));
    y += 5 * 20 + kGap;
    if (!model.notice.empty() && bottom - y - 2 >= 2 * 20) {
        f.paragraph(Rect{m_x, y + 2, m_w, std::max(0, bottom - y - 2)}, model.notice,
                    model.notice_is_error ? Tone::Warning : Tone::Dim, 20);
    }
}

void EditorPanel::layout_new(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.new.title"), Align::Left, Tone::Heading);
    y += 28;
    const auto value = [&](Id id, const char* label, std::string text, bool enabled = true) {
        f.label(Rect{m_x, y, m_w, kLine}, ui::tr(label), Align::Left, enabled ? Tone::Normal : Tone::Dim);
        y += kLine;
        f.cycle_button(id, Rect{m_x, y, m_w, kButtonH}, std::move(text), enabled);
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
    f.entry(Rect{m_x, y, m_w, kLine}, ui::tr("editor.new.seed"), std::to_string(m_new.seed),
            islands ? Tone::Normal : Tone::Dim, Tone::Dim);
    y += kLine + 2;
    f.button(kReroll, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.new.reroll"), islands);
    y += kButtonH + kGap + 4;
    const std::int32_t half = (m_w - kGap) / 2;
    f.button(kCreate, Rect{m_x, y, half, kButtonH}, ui::tr("editor.new.create"));
    f.button(kCancel, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.cancel"));
    y += kButtonH + kGap;
    // The note where it fits: in a small window the settings and the buttons take the room.
    const std::int32_t room = std::min(kNote, m_top + m_height - 4 - y);
    if (room >= 2 * 20) {
        f.paragraph(Rect{m_x, y, m_w, room}, ui::tr(islands ? "editor.new.note_islands" : "editor.new.note_sea"),
                    Tone::Dim, 20);
    }
}

void EditorPanel::layout_open(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    const bool islands = m_page == Page::IslandFiles;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr(islands ? "editor.islands.load_title" : "editor.open.title"), Align::Left,
            Tone::Heading);
    y += 32;
    if (model.files.empty()) {
        f.paragraph(Rect{m_x, y, m_w, 60}, ui::tr(islands ? "editor.islands.no_files" : "editor.open.empty"), Tone::Dim,
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
                f.label(Rect{m_x, y, m_w, kRowH}, group, Align::Left, Tone::Heading);
                y += kRowH;
            }
            const auto row = static_cast<Id>(i - m_first);
            f.row(kFirstRow + row, Rect{m_x, y, m_w, kRowH - 2}, false);
            f.label(Rect{m_x + 4, y, m_w - 8, kRowH - 2}, file.name);
            y += kRowH;
            ++shown;
        }
        y += 4;
        const std::int32_t half = (m_w - kGap) / 2;
        f.button(kPrevious, Rect{m_x, y, half, kButtonH}, ui::tr("editor.previous"), m_first > 0);
        f.button(kNext, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.next"), m_first + kRows < n);
        y += kButtonH + kGap;
    }
    f.button(islands ? kIslandPage : kCancel, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_save(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.save.title"), Align::Left, Tone::Heading);
    y += 34;
    f.label(Rect{m_x, y, m_w, kLine}, ui::tr("editor.save.name"));
    y += kLine;
    f.field(kField, Rect{m_x, y, m_w, 32}, m_text);
    y += 38;
    const std::string name = clean_name(m_text);
    const bool taken = !name.empty() && name_taken(model, name);
    f.paragraph(Rect{m_x, y, m_w, kNote}, ui::tr(taken ? "editor.save.taken" : "editor.save.where"),
                taken ? Tone::Warning : Tone::Dim, 20);
    y += kNote + 4;
    const std::int32_t half = (m_w - kGap) / 2;
    f.button(kStore, Rect{m_x, y, half, kButtonH}, ui::tr(taken ? "editor.save.replace" : "editor.save"),
             !name.empty());
    f.button(kCancel, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_selection(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.selection.title"), Align::Left, Tone::Heading);
    y += 30;
    if (model.selection.empty()) {
        f.paragraph(Rect{m_x, y, m_w, 5 * 20}, ui::tr("editor.selection.none"), Tone::Dim, 20);
        y += 5 * 20 + 4;
    } else {
        f.paragraph(Rect{m_x, y, m_w, 2 * kLine}, model.selection, Tone::Normal, kLine);
        y += 2 * kLine + 4;
        // Room for the buttons below: back, and an island's three rows or a city's two.
        const std::int32_t rows = model.island || model.placeholder ? 4 : model.city ? 3 : 1;
        const std::int32_t bottom = m_top + m_height - rows * (kButtonH + kGap) - kGap;
        for (const EditorProperty& p : model.properties) {
            if (y + kLine > bottom) {
                break;
            }
            f.entry(Rect{m_x, y, m_w, kLine}, p.label, p.value, Tone::Dim);
            y += kLine;
        }
        y += kGap;
        if (model.island) {
            layout_island_actions(model, y);
        } else if (model.placeholder) {
            layout_placeholder_actions(model, y);
        } else if (model.city) {
            const std::int32_t half = (m_w - kGap) / 2;
            f.button(kCityNamePage, Rect{m_x, y, half, kButtonH}, ui::tr("editor.city.rename"));
            f.button(kCityGoodsPage, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.city.stock"),
                     model.city->room > 0);
            y += kButtonH + kGap;
            f.cycle_button(kCityOwner, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.city.owner", {model.city->owner}));
            y += kButtonH + kGap;
        }
    }
    f.button(kBack, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_city_name(const EditorModel& model) {
    if (!model.city) {
        m_page = Page::Selection;  // the city is gone (undone)
        layout_selection(model);
        return;
    }
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.city.rename_title"), Align::Left, Tone::Heading);
    y += 34;
    f.label(Rect{m_x, y, m_w, kLine}, ui::tr("editor.city.name"));
    y += kLine;
    f.field(kField, Rect{m_x, y, m_w, 32}, m_text);
    y += 38;
    const bool valid = sim::valid_name(m_text, sim::CityOrder::kMaxNameLength);
    const std::int32_t half = (m_w - kGap) / 2;
    f.button(kStore, Rect{m_x, y, half, kButtonH}, ui::tr("editor.city.rename_ok"), valid);
    f.button(kCitySelection, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_players(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.players.title"), Align::Left, Tone::Heading);
    y += 28;
    const std::size_t n = model.player_slots.size();
    const EditorPlayer* p = n > 0 ? &model.player_slots[std::min(m_player, n - 1)] : nullptr;
    const bool on = p != nullptr;
    const Tone tone = on ? Tone::Normal : Tone::Dim;
    const std::int32_t half = (m_w - kGap) / 2;
    const std::int32_t right = m_x + half + kGap;
    const std::int32_t rest = m_w - half - kGap;
    f.label(Rect{m_x, y, m_w, 18},
            on ? ui::tr("editor.players.slot", {count(std::min(m_player, n - 1) + 1), count(n)})
               : ui::tr("editor.players.slot", {"-", "-"}),
            Align::Left, tone);
    y += 18;
    f.cycle_button(kPlayerSlot, Rect{m_x, y, m_w, kButtonH}, on ? p->name : ui::tr("editor.objects.no_owner"), on);
    y += kButtonH + 2;
    // Two values side by side: a label over each half and its button below.
    const auto pair = [&](const char* left_label, Id left, std::string left_text, const char* right_label, Id right_id,
                          std::string right_text) {
        f.label(Rect{m_x, y, half, 18}, ui::tr(left_label), Align::Left, tone);
        f.label(Rect{right, y, rest, 18}, ui::tr(right_label), Align::Left, tone);
        y += 18;
        f.cycle_button(left, Rect{m_x, y, half, kButtonH}, std::move(left_text), on);
        f.cycle_button(right_id, Rect{right, y, rest, kButtonH}, std::move(right_text), on);
        y += kButtonH + 2;
    };
    pair("editor.players.role", kPlayerRole,
         ui::tr(on && p->human ? "editor.players.human" : "editor.players.computer"), "editor.players.colour",
         kPlayerColour, on ? p->colour : std::string("-"));
    f.label(Rect{m_x, y, m_w, 18}, ui::tr("editor.players.money"), Align::Left, tone);
    y += 18;
    f.cycle_button(kPlayerMoney, Rect{m_x, y, m_w, kButtonH},
                   on ? ui::tr("editor.players.money_value", {std::to_string(p->money)}) : std::string("-"), on);
    y += kButtonH + 2;
    pair("editor.players.monuments", kPlayerMonuments, on ? count(p->monuments_allowed) : "-", "editor.players.arches",
         kPlayerArches, on ? count(p->arches_allowed) : "-");
    y += kGap - 2;
    f.button(kPlayerNamePage, Rect{m_x, y, half, kButtonH}, ui::tr("editor.players.rename"), on);
    f.button(kRelationsPage, Rect{right, y, rest, kButtonH}, ui::tr("editor.players.relations"), n > 1);
    y += kButtonH + kGap;
    f.button(kPlayerAdd, Rect{m_x, y, half, kButtonH}, ui::tr("editor.players.add"), model.can_add_player);
    f.button(kPlayerRemove, Rect{right, y, rest, kButtonH}, ui::tr("editor.players.remove"), model.can_remove_player);
    y += kButtonH + kGap;
    note_and_back(model, y, "editor.players.note", "editor.players.note_short", kBack, "editor.back");
}

void EditorPanel::note_and_back(const EditorModel& model, std::int32_t y, const char* note_key, const char* short_key,
                                Id back, const char* back_key) {
    // The way back stays at the foot of the panel; the note takes the lines that fit above it.
    ui::Form& f = m_form;
    const std::int32_t foot = std::max(y, m_top + m_height - 4 - kButtonH);
    const std::int32_t lines = std::clamp((foot - 4 - y) / 20, 0, 4);
    if (lines > 0) {
        // Where the whole note does not fit, its first sentence alone: none is cut off.
        const bool notice = !model.notice.empty();
        const std::string note = notice ? model.notice : ui::tr(note_key);
        f.paragraph(Rect{m_x, y, m_w, lines * 20}, note, notice && model.notice_is_error ? Tone::Warning : Tone::Dim,
                    20, notice ? std::string() : ui::tr(short_key));
    }
    f.button(back, Rect{m_x, foot, m_w, kButtonH}, ui::tr(back_key));
}

void EditorPanel::layout_player_name(const EditorModel& model) {
    if (model.player_slots.empty()) {
        m_page = Page::Players;
        layout_players(model);
        return;
    }
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.players.rename_title"), Align::Left, Tone::Heading);
    y += 34;
    f.label(Rect{m_x, y, m_w, kLine}, ui::tr("editor.players.name"));
    y += kLine;
    f.field(kField, Rect{m_x, y, m_w, 32}, m_text);
    y += 38;
    f.paragraph(Rect{m_x, y, m_w, 2 * 20}, ui::tr("editor.players.name_note"), Tone::Dim, 20);
    y += 2 * 20 + 4;
    const bool valid = m_text.empty() || sim::valid_name(m_text, sim::CityOrder::kMaxNameLength);
    const std::int32_t half = (m_w - kGap) / 2;
    f.button(kStore, Rect{m_x, y, half, kButtonH}, ui::tr("editor.city.rename_ok"), valid);
    f.button(kPlayersBack, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_relations(const EditorModel& model) {
    const std::size_t n = model.player_slots.size();
    if (n < 2) {
        m_page = Page::Players;
        layout_players(model);
        return;
    }
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.relations.title"), Align::Left, Tone::Heading);
    y += 28;
    const std::size_t a = std::min(m_player, n - 1);
    if (m_partner >= n || m_partner == a) {
        m_partner = (a + 1) % n;
    }
    const EditorPlayer& p = model.player_slots[a];
    const EditorPlayer& q = model.player_slots[m_partner];
    const EditorPlayer::Standing s = m_partner < p.relations.size() ? p.relations[m_partner] : EditorPlayer::Standing{};
    f.label(Rect{m_x, y, m_w, 18}, ui::tr("editor.relations.of"), Align::Left, Tone::Normal);
    y += 18;
    f.label(Rect{m_x, y, m_w, kButtonH}, p.name, Align::Left, Tone::Normal);
    y += kButtonH + 2;
    const auto value = [&](Id id, const std::string& label, std::string text, bool enabled) {
        f.label(Rect{m_x, y, m_w, 18}, label, Align::Left, enabled ? Tone::Normal : Tone::Dim);
        y += 18;
        f.cycle_button(id, Rect{m_x, y, m_w, kButtonH}, std::move(text), enabled);
        y += kButtonH + 2;
    };
    value(kRelationPartner, ui::tr("editor.relations.with"), q.name, true);
    value(kRelationPeace, ui::tr("editor.relations.standing"),
          ui::tr(s.peace ? "editor.relations.peace" : "editor.relations.war"), true);
    value(kRelationTrade, ui::tr("editor.relations.trade"),
          ui::tr(s.trade ? "editor.relations.trade_yes" : "editor.relations.trade_no"), s.peace);
    y += kGap;
    note_and_back(model, y, "editor.relations.note", "editor.relations.note", kPlayersBack, "editor.relations.back");
}

void EditorPanel::layout_rules(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.rules.title"), Align::Left, Tone::Heading);
    y += 28;
    f.cycle_button(kRulesOwn, Rect{m_x, y, m_w, kButtonH},
                   ui::tr(model.rules_own ? "editor.rules.own" : "editor.rules.players"));
    y += kButtonH + kGap;
    const std::int32_t half = (m_w - kGap) / 2;
    f.button(kRulesOriginal, Rect{m_x, y, half, kButtonH}, ui::tr("editor.rules.original"), true,
             model.rules_own && model.rules_original);
    f.button(kRulesOpen1602, Rect{m_x + half + kGap, y, m_w - half - kGap, kButtonH}, ui::tr("editor.rules.open1602"));
    y += kButtonH + kGap;
    f.button(kRandomPage, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.random.edit"), model.has_plan);
    y += kButtonH + kGap;
    // As many settings as fit above the pages' buttons and the way back.
    constexpr std::int32_t kRuleH = 18 + kButtonH + 2;
    const std::int32_t bottom = m_top + m_height - 4 - 2 * (kButtonH + kGap);
    const std::size_t n = model.rules.size();
    const auto fit = static_cast<std::size_t>(std::max<std::int32_t>(1, (bottom - y) / kRuleH));
    m_page_rows = std::min<std::size_t>(fit, 20);
    // Pages of whole rows: the last shows what is left, nothing twice.
    m_first = n == 0 ? 0 : std::min(m_first, (n - 1) / m_page_rows * m_page_rows);
    for (std::size_t row = 0; row < m_page_rows && m_first + row < n; ++row) {
        const EditorRule& r = model.rules[m_first + row];
        const bool on = model.rules_own && r.enabled;
        f.label(Rect{m_x, y, m_w, 18}, r.label, Align::Left, on ? Tone::Normal : Tone::Dim);
        y += 18;
        f.cycle_button(kFirstRow + static_cast<Id>(row), Rect{m_x, y, m_w, kButtonH}, r.value, on);
        y += kButtonH + 2;
    }
    y = std::max(y + 4, bottom);
    f.button(kPrevious, Rect{m_x, y, half, kButtonH}, ui::tr("editor.previous"), m_first > 0);
    f.button(kNext, Rect{m_x + half + kGap, y, m_w - half - kGap, kButtonH}, ui::tr("editor.next"),
             m_first + m_page_rows < n);
    y += kButtonH + kGap;
    f.button(kBack, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_random(const EditorModel& model) {
    if (!model.has_plan) {
        m_page = Page::Rules;
        layout_rules(model);
        return;
    }
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.random.title"), Align::Left, Tone::Heading);
    y += 28;
    f.paragraph(Rect{m_x, y, m_w, 2 * 20}, ui::tr("editor.random.note"), Tone::Dim, 20,
                ui::tr("editor.random.note_short"));
    y += 2 * 20 + 4;
    const std::int32_t half = (m_w - kGap) / 2;
    constexpr std::int32_t kEntryH = 18 + kButtonH + 2;
    const std::int32_t bottom = m_top + m_height - 4 - 3 * (kButtonH + kGap);
    const std::size_t n = model.randoms.size();
    const auto fit = static_cast<std::size_t>(std::max<std::int32_t>(1, (bottom - y) / kEntryH));
    m_page_rows = std::min<std::size_t>(fit, 20);
    m_first = n == 0 ? 0 : std::min(m_first, (n - 1) / m_page_rows * m_page_rows);
    for (std::size_t row = 0; row < m_page_rows && m_first + row < n; ++row) {
        const EditorRandom& r = model.randoms[m_first + row];
        f.label(Rect{m_x, y, m_w, 18}, r.label);
        y += 18;
        f.cycle_button(kFirstRow + static_cast<Id>(row), Rect{m_x, y, m_w, kButtonH},
                       ui::tr("editor.random.value", {count(r.random), count(r.fixed)}));
        y += kButtonH + 2;
    }
    y = std::max(y + 4, bottom);
    f.button(kPrevious, Rect{m_x, y, half, kButtonH}, ui::tr("editor.previous"), m_first > 0);
    f.button(kNext, Rect{m_x + half + kGap, y, m_w - half - kGap, kButtonH}, ui::tr("editor.next"),
             m_first + m_page_rows < n);
    y += kButtonH + kGap;
    f.button(kRandomRoll, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.random.roll"));
    y += kButtonH + kGap;
    f.button(kRandomBack, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.random.back"));
}

void EditorPanel::layout_preview(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.preview.title"), Align::Left, Tone::Heading);
    y += 28;
    f.label(Rect{m_x, y, m_w, kLine},
            ui::tr("editor.preview.roll", {count(model.preview_roll), count(model.preview_islands)}));
    y += kLine + 4;
    f.paragraph(Rect{m_x, y, m_w, kNote}, ui::tr("editor.preview.note"), Tone::Dim, 20,
                ui::tr("editor.preview.note_short"));
    y += kNote + kGap;
    f.button(kRandomRoll, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.preview.again"));
    y += kButtonH + kGap;
    f.button(kPreviewEnd, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.preview.back"));
}

void EditorPanel::layout_city_goods(const EditorModel& model) {
    if (!model.city || model.city->room == 0) {
        m_page = Page::Selection;
        layout_selection(model);
        return;
    }
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.city.stock_title", {model.city->name}), Align::Left, Tone::Heading);
    y += 28;
    f.paragraph(Rect{m_x, y, m_w, 3 * 20}, ui::tr("editor.city.room", {count(model.city->room / 32)}), Tone::Dim, 20);
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
        f.label(Rect{m_x, y, m_w - 2 * kStep - kAmount - 4, kGoodH - 2}, g.name);
        f.label(Rect{m_x + m_w - 2 * kStep - kAmount - 4, y, kAmount, kGoodH - 2},
                ui::tr("editor.city.tons", {count(g.amount / 32)}), Align::Right);
        // Cycle buttons: the right button takes a ton.
        f.cycle_button(kFirstGoodLess + id, Rect{m_x + m_w - 2 * kStep - 2, y, kStep, kGoodH - 2}, "-", g.amount > 0);
        f.cycle_button(kFirstGoodMore + id, Rect{m_x + m_w - kStep, y, kStep, kGoodH - 2}, "+",
                       g.amount < model.city->room);
        y += kGoodH;
    }
    y += 4;
    const std::int32_t half = (m_w - kGap) / 2;
    f.button(kPrevious, Rect{m_x, y, half, kButtonH}, ui::tr("editor.previous"), m_first > 0);
    f.button(kNext, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.next"), m_first + m_page_rows < n);
    y += kButtonH + kGap;
    f.button(kCitySelection, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.city.back"));
}

void EditorPanel::layout_island_actions(const EditorModel& model, std::int32_t& y) {
    ui::Form& f = m_form;
    const std::int32_t half = (m_w - kGap) / 2;
    if (model.placing) {
        f.paragraph(Rect{m_x, y, m_w, 3 * 20}, ui::tr("editor.islands.placing"), Tone::Dim, 20);
        y += 3 * 20 + 4;
        f.button(kStopPlacing, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.cancel"));
        y += kButtonH + kGap;
        return;
    }
    f.button(kMove, Rect{m_x, y, half, kButtonH}, ui::tr("editor.islands.move"));
    f.button(kRotate, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.islands.rotate"), model.island_free);
    y += kButtonH + kGap;
    f.button(kCopy, Rect{m_x, y, half, kButtonH}, ui::tr("editor.islands.copy"), model.island_free);
    f.button(kRemove, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.islands.remove"), model.island_free);
    y += kButtonH + kGap;
    f.button(kFertilityPage, Rect{m_x, y, half, kButtonH}, ui::tr("editor.fertility.edit"));
    f.button(kSaveIsland, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.islands.save"), model.island_free);
    y += kButtonH + kGap;
}

void EditorPanel::layout_placeholder_actions(const EditorModel& model, std::int32_t& y) {
    ui::Form& f = m_form;
    const std::int32_t half = (m_w - kGap) / 2;
    if (model.placing) {
        f.paragraph(Rect{m_x, y, m_w, 3 * 20}, ui::tr("editor.placeholder.placing"), Tone::Dim, 20);
        y += 3 * 20 + 4;
        f.button(kStopPlacing, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.cancel"));
        y += kButtonH + kGap;
        return;
    }
    f.button(kMove, Rect{m_x, y, half, kButtonH}, ui::tr("editor.islands.move"));
    f.button(kRemove, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.islands.remove"));
    y += kButtonH + kGap;
    f.cycle_button(kPlaceholderClimate, Rect{m_x, y, m_w, kButtonH},
                   ui::tr(kPlaceholderClimateTexts[model.placeholder_climate % kPlaceholderClimateTexts.size()]));
    y += kButtonH + kGap;
    f.cycle_button(kPlaceholderSize, Rect{m_x, y, m_w, kButtonH},
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
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.fertility.title"), Align::Left, Tone::Heading);
    y += 28;
    f.label(Rect{m_x, y, m_w, kLine}, model.selection);
    y += kLine + 4;
    for (std::size_t i = 0; i < kCropTexts.size(); ++i) {
        std::string text = ui::tr(kCropTexts[i]);
        const bool own = (model.island_fertility >> (kCropWares[i] - assets::ware::kCornField) & 1U) != 0;
        if (!own && (model.island_crops >> (kCropWares[i] - assets::ware::kCornField) & 1U) != 0) {
            text = ui::tr("editor.fertility.half", {text});
        }
        f.check(kFirstCrop + static_cast<Id>(i), Rect{m_x, y, m_w, kButtonH}, std::move(text), own);
        y += kButtonH + kGap;
    }
    constexpr std::int32_t kFertilityNote = 4 * 20;
    f.paragraph(Rect{m_x, y, m_w, kFertilityNote}, ui::tr("editor.fertility.note"), Tone::Dim, 20);
    y += kFertilityNote + 4;
    f.button(kSelectionPage, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.fertility.back"));
}

void EditorPanel::layout_terrain(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    const EditorTerrain& t = model.terrain;
    using Tool = EditorTerrain::Tool;
    // The tool chosen in the tool bar is the heading; its settings below it.
    f.label(Rect{m_x, y, m_w, 28},
            ui::tr(kTerrainToolTitles[static_cast<std::size_t>(t.tool) % kTerrainToolTitles.size()]), Align::Left,
            Tone::Heading);
    y += 28;
    // Each setting's name above its value, which takes the panel's width.
    const auto value = [&](Id id, const char* label, std::string text, bool enabled) {
        f.label(Rect{m_x, y, m_w, 18}, ui::tr(label), Align::Left, enabled ? Tone::Normal : Tone::Dim);
        y += 18;
        f.cycle_button(id, Rect{m_x, y, m_w, kButtonH}, std::move(text), enabled);
        y += kButtonH + 2;
    };
    std::string note = ui::tr(kTerrainToolNotes[static_cast<std::size_t>(t.tool)]);
    if (t.tool == Tool::Select || t.tool == Tool::Paste) {
        // The copy: what it holds, and copying, turning and mirroring it, in the place of the
        // four settings of the other tools.
        const bool copied = model.clip_width > 0 && model.clip_height > 0;
        f.label(Rect{m_x, y, m_w, 18},
                copied ? ui::tr("editor.terrain.copy_size", {count(model.clip_width), count(model.clip_height)})
                       : ui::tr("editor.terrain.no_copy"),
                Align::Left, copied ? Tone::Normal : Tone::Dim);
        y += 18;
        f.button(kTerrainCopy, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.terrain.copy"), model.terrain_selected);
        y += kButtonH + 4;
        f.button(kTerrainTurn, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.terrain.turn"), copied);
        y += kButtonH + 4;
        f.button(kTerrainMirror, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.terrain.mirror"), copied);
        y += kButtonH + 4;
        y += 4 * (18 + kButtonH + 2) - (18 + 3 * (kButtonH + 4));  // as tall as the four settings
    } else {
        const bool shaping = t.tool == Tool::Land || t.tool == Tool::Water || t.tool == Tool::Coast;
        const bool single = model.terrain_width == 1 && model.terrain_height == 1;
        const bool pieces = model.terrain_pieces > 0 && t.tool != Tool::Random && t.tool != Tool::Pipette && !shaping;
        // Only what the tool uses: the kind and piece for the tools that lay pieces.
        if (!shaping && t.tool != Tool::Pipette) {
            value(kTerrainGroup, "editor.terrain.group",
                  ui::tr(kTerrainGroupTexts[t.group % kTerrainGroupTexts.size()]), true);
        }
        if (t.tool == Tool::Brush || t.tool == Tool::Fill || t.tool == Tool::Replace) {
            std::string piece = ui::tr("editor.terrain.no_piece");
            if (model.terrain_pieces > 0) {
                piece = ui::tr("editor.terrain.piece_value",
                               {count(model.terrain_id), count(t.piece + 1), count(model.terrain_pieces)});
            }
            value(kTerrainPiece, "editor.terrain.piece", std::move(piece), pieces);
            value(kTerrainRotation, "editor.terrain.rotation", ui::tr(kTerrainRotationTexts[t.rotation & 3U]),
                  pieces && model.terrain_turns);
        }
        // The brush: its size (smaller, slid, bigger) and its shape.
        if (t.tool == Tool::Random || shaping || t.tool == Tool::Brush) {
            const bool brush = t.tool != Tool::Brush || single;
            const std::size_t at = t.brush % kTerrainBrushes.size();
            const std::uint32_t side = kTerrainBrushes[at];
            const Tone tone = brush ? Tone::Normal : Tone::Dim;
            f.label(Rect{m_x, y, m_w, 18}, ui::tr("editor.terrain.brush"), Align::Left, tone);
            f.label(Rect{m_x, y, m_w, 18}, ui::tr("editor.size", {count(side), count(side)}), Align::Right, tone);
            y += 18;
            constexpr std::int32_t kStep = 30;
            f.button(kTerrainSmaller, Rect{m_x, y, kStep, kButtonH}, "-", brush && at > 0);
            f.slider(kTerrainSlider, Rect{m_x + kStep + 6, y, m_w - 2 * (kStep + 6), kButtonH},
                     static_cast<double>(at) / static_cast<double>(kTerrainBrushes.size() - 1), brush);
            f.button(kTerrainBigger, Rect{m_x + m_w - kStep, y, kStep, kButtonH}, "+",
                     brush && at + 1 < kTerrainBrushes.size());
            y += kButtonH + 2;
            if (t.tool != Tool::Brush || single) {
                value(kTerrainShape, "editor.terrain.shape",
                      ui::tr(kBrushShapeTexts[t.shape % kBrushShapeTexts.size()]), brush);
            }
        }
        if (pieces && !single) {
            note = ui::tr("editor.terrain.note.large", {count(model.terrain_width), count(model.terrain_height)});
        }
    }
    // The tool's note, or in its place the notice of the last stroke (as tall as the panel is at
    // 1024 x 768: the back button stays below it).
    // The tools with few settings have room for a longer note (the smaller screens' font is wider).
    const bool few = t.tool == Tool::Land || t.tool == Tool::Water || t.tool == Tool::Coast || t.tool == Tool::Random ||
                     t.tool == Tool::Pipette;
    const std::int32_t note_h = (few ? 6 : 4) * 20;
    if (!model.notice.empty()) {
        f.paragraph(Rect{m_x, y, m_w, note_h}, model.notice, model.notice_is_error ? Tone::Warning : Tone::Dim, 20);
    } else {
        f.paragraph(Rect{m_x, y, m_w, note_h}, note, Tone::Dim, 20);
    }
    y += note_h + 4;
    f.button(kBack, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_objects(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    const EditorObjects& o = model.objects;
    // The tool chosen in the tool bar is the heading; its settings below it.
    f.label(Rect{m_x, y, m_w, 28},
            ui::tr(kObjectToolTitles[static_cast<std::size_t>(o.tool) % kObjectToolTitles.size()]), Align::Left,
            Tone::Heading);
    y += 28;
    using Tool = EditorObjects::Tool;
    const bool build = o.tool == Tool::Build;
    // Ships and land units: the piece is their type, the rotation the way they face.
    const bool figure =
        o.tool == Tool::Template || o.tool == Tool::Ship || o.tool == Tool::Unit || o.tool == Tool::Resource;
    // What is placed for an owner, turned.
    const bool owned = build || o.tool == Tool::Template || o.tool == Tool::Ship || o.tool == Tool::Unit;
    const std::size_t chosen = o.tool == Tool::Template   ? o.stamp
                               : o.tool == Tool::Ship     ? o.ship
                               : o.tool == Tool::Unit     ? o.unit
                               : o.tool == Tool::Resource ? o.resource
                                                          : o.piece;
    const auto value = [&](Id id, const std::string& label, std::string text, bool enabled) {
        f.label(Rect{m_x, y, m_w, 18}, label, Align::Left, enabled ? Tone::Normal : Tone::Dim);
        y += 18;
        f.cycle_button(id, Rect{m_x, y, m_w, kButtonH}, std::move(text), enabled);
        y += kButtonH + 2;
    };
    value(kObjectsGroup, ui::tr("editor.objects.group"), model.object_group, build && model.object_groups > 0);
    // A name is long: its number goes on the label above it.
    const bool pieces = (build || figure) && model.object_pieces > 0;
    std::string piece = ui::tr("editor.terrain.no_piece");
    const char* piece_label = kObjectPieceLabels[static_cast<std::size_t>(o.tool)];
    std::string label = ui::tr(piece_label, {"-", "-"});
    if (pieces) {
        piece = model.object_name;
        label = ui::tr(piece_label, {count(std::min(chosen, model.object_pieces - 1) + 1), count(model.object_pieces)});
    }
    value(kObjectsPiece, label, std::move(piece), pieces);
    value(kObjectsRotation, ui::tr("editor.objects.rotation"), ui::tr(kTerrainRotationTexts[o.rotation & 3U]),
          (build && model.object_turns) || (owned && !build));
    value(kObjectsOwner, ui::tr("editor.objects.owner"),
          model.object_owners > 0 ? model.object_owner : ui::tr("editor.objects.no_owner"),
          owned && model.object_owners > 0);
    std::string note = ui::tr(kObjectToolNotes[static_cast<std::size_t>(o.tool)]);
    if (owned && model.object_owners == 0) {
        note = ui::tr("editor.objects.note.no_players");
    } else if (o.tool == Tool::Template && model.object_pieces == 0) {
        note = ui::tr("editor.objects.note.no_templates");
    }
    // The objects placed lately, one to a row (their names are long), as far as room is left
    // above the note and "Back".
    const std::int32_t bottom = m_top + m_height - 4;
    constexpr std::int32_t kRecentH = 26;
    const std::size_t recent = std::min(model.recent_objects.size(), kEditorRecent);
    if (recent > 0) {
        const std::int32_t room = bottom - y - (kButtonH + 4) - 3 * 20 - 18;
        const std::size_t rows = std::min(recent, static_cast<std::size_t>(std::max(0, room / kRecentH)));
        if (rows > 0) {
            f.label(Rect{m_x, y, m_w, 18}, ui::tr("editor.objects.recent"), Align::Left, Tone::Normal);
            y += 18;
            for (std::size_t i = 0; i < rows; ++i) {
                f.button(kRecent + static_cast<Id>(i),
                         Rect{m_x, y + static_cast<std::int32_t>(i) * kRecentH, m_w, kRecentH - 2},
                         model.recent_objects[i].name);
            }
            y += static_cast<std::int32_t>(rows) * kRecentH + 2;
        }
    }
    const std::int32_t kObjectsNote = std::clamp(bottom - y - (kButtonH + 4), 3 * 20, 4 * 20);
    if (!model.object_hover.empty()) {
        // Why a click where the mouse is would not place, in full (the status line is short).
        f.paragraph(Rect{m_x, y, m_w, kObjectsNote}, model.object_hover, Tone::Warning, 20);
    } else if (!model.notice.empty()) {
        f.paragraph(Rect{m_x, y, m_w, kObjectsNote}, model.notice, model.notice_is_error ? Tone::Warning : Tone::Dim,
                    20);
    } else {
        f.paragraph(Rect{m_x, y, m_w, kObjectsNote}, note, Tone::Dim, 20);
    }
    y += kObjectsNote + 4;
    f.button(kBack, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_islands(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.islands.title"), Align::Left, Tone::Heading);
    y += 28;
    const auto value = [&](Id id, const char* label, std::string text) {
        f.label(Rect{m_x, y, m_w, kLine}, ui::tr(label));
        y += kLine;
        f.cycle_button(id, Rect{m_x, y, m_w, kButtonH}, std::move(text));
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
    f.button(kOtherIsland, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.islands.other"),
             !model.placing && !m_island.placeholder);
    y += kButtonH + kGap;
    if (!m_island.own_shape && !m_island.placeholder && !model.island_template.empty()) {
        f.label(Rect{m_x, y, m_w, kLine}, model.island_template, Align::Left, Tone::Dim);
        y += kLine + 2;
    }
    f.button(kPlace, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.islands.place"), !model.placing);
    y += kButtonH + kGap;
    f.button(kLoadIsland, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.islands.load"), !model.placing);
    y += kButtonH + kGap;
    // The note where its three lines fit above the buttons below: a sentence cut reads wrong.
    const std::int32_t below = (model.placing ? 2 : 1) * (kButtonH + kGap) + 4;
    constexpr std::int32_t note = 3 * 20;
    if (m_top + m_height - 4 - below - y >= note) {
        f.paragraph(Rect{m_x, y, m_w, note},
                    ui::tr(!model.placing         ? "editor.islands.note"
                           : m_island.placeholder ? "editor.placeholder.placing"
                                                  : "editor.islands.placing"),
                    Tone::Dim, 20);
        y += note + 4;
    }
    if (model.placing) {
        f.button(kStopPlacing, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.cancel"));
        y += kButtonH + kGap;
    }
    f.button(kBack, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_players_table(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    const std::size_t n = model.player_slots.size();
    // The heading, and the dialog's two tabs at its right.
    constexpr std::int32_t kTab = 150;
    f.label(Rect{m_x, y, m_w - 2 * (kTab + kGap), 28}, ui::tr("editor.players.title"), Align::Left, Tone::Heading);
    f.button(kTabPlayers, Rect{m_x + m_w - 2 * kTab - kGap, y, kTab, kButtonH}, ui::tr("editor.players.tab_players"),
             true, true);
    f.button(kTabRelations, Rect{m_x + m_w - kTab, y, kTab, kButtonH}, ui::tr("editor.players.tab_relations"), n > 1);
    y += kButtonH + kGap + 4;
    // Every slot in a row: its number, then a button for each of its values.
    constexpr std::int32_t kNumber = 40;
    constexpr std::array<std::int32_t, 6> kShares = {30, 12, 16, 16, 13, 13};  // percent of the room
    constexpr std::array<const char*, 6> kHeads = {"editor.players.name",      "editor.players.role",
                                                   "editor.players.colour",    "editor.players.money",
                                                   "editor.players.monuments", "editor.players.arches"};
    const std::int32_t room = m_w - kNumber - 6 * kGap;
    std::array<std::int32_t, 7> x{};
    x[0] = m_x + kNumber + kGap;
    for (std::size_t c = 0; c < kShares.size(); ++c) {
        x[c + 1] = x[c] + room * kShares[c] / 100 + kGap;
    }
    for (std::size_t c = 0; c < kHeads.size(); ++c) {
        f.label(Rect{x[c], y, x[c + 1] - x[c] - kGap, 18}, ui::tr(kHeads[c]), Align::Left, Tone::Dim);
    }
    y += 20;
    // As many rows as fit above the note and the foot's buttons; the others on pages.
    constexpr std::int32_t kRow = kButtonH + 2;
    const std::int32_t foot = m_top + m_height - kButtonH - 4;
    const std::int32_t bottom = foot - 2 * 20 - kGap;
    m_page_rows = std::clamp<std::size_t>(static_cast<std::size_t>(std::max(1, (bottom - y) / kRow)), 1, kTableRows);
    m_first = n == 0 ? 0 : std::min(m_first, (n - 1) / m_page_rows * m_page_rows);
    for (std::size_t row = 0; row < m_page_rows && m_first + row < n; ++row) {
        const std::size_t i = m_first + row;
        const EditorPlayer& p = model.player_slots[i];
        const Id base = kPlayerTable + static_cast<Id>(row) * 8;
        const auto cell = [&](std::size_t c) { return Rect{x[c], y, x[c + 1] - x[c] - kGap, kButtonH}; };
        f.label(Rect{m_x, y, kNumber, kButtonH}, count(i + 1), Align::Right, Tone::Dim);
        f.button(base, cell(0), p.name, true, i == m_player);
        f.cycle_button(base + 1, cell(1), ui::tr(p.human ? "editor.players.human" : "editor.players.computer"));
        f.cycle_button(base + 2, cell(2), p.colour);
        f.cycle_button(base + 3, cell(3), ui::tr("editor.players.money_value", {std::to_string(p.money)}));
        f.cycle_button(base + 4, cell(4), count(p.monuments_allowed));
        f.cycle_button(base + 5, cell(5), count(p.arches_allowed));
        y += kRow;
    }
    // The notice or the note, then adding, taking away, the pages and the way back.
    const bool notice = !model.notice.empty();
    f.paragraph(Rect{m_x, bottom, m_w, 2 * 20}, notice ? model.notice : ui::tr("editor.players.table_note"),
                notice && model.notice_is_error ? Tone::Warning : Tone::Dim, 20,
                notice ? std::string() : ui::tr("editor.players.note_short"));
    const std::int32_t quarter = (m_w - 3 * kGap) / 4;
    f.button(kPlayerAdd, Rect{m_x, foot, quarter, kButtonH}, ui::tr("editor.players.add"), model.can_add_player);
    f.button(kPlayerRemove, Rect{m_x + quarter + kGap, foot, quarter, kButtonH}, ui::tr("editor.players.remove"),
             model.can_remove_player);
    if (n > m_page_rows) {
        const std::int32_t eighth = (quarter - kGap) / 2;
        const std::int32_t px = m_x + 2 * (quarter + kGap);
        f.button(kPrevious, Rect{px, foot, eighth, kButtonH}, "<", m_first > 0);
        f.button(kNext, Rect{px + eighth + kGap, foot, quarter - eighth - kGap, kButtonH}, ">",
                 m_first + m_page_rows < n);
    }
    f.button(kBack, Rect{m_x + m_w - quarter, foot, quarter, kButtonH}, ui::tr("editor.dialog.close"));
}

void EditorPanel::layout_relations_table(const EditorModel& model) {
    const std::size_t n = model.player_slots.size();
    if (n < 2) {
        m_page = Page::Players;
        layout_players_table(model);
        return;
    }
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    constexpr std::int32_t kTab = 150;
    f.label(Rect{m_x, y, m_w - 2 * (kTab + kGap), 28}, ui::tr("editor.relations.title"), Align::Left, Tone::Heading);
    f.button(kTabPlayers, Rect{m_x + m_w - 2 * kTab - kGap, y, kTab, kButtonH}, ui::tr("editor.players.tab_players"));
    f.button(kTabRelations, Rect{m_x + m_w - kTab, y, kTab, kButtonH}, ui::tr("editor.players.tab_relations"), true,
             true);
    y += kButtonH + kGap + 4;
    // The players down the left and along the top by their numbers; each cell how the two stand.
    constexpr std::int32_t kNames = 220;
    constexpr std::int32_t kCell = 84;  // room for the longest standing in the smaller screens' font
    const auto columns = static_cast<std::size_t>(std::max<std::int32_t>(1, (m_w - kNames) / (kCell + 2)));
    const std::size_t shown = std::min({n, columns, kTableSide});
    m_first_column = n <= shown ? 0 : std::min(m_first_column, n - shown);
    const std::int32_t cell = std::min<std::int32_t>(110, (m_w - kNames) / static_cast<std::int32_t>(shown) - 2);
    for (std::size_t c = 0; c < shown; ++c) {
        const std::size_t b = m_first_column + c;
        f.label(Rect{m_x + kNames + static_cast<std::int32_t>(c) * (cell + 2), y, cell, 18}, count(b + 1),
                Align::Centre, Tone::Dim);
    }
    y += 20;
    constexpr std::int32_t kRow = kButtonH + 2;
    const std::int32_t foot = m_top + m_height - kButtonH - 4;
    const std::int32_t bottom = foot - 2 * 20 - kGap;
    m_page_rows = std::clamp<std::size_t>(static_cast<std::size_t>(std::max(1, (bottom - y) / kRow)), 1, kTableSide);
    m_first = std::min(m_first, (n - 1) / m_page_rows * m_page_rows);
    for (std::size_t row = 0; row < m_page_rows && m_first + row < n; ++row) {
        const std::size_t a = m_first + row;
        const EditorPlayer& p = model.player_slots[a];
        // The number, and the player's own name or else its colour (the colour's name is shorter).
        f.label(Rect{m_x, y, 40, kButtonH}, count(a + 1), Align::Right, Tone::Dim);
        f.label(Rect{m_x + 42, y, kNames - 42 - kGap, kButtonH}, p.own_name.empty() ? p.colour : p.own_name,
                Align::Left, Tone::Normal);
        for (std::size_t c = 0; c < shown; ++c) {
            const std::size_t b = m_first_column + c;
            const Rect r{m_x + kNames + static_cast<std::int32_t>(c) * (cell + 2), y, cell, kButtonH};
            if (a == b) {
                f.label(r, "-", Align::Centre, Tone::Dim);
                continue;
            }
            const EditorPlayer::Standing s = b < p.relations.size() ? p.relations[b] : EditorPlayer::Standing{};
            const char* text = !s.peace  ? "editor.relations.war"
                               : s.trade ? "editor.relations.trade_short"
                                         : "editor.relations.peace";
            f.cycle_button(kRelationTable + static_cast<Id>(row * kTableSide + c), r, ui::tr(text));
        }
        y += kRow;
    }
    const bool notice = !model.notice.empty();
    f.paragraph(Rect{m_x, bottom, m_w, 2 * 20}, notice ? model.notice : ui::tr("editor.relations.table_note"),
                notice && model.notice_is_error ? Tone::Warning : Tone::Dim, 20,
                notice ? std::string() : ui::tr("editor.relations.note"));
    const std::int32_t quarter = (m_w - 3 * kGap) / 4;
    if (n > m_page_rows) {
        const std::int32_t eighth = (quarter - kGap) / 2;
        f.button(kPrevious, Rect{m_x, foot, eighth, kButtonH}, "<", m_first > 0);
        f.button(kNext, Rect{m_x + eighth + kGap, foot, quarter - eighth - kGap, kButtonH}, ">",
                 m_first + m_page_rows < n);
    }
    if (n > shown) {
        const std::int32_t eighth = (quarter - kGap) / 2;
        const std::int32_t px = m_x + quarter + kGap;
        f.button(kColumnsBack, Rect{px, foot, eighth, kButtonH}, "<<", m_first_column > 0);
        f.button(kColumnsOn, Rect{px + eighth + kGap, foot, quarter - eighth - kGap, kButtonH}, ">>",
                 m_first_column + shown < n);
    }
    f.button(kBack, Rect{m_x + m_w - quarter, foot, quarter, kButtonH}, ui::tr("editor.dialog.close"));
}

void EditorPanel::layout_settings_grid(const EditorModel& model, bool rules) {
    if (!rules && !model.has_plan) {
        m_page = Page::Rules;
        layout_settings_grid(model, true);
        return;
    }
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr(rules ? "editor.rules.title" : "editor.random.title"), Align::Left,
            Tone::Heading);
    y += 30;
    const std::int32_t quarter = (m_w - 3 * kGap) / 4;
    if (rules) {
        // Whose settings, the two presets and the plan's random elements in one row.
        // Whose rules takes two fifths, its name being the longest.
        const std::int32_t fifth = (m_w - 3 * kGap) / 5;
        const std::int32_t own = m_w - 3 * kGap - 3 * fifth;
        f.cycle_button(kRulesOwn, Rect{m_x, y, own, kButtonH},
                       ui::tr(model.rules_own ? "editor.rules.own" : "editor.rules.players"));
        std::int32_t bx = m_x + own + kGap;
        f.button(kRulesOriginal, Rect{bx, y, fifth, kButtonH}, ui::tr("editor.rules.original"), true,
                 model.rules_own && model.rules_original);
        bx += fifth + kGap;
        f.button(kRulesOpen1602, Rect{bx, y, fifth, kButtonH}, ui::tr("editor.rules.open1602"));
        bx += fifth + kGap;
        f.button(kRandomPage, Rect{bx, y, fifth, kButtonH}, ui::tr("editor.random.edit"), model.has_plan);
        y += kButtonH + kGap + 4;
    } else {
        f.paragraph(Rect{m_x, y, m_w, 20}, ui::tr("editor.random.note_short"), Tone::Dim, 20);
        y += 24;
    }
    // Two columns of settings, each a label and its value beside it; the rest on pages.
    constexpr std::int32_t kRow = kButtonH + 4;
    const std::int32_t foot = m_top + m_height - kButtonH - 4;
    const std::int32_t bottom = foot - kGap;
    const std::int32_t column = (m_w - 2 * kGap) / 2;
    const std::int32_t label = column * 55 / 100;
    const auto per_column = static_cast<std::size_t>(std::max(1, (bottom - y) / kRow));
    const std::size_t n = rules ? model.rules.size() : model.randoms.size();
    m_page_rows = std::min<std::size_t>(2 * per_column, 20);
    m_first = n == 0 ? 0 : std::min(m_first, (n - 1) / m_page_rows * m_page_rows);
    const std::size_t rows = (m_page_rows + 1) / 2;
    for (std::size_t k = 0; k < m_page_rows && m_first + k < n; ++k) {
        const std::int32_t cx = m_x + static_cast<std::int32_t>(k / rows) * (column + 2 * kGap);
        const std::int32_t cy = y + static_cast<std::int32_t>(k % rows) * kRow;
        const Id id = kFirstRow + static_cast<Id>(k);
        if (rules) {
            const EditorRule& r = model.rules[m_first + k];
            const bool on = model.rules_own && r.enabled;
            f.label(Rect{cx, cy, label - kGap, kButtonH}, r.label, Align::Left, on ? Tone::Normal : Tone::Dim);
            f.cycle_button(id, Rect{cx + label, cy, column - label, kButtonH}, r.value, on);
        } else {
            const EditorRandom& r = model.randoms[m_first + k];
            f.label(Rect{cx, cy, label - kGap, kButtonH}, r.label);
            f.cycle_button(id, Rect{cx + label, cy, column - label, kButtonH},
                           ui::tr("editor.random.value", {count(r.random), count(r.fixed)}));
        }
    }
    // The foot: the pages, the dice preview or the way back to the rules, and closing.
    const std::int32_t eighth = (quarter - kGap) / 2;
    if (n > m_page_rows) {
        f.button(kPrevious, Rect{m_x, foot, eighth, kButtonH}, "<", m_first > 0);
        f.button(kNext, Rect{m_x + eighth + kGap, foot, quarter - eighth - kGap, kButtonH}, ">",
                 m_first + m_page_rows < n);
    }
    if (!rules) {
        f.button(kRandomBack, Rect{m_x + quarter + kGap, foot, quarter, kButtonH}, ui::tr("editor.random.back"));
        f.button(kRandomRoll, Rect{m_x + 2 * (quarter + kGap), foot, quarter, kButtonH}, ui::tr("editor.random.roll"));
    }
    f.button(kBack, Rect{m_x + m_w - quarter, foot, quarter, kButtonH}, ui::tr("editor.dialog.close"));
}

void EditorPanel::layout_world_size(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.world_size.title"), Align::Left, Tone::Heading);
    y += 28;
    f.entry(Rect{m_x, y, m_w, kLine}, ui::tr("editor.world_size.now"),
            ui::tr("editor.size", {count(model.width), count(model.height)}), Tone::Dim);
    y += kLine;
    f.entry(Rect{m_x, y, m_w, kLine}, ui::tr("editor.world_size.new"),
            ui::tr("editor.size", {count(m_size_width), count(m_size_height)}));
    y += kLine + 4;
    const std::int32_t half = (m_w - kGap) / 2;
    const auto side = [&](const char* label, Id less, Id more, std::uint32_t value, std::uint32_t used) {
        f.label(Rect{m_x, y, m_w, kLine}, ui::tr(label));
        y += kLine;
        f.button(less, Rect{m_x, y, half, kButtonH}, ui::tr("editor.world_size.less", {count(kSizeStep)}),
                 value > std::max<std::uint32_t>(used, 1));
        f.button(more, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.world_size.more", {count(kSizeStep)}),
                 value < sim::kMaxWorldSide);
        y += kButtonH + kGap;
    };
    side("editor.world_size.width", kWidthLess, kWidthMore, m_size_width, model.used_width);
    side("editor.world_size.height", kHeightLess, kHeightMore, m_size_height, model.used_height);
    f.button(kFitSize, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.world_size.fit"));
    y += kButtonH + kGap;
    constexpr std::int32_t kSizeNote = 4 * 20;
    f.paragraph(Rect{m_x, y, m_w, kSizeNote}, ui::tr("editor.world_size.note"), Tone::Dim, 20);
    y += kSizeNote + 4;
    const bool changed = m_size_width != model.width || m_size_height != model.height;
    f.button(kApplySize, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.world_size.apply"), changed);
    y += kButtonH + kGap;
    f.button(kCancel, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_view(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.view.title"), Align::Left, Tone::Heading);
    y += 34;
    for (std::size_t i = 0; i < kEditorOverlays; ++i) {
        f.check(kFirstOverlay + static_cast<Id>(i), Rect{m_x, y, m_w, kButtonH}, ui::tr(kOverlayTexts[i]),
                model.overlays[i]);
        y += kButtonH + kGap;
    }
    y += kGap;
    f.button(kBack, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_check(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{m_x, y, m_w, 28}, ui::tr("editor.check.title"), Align::Left, Tone::Heading);
    y += 32;
    // Each line gets three lines of text, a heading two; as many as the room holds above the
    // buttons, the rest on the next pages.
    constexpr std::int32_t kItem = 3 * 20;
    constexpr std::int32_t kHeading = 2 * 20 + 2;
    const std::size_t n = model.check.size();
    m_first = std::min(m_first, n == 0 ? 0 : n - 1);
    using Kind = EditorCheckLine::Kind;
    // Room for the pages' buttons only where the lines left do not fit without them.
    std::int32_t rest = 0;
    for (std::size_t k = m_first; k < n; ++k) {
        rest += model.check[k].kind == Kind::Heading ? kHeading : kItem;
    }
    const std::int32_t one_row = m_top + m_height - (kButtonH + kGap) - 6;
    const std::int32_t bottom = m_first == 0 && y + rest <= one_row ? one_row : one_row - (kButtonH + kGap);
    std::size_t i = m_first;
    for (; i < n; ++i) {
        const EditorCheckLine& line = model.check[i];
        const std::int32_t h = line.kind == Kind::Heading ? kHeading : kItem;
        // A heading goes to the next page with its first line.
        const std::int32_t needs = line.kind == Kind::Heading && i + 1 < n ? h + kItem : h;
        if (y + needs > bottom && i > m_first) {
            break;
        }
        if (line.kind == Kind::Heading) {
            f.paragraph(Rect{m_x, y, m_w, kHeading - 2}, line.text, Tone::Heading, 20);
        } else {
            const Tone tone = line.kind == Kind::Error || line.kind == Kind::Warning ? Tone::Warning
                              : line.kind == Kind::Fine                              ? Tone::Normal
                                                                                     : Tone::Dim;
            f.paragraph(Rect{m_x + 8, y, m_w - 8, kItem}, line.text, tone, 20);
        }
        y += h;
    }
    m_page_rows = std::max<std::size_t>(1, i - m_first);
    y += 4;
    if (m_first > 0 || i < n) {
        const std::int32_t half = (m_w - kGap) / 2;
        f.button(kPrevious, Rect{m_x, y, half, kButtonH}, ui::tr("editor.previous"), m_first > 0);
        f.button(kNext, Rect{m_x + half + kGap, y, half, kButtonH}, ui::tr("editor.next"), i < n);
        y += kButtonH + kGap;
    }
    f.button(kBack, Rect{m_x, y, m_w, kButtonH}, ui::tr("editor.back"));
}

std::vector<ui::DrawItem> EditorPanel::draw(const EditorModel& model,
                                            const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    layout(model);
    return m_form.draw(fonts);
}

std::optional<EditorRequest> EditorPanel::commit_name() {
    if (m_page == Page::PlayerName) {
        if (!m_text.empty() && !sim::valid_name(m_text, sim::CityOrder::kMaxNameLength)) {
            return std::nullopt;
        }
        m_page = Page::Players;
        m_form.set_focus(std::nullopt);
        EditorRequest r{EditorRequest::Kind::SetPlayer};
        r.index = 3;  // the name alone: the viewer takes the slot's other values
        r.name = m_text;
        return r;
    }
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
    return press(*id, model, m_form.clicked_with() == ui::MouseButton::Left);
}

std::optional<EditorRequest> EditorPanel::press(Id pressed, const EditorModel& model, bool forward) {
    const std::optional<Id> id = pressed;
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
        case kRulesPage:
            m_page = Page::Rules;
            m_first = 0;
            return std::nullopt;
        case kRandomPage:
            if (model.has_plan) {
                m_page = Page::Random;
                m_first = 0;
            }
            return std::nullopt;
        case kRandomRoll:
            return EditorRequest{EditorRequest::Kind::RandomRoll};
        case kPreviewEnd:
            return EditorRequest{EditorRequest::Kind::RandomPreviewEnd};
        case kRandomBack:
            m_page = Page::Rules;
            m_first = 0;
            return std::nullopt;
        case kRulesOwn:
            return EditorRequest{EditorRequest::Kind::RulesPreset, model.rules_own ? 0U : 3U};
        case kRulesOriginal:
            return EditorRequest{EditorRequest::Kind::RulesPreset, 1};
        case kRulesOpen1602:
            return EditorRequest{EditorRequest::Kind::RulesPreset, 2};
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
                std::size_t& chosen = o.tool == Tool::Template   ? o.stamp
                                      : o.tool == Tool::Ship     ? o.ship
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
                o.owner = model.object_owners > 0 ? step(o.owner, model.object_owners, forward) : 0;
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
        case kTerrainBrush:
        case kTerrainSmaller:
        case kTerrainBigger:
        case kTerrainShape:
        case kTerrainSlider: {
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
            } else if (*id == kTerrainSmaller) {
                t.brush = static_cast<std::uint8_t>(t.brush > 0 ? t.brush - 1 : 0);
            } else if (*id == kTerrainBigger) {
                t.brush = static_cast<std::uint8_t>(std::min<std::size_t>(t.brush + 1U, kTerrainBrushes.size() - 1));
            } else if (*id == kTerrainShape) {
                t.shape = static_cast<std::uint8_t>(step(t.shape, kBrushShapeTexts.size(), forward));
            } else if (*id == kTerrainSlider) {
                // Let go: the nearest size.
                const double last = static_cast<double>(kTerrainBrushes.size() - 1);
                t.brush = static_cast<std::uint8_t>(std::lround(std::clamp(m_form.slid_to(), 0.0, 1.0) * last));
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
        case kPlayersPage:
        case kTabPlayers:
            m_page = Page::Players;
            m_first = 0;
            return std::nullopt;
        case kTabRelations:
            if (model.player_slots.size() > 1) {
                m_page = Page::Relations;
                m_first = 0;
                m_first_column = 0;
            }
            return std::nullopt;
        case kColumnsBack:
            m_first_column = m_first_column > kTableSide / 2 ? m_first_column - kTableSide / 2 : 0;
            return std::nullopt;
        case kColumnsOn:
            m_first_column += kTableSide / 2;
            return std::nullopt;
        case kPlayerSlot:
            if (!model.player_slots.empty()) {
                m_player = step(std::min(m_player, model.player_slots.size() - 1), model.player_slots.size(), forward);
            }
            return std::nullopt;
        case kPlayerNamePage:
            if (!model.player_slots.empty()) {
                m_page = Page::PlayerName;
                m_text = model.player_slots[std::min(m_player, model.player_slots.size() - 1)].own_name;
                m_form.set_focus(kField);
                m_form.select_all();
            }
            return std::nullopt;
        case kRelationsPage:
            if (model.player_slots.size() > 1) {
                m_page = Page::Relations;
            }
            return std::nullopt;
        case kRelationPartner: {
            const std::size_t n = model.player_slots.size();
            if (n > 1) {
                // The others only: the slot itself is skipped.
                const std::size_t a = std::min(m_player, n - 1);
                do {
                    m_partner = step(std::min(m_partner, n - 1), n, forward);
                } while (m_partner == a);
            }
            return std::nullopt;
        }
        case kRelationPeace:
        case kRelationTrade: {
            const std::size_t n = model.player_slots.size();
            if (n < 2 || m_partner >= n) {
                return std::nullopt;
            }
            const EditorPlayer& p = model.player_slots[std::min(m_player, n - 1)];
            EditorPlayer::Standing s =
                m_partner < p.relations.size() ? p.relations[m_partner] : EditorPlayer::Standing{};
            if (*id == kRelationPeace) {
                s.peace = !s.peace;
                s.trade = s.trade && s.peace;  // war ends trade
            } else {
                s.trade = !s.trade && s.peace;
            }
            EditorRequest r{EditorRequest::Kind::SetRelations};
            r.player.slot = p.slot;
            r.player.relations = {s};
            r.index = model.player_slots[m_partner].slot;
            return r;
        }
        case kPlayersBack:
            m_page = Page::Players;
            m_form.set_focus(std::nullopt);
            return std::nullopt;
        case kPlayerAdd:
            m_player = model.player_slots.size();  // the new one
            return EditorRequest{EditorRequest::Kind::AddPlayer};
        case kPlayerRemove:
            // The slot shown stays; once the last is gone, the page shows the one before (it
            // clamps the choice), and a refused removal leaves it as it was.
            return EditorRequest{EditorRequest::Kind::RemovePlayer};
        case kPlayerRole:
        case kPlayerColour:
        case kPlayerMoney:
        case kPlayerMonuments:
        case kPlayerArches: {
            if (model.player_slots.empty()) {
                return std::nullopt;
            }
            EditorRequest r{EditorRequest::Kind::SetPlayer};
            r.player = model.player_slots[std::min(m_player, model.player_slots.size() - 1)];
            r.name = r.player.own_name;
            EditorPlayer& p = r.player;
            if (*id == kPlayerRole) {
                p.human = !p.human;
            } else if (*id == kPlayerColour) {
                r.index = forward ? 1 : 2;  // the viewer steps the banner among the free ones
            } else if (*id == kPlayerMoney) {
                // 1000 a click, 10 000 above 50 000.
                const std::int32_t stepped = p.money >= 50'000 || (!forward && p.money > 50'000) ? 10'000 : 1'000;
                p.money = forward ? p.money + stepped : p.money - stepped;
            } else if (*id == kPlayerMonuments) {
                p.monuments_allowed = static_cast<std::uint16_t>(step(p.monuments_allowed, 10, forward));
            } else {
                p.arches_allowed = static_cast<std::uint16_t>(step(p.arches_allowed, 10, forward));
            }
            return r;
        }
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
            const std::size_t rows = paged_by_rows(m_page) ? m_page_rows : kRows;
            m_first = m_first > rows ? m_first - rows : 0;
            return std::nullopt;
        }
        case kNext:
            m_first += paged_by_rows(m_page) ? m_page_rows : kRows;
            return std::nullopt;
        default:
            break;
    }
    if (*id >= kRecent && *id < kRecent + static_cast<Id>(kEditorRecent)) {
        // The object placed lately back in hand, for the owner chosen now.
        const auto i = static_cast<std::size_t>(*id - kRecent);
        if (i >= model.recent_objects.size()) {
            return std::nullopt;
        }
        EditorRequest r{EditorRequest::Kind::Objects};
        r.objects = model.recent_objects[i].objects;
        r.objects.owner = model.objects.owner;
        m_page = Page::Objects;
        return r;
    }
    if (*id >= kPlayerTable && *id < kPlayerTable + static_cast<Id>(8 * kTableRows)) {
        // A cell of the players' table: its row's slot, then what its button on the column's
        // page does.
        const auto row = static_cast<std::size_t>((*id - kPlayerTable) / 8);
        const auto column = (*id - kPlayerTable) % 8;
        if (m_first + row >= model.player_slots.size()) {
            return std::nullopt;
        }
        m_player = m_first + row;
        constexpr std::array<Id, 6> kCells = {kPlayerNamePage, kPlayerRole,      kPlayerColour,
                                              kPlayerMoney,    kPlayerMonuments, kPlayerArches};
        return column < 6 ? press(kCells[static_cast<std::size_t>(column)], model, forward) : std::nullopt;
    }
    if (*id >= kRelationTable && *id < kRelationTable + static_cast<Id>(kTableSide * kTableSide)) {
        // A cell of the diplomacy table: war, peace, peace and trade, round (the right button back).
        const auto a = m_first + static_cast<std::size_t>(*id - kRelationTable) / kTableSide;
        const auto b = m_first_column + static_cast<std::size_t>(*id - kRelationTable) % kTableSide;
        const std::size_t n = model.player_slots.size();
        if (a >= n || b >= n || a == b) {
            return std::nullopt;
        }
        const EditorPlayer& p = model.player_slots[a];
        EditorPlayer::Standing s = b < p.relations.size() ? p.relations[b] : EditorPlayer::Standing{};
        const std::size_t now = !s.peace ? 0 : s.trade ? 2 : 1;
        const std::size_t next = step(now, 3, forward);
        s.peace = next > 0;
        s.trade = next == 2;
        EditorRequest r{EditorRequest::Kind::SetRelations};
        r.player.slot = p.slot;
        r.player.relations = {s};
        r.index = model.player_slots[b].slot;
        return r;
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
    if (*id >= kFirstRow && *id < kFirstRow + 20 && m_page == Page::Rules) {
        const std::size_t index = m_first + static_cast<std::size_t>(*id - kFirstRow);
        if (index < model.rules.size() && model.rules_own) {
            EditorRequest r{EditorRequest::Kind::RuleStep, index};
            r.amount = forward ? 1U : 0U;
            return r;
        }
        return std::nullopt;
    }
    if (*id >= kFirstRow && *id < kFirstRow + 20 && m_page == Page::Random) {
        const std::size_t index = m_first + static_cast<std::size_t>(*id - kFirstRow);
        if (index < model.randoms.size()) {
            EditorRequest r{EditorRequest::Kind::RandomStep, index};
            r.amount = forward ? 1U : 0U;
            return r;
        }
        return std::nullopt;
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
            } else if (m_page == Page::PlayerName) {
                m_page = Page::Players;
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
