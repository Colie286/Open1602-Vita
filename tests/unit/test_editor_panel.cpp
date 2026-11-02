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
    CHECK(shows(panel.draw(m, kNoFonts), "18, made at the start"));
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
