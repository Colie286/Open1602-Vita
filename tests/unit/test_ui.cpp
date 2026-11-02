// SPDX-License-Identifier: GPL-3.0-or-later
// Interface layouts (GAD), fonts and text, panels at work (docs/formats/gad.md).

#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/gad.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/font.hpp"
#include "o1602/ui/form.hpp"
#include "o1602/ui/key_bindings.hpp"
#include "o1602/ui/panel.hpp"
#include "o1602/ui/resources.hpp"
#include "o1602/ui/text_format.hpp"

using namespace o1602;

namespace {

/// A font of 96 glyphs (codes 32..127), each 4 pixels wide and 6 high; 'W' (code 87) is 8 wide.
ui::Font test_font() {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes(96, {4, 6});
    sizes['W' - 32] = {8, 6};
    const auto sheet = test::make_sheet(sizes, 1);
    auto font = ui::Font::from_sheet(sheet);
    REQUIRE(font.ok());
    return std::move(font).value();
}

constexpr const char* kInclude =
    "  BLK_TOOL = 0\n"
    "  GFXPFEIL = 1\n";

constexpr const char* kLayout =
    "  Include: \"TOOLS.INC\"\n"
    "  Objekt: GADGET\n"
    "    @Nummer:    0\n"
    "    Id:         0\n"
    "    Blocknr:    BLK_TOOL\n"
    "    Kind:       GAD_UNUSED\n"
    "    ObjFill:    0,MAXGADGET\n"
    "    IDHAUPT = 500\n"
    "    @Nummer:    0\n"
    "    BASE = Nummer\n"
    "    Id:         IDHAUPT+0\n"
    "    Kind:       GAD_GFX\n"
    "    Gfxnr:      GFXPFEIL\n"
    "    Pressoff:   2\n"
    "    Flipflg:    1\n"
    "    Color:      1, 3\n"
    "    Pos:        10, 20\n"
    "    Size:       40, 30\n"
    "    Posoffs:    12-Pos[0], 25-Pos[1]\n"
    "    @Nummer:    +1\n"
    "    Id:         IDHAUPT+1\n"
    "    Kind:       GAD_TEXTZ\n"
    "    Color:      5\n"
    "    Reiheflg:   1\n"
    "    Basenr:     BASE+1\n"
    "    Pos:        0, 100\n"
    "    Size:       50, 20\n"
    "    @Nummer:    +1\n"
    "    Id:         IDHAUPT+2\n"
    "    Kind:       GAD_TEXTZ\n"
    "    Reiheflg:   1\n"
    "    Basenr:     BASE+1\n"
    "    @Pos:       +50\n"
    "    Size:       50, 20\n"
    "    @Nummer:    +1\n"
    "    Id:         IDHAUPT+3\n"
    "    Kind:       GAD_BALKEN\n"
    "    Slidhorflg: 1\n"
    "    Pos:        0, 200\n"
    "    Size:       100, 10\n"
    "  EndObj\n";

assets::GadgetLayout layout() {
    return assets::parse_gad(kLayout, [](std::string_view name) -> std::optional<std::string> {
        return name == "TOOLS.INC" ? std::optional<std::string>(kInclude) : std::nullopt;
    });
}

}  // namespace

TEST_CASE("ui: a GAD layout with include, relative numbers and pairs that keep their second value") {
    const auto l = layout();
    REQUIRE(l.gadgets.size() == 4);  // the highest Nummer, not the 300 defaults of ObjFill
    const assets::Gadget& arrow = l.gadgets[0];
    CHECK(arrow.id == 500);
    CHECK(arrow.kind == assets::GadgetKind::Graphic);
    CHECK(arrow.gfx == 1);
    CHECK(arrow.toggle);
    CHECK(arrow.offset[0] == 2);
    CHECK(arrow.offset[1] == 5);
    const assets::Gadget& text = l.gadgets[1];
    CHECK(text.colour[0] == 5);
    CHECK(text.colour[1] == 3);  // kept from the line before
    CHECK(text.base == 1);
    // "@Pos: +50" moves only x.
    CHECK(l.gadgets[2].pos[0] == 50);
    CHECK(l.gadgets[2].pos[1] == 100);
    CHECK(l.gadgets[3].slider);
    CHECK(l.gadgets[3].slider_size[0] == 100);  // a range of 0 is the size
    CHECK(l.find(502) == &l.gadgets[2]);
    CHECK(l.warnings.empty());
    const auto missing = assets::parse_gad(kLayout);
    CHECK(!missing.warnings.empty());
}

TEST_CASE("ui: a GAD file that includes itself again and again stays bounded") {
    // Each level includes the next 40 times: 40^4 copies of the text without a limit.
    std::string bomb;
    for (int i = 0; i < 40; ++i) {
        bomb += "Include: \"BOMB.INC\"\n";
    }
    bomb += std::string(1000, ';') + "\n";
    const auto l = assets::parse_gad(bomb, [&](std::string_view) -> std::optional<std::string> { return bomb; });
    CHECK(!l.warnings.empty());
}

TEST_CASE("ui: a GAD file with a huge Nummer keeps at most kMaxGadgets gadgets") {
    const auto l = assets::parse_gad(
        "Objekt: GADGET\n  @Nummer: 2000000000\n  Id: 7\n  Kind: 1\n  @Nummer: 3\n"
        "  Id: 8\n  Kind: 1\nEndObj\n");
    CHECK(l.gadgets.size() == static_cast<std::size_t>(assets::kMaxGadgets));  // not 2 billion (HI-2)
    CHECK(l.find(8) == &l.gadgets[3]);
    CHECK(l.find(7) == nullptr);  // beyond the limit, left out with a warning
    CHECK(!l.warnings.empty());
}

TEST_CASE("ui: regions of the screen and the original's rounding") {
    const auto r = ui::screen_regions(1024, 768);
    CHECK((r.side_panel == ui::Rect{768, 0, 256, 768}));
    CHECK((r.status_bar == ui::Rect{0, 734, 768, 34}));
    CHECK((r.tutor == ui::Rect{0, 594, 768, 140}));
    CHECK((r.game_view == ui::Rect{0, 0, 768, 734}));
    const auto s = ui::screen_regions(800, 600);
    CHECK(s.side_panel.w == 200);
    CHECK(ui::to_pixels(10, 0.78125) == 8);
    CHECK(ui::original_art_size(1280) == ui::ArtSize::Medium);
    CHECK(ui::art_size(1280) == ui::ArtSize::Large);
    CHECK(ui::sheet_file(0, ui::ArtSize::Medium) == "TOOLGFX/TOOLS8.BSH");
    CHECK(ui::font_file(2, ui::ArtSize::Small) == "TOOLGFX/ZEI20V.ZEI");
    // Text takes the largest fonts that are not shrunk at its scale.
    CHECK(ui::text_art_size(2.0) == ui::ArtSize::Large);
    CHECK(ui::text_art_size(1.0) == ui::ArtSize::Large);
    CHECK(ui::text_art_size(0.99) == ui::ArtSize::Medium);
    CHECK(ui::text_art_size(600.0 / 768.0) == ui::ArtSize::Medium);
    CHECK(ui::text_art_size(0.7) == ui::ArtSize::Small);
    CHECK(ui::text_art_size(480.0 / 768.0) == ui::ArtSize::Small);
    CHECK(ui::text_art_size(0.5) == ui::ArtSize::Small);
}

TEST_CASE("ui: a panel draws by state and answers the mouse") {
    ui::Panel panel(layout(), 1.0);
    panel.set_text(501, "Ja");
    const std::array<const ui::Font*, ui::kFontSlots> fonts{};
    auto items = panel.draw(fonts);
    REQUIRE(items.size() == 3);  // the arrow, one text (the other is empty), the bar
    CHECK(items[0].kind == ui::DrawItem::Kind::Sprite);
    CHECK(items[0].gfx == 1);
    CHECK(items[0].x == 12);
    CHECK(items[0].y == 25);
    CHECK(items[1].kind == ui::DrawItem::Kind::Text);
    CHECK(items[1].align == ui::Align::Centre);
    CHECK(items[1].colour == 5);
    CHECK(items[2].w == 0);  // the bar follows its slider
    // Pressing the arrow shows the pressed picture; releasing on it clicks and toggles it.
    CHECK(panel.mouse(20, 30, true).empty());
    CHECK(panel.draw(fonts)[0].gfx == 3);
    auto ev = panel.mouse(20, 30, false);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == ui::PanelEvent::Kind::Clicked);
    CHECK(ev[0].id == 500);
    CHECK(panel.selected(500));
    // Released elsewhere: no click.
    panel.mouse(20, 30, true);
    CHECK(panel.mouse(300, 300, false).empty());
    CHECK(panel.selected(500));
    // Radio group: one of the two texts.
    panel.mouse(10, 110, true);
    panel.mouse(10, 110, false);
    CHECK(panel.selected(501));
    panel.mouse(60, 110, true);
    panel.mouse(60, 110, false);
    CHECK(panel.selected(502));
    CHECK(!panel.selected(501));
    CHECK(panel.hovered() == std::optional<std::int32_t>(502));
    // The slider (bar) follows the mouse while held and scales to a range.
    ev = panel.mouse(40, 205, true);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == ui::PanelEvent::Kind::Slid);
    CHECK(panel.slider(503, 0, 1000) == 400);
    panel.mouse(250, 205, true);
    CHECK(panel.slider(503, 0, 1000) == 1000);
    panel.mouse(250, 205, false);
    panel.set_slider(503, 0, 250, 1000);
    CHECK(panel.draw(fonts)[2].w == 25);
    // Disabled: a checkerboard over the gadget.
    panel.set_disabled(500, true);
    items = panel.draw(fonts);
    CHECK(items[1].kind == ui::DrawItem::Kind::SpriteRaster);
    // Faded: the gadget itself on every second pixel, no black checkerboard.
    panel.set_disabled(500, false);
    const std::size_t plain = panel.draw(fonts).size();
    panel.set_faded(500, true);
    items = panel.draw(fonts);
    CHECK(items.size() == plain);
    CHECK(items[0].faded);
    CHECK(items[0].kind == ui::DrawItem::Kind::Sprite);
    panel.set_faded(500, false);
    CHECK(!panel.draw(fonts)[0].faded);
}

TEST_CASE("ui: a graphic set by the code also replaces the layout's in its shadow") {
    // The build menu: the preview (GAD_GFX) and its shadow (GAD_GFXFILL) show the chosen building,
    // never the layout's first graphic (the wooden wall).
    const auto shadow = assets::parse_gad(
        "  Objekt: GADGET\n"
        "    @Nummer:    0\n"
        "    Id:         31031\n"
        "    Kind:       GAD_GFXFILL\n"
        "    Gfxnr:      7\n"
        "    @Nummer:    +1\n"
        "    Id:         31032\n"
        "    Kind:       GAD_GFX\n"
        "    Gfxnr:      7\n"
        "  EndObj\n",
        [](std::string_view) -> std::optional<std::string> { return std::nullopt; });
    ui::Panel panel(shadow, 1.0);
    panel.set_graphic(31031, 42, 0);
    panel.set_graphic(31032, 42, 0);
    const auto items = panel.draw({});
    REQUIRE(items.size() == 2);
    CHECK(items[0].kind == ui::DrawItem::Kind::SpriteMask);
    CHECK(items[0].gfx == 42);
    CHECK(items[1].gfx == 42);
}

TEST_CASE("ui: a graphic set with a colour tint draws its sprite tinted, the pressed one too") {
    // A message icon in a further player colour: the white variant recoloured (ColourTint); the
    // shadow (GAD_GFXFILL) is only a shape and carries none. Setting the graphic again drops it.
    const auto layout = assets::parse_gad(
        "  Objekt: GADGET\n"
        "    @Nummer:    0\n"
        "    Id:         100011\n"
        "    Kind:       GAD_GFX\n"
        "    Gfxnr:      1\n"
        "    @Nummer:    +1\n"
        "    Id:         100012\n"
        "    Kind:       GAD_GFXFILL\n"
        "    Gfxnr:      1\n"
        "  EndObj\n",
        [](std::string_view) -> std::optional<std::string> { return std::nullopt; });
    ui::Panel panel(layout, 1.0);
    const ui::ColourTint tint{assets::Rgba{0, 230, 90, 255}, 1, 4, 3};
    panel.set_graphic(100011, 257, 6, tint);
    panel.set_graphic(100012, 257, 0, tint);
    auto items = panel.draw({});
    REQUIRE(items.size() == 2);
    CHECK(items[0].gfx == 257);
    CHECK(items[0].tint == std::optional<ui::ColourTint>(tint));
    CHECK(!items[1].tint);
    panel.set_graphic(100011, 254, 6);
    items = panel.draw({});
    CHECK(items[0].gfx == 254);
    CHECK(!items[0].tint);
}

TEST_CASE("ui: text width, colours, wrapping with hyphens and hard breaks") {
    const ui::Font font = test_font();
    CHECK(font.height() == 6);
    CHECK(font.width("abc") == 12);
    CHECK(font.width("aW") == 12);
    CHECK(font.width("ab\ncd") == 8);  // up to the control character
    // Lines of at most 20 pixels (5 characters).
    CHECK((ui::wrap_text(font, "aa bb cc", 20) == std::vector<std::string>{"aa bb", "cc"}));
    CHECK((ui::wrap_text(font, "aa\n\nbb", 100) == std::vector<std::string>{"aa", "bb"}));  // several breaks: one
    CHECK((ui::wrap_text(font, "abc--def", 100) == std::vector<std::string>{"abcdef"}));
    CHECK((ui::wrap_text(font, "x abc--def", 24) == std::vector<std::string>{"x abc-", "def"}));
    CHECK((ui::wrap_text(font, "abcdefgh", 12) == std::vector<std::string>{"abc", "def", "gh"}));
    // A line starting with a byte from 0x80 keeps it after a hard break (U-017).
    CHECK((ui::wrap_text(font, "a\n\xC4z", 100).back() == std::string("\xC4z")));
    CHECK((ui::wrap_text(font, "aa bb cc", 20000) == std::vector<std::string>{"aa bb cc"}));
    // Drawing: glyph index 1 becomes the colour's body.
    assets::IndexedImage canvas;
    canvas.width = 20;
    canvas.height = 10;
    canvas.indices.assign(200, 0);
    canvas.opaque.assign(200, 0);
    ui::Recolour colour{};
    colour[1] = 42;
    CHECK(ui::draw_text(canvas, font, "ab", 10, 8, colour, ui::Align::Centre) == 8);
    CHECK(canvas.indices[2 * 20 + 6] == 42);  // top row 8 - 6 = 2, left 10 - 4
    CHECK(canvas.opaque[2 * 20 + 5] == 0);
}

TEST_CASE("ui: a flowing text scrolls no further than its last full window (UI-12)") {
    // 20 pixels wide (5 characters of the test font), two lines of 6.
    const auto flow = assets::parse_gad(
        "  Objekt: GADGET\n"
        "    @Nummer:    0\n"
        "    Id:         110002\n"
        "    Kind:       GAD_TEXTFL\n"
        "    Blocknr:    0\n"
        "    Size:       20, 12\n"
        "    Posoffs:    0, 6\n"
        "  EndObj\n",
        [](std::string_view) -> std::optional<std::string> { return std::nullopt; });
    const ui::Font font = test_font();
    const std::array<const ui::Font*, ui::kFontSlots> fonts{&font, nullptr, nullptr};
    ui::Panel panel(flow, 1.0);
    CHECK(panel.max_scroll(110002, fonts) == 0);  // no text yet
    panel.set_text(110002, "aa bb cc dd ee");     // three lines
    CHECK(panel.max_scroll(110002, fonts) == 1);
    CHECK(panel.max_scroll(110002, {}) == 0);  // no font: nothing drawn
    CHECK(panel.max_scroll(4711, fonts) == 0);
    panel.set_scroll(110002, panel.max_scroll(110002, fonts));
    const auto items = panel.draw(fonts);
    REQUIRE(items.size() == 2);
    CHECK(items[0].text == "cc dd");
    CHECK(items[1].text == "ee");
    panel.set_text(110002, "aa");
    CHECK(panel.max_scroll(110002, fonts) == 0);
}

TEST_CASE("ui: a text gadget set to fit cuts its text to the gadget's width") {
    // 20 pixels wide: 5 characters of the test font.
    const auto row = assets::parse_gad(
        "  Objekt: GADGET\n"
        "    @Nummer:    0\n"
        "    Id:         31021\n"
        "    Kind:       GAD_TEXTL\n"
        "    Blocknr:    0\n"
        "    Size:       20, 12\n"
        "  EndObj\n",
        [](std::string_view) -> std::optional<std::string> { return std::nullopt; });
    const ui::Font font = test_font();
    const std::array<const ui::Font*, ui::kFontSlots> fonts{&font, nullptr, nullptr};
    ui::Panel panel(row, 1.0);
    panel.set_text(31021, "abcdefgh");
    REQUIRE(panel.draw(fonts).size() == 1);
    CHECK(panel.draw(fonts)[0].text == "abcdefgh");  // the original's texts are drawn whole
    panel.set_fit(31021, true);
    CHECK(panel.draw(fonts)[0].text == ui::fit_text(&font, "abcdefgh", 20, false));
    CHECK(panel.draw(fonts)[0].text.ends_with("..."));
    panel.set_text(31021, "abc");
    CHECK(panel.draw(fonts)[0].text == "abc");  // what fits stays as it is
    panel.set_fit(31021, true, 8);              // less room: something to the right of the text
    CHECK(panel.draw(fonts)[0].text == ui::fit_text(&font, "abc", 8, false));
    CHECK(panel.draw(fonts)[0].text != "abc");
}

TEST_CASE("ui: the build menu's categories and the pop-up of a category") {
    const ui::BuildMenuDefinition& menu = ui::original_build_menu();
    CHECK(menu.categories.size() == 8);
    CHECK(menu.default_category == 31006);
    const ui::BuildCategory* farms = menu.category(31006);
    REQUIRE(farms != nullptr);
    CHECK(farms->names == "FARM");
    CHECK(farms->size() == 19);
    CHECK(farms->at(0) == std::optional<std::uint32_t>(22003));   // Jagdhütte
    CHECK(farms->at(18) == std::optional<std::uint32_t>(21337));  // Kakaofeld
    CHECK(!farms->at(19));
    CHECK(farms->index_of(21515) == std::optional<std::size_t>(5));  // Schaffarm
    CHECK(menu.category_of(20805) == menu.category(31008));          // Kirche: Öffentliche Gebäude
    CHECK(menu.category(31004)->demolish);
    CHECK(menu.category(31004)->size() == 0);
    // Every building is listed once.
    std::size_t total = 0;
    for (const auto& c : menu.categories) {
        total += c.size();
    }
    CHECK(total == 10 + 8 + 4 + 16 + 19 + 8 + 16);

    // The pop-up of the farms: the first three and the sheep farm are available.
    std::vector<bool> ok(farms->size(), false);
    ok[0] = ok[1] = ok[2] = ok[5] = true;
    assets::Gadget button;
    button.id = 31006;
    button.pos = {72, 692};
    const auto layout = ui::build_popup(*farms, ok, button, 81, 43);
    // Two rows (the others have nothing to pick), the first next to the button.
    const assets::Gadget* backdrop = layout.find(ui::kPopupBackdrop);
    REQUIRE(backdrop != nullptr);
    CHECK(backdrop->menu);
    CHECK(layout.find(32901) != nullptr);
    CHECK(layout.find(32902) != nullptr);
    CHECK(layout.find(32903) == nullptr);
    CHECK(layout.find(32901)->pos[1] == 692 - 59);
    CHECK(layout.find(32902)->pos[1] == 692 - 2 * 59);
    CHECK(layout.find(32953)->pos[1] == 692 - 2 * 59 - 5);  // the strip above
    CHECK(layout.find(32952)->kind == assets::GadgetKind::GraphicRaster);
    // Icons: the category's sprites, "?" where nothing can be picked; frames only for those.
    const assets::Gadget* hunter = layout.find(ui::kPopupIcon + 0);
    REQUIRE(hunter != nullptr);
    CHECK(hunter->gfx == 81 + 44);
    CHECK(hunter->pos == (std::array<std::int32_t, 2>{16, 692 - 59 + 5}));
    CHECK(layout.find(ui::kPopupIcon + 3)->gfx == 81 + 43);  // grain farm: not yet
    CHECK(layout.find(ui::kPopupIcon + 5)->gfx == 81 + 44 + 5);
    CHECK(layout.find(ui::kPopupIcon + 5)->pos[0] == 16 + 2 * 57);
    CHECK(layout.find(ui::kPopupIcon + 7) == nullptr);  // row 3 is not shown
    const assets::Gadget* slot = layout.find(ui::kPopupSlot + 1);
    REQUIRE(slot != nullptr);
    CHECK(!slot->no_select);
    CHECK(slot->select_on_move);
    CHECK(slot->press_offset == 1);
    CHECK(layout.find(ui::kPopupSlot + 3)->no_select);

    // A click on a frame: the panel says which.
    ui::Panel popup(layout, 1.0);
    (void)popup.mouse(16 + 57 + 10, 692 - 59 + 20, true);
    const auto events = popup.mouse(16 + 57 + 10, 692 - 59 + 20, false);
    REQUIRE(events.size() == 1);
    CHECK(events[0].id == ui::kPopupSlot + 1);
    // A press beside the icons: the backdrop.
    const auto beside = popup.mouse(5, 100, true);
    REQUIRE(beside.size() == 1);
    CHECK(beside[0].id == ui::kPopupBackdrop);
    CHECK(beside[0].kind == ui::PanelEvent::Kind::Pressed);

    // Nothing available: the last row, all "?".
    const auto none = ui::build_popup(*farms, std::vector<bool>(farms->size(), false), button, 81, 43);
    CHECK(none.find(32901) != nullptr);
    CHECK(none.find(32902) == nullptr);
    CHECK(none.find(ui::kPopupIcon + 15)->gfx == 81 + 43);
    CHECK(none.find(ui::kPopupIcon + 0) == nullptr);
}

TEST_CASE("ui: the printf patterns of Text.cod, filled without printf") {
    using ui::format_text;
    CHECK(format_text("%d%%", {std::int64_t{42}}) == "42%");
    CHECK(format_text("%dt %s", {std::int64_t{5}, std::string_view("Holz")}) == "5t Holz");
    CHECK(format_text("%svorrat (reicht für weitere %0.1ft %s)",
                      {std::string_view("Wolle"), 1.5, std::string_view("Stoffe")}) ==
          "Wollevorrat (reicht für weitere 1.5t Stoffe)");
    CHECK(format_text("%d%% Steuer:", {std::int64_t{32}}) == "32% Steuer:");
    // Missing arguments stay empty, unknown conversions stay, wrong kinds are converted.
    CHECK(format_text("%s and %d", {}) == " and ");
    CHECK(format_text("%x %d", {std::int64_t{1}}) == "%x 1");
    CHECK(format_text("%d", {2.7}) == "2");
    CHECK(format_text("%s", {std::int64_t{7}}) == "7");
    CHECK(format_text("100%", {}) == "100%");
}

TEST_CASE("ui: key bindings: the context's own keys first, then the general ones") {
    ui::KeyBindings keys;
    keys.bind(ui::KeyContext::General, {'w'}, ui::KeyAction::ScrollUp);
    keys.bind(ui::KeyContext::Building, {'r'}, ui::KeyAction::RotateBuilding);
    keys.bind(ui::KeyContext::Units, {'r'}, ui::KeyAction::EditRoute);
    keys.bind(ui::KeyContext::General, {'1', true}, ui::KeyAction::GroupStore, 1);
    keys.bind(ui::KeyContext::General, {'1'}, ui::KeyAction::GroupRecall, 1);
    CHECK(keys.find(ui::KeyContext::Building, {'r'})->action == ui::KeyAction::RotateBuilding);
    CHECK(keys.find(ui::KeyContext::Units, {'r'})->action == ui::KeyAction::EditRoute);
    CHECK(!keys.find(ui::KeyContext::General, {'r'}));
    CHECK(keys.find(ui::KeyContext::Units, {'w'})->action == ui::KeyAction::ScrollUp);
    CHECK(keys.find(ui::KeyContext::General, {'1', true})->action == ui::KeyAction::GroupStore);
    CHECK(keys.find(ui::KeyContext::General, {'1'})->param == 1);
    CHECK(!keys.find(ui::KeyContext::General, {'1', false, true}));
    // Rebinding replaces; unbinding frees.
    keys.bind(ui::KeyContext::General, {'w'}, ui::KeyAction::WhiteFlag);
    CHECK(keys.keys(ui::KeyAction::ScrollUp).empty());
    keys.unbind(ui::KeyAction::WhiteFlag);
    CHECK(!keys.find(ui::KeyContext::General, {'w'}));
    CHECK(ui::KeyBindings::action_named("group_recall") == ui::KeyAction::GroupRecall);
    CHECK(ui::KeyBindings::name(ui::KeyAction::Delete) == "delete");
}

TEST_CASE("ui: key bindings: the settings file changes only the actions it names") {
    // Keycodes are the letters themselves; physical keys (scancodes) count from 4 = A.
    const auto name = [](std::int32_t key, bool physical) {
        if (physical) {
            return std::string(1, static_cast<char>('A' + key - 4));
        }
        return key == 0x40000000 ? std::string("F1") : std::string(1, static_cast<char>(std::toupper(key)));
    };
    const auto from_name = [](std::string_view n, bool physical) -> std::optional<std::int32_t> {
        if (physical) {
            return n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z' ? std::optional<std::int32_t>(n[0] - 'A' + 4)
                                                               : std::nullopt;
        }
        if (n == "F1") {
            return 0x40000000;
        }
        if (n.size() == 1 && std::isalnum(static_cast<unsigned char>(n[0])) != 0) {
            return std::tolower(static_cast<unsigned char>(n[0]));
        }
        return std::nullopt;
    };
    CHECK(ui::KeyBindings::chord_text({'c', false, true}, name) == "Shift+C");
    CHECK((ui::KeyBindings::parse_chord("ctrl+Shift+c", from_name) == ui::KeyChord{'c', true, true}));
    CHECK(!ui::KeyBindings::parse_chord("Ctrl+", from_name));
    // A key by its place stands in brackets.
    CHECK(ui::KeyBindings::chord_text({26, true, false, false, true}, name) == "Ctrl+[W]");
    CHECK((ui::KeyBindings::parse_chord("Ctrl+[W]", from_name) == ui::KeyChord{26, true, false, false, true}));
    CHECK(!ui::KeyBindings::parse_chord("[1]", from_name));

    ui::KeyBindings keys;
    keys.bind(ui::KeyContext::General, {'w'}, ui::KeyAction::ScrollUp);
    keys.bind(ui::KeyContext::Units, {'f'}, ui::KeyAction::AttackMove);
    keys.bind(ui::KeyContext::Units, {'h'}, ui::KeyAction::Stop);
    keys.bind(ui::KeyContext::General, {'1', true}, ui::KeyAction::GroupStore, 0);
    keys.bind(ui::KeyContext::General, {0x40000000}, ui::KeyAction::Help);
    keys.bind(ui::KeyContext::General, {7, false, false, false, true}, ui::KeyAction::ScrollRight);
    const std::string text = keys.write_text(name);
    CHECK(text.find("units attack_move = F\n") != std::string::npos);
    CHECK(text.find("general scroll_right = [D]\n") != std::string::npos);
    CHECK(text.find("general group_store 0 = Ctrl+1\n") != std::string::npos);
    // Written and read back: the same table.
    ui::KeyBindings copy;
    CHECK(copy.read_text(text, from_name).empty());
    CHECK(copy.all().size() == keys.all().size());
    CHECK(copy.find(ui::KeyContext::Units, {'f'})->action == ui::KeyAction::AttackMove);
    // Saved by an editor that puts a byte order mark first: the same.
    ui::KeyBindings with_bom;
    CHECK(with_bom.read_text("\xEF\xBB\xBF" + text, from_name).empty());
    CHECK(with_bom.all().size() == keys.all().size());
    // As comments it changes nothing.
    ui::KeyBindings same = keys;
    CHECK(same.read_text(keys.write_text(name, true), from_name).empty());
    CHECK(same.all().size() == keys.all().size());

    // Attack on A and Q, stop without a key; the rest keeps its defaults. Errors are reported by line.
    const auto warnings = keys.read_text(
        "# comment\n"
        "units attack_move = A\n"
        "units attack_move = Q\n"
        "units stop = none\n"
        "units nonsense = X\n"
        "general group_store 12 = Ctrl+2\n"
        "general help = Ctrl+?\n",
        from_name);
    CHECK(warnings.size() == 3);
    CHECK(warnings[0].rfind("line 5", 0) == 0);
    CHECK(keys.find(ui::KeyContext::Units, {'a'})->action == ui::KeyAction::AttackMove);
    CHECK(keys.find(ui::KeyContext::Units, {'q'})->action == ui::KeyAction::AttackMove);
    CHECK(!keys.find(ui::KeyContext::Units, {'f'}));
    CHECK(keys.keys(ui::KeyAction::Stop).empty());
    CHECK(keys.find(ui::KeyContext::Units, {'w'})->action == ui::KeyAction::ScrollUp);
    CHECK(keys.find(ui::KeyContext::General, {0x40000000})->action == ui::KeyAction::Help);
}

TEST_CASE("ui: a form of Open1602's own: clicks, focus, cut texts and the scale") {
    ui::Form form;
    form.set_scale(2.0);
    form.panel(ui::Rect{0, 0, 300, 200});
    form.button(1, ui::Rect{10, 10, 100, 30}, "Verbinden");
    form.button(2, ui::Rect{10, 50, 100, 30}, "Starten", false);
    form.field(3, ui::Rect{10, 90, 60, 30}, "eine sehr lange Adresse");
    form.check(4, ui::Rect{10, 130, 200, 30}, "Zuschauer", true);
    // Pressed and released on the button (pixels at scale 2).
    CHECK(!form.mouse(40, 40, true));
    CHECK(!form.released());
    CHECK(form.mouse(40, 40, false) == std::optional<ui::Form::Id>(1));
    CHECK(form.released());
    // Only moved, with no button held: nothing let go.
    CHECK(!form.mouse(40, 400, false));
    CHECK(!form.released());
    // Released elsewhere: no click; a disabled button takes none.
    CHECK(!form.mouse(40, 40, true));
    CHECK(!form.mouse(40, 400, false));
    CHECK(form.released());  // let go beside: no click, but the press ended
    CHECK(!form.mouse(40, 120, true));
    CHECK(!form.mouse(40, 120, false));
    // Laid out anew, the press survives (by its Id).
    CHECK(!form.mouse(40, 280, true));
    form.clear();
    form.check(4, ui::Rect{10, 130, 200, 30}, "Zuschauer", false);
    CHECK(form.mouse(40, 280, false) == std::optional<ui::Form::Id>(4));
    CHECK(form.hovered() == std::optional<ui::Form::Id>(4));

    form.clear();
    form.field(3, ui::Rect{10, 90, 60, 30}, "eine sehr lange Adresse");
    form.field(5, ui::Rect{10, 130, 200, 30}, "geheim", true);
    form.set_focus(3);
    const auto items = form.draw({nullptr, nullptr, nullptr});
    std::vector<std::string> texts;
    for (const ui::DrawItem& d : items) {
        if (d.kind == ui::DrawItem::Kind::Text) {
            texts.push_back(d.text);
            CHECK(d.x >= 20);  // design units times the scale
        }
    }
    REQUIRE(texts.size() == 2);
    // The focused field shows its end and the caret, cut to its box; a secret one only stars.
    CHECK(texts[0].back() == '_');
    CHECK(texts[0].size() < std::string("eine sehr lange Adresse_").size());
    CHECK(texts[1] == "******");
}

TEST_CASE("ui: a banner's colour made legible as text on a dark ground") {
    const assets::Rgba ground{35, 23, 12, 255};
    CHECK(ui::contrast(assets::Rgba{0, 0, 0, 255}, assets::Rgba{255, 255, 255, 255}) > 20.9);
    CHECK(ui::contrast(ground, ground) == 1.0);
    // Light enough already: unchanged.
    const assets::Rgba yellow{255, 219, 57, 255};
    const assets::Rgba y = ui::legible_on(yellow, ground, 4.5);
    CHECK((y.r == yellow.r && y.g == yellow.g && y.b == yellow.b));
    // A dark blue gets lighter, still bluish, and reaches the contrast.
    const assets::Rgba blue{0, 0, 241, 255};
    const assets::Rgba b = ui::legible_on(blue, ground, 4.5);
    CHECK(ui::contrast(blue, ground) < 4.5);
    CHECK(ui::contrast(b, ground) >= 4.5);
    CHECK(b.b > b.r);
    CHECK(b.r == b.g);
    CHECK(b.r < 200);  // not washed out to white
}

TEST_CASE("ui: an ink in a colour of its own carries its colour, a palette ink does not") {
    const assets::Rgba violet{168, 105, 210, 255};
    const std::int32_t ink = ui::rgb_ink(violet);
    CHECK(ui::is_rgb_ink(ink));
    const assets::Rgba back = ui::ink_rgb(ink);
    CHECK((back.r == 168 && back.g == 105 && back.b == 210));
    CHECK(!ui::is_rgb_ink(-1));
    CHECK(!ui::is_rgb_ink(255));
    CHECK(ui::is_rgb_ink(ui::rgb_ink(assets::Rgba{0, 0, 0, 255})));
}

TEST_CASE("ui: a form's chat line: the speaker's name in its ink, the rest behind it, cut as one") {
    ui::Form form;
    form.set_scale(2.0);
    form.speech(ui::Rect{10, 10, 300, 30}, "Anna", 150, ": Hallo", ui::Form::Tone::Heading);
    auto items = form.draw({nullptr, nullptr, nullptr});
    REQUIRE(items.size() == 2);
    CHECK(items[0].text == "Anna");
    CHECK(items[0].ink == 150);
    CHECK(items[0].colour == ui::FormStyle{}.heading);
    CHECK(items[1].text == ": Hallo");
    CHECK(items[1].ink == -1);
    CHECK(items[1].colour == ui::FormStyle{}.heading);
    CHECK(items[1].x == items[0].x + 4 * 9 * 2);  // 9 units a byte without a font, at scale 2
    // Too long: cut at the end, the name whole.
    form.clear();
    form.speech(ui::Rect{10, 10, 100, 30}, "Anna", 150, ": " + std::string(40, 'x'));
    items = form.draw({nullptr, nullptr, nullptr});
    REQUIRE(items.size() == 2);
    CHECK(items[0].text == "Anna");
    CHECK(items[1].text.ends_with("..."));
    // A box too narrow for the name: all of what is left in the name's ink.
    form.clear();
    form.speech(ui::Rect{10, 10, 60, 30}, "Annabella", 150, ": Hallo");
    items = form.draw({nullptr, nullptr, nullptr});
    REQUIRE(items.size() == 1);
    CHECK(items[0].text.ends_with("..."));
    CHECK(items[0].ink == 150);
}

TEST_CASE("ui: a form's cycle buttons take the right button, its other widgets only the left one") {
    using ui::MouseButton;
    ui::Form form;
    form.panel(ui::Rect{0, 0, 300, 200});
    form.cycle_button(1, ui::Rect{10, 10, 100, 30}, "4 Spieler");
    form.button(2, ui::Rect{10, 50, 100, 30}, "Verlassen");
    form.cycle_button(3, ui::Rect{10, 90, 100, 30}, "aus", false);
    // The left button clicks a cycle button as any other.
    CHECK(!form.mouse(20, 20, true));
    CHECK(form.mouse(20, 20, false) == std::optional<ui::Form::Id>(1));
    CHECK(form.clicked_with() == MouseButton::Left);
    // The right button clicks it too, and the form says which button it was.
    CHECK(!form.mouse(20, 20, true, MouseButton::Right));
    CHECK(form.mouse(20, 20, false, MouseButton::Right) == std::optional<ui::Form::Id>(1));
    CHECK(form.clicked_with() == MouseButton::Right);
    // A plain button and a disabled cycle button do not answer the right button.
    CHECK(!form.mouse(20, 60, true, MouseButton::Right));
    CHECK(!form.mouse(20, 60, false, MouseButton::Right));
    CHECK(form.hovered() == std::optional<ui::Form::Id>(2));
    CHECK(!form.mouse(20, 100, true, MouseButton::Right));
    CHECK(!form.mouse(20, 100, false, MouseButton::Right));
    // The other button while one is held changes nothing: the click is the first button's.
    CHECK(!form.mouse(20, 20, true, MouseButton::Right));
    CHECK(!form.mouse(20, 60, true));
    CHECK(!form.mouse(20, 60, false));
    CHECK(!form.mouse(20, 20, false));  // the pointer moving back with no left button held
    CHECK(form.mouse(20, 20, false, MouseButton::Right) == std::optional<ui::Form::Id>(1));
    CHECK(form.clicked_with() == MouseButton::Right);
    // A cycle button draws as a button.
    REQUIRE(form.widgets().size() == 4);
    CHECK(form.widgets()[1].kind == ui::Form::Widget::Kind::Button);
    CHECK(form.widgets()[1].cycles);
    CHECK(!form.widgets()[2].cycles);
}

TEST_CASE("ui: cutting text to a width finds the same cut as trying one byte after the other") {
    // The cut by removing one byte at a time, as the form did before.
    const auto linear = [](const ui::Font* font, std::string s, double width, bool keep_end) {
        const auto measure = [font](const std::string& t) {
            return font != nullptr ? font->width(t) / font->design_scale() : 9.0 * static_cast<double>(t.size());
        };
        if (measure(s) <= width) {
            return s;
        }
        if (keep_end) {
            while (!s.empty() && measure(s) > width) {
                s.erase(s.begin());
            }
            return s;
        }
        while (!s.empty() && measure(s + "...") > width) {
            s.pop_back();
        }
        return s + "...";
    };
    ui::Font font = test_font();
    ui::Font scaled = test_font();
    scaled.set_design_scale(0.625);
    const std::array<const ui::Font*, ui::kFontSlots> fonts = {&font, &scaled, nullptr};
    // Narrow and wide glyphs, bytes without a glyph and control characters, which end the measure.
    const std::string alphabet = std::string("aW .\x01\x80") + '\0';
    std::uint32_t seed = 12345;
    const auto next = [&seed] {
        seed = seed * 1103515245U + 12345U;
        return seed >> 16U;
    };
    for (int i = 0; i < 2000; ++i) {
        std::string text;
        const std::uint32_t length = next() % 40;
        for (std::uint32_t k = 0; k < length; ++k) {
            text += alphabet[next() % alphabet.size()];
        }
        const double width = static_cast<double>(next() % 200) - 5.0;
        for (const ui::Font* f : fonts) {
            for (const bool keep_end : {false, true}) {
                CHECK(ui::fit_text(f, text, width, keep_end) == linear(f, text, width, keep_end));
            }
        }
    }
}

TEST_CASE("ui: wrap_lines keeps every character of what players write") {
    using Lines = std::vector<std::string>;
    // Without a font every byte is 9 units wide: 45 units hold five bytes.
    CHECK((ui::wrap_lines(nullptr, "", 45) == Lines{""}));
    CHECK((ui::wrap_lines(nullptr, "ab cd", 45) == Lines{"ab cd"}));
    CHECK((ui::wrap_lines(nullptr, "ab cd ef", 45) == Lines{"ab cd", "ef"}));
    // A word wider than a line is cut where the line is full.
    CHECK((ui::wrap_lines(nullptr, "abcdefghijkl", 45) == Lines{"abcde", "fghij", "kl"}));
    // It starts on the line before, after what is there already.
    CHECK((ui::wrap_lines(nullptr, "ab abcdefgh", 45) == Lines{"ab ab", "cdefg", "h"}));
    CHECK((ui::wrap_lines(nullptr, "abcd abcdefgh", 45) == Lines{"abcd", "abcde", "fgh"}));
    // Hyphens and "--" stay as written; no room at all still gives a character a line.
    CHECK((ui::wrap_lines(nullptr, "a--b -x", 45) == Lines{"a--b", "-x"}));
    CHECK((ui::wrap_lines(nullptr, "abc", 0) == Lines{"a", "b", "c"}));
    // Every word is still there, in order.
    const std::string text = "Wer baut die erste Kirche auf der grossen Insel im Norden, bevor der Winter kommt?";
    std::string joined;
    for (const std::string& line : ui::wrap_lines(nullptr, text, 120)) {
        CHECK(line.size() * 9 <= 120);
        joined += (joined.empty() ? "" : " ") + line;
    }
    CHECK(joined == text);
}

TEST_CASE("ui: key bindings: a key by its place works on every layout, by meaning where it is printed") {
    // SDL numbers: scancode 26 is the key at W on a US keyboard; on AZERTY it prints Z.
    constexpr std::int32_t kScanW = 26;
    ui::KeyBindings keys;
    keys.bind(ui::KeyContext::General, {kScanW, false, false, false, true}, ui::KeyAction::ScrollUp);
    keys.bind(ui::KeyContext::General, {'b'}, ui::KeyAction::ModeBuild);
    keys.bind(ui::KeyContext::General, {'z'}, ui::KeyAction::ShowOwners);
    keys.bind(ui::KeyContext::Units, {'w'}, ui::KeyAction::WhiteFlag);
    // QWERTY: the key at W prints w; AZERTY: z. Both scroll; the place comes before the meaning.
    CHECK(keys.find_pressed(ui::KeyContext::General, ui::KeyPress{'w', kScanW})->action == ui::KeyAction::ScrollUp);
    CHECK(keys.find_pressed(ui::KeyContext::General, ui::KeyPress{'z', kScanW})->action == ui::KeyAction::ScrollUp);
    // B means building wherever it lies (scancode 5 on QWERTY, any other elsewhere).
    CHECK(keys.find_pressed(ui::KeyContext::General, ui::KeyPress{'b', 5})->action == ui::KeyAction::ModeBuild);
    CHECK(keys.find_pressed(ui::KeyContext::General, ui::KeyPress{'b', 40})->action == ui::KeyAction::ModeBuild);
    // Modifiers must agree; the context's own keys still come first.
    CHECK(!keys.find_pressed(ui::KeyContext::General, ui::KeyPress{'w', kScanW, true}));
    CHECK(keys.find_pressed(ui::KeyContext::Units, ui::KeyPress{'w', kScanW})->action == ui::KeyAction::WhiteFlag);
    CHECK(keys.find_pressed(ui::KeyContext::Units, ui::KeyPress{'z', kScanW})->action == ui::KeyAction::ScrollUp);
}
