// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ship's window: the ship, transfer with the own Kontor, trade
// with a foreign one.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "banner_graphic.hpp"
#include "panel_common.hpp"

#include "o1602/sim/construction.hpp"
#include "o1602/sim/diplomacy.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/ui/localization.hpp"
#include "o1602/ui/package_icons.hpp"
#include "o1602/ui/text_format.hpp"

namespace o1602::app {

namespace {

using namespace panel_detail;

// ---- Ships ---------------------------------------------------------------------------------------

constexpr std::uint8_t kCannonWare = sim::ware::kCannons;
constexpr std::array<std::uint32_t, 4> kSteps{32, 5 * 32, 10 * 32, 50 * 32};  // 1, 5, 10, 50 t

// Open1602's row below the four stops of ROUTE.GAD: back to the ship's page (the ship list's
// arrow) and "copy route" on the left, the list arrows of MUSIK.GAD on the right to show the stops
// 1-4 or 5-8.
constexpr std::int32_t kRouteCopy = 58301;
constexpr std::int32_t kRouteUp = 58302;
constexpr std::int32_t kRouteDown = 58303;
constexpr std::int32_t kRouteShip = 58304;
constexpr std::int32_t kRouteShipGfx = 2;    ///< GFXSHIPLIST + this: the ship list's arrow, red
constexpr std::int32_t kRouteShipPress = 2;  ///< and yellow while pressed, as the stops' symbols
constexpr std::int32_t kRouteShipGap = 2;    ///< between it and "copy route" (room for the text)
constexpr std::int32_t kMusikDown = 49011;
constexpr std::int32_t kMusikUp = 49012;
constexpr std::int32_t kStopShift = 4;    ///< how much closer the stops move to each other
constexpr std::int32_t kRouteRowGap = 4;  ///< between the last stop's arrows and the row
constexpr std::int32_t kRouteRowHeight = 26;
constexpr std::int32_t kRouteArrowGap = 4;
constexpr std::int32_t kRouteFont = 0;  // ZEINR, as the stop names
/// Stops ROUTE.GAD shows at once; the others are a second page.
constexpr std::size_t kStopsShown = 4;
/// Wares of a stop ROUTE.GAD shows in the list of stops; the others are on the stop's own page.
constexpr std::size_t kEntriesShown = 2;
// Open1602's page of one stop: its board as the first stop's, below it all its wares in two columns
// (the list's two entries, repeated per row); `+N` on each stop of the list opens it. Ids + the
// ware 0..7 of the stop.
constexpr std::int32_t kStopMore = 58311;  // + the stop's place 0..3 on the page
constexpr std::int32_t kWareIcon = 58410;
constexpr std::int32_t kWareBack = 58420;
constexpr std::int32_t kWareBar = 58430;
constexpr std::int32_t kWareFrame = 58440;
constexpr std::int32_t kWareSlider = 58450;
constexpr std::int32_t kWareArrow = 58460;
constexpr std::int32_t kWareRowPitch = 76;
constexpr std::int32_t kStopMoreRim = 58321;    // its plate: a rim
constexpr std::int32_t kStopMorePlate = 58331;  // and the inside
constexpr std::int32_t kMoreWidth = 26;         ///< the `+N` at the top right of the second ware
constexpr std::int32_t kMoreHeight = 18;
constexpr std::int32_t kMoreFont = 1;  // ZEILITTLE
// Its glyphs carry the spacing on the right and blank rows below the digits: the text is placed
// by these so that the ink sits in the middle of the plate.
constexpr std::int32_t kMoreInkX = 1;
constexpr std::int32_t kMoreInkY = 1;
constexpr std::int32_t kMoreRimColour = 238;    // STADTFLD.COL: pale gold
constexpr std::int32_t kMorePlateColour = 173;  // brown

// Open1602's arrows to page through the pirates' targets (PIRATORD.GAD has three), copied from
// TRANSFER.GAD's goods arrows (45007, 45008) as the end screen's.
constexpr std::int32_t kPirateFirstChoice = 55031;
constexpr std::int32_t kPirateChoices = 3;
constexpr std::int32_t kPirateLeft = 55041;
constexpr std::int32_t kPirateRight = 55042;
constexpr std::int32_t kPirateRightInset = 3;  // the right arrow as far from shield 3 as from the hand

/// PIRATORD.GAD with the scroll arrows of TRANSFER.GAD at both ends of its row of players to hire
/// the pirates against (LC-4); unchanged if either lacks its gadgets.
assets::GadgetLayout with_pirate_arrows(assets::GadgetLayout piratord, const assets::GadgetLayout& transfer) {
    const assets::Gadget* first = piratord.find(kPirateFirstChoice);
    const assets::Gadget* last = piratord.find(kPirateFirstChoice + kPirateChoices - 1);
    if (first == nullptr || last == nullptr) {
        return piratord;
    }
    const std::int32_t middle = first->pos[1] + first->size[1] / 2;
    const std::int32_t left_end = first->pos[0];
    const std::int32_t right_end = last->pos[0] + last->size[0];  // copied: the gadgets grow below
    for (const auto& [from, to] : {std::pair{45007, kPirateLeft}, std::pair{45008, kPirateRight}}) {
        const assets::Gadget* g = transfer.find(from);
        if (g == nullptr) {
            return piratord;
        }
        assets::Gadget arrow = *g;
        arrow.id = to;
        arrow.base = static_cast<std::int32_t>(piratord.gadgets.size());
        arrow.pos = {to == kPirateLeft ? std::max(0, left_end - g->size[0]) : right_end - kPirateRightInset,
                     middle - g->size[1] / 2};
        piratord.gadgets.push_back(arrow);
    }
    return piratord;
}

/// ROUTE.GAD with Open1602's row below its stops, the `+N` of each stop and the gadgets of a stop's
/// page; unchanged without MUSIK.GAD's arrows.
assets::GadgetLayout with_route_row(assets::GadgetLayout route, const std::optional<assets::GadgetLayout>& musik) {
    const assets::Gadget* last = route.find(58151 + static_cast<std::int32_t>(kStopsShown - 1));  // its arrow
    const assets::Gadget* name = route.find(58011);
    const assets::Gadget* down = musik ? musik->find(kMusikDown) : nullptr;
    const assets::Gadget* up = musik ? musik->find(kMusikUp) : nullptr;
    if (last == nullptr || name == nullptr || down == nullptr || up == nullptr) {
        return route;
    }
    // The four stops move closer together (each a few pixels up) to make room for the row.
    for (assets::Gadget& g : route.gadgets) {
        const std::int32_t slot = g.id % 10 - 1;
        if (g.id > 58000 && g.id < 58300 && slot >= 0 && slot < static_cast<std::int32_t>(kStopsShown)) {
            g.pos[1] -= slot * kStopShift;
        }
    }
    const std::int32_t top =
        last->pos[1] + last->size[1] + kRouteRowGap;  // copied: the gadgets grow below (last is moved)
    const std::int32_t right = name->pos[0] + name->size[0];
    const assets::Gadget label = *name;
    std::int32_t left = label.pos[0] + 4;
    if (const assets::Gadget* symbol = route.find(58041)) {  // a button of the same sheet
        assets::Gadget ship = *symbol;
        ship.id = kRouteShip;
        ship.base = static_cast<std::int32_t>(route.gadgets.size());
        ship.gfx = route.constant("GFXSHIPLIST").value_or(620) + kRouteShipGfx;
        ship.press_offset = kRouteShipPress;
        ship.size = {36, 25};
        ship.offset = {0, 0};
        ship.pos = {symbol->pos[0], top + (kRouteRowHeight - ship.size[1]) / 2};  // under the stops' symbols
        route.gadgets.push_back(ship);
        left = ship.pos[0] + ship.size[0] + kRouteShipGap;
    }
    assets::Gadget arrow_down = *down;
    arrow_down.id = kRouteDown;
    arrow_down.base = static_cast<std::int32_t>(route.gadgets.size());
    arrow_down.pos = {right - down->size[0], top + (kRouteRowHeight - down->size[1]) / 2};
    route.gadgets.push_back(arrow_down);
    assets::Gadget arrow_up = *up;
    arrow_up.id = kRouteUp;
    arrow_up.base = static_cast<std::int32_t>(route.gadgets.size());
    arrow_up.pos = {right - down->size[0] - kRouteArrowGap - up->size[0], top + (kRouteRowHeight - up->size[1]) / 2};
    route.gadgets.push_back(arrow_up);
    assets::Gadget copy = label;  // the stop names' font and colours
    copy.id = kRouteCopy;
    copy.kind = assets::GadgetKind::TextLeft;
    copy.base = static_cast<std::int32_t>(route.gadgets.size());
    copy.block = kRouteFont;
    copy.pos = {left, top};
    copy.size = {arrow_up.pos[0] - left - kRouteArrowGap, kRouteRowHeight};
    copy.offset = {0, static_cast<std::int16_t>(kRouteRowHeight - 5)};
    route.gadgets.push_back(copy);
    // `+N` at the top right of each stop's second ware, on a small plate: its own page.
    for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(kStopsShown); ++slot) {
        const assets::Gadget* icon = route.find(58201 + slot);
        if (icon == nullptr) {
            continue;
        }
        const std::array<std::int32_t, 2> pos{icon->pos[0] + icon->size[0] - kMoreWidth, icon->pos[1]};
        for (const bool rim : {true, false}) {
            assets::Gadget plate;
            plate.id = (rim ? kStopMoreRim : kStopMorePlate) + slot;
            plate.kind = assets::GadgetKind::Fill;
            plate.base = static_cast<std::int32_t>(route.gadgets.size());
            plate.no_select = true;
            plate.colour = {rim ? kMoreRimColour : kMorePlateColour, rim ? kMoreRimColour : kMorePlateColour};
            plate.pos = rim ? pos : std::array<std::int32_t, 2>{pos[0] + 1, pos[1] + 1};
            plate.size = rim ? std::array<std::int32_t, 2>{kMoreWidth, kMoreHeight}
                             : std::array<std::int32_t, 2>{kMoreWidth - 2, kMoreHeight - 2};
            route.gadgets.push_back(plate);
        }
        assets::Gadget more = label;
        more.id = kStopMore + slot;
        more.base = static_cast<std::int32_t>(route.gadgets.size());
        more.block = kMoreFont;
        more.pos = pos;
        more.size = {kMoreWidth, kMoreHeight};
        more.offset = {static_cast<std::int16_t>(kMoreWidth / 2 + kMoreInkX),
                       static_cast<std::int16_t>(kMoreHeight + kMoreInkY)};
        route.gadgets.push_back(more);
    }
    // The stop's page: the first stop's two wares (icon, bar background, bar, frame, slider,
    // arrow of the left and right entry), one row of them per two wares.
    constexpr std::array<std::int32_t, 6> kParts{58101, 58111, 58121, 58131, 58141, 58151};
    constexpr std::array<std::int32_t, 6> kWare{kWareIcon, kWareBack, kWareBar, kWareFrame, kWareSlider, kWareArrow};
    for (std::size_t w = 0; w < sim::RouteStop::kEntries; ++w) {
        const auto column = static_cast<std::int32_t>(w % kEntriesShown);
        const auto row = static_cast<std::int32_t>(w / kEntriesShown);
        for (std::size_t part = 0; part < kParts.size(); ++part) {
            const assets::Gadget* g = route.find(kParts[part] + 100 * column);
            if (g == nullptr) {
                continue;
            }
            assets::Gadget ware = *g;
            ware.id = kWare[part] + static_cast<std::int32_t>(w);
            ware.base = static_cast<std::int32_t>(route.gadgets.size());
            ware.pos[1] += row * kWareRowPitch;
            route.gadgets.push_back(ware);
        }
    }
    return route;
}

/// SHIP (the ship), TRANSFER (with the own Kontor), TRADE (with a foreign one), ROUTE (the trade
/// route, with LAGER and HANDEL as ware pickers) and PIRAT/PIRATORD (dealing with the pirates).
class ShipPanel final : public SelectionPanel {
public:
    explicit ShipPanel(ShipLayouts l)
        : m_ship(std::move(l.ship), 1.0),
          m_transfer(std::move(l.transfer), 1.0),
          m_trade(std::move(l.trade), 1.0),
          m_route(with_route_row(std::move(l.route), l.musik), 1.0),
          m_pirat(std::move(l.pirat), 1.0),
          m_piratord(with_pirate_arrows(std::move(l.piratord), m_transfer.layout()), 1.0),
          m_stock(std::move(l.lager)),
          m_lists(std::move(l.handel)) {
        if (l.trader) {
            m_cart.emplace(std::move(*l.trader), 1.0);
        }
        const assets::GadgetLayout& sl = m_ship.layout();
        m_gfx_ware = sl.constant("GFXWARE").value_or(192);
        m_gfx_empty = sl.constant("GFXWARELEER").value_or(188) + kEmptyHold;
        m_gfx_ship = sl.constant("GFXSHIP").value_or(363);
        // Where the buttons of the left column stand in SHIP.GAD, and the bottom of the name.
        for (std::size_t i = 0; i < m_column.size(); ++i) {
            m_column[i] = m_ship.position(kColumn[i]).value_or(std::array<std::int32_t, 2>{});
        }
        if (const auto name = sl.index_of(kNameFrame)) {
            m_name_bottom = sl.gadgets[*name].pos[1] + sl.gadgets[*name].size[1];
        }
        m_ship.set_visible(kDeadSlider, false);
        m_transfer.set_selected(45003, true);
        m_transfer.set_selected(45141, true);
        m_trade.set_selected(33003, true);
        if (const assets::Gadget* g = m_route.layout().find(kStopSymbol)) {
            m_gfx_stop = g->gfx;
        }
        if (const assets::Gadget* g = m_route.layout().find(kStopArms)) {
            m_gfx_arms = g->gfx;
        }
        if (const assets::Gadget* g = m_route.layout().find(kEntryArrow[0])) {
            m_gfx_arrow = g->gfx;
        }
        if (const assets::Gadget* g = m_piratord.layout().find(kPirateArms)) {
            m_gfx_pirate_arms = g->gfx;
        }
        m_route_row = m_route.layout().find(kRouteCopy) != nullptr;
    }
    void set_scale(double scale) override {
        for (ui::Panel* p : {&m_ship, &m_transfer, &m_trade, &m_route, &m_pirat, &m_piratord}) {
            p->set_scale(scale);
        }
        m_stock.panel().set_scale(scale);
        m_lists.panel().set_scale(scale);
        if (m_cart) {
            m_cart->set_scale(scale);
        }
    }
    ui::Panel& panel() override {
        switch (m_page) {
            case Page::Transfer:
                return m_transfer;
            case Page::Trade:
                return m_trade;
            case Page::Route:
                return m_route;
            case Page::PickOwn:
                return m_stock.panel();
            case Page::PickForeign:
                return m_lists.panel();
            case Page::Pirates:
                return m_pirat;
            case Page::PirateOrder:
                return m_piratord;
            default:
                return m_is_cart && m_cart ? *m_cart : m_ship;
        }
    }
    [[nodiscard]] bool picking() const override { return m_pick_stop.has_value() || m_pick_copy; }
    void describe(TutorialView& view) const override {
        view.ship_page = m_page == Page::Root       ? TutorialView::ShipPage::Main
                         : m_page == Page::Transfer ? TutorialView::ShipPage::Transfer
                         : m_page == Page::Trade    ? TutorialView::ShipPage::Trade
                                                    : TutorialView::ShipPage::Other;
        if (m_page == Page::Transfer) {
            view.consumer_goods = category() == kConsumerGoods;
            for (std::int32_t k = 0; k < 4; ++k) {
                if (m_transfer.selected(45031 + k)) {
                    view.kontor_ware = kontor_ware(k);
                }
            }
        }
    }
    void picked(std::uint32_t x, std::uint32_t y, const Selection& s, const PanelContext& c,
                std::vector<sim::Command>& out) override {
        const std::optional<std::uint8_t> stop = std::exchange(m_pick_stop, std::nullopt);
        const bool copy = std::exchange(m_pick_copy, false);
        const sim::Ship* ship = c.world().ship(s.ship);
        if ((!stop && !copy) || ship == nullptr || ship->owner != c.local) {
            return;
        }
        if (copy) {
            // The selected ship's stops go to the ship clicked on: copy here, paste there.
            if (const sim::Ship* to = route_target_near(*ship, c, x, y)) {
                sim::RouteEdit o;
                o.player = c.local;
                o.ship = to->id;
                o.op = sim::RouteEdit::Op::CopyFrom;
                o.target = sim::Target::other_ship(ship->id);
                out.emplace_back(o);
            }
            return;
        }
        // The city of the tile, any owner; no city ends the pick.
        const sim::World& w = c.world();
        if (x >= w.width() || y >= w.height()) {
            return;
        }
        const sim::World::Cell& cell = w.cell(x, y);
        if (cell.island == sim::World::kSea) {
            return;
        }
        const sim::MapCell& top = w.islands()[cell.island].map.top(cell.local_x, cell.local_y);
        const sim::City* city = w.city(cell.island, top.city);
        if (city != nullptr && city->warehouse_x) {
            out.emplace_back(route_stop(*ship, c, *stop, sim::Target::city_of(city->island, city->index)));
            if (m_cart_chain && ship->cart() && *stop + 1 < sim::kCartStops) {
                m_pick_stop = static_cast<std::uint8_t>(*stop + 1);  // a cart's stops one after another
                return;
            }
        }
        m_cart_chain = false;
    }

    void refresh(const Selection& s, const PanelContext& c) override {
        // Selected anew (another ship, or the same after it was deselected): the first page.
        if (m_pages.update(s) != SelectionPages::Change::None) {
            m_page = Page::Root;
            m_hold = 0;
            m_offer = 0;
            m_route_page = 0;
            m_stop_page.reset();
            m_cart_chain = false;
        }
        const sim::Ship* ship = c.world().ship(s.ship);
        if (ship == nullptr) {
            return;
        }
        m_is_cart = ship->cart();
        const Context k = context(*ship, c);
        // Transfer, trade and the pirates end when the ship sails off; the route
        // editor stays. With S-012 brought back (OriginalBugs::remote_trade) transfer and trade
        // stay open and go on with the Kontor they were opened at.
        const bool remote = c.world().original_bugs().remote_trade && m_city.has_value();
        const bool at_kontor = m_page == Page::Transfer                                 ? k.own_kontor || remote
                               : m_page == Page::Trade                                  ? k.foreign_kontor || remote
                               : m_page == Page::Pirates || m_page == Page::PirateOrder ? k.pirates
                                                                                        : true;
        if (ship->owner != c.local || !at_kontor) {
            m_page = Page::Root;
            m_pick_stop.reset();
            m_pick_copy = false;
        }
        switch (m_page) {
            case Page::Root:
                if (m_is_cart && m_cart) {
                    refresh_cart(*ship, k, c);
                } else {
                    refresh_root(*ship, k, c);
                }
                break;
            case Page::Transfer:
                refresh_transfer(*ship, k, c);
                break;
            case Page::Trade:
                refresh_trade(*ship, k, c);
                break;
            case Page::Route:
                refresh_route(*ship, c);
                break;
            case Page::PickOwn:
            case Page::PickForeign:
                refresh_picker(*ship, c);
                break;
            case Page::Pirates:
            case Page::PirateOrder:
                refresh_pirates(c);
                break;
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection& s, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        const sim::Ship* ship = c.world().ship(s.ship);
        if (ship == nullptr || ship->owner != c.local) {
            // Somebody else's ship: only buying it, if it is offered.
            if (ship != nullptr && e.kind == ui::PanelEvent::Kind::Clicked && e.id == kSale && ship->for_sale) {
                out.emplace_back(ship_command(*ship, c, sim::ShipCommand::Op::Buy));
            }
            return;
        }
        switch (m_page) {
            case Page::Transfer:
                clicked_transfer(e, *ship, c, out);
                break;
            case Page::Trade:
                clicked_trade(e, *ship, c, out);
                break;
            case Page::Route:
                clicked_route(e, *ship, c, out);
                break;
            case Page::PickOwn:
            case Page::PickForeign:
                clicked_picker(e, *ship, c, out);
                break;
            case Page::Pirates:
            case Page::PirateOrder:
                clicked_pirates(e, *ship, c, out);
                break;
            default:
                if (ship->cart() && m_cart) {
                    clicked_cart(e, *ship, c, out);
                } else {
                    clicked_root(e, *ship, c, out);
                }
                break;
        }
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& s,
                                                  const PanelContext& c) const override {
        const sim::Ship* ship = c.world().ship(s.ship);
        if (ship == nullptr) {
            return std::nullopt;
        }
        const auto line = [&](std::size_t n) { return std::optional<std::string>(help_line(c, n)); };
        // The name frame: the game's line names a ship, a trade cart has its own.
        const auto name_line = [&]() {
            return ship->cart() ? std::optional<std::string>(ui::tr("cart.name_help")) : line(10);
        };
        // Holds: their goods, the soldiers' kind, or empty.
        const auto hold_text = [&](std::size_t h) -> std::optional<std::string> {
            if (h >= ship->cargo.size()) {
                return std::nullopt;
            }
            return hold_help(*ship, h, c, m_page == Page::Root);
        };
        if (m_page == Page::Root && ship->cart() && m_cart) {
            // TRADER.GAD (LEISTE 166 to 169; the route switch as the ship's).
            switch (id) {
                case kCartName:
                    return name_line();
                case kCartTrade:
                    return line(167);
                case kCartTransfer:
                    return line(166);
                case kCartRouteEditor:
                    return line(168);
                case kCartRoute:
                    return line(ship->route.active ? 110 : 111);
                case kCartStop:
                    return line(169);
                default:
                    break;
            }
            if (id >= kCartHoldButton && id < kCartHoldButton + 8) {
                return hold_text(static_cast<std::size_t>(id - kCartHoldButton));
            }
            return std::nullopt;
        }
        if (m_page == Page::Root) {
            switch (id) {
                case kName:
                    return line(10);
                case kRepair:
                    return line(103);
                case kSale:
                    return line(ship->owner != c.local ? 107 : ship->for_sale ? 106 : 105);
                case kTrade:
                    return line(108);
                case kCannonsDown:
                    return unmount_cannon_help(*ship, c);
                case kCannons:
                    return mounted_cannons_help(*ship, c);
                case kHealthHover:
                    return line(121);
                case kPriceSlider:
                    return line(118);
                case kPriceDown:
                    return line(116);
                case kPriceUp:
                    return line(117);
                case kFound:
                    return line(122);
                case kTransfer:
                    return line(123);
                case kRoute:
                    return line(ship->route.active ? 110 : 111);
                case kStop:
                    return line(127);
                case kExplore:
                    return line(125);
                case kScuttle:
                    return line(238);
                case kPirates:
                    return line(104);
                case kRouteEditor:
                    return line(109);
                default:
                    break;
            }
            if (id >= kHoldButton && id < kHoldButton + 8) {
                return hold_text(static_cast<std::size_t>(id - kHoldButton));
            }
            return std::nullopt;
        }
        if (m_page == Page::Route) {
            return route_help(id, *ship, c);
        }
        if (m_page == Page::PickOwn || m_page == Page::PickForeign) {
            return picker_help(id, *ship, c);
        }
        if (m_page == Page::Pirates || m_page == Page::PirateOrder) {
            return std::nullopt;  // the original has none
        }
        const sim::City* city = m_city ? c.world().city(m_city->first, m_city->second) : nullptr;
        if (m_page == Page::Transfer) {
            switch (id) {
                case 45001:
                    return line(158);
                case 45002:
                    return line(160);
                case 45007:
                case 45008:
                    return line(165);
                case 45122:
                    return name_line();
                case 45124:
                    return line(11);
                default:
                    break;
            }
            if (id >= 45003 && id <= 45006) {
                return line(static_cast<std::size_t>(49 + id - 45003));
            }
            if (id >= 45141 && id <= 45144) {
                return line(static_cast<std::size_t>(161 + id - 45141));
            }
            if (id >= 45071 && id <= 45078) {
                return hold_text(static_cast<std::size_t>(id - 45071));
            }
            if (id >= 45031 && id <= 45034 && city != nullptr) {
                const std::uint8_t w = kontor_ware(static_cast<std::int32_t>(id - 45031));
                if (w == 0) {
                    return std::nullopt;
                }
                const std::uint32_t t = city->stock(w) >> 5U;
                return t >= 1
                           ? ui::format_text(c.text("GAME", 50), {std::int64_t{t}, std::string_view(c.text("WARE", w))})
                           : c.text("WARE", w);
            }
            return std::nullopt;
        }
        // Trade.
        const bool natives = city != nullptr && is_natives(*city, c);
        switch (id) {
            case 33001:
                return line(natives ? 47 : 45);
            case 33002:
                return line(natives ? 48 : 46);
            case 33122:
                return name_line();
            case 33124:
                return line(11);
            default:
                break;
        }
        if (id >= 33003 && id <= 33006) {
            return line(static_cast<std::size_t>(49 + id - 33003));
        }
        if (id >= 33031 && id <= 33038 && city != nullptr) {
            const auto k = static_cast<std::size_t>(id - 33031);
            if (k >= m_offers.size()) {
                return std::nullopt;
            }
            const std::uint8_t w = m_offers[k];
            return ui::format_text(c.text("GAME", 50),
                                   {std::int64_t{sellable(*city, w) >> 5U}, std::string_view(c.text("WARE", w))});
        }
        if (id >= 33071 && id <= 33078 && city != nullptr) {
            const auto h = static_cast<std::size_t>(id - 33071);
            if (h >= ship->cargo.size()) {
                return std::nullopt;
            }
            const sim::CargoSlot& slot = ship->cargo[h];
            if (slot.ware == 0 || slot.amount == 0 || sim::is_soldier_ware(slot.ware)) {
                return hold_text(h);
            }
            const std::uint32_t wanted = buyable(*city, slot.ware);
            const sim::City::Trade* t = city->trade_of(slot.ware);
            if (wanted == 0) {
                return ui::format_text(c.text("GAME", t != nullptr && t->buy ? 67 : 68),
                                       {std::int64_t{slot.amount >> 5U}, std::string_view(c.text("WARE", slot.ware))});
            }
            return ui::format_text(c.text("GAME", natives ? 69 : 70),
                                   {std::int64_t{slot.amount >> 5U}, std::string_view(c.text("WARE", slot.ware)),
                                    std::int64_t{wanted >> 5U}, std::int64_t{t != nullptr ? t->buy_price : 0}});
        }
        return std::nullopt;
    }

    bool back() override {
        if (m_pick_stop || m_pick_copy) {
            m_pick_stop.reset();
            m_pick_copy = false;
            m_cart_chain = false;
            return true;
        }
        switch (m_page) {
            case Page::Root:
                return false;
            case Page::PickOwn:
            case Page::PickForeign:
                m_page = Page::Route;
                return true;
            case Page::PirateOrder:
                m_page = Page::Pirates;
                return true;
            case Page::Route:
                if (m_stop_page) {
                    m_route_page = *m_stop_page / kStopsShown;
                    m_stop_page.reset();
                    return true;
                }
                m_page = Page::Root;
                return true;
            default:
                m_page = Page::Root;
                return true;
        }
    }
    /// The route editor, its pickers and choosing a stop: a right click goes back step by step to
    /// the ship's page, where the route is started.
    [[nodiscard]] bool steps_back() const override {
        return picking() || m_page == Page::Route || m_page == Page::PickOwn || m_page == Page::PickForeign;
    }
    std::optional<Request> take_request() override { return std::exchange(m_request, std::nullopt); }
    /// The mouse wheel over the trade route's list of stops: a page (four stops) per notch, as its
    /// arrows (Open1602).
    bool wheel(std::int32_t notches) override {
        if (m_page != Page::Route || m_stop_page || !m_route_row || notches == 0) {
            return false;
        }
        const auto page = static_cast<std::int64_t>(m_route_page) - notches;
        m_route_page =
            static_cast<std::size_t>(std::clamp<std::int64_t>(page, 0, static_cast<std::int64_t>(route_pages()) - 1));
        return true;
    }

private:
    enum class Page : std::uint8_t { Root, Transfer, Trade, Route, PickOwn, PickForeign, Pirates, PirateOrder };
    static constexpr std::int32_t kName = 38002;
    static constexpr std::int32_t kRepair = 38003;
    static constexpr std::int32_t kSale = 38004;
    static constexpr std::int32_t kTrade = 38005;
    static constexpr std::int32_t kCannonsDown = 38006;
    static constexpr std::int32_t kCannons = 38007;
    static constexpr std::int32_t kHealthFrame = 38008;
    static constexpr std::int32_t kHealth = 38009;
    static constexpr std::int32_t kHealthHover = 38010;
    static constexpr std::int32_t kPriceTrack = 38011;
    static constexpr std::int32_t kPriceSlider = 38012;
    static constexpr std::int32_t kPriceDown = 38013;
    static constexpr std::int32_t kPriceUp = 38014;
    static constexpr std::int32_t kPriceLabel = 38015;
    static constexpr std::int32_t kPrice = 38016;
    static constexpr std::int32_t kFound = 38017;
    static constexpr std::int32_t kTransfer = 38018;
    static constexpr std::int32_t kPirates = 38019;
    static constexpr std::int32_t kRoute = 38020;
    static constexpr std::int32_t kRouteEditor = 38021;
    static constexpr std::int32_t kStop = 38022;
    static constexpr std::int32_t kExplore = 38023;
    static constexpr std::int32_t kExploreRing = 38024;
    static constexpr std::int32_t kScuttle = 38025;
    static constexpr std::int32_t kHoldIcon = 38051;
    static constexpr std::int32_t kHoldBar = 38071;
    static constexpr std::int32_t kHoldButton = 38091;
    static constexpr std::int32_t kDeadSlider = 38111;
    // ROUTE.GAD: + the stop's place 0..3 on the page; entries 0 and 1 of a stop.
    static constexpr std::int32_t kStopBoard = 58001;
    static constexpr std::int32_t kStopName = 58011;
    static constexpr std::int32_t kStopArmsShadow = 58021;
    static constexpr std::int32_t kStopArms = 58031;
    static constexpr std::int32_t kStopSymbol = 58041;
    static constexpr std::array<std::int32_t, 2> kEntryIcon{58101, 58201};
    static constexpr std::array<std::int32_t, 2> kEntryBack{58111, 58211};
    static constexpr std::array<std::int32_t, 2> kEntryBar{58121, 58221};
    static constexpr std::array<std::int32_t, 2> kEntryFrame{58131, 58231};
    static constexpr std::array<std::int32_t, 2> kEntrySlider{58141, 58241};
    static constexpr std::array<std::int32_t, 2> kEntryArrow{58151, 58251};
    // PIRAT.GAD, PIRATORD.GAD.
    static constexpr std::int32_t kBuyPeace = 54004;
    static constexpr std::int32_t kHire = 54005;
    static constexpr std::int32_t kPirateText = 55011;                 // + line 0..2
    static constexpr std::int32_t kPirateArms = 55021;                 // + player 0..2
    static constexpr std::int32_t kPirateChoice = kPirateFirstChoice;  // + shown player 0..2
    static constexpr std::int32_t kPay = 55004;
    static constexpr std::int32_t kCostLabel = 55005;
    static constexpr std::int32_t kCost = 55006;

    /// What the ship can do where it lies.
    struct Context {
        bool foreign = true;  ///< not the local player's ship
        bool moving = false;
        bool own_kontor = false;                ///< at an own Kontor: transfer
        bool foreign_kontor = false;            ///< at another's Kontor with a trade treaty: trade
        bool found = false;                     ///< land within 4 tiles on an island without an own city
        std::optional<sim::CostBuilding> yard;  ///< an own shipyard in reach
        sim::ExploreState explore = sim::ExploreState::NotAllowed;
        bool pirates = false;                                        ///< at the pirates' Kontor (and trade)
        std::optional<std::pair<std::uint16_t, std::uint8_t>> city;  ///< the Kontor's city
    };

    Context context(const sim::Ship& ship, const PanelContext& c) {
        Context k;
        const sim::World& w = c.world();
        k.foreign = ship.owner != c.local;
        const sim::ShipMotion& m = ship.motion;
        k.moving = m.phase == sim::ShipMotion::Phase::Run || m.phase == sim::ShipMotion::Phase::Turn ||
                   (m.path_index < m.path.size());
        k.explore = c.simulation.fleet().explore_state(ship, c.local);
        if (k.foreign) {
            return k;
        }
        // With S-013 brought back (OriginalBugs::remote_repair) a ship can be put to repair as it
        // sails off from the yard, and is repaired on the way.
        const bool remote_repair = c.world().original_bugs().remote_repair;
        if (ship.cart()) {
            // A trade cart on a tile touching a market or Kontor: its own city's first, else one it
            // may trade with (TRADER.GAD 59004, 59003), as in the original.
            if (!k.moving) {
                for (const std::size_t index :
                     sim::cities_near_cart(w, c.buildings, ship.x, ship.y, sim::kCartHandReach)) {
                    const sim::City& city = w.cities()[index];
                    if (city.owner == ship.owner) {
                        k.city = std::make_pair(city.island, city.index);
                        k.own_kontor = true;
                        k.foreign_kontor = false;
                        break;
                    }
                    if (!k.city && w.diplomacy().may_trade(ship.owner, city.owner)) {
                        k.city = std::make_pair(city.island, city.index);
                        k.foreign_kontor = true;
                    }
                }
            }
            return k;
        }
        if (k.moving && !remote_repair) {
            return k;
        }
        if (k.moving) {
            // Sailing: only the yard it leaves counts.
        } else if (const auto index = sim::kontor_in_reach(w, c.buildings, ship.x, ship.y)) {
            const sim::City& city = w.cities()[*index];
            k.city = std::make_pair(city.island, city.index);
            const sim::Player* owner = w.player(city.owner);
            if (!sim::kontor_open(w, c.buildings, city)) {
                // A destroyed Kontor of the natives, pirates or a defeated player: no buttons.
            } else if (city.owner == ship.owner) {
                k.own_kontor = true;
            } else if (owner != nullptr && w.behaviour(*owner) == sim::FactionBehaviour::Pirates) {
                // Dealing with the pirates, and with a trade treaty (players start with one) trading
                // at their Kontor as at any other (both buttons, as in the original).
                k.pirates = true;
                k.foreign_kontor = w.diplomacy().may_trade(ship.owner, city.owner);
            } else if (w.diplomacy().may_trade(ship.owner, city.owner)) {  // natives too: by treaty
                k.foreign_kontor = true;
            }
            // The destroyed Kontor of a defenceless city of another human or computer player can be
            // built over with a Kontor of one's own, which takes the city over: the ship offers it.
            const auto kontor =
                city.warehouse_x && city.warehouse_y
                    ? sim::building_at(w, c.buildings, city.island, {*city.warehouse_x, *city.warehouse_y})
                    : std::nullopt;
            k.found = kontor && kontor->type->destroyed && city.owner != ship.owner && owner != nullptr &&
                      (owner->role == sim::PlayerRole::Human || owner->role == sim::PlayerRole::Computer) &&
                      sim::city_defenceless(w, c.buildings, city.island, city.index);
        } else if (const auto site = sim::kontor_site_near(w, c.buildings, ship.x, ship.y)) {
            // Land within 4 tiles of a ship in an island's rectangle where the player has no city
            // yet.
            k.found = std::none_of(w.cities().begin(), w.cities().end(), [&](const sim::City& ct) {
                return ct.island == site->island && ct.owner == ship.owner;
            });
        }
        for (const sim::CostBuilding& b : w.cost_buildings()) {
            if (b.kind != sim::CostBuilding::Kind::Shipyard || b.island >= w.islands().size()) {
                continue;
            }
            const sim::MapCell& top = w.islands()[b.island].map.top(b.x, b.y);
            if (top.owner == ship.owner && sim::building_in_reach(w, c.buildings, b.island, b.x, b.y, ship.x, ship.y)) {
                k.yard = b;
                break;
            }
        }
        return k;
    }

    [[nodiscard]] static bool is_natives(const sim::City& city, const PanelContext& c) {
        const sim::Player* p = c.world().player(city.owner);
        return p != nullptr && c.world().behaviour(*p) == sim::FactionBehaviour::Natives;
    }
    [[nodiscard]] static sim::ShipCommand ship_command(const sim::Ship& ship, const PanelContext& c,
                                                       sim::ShipCommand::Op op) {
        sim::ShipCommand o;
        o.player = c.local;
        o.ship = ship.id;
        o.op = op;
        return o;
    }
    [[nodiscard]] std::uint32_t step(const ui::Panel& p, std::int32_t first) const {
        for (std::size_t k = 0; k < kSteps.size(); ++k) {
            if (p.selected(first + static_cast<std::int32_t>(k))) {
                return kSteps[k];
            }
        }
        return kSteps[0];
    }

    void holds(ui::Panel& p, const sim::Ship& ship, const PanelContext& c, std::int32_t icon, std::int32_t bar,
               std::int32_t button, bool shown) {
        const std::size_t n = sim::usable_holds(ship, c.figures);
        for (std::size_t h = 0; h < 8; ++h) {
            const auto k = static_cast<std::int32_t>(h);
            const bool hold = shown && h < n;
            p.set_visible(icon + k, hold);
            p.set_visible(button + k, hold);
            const bool filled = hold && ship.cargo[h].ware != 0 && ship.cargo[h].amount != 0;
            p.set_visible(bar + k, filled);
            if (!hold) {
                continue;
            }
            const sim::CargoSlot& slot = ship.cargo[h];
            p.set_graphic(icon + k, filled ? ui::ware_sprite(m_gfx_ware, c.world().goods(), slot.ware) : m_gfx_empty,
                          0);
            if (filled) {
                p.set_slider(bar + k, 1, static_cast<std::int32_t>(std::min(slot.amount, sim::hold_capacity(ship))),
                             static_cast<std::int32_t>(sim::hold_capacity(ship)));
            }
        }
    }

    void refresh_root(const sim::Ship& ship, const Context& k, const PanelContext& c) {
        // The name: a click on it types a new one (own ships that are not sinking).
        m_ship.set_text(kName, c.shown_name(NameTarget::of(ship), ship_name(ship)));
        m_ship.set_no_select(kName, !can_rename(c.world(), c.local, NameTarget::of(ship)));
        const bool own = !k.foreign;
        const bool idle = own && !k.moving;
        m_ship.set_visible(kTrade, idle && k.foreign_kontor);
        m_ship.set_visible(kTransfer, idle && k.own_kontor);
        const assets::FigureType* f = c.figures.get(ship.figure);
        const std::uint32_t max_health = f != nullptr ? std::max<std::uint32_t>(f->ship.max_health, 1) : 1;
        const std::uint32_t health = ship.health == 0 ? max_health : ship.health;
        const bool repair_moving = own && c.world().original_bugs().remote_repair;
        m_ship.set_visible(kRepair, (idle || repair_moving) && k.yard.has_value() && health < max_health);
        m_ship.set_selected(kRepair, ship.repairing);
        m_ship.set_visible(kFound, idle && k.found);
        m_ship.set_visible(kStop, own && k.moving);
        const bool running = k.explore == sim::ExploreState::Running;
        m_ship.set_visible(kExplore, own && (k.explore == sim::ExploreState::Possible || running));
        m_ship.set_visible(kExploreRing, own && running);
        if (running) {
            m_ship.set_graphic(kExploreRing, m_gfx_ship + 28 + (11 - std::min<std::int32_t>(ship.explore_steps, 11)),
                               0);
        }
        // Sale: an own ship can be offered; another's only shows its price, and can be bought.
        m_ship.set_visible(kSale, own || ship.for_sale);
        m_ship.set_selected(kSale, ship.for_sale && own);
        for (const std::int32_t id : {kPriceTrack, kPriceSlider, kPriceDown, kPriceUp}) {
            m_ship.set_visible(id, ship.for_sale && own);
        }
        m_ship.set_visible(kPriceLabel, ship.for_sale);
        m_ship.set_visible(kPrice, ship.for_sale);
        if (ship.for_sale) {
            m_ship.set_text(kPriceLabel, c.text("GAME", 10));
            m_ship.set_text(kPrice, "$ " + std::to_string(c.simulation.fleet().sale_price(ship)));
            if (m_ship.held() != kPriceSlider) {
                m_ship.set_slider(kPriceSlider, 0, ship.sale_factor, sim::ShipCommand::kMaxSaleFactor);
            }
        }
        for (const std::int32_t id : {kHealthFrame, kHealth, kHealthHover, kCannonsDown, kCannons}) {
            m_ship.set_visible(id, own);
        }
        m_ship.set_slider(kHealth, 0, static_cast<std::int32_t>(std::uint64_t{health} * 1000 / max_health), 1000);
        m_ship.set_text(kCannons, std::to_string(ship.cannons));
        m_ship.set_disabled(kCannonsDown, ship.cannons == 0);
        m_ship.set_visible(kRoute, own && !ship.for_sale);  // also without stops
        m_ship.set_visible(kRouteEditor, own && !ship.for_sale);
        m_ship.set_visible(kScuttle, own && !ship.for_sale);
        m_ship.set_visible(kPirates, idle && k.pirates);
        arrange_column(own && (k.explore == sim::ExploreState::Possible || running),
                       idle && k.yard.has_value() && health < max_health, idle && k.pirates);
        m_ship.set_selected(kRoute, !ship.route.active);
        holds(m_ship, ship, c, kHoldIcon, kHoldBar, kHoldButton, own || ship.for_sale);
    }

    /// TRADER.GAD, the trade cart's window: its name and holds; stop while it drives, the exchange
    /// with its own city's market or Kontor or trade with a foreign one where it stands (one place
    /// for the three); the route.
    void refresh_cart(const sim::Ship& ship, const Context& k, const PanelContext& c) {
        ui::Panel& p = *m_cart;
        p.set_text(kCartName, c.shown_name(NameTarget::of(ship), ship_name(ship)));
        p.set_no_select(kCartName, !can_rename(c.world(), c.local, NameTarget::of(ship)));
        const bool own = !k.foreign;
        const bool idle = own && !k.moving;
        p.set_visible(kCartStop, own && k.moving);
        p.set_visible(kCartTransfer, idle && k.own_kontor);
        p.set_visible(kCartTrade, idle && k.foreign_kontor);
        p.set_visible(kCartRoute, own);
        p.set_visible(kCartRouteEditor, own);
        p.set_selected(kCartRoute, !ship.route.active);
        holds(p, ship, c, kCartHoldIcon, kCartHoldBar, kCartHoldButton, own);
    }

    void clicked_cart(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                      std::vector<sim::Command>& out) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        // The same orders as the ship's buttons.
        const std::int32_t as_ship = e.id == kCartName          ? kName
                                     : e.id == kCartStop        ? kStop
                                     : e.id == kCartTransfer    ? kTransfer
                                     : e.id == kCartTrade       ? kTrade
                                     : e.id == kCartRoute       ? kRoute
                                     : e.id == kCartRouteEditor ? kRouteEditor
                                                                : 0;
        if (as_ship != 0) {
            clicked_root(ui::PanelEvent{e.kind, as_ship}, ship, c, out);
        }
        if (e.id == kCartRouteEditor) {
            // "Automatische Handelsroute festlegen": the stops are clicked on the map one after
            // another, from the first free one (LEISTE 170); a click beside a city or Escape ends
            // it. Open1602 shows the route editor meanwhile, with the wares of each stop.
            std::uint8_t first = 0;
            while (first + 1 < sim::kCartStops && !ship.route.stops[first].target.empty()) {
                ++first;
            }
            m_pick_stop = first;
            m_pick_copy = false;
            m_cart_chain = true;
        }
    }

    /// U-013: in the original, "repair" and "pirates" take the place of "explore" (one button
    /// over the other). When both apply, Open1602 puts the other button right under the name and
    /// "explore" below it, so every action can be clicked.
    void arrange_column(bool explore, bool repair, bool pirates) {
        const bool apart = explore && (repair || pirates);
        std::int32_t explore_top = m_column[2][1];
        for (std::size_t i = 0; i < 2; ++i) {
            const std::int32_t y = apart ? m_name_bottom + 2 : m_column[i][1];
            m_ship.set_position(kColumn[i], m_column[i][0], y);
            const bool shown = i == 0 ? repair : pirates;
            if (apart && shown) {
                if (const auto g = m_ship.layout().index_of(kColumn[i])) {
                    explore_top = std::max(explore_top, y + m_ship.layout().gadgets[*g].size[1]);
                }
            }
        }
        for (std::size_t i = 2; i < m_column.size(); ++i) {
            m_ship.set_position(kColumn[i], m_column[i][0], m_column[i][1] + (explore_top - m_column[2][1]));
        }
    }

    void clicked_root(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                      std::vector<sim::Command>& out) {
        if (e.kind == ui::PanelEvent::Kind::Slid && e.id == kPriceSlider) {
            const auto factor =
                static_cast<std::uint16_t>(m_ship.slider(kPriceSlider, 0, sim::ShipCommand::kMaxSaleFactor));
            if (factor != ship.sale_factor) {
                sim::ShipCommand o = ship_command(ship, c, sim::ShipCommand::Op::OfferForSale);
                o.factor = factor;
                out.emplace_back(o);
            }
            return;
        }
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const Context k = context(ship, c);
        switch (e.id) {
            case kName:
                c.start_rename(NameTarget::of(ship), out);
                break;
            case kStop: {
                sim::ShipOrder o;
                o.player = c.local;
                o.ship = ship.id;
                out.emplace_back(o);
                break;
            }
            case kExplore: {
                sim::ExploreOrder o;
                o.player = c.local;
                o.ship = ship.id;
                out.emplace_back(o);
                break;
            }
            case kRoute: {
                sim::RouteEdit o;
                o.player = c.local;
                o.ship = ship.id;
                o.op = ship.route.active ? sim::RouteEdit::Op::Stop : sim::RouteEdit::Op::Start;
                out.emplace_back(o);
                break;
            }
            case kRepair:
                if (k.yard) {
                    sim::ShipyardOrder o;
                    o.player = c.local;
                    o.island = k.yard->island;
                    o.x = k.yard->x;
                    o.y = k.yard->y;
                    o.op = sim::ShipyardOrder::Op::Repair;
                    o.ship = ship.id;
                    o.on = !ship.repairing;
                    out.emplace_back(o);
                }
                break;
            case kSale:
                out.emplace_back(ship_command(
                    ship, c, ship.for_sale ? sim::ShipCommand::Op::WithdrawSale : sim::ShipCommand::Op::OfferForSale));
                break;
            case kPriceDown:
            case kPriceUp: {
                sim::ShipCommand o = ship_command(ship, c, sim::ShipCommand::Op::OfferForSale);
                o.factor = static_cast<std::uint16_t>(std::clamp<int>(ship.sale_factor + (e.id == kPriceUp ? 1 : -1), 0,
                                                                      sim::ShipCommand::kMaxSaleFactor));
                out.emplace_back(o);
                break;
            }
            case kCannonsDown:
                if (ship.cannons > 0) {
                    out.emplace_back(unmount_cannons(ship, c));
                }
                break;
            case kCannons:
                // Open1602: Shift+click on the mounted cannons takes them all off.
                if (c.shift && ship.cannons > 0) {
                    out.emplace_back(unmount_cannons(ship, c));
                }
                break;
            case kTrade:
            case kTransfer:
                if (k.city) {
                    m_city = k.city;
                    m_page = e.id == kTrade ? Page::Trade : Page::Transfer;
                    // The fullest hold is chosen first; an empty ship starts at the first one.
                    m_hold = static_cast<std::int32_t>(sim::fullest_hold(ship, c.figures));
                    m_offer = 0;
                }
                break;
            case kFound: {
                m_request = Request{Request::Kind::FoundKontor, ship.id};
                // The island is asked at the same time: a computer with a city there answers.
                sim::DiplomacyOrder ask;
                ask.player = c.local;
                ask.kind = sim::DiplomacyOrder::Kind::AskFounding;
                ask.ship = ship.id;
                out.emplace_back(ask);
                break;
            }
            case kScuttle:
                out.emplace_back(ship_command(ship, c, sim::ShipCommand::Op::Scuttle));
                break;
            case kPirates:
                if (k.city) {
                    m_city = k.city;
                    m_page = Page::Pirates;
                }
                break;
            case kRouteEditor:
                // Opening the editor drops the stops that are no city (patrol points).
                for (std::uint8_t stop = 0; stop < sim::TradeRoute::kStops; ++stop) {
                    const sim::Target& t = ship.route.stops[stop].target;
                    if (!t.empty() && t.kind != sim::Target::Kind::City) {
                        sim::RouteEdit o;
                        o.player = c.local;
                        o.ship = ship.id;
                        o.op = sim::RouteEdit::Op::ClearStop;
                        o.stop = stop;
                        out.emplace_back(o);
                    }
                }
                m_page = Page::Route;
                m_stop_page.reset();
                // The page with the stop it sails to.
                m_route_page = m_route_row && ship.route.active ? ship.route.current / kStopsShown : 0;
                break;
            default:
                if (e.id >= kHoldButton && e.id < kHoldButton + 8) {
                    const auto h = static_cast<std::size_t>(e.id - kHoldButton);
                    if (h >= ship.cargo.size()) {
                        break;
                    }
                    const sim::CargoSlot& slot = ship.cargo[h];
                    if (sim::is_soldier_ware(slot.ware) && slot.amount != 0) {
                        // Soldiers in the hold go ashore.
                        sim::UnloadOrder o;
                        o.player = c.local;
                        o.ship = ship.id;
                        o.hold = static_cast<std::uint8_t>(h);
                        out.emplace_back(o);
                    } else if (slot.ware == kCannonWare && slot.amount >= 32) {
                        out.emplace_back(mount_cannons(ship, h, c));
                    }
                }
                break;
        }
    }

    // Transfer: the wares of the Kontor in four groups, four at a time.
    struct WareRange {
        std::uint8_t first;
        std::uint8_t last;
    };
    static constexpr std::int32_t kConsumerGoods = 2;  // the group "Bedarfsgüter" (45143)
    static WareRange category_range(std::int32_t cat) {
        switch (cat) {
            case 0:
                return {22, 24};  // Baumaterial
            case 1:
                return {11, 13};  // Waffen
            case kConsumerGoods:
                return {14, 21};  // Bedarfsgüter
            default:
                return {2, 10};  // Rohstoffe
        }
    }
    static std::int32_t category_of(std::uint8_t w) {
        for (std::int32_t c = 0; c < 4; ++c) {
            const auto [first, last] = category_range(c);
            if (w >= first && w <= last) {
                return c;
            }
        }
        return 0;
    }
    [[nodiscard]] std::int32_t category() const {
        for (std::int32_t k = 0; k < 4; ++k) {
            if (m_transfer.selected(45141 + k)) {
                return k;
            }
        }
        return 0;
    }
    /// The ware on Kontor button k (0..3), 0 if none.
    [[nodiscard]] std::uint8_t kontor_ware(std::int32_t k) const {
        const std::int32_t cat = category();
        const auto [first, last] = category_range(cat);
        const std::int32_t w = first + m_scroll[static_cast<std::size_t>(cat)] + k;
        return w <= last ? static_cast<std::uint8_t>(w) : 0;
    }
    void show_ware(std::uint8_t w) {
        const std::int32_t cat = category_of(w);
        for (std::int32_t k = 0; k < 4; ++k) {
            m_transfer.set_selected(45141 + k, k == cat);
        }
        const auto [first, last] = category_range(cat);
        std::int32_t& scroll = m_scroll[static_cast<std::size_t>(cat)];
        if (last - first + 1 > 4) {
            scroll = std::clamp<std::int32_t>(w - first - 1, 0, last - first - 3);
        }
        for (std::int32_t k = 0; k < 4; ++k) {
            m_transfer.set_selected(45031 + k, kontor_ware(k) == w);
        }
    }

    void refresh_transfer(const sim::Ship& ship, const Context&, const PanelContext& c) {
        const sim::City* city = m_city ? c.world().city(m_city->first, m_city->second) : nullptr;
        if (city == nullptr) {
            m_page = Page::Root;
            return;
        }
        m_transfer.set_text(45122, ship_name(ship));
        m_transfer.set_text(45124, city_name(*city));
        const std::int32_t cat = category();
        const auto [first, last] = category_range(cat);
        const bool scrolls = last - first + 1 > 4;
        const std::int32_t scroll = m_scroll[static_cast<std::size_t>(cat)];
        m_transfer.set_visible(45007, scrolls && scroll > 0);
        m_transfer.set_visible(45008, scrolls && scroll < last - first - 3);
        const auto cap = static_cast<std::int32_t>(std::max<std::uint32_t>(city->capacity, 1));
        for (std::int32_t k = 0; k < 4; ++k) {
            const std::uint8_t w = kontor_ware(k);
            // A slot the goods group leaves free shows the empty slot (water), without a bar or a button.
            m_transfer.set_visible(45011 + k, true);
            m_transfer.set_visible(45031 + k, w != 0);
            m_transfer.set_visible(45131 + k, w != 0);
            if (w == 0) {
                m_transfer.set_graphic(45011 + k, m_gfx_empty, 0);
                m_transfer.set_disabled(45011 + k, false);
                continue;
            }
            m_transfer.set_graphic(45011 + k, ui::ware_sprite(m_gfx_ware, c.world().goods(), w), 0);
            m_transfer.set_disabled(45011 + k, city->stock(w) == 0);
            m_transfer.set_slider(
                45131 + k, 1, static_cast<std::int32_t>(std::min<std::uint32_t>(city->stock(w), city->capacity)), cap);
        }
        holds(m_transfer, ship, c, 45051, 45091, 45071, true);
        for (std::int32_t h = 0; h < 8; ++h) {
            m_transfer.set_selected(45071 + h, h == m_hold);
        }
    }

    void clicked_transfer(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                          std::vector<sim::Command>& out) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const std::uint32_t amount = step(m_transfer, 45003);
        const std::int32_t cat = category();
        const auto [first, last] = category_range(cat);
        std::int32_t& scroll = m_scroll[static_cast<std::size_t>(cat)];
        if (e.id == 45007) {
            scroll = std::max(0, scroll - 1);
        } else if (e.id == 45008) {
            scroll = std::min(scroll + 1, std::max(0, last - first - 3));
        } else if (e.id >= 45071 && e.id <= 45078) {
            m_hold = e.id - 45071;
            const auto h = static_cast<std::size_t>(m_hold);
            if (h < ship.cargo.size() && c.world().goods().is_trade(ship.cargo[h].ware) && ship.cargo[h].amount != 0) {
                show_ware(ship.cargo[h].ware);  // the Kontor shows the same ware
            }
        } else if (e.id == 45001) {
            std::uint8_t w = 0;
            for (std::int32_t k = 0; k < 4; ++k) {
                if (m_transfer.selected(45031 + k)) {
                    w = kontor_ware(k);
                }
            }
            if (c.world().goods().is_trade(w)) {
                sim::CargoTransfer o;
                if (m_city) {  // the Kontor of the window (OriginalBugs::remote_trade)
                    o.kontor_island = m_city->first;
                    o.kontor_city = m_city->second;
                }
                o.player = c.local;
                o.ship = ship.id;
                o.ware = w;
                o.to_ship = true;
                o.amount = amount;
                out.emplace_back(o);
            }
        } else if (e.id == 45002) {
            const auto h = static_cast<std::size_t>(m_hold);
            if (h < ship.cargo.size() && c.world().goods().is_trade(ship.cargo[h].ware) && ship.cargo[h].amount != 0) {
                show_ware(ship.cargo[h].ware);
                sim::CargoTransfer o;
                if (m_city) {  // the Kontor of the window (OriginalBugs::remote_trade)
                    o.kontor_island = m_city->first;
                    o.kontor_city = m_city->second;
                }
                o.player = c.local;
                o.ship = ship.id;
                o.ware = ship.cargo[h].ware;
                o.to_ship = false;
                o.amount = std::min(amount, ship.cargo[h].amount);
                o.hold = static_cast<std::uint8_t>(h);
                out.emplace_back(o);
            }
        }
    }

    // Trade.
    [[nodiscard]] static std::uint32_t sellable(const sim::City& city, std::uint8_t w) {
        const sim::City::Trade* t = city.trade_of(w);
        if (t == nullptr || !t->sell) {
            return 0;
        }
        const std::int64_t n = std::int64_t{city.stock(w)} - t->keep - city.reserved_of(w);
        return n < 32 ? 0 : static_cast<std::uint32_t>(n);
    }
    [[nodiscard]] static std::uint32_t buyable(const sim::City& city, std::uint8_t w) {
        const sim::City::Trade* t = city.trade_of(w);
        if (t == nullptr || !t->buy) {
            return 0;
        }
        const std::int64_t n = std::int64_t{t->limit} - city.stock(w) + city.reserved_of(w);
        return n < 32 ? 0 : static_cast<std::uint32_t>(n);
    }

    void refresh_trade(const sim::Ship& ship, const Context&, const PanelContext& c) {
        const sim::City* city = m_city ? c.world().city(m_city->first, m_city->second) : nullptr;
        if (city == nullptr) {
            m_page = Page::Root;
            return;
        }
        const bool natives = is_natives(*city, c);
        set_city_plate(m_trade, *city, c);
        m_trade.set_text(33122, ship_name(ship));
        m_trade.set_text(33124, city_name(*city));
        const assets::BuildingType* kontor = sim::kontor_of(c.world(), c.buildings, *city);
        const std::int32_t places = std::min<std::int32_t>(kontor != nullptr ? kontor->max_wares : 8, 8);
        m_offers.clear();
        for (const std::uint8_t w : c.world().goods().trade_wares()) {
            if (sellable(*city, w) >= 32) {
                m_offers.push_back(w);
            }
        }
        const auto cap = static_cast<std::int32_t>(std::max<std::uint32_t>(city->capacity, 1));
        for (std::int32_t k = 0; k < 8; ++k) {
            const bool place = k < places;
            const bool ware = place && k < static_cast<std::int32_t>(m_offers.size());
            m_trade.set_visible(33011 + k, place);
            m_trade.set_visible(33031 + k, place);
            m_trade.set_visible(33131 + k, ware);
            m_trade.set_selected(33031 + k, k == m_offer);
            if (ware) {
                const std::uint8_t w = m_offers[static_cast<std::size_t>(k)];
                m_trade.set_graphic(33011 + k, ui::ware_sprite(m_gfx_ware, c.world().goods(), w), 0);
                m_trade.set_slider(
                    33131 + k, 1,
                    static_cast<std::int32_t>(std::min<std::uint32_t>(sellable(*city, w), city->capacity)), cap);
            } else if (place) {
                m_trade.set_graphic(33011 + k, m_gfx_empty, 0);
            }
        }
        const bool offer = m_offer < static_cast<std::int32_t>(m_offers.size());
        m_trade.set_visible(33111, offer);
        m_trade.set_visible(33112, offer && !natives);
        m_trade.set_visible(33113, offer && !natives);
        m_trade.set_disabled(33001, !offer);
        if (offer) {
            const std::uint8_t w = m_offers[static_cast<std::size_t>(m_offer)];
            m_trade.set_text(33111, c.text("WARE", w));
            const sim::City::Trade* t = city->trade_of(w);
            m_trade.set_text(33112, std::to_string(t != nullptr ? t->sell_price : 0));
        }
        holds(m_trade, ship, c, 33051, 33091, 33071, true);
        if (m_hold >= static_cast<std::int32_t>(ship.cargo.size())) {
            m_hold = 0;
        }
        for (std::int32_t h = 0; h < 8; ++h) {
            m_trade.set_selected(33071 + h, h == m_hold);
        }
        const auto h = static_cast<std::size_t>(m_hold);
        const std::uint8_t hw = h < ship.cargo.size() && ship.cargo[h].amount != 0 ? ship.cargo[h].ware : 0;
        const bool wanted = c.world().goods().is_trade(hw) && buyable(*city, hw) >= 32;
        m_trade.set_visible(33114, wanted);
        m_trade.set_visible(33115, wanted && !natives);
        m_trade.set_visible(33116, wanted && !natives);
        m_trade.set_disabled(33002, !wanted);
        if (wanted) {
            const sim::City::Trade* t = city->trade_of(hw);
            m_trade.set_text(33114, c.text("WARE", hw));
            m_trade.set_text(33115, std::to_string(t != nullptr ? t->buy_price : 0));
        }
    }

    void clicked_trade(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                       std::vector<sim::Command>& out) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const std::uint32_t amount = step(m_trade, 33003);
        if (e.id >= 33031 && e.id <= 33038) {
            m_offer = e.id - 33031;
        } else if (e.id >= 33071 && e.id <= 33078) {
            m_hold = e.id - 33071;
        } else if (e.id == 33001 && m_offer < static_cast<std::int32_t>(m_offers.size())) {
            sim::CargoTransfer o;
            if (m_city) {  // the Kontor of the window (OriginalBugs::remote_trade)
                o.kontor_island = m_city->first;
                o.kontor_city = m_city->second;
            }
            o.player = c.local;
            o.ship = ship.id;
            o.ware = m_offers[static_cast<std::size_t>(m_offer)];
            o.to_ship = true;
            o.amount = amount;  // the simulation takes what fits, what is offered and what is paid for
            out.emplace_back(o);
        } else if (e.id == 33002) {
            const auto h = static_cast<std::size_t>(m_hold);
            if (h < ship.cargo.size() && c.world().goods().is_trade(ship.cargo[h].ware) && ship.cargo[h].amount != 0) {
                sim::CargoTransfer o;
                if (m_city) {  // the Kontor of the window (OriginalBugs::remote_trade)
                    o.kontor_island = m_city->first;
                    o.kontor_city = m_city->second;
                }
                o.player = c.local;
                o.ship = ship.id;
                o.ware = ship.cargo[h].ware;
                o.to_ship = false;
                o.amount = std::min(amount, ship.cargo[h].amount);
                o.hold = static_cast<std::uint8_t>(h);
                out.emplace_back(o);
            }
        }
    }

    // ---- The trade route (ROUTE, pickers LAGER and HANDEL) ----

    [[nodiscard]] static sim::RouteEdit route_stop(const sim::Ship& ship, const PanelContext& c, std::uint8_t stop,
                                                   const sim::Target& target) {
        sim::RouteEdit o;
        o.player = c.local;
        o.ship = ship.id;
        o.op = target.empty() ? sim::RouteEdit::Op::ClearStop : sim::RouteEdit::Op::SetStop;
        o.stop = stop;
        o.target = target;
        return o;
    }
    [[nodiscard]] static sim::RouteEdit route_entry(const sim::Ship& ship, const PanelContext& c, std::uint8_t stop,
                                                    std::uint8_t entry, const sim::RouteEntry& value) {
        sim::RouteEdit o;
        o.player = c.local;
        o.ship = ship.id;
        o.op = sim::RouteEdit::Op::SetEntry;
        o.stop = stop;
        o.entry = entry;
        o.value = value;
        return o;
    }
    [[nodiscard]] static const sim::City* stop_city(const sim::Ship& ship, const PanelContext& c, std::size_t stop) {
        const sim::Target& t = ship.route.stops[stop].target;
        return t.kind == sim::Target::Kind::City ? c.world().city(t.island, t.city) : nullptr;
    }
    /// Another's city without a trade treaty: its goods can not be chosen.
    [[nodiscard]] static bool locked(const sim::City& city, const PanelContext& c) {
        return city.owner != c.local && !c.world().diplomacy().may_trade(c.local, city.owner);
    }

    /// The gadgets of one ware of a stop, in the list of stops or on the stop's page.
    struct WareGadgets {
        std::uint8_t stop = 0;
        std::uint8_t entry = 0;
        std::int32_t icon = 0;
        std::int32_t back = 0;
        std::int32_t bar = 0;
        std::int32_t frame = 0;
        std::int32_t slider = 0;
        std::int32_t arrow = 0;
    };
    static_assert(kStopsShown * kEntriesShown == sim::RouteStop::kEntries, "the list and a stop's page show as many");
    using Wares = std::array<WareGadgets, sim::RouteStop::kEntries>;

    /// Places of stops in the editor: the four of the list, or the one stop of its page.
    [[nodiscard]] std::size_t slots() const { return m_stop_page ? 1 : kStopsShown; }
    /// The stop shown in place `slot` of the editor.
    [[nodiscard]] std::uint8_t stop_at(std::size_t slot) const {
        if (m_stop_page) {
            return *m_stop_page;
        }
        return static_cast<std::uint8_t>(m_route_page * kStopsShown + slot);
    }
    /// The list's wares (two of each stop) or those of the stop on its page (all of them).
    [[nodiscard]] static Wares list_wares(std::size_t page) {
        Wares out{};
        for (std::size_t slot = 0; slot < kStopsShown; ++slot) {
            const auto s = static_cast<std::int32_t>(slot);
            for (std::size_t e = 0; e < kEntriesShown; ++e) {
                out[slot * kEntriesShown + e] = {static_cast<std::uint8_t>(page * kStopsShown + slot),
                                                 static_cast<std::uint8_t>(e),
                                                 kEntryIcon[e] + s,
                                                 kEntryBack[e] + s,
                                                 kEntryBar[e] + s,
                                                 kEntryFrame[e] + s,
                                                 kEntrySlider[e] + s,
                                                 kEntryArrow[e] + s};
            }
        }
        return out;
    }
    [[nodiscard]] static Wares page_wares(std::uint8_t stop) {
        Wares out{};
        for (std::size_t k = 0; k < out.size(); ++k) {
            const auto w = static_cast<std::int32_t>(k);
            out[k] = {stop,
                      static_cast<std::uint8_t>(k),
                      kWareIcon + w,
                      kWareBack + w,
                      kWareBar + w,
                      kWareFrame + w,
                      kWareSlider + w,
                      kWareArrow + w};
        }
        return out;
    }
    [[nodiscard]] Wares wares_shown() const {
        return m_stop_page ? page_wares(*m_stop_page) : list_wares(m_route_page);
    }
    /// A ware the ship has no hold for (stored for larger ships, e.g. from a copied route).
    [[nodiscard]] static bool spare(const WareGadgets& w, const sim::Ship& ship, const PanelContext& c) {
        return w.entry >= sim::usable_route_entries(ship, c.figures);
    }
    /// A ware of the stop's page that is drawn: one per hold, and the spare ones that are set.
    [[nodiscard]] bool shown(const WareGadgets& w, const sim::Ship& ship, const PanelContext& c) const {
        return !m_stop_page || !spare(w, ship, c) || ship.route.stops[w.stop].entries[w.entry] != sim::RouteEntry{};
    }
    /// Wares of a stop beyond the two of the list.
    [[nodiscard]] static std::size_t wares_beyond_list(const sim::RouteStop& stop) {
        return static_cast<std::size_t>(std::count_if(stop.entries.begin() + kEntriesShown, stop.entries.end(),
                                                      [](const sim::RouteEntry& r) { return r.ware != 0; }));
    }
    /// `+N` on a stop of the list: the wares beyond the two shown, or `+` once both are set (a free
    /// one is chosen in the list itself).
    [[nodiscard]] static bool more_shown(const sim::RouteStop& stop) {
        const bool list_full = std::all_of(stop.entries.begin(), stop.entries.begin() + kEntriesShown,
                                           [](const sim::RouteEntry& r) { return r.ware != 0; });
        return list_full || wares_beyond_list(stop) > 0;
    }
    /// The next stop with a city before (`step` -1) or after (+1) `from`.
    [[nodiscard]] static std::optional<std::uint8_t> neighbour_stop(const sim::Ship& ship, const PanelContext& c,
                                                                    std::uint8_t from, int step) {
        for (int k = from + step; k >= 0 && k < static_cast<int>(sim::TradeRoute::kStops); k += step) {
            if (stop_city(ship, c, static_cast<std::size_t>(k)) != nullptr) {
                return static_cast<std::uint8_t>(k);
            }
        }
        return std::nullopt;
    }
    /// Pages of the editor: two with Open1602's row, the original's one without it.
    [[nodiscard]] std::size_t route_pages() const {
        return m_route_row && !m_is_cart ? sim::TradeRoute::kStops / kStopsShown : 1;  // a cart has four stops
    }
    /// Another own ship with a route of cities by the tile clicked (ships stand out of their tile).
    /// Whether the route has a city to copy (patrol points and ships to meet are not copied).
    [[nodiscard]] static bool has_city_stop(const sim::Ship& ship) {
        return std::ranges::any_of(ship.route.stops,
                                   [](const sim::RouteStop& st) { return st.target.kind == sim::Target::Kind::City; });
    }

    /// The own ship (or cart, for a cart) nearest to the clicked tile that takes the copied route.
    [[nodiscard]] static const sim::Ship* route_target_near(const sim::Ship& ship, const PanelContext& c,
                                                            std::uint32_t x, std::uint32_t y) {
        constexpr std::int32_t kReach = 2;
        const sim::Ship* best = nullptr;
        std::int32_t best_distance = kReach + 1;
        for (const sim::Ship& other : c.world().ships()) {
            if (other.id == ship.id || other.owner != ship.owner || other.sinking || other.cart() != ship.cart()) {
                continue;
            }
            const std::int32_t d =
                std::max(std::abs(static_cast<std::int32_t>(other.x) - static_cast<std::int32_t>(x)),
                         std::abs(static_cast<std::int32_t>(other.y) - static_cast<std::int32_t>(y)));
            if (d < best_distance) {
                best = &other;
                best_distance = d;
            }
        }
        return best;
    }

    void refresh_route(const sim::Ship& ship, const PanelContext& c) {
        const auto cap = static_cast<std::int32_t>(sim::hold_capacity(ship));
        m_route_page = std::min(m_route_page, route_pages() - 1);
        if (m_stop_page && (!m_route_row || stop_city(ship, c, *m_stop_page) == nullptr)) {
            m_stop_page.reset();  // the stop was cleared
        }
        // The list's stops and wares, or the first stop's board and the wares of the stop's page.
        for (std::size_t slot = 0; slot < kStopsShown; ++slot) {
            const auto s = static_cast<std::int32_t>(slot);
            for (const std::int32_t id : {kStopBoard, kStopName, kStopSymbol}) {
                m_route.set_visible(id + s, slot < slots());
            }
            for (std::size_t e = 0; e < kEntriesShown; ++e) {
                for (const std::int32_t id :
                     {kEntryIcon[e], kEntryBack[e], kEntryBar[e], kEntryFrame[e], kEntrySlider[e], kEntryArrow[e]}) {
                    m_route.set_visible(id + s, !m_stop_page);
                }
            }
        }
        for (std::size_t slot = 0; slot < kStopsShown; ++slot) {
            const auto s = static_cast<std::int32_t>(slot);
            const std::uint8_t stop = stop_at(slot);
            const sim::City* city = slot < slots() ? stop_city(ship, c, stop) : nullptr;
            m_route.set_visible(kStopArms + s, city != nullptr);
            m_route.set_visible(kStopArmsShadow + s, city != nullptr);
            if (m_route_row) {
                for (const std::int32_t id : {kStopMoreRim, kStopMorePlate, kStopMore}) {
                    m_route.set_visible(id + s, city != nullptr && !m_stop_page && more_shown(ship.route.stops[stop]));
                }
            }
            if (slot >= slots()) {
                continue;
            }
            std::string name = city != nullptr ? city_name(*city) : c.text("GAME", 46);
            if (m_route_row) {
                name.insert(0, std::to_string(stop + 1) + ". ");  // with eight stops their order shows
            }
            m_route.set_text(kStopName + s, std::move(name));
            m_route.set_selected(kStopName + s, m_pick_stop == stop);
            m_route.set_graphic(kStopSymbol + s, m_gfx_stop + (city != nullptr ? 0 : 1), 0);
            if (city != nullptr) {
                set_banner_graphic(m_route, kStopArms + s, render::BannerArt::at(m_gfx_arms), c.banner(city->owner));
                set_banner_graphic(m_route, kStopArmsShadow + s, render::BannerArt::at(m_gfx_arms),
                                   c.banner(city->owner));
            }
            if (m_route_row && !m_stop_page) {
                const std::size_t more = wares_beyond_list(ship.route.stops[stop]);
                m_route.set_text(kStopMore + s, more > 0 ? "+" + std::to_string(more) : std::string("+"));
            }
        }
        if (m_route_row) {
            for (const WareGadgets& w : page_wares(0)) {
                for (const std::int32_t id : {w.icon, w.back, w.bar, w.frame, w.slider, w.arrow}) {
                    m_route.set_visible(id, false);
                }
            }
        }
        for (const WareGadgets& w : wares_shown()) {
            if (!shown(w, ship, c)) {
                continue;
            }
            const sim::City* city = stop_city(ship, c, w.stop);
            const bool no_hold = spare(w, ship, c);
            const bool lock = city == nullptr || locked(*city, c) || no_hold;
            const sim::RouteEntry& entry = ship.route.stops[w.stop].entries[w.entry];
            const bool off = city == nullptr || entry.ware == 0 || lock;
            for (const std::int32_t id : {w.icon, w.back, w.frame, w.slider, w.arrow}) {
                m_route.set_visible(id, true);
            }
            m_route.set_graphic(w.icon,
                                city != nullptr && entry.ware != 0
                                    ? ui::ware_sprite(m_gfx_ware, c.world().goods(), entry.ware)
                                    : m_gfx_empty,
                                0);
            m_route.set_disabled(w.icon, no_hold);
            m_route.set_visible(w.bar, !off);
            m_route.set_disabled(w.slider, off);
            m_route.set_disabled(w.frame, lock);
            // The direction is fixed at another's city by what it buys and sells.
            m_route.set_disabled(w.arrow, off || city->owner != c.local);
            m_route.set_graphic(w.arrow, m_gfx_arrow + (entry.unload ? 2 : 0), 0);
            const auto amount = static_cast<std::int32_t>(std::min(entry.amount, sim::hold_capacity(ship)));
            m_route.set_slider(w.bar, 1, off ? 0 : amount, cap);
            if (m_route.held() != w.slider) {
                m_route.set_slider(w.slider, 1, off ? 0 : amount, cap);
            }
        }
        if (m_route_row) {
            if (m_stop_page) {
                m_route.set_text(kRouteCopy, ui::tr("route.back"));
                m_route.set_disabled(kRouteCopy, false);
                m_route.set_disabled(kRouteUp, !neighbour_stop(ship, c, *m_stop_page, -1));
                m_route.set_disabled(kRouteDown, !neighbour_stop(ship, c, *m_stop_page, 1));
                m_route.set_selected(kRouteCopy, false);
            } else {
                m_route.set_text(kRouteCopy, ui::tr("route.copy"));
                m_route.set_disabled(kRouteUp, m_route_page == 0);
                m_route.set_disabled(kRouteDown, m_route_page + 1 >= route_pages());
                const bool can_copy = has_city_stop(ship);
                if (!can_copy) {
                    m_pick_copy = false;
                }
                m_route.set_disabled(kRouteCopy, !can_copy);
                m_route.set_selected(kRouteCopy, m_pick_copy);
            }
        }
    }

    void clicked_route(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                       std::vector<sim::Command>& out) {
        if (e.kind == ui::PanelEvent::Kind::Clicked && m_route_row) {
            switch (e.id) {
                case kRouteShip:
                    // Back to the ship's page, where the route is started.
                    m_page = Page::Root;
                    m_stop_page.reset();
                    m_pick_stop.reset();
                    m_pick_copy = false;
                    m_cart_chain = false;
                    return;
                case kRouteUp:
                case kRouteDown:
                    if (m_stop_page) {
                        // The previous or next stop's page.
                        m_stop_page =
                            neighbour_stop(ship, c, *m_stop_page, e.id == kRouteUp ? -1 : 1).value_or(*m_stop_page);
                        m_pick_stop.reset();
                    } else if (e.id == kRouteUp && m_route_page > 0) {
                        --m_route_page;
                    } else if (e.id == kRouteDown && m_route_page + 1 < route_pages()) {
                        ++m_route_page;
                    }
                    return;
                case kRouteCopy:
                    if (m_stop_page) {
                        // Back to the list, on the page with this stop.
                        m_route_page = *m_stop_page / kStopsShown;
                        m_stop_page.reset();
                        m_pick_stop.reset();
                        return;
                    }
                    // Waits for a click on another own ship on the map, which takes this ship's stops.
                    m_pick_copy = !m_pick_copy && has_city_stop(ship);
                    m_pick_stop.reset();
                    return;
                default:
                    break;
            }
            if (!m_stop_page && e.id >= kStopMore && e.id < kStopMore + static_cast<std::int32_t>(kStopsShown)) {
                const std::uint8_t stop = stop_at(static_cast<std::size_t>(e.id - kStopMore));
                if (stop_city(ship, c, stop) != nullptr) {
                    m_stop_page = stop;
                    m_pick_stop.reset();
                    m_pick_copy = false;
                }
                return;
            }
        }
        for (const WareGadgets& w : wares_shown()) {
            const sim::City* city = stop_city(ship, c, w.stop);
            if (city == nullptr || locked(*city, c) || spare(w, ship, c)) {
                continue;
            }
            const sim::RouteEntry& entry = ship.route.stops[w.stop].entries[w.entry];
            if (e.kind == ui::PanelEvent::Kind::Slid && e.id == w.slider && entry.ware != 0) {
                // Whole 1/32 t like the original (no rounding to tons).
                sim::RouteEntry v = entry;
                v.amount = static_cast<std::uint32_t>(
                    m_route.slider(e.id, 1, static_cast<std::int32_t>(sim::hold_capacity(ship))));
                if (v.amount != entry.amount) {
                    out.emplace_back(route_entry(ship, c, w.stop, w.entry, v));
                }
                return;
            }
            if (e.kind != ui::PanelEvent::Kind::Clicked) {
                continue;
            }
            if (e.id == w.frame) {
                m_pick_entry_stop = w.stop;
                m_pick_entry = w.entry;
                m_pick_stop.reset();
                m_pick_copy = false;
                m_page = city->owner == c.local ? Page::PickOwn : Page::PickForeign;
                return;
            }
            if (e.id == w.arrow && city->owner == c.local && entry.ware != 0) {
                sim::RouteEntry v = entry;
                v.unload = !v.unload;
                out.emplace_back(route_entry(ship, c, w.stop, w.entry, v));
                return;
            }
        }
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        for (std::size_t slot = 0; slot < slots(); ++slot) {
            const std::uint8_t stop = stop_at(slot);
            const auto s = static_cast<std::int32_t>(slot);
            const sim::City* city = stop_city(ship, c, stop);
            if (e.id == kStopName + s) {
                m_pick_copy = false;
                if (m_pick_stop && *m_pick_stop != stop && city != nullptr) {
                    // While choosing: another stop's name copies its city.
                    out.emplace_back(route_stop(ship, c, *m_pick_stop, ship.route.stops[stop].target));
                    m_pick_stop.reset();
                } else {
                    m_pick_stop = stop;
                }
                return;
            }
            if (e.id == kStopSymbol + s) {
                if (city == nullptr) {
                    m_pick_copy = false;
                    m_pick_stop = stop;
                } else {
                    out.emplace_back(route_stop(ship, c, stop, sim::Target{}));
                }
                return;
            }
        }
    }

    [[nodiscard]] std::optional<std::string> route_help(std::int32_t id, const sim::Ship& ship,
                                                        const PanelContext& c) const {
        if (m_route_row) {
            const auto range = [](std::size_t page) {
                return std::to_string(page * kStopsShown + 1) + "-" + std::to_string((page + 1) * kStopsShown);
            };
            const auto stop_line = [&](int step) -> std::optional<std::string> {
                if (const auto k = neighbour_stop(ship, c, *m_stop_page, step)) {
                    return ui::tr("route.show_stop", {std::to_string(*k + 1)});
                }
                return std::nullopt;
            };
            switch (id) {
                case kRouteUp:
                    return m_stop_page ? stop_line(-1)
                                       : ui::tr("route.show_stops", {range(m_route_page > 0 ? m_route_page - 1 : 0)});
                case kRouteDown:
                    return m_stop_page
                               ? stop_line(1)
                               : ui::tr("route.show_stops", {range(std::min(m_route_page + 1, route_pages() - 1))});
                case kRouteCopy:
                    return ui::tr(m_stop_page ? "route.back" : "route.copy_help");
                case kRouteShip:
                    return ui::tr("route.back_to_ship");
                default:
                    break;
            }
            if (!m_stop_page && id >= kStopMore && id < kStopMore + static_cast<std::int32_t>(kStopsShown)) {
                return ui::tr("route.stop_wares", {std::to_string(sim::usable_route_entries(ship, c.figures))});
            }
        }
        for (std::size_t slot = 0; slot < slots(); ++slot) {
            const std::uint8_t stop = stop_at(slot);
            const auto s = static_cast<std::int32_t>(slot);
            const sim::City* city = stop_city(ship, c, stop);
            if (id == kStopName + s || id == kStopSymbol + s) {
                std::string line = help_line(c, id == kStopSymbol + s && city != nullptr ? 171 : 170);
                // Open1602: which stop the ship sails to (or lies at) now.
                if (city != nullptr && ship.route.active && ship.route.current == stop) {
                    return ui::tr("route.current_stop", {line});
                }
                return line;
            }
        }
        for (const WareGadgets& w : wares_shown()) {
            if (id != w.frame && id != w.arrow && id != w.slider && id != w.icon) {
                continue;
            }
            const sim::City* city = stop_city(ship, c, w.stop);
            const sim::RouteEntry& entry = ship.route.stops[w.stop].entries[w.entry];
            if (city == nullptr || !shown(w, ship, c)) {
                return std::nullopt;
            }
            if (spare(w, ship, c)) {
                return ui::tr("route.no_hold");
            }
            if (entry.ware == 0) {
                return std::nullopt;
            }
            // Own city load/unload, another's buy/sell.
            const bool own = city->owner == c.local;
            const std::size_t line = own ? (entry.unload ? 53 : 51) : (entry.unload ? 55 : 54);
            return ui::format_text(c.text("GAME", line),
                                   {std::int64_t{entry.amount >> 5U}, std::string_view(c.text("WARE", entry.ware))});
        }
        return std::nullopt;
    }

    void refresh_picker(const sim::Ship& ship, const PanelContext& c) {
        const sim::City* city = stop_city(ship, c, m_pick_entry_stop);
        if (city == nullptr || locked(*city, c)) {
            m_page = Page::Route;
            return;
        }
        if (m_page == Page::PickOwn) {
            m_stock.refresh(*city, c.world().goods(), true);
        } else {
            m_lists.refresh(*city, c);
        }
    }

    void clicked_picker(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                        std::vector<sim::Command>& out) {
        const sim::City* city = stop_city(ship, c, m_pick_entry_stop);
        if (e.kind != ui::PanelEvent::Kind::Clicked || city == nullptr) {
            return;
        }
        sim::RouteEntry v = ship.route.stops[m_pick_entry_stop].entries[m_pick_entry];
        if (m_page == Page::PickOwn) {
            const std::optional<std::uint8_t> w = m_stock.ware_of(e.id);
            if (!w) {
                return;  // the original ignores clicks beside the list
            }
            // 50 t; delivered if the own Kontor buys the ware, taken if it sells it.
            v.ware = *w;
            v.amount = *w != 0 ? sim::hold_capacity(ship) : 0;
            if (const sim::City::Trade* t = city->trade_of(*w); t != nullptr && *w != 0) {
                v.unload = t->buy ? true : t->sell ? false : v.unload;
            }
        } else {
            const auto w = m_lists.ware_of(e.id);
            if (!w) {
                return;
            }
            // What the city buys is delivered (50 t), what it sells is taken (25 t).
            v.ware = w->first;
            v.unload = w->second;
            v.amount = w->second ? sim::hold_capacity(ship) : sim::hold_capacity(ship) / 2;
        }
        out.emplace_back(route_entry(ship, c, m_pick_entry_stop, m_pick_entry, v));
        m_page = Page::Route;
    }

    [[nodiscard]] std::optional<std::string> picker_help(std::int32_t id, const sim::Ship& ship,
                                                         const PanelContext& c) const {
        const sim::City* city = stop_city(ship, c, m_pick_entry_stop);
        if (city == nullptr) {
            return std::nullopt;
        }
        if (m_page == Page::PickForeign) {
            const sim::Player* owner = c.world().player(city->owner);
            if (owner != nullptr && c.world().behaviour(*owner) == sim::FactionBehaviour::Natives) {
                if (const auto w = m_lists.ware_of(id)) {
                    return ui::format_text(c.text("GAME", 50),
                                           {std::int64_t{TradeLists::amount(*city, w->first, w->second) >> 5U},
                                            std::string_view(c.text("WARE", w->first))});
                }
                return std::nullopt;
            }
            return m_lists.help(id, *city, c);
        }
        const std::optional<std::uint8_t> w = m_stock.ware_of(id);
        if (!w || *w == 0) {
            return std::nullopt;
        }
        const std::uint32_t t = city->stock(*w) >> 5U;
        return t >= 1 ? ui::format_text(c.text("GAME", 50), {std::int64_t{t}, std::string_view(c.text("WARE", *w))})
                      : c.text("WARE", *w);
    }

    // ---- The pirates (PIRAT, PIRATORD) ----

    [[nodiscard]] std::uint32_t pirate_cost(const PanelContext& c) const {
        const sim::City* city = m_city ? c.world().city(m_city->first, m_city->second) : nullptr;
        if (city == nullptr) {
            return 0;
        }
        if (!m_hire) {
            return sim::pirate_peace_price(c.world(), city->owner, c.local);
        }
        const auto k = static_cast<std::size_t>(m_hire_choice);
        return k < m_opponents.size() ? sim::pirate_bribe_price(c.world(), city->owner, c.local, m_opponents[k]) : 0;
    }

    void refresh_pirates(const PanelContext& c) {
        // The other human and computer players still in the game.
        m_opponents.clear();
        const auto& players = c.world().players();
        for (std::size_t p = 0; p < players.size(); ++p) {
            const sim::Player& pl = players[p];
            if (p != c.local && (pl.role == sim::PlayerRole::Human || pl.role == sim::PlayerRole::Computer) &&
                pl.status == sim::PlayerStatus::Playing) {
                m_opponents.push_back(static_cast<sim::PlayerId>(p));
            }
        }
        if (m_page == Page::Pirates) {
            // Hiring them needs at least two players beside the pirates.
            m_pirat.set_visible(kHire, m_opponents.size() + 1 >= 2);
            return;
        }
        m_piratord.set_text(kPirateText, c.text("GAME", m_hire ? 39 : 41));
        m_piratord.set_text(kPirateText + 1, c.text("GAME", m_hire ? 40 : 42));
        m_piratord.set_text(kPirateText + 2, c.text("GAME", 43));
        m_piratord.set_visible(kPirateText + 2, !m_hire);
        const auto opponents = static_cast<std::int32_t>(m_opponents.size());
        if (m_hire_choice >= opponents) {
            m_hire_choice = 0;
        }
        // Three at a time; the arrows page through the others (LC-4).
        m_hire_first = std::clamp(m_hire_first, 0, std::max(0, opponents - kPirateChoices));
        for (std::int32_t k = 0; k < kPirateChoices; ++k) {
            const std::int32_t o = m_hire_first + k;
            const bool shown = m_hire && o < opponents;
            m_piratord.set_visible(kPirateArms + k, shown);
            m_piratord.set_visible(kPirateChoice + k, shown);
            m_piratord.set_selected(kPirateChoice + k, o == m_hire_choice);
            if (shown) {
                set_banner_graphic(m_piratord, kPirateArms + k, render::BannerArt::at(m_gfx_pirate_arms),
                                   c.banner(m_opponents[static_cast<std::size_t>(o)]));
            }
        }
        const bool scrolls = m_hire && opponents > kPirateChoices;
        m_piratord.set_visible(kPirateLeft, scrolls);
        m_piratord.set_visible(kPirateRight, scrolls);
        m_piratord.set_disabled(kPirateLeft, m_hire_first == 0);
        m_piratord.set_disabled(kPirateRight, m_hire_first + kPirateChoices >= opponents);
        m_piratord.set_text(kCostLabel, c.text("GAME", 45));
        m_piratord.set_text(kCost, std::to_string(pirate_cost(c)));
    }

    void clicked_pirates(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                         std::vector<sim::Command>& out) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        if (m_page == Page::Pirates) {
            if (e.id == kBuyPeace || e.id == kHire) {
                m_hire = e.id == kHire;
                m_page = Page::PirateOrder;
            }
            return;
        }
        if (e.id >= kPirateChoice && e.id < kPirateChoice + kPirateChoices) {
            m_hire_choice = m_hire_first + e.id - kPirateChoice;
            return;
        }
        if (e.id == kPirateLeft || e.id == kPirateRight) {
            const auto opponents = static_cast<std::int32_t>(m_opponents.size());
            m_hire_first =
                std::clamp(m_hire_first + (e.id == kPirateLeft ? -1 : 1), 0, std::max(0, opponents - kPirateChoices));
            // The choice stays in view: paying is never for a target the player cannot see.
            m_hire_choice = std::clamp(m_hire_choice, m_hire_first, m_hire_first + kPirateChoices - 1);
            return;
        }
        const sim::City* city = m_city ? c.world().city(m_city->first, m_city->second) : nullptr;
        const sim::Player* me = c.world().player(c.local);
        if (e.id != kPay || city == nullptr || me == nullptr) {
            return;
        }
        const std::uint32_t cost = pirate_cost(c);
        if (std::int64_t{cost} > me->money) {
            return;  // the original does nothing without the money
        }
        sim::DiplomacyOrder o;
        o.player = c.local;
        o.partner = city->owner;
        o.ship = ship.id;
        o.kind = m_hire ? sim::DiplomacyOrder::Kind::Bribe : sim::DiplomacyOrder::Kind::BuyPeace;
        if (m_hire) {
            const auto k = static_cast<std::size_t>(m_hire_choice);
            if (k >= m_opponents.size()) {
                return;
            }
            o.target = m_opponents[k];
        }
        out.emplace_back(o);
        m_page = Page::Root;
    }

    ui::Panel m_ship;
    ui::Panel m_transfer;
    ui::Panel m_trade;
    ui::Panel m_route;
    ui::Panel m_pirat;
    ui::Panel m_piratord;
    StockList m_stock;
    TradeLists m_lists;
    std::int32_t m_gfx_stop = 612;
    std::int32_t m_gfx_arms = 548;
    std::int32_t m_gfx_arrow = 605;
    std::int32_t m_gfx_pirate_arms = 548;
    std::optional<std::uint8_t> m_pick_stop;  ///< the stop whose city a click on the map chooses
    bool m_pick_copy = false;                 ///< a click on the map chooses the ship that takes the route
    bool m_route_row = false;                 ///< the editor has Open1602's row (copy, pages)
    std::size_t m_route_page = 0;             ///< stops 1-4 or 5-8 in the editor
    std::optional<std::uint8_t> m_stop_page;  ///< the stop whose page of wares the editor shows
    std::uint8_t m_pick_entry_stop = 0;       ///< the entry whose ware a picker chooses
    std::uint8_t m_pick_entry = 0;
    bool m_hire = false;             ///< PIRATORD: hire them (or buy peace)
    std::int32_t m_hire_choice = 0;  ///< whom, among the other players
    std::int32_t m_hire_first = 0;   ///< the first of them shown
    std::vector<sim::PlayerId> m_opponents;
    std::int32_t m_gfx_ware = 192;
    std::int32_t m_gfx_empty = 192;
    std::int32_t m_gfx_ship = 363;
    static constexpr std::int32_t kNameFrame = 38001;
    // TRADER.GAD
    static constexpr std::int32_t kCartName = 59002;
    static constexpr std::int32_t kCartTrade = 59003;
    static constexpr std::int32_t kCartTransfer = 59004;
    static constexpr std::int32_t kCartRouteEditor = 59005;
    static constexpr std::int32_t kCartRoute = 59006;
    static constexpr std::int32_t kCartStop = 59007;
    static constexpr std::int32_t kCartHoldIcon = 59051;
    static constexpr std::int32_t kCartHoldBar = 59071;
    static constexpr std::int32_t kCartHoldButton = 59091;
    std::optional<ui::Panel> m_cart;  ///< TRADER.GAD, the trade cart's window
    bool m_is_cart = false;           ///< the selected ship is a trade cart
    bool m_cart_chain = false;        ///< picking a cart's stops one after another on the map
    /// The left column: repair, pirates, explore and its ring (arrange_column).
    static constexpr std::array<std::int32_t, 4> kColumn = {38003, 38019, 38023, 38024};
    std::array<std::array<std::int32_t, 2>, 4> m_column{};
    std::int32_t m_name_bottom = 384;
    Page m_page = Page::Root;
    SelectionPages m_pages;
    std::int32_t m_hold = 0;
    std::int32_t m_offer = 0;
    std::array<std::int32_t, 4> m_scroll{};
    std::vector<std::uint8_t> m_offers;
    std::optional<std::pair<std::uint16_t, std::uint8_t>> m_city;
    std::optional<Request> m_request;
};

}  // namespace

std::unique_ptr<SelectionPanel> make_ship_panel(ShipLayouts layouts) {
    return std::make_unique<ShipPanel>(std::move(layouts));
}

}  // namespace o1602::app
