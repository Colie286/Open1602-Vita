// SPDX-License-Identifier: GPL-3.0-or-later
// Templates (Open1602): their files, the folder that keeps them and their window
// (engine/app/src/templates.hpp, template_window.hpp).

#include <filesystem>
#include <string>

#include "../test_framework.hpp"
#include "template_window.hpp"
#include "templates.hpp"

#include "o1602/assets/cod_parser.hpp"

using namespace o1602;
using app::BuildTemplate;
using app::BuildTool;
using app::TemplateRequest;
using app::TemplatesModel;
using app::TemplateWindow;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts = {nullptr, nullptr, nullptr};

BuildTool::Layout piece() {
    BuildTool::Layout layout;
    layout.width = 6;
    layout.height = 5;
    layout.items = {{20401, 0, 2, 0, 1, 1}, {21401, 1, 0, 1, 2, 2}, {20605, 4, 3, 3, 2, 2}};
    return layout;
}

bool shows(const std::vector<ui::DrawItem>& items, const std::string& text) {
    for (const ui::DrawItem& d : items) {
        if (d.kind == ui::DrawItem::Kind::Text && d.text.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::optional<TemplateRequest> click(TemplateWindow& w, const TemplatesModel& m, std::int32_t x, std::int32_t y) {
    (void)w.mouse(x, y, true, m);
    return w.mouse(x, y, false, m);
}

TemplatesModel listed(std::size_t count) {
    TemplatesModel m;
    for (std::size_t i = 0; i < count; ++i) {
        app::TemplateEntry e;
        e.name = "Viertel " + std::to_string(i + 1);
        e.width = 6;
        e.height = 5;
        e.buildings = 3;
        e.view = {{0, 0, 2, 2, app::TemplateEntry::Piece::Kind::House}};
        m.entries.push_back(e);
    }
    return m;
}

// A row's buttons: take, rename, delete (design units, scale 1).
constexpr std::int32_t kTakeX = 150;
constexpr std::int32_t kRenameX = 270;
constexpr std::int32_t kRemoveX = 390;
std::int32_t button_y(std::size_t row) {
    return 52 + static_cast<std::int32_t>(row) * 112 + 86;
}

}  // namespace

TEST_CASE("templates: a file keeps the name, the size and the items, and leaves out what it cannot read") {
    BuildTemplate t;
    t.name = "Weber\xE4";  // Windows-1252 "Webera"-umlaut, UTF-8 in the file
    t.layout = piece();
    const std::string text = app::write_template(t);
    CHECK(text.find("name = Weber\xC3\xA4\n") != std::string::npos);
    CHECK(text.find("item = 21401 1 0 1 2 2\n") != std::string::npos);
    const auto back = app::read_template(text);
    REQUIRE(back.has_value());
    CHECK(back->name == t.name);
    CHECK(back->layout.width == 6);
    CHECK(back->layout.height == 5);
    REQUIRE(back->layout.items.size() == 3);
    CHECK(back->layout.items[2].building == 20605);
    CHECK(back->layout.items[2].rotation == 3);

    // Lines of later versions, broken items and items outside the piece are left out.
    const auto edited = app::read_template(
        "# Open1602 template\nname = X\nsize = 4 4\ncolour = blue\n"
        "item = 20401 0 0 0 1 1\nitem = 20401 a 0 0 1 1\n"
        "item = 20401 0 0 7 1 1\nitem = 21401 3 3 0 2 2\n");
    REQUIRE(edited.has_value());
    CHECK(edited->layout.items.size() == 1);
    CHECK(!app::read_template("# Open1602 template\nname = X\nitem = 20401 0 0 0 1 1\n").has_value());  // no size
    CHECK(!app::read_template("size = 3 3\n").has_value());                                             // no item
    CHECK(app::clean_template_name("  Ein\tsehr langer Name, der nicht passt  ") == "Einsehr langer Name, der");
}

TEST_CASE("templates: the folder keeps them as files, sorted by name") {
    const std::filesystem::path folder = test::temp_path("o1602-test-templates");
    std::filesystem::remove_all(folder);
    app::TemplateStore store(folder);
    store.load();  // no folder yet
    CHECK(store.all().empty());
    CHECK(store.next_name() == "Template 1");
    REQUIRE(store.add("Zollhaus", piece()));
    REQUIRE(store.add("", piece()));  // no name: the next one offered
    CHECK(!store.add("Leer", {}));
    REQUIRE(store.all().size() == 2);
    CHECK(store.all()[0].name == "Template 2");
    CHECK(store.all()[1].name == "Zollhaus");
    CHECK(std::filesystem::exists(folder / "1.txt"));
    CHECK(std::filesystem::exists(folder / "2.txt"));

    REQUIRE(store.rename(1, "Am Hafen"));
    CHECK(!store.rename(5, "Nichts"));
    CHECK(!store.rename(0, "   "));

    // Another game reads the same.
    app::TemplateStore other(folder);
    other.load();
    REQUIRE(other.all().size() == 2);
    CHECK(other.all()[0].name == "Am Hafen");
    CHECK(other.all()[0].file == "1.txt");
    CHECK(other.all()[0].layout.items.size() == 3);

    REQUIRE(other.remove(0));
    CHECK(!std::filesystem::exists(folder / "1.txt"));
    REQUIRE(other.add("Neu", piece()));
    CHECK(other.all().size() == 2);
    CHECK(std::filesystem::exists(folder / "3.txt"));  // a number not used before in this folder
    std::filesystem::remove_all(folder);
}

TEST_CASE("template window: saving asks for the name, Enter keeps it and Escape takes it back") {
    TemplateWindow w;
    TemplatesModel m;
    m.piece.width = 6;
    m.piece.height = 5;
    m.piece.buildings = 2;
    w.open_save("Template 3");
    CHECK(w.typing());
    const auto items = w.draw(m, kNoFonts);
    CHECK(shows(items, "Save template"));
    CHECK(shows(items, "6 \xD7 5, 2 buildings"));
    w.type("Hafen");  // replaces the name offered
    w.key(8);
    CHECK(w.text() == "Hafe");
    const auto r = w.key(13);
    REQUIRE(r.has_value());
    CHECK(r->kind == TemplateRequest::Kind::Save);
    CHECK(r->name == "Hafe");
    CHECK(!w.is_open());

    w.open_save("Template 3");
    CHECK(!w.key(27).has_value());
    CHECK(!w.is_open());

    // The button saves too; with no name it cannot.
    w.open_save("Template 3");
    (void)w.draw(m, kNoFonts);
    const auto saved = click(w, m, 60, 180);
    REQUIRE(saved.has_value());
    CHECK(saved->name == "Template 3");
    w.open_save("");
    CHECK(!click(w, m, 60, 180).has_value());
    CHECK(w.is_open());
    CHECK(!click(w, m, 300, 180).has_value());  // cancel
    CHECK(!w.is_open());
}

TEST_CASE("template window: the list takes, renames and deletes after asking again, page by page") {
    TemplateWindow w;
    TemplatesModel m = listed(5);
    w.open_list();
    CHECK(!w.typing());
    auto items = w.draw(m, kNoFonts);
    CHECK(shows(items, "Viertel 4"));
    CHECK(!shows(items, "Viertel 5"));
    CHECK(shows(items, "1/2"));

    const auto take = click(w, m, kTakeX, button_y(1));
    REQUIRE(take.has_value());
    CHECK(take->kind == TemplateRequest::Kind::Take);
    CHECK(take->index == 1);

    // Delete: the first click asks, the second deletes.
    CHECK(!click(w, m, kRemoveX, button_y(2)).has_value());
    CHECK(shows(w.draw(m, kNoFonts), "Sure?"));
    const auto remove = click(w, m, kRemoveX, button_y(2));
    REQUIRE(remove.has_value());
    CHECK(remove->kind == TemplateRequest::Kind::Remove);
    CHECK(remove->index == 2);

    // Rename: typed in the row, Enter keeps it.
    CHECK(!click(w, m, kRenameX, button_y(0)).has_value());
    CHECK(w.typing());
    CHECK(w.text() == "Viertel 1");
    w.type("Markt");
    const auto renamed = w.key(13);
    REQUIRE(renamed.has_value());
    CHECK(renamed->kind == TemplateRequest::Kind::Rename);
    CHECK(renamed->index == 0);
    CHECK(renamed->name == "Markt");
    CHECK(!w.typing());

    // The next page: the fifth template in the first row.
    const std::int32_t h = w.height(m);
    CHECK(!click(w, m, TemplateWindow::kWidth - 70, h - 28).has_value());
    items = w.draw(m, kNoFonts);
    CHECK(shows(items, "Viertel 5"));
    const auto fifth = click(w, m, kTakeX, button_y(0));
    REQUIRE(fifth.has_value());
    CHECK(fifth->index == 4);

    CHECK(!click(w, m, TemplateWindow::kWidth - 80, 24).has_value());  // close
    CHECK(!w.is_open());
}

TEST_CASE("templates: the window's entry counts the buildings and sums what they cost") {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Size: 1, 1\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 1\n  Id: 20605\n  Kind: 14\n  Gfx: 50\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 3\n    Money: 20\n  EndObj\n"
        "  @Nummer: 2\n  Id: 21401\n  Kind: 14\n  Gfx: 72\n  Size: 2, 2\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 2\n    Money: 50\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    const app::TemplateEntry e = app::template_entry("Hof", piece(), table.value());
    CHECK(e.name == "Hof");
    CHECK(e.width == 6);
    CHECK(e.height == 5);
    CHECK(e.buildings == 2);  // the road not counted
    CHECK(e.cost.money == 75);
    CHECK(e.cost.wood == 3 * 32);  // in parts of a ton, as the game counts goods
    CHECK(e.cost.tools == 2 * 32);
    REQUIRE(e.view.size() == 3);
    CHECK(e.view[0].kind == app::TemplateEntry::Piece::Kind::Road);
    CHECK(e.view[1].kind == app::TemplateEntry::Piece::Kind::Building);
    CHECK(e.view[2].kind == app::TemplateEntry::Piece::Kind::House);

    TemplateWindow w;
    w.open_list();
    CHECK(shows(w.draw(TemplatesModel{}, kNoFonts), "No templates yet"));
}
