// SPDX-License-Identifier: GPL-3.0-or-later
#include "panel_common.hpp"

#include <algorithm>
#include <string_view>

#include "goods_window.hpp"

#include "o1602/sim/ships.hpp"
#include "o1602/ui/font.hpp"
#include "o1602/ui/package_icons.hpp"
#include "o1602/ui/text_format.hpp"

namespace o1602::app::panel_detail {

namespace {
// LAGER.GAD: + ware number
constexpr std::int32_t kLTitle = 44002;
constexpr std::int32_t kLIcon = 44019;
constexpr std::int32_t kLBar = 44049;
constexpr std::int32_t kLFrame = 44079;
constexpr std::int32_t kLMarker = 44109;
constexpr std::int32_t kLCorner = 44139;
constexpr std::int32_t kLDeadSlider = 44171;
// Open1602's own: the goods trend under each icon, in green and in red, on a dark ground.
constexpr std::int32_t kLTrendUp = 44301;
constexpr std::int32_t kLTrendDown = 44331;
constexpr std::int32_t kLTrendGround = 44361;
/// Open1602's own: the button that turns to the next page of goods, at the title's right end.
constexpr std::int32_t kLPage = 44391;

/// LAGER.GAD with small text at the foot of every good's icon, left of its bar, in green or in
/// red, on a half dark ground so that it reads over the picture.
assets::GadgetLayout with_trend(assets::GadgetLayout layout) {
    for (std::uint8_t w = kFirstTradeWare; w <= kLastTradeWare; ++w) {
        const assets::Gadget* found = layout.find(kLIcon + w);
        if (found == nullptr) {
            continue;
        }
        const assets::Gadget icon = *found;  // copied: the gadgets grow below
        assets::Gadget ground;
        ground.id = kLTrendGround + w;
        ground.kind = assets::GadgetKind::Fill;
        ground.no_select = true;
        ground.half = true;
        ground.colour = {0, 0};  // black
        ground.pos = {icon.pos[0] + 2, icon.pos[1] + icon.size[1] - 15};
        ground.size = {38, 13};
        layout.gadgets.push_back(ground);
        assets::Gadget g;
        g.kind = assets::GadgetKind::TextLeft;
        g.no_select = true;
        g.block = 1;  // ZEILITTLE
        g.pos = icon.pos;
        g.size = {40, icon.size[1]};
        g.offset = {4, static_cast<std::int16_t>(icon.size[1] - 3)};
        g.id = kLTrendUp + w;
        g.colour = {ui::kTextBrightGreen, ui::kTextBrightGreen};
        layout.gadgets.push_back(g);
        g.id = kLTrendDown + w;
        g.colour = {ui::kTextBrightRed, ui::kTextBrightRed};
        layout.gadgets.push_back(g);
    }
    if (const assets::Gadget* title = layout.find(kLTitle)) {
        assets::Gadget page = *title;
        page.id = kLPage;
        page.kind = assets::GadgetKind::TextRight;
        page.no_select = false;
        page.menu = true;
        page.offset = {static_cast<std::int16_t>(title->size[0] - 4), title->offset[1]};
        layout.gadgets.push_back(page);
    }
    return layout;
}
}  // namespace

StockList::StockList(assets::GadgetLayout layout) : m_panel(with_trend(std::move(layout)), 1.0) {
    const assets::GadgetLayout& l = m_panel.layout();
    m_gfx_ware = l.constant("GFXWARE").value_or(192);
    m_gfx_empty = l.constant("GFXWARELEER").value_or(188) + kEmptyHold;
    if (const assets::Gadget* g = l.find(kLCorner + 2)) {
        m_corner = g->gfx;
    }
    m_panel.set_visible(kLDeadSlider, false);
    m_panel.set_visible(kLPage, false);
    m_panel.set_visible(kLMarker + kLastTradeWare + 1, false);  // marker and corner of the 24th place:
    m_panel.set_visible(kLCorner + kLastTradeWare + 1, false);  // never shown
}

std::string StockList::trend_text(const sim::City& city, std::uint8_t ware) {
    const sim::FlowTotals t = city.ledger.totals(ware, kTrendMinutes);
    const std::string text = GoodsWindow::rate_text(t.net(), t.minutes, true);
    return text == "0" || text == "-" ? std::string() : text;
}

void StockList::refresh(const sim::City& city, const assets::Goods& goods, bool picker, bool trend) {
    m_panel.set_text(kLTitle, city_name(city));
    // The original's goods on the first page, the packages' in the original's order of places after.
    const std::vector<std::uint8_t>& all = goods.trade_wares();
    const std::size_t own = all.size() > kPlaces ? all.size() - kPlaces : 0;
    m_pages = 1 + (own + kPlaces - 1) / kPlaces;
    m_page = std::min(m_page, m_pages - 1);
    m_place_ware.fill(0);
    for (std::size_t k = 0; k < kPlaces; ++k) {
        const std::size_t i = m_page == 0 ? k : kPlaces + (m_page - 1) * kPlaces + k;
        m_place_ware[kFirstTradeWare + k] = i < all.size() ? all[i] : std::uint8_t{0};
    }
    m_panel.set_visible(kLPage, m_pages > 1);
    if (m_pages > 1) {
        m_panel.set_text(kLPage, std::to_string(m_page + 1) + "/" + std::to_string(m_pages) + " >");
    }
    const auto cap = static_cast<std::int32_t>(std::max<std::uint32_t>(city.capacity, 1));
    for (std::uint8_t place = kFirstTradeWare; place <= kLastTradeWare; ++place) {
        const std::uint8_t w = m_place_ware[place];
        const std::string rate = w != 0 && trend && !picker ? trend_text(city, w) : std::string();
        const bool up = !rate.empty() && rate.front() == '+';
        m_panel.set_visible(kLTrendGround + place, !rate.empty());
        m_panel.set_visible(kLTrendUp + place, up);
        m_panel.set_visible(kLTrendDown + place, !rate.empty() && !up);
        m_panel.set_text(up ? kLTrendUp + place : kLTrendDown + place, rate);
        for (const std::int32_t id : {kLIcon, kLFrame, kLBar}) {
            m_panel.set_visible(id + place, w != 0);
        }
        if (w == 0) {
            m_panel.set_visible(kLMarker + place, false);
            m_panel.set_visible(kLCorner + place, false);
            continue;
        }
        const sim::City::Trade* t = city.trade_of(w);
        m_panel.set_graphic(kLIcon + place, ui::ware_sprite(m_gfx_ware, goods, w), 0);
        m_panel.set_slider(kLBar + place, 1, static_cast<std::int32_t>(city.stock(w)), cap);
        const bool buy = t != nullptr && t->buy;
        const bool sell = t != nullptr && t->sell;
        m_panel.set_visible(kLMarker + place, buy || sell);
        m_panel.set_visible(kLCorner + place, buy || sell);
        if (buy || sell) {
            m_panel.set_slider(kLMarker + place, 1, static_cast<std::int32_t>(buy ? t->limit : t->keep), cap);
            // Price level 0 (cheapest) .. 4 (base price).
            const std::int32_t base = goods.price(w);
            const auto min = static_cast<std::int32_t>(static_cast<std::uint32_t>(base) * 300U >> 10U);
            const std::int32_t price = buy ? t->buy_price : t->sell_price;
            const std::int32_t level = std::clamp(((price - min) * 5 - 1) / std::max(1, base - min), 0, 4);
            m_panel.set_graphic(kLCorner + place, m_corner + (buy ? 0 : 5) + level, 0);
        }
    }
    // The 24th place: the empty frame that closes the picker.
    const std::int32_t last = kLastTradeWare + 1;
    m_panel.set_visible(kLIcon + last, picker);
    m_panel.set_visible(kLFrame + last, picker);
    // It holds no good: no stock bar, no trade marks (they showed as a red stroke in the empty place).
    for (const std::int32_t id : {kLBar, kLMarker, kLCorner}) {
        m_panel.set_visible(id + last, false);
    }
    if (picker) {
        m_panel.set_graphic(kLIcon + last, m_gfx_empty, 0);
    }
}

std::optional<std::uint8_t> StockList::ware_of(std::int32_t id) const {
    const std::int32_t place = id - kLFrame;
    if (place >= kFirstTradeWare && place <= kLastTradeWare) {
        const std::uint8_t w = m_place_ware[static_cast<std::size_t>(place)];
        return w != 0 ? std::optional(w) : std::nullopt;
    }
    if (place == kLastTradeWare + 1) {
        return std::uint8_t{0};
    }
    return std::nullopt;
}

bool StockList::turn_page(std::int32_t id) {
    if (id != kLPage || m_pages <= 1) {
        return false;
    }
    m_page = (m_page + 1) % m_pages;
    return true;
}

TradeLists::TradeLists(assets::GadgetLayout layout) : m_panel(std::move(layout), 1.0) {
    m_gfx_ware = m_panel.layout().constant("GFXWARE").value_or(192);
    m_gfx_empty = m_panel.layout().constant("GFXWARELEER").value_or(188) + kEmptyHold;
}

void TradeLists::refresh(const sim::City& city, const PanelContext& c) {
    m_panel.set_text(53002, city_name(city));
    m_panel.set_text(53003, c.text("GAME", 36));
    m_panel.set_text(53004, c.text("GAME", 35));
    const assets::BuildingType* k = sim::kontor_of(c.world(), c.buildings, city);
    fill(city, c.world().goods(), true, 53011, 53031, kDemandFrame, 11, k != nullptr ? k->max_wares2 : 0);
    fill(city, c.world().goods(), false, 53071, 53081, kOfferFrame, 8, k != nullptr ? k->max_wares : 0);
}

std::optional<std::pair<std::uint8_t, bool>> TradeLists::ware_of(std::int32_t id) const {
    if (id >= kDemandFrame && id < kDemandFrame + 11) {
        const auto k = static_cast<std::size_t>(id - kDemandFrame);
        return k < m_buy.size() ? std::optional(std::make_pair(m_buy[k], true)) : std::nullopt;
    }
    if (id >= kOfferFrame && id < kOfferFrame + 8) {
        const auto k = static_cast<std::size_t>(id - kOfferFrame);
        return k < m_sell.size() ? std::optional(std::make_pair(m_sell[k], false)) : std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> TradeLists::help(std::int32_t id, const sim::City& city, const PanelContext& c) const {
    const auto w = ware_of(id);
    if (!w) {
        return std::nullopt;
    }
    const auto [ware, buy] = *w;
    const sim::City::Trade* t = city.trade_of(ware);
    const std::int64_t price = t == nullptr ? 0 : (buy ? t->buy_price : t->sell_price);
    return ui::format_text(c.text("GAME", 58), {std::int64_t{amount(city, ware, buy) >> 5U},
                                                std::string_view(c.text("WARE", ware)), price});
}

std::uint32_t TradeLists::amount(const sim::City& city, std::uint8_t w, bool buy) {
    const sim::City::Trade* t = city.trade_of(w);
    if (t == nullptr) {
        return 0;
    }
    const std::int64_t stock = city.stock(w);
    const std::int64_t reserved = city.reserved_of(w);
    const std::int64_t n = buy ? std::int64_t{t->limit} - stock + reserved : stock - std::int64_t{t->keep} - reserved;
    return n < 32 ? 0 : static_cast<std::uint32_t>(n);
}

void TradeLists::fill(const sim::City& city, const assets::Goods& goods, bool buy, std::int32_t icon, std::int32_t bar,
                      std::int32_t frame, std::int32_t gadgets, std::int32_t places) {
    auto& list = buy ? m_buy : m_sell;
    list.clear();
    for (const std::uint8_t w : goods.trade_wares()) {
        const sim::City::Trade* t = city.trade_of(w);
        if (t != nullptr && (buy ? t->buy : t->sell)) {
            list.push_back(w);
        }
    }
    const auto cap = static_cast<std::int32_t>(std::max<std::uint32_t>(city.capacity, 1));
    for (std::int32_t k = 0; k < gadgets; ++k) {
        const bool ware = k < static_cast<std::int32_t>(list.size());
        const bool place = ware || k < places;
        m_panel.set_visible(icon + k, place);
        m_panel.set_visible(frame + k, place);
        m_panel.set_visible(bar + k, ware);
        if (ware) {
            const std::uint8_t w = list[static_cast<std::size_t>(k)];
            m_panel.set_graphic(icon + k, ui::ware_sprite(m_gfx_ware, goods, w), 0);
            m_panel.set_slider(bar + k, 1,
                               static_cast<std::int32_t>(std::min<std::uint32_t>(amount(city, w, buy), city.capacity)),
                               cap);
        } else if (place) {
            m_panel.set_graphic(icon + k, m_gfx_empty, 0);
        }
    }
}

}  // namespace o1602::app::panel_detail
