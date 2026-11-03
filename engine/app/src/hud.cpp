// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

#include "banner_graphic.hpp"
#include "diplomacy_strip.hpp"
#include "endless_setup.hpp"
#include "names.hpp"
#include "parchment.hpp"

#include "o1602/audio/game_audio.hpp"
#include "o1602/platform/window.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/form.hpp"
#include "o1602/ui/localization.hpp"
#include "o1602/ui/package_icons.hpp"
#include "o1602/ui/text_format.hpp"

namespace o1602::app {

namespace {

// Gadgets of CTRL.GAD and LEIST.GAD.
constexpr std::int32_t kRotateLeft = 30001;
constexpr std::int32_t kRotateRight = 30002;
constexpr std::int32_t kZoomIn = 30003;
constexpr std::int32_t kZoomOut = 30004;
constexpr std::int32_t kMinimap = 30005;
constexpr std::int32_t kCompass = 30006;
constexpr std::int32_t kFirstMode = 30011;  // build, military, info, options
constexpr std::int32_t kStatusLine = 100001;
constexpr std::int32_t kFirstEvent = 100011;
constexpr std::size_t kEvents = 3;
// Open1602: the offers of treaties not answered yet, after the message icons (+ 0..10), each the
// treaty message icon in the colour of the partner who made it.
constexpr std::int32_t kFirstOffer = 100101;
constexpr std::int32_t kOfferGap = 12;  // design pixels between the message icons and the offers
constexpr std::uint8_t kTreatyIcon = 1;
// TOOLS.INC: the compass (GFXMAP + 1 + rotation) and the message icons (GFXLEISTE).
constexpr std::int32_t kGfxTafel = 0;  // GFXTAFEL: the side panel's art
constexpr std::int32_t kGfxParchment = 17;
constexpr std::int32_t kGfxCompass = 18;
constexpr std::int32_t kGfxLeiste = 253;
constexpr std::int32_t kLeistArt = 101001;   // the status bar's art (drawn widened instead)
constexpr std::int32_t kLeistTextX = 750;    // its line ends here, 18 before the art's right edge
constexpr std::int32_t kTutorArt = 111001;   // the mission window's art (drawn widened instead)
constexpr std::int32_t kTutorText = 110002;  // its flowing text
constexpr std::int32_t kDesignViewW = 768;   // game view, status bar and mission window at 1024 x 768
constexpr std::uint64_t kEventMs = 45'000;   // an icon stays this long (449 time units)
constexpr std::uint64_t kMinimapMs = 500;    // the minimap texture is uploaded at most this often
// TUTOR.GAD in the tutorial: six lines of text, the arrows back and forward, each a picture and
// five parts to click.
constexpr std::int32_t kTutorClose = 110001;
constexpr std::int32_t kTutorLine = 110011;  // + 0..5
constexpr std::int32_t kTutorBack = 110021;  // + 0..5
constexpr std::int32_t kTutorNext = 110031;  // + 0..5
constexpr std::int32_t kTutorArrowParts = 6;
constexpr std::int32_t kTutorDesignY = 594;  // top of the window at 1024 x 768
// BUBBLE.GAD: bubble k is the box kFirstBubble + 10 k, its two lines and its arrow after it.
constexpr std::int32_t kFirstBubble = 111001;
constexpr std::int32_t kBubbleParts = 4;
constexpr std::uint64_t kTutorialFactsMs = 250;  // chapter 1 searches the town's roads this often

/// What a help bubble is kept beside when the frame is larger than 1024 x 768: the side panel, the
/// top left of the map (the signs and the resource bar), the left or the right end of the
/// tutorial's window (its X; „weiter“ and the status line).
enum class BubbleAnchor : std::uint8_t { SidePanel, Map, TutorLeft, TutorRight };

BubbleAnchor bubble_anchor(std::uint8_t k) {
    switch (k) {
        case bubble::kEnd:
            return BubbleAnchor::TutorLeft;
        case bubble::kStatusLine:
        case bubble::kNext:
            return BubbleAnchor::TutorRight;
        case bubble::kClickShip:
        case bubble::kSailToIsland:
        case bubble::kNoOre:
        case bubble::kRightMouse:
        case bubble::kClickHouse:
        case bubble::kLetGo:
        case bubble::kShortOfWood:
        case bubble::kShortOfWood + 1:  // tools, bricks, money: the resource bar
        case bubble::kShortOfWood + 2:
        case bubble::kShortOfWood + 3:
        case bubble::kToKontor:
        case bubble::kPartnerKontor:
        case bubble::kSailToPartner:
        case bubble::kSailHome:
        case bubble::kAttackWithLarge:
        case bubble::kAttackWithSmall:
        case bubble::kSelectSmall:
        case bubble::kPatrolPoint:
        case bubble::kSailToCoast:
        case bubble::kFrameSoldiers:
        case bubble::kAttackWithCavalry:
        case bubble::kAttackWithRest:
        case bubble::kClickLargeShip:
            return BubbleAnchor::Map;
        default:
            return BubbleAnchor::SidePanel;
    }
}

/// Help text line of [LEISTE] for the gadgets of the panel's head (in the order of the texts).
std::optional<std::size_t> help_line(std::int32_t id) {
    switch (id) {
        case kRotateLeft:
            return 2;
        case kRotateRight:
            return 3;
        case kZoomIn:
            return 4;
        case kZoomOut:
            return 5;
        default:
            break;
    }
    if (id >= kFirstMode && id < kFirstMode + 4) {
        return static_cast<std::size_t>(6 + id - kFirstMode);
    }
    return std::nullopt;
}

/// The icon of a message: types 0..3 by colour (! event,
/// scroll treaty or trade, swords attack, lips other players), 4..7 plague, fire, drought, volcano.
/// Which message has which type is not documented; this follows the icons.
/// The message icons of the four types drawn per colour: red, blue, yellow, white, (military
/// only) the pirates' skull, an empty slot; pressed six further on.
const render::BannerArt& message_icon_art(std::uint8_t type) {
    static const std::array<render::BannerArt, 4> kArt = [] {
        std::array<render::BannerArt, 4> art{};
        for (std::uint8_t t = 0; t < art.size(); ++t) {
            art[t].first = kGfxLeiste + 1 + t * 12;
        }
        art[2].own = {{"anno1602:pirates", 4}};
        return art;
    }();
    return kArt[std::min<std::size_t>(type, kArt.size() - 1)];
}

/// LEIST.GAD with a place for every partner's open offer: copies of the message icon after the
/// three of the original.
assets::GadgetLayout with_offer_icons(assets::GadgetLayout leist) {
    const assets::Gadget* first = leist.find(kFirstEvent);
    if (first == nullptr) {
        return leist;
    }
    const assets::Gadget icon = *first;
    const std::int32_t step =
        leist.find(kFirstEvent + 1) != nullptr ? leist.find(kFirstEvent + 1)->pos[0] - icon.pos[0] : icon.size[0] + 3;
    for (std::size_t k = 0; k < diplomacy::kMaxPartners; ++k) {
        assets::Gadget g = icon;
        g.id = kFirstOffer + static_cast<std::int32_t>(k);
        g.pos[0] =
            icon.pos[0] + static_cast<std::int32_t>(kEvents) * step + kOfferGap + static_cast<std::int32_t>(k) * step;
        leist.gadgets.push_back(g);
    }
    return leist;
}

/// The end screen (ENDE.GAD), with the scroll arrows of TRANSFER.GAD for the strip of players.
EndScreen load_end_screen(const assets::GameDirectory& game) {
    auto ende = assets::load_gad(game, "ENDE.GAD");
    auto transfer = assets::load_gad(game, "TRANSFER.GAD");
    return EndScreen(ende ? std::optional<assets::GadgetLayout>(std::move(ende).value()) : std::nullopt,
                     transfer ? &transfer.value() : nullptr);
}

/// The key overview's columns of a page, design units: two columns side by side, in each the keys
/// as wide as the page's widest (at most two fifths of the column) and the texts beside them.
struct HelpColumns {
    double column = 0.0;  ///< width of a column
    double keys = 0.0;    ///< width of the keys
    double text = 0.0;    ///< width of the texts, wrapped to it
};
constexpr double kHelpMargin = 14.0;
constexpr double kHelpIndent = 8.0;
constexpr double kHelpKeyGap = 12.0;
constexpr double kHelpColumnGap = 10.0;

HelpColumns help_columns(const ui::Font* font, const KeyHelpPage& page, double width) {
    HelpColumns c;
    c.column = std::max(0.0, (width - 2 * kHelpMargin) / 2);
    double widest = 0.0;
    for (const KeyHelpSection& s : page.sections) {
        for (const KeyHelpLine& l : s.lines) {
            widest = std::max(widest, font != nullptr ? font->width(l.keys) / font->design_scale()
                                                      : 9.0 * static_cast<double>(l.keys.size()));
        }
    }
    c.keys = std::min(widest, c.column * 0.4);
    c.text = std::max(0.0, c.column - kHelpIndent - c.keys - kHelpKeyGap - kHelpColumnGap);
    return c;
}

/// The text of a line of the key overview wrapped to its column.
std::vector<std::string> help_text_lines(const ui::Font* font, const std::string& text, double width) {
    if (font == nullptr) {
        return {text};
    }
    std::vector<std::string> lines =
        ui::wrap_text(*font, text, static_cast<std::uint32_t>(std::max(1.0, width * font->design_scale())));
    if (lines.empty()) {
        lines.emplace_back();
    }
    return lines;
}

/// The texts of all lines of a page wrapped to their column, by section and line.
std::vector<std::vector<std::vector<std::string>>> help_wrapped(const ui::Font* font, const KeyHelpPage& page,
                                                                double width) {
    std::vector<std::vector<std::vector<std::string>>> wrapped;
    for (const KeyHelpSection& s : page.sections) {
        auto& section = wrapped.emplace_back();
        for (const KeyHelpLine& l : s.lines) {
            section.push_back(help_text_lines(font, l.text, width));
        }
    }
    return wrapped;
}

}  // namespace

std::unique_ptr<Hud> Hud::create(SDL_Renderer* renderer, const assets::GameDirectory& game,
                                 const assets::Palette& palette, const assets::TextTable* texts, bool data_fixes) {
    auto ctrl = assets::load_gad(game, "CTRL.GAD");
    auto leist = assets::load_gad(game, "LEIST.GAD");
    if (!ctrl || !leist || !game.resolve(ui::sheet_file(0, ui::ArtSize::Large))) {
        return nullptr;
    }
    return std::unique_ptr<Hud>(new Hud(renderer, game, palette, std::move(ctrl).value(),
                                        with_offer_icons(std::move(leist).value()), texts, data_fixes));
}

Hud::Hud(SDL_Renderer* renderer, const assets::GameDirectory& game, const assets::Palette& palette,
         assets::GadgetLayout ctrl, assets::GadgetLayout leist, const assets::TextTable* texts, bool data_fixes)
    : m_sdl(renderer),
      m_game(game),
      m_palette(palette),
      m_renderer(std::make_unique<render::SdlUiRenderer>(renderer, game, palette, ui::ArtSize::Large, data_fixes)),
      m_ctrl(std::move(ctrl), 1.0),
      m_leist(std::move(leist), 1.0),
      m_end(load_end_screen(game)) {
    if (texts != nullptr) {
        m_texts = *texts;
        m_leiste = texts->section("LEISTE");
        m_meldung = texts->section("MELDUNG");
        m_tutor_lines = texts->section("TUTOR");
    }
    const assets::TextTable* own = m_texts ? &*m_texts : nullptr;
    for (std::size_t i = 0; i < kEvents; ++i) {
        m_leist.set_hidden(kFirstEvent + static_cast<std::int32_t>(i), true);
    }
    for (std::size_t k = 0; k < diplomacy::kMaxPartners; ++k) {
        m_leist.set_visible(kFirstOffer + static_cast<std::int32_t>(k), false);
    }
    m_leist.set_hidden(kLeistArt, true);
    // The panels of the modes; without their layouts the head stays alone.
    if (auto bau = assets::load_gad(game, "BAU.GAD")) {
        m_build = std::make_unique<BuildPanel>(std::move(bau).value(), ui::build_menu(), own);
    }
    if (auto info = assets::load_gad(game, "INFO.GAD")) {
        m_info = std::make_unique<InfoPanel>(std::move(info).value(), own);
    }
    m_selection_panels = std::make_unique<SelectionPanels>(game);
    {
        auto option = assets::load_gad(game, "OPTION.GAD");
        auto disk = assets::load_gad(game, "DISK.GAD");
        auto musik = assets::load_gad(game, "MUSIK.GAD");
        auto anno = assets::load_gad(game, "ANNO.GAD");
        if (option && disk && musik && anno) {
            m_options = std::make_unique<OptionsPanel>(std::move(option).value(), std::move(disk).value(),
                                                       std::move(musik).value(), std::move(anno).value(), own);
        }
    }
    m_menu = std::make_unique<MainMenu>(game, own);
    if (!m_menu->usable()) {
        m_menu.reset();
    } else {
        // Open1602's own windows in the colours of its others (gold frame, dark ground).
        ui::FormStyle style;
        style.field = ui::nearest_colour(palette, 12, 8, 4);
        style.hover = ui::nearest_colour(palette, 72, 50, 24);
        style.selected = ui::nearest_colour(palette, 112, 80, 30);
        m_multiplayer = std::make_unique<MultiplayerMenu>(style);
        m_menu->set_form_style(style);
        // The preview of a new world: sea, surf, beach, ground, forest, mountains, villages, starts.
        constexpr std::array<std::array<std::uint8_t, 3>, sim::WorldPreview::kTiles + 1> kPreview = {{{30, 60, 110},
                                                                                                      {60, 105, 160},
                                                                                                      {220, 200, 140},
                                                                                                      {125, 150, 70},
                                                                                                      {45, 95, 40},
                                                                                                      {125, 105, 85},
                                                                                                      {190, 60, 40},
                                                                                                      {255, 255, 255}}};
        GameSettingsMenu::PreviewColours preview{};
        for (std::size_t i = 0; i < kPreview.size(); ++i) {
            preview[i] = ui::nearest_colour(palette, kPreview[i][0], kPreview[i][1], kPreview[i][2]);
        }
        m_menu->set_preview_colours(preview);
        m_multiplayer->set_preview_colours(preview);
        m_display = std::make_unique<DisplayMenu>(style);
        m_question_form.set_style(style);
        m_chat = std::make_unique<ChatWindow>(style);
        m_blueprint = std::make_unique<BlueprintWindow>(style);
        m_templates = std::make_unique<TemplateWindow>(style);
        m_editor = std::make_unique<EditorPanel>(style);
        m_chrome = std::make_unique<EditorChrome>(style);
        // The templates' views from above: land, buildings, houses, roads, fields, forest.
        TemplateColours colours;
        colours.ground = ui::nearest_colour(palette, 125, 150, 70);
        colours.pieces = {ui::nearest_colour(palette, 150, 70, 45), ui::nearest_colour(palette, 215, 175, 110),
                          ui::nearest_colour(palette, 120, 115, 110), ui::nearest_colour(palette, 230, 205, 90),
                          ui::nearest_colour(palette, 35, 80, 35)};
        m_templates->set_colours(colours);
        // The window "Warenbilanz" as a parchment: ink on sandstone, the grounds at half strength.
        GoodsStyle goods;
        goods.form.translucent = true;
        goods.form.frame = ui::nearest_colour(palette, 90, 60, 25);
        goods.form.ground = ui::nearest_colour(palette, 150, 105, 55);
        goods.form.field = goods.form.ground;
        goods.form.hover = ui::nearest_colour(palette, 175, 154, 124);
        goods.form.selected = ui::nearest_colour(palette, 120, 80, 35);
        goods.form.text = ui::kTextInk;
        goods.form.heading = ui::kTextInk;
        goods.form.dim = ui::kTextInkFaded;
        goods.form.warning = ui::kTextInkRed;
        goods.dark = ui::nearest_colour(palette, 40, 24, 8);
        goods.light = ui::nearest_colour(palette, 245, 230, 195);
        goods.shade = ui::nearest_colour(palette, 95, 65, 30);
        goods.band = ui::nearest_colour(palette, 150, 105, 55);
        goods.green = ui::nearest_colour(palette, 0, 120, 30);
        goods.red = ui::nearest_colour(palette, 170, 20, 10);
        goods.stock = ui::nearest_colour(palette, 150, 115, 70);
        goods.shadow = ui::nearest_colour(palette, 0, 0, 0);
        // The lines of the charts: taxes or the first class green, then red, blue, ochre, purple, grey,
        // and teal and brown for the classes of packages.
        constexpr std::array<std::array<std::uint8_t, 3>, 8> kSeries = {{{0, 120, 30},
                                                                         {170, 20, 10},
                                                                         {30, 60, 160},
                                                                         {190, 130, 20},
                                                                         {120, 40, 130},
                                                                         {90, 90, 90},
                                                                         {0, 130, 130},
                                                                         {120, 70, 20}}};
        for (std::size_t i = 0; i < kSeries.size(); ++i) {
            goods.series[i] = ui::nearest_colour(palette, kSeries[i][0], kSeries[i][1], kSeries[i][2]);
        }
        // A building's name as the build menu gives it (Text.cod, by its place in its category).
        GoodsWindow::BuildingNames building_names;
        if (own != nullptr) {
            building_names = [names = *own](std::uint32_t id) -> std::string {
                const ui::BuildCategory* category = ui::build_menu().category_of(id);
                if (category == nullptr) {
                    return {};
                }
                return category->name(category->index_of(id).value_or(0), names.section(category->names));
            };
        }
        m_goods =
            std::make_unique<GoodsWindow>(goods, own != nullptr ? own->section("WARE") : std::vector<std::string>{},
                                          m_ctrl.layout().constant("GFXWARE").value_or(192), std::move(building_names));
        if (own != nullptr) {
            // The classes of inhabitants as the house window names them ([GAME] 3 and on).
            const std::vector<std::string> lines = own->section("GAME");
            std::vector<std::string> classes;
            for (std::size_t k = 3; k < 3 + assets::kResidentClasses && k < lines.size(); ++k) {
                classes.push_back(lines[k]);
            }
            m_goods->set_class_names(std::move(classes));
        }
        m_goods_shadow = goods.shadow;
        m_goods_parchment = parchment_pixels(GoodsWindow::kWidth, GoodsWindow::kHeight);
        // The banners of the slots in exactly their colours (the palette has no pink and no violet),
        // grey for any further one.
        render::BannerTable banners = render::BannerTable::original();
        banners.add_more_players();
        std::vector<MenuBanner> menu_banners;
        std::vector<std::int32_t> banner_colours;
        // Names in a banner's colour stay readable on the menus' dark ground (4.5:1, as for text).
        const assets::Rgba ground = palette[ui::FormStyle{}.ground];
        for (const render::Banner& b : banners.banners()) {
            // In exactly that colour: the palette has no light shade of some banners (no violet).
            menu_banners.push_back(
                MenuBanner{ui::rgb_ink(b.colour), b.id, ui::rgb_ink(ui::legible_on(b.colour, ground, 4.5))});
            banner_colours.push_back(menu_banners.back().colour);
        }
        const std::uint8_t grey = ui::nearest_colour(palette, 128, 128, 128);
        m_multiplayer->set_banners(std::move(menu_banners), grey);
        m_banner_palette = banner_colours;
        m_other_banner_palette = grey;
        m_chat->set_banners(std::move(banner_colours), grey);
    }
    if (auto tutor = assets::load_gad(game, "TUTOR.GAD")) {
        m_tutor.emplace(std::move(tutor).value(), 1.0);
        m_tutor->set_hidden(kTutorArt, true);
        // The History Edition's TOOLS.INC puts five sprites more before it (GFXKAMPF).
        m_gfx_tutor = m_tutor->layout().constant("GFXTUTOR").value_or(m_gfx_tutor);
        // The gadgets at the window's right end (the scroll arrows) move with it on wide screens.
        for (const assets::Gadget& g : m_tutor->layout().gadgets) {
            if (g.kind != assets::GadgetKind::Unused && g.id != kTutorArt && g.pos[0] >= kDesignViewW / 2) {
                m_tutor_right.emplace_back(g.id, g.pos);
            }
        }
        if (const assets::Gadget* text = m_tutor->layout().find(kTutorText)) {
            m_tutor_text_size = text->size;
            m_tutor_text_x = text->pos[0];
        }
        // Where the scroll arrows begin: a text longer than the window ends left of them.
        m_tutor_arrows_x = std::numeric_limits<std::int32_t>::max();
        for (std::int32_t i = 0; i < kTutorArrowParts; ++i) {
            for (const std::int32_t id : {kTutorBack + i, kTutorNext + i}) {
                if (const assets::Gadget* g = m_tutor->layout().find(id)) {
                    m_tutor_arrows_x = std::min(m_tutor_arrows_x, g->pos[0]);
                }
            }
        }
    }
    if (auto bubbles = assets::load_gad(game, "BUBBLE.GAD")) {
        // All hidden; each frame shows the bubbles of the tutorial's step.
        m_bubble_panel.emplace(std::move(bubbles).value(), 1.0);
        for (const assets::Gadget& g : m_bubble_panel->layout().gadgets) {
            if (g.kind != assets::GadgetKind::Unused) {
                m_bubble_panel->set_visible(g.id, false);
            }
        }
        const std::vector<std::string> lines = own != nullptr ? own->section("BUBBLES") : std::vector<std::string>{};
        for (std::size_t k = 0; k < bubble::kCount; ++k) {
            const std::int32_t id = kFirstBubble + 10 * static_cast<std::int32_t>(k);
            for (std::size_t i = 0; i < 2; ++i) {
                if (2 * k + i < lines.size()) {
                    m_bubble_panel->set_text(id + 1 + static_cast<std::int32_t>(i), lines[2 * k + i]);
                }
            }
        }
    }
    m_ctrl.set_visible(side_mode::kMilitaryDuplicate, false);
    m_ctrl.set_selected(m_mode, true);
    m_gfx_baumat = m_ctrl.layout().constant("GFXBAUMAT").value_or(m_gfx_baumat);
}

void Hud::draw_resources() {
    // Design units from the top left of the map, icons at the top, text cells ending at 30.
    const double s = m_regions.scale;
    const ui::Rect& v = m_regions.game_view;
    const std::int32_t x0 = v.x + static_cast<std::int32_t>(5.5 * s);
    std::vector<ui::DrawItem> items;
    const auto icon = [&](std::int32_t gfx, std::int32_t x) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Sprite;
        d.gfx = gfx;
        d.x = ui::to_pixels(x, s);
        d.y = 0;
        items.push_back(d);
    };
    const auto text = [&](std::string t, std::int32_t x) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = 0;   // ZEINR
        d.colour = 4;  // light grey (240, 240, 240)
        d.x = ui::to_pixels(x, s);
        d.y = ui::to_pixels(30, s);
        d.text = std::move(t);
        items.push_back(d);
    };
    const auto with_cost = [](std::int64_t value, const std::optional<std::int64_t>& cost) {
        return cost ? std::to_string(value) + " (" + std::to_string(*cost) + ")" : std::to_string(value);
    };
    const Resources& r = m_resources;
    icon(m_gfx_baumat + 11, 16);
    text(r.money ? with_cost(*r.money, r.money_cost) : std::string("???"), 46);
    if (r.goods) {
        // Werkzeug, Holz, Ziegel: GFXBAUMAT + 13, 14, 12.
        const std::array<std::int32_t, 3> icons{m_gfx_baumat + 13, m_gfx_baumat + 14, m_gfx_baumat + 12};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto x = static_cast<std::int32_t>(296 + 110 * i);
            icon(icons[i], x);
            const auto cost = r.goods_cost ? std::optional<std::int64_t>((*r.goods_cost)[i]) : std::nullopt;
            text(with_cost((*r.goods)[i], cost), x + 30);
        }
    }
    if (r.inhabitants) {
        if (r.shield >= 0) {
            // The coat of arms of a human or computer owner: drawn for the original four, recoloured for
            // the others (LC-5).
            const render::Banner* banner =
                m_banners != nullptr ? m_banners->get(static_cast<sim::BannerId>(r.shield)) : nullptr;
            const render::BannerArt art = render::BannerArt::at(m_gfx_baumat + 17);
            const render::BannerArtSprite sprite =
                banner != nullptr ? render::banner_art(art, banner)
                                  : render::BannerArtSprite{art.first + std::min(r.shield, 3), std::nullopt};
            icon(sprite.gfx, 666);
            if (sprite.tint) {
                items.back().tint = ui::ColourTint{*sprite.tint, art.stride, art.variants, art.base, sprite.pattern};
            }
        } else if (const auto native = r.natives ? ui::package_icon_sprite("open1602:native") : std::nullopt) {
            // A native village: the native figure next to the number like the coat of arms, as in the
            // History Edition.
            icon(*native, 666);
        } else {
            icon(m_gfx_baumat + 15, 646);
        }
        text(std::to_string(*r.inhabitants), 696);
    }
    m_renderer->draw(items, x0, v.y, s);
}

void Hud::draw_unit_arms(const std::vector<UnitArms>& marks, sim::BannerId banner) {
    if (marks.empty()) {
        return;
    }
    const render::Banner* b = m_banners != nullptr ? m_banners->get(banner) : nullptr;
    const render::BannerArt art = render::BannerArt::at(m_gfx_baumat + 17);
    const render::BannerArtSprite sprite =
        b != nullptr ? render::banner_art(art, b)
                     : render::BannerArtSprite{art.first + std::min<std::int32_t>(banner, 3), std::nullopt};
    constexpr std::uint8_t kPole = 28;  // dark brown, as the frames of the key overview
    std::vector<ui::DrawItem> items;
    items.reserve(2 * marks.size());
    for (const UnitArms& m : marks) {
        ui::DrawItem pole;
        pole.kind = ui::DrawItem::Kind::Fill;
        pole.colour = kPole;
        pole.w = std::max(1, m.arms.w / 8);
        pole.x = m.pole_x - pole.w / 2;
        pole.y = m.pole_top;
        pole.h = std::max(1, m.pole_foot - m.pole_top);
        items.push_back(pole);
        ui::DrawItem arms;
        arms.kind = ui::DrawItem::Kind::Sprite;
        arms.gfx = sprite.gfx;
        arms.x = m.arms.x;
        arms.y = m.arms.y;
        arms.w = m.arms.w;
        arms.h = m.arms.h;
        if (sprite.tint) {
            arms.tint = ui::ColourTint{*sprite.tint, art.stride, art.variants, art.base, sprite.pattern};
        }
        items.push_back(std::move(arms));
    }
    m_renderer->draw(items, 0, 0, m_regions.scale);
}

SelectionPanel* Hud::selection_panel(std::optional<PanelContext>& context) {
    if (m_simulation == nullptr || m_buildings == nullptr || m_figure_table == nullptr || !m_selection_panels) {
        return nullptr;
    }
    if (m_mode == kModeMilitary) {
        context.emplace(PanelContext{*m_simulation, *m_buildings, *m_figure_table, m_texts ? &*m_texts : nullptr,
                                     m_local, m_units, m_banners, &m_names});
        context->ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
        context->shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        context->now_ms = platform::ticks_ms();
        context->own_units = m_own_units;
        context->own_ships = m_own_ships;
        return m_selection_panels->military_panel_for(*context);
    }
    if (m_mode != kModeInfo || m_selection.kind == Selection::Kind::None) {
        return nullptr;
    }
    context.emplace(PanelContext{*m_simulation, *m_buildings, *m_figure_table, m_texts ? &*m_texts : nullptr, m_local,
                                 m_units, m_banners, &m_names, m_goods_trend});
    context->ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    context->shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    context->now_ms = platform::ticks_ms();
    context->own_units = m_own_units;
    context->own_ships = m_own_ships;
    return m_selection_panels->panel_for(m_selection, *context);
}

bool Hud::picking() {
    if (m_mode != kModeInfo && m_mode != kModeMilitary) {
        return false;
    }
    std::optional<PanelContext> context;
    SelectionPanel* p = selection_panel(context);
    return p != nullptr && p->picking();
}

bool Hud::step_back() {
    if (m_menu_active || (m_mode != kModeInfo && m_mode != kModeMilitary)) {
        return false;
    }
    std::optional<PanelContext> context;
    SelectionPanel* p = selection_panel(context);
    return p != nullptr && p->steps_back() && p->back();
}

bool Hud::panel_wheel(std::int32_t x, std::int32_t y, std::int32_t notches) {
    if (m_menu_active) {
        // The menus' lists scroll a row per notch (Open1602); the map behind them never zooms. A
        // window over the menu takes the wheel as it takes the clicks.
        if (!m_help && !m_question && !m_end_active && !m_display_open) {
            if (multiplayer_open()) {
                (void)m_multiplayer->wheel(m_multiplayer_model, notches);
            } else if (m_menu) {
                (void)m_menu->wheel(notches);
            }
        }
        return true;
    }
    if (m_options_model.mission_window && m_tutor && m_regions.tutor.contains(x, y)) {
        // The mission's text scrolls a line per notch, as its arrows (Open1602); the tutorial's
        // pages do not, and the map does not zoom under the window either.
        if (!m_tutorial) {
            m_tutor_line = std::max(0, m_tutor_line - notches);
        }
        return true;
    }
    if (!m_regions.side_panel.contains(x, y)) {
        return false;
    }
    if (m_mode == kModeOptions) {
        return m_options && m_options->wheel(notches, m_options_model);
    }
    if (m_mode != kModeInfo && m_mode != kModeMilitary) {
        return false;
    }
    std::optional<PanelContext> context;
    SelectionPanel* p = selection_panel(context);
    return p != nullptr && p->wheel(notches);
}

void Hud::map_click(std::uint32_t x, std::uint32_t y) {
    std::optional<PanelContext> context;
    if (SelectionPanel* p = selection_panel(context); p != nullptr && p->picking()) {
        p->picked(x, y, m_selection, *context, m_commands);
    }
}

std::optional<Hud::Action> Hud::toggle_window(Selection::Kind kind) {
    if (m_mode == kModeInfo && m_selection.kind == kind) {
        m_selection = Selection{};
        return std::nullopt;
    }
    std::optional<Action> action;
    if (m_mode != kModeInfo) {
        action = set_mode(kModeInfo);
    }
    Selection next;
    next.kind = kind;
    m_selection = stamped_selection(m_selection, next, m_selection_serial);
    std::optional<PanelContext> context;
    if (SelectionPanel* p = selection_panel(context)) {
        p->opened();
    }
    return action;
}

std::optional<Hud::Action> Hud::open_diplomacy(sim::PlayerId partner) {
    std::optional<Action> action;
    if (m_mode != kModeInfo) {
        action = set_mode(kModeInfo);
    }
    // Always a new serial: the window takes the partner even when it is open already.
    Selection next;
    next.kind = Selection::Kind::Diplomacy;
    next.partner = partner;
    next.serial = ++m_selection_serial;
    const bool was_open = m_selection.kind == Selection::Kind::Diplomacy;
    m_selection = next;
    std::optional<PanelContext> context;
    if (SelectionPanel* p = selection_panel(context); p != nullptr && !was_open) {
        p->opened();
    }
    return action;
}

bool Hud::back() {
    if (chat_open()) {
        m_chat->close();
        return true;
    }
    if (m_blueprint_at) {
        m_blueprint_at.reset();
        return true;
    }
    if (templates_open()) {
        m_templates->close();
        return true;
    }
    if (m_editing && m_chrome && !m_menu_active && m_chrome->close_menu()) {
        return true;
    }
    if (m_editing && m_editor && !m_menu_active && m_editor->back()) {
        return true;
    }
    if (m_display_open) {
        m_display_open = false;
        return true;
    }
    if (m_goods_open && !m_menu_active) {
        goods_back();
        return true;
    }
    if (m_end_active) {
        if (const auto choice = m_end.back()) {
            m_end_choice = choice;
        }
        return true;  // no keys on the final screen of a lost game
    }
    if (multiplayer_open()) {
        if (auto e = m_multiplayer->back()) {
            m_multiplayer_events.push_back(std::move(*e));
        }
        return true;
    }
    if (m_menu_active) {
        return m_menu && m_menu->back();
    }
    if (m_mode == kModeOptions) {
        return m_options && m_options->back();
    }
    if (m_mode != kModeInfo && m_mode != kModeMilitary) {
        return false;
    }
    if (m_mode == kModeInfo &&
        (m_selection.kind == Selection::Kind::ShipList || m_selection.kind == Selection::Kind::CityList ||
         m_selection.kind == Selection::Kind::Diplomacy)) {
        // A page back (tribute -> treaties), else back to the player status.
        std::optional<PanelContext> context;
        if (SelectionPanel* p = selection_panel(context); p != nullptr && p->back()) {
            return true;
        }
        m_selection = Selection{};
        return true;
    }
    std::optional<PanelContext> context;
    SelectionPanel* p = selection_panel(context);
    return p != nullptr && p->back();
}

std::optional<Hud::Action> Hud::build_tool_action() {
    if (!m_build) {
        return std::nullopt;
    }
    if (m_build->demolish()) {
        return Action{Action::Kind::Demolish};
    }
    if (m_build->copy()) {
        return Action{Action::Kind::Copy};
    }
    if (m_build->moving()) {
        return Action{Action::Kind::Move};
    }
    const BuildPanel::Available available =
        m_available ? m_available : BuildPanel::Available([](std::uint32_t) { return true; });
    if (const auto id = m_build->current(available)) {
        Action a{Action::Kind::Build};
        a.building = *id;
        return a;
    }
    return Action{Action::Kind::ToolOff};
}

void Hud::found_kontor(std::uint32_t ship, std::uint32_t kontor) {
    m_ctrl.set_selected(m_mode, false);
    m_mode = kModeBuild;
    m_ctrl.set_selected(m_mode, true);
    if (m_build) {
        m_build->close_popup();
        m_build->show(kontor);
    }
    m_founding_ship = ship;
}

bool Hud::deselect_build_category() {
    if (m_menu_active || m_mode != kModeBuild || !m_build || m_founding_ship) {
        return false;
    }
    return m_build->deselect();
}

std::optional<Hud::Action> Hud::set_mode(std::int32_t mode) {
    if (mode < kModeBuild || mode > kModeOptions) {
        return std::nullopt;
    }
    m_founding_ship.reset();
    m_ctrl.set_selected(m_mode, false);
    if (mode == kModeOptions && m_mode != kModeOptions && m_options) {
        m_options->opened();
    }
    m_mode = mode;
    m_ctrl.set_selected(m_mode, true);
    if (m_build) {
        m_build->close_popup();
        if (mode == kModeBuild) {
            m_build->reselect();
        }
    }
    // The build mode arms the remembered building again, the others put
    // the tool away.
    return mode == kModeBuild ? build_tool_action() : std::optional<Action>(Action{Action::Kind::ToolOff});
}

ui::Rect Hud::layout(std::int32_t width, std::int32_t height, double ui_scale) {
    // The menus fill the window as far as they fit: the smaller of the two scales (the original:
    // width / 1024). The frame around the map takes the interface's scale, never larger.
    m_menu_scale =
        std::min(static_cast<double>(width) / ui::kDesignWidth, static_cast<double>(height) / ui::kDesignHeight);
    const double scale = std::isfinite(ui_scale) && ui_scale > 0.0 ? std::min(ui_scale, m_menu_scale) : m_menu_scale;
    m_layout_w = width;
    m_layout_h = height;
    m_layout_scale = ui_scale;
    m_regions = ui::screen_regions(width, height, scale);
    if (m_editing && m_chrome) {
        // The editor's own frame: a wider column on the right, its own status line below the map;
        // the menu and tool bars lie over the map's top and left edge.
        const auto column = static_cast<std::int32_t>(EditorChrome::kColumn * scale);
        const auto bar = static_cast<std::int32_t>(EditorChrome::kStatusH * scale);
        m_regions.side_panel = {width - column, 0, column, height};
        m_regions.status_bar = {0, height - bar, width - column, bar};
        m_regions.tutor = {};
        m_regions.game_view = {0, 0, width - column, height - bar};
        m_chrome->set_screen(width, height, scale);
    }
    m_ctrl.set_scale(scale);
    m_leist.set_scale(scale);
    if (m_build) {
        m_build->set_scale(scale);
    }
    if (m_info) {
        m_info->set_scale(scale);
    }
    if (m_options) {
        m_options->set_scale(scale);
    }
    if (m_menu) {
        m_menu->set_scale(m_menu_scale);
    }
    if (m_multiplayer) {
        m_multiplayer->set_scale(m_menu_scale);
    }
    if (m_display) {
        m_display->set_scale(scale);
    }
    if (m_chat) {
        m_chat->set_scale(scale);
    }
    if (m_blueprint) {
        m_blueprint->set_scale(scale);
    }
    if (m_templates) {
        m_templates->set_scale(scale);
    }
    if (m_goods) {
        m_goods->set_scale(scale);
    }
    m_end.set_scale(m_menu_scale);
    if (m_selection_panels) {
        m_selection_panels->set_scale(scale);
    }
    return m_regions.game_view;
}

bool Hud::covers(std::int32_t x, std::int32_t y) const noexcept {
    // The mission's window takes the clicks inside it; the menu and the window
    // "Anzeige" all of them.
    // A question takes all of them: nothing behind it answers the mouse.
    if (m_editing && m_chrome && m_chrome->covers(x, y)) {
        return true;
    }
    return m_menu_active || m_question.has_value() || m_display_open || m_goods_open ||
           (chat_open() && chat_rect().contains(x, y)) || (m_blueprint_at && blueprint_rect().contains(x, y)) ||
           (templates_open() && templates_rect().contains(x, y)) || m_regions.side_panel.contains(x, y) ||
           m_regions.status_bar.contains(x, y) ||
           (m_options_model.mission_window && m_tutor && m_regions.tutor.contains(x, y));
}

void Hud::set_editing(bool on) {
    const bool changed = m_editing != on;
    m_editing = on;
    if (changed && m_layout_w > 0) {
        (void)layout(m_layout_w, m_layout_h, m_layout_scale);
    }
    if (m_chrome) {
        (void)m_chrome->close_menu();
    }
    // The modes' buttons are greyed while the editor runs (clicks on them are ignored there).
    for (const std::int32_t mode : {kModeBuild, kModeMilitary, kModeInfo, kModeOptions}) {
        m_ctrl.set_disabled(mode, on);
    }
    if (m_editor) {
        m_editor->show_document();
    }
    m_editor_requests.clear();
}

void Hud::place_editor_panel() {
    if (!m_editor) {
        return;
    }
    // Below the minimap at the column's top.
    const double s = m_regions.scale;
    const ui::Rect map = minimap_rect();
    const std::int32_t top = map.w > 0 ? static_cast<std::int32_t>((map.y + map.h - m_regions.side_panel.y) / s) : 0;
    const auto height = static_cast<std::int32_t>(m_regions.side_panel.h / std::max(s, 0.01));
    m_editor->set_scale(s);
    m_editor->set_room(top + 12, std::max(0, height - top - 20));
}

void Hud::editor_tool(EditorTool tool) {
    if (!m_editor || m_editor->typing()) {
        return;
    }
    if (m_chrome) {
        (void)m_chrome->close_menu();
    }
    if (auto r = m_editor->choose_tool(tool, m_editor_model)) {
        m_editor_requests.push_back(std::move(*r));
    }
}

void Hud::editor_command(EditorCommand command) {
    if (!m_editor) {
        return;
    }
    if (m_chrome) {
        (void)m_chrome->close_menu();
    }
    if (auto r = m_editor->command(command, m_editor_model)) {
        m_editor_requests.push_back(std::move(*r));
    }
}

ui::Rect Hud::minimap_rect() const {
    const assets::Gadget* g = m_ctrl.layout().find(kMinimap);
    if (g == nullptr) {
        return {};
    }
    const double s = m_regions.scale;
    if (m_editing) {
        // At the top of the editor's column, in its middle.
        const auto w = static_cast<std::int32_t>(g->size[0] * s);
        const auto h = static_cast<std::int32_t>(g->size[1] * s);
        return {m_regions.side_panel.x + (m_regions.side_panel.w - w) / 2,
                m_regions.side_panel.y + ui::to_pixels(12, s), w, h};
    }
    return {m_regions.side_panel.x + ui::to_pixels(g->pos[0], s), m_regions.side_panel.y + ui::to_pixels(g->pos[1], s),
            static_cast<std::int32_t>(g->size[0] * s), static_cast<std::int32_t>(g->size[1] * s)};
}

ui::Rect Hud::minimap_image() const {
    // As in the original the world always fills the whole parchment,
    // stretched as it comes (500 x 350 on 186 x 160).
    return m_world_w == 0 || m_world_h == 0 ? ui::Rect{} : minimap_rect();
}

std::optional<std::pair<double, double>> Hud::minimap_tile(std::int32_t x, std::int32_t y) const {
    const ui::Rect at = minimap_image();
    if (at.w <= 0 || !at.contains(x, y)) {
        return std::nullopt;
    }
    const double tx = (x - at.x) * static_cast<double>(m_world_w) / at.w;
    const double ty = (y - at.y) * static_cast<double>(m_world_h) / at.h;
    if (tx < 0.0 || ty < 0.0 || tx >= m_world_w || ty >= m_world_h) {
        return std::nullopt;
    }
    return std::make_pair(tx, ty);
}

std::optional<std::pair<double, double>> Hud::last_message_tile() const {
    for (const Event& e : m_events) {
        if (e.tile) {
            return e.tile;
        }
    }
    return std::nullopt;
}

void Hud::draw_question() {
    if (!m_question) {
        return;
    }
    const ui::Rect r = layout_question();
    m_renderer->draw(m_question_form.draw(m_renderer->fonts(m_question_form.scale())), r.x, r.y,
                     m_question_form.scale());
}

namespace {

// The question's window in design units, built as the settings' windows (ui::Form): a heading,
// buttons 44 high, the answer of Enter highlighted.
constexpr std::int32_t kQuestionButtonW = 320;
constexpr std::int32_t kQuestionButtonH = 44;
constexpr std::int32_t kQuestionGap = 8;
constexpr std::int32_t kQuestionSwatch = 28;
constexpr std::int32_t kQuestionMargin = 24;
constexpr std::int32_t kQuestionTextY = 54;  ///< the question's lines after the first
constexpr std::int32_t kQuestionLine = 22;
constexpr std::int32_t kYesNoW = 180;
constexpr ui::Form::Id kQuestionYes = 1;
constexpr ui::Form::Id kQuestionNo = 2;
constexpr ui::Form::Id kQuestionOption = 10;

}  // namespace

ui::Rect Hud::layout_question() {
    const double s = m_menu_active ? m_menu_scale : m_regions.scale;
    const ui::Rect& v = m_menu_active ? m_regions.full : m_regions.game_view;
    // Yes and no side by side; options one under the other, in two columns when there are more
    // than seven.
    const std::size_t n = m_choices.size();
    const std::string& q = *m_question;
    const std::size_t nl = q.find('\n');
    const auto more_lines =
        nl == std::string::npos ? 0 : static_cast<std::int32_t>(std::count(q.begin(), q.end(), '\n'));
    const std::int32_t top = std::max(100, kQuestionTextY + more_lines * kQuestionLine + 2 * kQuestionGap);
    const std::int32_t columns = n > 7 ? 2 : 1;
    const auto rows = n == 0 ? 1
                             : static_cast<std::int32_t>((n + static_cast<std::size_t>(columns) - 1) /
                                                         static_cast<std::size_t>(columns));
    const std::int32_t cell = kQuestionSwatch + kQuestionGap + kQuestionButtonW;
    const std::int32_t row_width = columns * cell + (columns - 1) * 2 * kQuestionGap;
    const std::int32_t width = std::max(560, row_width + 2 * kQuestionMargin);
    const std::int32_t height = top + rows * (kQuestionButtonH + kQuestionGap) - kQuestionGap + kQuestionMargin;
    ui::Form& f = m_question_form;
    f.set_scale(s);
    f.clear();
    f.panel(ui::Rect{0, 0, width, height});
    f.label(ui::Rect{kQuestionMargin, 14, width - 2 * kQuestionMargin, 40}, q.substr(0, nl), ui::Align::Left,
            ui::Form::Tone::Heading);
    if (nl != std::string::npos) {
        f.paragraph(ui::Rect{kQuestionMargin, kQuestionTextY, width - 2 * kQuestionMargin, more_lines * kQuestionLine},
                    q.substr(nl + 1), ui::Form::Tone::Normal, kQuestionLine);
    }
    if (n == 0) {
        // Enter answers yes: that button is highlighted.
        const std::int32_t y = top;
        const std::int32_t gap = 40;
        const std::int32_t x = (width - 2 * kYesNoW - gap) / 2;
        f.button(kQuestionYes, ui::Rect{x, y, kYesNoW, kQuestionButtonH}, ui::tr("dialog.yes"), true, true);
        f.button(kQuestionNo, ui::Rect{x + kYesNoW + gap, y, kYesNoW, kQuestionButtonH}, ui::tr("dialog.no"));
    }
    const std::int32_t left = (width - row_width) / 2;
    for (std::size_t i = 0; i < n; ++i) {
        const auto column = static_cast<std::int32_t>(i / static_cast<std::size_t>(rows));
        const auto row = static_cast<std::int32_t>(i % static_cast<std::size_t>(rows));
        const std::int32_t x = left + column * (cell + 2 * kQuestionGap);
        const std::int32_t y = top + row * (kQuestionButtonH + kQuestionGap);
        // Enter takes the first option: it is highlighted. The banner's colour is a field at the
        // left end of the button, so that a click on it chooses the option too.
        f.button(kQuestionOption + static_cast<ui::Form::Id>(i), ui::Rect{x, y, cell, kQuestionButtonH},
                 m_choices[i].text, true, i == 0);
        if (const auto& banner = m_choices[i].banner) {
            const std::int32_t colour =
                *banner < m_banner_palette.size() ? m_banner_palette[*banner] : m_other_banner_palette;
            f.swatch(ui::Rect{x + kQuestionGap + 2, y + 10, kQuestionSwatch, kQuestionButtonH - 20}, colour);
        }
    }
    const auto w = static_cast<std::int32_t>(width * s);
    const auto h = static_cast<std::int32_t>(height * s);
    return {v.x + std::max(0, (v.w - w) / 2), v.y + std::max(0, (v.h - h) / 2), w, h};
}

std::optional<Hud::Action> Hud::question_mouse(std::int32_t x, std::int32_t y, bool down) {
    const ui::Rect r = layout_question();
    const auto id = m_question_form.mouse(x - r.x, y - r.y, down);
    if (!id) {
        return std::nullopt;
    }
    Action a{Action::Kind::Answer};
    if (*id == kQuestionYes || *id == kQuestionNo) {
        a.step = *id == kQuestionYes ? 1 : 0;
    } else if (*id >= kQuestionOption && static_cast<std::size_t>(*id - kQuestionOption) < m_choices.size()) {
        a.step = *id - kQuestionOption;
    } else {
        return std::nullopt;
    }
    close_question();
    return a;
}

ui::Rect Hud::display_rect() const {
    const double s = m_display ? m_display->scale() : m_regions.scale;
    const auto w = static_cast<std::int32_t>(DisplayMenu::kWidth * s);
    const auto h = static_cast<std::int32_t>(DisplayMenu::kHeight * s);
    // Over the main menu in the middle of the window, else over the map.
    const ui::Rect& v = m_menu_active ? m_regions.full : m_regions.game_view;
    return {v.x + std::max(0, (v.w - w) / 2), v.y + std::max(0, (v.h - h) / 2), w, h};
}

ui::Rect Hud::chat_rect() const {
    const double s = m_chat ? m_chat->scale() : m_regions.scale;
    const auto w = static_cast<std::int32_t>(ChatWindow::kWidth * s);
    const auto h = static_cast<std::int32_t>((m_chat ? m_chat->height(m_chat_model) : 0) * s);
    const auto gap = static_cast<std::int32_t>(8 * s);
    const ui::Rect& v = m_regions.game_view;
    return {v.x + gap, std::max(v.y, v.y + v.h - h - gap), w, h};
}

ui::Rect Hud::blueprint_rect() const {
    // Over the upper left of the map, where it hides little of what the player builds.
    const double s = m_blueprint ? m_blueprint->scale() : m_regions.scale;
    const auto gap = static_cast<std::int32_t>(8 * s);
    const ui::Rect& v = m_regions.game_view;
    return {v.x + gap, v.y + gap, static_cast<std::int32_t>(BlueprintWindow::kWidth * s),
            static_cast<std::int32_t>(BlueprintWindow::kHeight * s)};
}

ui::Rect Hud::templates_rect() const {
    // Over the upper right of the map, by the side panel and its tools.
    const double s = m_templates ? m_templates->scale() : m_regions.scale;
    const auto gap = static_cast<std::int32_t>(8 * s);
    const auto w = static_cast<std::int32_t>(TemplateWindow::kWidth * s);
    const auto h = static_cast<std::int32_t>((m_templates ? m_templates->height(m_templates_model) : 0) * s);
    const ui::Rect& v = m_regions.game_view;
    return {std::max(v.x, v.x + v.w - w - gap), v.y + gap, w, h};
}

bool Hud::open_chat() {
    if (!m_chat || !m_chat_model.available) {
        return false;
    }
    m_chat->open();
    return true;
}

ui::Rect Hud::goods_rect() const {
    const double s = m_goods ? m_goods->scale() : m_regions.scale;
    const auto w = static_cast<std::int32_t>(GoodsWindow::kWidth * s);
    const auto h = static_cast<std::int32_t>(GoodsWindow::kHeight * s);
    const ui::Rect& v = m_regions.game_view;
    return {v.x + std::max(0, (v.w - w) / 2), v.y + std::max(0, (v.h - h) / 2), w, h};
}

void Hud::menu_origin(std::int32_t& x, std::int32_t& y) const {
    const double s = m_menu_scale;
    x = std::max(0, (m_regions.full.w - static_cast<std::int32_t>(ui::kDesignWidth * s)) / 2);
    y = std::max(0, (m_regions.full.h - static_cast<std::int32_t>(ui::kDesignHeight * s)) / 2);
}

ui::Rect Hud::menu_video_rect() const {
    std::int32_t ox = 0;
    std::int32_t oy = 0;
    menu_origin(ox, oy);
    const double s = m_menu_scale;
    const ui::Rect f = MainMenu::video_frame();
    return {ox + ui::to_pixels(f.x, s), oy + ui::to_pixels(f.y, s), ui::to_pixels(f.w, s), ui::to_pixels(f.h, s)};
}

void Hud::draw_menu() {
    m_renderer->fill(m_regions.full, 0);
    std::int32_t ox = 0;
    std::int32_t oy = 0;
    menu_origin(ox, oy);
    if (m_end_active) {
        m_renderer->draw(m_end.draw(m_renderer->fonts(m_end.scale())), ox, oy, m_end.scale());
    } else if (multiplayer_open()) {
        m_renderer->draw(m_multiplayer->draw(m_multiplayer_model, m_renderer->fonts(m_menu_scale)), ox, oy,
                         m_menu_scale);
    } else if (m_menu) {
        m_renderer->draw(m_menu->draw(m_renderer->fonts(m_menu_scale)), ox, oy, m_menu_scale);
        if (m_display_open && m_display) {
            const ui::Rect r = display_rect();
            m_renderer->draw(m_display->draw(m_display_model, m_renderer->fonts(m_display->scale())), r.x, r.y,
                             m_display->scale());
        }
    }
    draw_question();
    draw_help();
}

void Hud::open_end_screen(sim::EndScreenSummary summary, bool view_only) {
    m_end.open(std::move(summary), view_only);
    m_end_choice.reset();
    m_end_active = true;
    m_menu_active = true;
}

void Hud::close_end_screen(EndScreen::Choice choice) {
    m_end_active = false;
    m_end_choice.reset();
    if (choice != EndScreen::Choice::Resume && m_menu) {
        open_menu();
    } else {
        m_menu_active = false;
    }
}

void Hud::draw_mission(const sim::World& world) {
    if (!m_options_model.mission_window || !m_tutor || m_simulation == nullptr) {
        return;
    }
    const sim::Player* p = world.player(m_local);
    const sim::Mission* mission = p != nullptr ? world.mission_of(*p) : nullptr;
    if (mission == nullptr && !world.missions().empty()) {
        mission = &world.missions().front();  // the endless game: one mission without owner, only its text
    }
    // The window of the original (region above the status bar): its art, the text
    // flowing in 110002; the lines and buttons of the tutorial stay hidden.
    m_tutor->set_scale(m_regions.scale);
    // As wide as the game view (the original's art is 768 wide): the art widened, the text wider,
    // the arrows at the right end.
    const ui::Rect& window = m_regions.tutor;
    const std::int32_t extra = std::max(0, static_cast<std::int32_t>(window.w / m_regions.scale) - kDesignViewW);
    for (const auto& [id, pos] : m_tutor_right) {
        m_tutor->set_position(id, pos[0] + extra, pos[1]);
    }
    m_tutor->set_size(kTutorText, m_tutor_text_size[0] + extra, m_tutor_text_size[1]);
    m_renderer->draw_widened(0, m_gfx_tutor, window, m_regions.scale);
    if (m_tutorial) {
        // The tutorial: six lines of its page instead of the mission's text, „weiter“
        // only where the chapter pages forward by hand, never "back".
        m_tutor->set_visible(kTutorText, false);
        const auto first = static_cast<std::size_t>(m_tutorial->page()) * Tutorial::kLinesPerPage;
        for (std::size_t i = 0; i < Tutorial::kLinesPerPage; ++i) {
            const auto id = kTutorLine + static_cast<std::int32_t>(i);
            m_tutor->set_visible(id, true);
            m_tutor->set_text(id, first + i < m_tutor_lines.size() ? m_tutor_lines[first + i] : std::string());
        }
        const bool next = m_tutorial->can_page_forward();
        for (std::int32_t i = 0; i < kTutorArrowParts; ++i) {
            m_tutor->set_visible(kTutorBack + i, false);
            m_tutor->set_visible(kTutorNext + i, next);
        }
        m_renderer->draw(m_tutor->draw(m_renderer->fonts(m_regions.scale)), m_regions.tutor.x, m_regions.tutor.y,
                         m_regions.scale);
        return;
    }
    m_tutor->set_visible(kTutorText, true);
    // The victory conditions of an endless game after its text.
    std::string text = mission != nullptr && !mission->text.empty() ? m_game.expand(mission->text) : std::string();
    if (const std::string victory = victory_text(m_simulation->rules().victory); !victory.empty()) {
        text += text.empty() ? victory : "\n" + victory;
    }
    m_tutor->set_text(110002, text.empty() ? std::string("-") : text);
    for (std::int32_t id = 110011; id <= 110016; ++id) {
        m_tutor->set_visible(id, false);
    }
    // The scroll arrows (one picture, the back arrow's, and both arrows' parts) only for a text
    // longer than the window, as in the original; the text then ends left of them.
    bool scrolls = m_tutor->max_scroll(110002, m_renderer->fonts(m_regions.scale)) > 0;
    if (scrolls && m_tutor_arrows_x != std::numeric_limits<std::int32_t>::max()) {
        const std::int32_t room = m_tutor_arrows_x + extra - m_tutor_text_x - 4;
        m_tutor->set_size(kTutorText, std::clamp(room, 1, m_tutor_text_size[0] + extra), m_tutor_text_size[1]);
        scrolls = m_tutor->max_scroll(110002, m_renderer->fonts(m_regions.scale)) > 0;
    }
    for (std::int32_t i = 0; i < kTutorArrowParts; ++i) {
        m_tutor->set_visible(kTutorBack + i, scrolls);
        m_tutor->set_visible(kTutorNext + i, scrolls && i != 0);
    }
    // Clamped here, where the text and the window's width are known: "down" past the end would
    // blank the window and take as many clicks back up.
    m_tutor_line = std::min(m_tutor_line, m_tutor->max_scroll(110002, m_renderer->fonts(m_regions.scale)));
    m_tutor->set_scroll(110002, m_tutor_line);
    m_renderer->draw(m_tutor->draw(m_renderer->fonts(m_regions.scale)), m_regions.tutor.x, m_regions.tutor.y,
                     m_regions.scale);
}

void Hud::reset_game() {
    m_events.clear();
    m_offers.clear();
    m_selection = Selection{};
    close_question();
    m_help.reset();
    m_goods_open = false;
    m_modal_press = ModalPress::None;
    m_commands.clear();
    m_names.cancel();
    m_minimap_rebuild = true;
    m_tutor_line = 0;
    m_tutorial.reset();
    m_bubbles.clear();
}

void Hud::set_tutorial(std::optional<std::uint8_t> chapter) {
    m_bubbles.clear();
    m_tutorial_facts = {};
    m_tutorial_facts_ms = 0;
    if (!chapter || m_simulation == nullptr) {
        m_tutorial.reset();
        return;
    }
    m_tutorial.emplace(*chapter);
    m_tutorial->start(m_simulation->world(), m_local);
}

TutorialView Hud::tutorial_view(const BuildPanel::Available& available) {
    TutorialView v;
    switch (m_mode) {
        case kModeBuild:
            v.mode = TutorialView::Mode::Build;
            break;
        case kModeMilitary:
            v.mode = TutorialView::Mode::Military;
            break;
        case kModeInfo:
            v.mode = m_selection.kind == Selection::Kind::Diplomacy ? TutorialView::Mode::Diplomacy
                                                                    : TutorialView::Mode::Info;
            break;
        default:
            v.mode = TutorialView::Mode::Settings;
            break;
    }
    if (m_mode == kModeInfo && m_simulation != nullptr && m_buildings != nullptr) {
        const sim::World& world = m_simulation->world();
        switch (m_selection.kind) {
            case Selection::Kind::Ship:
                v.window = TutorialView::Window::Ship;
                v.ship = m_selection.ship;
                break;
            case Selection::Kind::Building:
                v.window = TutorialView::Window::Other;
                if (m_selection.island < world.islands().size()) {
                    const sim::IslandMap& map = world.islands()[m_selection.island].map;
                    if (map.contains(m_selection.x, m_selection.y)) {
                        const assets::BuildingType* b =
                            map.sub_tile(*m_buildings, m_selection.x, m_selection.y).building;
                        const sim::PlayerId owner = map.top(m_selection.x, m_selection.y).owner;
                        const std::int32_t kind = b != nullptr ? b->prod_kind : 0;
                        if (kind == sim::prod::kHouse && owner == m_local) {
                            v.window = TutorialView::Window::OwnHouse;
                        } else if ((kind == sim::prod::kKontor || kind == sim::prod::kMarket) && owner != m_local &&
                                   owner != sim::kNoPlayer) {
                            v.window = TutorialView::Window::PartnerKontor;
                        }
                    }
                }
                break;
            case Selection::Kind::ShipList:
            case Selection::Kind::CityList:
                v.window = TutorialView::Window::Other;
                break;
            default:
                break;
        }
    }
    if (m_mode == kModeInfo || m_mode == kModeMilitary) {
        std::optional<PanelContext> context;
        if (SelectionPanel* p = selection_panel(context)) {
            p->describe(v);
            v.picking_patrol = m_mode == kModeMilitary && p->picking();
        }
    }
    if (m_founding_ship) {
        // The original places the Kontor from a page of the ship's window: „Bitte rechte
        // Maustaste klicken!“ instead of the build mode's bubbles.
        v.mode = TutorialView::Mode::Info;
        v.window = TutorialView::Window::Ship;
        v.ship = *m_founding_ship;
        v.ship_page = TutorialView::ShipPage::Other;
    }
    if (m_units != nullptr) {
        v.units = m_units->units();
    }
    if (m_build) {
        v.popup_open = m_build->popup_open();
        v.popup_category = m_build->popup_category();
        v.menu_building = m_build->current(available);
    }
    v.holding_building = m_tool_holding;
    v.short_of_wood = m_tool_short_of_wood;
    return v;
}

void Hud::step_tutorial(const BuildPanel::Available& available, std::uint64_t now_ms) {
    m_bubbles.clear();
    if (!m_tutorial || m_simulation == nullptr || m_buildings == nullptr || m_figure_table == nullptr) {
        return;
    }
    const TutorialView v = tutorial_view(available);
    // Chapter 1 searches the roads of the town: not every frame.
    if (m_tutorial->chapter() != 1 || m_tutorial_facts_ms == 0 || now_ms < m_tutorial_facts_ms ||
        now_ms >= m_tutorial_facts_ms + kTutorialFactsMs) {
        m_tutorial_facts = m_tutorial->facts(v, *m_simulation, *m_buildings, *m_figure_table, m_local);
        m_tutorial_facts_ms = std::max<std::uint64_t>(now_ms, 1);
    }
    m_bubbles = m_tutorial->step(v, m_tutorial_facts);
}

void Hud::draw_bubbles() {
    if (!m_bubble_panel || m_bubbles.empty()) {
        return;
    }
    const double s = m_regions.scale;
    m_bubble_panel->set_scale(s);
    const std::int32_t extra = std::max(0, static_cast<std::int32_t>(m_regions.tutor.w / s) - kDesignViewW);
    for (const std::uint8_t k : m_bubbles) {
        const std::int32_t id = kFirstBubble + 10 * k;
        std::int32_t x = 0;
        std::int32_t y = 0;
        switch (bubble_anchor(k)) {
            case BubbleAnchor::SidePanel:
                x = m_regions.side_panel.x - static_cast<std::int32_t>(kDesignViewW * s);
                y = m_regions.side_panel.y;
                break;
            case BubbleAnchor::Map:
                x = m_regions.game_view.x;
                y = m_regions.game_view.y;
                break;
            case BubbleAnchor::TutorLeft:
            case BubbleAnchor::TutorRight:
                x = m_regions.tutor.x +
                    (bubble_anchor(k) == BubbleAnchor::TutorRight ? static_cast<std::int32_t>(extra * s) : 0);
                y = m_regions.tutor.y - static_cast<std::int32_t>(kTutorDesignY * s);
                break;
        }
        for (std::int32_t i = 0; i < kBubbleParts; ++i) {
            m_bubble_panel->set_visible(id + i, true);
        }
        m_renderer->draw(m_bubble_panel->draw(m_renderer->fonts(s)), x, y, s);
        for (std::int32_t i = 0; i < kBubbleParts; ++i) {
            m_bubble_panel->set_visible(id + i, false);
        }
    }
}

ui::Rect Hud::help_box() const {
    const double s = m_menu_active ? m_menu_scale : m_regions.scale;
    const ui::Rect& v = m_regions.game_view;
    const auto px = [&](double d) { return static_cast<std::int32_t>(d * s); };
    // The window keeps its size from page to page: as high as the longest page.
    const std::int32_t w = std::min(v.w - px(16), px(760));
    std::size_t rows = 0;
    if (m_help) {
        const ui::Font* font = m_renderer->fonts(s)[1];
        for (const KeyHelpPage& p : *m_help) {
            const HelpColumns c = help_columns(font, p, w / s);
            const auto wrapped = help_wrapped(font, p, c.text);
            rows = std::max(rows,
                            key_help_rows(p, [&](std::size_t sec, std::size_t l) { return wrapped[sec][l].size(); }));
        }
    }
    const std::int32_t h = std::min(v.h - px(16), px(112) + static_cast<std::int32_t>(rows) * px(14));
    return ui::Rect{v.x + (v.w - w) / 2, v.y + (v.h - h) / 2, w, h};
}

ui::Rect Hud::help_tab(const ui::Rect& box, std::size_t page) const {
    const double s = m_menu_active ? m_menu_scale : m_regions.scale;
    const auto px = [&](double d) { return static_cast<std::int32_t>(d * s); };
    // The tabs side by side, a gap between them, the row centred in the window.
    const auto n = static_cast<std::int32_t>(kKeyHelpPages);
    const std::int32_t gap = px(4);
    const std::int32_t w = (box.w - 2 * px(kHelpMargin) + gap) / n;
    const std::int32_t left = box.x + (box.w - (n * w - gap)) / 2;
    return ui::Rect{left + static_cast<std::int32_t>(page) * w, box.y + px(40), w - gap, px(20)};
}

void Hud::draw_help() {
    if (!m_help) {
        return;
    }
    const double s = m_menu_active ? m_menu_scale : m_regions.scale;
    const auto px = [&](double d) { return static_cast<std::int32_t>(d * s); };
    const ui::Rect box = help_box();
    constexpr std::uint8_t kGold = 215;
    constexpr std::uint8_t kDark = 28;
    m_renderer->fill(ui::Rect{box.x - px(3), box.y - px(3), box.w + px(6), box.h + px(6)}, kGold);
    m_renderer->fill(box, kDark);
    // The tabs: the page shown framed thick, the others thin; a gold line below them all.
    const ui::Rect first = help_tab(box, 0);
    const ui::Rect last = help_tab(box, kKeyHelpPages - 1);
    m_renderer->fill(ui::Rect{first.x, first.y + first.h, last.x + last.w - first.x, std::max(1, px(1))}, kGold);
    for (std::size_t p = 0; p < kKeyHelpPages; ++p) {
        const ui::Rect t = help_tab(box, p);
        const std::int32_t edge = p == m_help_page ? px(2) : std::max(1, px(1));
        m_renderer->fill(t, kGold);
        m_renderer->fill(ui::Rect{t.x + edge, t.y + edge, t.w - 2 * edge, t.h - (p == m_help_page ? edge : 2 * edge)},
                         kDark);
    }
    std::vector<ui::DrawItem> items;
    const auto text = [&](const std::string& t, std::int32_t x, std::int32_t y, ui::Align align,
                          std::int32_t font = 1) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = font;  // the small font (ZEILITTLE), headings in the normal one
        d.colour = 4;
        d.align = align;
        d.x = x;
        d.y = y;
        d.text = t;
        items.push_back(d);
    };
    text(ui::tr("keys.title"), box.w / 2, px(28), ui::Align::Centre, 0);
    // The tab titles in the small font, cut to their tab's width and centred on its upper part, so
    // that descenders ("g", "p") keep clear of the bottom edge; the page shown in yellow (a larger
    // font would not fit the tabs).
    const ui::Font* tab_font = m_renderer->fonts(s)[1];
    for (std::size_t p = 0; p < kKeyHelpPages; ++p) {
        const ui::Rect t = help_tab(box, p);
        const double inner = std::max(0.0, t.w / s - 8.0);
        const std::int32_t top = static_cast<std::int32_t>(std::lround((t.y - box.y) / s));
        const std::int32_t h = static_cast<std::int32_t>(std::lround(t.h / s));
        text(ui::fit_text(tab_font, (*m_help)[p].title, inner, false), t.x - box.x + t.w / 2,
             px(ui::text_bottom(tab_font, top, h - 4)), ui::Align::Centre);
        items.back().colour = p == m_help_page ? 3 : 1;
    }
    const KeyHelpPage& page = (*m_help)[m_help_page];
    // Two columns; a text too long for its column wraps onto the rows below it.
    const ui::Font* font = m_renderer->fonts(s)[1];
    const HelpColumns c = help_columns(font, page, box.w / s);
    const auto wrapped = help_wrapped(font, page, c.text);
    const KeyHelpHeight height = [&](std::size_t sec, std::size_t l) { return wrapped[sec][l].size(); };
    const auto place = [&](std::size_t section, std::size_t l) {
        const KeyHelpCell cell = key_help_cell(page, section, l, height);
        return std::pair{kHelpMargin + static_cast<double>(cell.column) * c.column,
                         84.0 + static_cast<double>(cell.row) * 14.0};
    };
    for (std::size_t sec = 0; sec < page.sections.size(); ++sec) {
        const KeyHelpSection& section = page.sections[sec];
        const auto [hx, hy] = place(sec, SIZE_MAX);
        text(section.heading, px(hx), px(hy + 1), ui::Align::Left, 0);
        for (std::size_t i = 0; i < section.lines.size(); ++i) {
            const auto [x, y] = place(sec, i);
            const std::vector<std::string>& lines = wrapped[sec][i];
            for (std::size_t k = 0; k < lines.size(); ++k) {
                const double row = y + static_cast<double>(k) * 14.0;
                if (px(row) > box.h - px(24)) {
                    break;  // a small window: what does not fit is left out
                }
                if (k == 0) {
                    text(ui::fit_text(font, section.lines[i].keys, c.keys, false), px(x + kHelpIndent), px(row),
                         ui::Align::Left);
                }
                text(lines[k], px(x + kHelpIndent + c.keys + kHelpKeyGap), px(row), ui::Align::Left);
            }
        }
    }
    text(ui::tr("keys.footer"), box.w / 2, box.h - px(10), ui::Align::Centre);
    m_renderer->draw(items, box.x, box.y, s);
}

std::optional<Hud::Action> Hud::mouse(std::int32_t x, std::int32_t y, bool down, ui::MouseButton button) {
    std::optional<Action> action;
    // Over the main menu too: open_menu() closes the game's, so one open there came from its
    // "Options".
    if (m_display_open && !m_end_active && !multiplayer_open() && !m_question && !m_help) {
        const ui::Rect r = display_rect();
        switch (m_display->mouse(x - r.x, y - r.y, down, m_display_model, button)) {
            case DisplayMenu::Result::Changed:
                m_display_changed = true;
                break;
            case DisplayMenu::Result::Close:
                m_display_open = false;
                break;
            case DisplayMenu::Result::None:
                break;
        }
        return std::nullopt;
    }
    if (m_goods_open && !m_menu_active && !m_question && !m_help) {
        const ui::Rect r = goods_rect();
        switch (m_goods->mouse(x - r.x, y - r.y, down, button)) {
            case GoodsWindow::Result::Close:
                m_goods_open = false;
                break;
            case GoodsWindow::Result::Ship:
                // A ship of the tab "Routes": the window closes and the ship is selected and looked at.
                if (const auto ship = m_goods->take_ship()) {
                    m_goods_open = false;
                    Action a{Action::Kind::SelectShip, ship->x, ship->y};
                    a.ship = ship->id;
                    return a;
                }
                break;
            case GoodsWindow::Result::None:
                break;
        }
        return std::nullopt;
    }
    if (m_blueprint_at && m_blueprint && !m_menu_active && !m_question && !m_help && button == ui::MouseButton::Left) {
        // The blueprint's window takes the clicks on it; the map beside it stays usable.
        const ui::Rect r = blueprint_rect();
        const BlueprintWindow::Result result = m_blueprint->mouse(x - r.x, y - r.y, down, m_blueprint_model);
        if (result == BlueprintWindow::Result::Close) {
            m_blueprint_at.reset();
        } else if (result != BlueprintWindow::Result::None) {
            m_blueprint_clicks.push_back(result);
        }
        if (r.contains(x, y)) {
            return std::nullopt;
        }
    }
    if (templates_open() && !m_question && !m_help && button == ui::MouseButton::Left) {
        // The templates' window takes the clicks on it; the map beside it stays usable.
        const ui::Rect r = templates_rect();
        if (auto request = m_templates->mouse(x - r.x, y - r.y, down, m_templates_model)) {
            m_template_requests.push_back(std::move(*request));
        }
        if (r.contains(x, y)) {
            return std::nullopt;
        }
    }
    if (chat_open() && !m_menu_active && !m_question && !m_help && button == ui::MouseButton::Left) {
        // The chat takes the clicks on it; the map beside it stays usable.
        const ui::Rect r = chat_rect();
        (void)m_chat->mouse(x - r.x, y - r.y, down, m_chat_model);
        if (r.contains(x, y)) {
            return std::nullopt;
        }
    }
    if (m_editing && m_chrome && !m_menu_active && !m_help && !m_question && !m_end_active) {
        // The editor's bars first: their buttons, and an open menu that a press beside closes.
        if (button == ui::MouseButton::Left) {
            bool taken = false;
            const std::optional<EditorTool> active = m_editor ? m_editor->active_tool(m_editor_model) : std::nullopt;
            const EditorChrome::Result r = m_chrome->mouse(x, y, down, m_editor_model, m_chrome_model, active, taken);
            if (r.tool) {
                editor_tool(*r.tool);
            }
            if (r.command) {
                editor_command(*r.command);
            }
            if (taken) {
                return std::nullopt;
            }
        } else if (m_chrome->covers(x, y)) {
            return std::nullopt;
        }
    }
    if (button != ui::MouseButton::Left && m_editing && !m_menu_active && !m_help && !m_question && !m_end_active &&
        m_editor) {
        // The right button goes back through the values of the editor's panel, and does nothing else.
        place_editor_panel();
        const ui::Rect& side = m_regions.side_panel;
        if (auto r = m_editor->mouse(x - side.x, y - side.y, down, m_editor_model, button)) {
            m_editor_requests.push_back(std::move(*r));
        }
        return std::nullopt;
    }
    if (button != ui::MouseButton::Left && (m_help || m_question || m_end_active || !m_menu_active || !m_menu)) {
        return std::nullopt;  // the right button only goes through the values of the menus' dialogs
    }
    // Mouse motion also comes here with down == false: only a release after a press is a click.
    if (m_help) {
        // The key overview takes the click: on a tab it turns to that page, outside the window it
        // closes, elsewhere in it nothing happens (a click just beside a tab must not close it).
        if (down) {
            m_modal_press = ModalPress::Elsewhere;
        } else if (m_modal_press != ModalPress::None) {
            m_modal_press = ModalPress::None;
            const ui::Rect box = help_box();
            for (std::size_t p = 0; p < kKeyHelpPages; ++p) {
                if (help_tab(box, p).contains(x, y)) {
                    m_help_page = p;
                    return std::nullopt;
                }
            }
            if (!box.contains(x, y)) {
                m_help.reset();
            }
        }
        return std::nullopt;
    }
    if (!m_menu_active && !m_editing && m_options_model.mission_window && m_tutor && m_regions.tutor.contains(x, y)) {
        // The mission's window: X closes it, the arrows scroll its text. In the tutorial X ends the
        // chapter and „weiter“ turns the page.
        for (const ui::PanelEvent& e : m_tutor->mouse(x - m_regions.tutor.x, y - m_regions.tutor.y, down)) {
            if (e.kind != ui::PanelEvent::Kind::Clicked) {
                continue;
            }
            if (m_tutorial) {
                if (e.id == kTutorClose) {
                    Action a{Action::Kind::EndTutorial};
                    a.step = m_tutorial->chapter();
                    return a;
                }
                if (e.id > kTutorNext && e.id < kTutorNext + kTutorArrowParts) {
                    m_tutorial->page_forward();
                }
                continue;
            }
            if (e.id == 110001) {
                m_options_model.mission_window = false;
                m_option_events.push_back(OptionsEvent{OptionsEvent::Kind::MissionWindow});
            } else if (e.id >= 110022 && e.id <= 110026) {
                m_tutor_line = std::max(0, m_tutor_line - 1);
            } else if (e.id >= 110032 && e.id <= 110036) {
                ++m_tutor_line;
            }
        }
        return std::nullopt;
    }
    if (m_question) {
        // The question takes every click until it is answered: pressed and released on the same
        // button (as the original's buttons), so passing over "Ja" answers nothing.
        return question_mouse(x, y, down);
    }
    if (m_end_active) {
        std::int32_t ox = 0;
        std::int32_t oy = 0;
        menu_origin(ox, oy);
        if (const auto choice = m_end.mouse(x - ox, y - oy, down)) {
            m_end_choice = choice;
        }
        return std::nullopt;
    }
    if (multiplayer_open()) {
        std::int32_t ox = 0;
        std::int32_t oy = 0;
        menu_origin(ox, oy);
        for (MultiplayerEvent& e : m_multiplayer->mouse(m_multiplayer_model, x - ox, y - oy, down, button)) {
            m_multiplayer_events.push_back(std::move(e));
        }
        return std::nullopt;
    }
    if (m_menu_active && m_menu) {
        std::int32_t ox = 0;
        std::int32_t oy = 0;
        menu_origin(ox, oy);
        for (MenuEvent& e : m_menu->mouse(x - ox, y - oy, down, button)) {
            m_menu_events.push_back(std::move(e));
        }
        return std::nullopt;
    }
    const ui::Rect& panel = m_regions.side_panel;
    const ui::Rect& bar = m_regions.status_bar;
    // The mode's panel first: an open pop-up takes every press on the side panel.
    if (m_editing) {
        if (m_editor) {
            place_editor_panel();
            if (auto r = m_editor->mouse(x - panel.x, y - panel.y, down, m_editor_model)) {
                m_editor_requests.push_back(std::move(*r));
            }
        }
    } else if (m_mode == kModeBuild && m_build) {
        const BuildPanel::Available available =
            m_available ? m_available : BuildPanel::Available([](std::uint32_t) { return true; });
        const bool popup = m_build->popup_open();
        bool consumed = false;
        if (const auto a = m_build->mouse(x - panel.x, y - panel.y, down, available, consumed)) {
            switch (a->kind) {
                case BuildPanel::Action::Kind::Pick:
                    m_founding_ship.reset();  // another building: the ship no longer pays
                    action = Action{Action::Kind::Build};
                    action->building = a->building;
                    break;
                case BuildPanel::Action::Kind::Demolish:
                    action = Action{Action::Kind::Demolish};
                    break;
                case BuildPanel::Action::Kind::Copy:
                    action = Action{Action::Kind::Copy};
                    break;
                case BuildPanel::Action::Kind::Move:
                    action = Action{Action::Kind::Move};
                    break;
                case BuildPanel::Action::Kind::Blueprint:
                    action = Action{Action::Kind::Blueprint};
                    break;
                case BuildPanel::Action::Kind::Templates:
                    action = Action{Action::Kind::Templates};
                    break;
                case BuildPanel::Action::Kind::Off:
                    action = Action{Action::Kind::ToolOff};
                    break;
                case BuildPanel::Action::Kind::Rotate:
                    action = Action{Action::Kind::TurnBuilding};
                    action->step = a->step;
                    break;
                case BuildPanel::Action::Kind::OrientToRoad:
                    action = Action{Action::Kind::OrientToRoad};
                    action->step = a->step;
                    break;
            }
        }
        if (popup && consumed) {
            return action;
        }
    } else if (m_mode == kModeInfo || m_mode == kModeMilitary) {
        std::optional<PanelContext> context;
        if (SelectionPanel* p = selection_panel(context)) {
            for (const ui::PanelEvent& e : p->panel().mouse(x - panel.x, y - panel.y, down)) {
                // A click elsewhere in the window keeps the name being typed.
                m_names.begin_click();
                p->clicked(e, m_selection, *context, m_commands);
                if (e.kind == ui::PanelEvent::Kind::Clicked) {
                    if (auto c = m_names.end_click()) {
                        m_commands.push_back(std::move(*c));
                    }
                }
            }
            if (const auto r = p->take_request()) {
                switch (r->kind) {
                    case SelectionPanel::Request::Kind::FoundKontor:
                        action = Action{Action::Kind::FoundKontor};
                        action->ship = r->ship;
                        break;
                    case SelectionPanel::Request::Kind::SelectShip:
                        action = Action{Action::Kind::SelectShip, r->x, r->y};
                        action->ship = r->ship;
                        break;
                    case SelectionPanel::Request::Kind::Centre:
                        action = Action{Action::Kind::Message, r->x, r->y};
                        break;
                    case SelectionPanel::Request::Kind::Goods:
                        if (m_goods) {
                            m_goods->open_ware(r->ware, r->island);
                            m_goods_open = true;
                            m_display_open = false;
                        }
                        break;
                }
            }
        } else if (m_info && m_mode == kModeInfo) {
            if (const auto list = m_info->mouse(x - panel.x, y - panel.y, down)) {
                action = toggle_window(*list);
            } else if (m_info->take_rating()) {
                action = Action{Action::Kind::Rating};
            } else if (m_info->take_goods()) {
                toggle_goods();
            }
        }
    } else if (m_mode == kModeOptions && m_options) {
        for (OptionsEvent& e : m_options->mouse(x - panel.x, y - panel.y, down, m_options_model)) {
            m_option_events.push_back(std::move(e));
        }
    }
    // The editor's column has no compass, zoom, modes or messages of the game.
    const auto ctrl = m_editing ? decltype(m_ctrl.mouse(0, 0, false)){} : m_ctrl.mouse(x - panel.x, y - panel.y, down);
    const auto leist = m_editing ? decltype(m_leist.mouse(0, 0, false)){} : m_leist.mouse(x - bar.x, y - bar.y, down);
    for (const ui::PanelEvent& e : ctrl) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            continue;
        }
        switch (e.id) {
            case kRotateLeft:
                action = Action{Action::Kind::RotateLeft};
                break;
            case kRotateRight:
                action = Action{Action::Kind::RotateRight};
                break;
            case kZoomIn:
                action = Action{Action::Kind::ZoomIn};
                break;
            case kZoomOut:
                action = Action{Action::Kind::ZoomOut};
                break;
            default:
                if (e.id >= kModeBuild && e.id <= kModeOptions && !m_editing) {
                    action = set_mode(e.id);
                }
                break;
        }
    }
    for (const ui::PanelEvent& e : leist) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            continue;
        }
        const auto k = static_cast<std::size_t>(e.id - kFirstEvent);
        if (k < m_events.size() && m_events[k].tile) {
            action = Action{Action::Kind::Message, m_events[k].tile->first, m_events[k].tile->second};
        }
        // An offer's icon opens the diplomacy window at the partner who made it.
        const auto o = static_cast<std::size_t>(e.id - kFirstOffer);
        if (e.id >= kFirstOffer && o < m_offers.size()) {
            if (auto open = open_diplomacy(m_offers[o].from)) {
                action = open;
            }
        }
    }
    // The minimap: pressing or dragging on it moves the view there.
    if (down) {
        if (const auto t = minimap_tile(x, y)) {
            action = Action{Action::Kind::Minimap, t->first, t->second};
        }
    }
    return action;
}

void Hud::message(MessageId id, sim::BannerId banner, std::optional<std::pair<double, double>> tile,
                  std::uint64_t now_ms, std::string_view text) {
    // A text of the message's own (a mod's) in the language of the game, else the original's.
    std::string shown = text.empty() ? m_messages->text(id, ui::current_language()) : std::string(text);
    // The same message at the same place only refreshes its time; a new one
    // pushes the oldest out.
    for (Event& e : m_events) {
        if (e.id == id && e.tile == tile && e.text == shown) {
            e.time_ms = now_ms;
            return;
        }
    }
    m_events.push_front(Event{id, banner, tile, now_ms, std::move(shown)});
    if (m_events.size() > kEvents) {
        m_events.pop_back();
    }
}

namespace {

/// The colour of a world tile on the minimap. An island `local` has not discovered shows its ground
/// without owners, like the map. The sea is left out (alpha 0): the parchment shows
/// there, as in the original.
std::array<std::uint8_t, 4> minimap_colour(const sim::World& world, const assets::BuildingTable& buildings,
                                           const render::BannerTable& banners, const sim::World::Cell& c,
                                           sim::PlayerId local) {
    std::array<std::uint8_t, 4> rgb{0, 0, 0, 0};  // the open sea
    if (c.island == sim::World::kSea) {
        return rgb;
    }
    const sim::WorldIsland& island = world.islands()[c.island];
    const bool hidden = local != sim::kNoPlayer && !island.discovered_by(local);
    const sim::MapCell& ground = island.map.ground(c.local_x, c.local_y);
    const sim::MapCell& top = hidden && !ground.empty() ? ground : island.map.top(c.local_x, c.local_y);
    const assets::BuildingType* b = top.empty() ? nullptr : buildings.by_sprite(top.sprite);
    const std::int32_t kind = b != nullptr ? b->kind : sim::building_kind::kSea;
    namespace bk = sim::building_kind;
    // Ground kinds 19 to 22 are water to the original's minimap, the river mouth too:
    // alpha 1 marks an island's water, which counts against its land when the picture is shrunk.
    if (kind == bk::kSea || kind == bk::kSurf || kind == bk::kSurfCorner || kind == bk::kRiverMouth) {
        rgb[3] = 1;
        return rgb;
    }
    // Land of nobody in the original's brown (0x966430, which its palette makes 0x896340).
    rgb = {0x89, 0x63, 0x40, 255};
    // The territory is the ground's owner: a native village in a city's land shows the city.
    const sim::PlayerId owner = ground.owner != sim::kNoPlayer ? ground.owner : top.owner;
    const sim::Player* p = hidden || owner == sim::kNoPlayer ? nullptr : world.player(owner);
    if (p != nullptr && p->role != sim::PlayerRole::Neutral) {
        // The whole territory of a city, forest and meadow too, in the four colours of the original
        // (red, blue, yellow, grey); a fifth player and those after it in their banner's colour.
        // The natives and the pirates stay brown, as the original has colours only for the four.
        constexpr std::array<std::array<std::uint8_t, 3>, 4> kOriginal{
            {{0xAC, 0x27, 0x05}, {0x11, 0x88, 0xDC}, {0xF8, 0xC2, 0x30}, {0xC0, 0xC0, 0xC0}}};
        const render::Banner* banner = banners.get(p->banner);
        if (banner != nullptr && banner->unit_variant && *banner->unit_variant < kOriginal.size()) {
            const auto& o = kOriginal[*banner->unit_variant];
            rgb = {o[0], o[1], o[2], 255};
        } else if (banner != nullptr) {
            rgb = {banner->colour.r, banner->colour.g, banner->colour.b, 255};
        }
    }
    return rgb;
}

}  // namespace

void Hud::build_minimap(const sim::World& world, const assets::BuildingTable& buildings,
                        const render::BannerTable& banners) {
    m_world_w = world.width();
    m_world_h = world.height();
    m_minimap_islands = world.islands().size();
    m_minimap_next = 0;
    m_minimap.assign(std::size_t{m_world_w} * m_world_h * 4, 0);
    for (std::uint32_t y = 0; y < m_world_h; ++y) {
        for (std::uint32_t x = 0; x < m_world_w; ++x) {
            const std::array<std::uint8_t, 4> rgb =
                minimap_colour(world, buildings, banners, world.cell(x, y), m_local);
            const std::size_t i = (std::size_t{y} * m_world_w + x) * 4;
            m_minimap[i] = rgb[0];
            m_minimap[i + 1] = rgb[1];
            m_minimap[i + 2] = rgb[2];
            m_minimap[i + 3] = rgb[3];
        }
    }
}

bool Hud::refresh_minimap_island(const sim::World& world, const assets::BuildingTable& buildings,
                                 const render::BannerTable& banners, std::size_t island) {
    // Only islands change on the map: the sea between them stays as the full build drew it.
    const sim::WorldIsland& is = world.islands()[island];
    const std::uint32_t x1 = std::min(m_world_w, is.x + is.map.width());
    const std::uint32_t y1 = std::min(m_world_h, is.y + is.map.height());
    bool changed = false;
    for (std::uint32_t y = is.y; y < y1; ++y) {
        for (std::uint32_t x = is.x; x < x1; ++x) {
            const sim::World::Cell& c = world.cell(x, y);
            if (c.island != island) {
                continue;
            }
            const std::array<std::uint8_t, 4> rgb = minimap_colour(world, buildings, banners, c, m_local);
            const std::size_t i = (std::size_t{y} * m_world_w + x) * 4;
            if (m_minimap[i] != rgb[0] || m_minimap[i + 1] != rgb[1] || m_minimap[i + 2] != rgb[2] ||
                m_minimap[i + 3] != rgb[3]) {
                m_minimap[i] = rgb[0];
                m_minimap[i + 1] = rgb[1];
                m_minimap[i + 2] = rgb[2];
                m_minimap[i + 3] = rgb[3];
                changed = true;
            }
        }
    }
    return changed;
}

void Hud::shrink_minimap(std::int32_t w, std::int32_t h) {
    m_minimap_w = w;
    m_minimap_h = h;
    const auto pixels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    m_minimap_small.assign(pixels * 4, 0);
    // As the original: a tile lands on pixel (x * step) >> shift with the step in fixed point, cut
    // off; a pixel shows land when more of its island tiles are land than water (the open sea does
    // not count), in the colour of its last land tile. A world smaller than the parchment spreads
    // each tile over the pixels up to the next one's. The original's step has 8 bits of fraction;
    // on a large world that cut-off step shrinks the land towards the top left, away from the
    // ships, the view frame and the clicks, which use the exact scale (on 4000 tiles by up to 8 %), so
    // Open1602 keeps 16 bits.
    constexpr unsigned kShift = 16U;
    const std::uint64_t step_x = (std::uint64_t{static_cast<std::uint32_t>(w)} << kShift) / m_world_w;
    const std::uint64_t step_y = (std::uint64_t{static_cast<std::uint32_t>(h)} << kShift) / m_world_h;
    std::vector<std::int32_t> balance(pixels, 0);  // land tiles minus water tiles
    std::vector<std::size_t> colour(pixels, 0);    // the land tile that gives the colour
    for (std::uint32_t y = 0; y < m_world_h; ++y) {
        const std::uint64_t y0 = (y * step_y) >> kShift;
        const std::uint64_t y1 = std::max(y0 + 1, ((y + 1) * step_y) >> kShift);
        for (std::uint32_t x = 0; x < m_world_w; ++x) {
            const std::size_t i = (std::size_t{y} * m_world_w + x) * 4;
            const std::uint8_t alpha = m_minimap[i + 3];
            if (alpha == 0) {
                continue;  // the open sea
            }
            const std::uint64_t x0 = (x * step_x) >> kShift;
            const std::uint64_t x1 = std::max(x0 + 1, ((x + 1) * step_x) >> kShift);
            for (std::uint64_t py = y0; py < y1 && py < static_cast<std::uint64_t>(h); ++py) {
                for (std::uint64_t px = x0; px < x1 && px < static_cast<std::uint64_t>(w); ++px) {
                    const std::size_t o = static_cast<std::size_t>(py * static_cast<std::uint64_t>(w) + px);
                    if (alpha == 255) {
                        ++balance[o];
                        colour[o] = i;
                    } else {
                        --balance[o];
                    }
                }
            }
        }
    }
    for (std::size_t o = 0; o < pixels; ++o) {
        if (balance[o] > 0) {
            std::copy_n(m_minimap.begin() + static_cast<std::ptrdiff_t>(colour[o]), 4,
                        m_minimap_small.begin() + static_cast<std::ptrdiff_t>(o * 4));
        }
    }
}

void Hud::draw(const sim::World& world, const assets::BuildingTable& buildings, const render::BannerTable& banners,
               const std::array<std::pair<double, double>, 4>& view, std::uint64_t now_ms,
               const sim::PlayerFigures& figures, std::uint8_t colour) {
    const double s = m_regions.scale;
    if (!m_editing) {
        draw_resources();
    }
    // Black under the art where the window is larger than it.
    m_renderer->fill(m_regions.side_panel, 0);
    m_renderer->fill(m_regions.status_bar, 0);
    // Help text: the gadget under the mouse, else the status of the game.
    std::string status = m_status;
    if (const auto id = m_ctrl.hovered()) {
        if (const auto line = help_line(*id); line && *line < m_leiste.size()) {
            status = m_leiste[*line];
        }
    }
    // Message icons: expired ones go, the others show their type in the player's colour.
    std::erase_if(m_events, [&](const Event& e) { return now_ms > e.time_ms + kEventMs; });
    for (std::size_t i = 0; i < kEvents; ++i) {
        const auto id = kFirstEvent + static_cast<std::int32_t>(i);
        if (i >= m_events.size()) {
            m_leist.set_hidden(id, true);
            continue;
        }
        const Event& e = m_events[i];
        const MessageDefinition* def = m_messages->get(e.id);
        const auto type = static_cast<std::uint8_t>(def != nullptr ? def->icon : MessageIcon::Notice);
        const auto picture =
            def != nullptr && !def->picture.empty() ? ui::package_icon_sprite(def->picture) : std::nullopt;
        if (picture) {
            // A package's icon: as drawn, or a template in the colour of the player it is about.
            set_package_icon(m_leist, id, *picture, banners.get(e.banner));
        } else if (type < 4) {
            set_banner_graphic(m_leist, id, message_icon_art(type), banners.get(e.banner), 6);
        } else {
            m_leist.set_graphic(id, kGfxLeiste + 1 + 0x28 + type * 2, 1);
        }
        m_leist.set_hidden(id, false);
        if (m_leist.hovered() == id && !e.text.empty()) {
            status = e.text;
        } else if (m_leist.hovered() == id && def != nullptr && def->meldung && *def->meldung < m_meldung.size()) {
            status = m_meldung[*def->meldung];
        }
    }
    // Offers not answered yet stay until they are (Open1602): the treaty icon in the partner's colour.
    m_offers = m_simulation != nullptr ? diplomacy::open_offers(world, m_local) : std::vector<diplomacy::Offer>{};
    for (std::size_t k = 0; k < diplomacy::kMaxPartners; ++k) {
        const auto id = kFirstOffer + static_cast<std::int32_t>(k);
        const sim::Player* from = k < m_offers.size() ? world.player(m_offers[k].from) : nullptr;
        m_leist.set_visible(id, from != nullptr);
        if (from == nullptr) {
            continue;
        }
        set_banner_graphic(m_leist, id, message_icon_art(kTreatyIcon), banners.get(from->banner), 6);
        if (m_leist.hovered() == id) {
            status = diplomacy::offer_text(m_offers[k], player_name(*from));
        }
    }
    if (m_editing && m_chrome) {
        // The editor's column instead of the game's panel: its own ground under the minimap.
        m_renderer->draw(m_chrome->draw_back(m_renderer->fonts(s), minimap_rect()), 0, 0, s);
    } else {
        m_ctrl.set_graphic(kCompass, kGfxCompass + m_rotation, 0);
        m_renderer->draw(m_ctrl.draw(m_renderer->fonts(s)), m_regions.side_panel.x, m_regions.side_panel.y, s);
        // A window taller than the panel's art (narrower than 4:3, or a smaller interface): the
        // parchment goes on to the bottom, where the art's lower edge (5 rows) closes it.
        m_renderer->draw_lengthened(0, kGfxTafel, m_regions.side_panel, s, 5);
    }
    // The mode's panel below the head, and its help texts.
    const BuildPanel::Available available =
        m_available ? m_available : BuildPanel::Available([](std::uint32_t) { return true; });
    if (m_editing) {
        if (m_editor) {
            place_editor_panel();
            m_renderer->draw(m_editor->draw(m_editor_model, m_renderer->fonts(s)), m_regions.side_panel.x,
                             m_regions.side_panel.y, s);
            if (!m_editor_model.notice.empty()) {
                status = m_editor_model.notice;
            }
        }
    } else if (m_mode == kModeBuild && m_build) {
        m_build->refresh(
            buildings, available, static_cast<std::uint8_t>((m_rotation + m_building_rotation) & 3U),
            [this](std::int32_t block, std::int32_t gfx) { return m_renderer->sprite_size(block, gfx); },
            m_simulation != nullptr ? m_simulation->world().goods() : assets::Goods{});
        m_renderer->draw(m_build->panel().draw(m_renderer->fonts(s)), m_regions.side_panel.x, m_regions.side_panel.y,
                         s);
        if (const ui::Panel* popup = m_build->popup()) {
            m_renderer->draw(popup->draw(m_renderer->fonts(s)), m_regions.side_panel.x, m_regions.side_panel.y, s);
        }
        if (const auto text = m_build->help(m_leiste)) {
            status = *text;
        }
    } else if (m_mode == kModeInfo || m_mode == kModeMilitary) {
        std::optional<PanelContext> context;
        if (SelectionPanel* p = selection_panel(context)) {
            p->refresh(m_selection, *context);
            m_renderer->draw(p->panel().draw(m_renderer->fonts(s)), m_regions.side_panel.x, m_regions.side_panel.y, s);
            if (const auto id = p->panel().hovered()) {
                if (const auto text = p->help(*id, m_selection, *context)) {
                    status = *text;
                }
            }
        } else if (m_info && m_mode == kModeInfo) {
            m_info->refresh(figures, banners.get(colour));
            m_renderer->draw(m_info->panel().draw(m_renderer->fonts(s)), m_regions.side_panel.x, m_regions.side_panel.y,
                             s);
            if (const auto text = m_info->help(m_leiste)) {
                status = *text;
            }
        }
    } else if (m_mode == kModeOptions && m_options) {
        m_options->refresh(m_options_model);
        m_renderer->draw(m_options->panel().draw(m_renderer->fonts(s)), m_regions.side_panel.x, m_regions.side_panel.y,
                         s);
        if (const auto text = m_options->help(m_leiste, m_options_model)) {
            status = *text;
        }
    }
    if (!m_editing) {
        // The name being typed ends with its window (or its ship or city).
        if (auto c = m_names.end_frame(world)) {
            m_commands.push_back(std::move(*c));
        }
        step_tutorial(available, now_ms);
        draw_mission(world);
    }
    // The minimap on the parchment.
    // Built in full once per game; then one island per frame is brought up to date and the texture
    // is uploaded again only when a pixel changed, at most every kMinimapMs. A pending upload no
    // longer makes it build the whole picture again (FB-5: without a minimap gadget nothing
    // uploads, and every frame rebuilt it).
    if (m_minimap_rebuild || world.width() != m_world_w || world.height() != m_world_h ||
        world.islands().size() != m_minimap_islands) {
        build_minimap(world, buildings, banners);
        m_minimap_ms = now_ms;
        m_minimap_rebuild = false;
        m_minimap_upload = true;
        m_minimap_changed = false;
    } else if (m_minimap_islands > 0) {
        m_minimap_next %= m_minimap_islands;
        m_minimap_changed |= refresh_minimap_island(world, buildings, banners, m_minimap_next++);
        if (m_minimap_changed && now_ms >= m_minimap_ms + kMinimapMs) {
            m_minimap_ms = now_ms;
            m_minimap_upload = true;
            m_minimap_changed = false;
        }
    }
    const ui::Rect map = minimap_rect();
    if (map.w > 0) {
        // The parchment under the map (GFXMAP, drawn by the code under the GAD_NIX area).
        ui::DrawItem parchment;
        parchment.kind = ui::DrawItem::Kind::Sprite;
        parchment.gfx = kGfxParchment;
        m_renderer->draw({parchment}, map.x, map.y, s);
    }
    if (const ui::Rect at = minimap_image(); at.w > 0) {
        // Each pixel of the parchment shows land when more of its tiles are land than sea, the sea
        // not at all.
        if (m_minimap_upload || at.w != m_minimap_w || at.h != m_minimap_h) {
            shrink_minimap(at.w, at.h);
            m_minimap_upload = true;
        }
        m_renderer->image(1, m_minimap_small, at.w, at.h, at, m_minimap_upload);
        m_minimap_upload = false;
        const double fx = static_cast<double>(at.w) / m_world_w;
        const double fy = static_cast<double>(at.h) / m_world_h;
        const float unit = std::max(1.0F, std::round(static_cast<float>(s)));
        const SDL_Rect clip{at.x, at.y, at.w, at.h};
        SDL_SetRenderClipRect(m_sdl, &clip);
        // The visible part of the map, each edge dashed two pixels on and two off as in the
        // original. The original draws its single pixels white over a blue shadow one pixel lower,
        // which hardly shows on the blue sea and the light islands; Open1602 draws the dashes as
        // squares of the interface's scale, white with a black border all round.
        std::array<SDL_FPoint, 4> corners{};
        for (std::size_t i = 0; i < 4; ++i) {
            corners[i] = SDL_FPoint{static_cast<float>(at.x + view[i].first * fx),
                                    static_cast<float>(at.y + view[i].second * fy)};
        }
        std::vector<SDL_FPoint> dashes;
        for (std::size_t i = 0; i < 4; ++i) {
            const SDL_FPoint a = corners[i];
            const SDL_FPoint b = corners[(i + 1) % 4];
            const float length = std::hypot(b.x - a.x, b.y - a.y);
            const auto steps = static_cast<std::size_t>(std::ceil(length));
            for (std::size_t k = 0; k < steps; ++k) {
                if (std::fmod(static_cast<float>(k), 4.0F * unit) < 2.0F * unit) {
                    const float t = static_cast<float>(k) / static_cast<float>(steps);
                    dashes.push_back(SDL_FPoint{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
                }
            }
        }
        std::vector<SDL_FRect> border;
        std::vector<SDL_FRect> marks;
        border.reserve(dashes.size());
        marks.reserve(dashes.size());
        for (const SDL_FPoint& p : dashes) {
            const float x = std::floor(p.x - unit / 2.0F);
            const float y = std::floor(p.y - unit / 2.0F);
            border.push_back(SDL_FRect{x - 1.0F, y - 1.0F, unit + 2.0F, unit + 2.0F});
            marks.push_back(SDL_FRect{x, y, unit, unit});
        }
        SDL_SetRenderDrawColor(m_sdl, 0, 0, 0, 255);
        SDL_RenderFillRects(m_sdl, border.data(), static_cast<int>(border.size()));
        SDL_SetRenderDrawColor(m_sdl, 255, 255, 255, 255);
        SDL_RenderFillRects(m_sdl, marks.data(), static_cast<int>(marks.size()));
        // The own ships, and in military mode the own land units, as blue squares of 2 x 2 pixels;
        // nothing of the other players.
        SDL_SetRenderDrawColor(m_sdl, 0, 0, 255, 255);
        const auto unit_square = [&](double tx, double ty) {
            const SDL_FRect r{static_cast<float>(at.x + std::floor(tx * fx)),
                              static_cast<float>(at.y + std::floor(ty * fy)), 2.0F * unit, 2.0F * unit};
            SDL_RenderFillRect(m_sdl, &r);
        };
        if (m_local != sim::kNoPlayer) {
            for (const sim::Ship& ship : world.ships()) {
                if (ship.owner == m_local && !ship.sinking && !ship.cart()) {
                    unit_square(ship.pos_x / static_cast<double>(sim::kShipUnit),
                                ship.pos_y / static_cast<double>(sim::kShipUnit));
                }
            }
            if (m_mode == kModeMilitary) {
                for (const sim::Soldier& soldier : world.soldiers()) {
                    if (soldier.owner == m_local) {
                        unit_square(soldier.x(), soldier.y());
                    }
                }
            }
        }
        SDL_SetRenderClipRect(m_sdl, nullptr);
    }
    if (m_editing && m_chrome) {
        // The editor's bars over the map's edges, its status line below it.
        EditorChromeModel chrome = m_chrome_model;
        chrome.status = m_editor_model.notice.empty() ? m_status : m_editor_model.notice;
        chrome.status_error = !m_editor_model.notice.empty() && m_editor_model.notice_is_error;
        const std::optional<EditorTool> active = m_editor ? m_editor->active_tool(m_editor_model) : std::nullopt;
        m_renderer->draw(m_chrome->draw(m_editor_model, chrome, active, m_renderer->fonts(s)), 0, 0, s);
        draw_question();
        draw_help();
        return;
    }
    // A status bar wider than its art (wide windows): the art widened, the line at its right end.
    m_renderer->draw_widened(0, kGfxLeiste, m_regions.status_bar, s);
    const std::int32_t extra = std::max(0, static_cast<std::int32_t>(m_regions.status_bar.w / s) - kDesignViewW);
    if (const assets::Gadget* line = m_leist.layout().find(kStatusLine)) {
        m_leist.set_offset(kStatusLine, kLeistTextX + extra, line->offset[1]);
    }
    m_leist.set_text(kStatusLine, status);
    m_renderer->draw(m_leist.draw(m_renderer->fonts(s)), m_regions.status_bar.x, m_regions.status_bar.y, s);
    draw_bubbles();
    if (m_chat && m_chat_model.available) {
        if (m_chat->is_open()) {
            const ui::Rect r = chat_rect();
            m_renderer->draw(m_chat->draw(m_chat_model, m_multiplayer_model.chat, m_renderer->fonts(m_chat->scale())),
                             r.x, r.y, m_chat->scale());
        } else {
            // New lines for a while over the lower left of the map.
            const ui::Rect& v = m_regions.game_view;
            const auto gap = static_cast<std::int32_t>(8 * m_chat->scale());
            m_renderer->draw(m_chat->draw_recent(m_multiplayer_model.chat, now_ms, m_renderer->fonts(m_chat->scale())),
                             v.x + gap, v.y + v.h - gap, m_chat->scale());
        }
    }
    if (m_blueprint_at && m_blueprint && m_blueprint_model.valid) {
        const ui::Rect r = blueprint_rect();
        m_renderer->draw(m_blueprint->draw(m_blueprint_model, m_renderer->fonts(m_blueprint->scale())), r.x, r.y,
                         m_blueprint->scale());
    }
    if (templates_open()) {
        const ui::Rect r = templates_rect();
        m_renderer->draw(m_templates->draw(m_templates_model, m_renderer->fonts(m_templates->scale())), r.x, r.y,
                         m_templates->scale());
    }
    if (m_display_open && m_display) {
        const ui::Rect r = display_rect();
        m_renderer->draw(m_display->draw(m_display_model, m_renderer->fonts(m_display->scale())), r.x, r.y,
                         m_display->scale());
    }
    if (m_goods_open && m_goods && m_figure_table != nullptr) {
        const ui::Rect r = goods_rect();
        // A shadow at the lower right, the parchment, then the window's ink.
        ui::DrawItem shadow;
        shadow.kind = ui::DrawItem::Kind::Fill;
        shadow.x = r.x + 8;
        shadow.y = r.y + 8;
        shadow.w = r.w;
        shadow.h = r.h;
        shadow.colour = m_goods_shadow;
        shadow.half = true;
        m_renderer->draw({shadow}, 0, 0, 1.0);
        m_renderer->image(2, m_goods_parchment, GoodsWindow::kWidth, GoodsWindow::kHeight, r, false);
        m_renderer->draw(m_goods->draw(world, buildings, *m_figure_table, m_local, m_renderer->fonts(m_goods->scale())),
                         r.x, r.y, m_goods->scale());
    }
    draw_question();
    draw_help();
}

}  // namespace o1602::app
