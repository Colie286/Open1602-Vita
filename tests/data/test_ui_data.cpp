// SPDX-License-Identifier: GPL-3.0-or-later
// The original's layouts and fonts (docs/formats/gad.md).

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "selection.hpp"

#include "o1602/assets/gad.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/ui/font.hpp"
#include "o1602/ui/panel.hpp"
#include "o1602/ui/resources.hpp"

using namespace o1602;

namespace {

assets::GameDirectory game_or_skip(const char* needs) {
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        SKIP("ANNO1602_DIR not set");
    }
    if (!game->resolve(needs)) {
        SKIP("interface files not installed");
    }
    return std::move(game).value();
}

}  // namespace

TEST_CASE("data: the layouts of GADDATA load; the panel's minimap area and buttons") {
    const auto game = game_or_skip("GADDATA/CTRL.GAD");
    auto ctrl = assets::load_gad(game, "CTRL.GAD");
    REQUIRE(ctrl.ok());
    CHECK(ctrl->warnings.empty());
    const assets::Gadget* map = ctrl->find(30005);
    REQUIRE(map != nullptr);
    CHECK(map->kind == assets::GadgetKind::Area);
    CHECK(map->pos == (std::array<std::int32_t, 2>{34, 14}));
    CHECK(map->size == (std::array<std::int32_t, 2>{186, 160}));
    const assets::Gadget* left = ctrl->find(30001);
    REQUIRE(left != nullptr);
    CHECK(left->kind == assets::GadgetKind::Graphic);
    CHECK(left->gfx == 1);  // GFXPFEIL
    CHECK(left->press_offset == 2);
    CHECK(left->offset == (std::array<std::int16_t, 2>{9, 14}));  // 32 - 23, 213 - 199
    CHECK(ctrl->gadgets[0].gfx == 0);                             // GFXTAFEL, the panel's background
    // Every file the original loads parses without trouble, none beyond the original's 300.
    std::size_t files = 0;
    for (const char* name :
         {"ANNO.GAD",    "BASE.GAD",     "BAU.GAD",    "BAUKONT.GAD",  "BAUSHIP.GAD",  "BERGWERK.GAD", "BUBBLE.GAD",
          "CLIENT.GAD",  "CTRL.GAD",     "DISK.GAD",   "ENDE.GAD",     "FARBWAHL.GAD", "HANDEL.GAD",   "HOST.GAD",
          "INFO.GAD",    "INFRA.GAD",    "KONTOR.GAD", "LAGER.GAD",    "LEIST.GAD",    "MILITAR.GAD",  "MISSION.GAD",
          "MKSHIP.GAD",  "MKSOLDAT.GAD", "MUSIK.GAD",  "OPTION.GAD",   "PIRAT.GAD",    "PIRATORD.GAD", "PLANTAGE.GAD",
          "PROD.GAD",    "ROUTE.GAD",    "SHIP.GAD",   "SHIPLIST.GAD", "SIEDLER.GAD",  "SKSHIP.GAD",   "SKSOLDAT.GAD",
          "STADT.GAD",   "STADTLST.GAD", "TRADE.GAD",  "TRADER.GAD",   "TRANSFER.GAD", "TRIBUT.GAD",   "TUTOR.GAD",
          "VERTRAG.GAD", "WAITLOAD.GAD", "WERFT.GAD"}) {
        auto l = assets::load_gad(game, name);
        if (!l) {
            continue;
        }
        ++files;
        CHECK(!l->gadgets.empty());
        CHECK(l->gadgets.size() <= assets::kOriginalMaxGadgets);
        for (const auto& w : l->warnings) {
            std::printf("  %s: %s\n", name, w.c_str());
        }
    }
    CHECK(files == 45);
    // The status line: right aligned text in the brown colour (LEIST.GAD 100001).
    auto leist = assets::load_gad(game, "LEIST.GAD");
    REQUIRE(leist.ok());
    const assets::Gadget* status = leist->find(100001);
    REQUIRE(status != nullptr);
    CHECK(status->kind == assets::GadgetKind::TextRight);
    CHECK(status->colour[0] == 5);
    // The constants of TOOLS.INC the code uses for what it draws itself.
    CHECK(leist->constant("GFXLEISTE") == std::optional<std::int32_t>(253));
    auto bau = assets::load_gad(game, "BAU.GAD");
    REQUIRE(bau.ok());
    CHECK(bau->constant("GFXBAUHAUS") == std::optional<std::int32_t>(81));
    CHECK(bau->constant("GFXBAUMAT") == std::optional<std::int32_t>(46));
    CHECK(bau->constant("GFXWARE") == std::optional<std::int32_t>(192));
    CHECK(bau->constant("IDHAUPT") == std::optional<std::int32_t>(31001));
    CHECK(!bau->constant("NOSUCHTHING"));
    // The build menu's categories are its first eight gadgets, radio buttons that answer the press.
    for (std::int32_t id = 31001; id <= 31008; ++id) {
        const assets::Gadget* g = bau->find(id);
        REQUIRE(g != nullptr);
        CHECK(g->menu);
        CHECK(g->radio);
    }
}

TEST_CASE("data: the military button switches the mode once its duplicate is hidden (CTRL.GAD 30015)") {
    const auto game = game_or_skip("GADDATA/CTRL.GAD");
    auto ctrl = assets::load_gad(game, "CTRL.GAD");
    REQUIRE(ctrl.ok());
    const assets::Gadget* military = ctrl->find(app::side_mode::kMilitary);
    const assets::Gadget* duplicate = ctrl->find(app::side_mode::kMilitaryDuplicate);
    REQUIRE(military != nullptr);
    REQUIRE(duplicate != nullptr);
    // The duplicate lies on the military button and comes after it, so it is on top.
    CHECK(duplicate->pos == military->pos);
    CHECK(duplicate->size == military->size);
    const std::int32_t x = military->pos[0] + military->size[0] / 2;
    const std::int32_t y = military->pos[1] + military->size[1] / 2;
    const auto click = [&](ui::Panel& panel) {
        panel.mouse(x, y, true);
        const std::vector<ui::PanelEvent> events = panel.mouse(x, y, false);
        return events.empty() ? 0 : events.back().id;
    };
    ui::Panel as_loaded(*ctrl, 1.0);
    CHECK(click(as_loaded) == app::side_mode::kMilitaryDuplicate);
    // As the Hud sets it up, as the original does at the start of a game.
    ui::Panel panel(std::move(ctrl).value(), 1.0);
    panel.set_visible(app::side_mode::kMilitaryDuplicate, false);
    CHECK(click(panel) == app::side_mode::kMilitary);
    CHECK(panel.selected(app::side_mode::kMilitary));
}

TEST_CASE("data: the fonts of the three screen sizes, umlauts and the text colours") {
    const auto game = game_or_skip("TOOLGFX/ZEI14A.ZEI");
    auto font = ui::Font::load(game, ui::font_file(0, ui::ArtSize::Large));
    REQUIRE(font.ok());
    CHECK(font->height() == 24);
    CHECK(font->width("0") == 11);
    CHECK(font->width(" ") == 8);
    CHECK(font->has(static_cast<unsigned char>('\xDC')));  // Ü
    auto large = ui::Font::load(game, ui::font_file(2, ui::ArtSize::Large));
    REQUIRE(large.ok());
    CHECK(large->has('9'));
    CHECK(!large->has('A'));  // digits and punctuation only
    for (auto size : {ui::ArtSize::Small, ui::ArtSize::Medium, ui::ArtSize::Large}) {
        if (size != ui::ArtSize::Large && game.history_texts() != nullptr) {
            continue;  // the History Edition has only the large screen's fonts
        }
        for (std::int32_t slot = 0; slot < 3; ++slot) {
            CHECK(ui::Font::load(game, ui::font_file(slot, size)).ok());
        }
    }
    const auto palette = assets::Palette::load(*game.resolve("TOOLGFX/STADTFLD.COL"));
    REQUIRE(palette.ok());
    const ui::Recolour white = ui::text_recolour(*palette, 0);
    const assets::Rgba& body = (*palette)[white[1]];
    CHECK(body.r > 240);
    CHECK(body.g > 240);
    const ui::Recolour red = ui::text_recolour(*palette, 9);
    CHECK((*palette)[red[1]].r > 180);
    CHECK((*palette)[red[1]].g < 60);
    // A mission text wraps into the briefing's lines without losing words.
    const std::string text =
        "Nach einer langen, kr\xE4"
        "ftezehrenden Reise entdeckt Ihr ein Atoll.";
    const auto lines = ui::wrap_text(*font, text, 200);
    CHECK(lines.size() >= 2);
    std::string joined;
    for (const auto& l : lines) {
        joined += (joined.empty() ? "" : " ") + l;
    }
    CHECK(joined == text);
}

TEST_CASE("data: the accents of the capitals stand above them in every font that has them (D-012)") {
    const auto game = game_or_skip("TOOLGFX/ZEI20V.ZEI");
    // How many capitals keep part of their accent in the cell's last rows (counted 2026-10-23).
    const std::array<std::pair<const char*, std::size_t>, 4> kFonts = {{{"TOOLGFX/ZEI14V.ZEI", 21},
                                                                        {"TOOLGFX/ZEI16H.ZEI", 13},
                                                                        {"TOOLGFX/ZEI20H.ZEI", 21},
                                                                        {"TOOLGFX/ZEI20V.ZEI", 15}}};
    for (const auto& [file, capitals] : kFonts) {
        if (game.history_texts() != nullptr && !game.resolve(file)) {
            continue;  // the History Edition lacks ZEI14V
        }
        auto font = ui::Font::load(game, file);
        REQUIRE(font.ok());
        const std::uint32_t cell = font->height();
        CHECK(font->fix_accents() == capitals);
        CHECK(font->height() == cell);
        CHECK(font->above() >= 1);
        CHECK(font->above() <= 4);
    }
    if (!game.resolve("TOOLGFX/ZEI16V.ZEI")) {
        return;  // the History Edition has only the large screen's fonts
    }
    auto plain = ui::Font::load(game, "TOOLGFX/ZEI16V.ZEI");  // its accents fit
    REQUIRE(plain.ok());
    CHECK(plain->fix_accents() == 0);
}
