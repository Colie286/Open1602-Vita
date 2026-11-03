// SPDX-License-Identifier: GPL-3.0-or-later
// The build menu and the player status of the side panel (BT-2) without the original data:
// synthetic BAU and INFO layouts with one gadget per Id, a menu of two categories and two
// buildings, clicked with the mouse as the side panel does.

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "side_panels.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/gad.hpp"
#include "o1602/assets/text_table.hpp"

using namespace o1602;
using app::BuildPanel;
using app::InfoPanel;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts{};

constexpr std::uint32_t kBakery = 20503;
constexpr std::uint32_t kSmith = 20511;
constexpr std::int32_t kDemolishButton = 31001;
constexpr std::int32_t kWorkshops = 31002;
constexpr std::uint32_t kDirtRoad = 20000;
constexpr std::uint32_t kStoneRoad = 20001;
constexpr std::int32_t kRoads = 31003;

/// A layout of `ids`, each a 20x10 box of its own place; `extra` adds fields to a gadget.
assets::GadgetLayout layout(const std::vector<std::int32_t>& ids, const std::map<std::int32_t, std::string>& extra) {
    std::string text = "Objekt: GADGET\n";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto n = static_cast<std::int32_t>(i);
        const auto it = extra.find(ids[i]);
        const std::string fields = it != extra.end() ? it->second : std::string("Kind: 5\n");  // text by default
        text += "  @Nummer: " + std::to_string(i) + "\n  Id: " + std::to_string(ids[i]) + "\n  " + fields +
                "  Pos: " + std::to_string(n % 8 * 30) + ", " + std::to_string(n / 8 * 20) + "\n  Size: 20, 10\n";
    }
    return assets::parse_gad(text + "EndObj\n");
}

std::vector<std::int32_t> range(std::int32_t first, std::int32_t last) {
    std::vector<std::int32_t> out;
    for (std::int32_t id = first; id <= last; ++id) {
        out.push_back(id);
    }
    return out;
}

assets::GadgetLayout bau_layout() {
    std::vector<std::int32_t> ids = range(31001, 31008);
    for (const auto& part :
         {range(31021, 31023), range(31031, 31033), range(31041, 31046), range(31051, 31056), range(31061, 31068)}) {
        ids.insert(ids.end(), part.begin(), part.end());
    }
    std::map<std::int32_t, std::string> extra;
    for (std::int32_t id = 31001; id <= 31008; ++id) {
        extra[id] = "Kind: 2\n  Gfxnr: 1\n  Menuflg: 1\n";
    }
    extra[31023] = "Kind: 2\n  Gfxnr: 2\n  Flipflg: 1\n";
    for (const std::int32_t id : {31021, 31022, 31031, 31032}) {
        extra[id] = "Kind: 2\n  Gfxnr: 3\n";
    }
    for (std::int32_t id = 31061; id <= 31068; ++id) {
        extra[id] = "Kind: 2\n  Gfxnr: 4\n";
    }
    return layout(ids, extra);
}

ui::BuildMenuDefinition menu() {
    ui::BuildMenuDefinition m;
    ui::BuildCategory demolish;
    demolish.button = kDemolishButton;
    demolish.demolish = true;
    ui::BuildCategory workshops;
    workshops.button = kWorkshops;
    workshops.names = "WERK";
    workshops.rows = {{ui::BuildButton{kBakery, 5, 0, {}, {}}, ui::BuildButton{kSmith, 6, 1, {}, {}}}};
    ui::BuildCategory roads;
    roads.button = kRoads;
    roads.names = "WEG";
    roads.rows = {{ui::BuildButton{kDirtRoad, 7, 0, {}, {}}, ui::BuildButton{kStoneRoad, 8, 1, {}, {}}}};
    m.categories = {demolish, workshops, roads};
    m.default_category = kWorkshops;
    return m;
}

assets::BuildingTable buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20503\n  Gfxnr: 0\n  Baugfx: 40\n  Rotate: 1\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 10\n    Rohstoff: 5\n"
        "    Rohmenge: 1\n    Prodmenge: 1\n    Interval: 20\n    Kosten: 25, 10\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 1\n  Id: 20511\n  Gfxnr: 10\n  Baugfx: 50\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 11\n    Rohstoff: 7\n    Workstoff: 8\n"
        "    Rohmenge: 1\n    Prodmenge: 1\n    Interval: 20\n    Kosten: 30, 10\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Ziegel: 2\n    Money: 200\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// The texts a panel draws.
std::vector<std::string> texts_of(const ui::Panel& panel) {
    std::vector<std::string> out;
    for (const ui::DrawItem& d : panel.draw(kNoFonts)) {
        if (d.kind == ui::DrawItem::Kind::Text && !d.text.empty()) {
            out.push_back(d.text);
        }
    }
    return out;
}

bool shows(const ui::Panel& panel, const std::string& text) {
    const std::vector<std::string> t = texts_of(panel);
    return std::find(t.begin(), t.end(), text) != t.end();
}

bool draws_sprite(const ui::Panel& panel, std::int32_t gfx) {
    const std::vector<ui::DrawItem> items = panel.draw(kNoFonts);
    return std::any_of(items.begin(), items.end(),
                       [&](const ui::DrawItem& d) { return d.kind == ui::DrawItem::Kind::Sprite && d.gfx == gfx; });
}

std::pair<std::int32_t, std::int32_t> centre(const ui::Panel& panel, std::int32_t id) {
    const auto at = panel.position(id);
    REQUIRE(at.has_value());
    return {(*at)[0] + 5, (*at)[1] + 5};
}

struct Build {
    assets::TextTable texts = assets::TextTable::parse("[WERK]\nBaecker\nSchmied\n[ABRISS]\nAbreissen\n");
    ui::BuildMenuDefinition definition = menu();
    assets::BuildingTable table = buildings();
    BuildPanel panel{bau_layout(), definition, &texts};
    std::vector<std::uint32_t> locked;
    BuildPanel::Available available = [this](std::uint32_t id) {
        return std::find(locked.begin(), locked.end(), id) == locked.end();
    };

    void refresh(std::uint8_t rotation = 0) {
        panel.refresh(
            table, available, rotation,
            [](std::int32_t, std::int32_t) { return std::optional<std::pair<std::int32_t, std::int32_t>>{{60, 50}}; },
            assets::Goods{});
    }
    std::optional<BuildPanel::Action> press(std::int32_t id) {
        const auto [x, y] = centre(panel.panel(), id);
        bool consumed = false;
        (void)panel.mouse(x, y, false, available, consumed);  // the mouse moves there first
        auto a = panel.mouse(x, y, true, available, consumed);
        CHECK(consumed);
        return a;
    }
    std::optional<BuildPanel::Action> click(std::int32_t id) {
        const auto [x, y] = centre(panel.panel(), id);
        bool consumed = false;
        auto a = panel.mouse(x, y, true, available, consumed);
        auto b = panel.mouse(x, y, false, available, consumed);
        return b ? b : a;
    }
    /// Lets go over icon `index` of the open pop-up.
    std::optional<BuildPanel::Action> release_on(std::size_t index) {
        REQUIRE(panel.popup() != nullptr);
        const auto [x, y] = centre(*panel.popup(), ui::kPopupSlot + static_cast<std::int32_t>(index));
        bool consumed = false;
        (void)panel.mouse(x, y, true, available, consumed);
        auto a = panel.mouse(x, y, false, available, consumed);
        CHECK(consumed);
        return a;
    }
};

}  // namespace

TEST_CASE("side panel build menu: the current building's name, costs, preview and production chain") {
    Build b;
    b.refresh(1);
    const ui::Panel& p = b.panel.panel();
    CHECK(shows(p, "Baecker"));
    for (const char* cost : {"3", "4", "100", "25"}) {
        CHECK(shows(p, cost));
    }
    // The preview turns with view and building (Rotate), centred in its 160x110 box.
    CHECK(draws_sprite(p, 41));
    const auto at = p.position(31032);
    REQUIRE(at.has_value());
    const std::vector<ui::DrawItem> items = p.draw(kNoFonts);
    CHECK(std::any_of(items.begin(), items.end(), [&](const ui::DrawItem& d) {
        return d.kind == ui::DrawItem::Kind::Sprite && d.gfx == 41 && d.x == (*at)[0] + 50 && d.y == (*at)[1] + 30;
    }));
    // A workshop with one raw material: its raw material and its product, no second input.
    CHECK(draws_sprite(p, 192 + 5));
    CHECK(draws_sprite(p, 192 + 10));
    // Frame and arrow of the input and the product's frame; the second input's group stays hidden.
    CHECK(std::count_if(items.begin(), items.end(),
                        [](const ui::DrawItem& d) { return d.kind == ui::DrawItem::Kind::Sprite && d.gfx == 4; }) == 3);
    CHECK(b.panel.current(b.available) == kBakery);
    CHECK(!b.panel.demolish());
}

TEST_CASE("side panel build menu: a category's pop-up picks a building, an unavailable one is not picked") {
    Build b;
    CHECK(!b.press(kWorkshops).has_value());
    REQUIRE(b.panel.popup_open());
    CHECK(b.panel.popup_category() == kWorkshops);
    auto a = b.release_on(1);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Pick);
    CHECK(a->building == kSmith);
    CHECK(!b.panel.popup_open());
    CHECK(b.panel.current(b.available) == kSmith);
    b.refresh();
    CHECK(shows(b.panel.panel(), "Schmied"));
    CHECK(shows(b.panel.panel(), "2"));              // bricks
    CHECK(draws_sprite(b.panel.panel(), 192 + 8));   // second input
    CHECK(draws_sprite(b.panel.panel(), 192 + 7));   // input
    CHECK(draws_sprite(b.panel.panel(), 192 + 11));  // product

    // The smith is locked: its icon picks nothing, and the menu falls back to the bakery.
    b.locked = {kSmith};
    (void)b.press(kWorkshops);
    CHECK(!b.release_on(1).has_value());
    CHECK(b.panel.current(b.available) == kBakery);
}

TEST_CASE("side panel build menu: another category puts its building in hand when pressed") {
    Build b;
    (void)b.press(kWorkshops);
    REQUIRE(b.release_on(1).has_value());  // the smith in hand
    // The roads' category: the panel shows its remembered road, so the map builds that one too,
    // whether an icon is picked, the pop-up closes beside the icons, or the map is clicked.
    auto a = b.press(kRoads);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Pick);
    CHECK(a->building == kDirtRoad);
    CHECK(b.panel.popup_open());
    CHECK(b.panel.current(b.available) == kDirtRoad);
    bool consumed = false;
    (void)b.panel.mouse(600, 600, false, b.available, consumed);  // the mouse moves beside the icons
    (void)b.panel.mouse(600, 600, true, b.available, consumed);
    CHECK(!b.panel.popup_open());
    CHECK(b.panel.current(b.available) == kDirtRoad);
    // The same category again changes nothing in hand; its pop-up picks as before.
    CHECK(!b.press(kRoads).has_value());
    a = b.release_on(1);
    REQUIRE(a.has_value());
    CHECK(a->building == kStoneRoad);
    // Back to the workshops: the smith remembered there is in hand again.
    a = b.press(kWorkshops);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Pick);
    CHECK(a->building == kSmith);
    REQUIRE(b.release_on(1).has_value());  // the smith again, the pop-up closes
    // Nothing of a category available: the tool is put away.
    b.locked = {kDirtRoad, kStoneRoad};
    a = b.press(kRoads);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Off);
}

TEST_CASE("side panel build menu: with a pop-up open, one press opens another category") {
    Build b;
    // A press and release on the workshops' button, the mouse not moving in between: the pop-up
    // takes the release, the button is let go of all the same.
    const auto [wx, wy] = centre(b.panel.panel(), kWorkshops);
    bool consumed = false;
    (void)b.panel.mouse(wx, wy, true, b.available, consumed);
    (void)b.panel.mouse(wx, wy, false, b.available, consumed);
    REQUIRE(b.panel.popup_open());
    REQUIRE(b.panel.popup_category() == kWorkshops);
    // The next press on the roads (no move first, the pop-up had the mouse) opens them at once.
    const auto [rx, ry] = centre(b.panel.panel(), kRoads);
    const auto a = b.panel.mouse(rx, ry, true, b.available, consumed);
    CHECK(consumed);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Pick);
    CHECK(a->building == kDirtRoad);
    CHECK(b.panel.popup_open());
    CHECK(b.panel.popup_category() == kRoads);
}

TEST_CASE("side panel build menu: a pop-up stands above all category buttons") {
    // Two rows of buttons as in the game: the roads below the workshops.
    assets::GadgetLayout bau = bau_layout();
    for (assets::Gadget& g : bau.gadgets) {
        if (g.id == kRoads) {
            g.pos[1] = 200;
        } else if (g.id >= 31001 && g.id <= 31008) {
            g.pos[1] = 130;
        }
    }
    const assets::TextTable texts = assets::TextTable::parse("[WERK]\nBaecker\nSchmied\n");
    const ui::BuildMenuDefinition definition = menu();
    BuildPanel panel{bau, definition, &texts};
    const BuildPanel::Available all = [](std::uint32_t) { return true; };
    const auto [x, y] = centre(panel.panel(), kRoads);
    bool consumed = false;
    (void)panel.mouse(x, y, true, all, consumed);
    REQUIRE(panel.popup() != nullptr);
    // The pop-up of the lower row ends above the upper row's buttons, which stay free to click.
    const std::vector<ui::DrawItem> drawn = panel.popup()->draw(kNoFonts);
    for (const ui::DrawItem& d : drawn) {
        if (d.kind == ui::DrawItem::Kind::Sprite) {
            CHECK(d.y <= 135);  // the bottom strip and its shadow touch the buttons, as in the original
        }
    }
    (void)panel.mouse(x, y, false, all, consumed);
    const auto [wx, wy] = centre(panel.panel(), kWorkshops);
    const auto a = panel.mouse(wx, wy, true, all, consumed);
    REQUIRE(a.has_value());
    CHECK(a->building == kBakery);
    CHECK(panel.popup_category() == kWorkshops);
}

TEST_CASE("side panel build menu: a right click deselects the category, the menu picks it up again") {
    Build b;
    (void)b.press(kWorkshops);
    REQUIRE(b.release_on(1).has_value());  // the smith in hand
    (void)b.press(kWorkshops);
    REQUIRE(b.panel.popup_open());
    // Deselected: the pop-up closes, nothing is in hand, the panel shows no building.
    CHECK(b.panel.deselect());
    CHECK(b.panel.deselected());
    CHECK(!b.panel.popup_open());
    CHECK(!b.panel.panel().selected(kWorkshops));
    CHECK(!b.panel.current(b.available).has_value());
    CHECK(!b.panel.demolish());
    b.refresh();
    CHECK(!shows(b.panel.panel(), "Schmied"));
    CHECK(!b.panel.deselect());  // nothing more to deselect: the next right click leaves the menu
    // A category pressed now puts its building in hand.
    auto a = b.press(kRoads);
    REQUIRE(a.has_value());
    CHECK(a->building == kDirtRoad);
    CHECK(b.panel.deselect());
    // The build menu opened again picks up the category deselected last, with its building.
    b.panel.reselect();
    CHECK(!b.panel.deselected());
    CHECK(b.panel.panel().selected(kRoads));
    CHECK(b.panel.current(b.available) == kDirtRoad);
}

TEST_CASE("side panel build menu: the blueprint switch goes back to the building in hand and names it") {
    Build b;
    (void)b.press(kWorkshops);
    REQUIRE(b.release_on(1).has_value());  // the smith
    (void)b.press(kDemolishButton);
    REQUIRE(b.panel.popup_open());
    const auto a = b.release_on(2);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Blueprint);
    CHECK(!b.panel.popup_open());
    CHECK(!b.panel.demolish());
    CHECK(b.panel.current(b.available) == kSmith);
    b.panel.set_blueprint(true);
    b.refresh();
    CHECK(shows(b.panel.panel(), "Blueprint: Schmied"));
    b.panel.set_blueprint(false);
    b.refresh();
    CHECK(!shows(b.panel.panel(), "Blueprint: Schmied"));
    // With the copy tool in hand the menu stays at the tools.
    (void)b.press(kDemolishButton);
    REQUIRE(b.release_on(1).has_value());
    (void)b.press(kDemolishButton);
    REQUIRE(b.release_on(2).has_value());
    CHECK(b.panel.copy());
}

TEST_CASE("side panel build menu: the move tool is the fourth tool, offered where the game allows it") {
    Build b;
    (void)b.press(kDemolishButton);
    REQUIRE(b.panel.popup_open());
    const auto a = b.release_on(3);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Move);
    CHECK(b.panel.moving());
    CHECK(!b.panel.copy());
    b.refresh();
    CHECK(shows(b.panel.panel(), "Move buildings"));
    // Off in the game's settings: the icon picks nothing.
    b.panel.set_moving(false);
    (void)b.press(kDemolishButton);
    REQUIRE(b.release_on(1).has_value());  // the copy tool
    (void)b.press(kDemolishButton);
    CHECK(!b.release_on(3).has_value());
    CHECK(b.panel.copy());
}

TEST_CASE("side panel build menu: the templates are the fifth tool and take the copy tool") {
    Build b;
    (void)b.press(kDemolishButton);
    REQUIRE(b.panel.popup_open());
    // The row of one icon has the middle of its picture covered; the full row needs no patch.
    CHECK(!b.panel.popup()->position(ui::kPopupPatch).has_value());
    REQUIRE(b.panel.popup()->position(ui::kPopupPatch + 1).has_value());
    const std::vector<ui::DrawItem> drawn = b.panel.popup()->draw(kNoFonts);
    CHECK(std::any_of(drawn.begin(), drawn.end(), [](const ui::DrawItem& d) {
        return d.first_col == ui::kPopupPatchFrom && d.cols == ui::kPopupPatchWidth;
    }));
    const auto a = b.release_on(4);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Templates);
    CHECK(!b.panel.popup_open());
    CHECK(b.panel.copy());
    const ui::BuildCategory tools = b.panel.tools_category(0);
    REQUIRE(tools.rows.size() == 2);  // four icons a row
    REQUIRE(tools.rows[1].size() == 1);
    CHECK(tools.rows[1][0].picture == "open1602:templates");
    CHECK(tools.rows[1][0].name == "Templates");
}

TEST_CASE("side panel build menu: demolish, rotate and orient to the road") {
    Build b;
    // The demolish button opens the tools' pop-up (Open1602): demolish and copy. The tool
    // remembered there (demolish) is in hand at once.
    auto a = b.press(kDemolishButton);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Demolish);
    CHECK(b.panel.popup_open());
    a = b.release_on(1);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Copy);
    CHECK(b.panel.copy());
    CHECK(!b.panel.demolish());
    (void)b.press(kDemolishButton);
    a = b.release_on(0);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Demolish);
    CHECK(b.panel.demolish());
    CHECK(!b.panel.copy());
    // A key starts a tool: the menu shows it.
    (void)b.press(kWorkshops);
    b.panel.show_tool(BuildPanel::Tool::Copy);
    CHECK(!b.panel.popup_open());
    CHECK(b.panel.copy());
    b.panel.show_tool(BuildPanel::Tool::Demolish);
    CHECK(b.panel.demolish());
    CHECK(!b.panel.current(b.available).has_value());
    bool consumed = false;
    (void)b.panel.mouse(0, 0, false, b.available, consumed);
    b.refresh();
    CHECK(shows(b.panel.panel(), "Abreissen"));
    CHECK(!shows(b.panel.panel(), "100"));  // no costs

    a = b.click(31021);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::Rotate);
    CHECK(a->step == -1);
    a = b.click(31022);
    REQUIRE(a.has_value());
    CHECK(a->step == 1);
    a = b.click(31023);
    REQUIRE(a.has_value());
    CHECK(a->kind == BuildPanel::Action::Kind::OrientToRoad);
    CHECK(a->step == 1);
    a = b.click(31023);
    REQUIRE(a.has_value());
    CHECK(a->step == 0);
}

TEST_CASE("side panel build menu: show() opens a building's category, help names gadgets and icons") {
    Build b;
    (void)b.press(kDemolishButton);
    bool consumed = false;
    (void)b.panel.mouse(0, 0, false, b.available, consumed);
    CHECK(b.panel.show(kSmith));
    CHECK(b.panel.current(b.available) == kSmith);
    CHECK(!b.panel.show(99999));

    std::vector<std::string> leiste(30);
    for (std::size_t i = 0; i < leiste.size(); ++i) {
        leiste[i] = "line " + std::to_string(i);
    }
    const auto [x, y] = centre(b.panel.panel(), 31021);
    (void)b.panel.mouse(x, y, false, b.available, consumed);
    CHECK(b.panel.help(leiste) == "line 13");
    const auto [cx, cy] = centre(b.panel.panel(), kWorkshops);
    (void)b.panel.mouse(cx, cy, false, b.available, consumed);
    CHECK(b.panel.help(leiste) == "line 24");
    // Over an icon of the open pop-up: the building's name.
    (void)b.panel.mouse(cx, cy, true, b.available, consumed);
    REQUIRE(b.panel.popup() != nullptr);
    const auto [ix, iy] = centre(*b.panel.popup(), ui::kPopupSlot);
    (void)b.panel.mouse(ix, iy, true, b.available, consumed);
    CHECK(b.panel.help(leiste) == "Baecker");
}

namespace {

struct Info {
    assets::TextTable texts = assets::TextTable::parse("[GAME]\n" + [] {
        std::string s;
        for (int i = 0; i < 90; ++i) {
            s += "game " + std::to_string(i) + "\n";
        }
        return s;
    }());
    InfoPanel panel{layout(range(57002, 57033), {{57003, "Kind: 2\n  Gfxnr: 60\n"},
                                                 {57004, "Kind: 2\n  Gfxnr: 60\n"},
                                                 {57025, "Kind: 2\n  Gfxnr: 70\n"},
                                                 {57028, "Kind: 2\n  Gfxnr: 3\n"},
                                                 {57031, "Kind: 2\n  Gfxnr: 3\n"},
                                                 {57032, "Kind: 2\n  Gfxnr: 3\n"},
                                                 {57033, "Kind: 2\n  Gfxnr: 3\n"}}),
                    &texts};

    void click(std::int32_t id, std::optional<app::Selection::Kind>* list = nullptr) {
        const auto [x, y] = centre(panel.panel(), id);
        (void)panel.mouse(x, y, true);
        const auto l = panel.mouse(x, y, false);
        if (list != nullptr) {
            *list = l;
        }
    }
};

}  // namespace

TEST_CASE("side panel player status: the figures, the balance icon and the labels") {
    Info i;
    sim::PlayerFigures f;
    f.inhabitants = 1234;
    f.taxes = 56;
    f.running_costs = 78;
    f.balance = -9;
    i.panel.refresh(f, nullptr);
    const ui::Panel& p = i.panel.panel();
    CHECK(shows(p, "game 38"));  // the title
    CHECK(shows(p, "game 15"));  // Einwohner
    CHECK(shows(p, "1234"));
    CHECK(shows(p, "56"));
    CHECK(shows(p, "78"));
    CHECK(shows(p, "-9"));
    CHECK(draws_sprite(p, 71));  // red: the balance is negative
    f.balance = 9;
    i.panel.refresh(f, nullptr);
    CHECK(draws_sprite(i.panel.panel(), 70));
    i.panel.set_points(4321, true);
    CHECK(shows(i.panel.panel(), "4321"));
}

TEST_CASE("side panel player status: the list buttons, the rating and the goods balance line") {
    Info i;
    std::optional<app::Selection::Kind> list;
    i.click(57031, &list);
    CHECK(list == app::Selection::Kind::Diplomacy);
    i.click(57032, &list);
    CHECK(list == app::Selection::Kind::ShipList);
    i.click(57033, &list);
    CHECK(list == app::Selection::Kind::CityList);
    i.panel.set_points(0, true);
    i.click(57028);
    CHECK(i.panel.take_rating());
    CHECK(!i.panel.take_rating());
    i.click(57040);  // Open1602's own line below "Punkte"
    CHECK(i.panel.take_goods());

    std::vector<std::string> leiste(220);
    leiste[216] = "diplomacy help";
    const auto [x, y] = centre(i.panel.panel(), 57031);
    (void)i.panel.mouse(x, y, false);
    CHECK(i.panel.help(leiste) == "diplomacy help");

    // The goods balance line lights up under the mouse, like a link, and not elsewhere.
    CHECK(!i.panel.panel().selected(57040));
    const auto [gx, gy] = centre(i.panel.panel(), 57040);
    (void)i.panel.mouse(gx, gy, false);
    CHECK(i.panel.panel().selected(57040));
    (void)i.panel.mouse(x, y, false);
    CHECK(!i.panel.panel().selected(57040));
}
