// SPDX-License-Identifier: GPL-3.0-or-later
#include "side_panels.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "banner_graphic.hpp"

#include "o1602/core/text.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/ui/localization.hpp"
#include "o1602/ui/package_icons.hpp"

namespace o1602::app {

namespace {

// BAU.GAD.
constexpr std::int32_t kRotateLeft = 31021;
constexpr std::int32_t kRotateRight = 31022;
constexpr std::int32_t kAutoAlign = 31023;
constexpr std::int32_t kPreviewShadow = 31031;
constexpr std::int32_t kPreview = 31032;
constexpr std::int32_t kTitle = 31033;
constexpr std::int32_t kFirstCost = 31041;  // tools, wood, bricks, cannons, gold, running cost
constexpr std::int32_t kFirstCostIcon = 31051;
constexpr std::int32_t kCosts = 6;
constexpr std::int32_t kChainFirst = 31061;  // 2nd input (icon, frame, arrow), input (..), product (icon, frame)
constexpr std::int32_t kChainLast = 31068;
constexpr std::int32_t kPreviewBlock = 3;  // BAUHAUS
constexpr std::int32_t kPreviewW = 160;    // the preview's box (gadget size)
constexpr std::int32_t kPreviewH = 110;

/// LEISTE lines of the build menu's gadgets.
std::optional<std::size_t> bau_help(std::int32_t id, bool auto_align) {
    if (id >= 31001 && id <= 31008) {
        return static_cast<std::size_t>(23 + id - 31001);
    }
    switch (id) {
        case kRotateLeft:
            return 13;
        case kRotateRight:
            return 14;
        case kAutoAlign:
            return auto_align ? 16 : 15;
        case kPreview:
            return 12;
        case kFirstCost + 0:
            return 17;
        case kFirstCost + 1:
            return 18;
        case kFirstCost + 2:
            return 19;
        case kFirstCost + 3:
            return 21;
        case kFirstCost + 4:
            return 20;
        case kFirstCost + 5:
            return 22;
        default:
            return std::nullopt;
    }
}

}  // namespace

// ---- Build menu ----------------------------------------------------------------------------------

BuildPanel::BuildPanel(assets::GadgetLayout bau, const ui::BuildMenuDefinition& menu, const assets::TextTable* texts)
    : m_bau(bau, 1.0), m_layout(std::move(bau)), m_menu(&menu), m_texts(texts), m_category(menu.default_category) {
    m_gfx_bauhaus = m_layout.constant("GFXBAUHAUS").value_or(m_gfx_bauhaus);
    m_gfx_ware = m_layout.constant("GFXWARE").value_or(m_gfx_ware);
    m_bau.set_selected(m_category, true);
}

void BuildPanel::set_scale(double scale) {
    m_bau.set_scale(scale);
    if (m_popup) {
        m_popup->set_scale(scale);
    }
}

std::string BuildPanel::name_of(const ui::BuildCategory& category, std::size_t index) const {
    static const std::vector<std::string> kNone;
    return category.name(index, m_texts != nullptr ? m_texts->section(category.names) : kNone);
}

bool BuildPanel::demolish() const noexcept {
    const ui::BuildCategory* c = m_menu->category(m_category);
    return c != nullptr && c->demolish && m_tool == Tool::Demolish;
}

bool BuildPanel::copy() const noexcept {
    const ui::BuildCategory* c = m_menu->category(m_category);
    return c != nullptr && c->demolish && m_tool == Tool::Copy;
}

bool BuildPanel::moving() const noexcept {
    const ui::BuildCategory* c = m_menu->category(m_category);
    return c != nullptr && c->demolish && m_tool == Tool::Move;
}

void BuildPanel::show_tool(Tool tool) {
    for (const ui::BuildCategory& c : m_menu->categories) {
        if (c.demolish) {
            m_popup.reset();
            m_bau.set_selected(m_category, false);
            m_category = c.button;
            m_bau.set_selected(m_category, true);
            m_tool = tool;
            return;
        }
    }
}

ui::BuildCategory BuildPanel::tools_category(std::int32_t button) const {
    ui::BuildCategory tools;
    tools.button = button;
    tools.demolish = true;
    tools.names = "ABRISS";  // "Abreißen" in the game's own language
    const auto tool = [](Tool t, const char* icon, const char* name) {
        ui::BuildButton b;
        b.id = static_cast<std::uint32_t>(t);
        b.picture = icon;
        b.name = ui::tr(name);  // already Windows-1252
        return b;
    };
    ui::BuildButton demolish = tool(Tool::Demolish, "open1602:demolish", "build.tool.demolish");
    if (m_texts != nullptr && !m_texts->section("ABRISS").empty()) {
        demolish.text = 0;
    }
    tools.rows.push_back({demolish, tool(Tool::Copy, "open1602:copy", "build.tool.copy"),
                          tool(Tool::Blueprint, "open1602:blueprint", "build.tool.blueprint"),
                          tool(Tool::Move, "open1602:move", "build.tool.move")});
    // A row holds four icons: the templates above the tools.
    tools.rows.push_back({tool(Tool::Templates, "open1602:templates", "build.tool.templates")});
    return tools;
}

std::optional<std::uint32_t> BuildPanel::current(const Available& available) {
    const ui::BuildCategory* c = m_menu->category(m_category);
    if (c == nullptr || c->demolish) {
        return std::nullopt;
    }
    std::size_t& picked = m_picked[m_category];
    if (const auto id = c->at(picked); id && available(*id)) {
        return id;
    }
    // The remembered building is not (or no longer) available: the first one that is.
    for (std::size_t i = 0; i < c->size(); ++i) {
        if (const auto id = c->at(i); id && available(*id)) {
            picked = i;
            return id;
        }
    }
    return std::nullopt;
}

bool BuildPanel::show(std::uint32_t id) {
    const ui::BuildCategory* c = m_menu->category_of(id);
    if (c == nullptr) {
        return false;
    }
    m_popup.reset();  // an open pop-up (the tools') belongs to the category left
    m_bau.set_selected(m_category, false);
    m_category = c->button;
    m_building_category = m_category;
    m_bau.set_selected(m_category, true);
    m_picked[m_category] = c->index_of(id).value_or(0);
    return true;
}

void BuildPanel::open_popup(const ui::BuildCategory& category, const Available& available, std::int32_t x,
                            std::int32_t y) {
    std::vector<bool> ok(category.size());
    for (std::size_t i = 0; i < ok.size(); ++i) {
        const auto id = category.at(i);
        ok[i] = id && available(*id);
    }
    const assets::Gadget* button = m_layout.find(category.button);
    if (button == nullptr) {
        return;
    }
    m_popup.emplace(ui::build_popup(category, ok, *button, m_gfx_bauhaus, m_menu->locked_icon_offset), m_bau.scale());
    ui::show_popup_patches(*m_popup);
    m_popup_category = category.button;
    // The button is still held: the pop-up takes over this press, so that dragging onto an icon
    // and letting go there picks it.
    (void)m_popup->mouse(x, y, true);
}

std::optional<BuildPanel::Action> BuildPanel::mouse(std::int32_t x, std::int32_t y, bool down,
                                                    const Available& available, bool& consumed) {
    consumed = false;
    std::optional<Action> action;
    if (m_popup) {
        consumed = true;
        bool closed = false;
        for (const ui::PanelEvent& e : m_popup->mouse(x, y, down)) {
            if (e.kind == ui::PanelEvent::Kind::Pressed && e.id == ui::kPopupBackdrop) {
                // A press beside the icons closes the pop-up and goes on to the panel.
                closed = true;
            } else if (e.kind == ui::PanelEvent::Kind::Clicked && e.id >= ui::kPopupSlot &&
                       e.id < ui::kPopupSlot + 1000) {
                const ui::BuildCategory* c = m_menu->category(m_popup_category);
                const auto index = static_cast<std::size_t>(e.id - ui::kPopupSlot);
                if (c != nullptr && c->demolish) {
                    // The tools' pop-up: the tool picked.
                    if (const auto tool = m_tools.at(index); tool && static_cast<Tool>(*tool) == Tool::Blueprint) {
                        // A switch: the menu goes back to what is in hand, the copy tool or the
                        // building shown last.
                        action = Action{Action::Kind::Blueprint, 0, 0};
                        if (m_tool != Tool::Copy) {
                            // No building shown yet in this game: the menu's first category.
                            if (m_menu->category(m_building_category) == nullptr) {
                                for (const ui::BuildCategory& other : m_menu->categories) {
                                    if (!other.demolish) {
                                        m_building_category = other.button;
                                        break;
                                    }
                                }
                            }
                            m_bau.set_selected(m_category, false);
                            m_category = m_building_category;
                            m_bau.set_selected(m_category, true);
                        }
                    } else if (tool && static_cast<Tool>(*tool) == Tool::Templates) {
                        // The copy tool, waiting for a piece or a template from the list.
                        m_tool = Tool::Copy;
                        action = Action{Action::Kind::Templates, 0, 0};
                    } else if (tool && (static_cast<Tool>(*tool) != Tool::Move || m_moving)) {
                        m_tool = static_cast<Tool>(*tool);
                        const auto kind = m_tool == Tool::Copy   ? Action::Kind::Copy
                                          : m_tool == Tool::Move ? Action::Kind::Move
                                                                 : Action::Kind::Demolish;
                        action = Action{kind, 0, 0};
                    }
                } else if (c != nullptr) {
                    if (const auto id = c->at(index); id && available(*id)) {
                        m_picked[m_popup_category] = index;
                        m_building_category = m_popup_category;
                        action = Action{Action::Kind::Pick, *id, 0};
                    }
                }
                m_popup.reset();
                return action;
            }
        }
        if (!closed) {
            return action;
        }
        m_popup.reset();
        consumed = false;
    }
    for (const ui::PanelEvent& e : m_bau.mouse(x, y, down)) {
        consumed = true;
        if (e.kind == ui::PanelEvent::Kind::Pressed && e.id >= 31001 && e.id <= 31008) {
            // The categories answer the press (Menuflg): demolish at once, the others open their icons.
            m_bau.set_selected(m_category, false);
            m_category = e.id;
            m_bau.set_selected(m_category, true);
            if (const ui::BuildCategory* c = m_menu->category(e.id)) {
                if (c->demolish) {
                    // Open1602: the tools in a pop-up like the buildings' (demolish, copy).
                    m_tools = tools_category(c->button);
                    open_popup(
                        m_tools, [this](std::uint32_t id) { return static_cast<Tool>(id) != Tool::Move || m_moving; },
                        x, y);
                } else {
                    m_building_category = e.id;
                    open_popup(*c, available, x, y);
                }
            }
        } else if (e.kind == ui::PanelEvent::Kind::Clicked && (e.id == kRotateLeft || e.id == kRotateRight)) {
            action = Action{Action::Kind::Rotate, 0, e.id == kRotateLeft ? -1 : 1};
        } else if (e.kind == ui::PanelEvent::Kind::Clicked && e.id == kAutoAlign) {
            // Flipflg: the panel has already turned it over (flag 0x6C3064).
            action = Action{Action::Kind::OrientToRoad, 0, m_bau.selected(kAutoAlign) ? 1 : 0};
        }
    }
    if (m_bau.hovered()) {
        consumed = true;
    }
    return action;
}

std::optional<std::string> BuildPanel::help(const std::vector<std::string>& leiste) const {
    if (m_popup) {
        const auto id = m_popup->hovered();
        if (id && *id >= ui::kPopupSlot && *id < ui::kPopupSlot + 1000) {
            if (const ui::BuildCategory* c = m_menu->category(m_popup_category); c != nullptr && c->demolish) {
                return name_of(m_tools, static_cast<std::size_t>(*id - ui::kPopupSlot));
            } else if (c != nullptr) {
                return name_of(*c, static_cast<std::size_t>(*id - ui::kPopupSlot));
            }
        }
        return std::nullopt;
    }
    if (const auto id = m_bau.hovered()) {
        if (const auto line = bau_help(*id, m_bau.selected(kAutoAlign)); line && *line < leiste.size()) {
            return leiste[*line];
        }
    }
    return std::nullopt;
}

void BuildPanel::refresh(const assets::BuildingTable& buildings, const Available& available, std::uint8_t rotation,
                         const SpriteSize& size_of, const assets::Goods& goods) {
    const ui::BuildCategory* category = m_menu->category(m_category);
    const auto id = current(available);
    const assets::BuildingType* b = id ? buildings.by_id(*id) : nullptr;
    // Title: the building's name, "Abreißen", or nothing.
    if (category != nullptr && category->demolish) {
        const ui::BuildCategory tools = tools_category(category->button);
        const std::string name = name_of(tools, static_cast<std::size_t>(m_tool));
        m_bau.set_text(kTitle, m_blueprint && m_tool == Tool::Copy ? ui::tr("build.blueprint.title", {name}) : name);
        m_bau.set_visible(kTitle, true);
    } else if (b != nullptr && category != nullptr) {
        const std::string name = name_of(*category, category->index_of(*id).value_or(0));
        m_bau.set_text(kTitle, m_blueprint ? ui::tr("build.blueprint.title", {name}) : name);
        m_bau.set_visible(kTitle, true);
    } else {
        m_bau.set_visible(kTitle, false);
    }
    // Costs: goods in tons, gold, running cost while working.
    const bool show = b != nullptr;
    for (std::int32_t k = 0; k < kCosts; ++k) {
        m_bau.set_visible(kFirstCost + k, show);
        m_bau.set_visible(kFirstCostIcon + k, show);
    }
    m_bau.set_visible(kPreview, show);
    m_bau.set_visible(kPreviewShadow, show);
    for (std::int32_t g = kChainFirst; g <= kChainLast; ++g) {
        m_bau.set_visible(g, false);
    }
    if (b != nullptr) {
        const auto& c = b->build_cost;
        const std::array<std::int64_t, kCosts> values{c.tools >> 5U,   c.wood >> 5U, c.bricks >> 5U,
                                                      c.cannons >> 5U, c.money,      b->running_cost};
        for (std::int32_t k = 0; k < kCosts; ++k) {
            m_bau.set_text(kFirstCost + k, std::to_string(values[static_cast<std::size_t>(k)]));
        }
        // The preview turns with the view and the building; centred in its box.
        const auto gfx = static_cast<std::int32_t>(b->build_gfx + (b->rotate != 0 ? rotation & 3U : 0U));
        const auto size = size_of ? size_of(kPreviewBlock, gfx) : std::nullopt;
        const std::int32_t ox = size ? (kPreviewW - size->first) / 2 : 0;
        const std::int32_t oy = size ? (kPreviewH - size->second) / 2 : 0;
        for (const std::int32_t g : {kPreview, kPreviewShadow}) {
            m_bau.set_graphic(g, gfx, 0);
            m_bau.set_offset(g, ox, oy);
        }
        // The production chain: workshops show their inputs, raw material producers their product.
        // Ware icons are GFXWARE + ware number (the first two are the empty backgrounds), or a package's.
        const auto ware_icon = [&](std::int32_t gadget, std::uint8_t ware, std::int32_t shift) {
            for (std::int32_t g = gadget; g < gadget + 3 && g <= kChainLast; ++g) {
                const assets::Gadget* original = m_layout.find(g);
                if (original == nullptr) {
                    continue;
                }
                m_bau.set_offset(g, original->offset[0] - shift, original->offset[1]);
                m_bau.set_visible(g, true);
            }
            m_bau.set_graphic(gadget, ui::ware_sprite(m_gfx_ware, goods, ware), 0);
        };
        if (b->prod_kind == sim::prod::kWorkshop && b->ware != 0) {
            const std::int32_t shift = b->raw2_ware != 0 ? 0 : 36;
            if (b->raw2_ware != 0) {
                ware_icon(kChainFirst, b->raw2_ware, 0);
            }
            if (b->raw_ware != 0) {
                ware_icon(kChainFirst + 3, b->raw_ware, shift);
            }
            ware_icon(kChainFirst + 6, b->ware, shift);
        } else if (b->prod_kind > sim::prod::kWorkshop && b->prod_kind <= sim::prod::kFisher && b->ware != 0) {
            ware_icon(kChainFirst + 6, b->ware, 73);
        }
    }
}

// ---- Player status -------------------------------------------------------------------------------

namespace {

constexpr std::int32_t kInfoTitle = 57002;
constexpr std::int32_t kInfoArmsShadow = 57003;
constexpr std::int32_t kInfoArms = 57004;
constexpr std::int32_t kInfoBalanceIcon = 57025;
constexpr std::int32_t kInfoPoints = 57027;
constexpr std::int32_t kInfoRating = 57028;
constexpr std::array<std::int32_t, 3> kInfoLists = {57031, 57032, 57033};  // diplomacy, ships, cities
/// Open1602's own: the line that opens the window "Warenbilanz", below "Punkte".
constexpr std::int32_t kInfoGoods = 57040;
/// Label gadget and its [GAME] line; the value is the gadget after it.
constexpr std::array<std::pair<std::int32_t, std::size_t>, 8> kInfoRows = {{
    {57005, 15},  // Einwohner
    {57008, 19},  // Steuererträge
    {57011, 18},  // Betriebskosten
    {57014, 72},  // Heereskosten
    {57017, 64},  // Verkaufserlöse
    {57020, 65},  // Einkaufskosten
    {57023, 80},  // Bilanz
    {57026, 82},  // Punkte
}};

}  // namespace

namespace {

/// INFO.GAD with the line "Warenbilanz" below "Punkte", in the font of the other lines: "Bilanz"
/// moves up by 5, "Punkte" with "Bewertung" by 10 (into the gaps the original leaves between its
/// groups), so that the line fits above the list buttons.
assets::GadgetLayout with_goods_line(assets::GadgetLayout info) {
    const assets::Gadget* label = info.find(kInfoRows.back().first);
    if (label == nullptr) {
        return info;
    }
    assets::Gadget g = *label;
    g.id = kInfoGoods;
    g.kind = assets::GadgetKind::TextLeft;
    g.colour = {6, 3};  // gold, yellow while pressed
    g.no_select = false;
    g.pos = {label->pos[0], label->pos[1] + 20};
    g.size = {190, 25};
    for (assets::Gadget& m : info.gadgets) {
        const std::int32_t up = m.id >= kInfoBalanceIcon - 2 && m.id <= kInfoBalanceIcon ? 5
                                : m.id >= kInfoPoints - 1 && m.id <= kInfoRating         ? 10
                                                                                         : 0;
        m.pos[1] -= up;
    }
    info.gadgets.push_back(g);
    return info;
}

}  // namespace

InfoPanel::InfoPanel(assets::GadgetLayout info, const assets::TextTable* texts)
    : m_panel(with_goods_line(std::move(info)), 1.0) {
    if (texts != nullptr) {
        m_game = texts->section("GAME");
    }
    const auto line = [&](std::size_t n) { return n < m_game.size() ? m_game[n] : std::string(); };
    m_panel.set_text(kInfoTitle, line(38));
    for (const auto& [gadget, text] : kInfoRows) {
        m_panel.set_text(gadget, line(text));
    }
    if (const assets::Gadget* g = m_panel.layout().find(kInfoArms)) {
        m_arms = g->gfx;
    }
    if (const assets::Gadget* g = m_panel.layout().find(kInfoBalanceIcon)) {
        m_balance_icon = g->gfx;
    }
}

void InfoPanel::set_points(std::int64_t points, bool rating) {
    m_panel.set_text(kInfoPoints, std::to_string(points));
    m_panel.set_disabled(kInfoRating, !rating);
}

std::optional<Selection::Kind> InfoPanel::mouse(std::int32_t x, std::int32_t y, bool down) {
    std::optional<Selection::Kind> list;
    for (const ui::PanelEvent& e : m_panel.mouse(x, y, down)) {
        if (e.kind == ui::PanelEvent::Kind::Clicked && e.id == kInfoLists[0]) {
            list = Selection::Kind::Diplomacy;
        } else if (e.kind == ui::PanelEvent::Kind::Clicked && e.id == kInfoLists[1]) {
            list = Selection::Kind::ShipList;
        } else if (e.kind == ui::PanelEvent::Kind::Clicked && e.id == kInfoLists[2]) {
            list = Selection::Kind::CityList;
        } else if (e.kind == ui::PanelEvent::Kind::Clicked && e.id == kInfoRating) {
            m_rating_clicked = true;
        } else if (e.kind == ui::PanelEvent::Kind::Clicked && e.id == kInfoGoods) {
            m_goods_clicked = true;
        }
    }
    return list;
}

std::optional<std::string> InfoPanel::help(const std::vector<std::string>& leiste) const {
    const auto id = m_panel.hovered();
    if (!id) {
        return std::nullopt;
    }
    if (*id == kInfoGoods) {
        return ui::tr("stats.open_help");
    }
    std::size_t line = 0;
    switch (*id) {
        case 57028:
            line = 217;
            break;
        case 57031:
            line = 216;
            break;
        case 57032:
            line = 214;
            break;
        case 57033:
            line = 215;
            break;
        default:
            return std::nullopt;
    }
    return line < leiste.size() ? std::optional<std::string>(leiste[line]) : std::nullopt;
}

void InfoPanel::refresh(const sim::PlayerFigures& f, const render::Banner* banner) {
    const std::array<std::int64_t, 7> values{f.inhabitants, f.taxes,     f.running_costs, f.military,
                                             f.sales,       f.purchases, f.balance};
    for (std::size_t i = 0; i < values.size(); ++i) {
        m_panel.set_text(kInfoRows[i].first + 1, std::to_string(values[i]));
    }
    // The coat of arms in the player's colour, the balance icon green or red.
    const render::BannerArt arms = render::BannerArt::at(m_arms);
    set_banner_graphic(m_panel, kInfoArms, arms, banner);
    set_banner_graphic(m_panel, kInfoArmsShadow, arms, banner);
    m_panel.set_graphic(kInfoBalanceIcon, m_balance_icon + (f.balance < 0 ? 1 : 0), 0);
    m_panel.set_text(kInfoGoods, ui::tr("stats.open"));  // the language may have changed
}

}  // namespace o1602::app
