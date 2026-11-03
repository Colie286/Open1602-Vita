// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's side panel (editor_panel.hpp): its pages and what their buttons and keys ask for.
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "editor_panel.hpp"

#include "o1602/ui/localization.hpp"

using namespace o1602;
using app::EditorModel;
using app::EditorNewMap;
using app::EditorPanel;
using app::EditorRequest;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts = {nullptr, nullptr, nullptr};

bool shows(const std::vector<ui::DrawItem>& items, const std::string& text) {
    for (const ui::DrawItem& d : items) {
        if (d.kind == ui::DrawItem::Kind::Text && d.text.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// Whether a widget as laid out last says `text` (whole, before it is cut to its box).
bool says(const EditorPanel& panel, const std::string& text) {
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        if (w.text.find(text) != std::string::npos || w.value.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// The middle of the widget with `text` as laid out last (a button, or a list's label).
std::optional<std::array<std::int32_t, 2>> find(const EditorPanel& panel, const std::string& text) {
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        if (w.text == text) {
            return std::array<std::int32_t, 2>{w.rect.x + w.rect.w / 2, w.rect.y + w.rect.h / 2};
        }
    }
    return std::nullopt;
}

std::optional<EditorRequest> click(EditorPanel& panel, const EditorModel& model, const std::string& text,
                                   ui::MouseButton button = ui::MouseButton::Left) {
    panel.layout(model);
    const auto at = find(panel, text);
    REQUIRE(at.has_value());
    (void)panel.mouse((*at)[0], (*at)[1], true, model, button);
    return panel.mouse((*at)[0], (*at)[1], false, model, button);
}

std::optional<EditorRequest> click_id(EditorPanel& panel, const EditorModel& model, ui::Form::Id id) {
    panel.layout(model);
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        if (w.id == id) {
            const std::int32_t x = w.rect.x + w.rect.w / 2;
            const std::int32_t y = w.rect.y + w.rect.h / 2;
            (void)panel.mouse(x, y, true, model, ui::MouseButton::Left);
            return panel.mouse(x, y, false, model, ui::MouseButton::Left);
        }
    }
    REQUIRE(false);
    return std::nullopt;
}

EditorModel document() {
    EditorModel m;
    m.width = 500;
    m.height = 350;
    m.islands = 18;
    m.players = 4;
    return m;
}

}  // namespace

TEST_CASE("editor panel: the document's page tells what the map is and offers the files and undo") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    auto items = panel.draw(m, kNoFonts);
    CHECK(shows(items, "Untitled map"));
    CHECK(shows(items, "500 \xD7 350"));
    CHECK(shows(items, "18"));
    // Nothing to undo yet: the buttons cannot be pressed.
    CHECK(!click(panel, m, "Undo").has_value());
    m.can_undo = true;
    m.file = "Zwei Kuesten";
    m.modified = true;
    items = panel.draw(m, kNoFonts);
    CHECK(shows(items, "Zwei Kuesten *"));
    auto r = click(panel, m, "Undo");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::Undo);
    r = click(panel, m, "Save");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::Save);
    r = click(panel, m, "Leave the editor");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::Close);
    // A world plan: the islands are made when a game starts.
    m.planned_islands = 18;
    CHECK(shows(panel.draw(m, kNoFonts), "18 planned"));
    // Everything lies in the panel's room.
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        CHECK(w.rect.y >= 200);
        CHECK(w.rect.y + w.rect.h <= 760);
        CHECK(w.rect.x >= 0);
        CHECK(w.rect.x + w.rect.w <= EditorPanel::kWidth);
    }
}

TEST_CASE("editor panel: a new map goes through its sizes, islands and players") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    const EditorModel m = document();
    CHECK(!click(panel, m, "New map").has_value());
    CHECK(panel.page() == EditorPanel::Page::New);
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "500 \xD7 350 (as the original)"));
    CHECK(!click(panel, m, "500 \xD7 350 (as the original)").has_value());
    CHECK(panel.new_map().width() == 700);
    // The right button goes back, and round from the first to the last.
    (void)click(panel, m, "700 \xD7 500", ui::MouseButton::Right);
    (void)click(panel, m, "500 \xD7 350 (as the original)", ui::MouseButton::Right);
    CHECK(panel.new_map().width() == 4000);
    (void)click(panel, m, "As in the original");
    CHECK(panel.new_map().islands == EditorNewMap::Islands::Many);
    (void)click(panel, m, "4");
    CHECK(panel.new_map().players == 5);
    const std::uint64_t seed = panel.new_map().seed;
    (void)click(panel, m, "Another world");
    CHECK(panel.new_map().seed != seed);
    const auto r = click(panel, m, "Create");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::New);
    CHECK(r->map.width() == 4000);
    CHECK(r->map.height() == 4000);
    CHECK(r->map.islands == EditorNewMap::Islands::Many);
    CHECK(r->map.players == 5);
    CHECK(panel.page() == EditorPanel::Page::Document);

    // Only sea: no players to choose.
    panel.ask_new(EditorNewMap{});
    (void)click(panel, m, "As in the original");
    (void)click(panel, m, "Many");
    CHECK(panel.new_map().islands == EditorNewMap::Islands::None);
    CHECK(!click(panel, m, "4").has_value());
    CHECK(panel.new_map().players == 4);
    CHECK(panel.back());
    CHECK(panel.page() == EditorPanel::Page::Document);
    CHECK(!panel.back());
}

TEST_CASE("editor panel: the list of files opens one, in pages, with the groups' headings") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    auto r = click(panel, m, "Open");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::ListFiles);
    CHECK(panel.page() == EditorPanel::Page::Open);
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "There are no scenarios to open."));
    for (int i = 0; i < 3; ++i) {
        m.files.push_back(app::EditorFile{"Own " + std::to_string(i), "Own scenarios"});
    }
    for (int i = 0; i < 20; ++i) {
        m.files.push_back(app::EditorFile{"Game " + std::to_string(i) + ".szs", "Scenarios of the game"});
    }
    auto items = panel.draw(m, kNoFonts);
    CHECK(shows(items, "Own scenarios"));
    CHECK(says(panel, "Scenarios of the game"));
    CHECK(!shows(items, "Game 9.szs"));  // on the next page
    (void)click(panel, m, "More");
    items = panel.draw(m, kNoFonts);
    CHECK(says(panel, "Scenarios of the game"));  // the group goes on: its heading again
    CHECK(shows(items, "Game 9.szs"));
    r = click(panel, m, "Game 9.szs");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::Open);
    CHECK(r->index == 12);
    CHECK(panel.page() == EditorPanel::Page::Document);
}

TEST_CASE("editor panel: a name is typed to save under, and a taken one replaces that scenario") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    m.file = "Das Atoll";
    CHECK(!click(panel, m, "Save as").has_value());
    CHECK(panel.typing());
    CHECK(panel.text() == "Das Atoll");
    panel.type("Insel");  // replaces the name offered
    panel.type("n/?");    // what a file's name cannot carry goes when it is saved
    CHECK(panel.text() == "Inseln/?");
    m.own_names = {"inseln"};
    auto items = panel.draw(m, kNoFonts);
    CHECK(shows(items, "Replace"));
    CHECK(says(panel, "saving replaces that scenario"));
    (void)panel.key(8);
    (void)panel.key(8);
    (void)panel.key(8);
    CHECK(panel.text() == "Insel");
    CHECK(shows(panel.draw(m, kNoFonts), "Save"));
    const auto r = panel.key(13);
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::SaveAs);
    CHECK(r->name == "Insel");
    CHECK(!panel.typing());

    // Escape takes the question back; a name of nothing cannot be saved.
    panel.ask_name("x");
    CHECK(!panel.key(27).has_value());
    CHECK(panel.page() == EditorPanel::Page::Document);
    panel.ask_name(" . ");
    CHECK(panel.text().empty());
    CHECK(!panel.key(13).has_value());
    CHECK(EditorPanel::clean_name("  a<b>:c|d.  ") == "abcd");
    CHECK(EditorPanel::name_taken(m, "INSELN"));
    CHECK(!EditorPanel::name_taken(m, "Insel"));
}

TEST_CASE("editor panel: a pick on the map shows its properties, Back goes to the document") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    m.selection = "Island 3";
    m.properties = {{"Size", "40 \xD7 30"}, {"Climate", "North"}};
    CHECK(panel.show_selection());
    CHECK(panel.page() == EditorPanel::Page::Selection);
    auto items = panel.draw(m, kNoFonts);
    CHECK(says(panel, "Island 3"));
    CHECK(says(panel, "40 \xD7 30"));
    CHECK(says(panel, "Climate"));
    CHECK(!click(panel, m, "Back to the map").has_value());
    CHECK(panel.page() == EditorPanel::Page::Document);
    // What was picked is gone: the page says so.
    m.selection.clear();
    m.properties.clear();
    CHECK(panel.show_selection());
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "is gone"));
    CHECK(panel.back());
    // A click on the map does not leave a page of the files or of a new map.
    panel.ask_new(EditorNewMap{});
    CHECK(!panel.show_selection());
    CHECK(panel.page() == EditorPanel::Page::New);
}

TEST_CASE("editor panel: the overlays go on and off, the check list goes through its pages") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    CHECK(!click(panel, m, "Overlays").has_value());
    CHECK(panel.page() == EditorPanel::Page::View);
    auto r = click(panel, m, "Island numbers");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::Overlay);
    CHECK(r->index == static_cast<std::size_t>(app::EditorOverlay::IslandNumbers));
    CHECK(panel.back());

    r = click(panel, m, "Check");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::Check);
    CHECK(panel.page() == EditorPanel::Page::Check);
    using Line = app::EditorCheckLine;
    m.check.push_back(Line{"For saving", Line::Kind::Heading});
    m.check.push_back(Line{"All right.", Line::Kind::Fine});
    m.check.push_back(Line{"The original's limits (not asked for)", Line::Kind::Heading});
    for (int i = 0; i < 10; ++i) {
        m.check.push_back(Line{"Island " + std::to_string(i + 1) + " has 3 ore deposits", Line::Kind::Note});
    }
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "All right."));
    CHECK(!says(panel, "Island 10 has"));
    CHECK(!click(panel, m, "More").has_value());
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "Island 10 has"));
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        CHECK(w.rect.y + w.rect.h <= 760);
    }
    CHECK(!click(panel, m, "Back to the map").has_value());
    CHECK(panel.page() == EditorPanel::Page::Document);
}

TEST_CASE("editor panel: new islands are set up and placed, a selected island moves, turns, copies, goes") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    CHECK(!click(panel, m, "Edit islands").has_value());
    REQUIRE(panel.page() == EditorPanel::Page::Islands);
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "Original template"));
    CHECK(says(panel, "Medium (50 \xD7 52)"));
    CHECK(!click(panel, m, "Original template").has_value());
    CHECK(panel.new_island().own_shape);
    CHECK(!click(panel, m, "North").has_value());
    CHECK(panel.new_island().climate == 1);
    CHECK(!click(panel, m, "Medium (50 \xD7 52)").has_value());
    CHECK(panel.new_island().size == 3);
    CHECK(!click(panel, m, "Large (70 \xD7 60)", ui::MouseButton::Right).has_value());
    CHECK(panel.new_island().size == 2);
    const auto seed = panel.new_island().seed;
    CHECK(!click(panel, m, "Another one").has_value());
    CHECK(panel.new_island().seed != seed);
    auto r = click(panel, m, "Place");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::PlaceIsland);
    CHECK(r->island == panel.new_island());
    // While it is placed: a note and a way back.
    m.placing = true;
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "right click cancels"));
    r = click(panel, m, "Cancel");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::StopPlacing);
    m.placing = false;
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        CHECK(w.rect.y + w.rect.h <= 760);
    }

    // An island picked on the map: its buttons, those that need a free island only for one.
    m.selection = "Island 3";
    m.properties = {{"Size", "40 \xD7 30"}, {"Climate", "North"}};
    m.island = 2;
    m.island_free = false;
    CHECK(panel.show_selection());
    CHECK(!click(panel, m, "Turn").has_value());
    CHECK(!click(panel, m, "Delete").has_value());
    r = click(panel, m, "Move");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::MoveIsland);
    CHECK(r->index == 2);
    m.island_free = true;
    r = click(panel, m, "Turn");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::RotateIsland);
    r = click(panel, m, "Copy");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::CopyIsland);
    r = click(panel, m, "Delete");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::RemoveIsland);
    CHECK(r->index == 2);
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        CHECK(w.rect.y + w.rect.h <= 760);
    }
}

TEST_CASE("editor panel: the map size page grows and cuts the map, not below what it holds") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    m.width = 500;
    m.height = 350;
    m.used_width = 112;
    m.used_height = 330;
    CHECK(!click(panel, m, "Change map size").has_value());
    REQUIRE(panel.page() == EditorPanel::Page::WorldSize);
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "500 \xD7 350"));
    CHECK(!click(panel, m, "Apply").has_value());  // nothing changed yet
    // Width and height have a pair of buttons each: the first pair is the width's.
    std::vector<ui::Form::Id> more;
    std::vector<ui::Form::Id> less;
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        if (w.text == "+ 50") {
            more.push_back(w.id);
        } else if (w.text == "- 50") {
            less.push_back(w.id);
        }
    }
    REQUIRE(more.size() == 2);
    REQUIRE(less.size() == 2);
    CHECK(!click_id(panel, m, more[0]).has_value());
    CHECK(!click_id(panel, m, less[1]).has_value());
    CHECK(!click_id(panel, m, less[1]).has_value());  // not below 330
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "550 \xD7 330"));
    auto r = click(panel, m, "Apply");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::ResizeWorld);
    CHECK(r->width == 550);
    CHECK(r->height == 330);
    CHECK(!click(panel, m, "Fit to content").has_value());
    r = click(panel, m, "Apply");
    REQUIRE(r.has_value());
    CHECK(r->width == 120);
    CHECK(r->height == 340);
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        CHECK(w.rect.y + w.rect.h <= 760);
    }
    CHECK(!click(panel, m, "Cancel").has_value());
    CHECK(panel.page() == EditorPanel::Page::Document);
}

TEST_CASE("editor panel: an island's crops are ticked, it is saved, and saved islands are loaded") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    m.selection = "Island 2";
    m.island = 1;
    m.island_free = true;
    m.island_climate = 0;
    m.island_fertility = 1U << 2U;  // spices
    CHECK(panel.show_selection());
    auto r = click(panel, m, "Save");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::SaveIsland);
    CHECK(r->index == 1);
    CHECK(!click(panel, m, "Crops").has_value());
    REQUIRE(panel.page() == EditorPanel::Page::Fertility);
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "Tobacco (half)"));  // a crop of the north
    CHECK(says(panel, "Cotton"));
    CHECK(!says(panel, "Cotton (half)"));
    r = click(panel, m, "Spices");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::SetFertility);
    CHECK(r->fertility == 0);
    r = click(panel, m, "Wine (half)");
    REQUIRE(r.has_value());
    CHECK(r->fertility == (1U << 2U | 1U << 5U));
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        CHECK(w.rect.y + w.rect.h <= 760);
    }
    CHECK(!click(panel, m, "Back to the selection").has_value());
    CHECK(panel.page() == EditorPanel::Page::Selection);

    m.selection.clear();
    m.island.reset();
    panel.show_document();
    CHECK(!click(panel, m, "Edit islands").has_value());
    r = click(panel, m, "Load island");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::ListIslandFiles);
    REQUIRE(panel.page() == EditorPanel::Page::IslandFiles);
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "No islands saved yet"));
    m.files = {app::EditorFile{"Island 1", "Own islands"}, app::EditorFile{"Reef", "Own islands"}};
    r = click(panel, m, "Reef");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::LoadIsland);
    CHECK(r->index == 1);
    CHECK(panel.page() == EditorPanel::Page::Islands);
}

TEST_CASE("editor panel: placeholders are set with a plan, and a selected one moves, changes, goes") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    CHECK(!click(panel, m, "Edit islands").has_value());
    CHECK(!click(panel, m, "Original template").has_value());
    CHECK(!click(panel, m, "Own shape").has_value());
    CHECK(!panel.new_island().placeholder);  // no plan: back to the templates
    m.has_plan = true;
    CHECK(!click(panel, m, "Original template").has_value());
    CHECK(!click(panel, m, "Own shape").has_value());
    CHECK(panel.new_island().placeholder);
    (void)panel.draw(m, kNoFonts);
    auto r = click(panel, m, "Place");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::PlaceIsland);
    CHECK(r->island.placeholder);

    m.selection = "Placeholder 3";
    m.placeholder = 2;
    m.placeholder_climate = 1;
    m.placeholder_size = 4;
    CHECK(panel.show_selection());
    r = click(panel, m, "Move");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::MovePlaceholder);
    CHECK(r->index == 2);
    r = click(panel, m, "South");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::ChangePlaceholder);
    CHECK(r->island.climate == 2);
    CHECK(r->island.size == 4);
    r = click(panel, m, "Huge (100 \xD7 90)");
    REQUIRE(r.has_value());
    CHECK(r->island.size == 0);
    r = click(panel, m, "Delete");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::RemovePlaceholder);
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        CHECK(w.rect.y + w.rect.h <= 760);
    }
}

TEST_CASE("editor panel: the terrain tools go through their tools, kinds, pieces, turns and brushes") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    m.terrain_pieces = 3;
    m.terrain_id = 22722;
    CHECK(!click(panel, m, "Edit terrain").has_value());
    REQUIRE(panel.page() == EditorPanel::Page::Terrain);
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "22722 (1/3)"));
    CHECK(says(panel, "The left button paints"));

    auto r = click(panel, m, "Brush");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::Terrain);
    CHECK(r->terrain.tool == app::EditorTerrain::Tool::Land);
    r = click(panel, m, "Brush", ui::MouseButton::Right);
    REQUIRE(r.has_value());
    CHECK(r->terrain.tool == app::EditorTerrain::Tool::Paste);

    // Selecting and pasting: the copy's buttons in the place of the settings.
    m.terrain.tool = app::EditorTerrain::Tool::Select;
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "Nothing copied yet"));
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        CHECK(w.rect.y + w.rect.h <= 760);
        if (w.text == "Copy" || w.text == "Turn copy" || w.text == "Mirror copy") {
            CHECK(!w.enabled);
        }
    }
    m.terrain_selected = true;
    m.clip_width = 12;
    m.clip_height = 9;
    r = click(panel, m, "Copy");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::TerrainCopy);
    r = click(panel, m, "Turn copy");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::TerrainTurn);
    r = click(panel, m, "Mirror copy");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::TerrainMirror);
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "Copy: 12 \xD7 9 tiles"));
    m.terrain.tool = app::EditorTerrain::Tool::Brush;

    m.terrain.piece = 2;
    m.terrain.rotation = 1;
    r = click(panel, m, "Ground");
    REQUIRE(r.has_value());
    CHECK(r->terrain.group == 1);
    CHECK(r->terrain.piece == 0);  // a new kind starts at its first piece
    CHECK(r->terrain.rotation == 0);

    m.terrain = app::EditorTerrain{};
    r = click(panel, m, "22722 (1/3)", ui::MouseButton::Right);
    REQUIRE(r.has_value());
    CHECK(r->terrain.piece == 2);

    // The turn only for a piece that turns.
    panel.layout(m);
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        if (w.text == "0\xB0") {
            CHECK(!w.enabled);
        }
    }
    m.terrain_turns = true;
    r = click(panel, m, "0\xB0");
    REQUIRE(r.has_value());
    CHECK(r->terrain.rotation == 1);

    r = click(panel, m, "1 \xD7 1");
    REQUIRE(r.has_value());
    CHECK(r->terrain.brush == 1);

    // A piece of several tiles: no brush size, a note of its size.
    m.terrain_width = 4;
    m.terrain_height = 4;
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "A piece of 4 \xD7 4 tiles"));
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        CHECK(w.rect.y + w.rect.h <= 760);
        if (w.text == "1 \xD7 1") {
            CHECK(!w.enabled);
        }
    }
    CHECK(!click(panel, m, "Back to the map").has_value());
    CHECK(panel.page() == EditorPanel::Page::Document);
}

TEST_CASE("editor panel: the buildings' tools go through their tools, groups, buildings, turns and owners") {
    ui::set_language("en");
    EditorPanel panel;
    panel.set_room(200, 560);
    EditorModel m = document();
    m.object_groups = 7;
    m.object_pieces = 4;
    m.object_group = "Farms";
    m.object_name = "Cattle farm";
    m.object_owner = "Merchant";
    CHECK(!click(panel, m, "Edit buildings").has_value());
    REQUIRE(panel.page() == EditorPanel::Page::Objects);
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "Cattle farm (1/4)"));
    CHECK(says(panel, "builds for the owner without costs"));
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        CHECK(w.rect.y + w.rect.h <= 760);
    }

    auto r = click(panel, m, "Build");
    REQUIRE(r.has_value());
    CHECK(r->kind == EditorRequest::Kind::Objects);
    CHECK(r->objects.tool == app::EditorObjects::Tool::Remove);

    m.objects.piece = 2;
    m.objects.rotation = 3;
    r = click(panel, m, "Farms");
    REQUIRE(r.has_value());
    CHECK(r->objects.group == 1);
    CHECK(r->objects.piece == 0);  // a new group starts at its first building
    CHECK(r->objects.rotation == 0);
    r = click(panel, m, "Cattle farm (3/4)", ui::MouseButton::Right);
    REQUIRE(r.has_value());
    CHECK(r->objects.piece == 1);
    m.objects = app::EditorObjects{};

    // The turn only for a building that turns.
    panel.layout(m);
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        if (w.text == "0\xB0") {
            CHECK(!w.enabled);
        }
    }
    m.object_turns = true;
    r = click(panel, m, "0\xB0");
    REQUIRE(r.has_value());
    CHECK(r->objects.rotation == 1);

    r = click(panel, m, "Merchant", ui::MouseButton::Right);
    REQUIRE(r.has_value());
    CHECK(r->objects.owner == 3);  // back from the first of four players

    // Removing needs none of the building's settings.
    m.objects.tool = app::EditorObjects::Tool::Remove;
    panel.layout(m);
    for (const ui::Form::Widget& w : panel.form().widgets()) {
        if (w.text == "Farms" || w.text == "Merchant") {
            CHECK(!w.enabled);
        }
    }
    // A map without players has nobody to build for.
    m.objects.tool = app::EditorObjects::Tool::Build;
    m.players = 0;
    (void)panel.draw(m, kNoFonts);
    CHECK(says(panel, "no players to build for"));
    CHECK(!click(panel, m, "Back to the map").has_value());
    CHECK(panel.page() == EditorPanel::Page::Document);
}
