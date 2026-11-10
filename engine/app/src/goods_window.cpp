// SPDX-License-Identifier: GPL-3.0-or-later
#include "goods_window.hpp"

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

#include "goods_window_parts.hpp"
#include "names.hpp"

#include "o1602/assets/game_data.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/sim/goods_statistics.hpp"
#include "o1602/ui/localization.hpp"
#include "o1602/ui/package_icons.hpp"

namespace o1602::app {

namespace {

using namespace goods_parts;
using sim::GoodsFlow;

/// The table's columns: where the numbers end (right aligned) and what heads them.
struct Column {
    std::int32_t right;
    std::string_view heading;
};
constexpr std::int32_t kNameX = 54;
constexpr std::int32_t kColumnW = 66;
constexpr std::array<Column, 7> kColumns = {{
    {258, "stats.col.stock"},
    {328, "stats.col.produced"},
    {394, "stats.col.imported"},
    {464, "stats.col.consumed"},
    {530, "stats.col.exported"},
    {594, "stats.col.balance"},
    {660, "stats.col.lasts"},
}};
constexpr std::int32_t kSparkX = 668;
constexpr std::int32_t kSparkW = 52;

/// What came in from outside the production: bought, own ships, given back.
std::uint64_t imported(const sim::FlowTotals& t) {
    return t.of(GoodsFlow::Bought) + t.of(GoodsFlow::ShippedIn) + t.of(GoodsFlow::Returned);
}
/// What was used up in the cities.
std::uint64_t consumed(const sim::FlowTotals& t) {
    return t.of(GoodsFlow::Residents) + t.of(GoodsFlow::Workshops) + t.of(GoodsFlow::Military) +
           t.of(GoodsFlow::Construction);
}
/// What left the cities: sold, own ships, lost to a full warehouse.
std::uint64_t exported(const sim::FlowTotals& t) {
    return t.of(GoodsFlow::Sold) + t.of(GoodsFlow::ShippedOut) + t.of(GoodsFlow::Lost);
}

/// The flows of a good's page, with their texts.
struct FlowLine {
    GoodsFlow flow;
    std::string_view label;
};
constexpr std::array<FlowLine, 3> kInflows = {{
    {GoodsFlow::Bought, "stats.flow.bought"},
    {GoodsFlow::ShippedIn, "stats.flow.shipped_in"},
    {GoodsFlow::Returned, "stats.flow.returned"},
}};
constexpr std::array<FlowLine, 7> kOutflows = {{
    {GoodsFlow::Residents, "stats.flow.residents"},
    {GoodsFlow::Workshops, "stats.flow.workshops"},
    {GoodsFlow::Military, "stats.flow.military"},
    {GoodsFlow::Construction, "stats.flow.construction"},
    {GoodsFlow::Sold, "stats.flow.sold"},
    {GoodsFlow::ShippedOut, "stats.flow.shipped_out"},
    {GoodsFlow::Lost, "stats.flow.lost"},
}};

}  // namespace

GoodsWindow::GoodsWindow(GoodsStyle style, std::vector<std::string> ware_names, std::int32_t ware_icon,
                         BuildingNames building_names)
    : m_style(style),
      m_ware_names(std::move(ware_names)),
      m_ware_icon(ware_icon),
      m_building_names(std::move(building_names)),
      m_form(style.form) {}

std::string GoodsWindow::rate_text(std::int64_t units, std::uint32_t minutes, bool sign) {
    if (minutes == 0) {
        return "-";
    }
    // Tenths of a ton per minute, rounded half away from zero.
    const std::int64_t div = static_cast<std::int64_t>(assets::kStockUnitsPerTon) * minutes;
    const std::int64_t tenths = (std::llabs(units) * 10 + div / 2) / div;
    if (tenths == 0) {
        return "0";
    }
    std::string s = units < 0 ? "-" : (sign ? "+" : "");
    s += std::to_string(tenths / 10);
    s += ui::tr("stats.decimal_point");
    s += std::to_string(tenths % 10);
    return s;
}

std::string GoodsWindow::lasts_text(std::optional<std::uint32_t> minutes) {
    if (!minutes) {
        return {};
    }
    if (*minutes == 0) {
        return ui::tr("stats.under_minute");  // some is left, but not for a whole minute
    }
    if (*minutes < 100) {
        return ui::tr("stats.minutes", {std::to_string(*minutes)});
    }
    return ui::tr("stats.hours", {std::to_string(*minutes / 60)});
}

std::string GoodsWindow::ware_name(const sim::World& world, std::uint8_t ware) const {
    if (const assets::OwnGood* own = world.goods().own(ware)) {
        return own->name(ui::current_language());
    }
    return ware < m_ware_names.size() ? m_ware_names[ware] : std::to_string(ware);
}

std::string GoodsWindow::class_name(const assets::BuildingTable& buildings, std::size_t k) const {
    const auto& classes = buildings.resident_classes();
    if (k < classes.size()) {
        if (std::string name = assets::class_name(classes[k], ui::current_language()); !name.empty()) {
            return name;
        }
    }
    return k < m_class_names.size() ? m_class_names[k] : std::to_string(k + 1);
}

std::int32_t GoodsWindow::ware_icon(const sim::World& world, std::uint8_t ware) const {
    return ui::ware_sprite(m_ware_icon, world.goods(), ware);
}

void GoodsWindow::wheel(std::int32_t notches) noexcept {
    if (m_detail) {
        return;
    }
    const std::size_t last = m_row_count > m_visible_rows ? m_row_count - m_visible_rows : 0;
    const auto step = static_cast<std::size_t>(std::abs(notches)) * 3;
    m_scroll = notches > 0 ? (m_scroll > step ? m_scroll - step : 0) : std::min(last, m_scroll + step);
}

std::vector<ui::DrawItem> GoodsWindow::draw(const sim::World& world, const assets::BuildingTable& buildings,
                                            const assets::FigureTable& figures, sim::PlayerId player,
                                            const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    m_islands = sim::goods_islands(world, player);
    if (m_island && std::ranges::find(m_islands, *m_island) == m_islands.end()) {
        m_island.reset();  // the city there was lost
    }
    m_form.clear();
    // Under the form's buttons and rows: the frame and the bands; over them: texts and pictures.
    Painter below(scale(), fonts);
    Painter above(scale(), fonts);
    layout_head(world, player, below, above);
    if (m_detail) {
        layout_detail(world, buildings, player, *m_detail, below, above);
    } else {
        layout_tabs();
        switch (m_tab) {
            case Tab::Goods:
                layout_table(world, player, below, above);
                break;
            case Tab::Producers:
                layout_producers(world, buildings, figures, player, below, above);
                break;
            case Tab::Population:
                layout_population(world, buildings, player, below, above);
                break;
            case Tab::Trade:
                layout_trade(world, player, below, above);
                break;
            case Tab::Finances:
                layout_finances(world, buildings, figures, player, below, above);
                break;
        }
    }
    std::vector<ui::DrawItem> out = std::move(below.items());
    std::vector<ui::DrawItem> form = m_form.draw(fonts);
    out.insert(out.end(), std::make_move_iterator(form.begin()), std::make_move_iterator(form.end()));
    out.insert(out.end(), std::make_move_iterator(above.items().begin()), std::make_move_iterator(above.items().end()));
    return out;
}

void GoodsWindow::layout_head(const sim::World& world, sim::PlayerId player, Painter& below, Painter& above) {
    const GoodsStyle& s = m_style;
    const ui::FormStyle& st = s.form;
    // The frame: a dark outline, lit at the top left and shaded at the bottom right.
    below.outline(Rect{0, 0, kWidth, kHeight}, s.dark, 2);
    below.fill(Rect{2, 2, kWidth - 4, 1}, s.light);
    below.fill(Rect{2, 3, 1, kHeight - 5}, s.light);
    below.fill(Rect{2, kHeight - 3, kWidth - 4, 1}, s.shade);
    below.fill(Rect{kWidth - 3, 3, 1, kHeight - 5}, s.shade);
    above.text(Rect{0, 10, kWidth, 30}, ui::tr("stats.title"), Align::Centre, st.text);
    // The close button: a cross.
    const Rect close{kWidth - 40, 8, 30, 30};
    m_form.row(kClose, close, false);
    for (std::int32_t i = 0; i < 15; ++i) {
        above.fill(Rect{close.x + 7 + i, close.y + 7 + i, 3, 3}, s.dark);
        above.fill(Rect{close.x + 7 + i, close.y + 21 - i, 3, 3}, s.dark);
    }

    // The island and the time averaged over.
    std::string island = ui::tr("stats.all_islands", {std::to_string(m_islands.size())});
    if (m_island) {
        for (const sim::City& c : world.cities()) {
            if (c.owner == player && c.island == *m_island) {
                island = city_name(c);
                break;
            }
        }
    }
    const Rect selector{kSelectorX, kSelectorY, kSelectorW, kSelectorH};
    m_form.cycle_button(kIsland, selector, island, !m_islands.empty());
    above.arrow(selector.x + 9, selector.y + selector.h / 2, true, 7, s.dark);
    above.arrow(selector.x + selector.w - 9, selector.y + selector.h / 2, false, 7, s.dark);
    above.text(Rect{kWidth - 110, 38, 90, 16}, ui::tr("stats.period"), Align::Centre, st.dim, kFontSmall);
    m_form.cycle_button(kWindow, Rect{kWidth - 110, 56, 90, 22}, ui::tr("stats.minutes", {std::to_string(minutes())}));
    if (m_detail) {
        // Back to the table, at the top left.
        const Rect back{kMargin, 44, 150, 28};
        m_form.row(kBack, back, false);
        above.arrow(back.x + 4, back.y + back.h / 2, true, 6, s.dark);
        above.text(Rect{back.x + 16, back.y, back.w - 16, back.h}, ui::tr("stats.overview"), Align::Left, st.text,
                   kFontMedium);
    }
}

void GoodsWindow::layout_table(const sim::World& world, sim::PlayerId player, Painter& below, Painter& above) {
    const GoodsStyle& s = m_style;
    const ui::FormStyle& st = s.form;
    const sim::GoodsScope scope{player, m_island};
    const std::uint32_t window = minutes();
    m_form.check(kAllGoods, Rect{kWidth - 160, kFooterY + 4, 146, 24}, ui::tr("stats.all_goods"), m_all_goods);

    // The rows: group headings and the goods worth showing.
    struct Line {
        std::optional<std::uint8_t> ware;  // nothing: a heading
        std::string_view title;
        sim::FlowTotals totals;
        std::uint64_t stock = 0;
    };
    std::vector<Line> lines;
    std::uint32_t known = 0;  // minutes the figures cover (fewer at the start)
    for (const Group& g : groups_of(world, player)) {
        const std::size_t heading = lines.size();
        lines.push_back(Line{std::nullopt, g.title, {}, 0});
        for (const std::uint8_t w : g.wares) {
            Line l{w, {}, sim::goods_totals(world, scope, w, window), sim::goods_stock(world, scope, w)};
            known = std::max(known, l.totals.minutes);
            if (m_all_goods || l.stock != 0 || l.totals.in() != 0 || l.totals.out() != 0) {
                lines.push_back(l);
            }
        }
        if (lines.size() == heading + 1) {
            lines.pop_back();  // nothing of the group
        }
    }
    m_row_count = lines.size();
    m_visible_rows = kRows;
    m_scroll = std::min(m_scroll, m_row_count > kRows ? m_row_count - kRows : 0);

    // The heading of the table.
    above.text(Rect{kMargin + 2, kHeaderY, 120, 20}, ui::tr("stats.col.good"), Align::Left, st.dim, kFontSmall);
    for (const Column& c : kColumns) {
        above.text(Rect{c.right - kColumnW, kHeaderY, kColumnW, 20}, ui::tr(c.heading), Align::Right, st.dim,
                   kFontSmall);
    }
    above.text(Rect{kSparkX, kHeaderY, kSparkW, 20}, ui::tr("stats.col.trend"), Align::Centre, st.dim, kFontSmall);
    below.fill(Rect{kMargin, kTableY - 4, kWidth - 2 * kMargin, 1}, s.shade);

    if (lines.empty()) {
        above.text(Rect{kMargin, kTableY + 20, kWidth - 2 * kMargin, 30}, ui::tr("stats.empty"), Align::Centre, st.dim);
    }
    bool banded = false;  // every other good's row
    for (std::size_t i = 0; i < kRows && m_scroll + i < lines.size(); ++i) {
        const Line& l = lines[m_scroll + i];
        const std::int32_t y = kTableY + static_cast<std::int32_t>(i) * kRowH;
        if (!l.ware) {
            above.text(Rect{kMargin + 2, y + 4, 400, kRowH - 4}, ui::tr(l.title), Align::Left, st.text, kFontMedium);
            continue;
        }
        const std::uint8_t w = *l.ware;
        const Rect row{kMargin, y + 1, kScrollX - 6 - kMargin, kRowH - 2};
        if (banded) {
            below.fill(row, s.band, true);
        }
        banded = !banded;
        m_form.row(kRow + w, row, false);
        below.fill(Rect{kMargin + 2, y + 2, 30, kRowH - 4}, s.dark);
        above.sprite(Rect{kMargin + 3, y + 3, 28, kRowH - 6}, ware_icon(world, w));
        const bool quiet = l.stock == 0 && l.totals.in() == 0 && l.totals.out() == 0;
        const std::int32_t tone = quiet ? st.dim : st.text;
        above.text(Rect{kNameX, y, kColumns[0].right - kColumnW - kNameX, kRowH}, ware_name(world, w), Align::Left,
                   tone, kFontMedium);
        const sim::FlowTotals& t = l.totals;
        const std::uint32_t m = std::max<std::uint32_t>(t.minutes, 1);
        const auto rate = [&](std::uint64_t v) {
            return v == 0 ? std::string("-") : rate_text(static_cast<std::int64_t>(v), m);
        };
        const std::array<std::string, 5> values = {
            ui::tr("stats.tons", {std::to_string(tons(l.stock))}),
            rate(t.of(GoodsFlow::Produced)),
            rate(imported(t)),
            rate(consumed(t)),
            rate(exported(t)),
        };
        for (std::size_t c = 0; c < values.size(); ++c) {
            above.text(Rect{kColumns[c].right - kColumnW, y, kColumnW, kRowH}, values[c], Align::Right, tone,
                       kFontMedium);
        }
        const std::int64_t net = t.net();
        above.text(Rect{kColumns[5].right - kColumnW, y, kColumnW, kRowH}, rate_text(net, m, true), Align::Right,
                   net > 0   ? ui::kTextInkGreen
                   : net < 0 ? ui::kTextInkRed
                             : tone,
                   kFontMedium);
        const std::optional<std::uint32_t> left = sim::minutes_left(l.stock, t);
        above.text(Rect{kColumns[6].right - kColumnW, y, kColumnW, kRowH},
                   !left          ? std::string("-")
                   : l.stock == 0 ? ui::tr("stats.gone")
                                  : lasts_text(left),
                   Align::Right, left && *left < 10 ? ui::kTextInkRed : tone, kFontMedium);
        // The balance of each of the last minutes: green above the line, red below.
        const std::vector<sim::GoodsSample> history = sim::goods_history(world, scope, w, false);
        const std::int32_t mid = y + kRowH / 2;
        const std::int32_t half = kRowH / 2 - 5;
        std::int64_t top = 1;
        for (const sim::GoodsSample& g : history) {
            top = std::max(top, std::abs(static_cast<std::int64_t>(g.in()) - static_cast<std::int64_t>(g.out())));
        }
        const std::size_t slots = sim::GoodsLedger::kMinutes;
        const std::size_t first = slots - std::min(slots, history.size());
        for (std::size_t k = 0; k < history.size() && k < slots; ++k) {
            const sim::GoodsSample& g = history[history.size() - std::min(slots, history.size()) + k];
            const std::int64_t v = static_cast<std::int64_t>(g.in()) - static_cast<std::int64_t>(g.out());
            const auto h = static_cast<std::int32_t>(std::abs(v) * half / top);
            const std::int32_t x = kSparkX + static_cast<std::int32_t>((first + k) * kSparkW / slots);
            if (v > 0) {
                above.fill(Rect{x, mid - h, 1, h}, s.green);
            } else if (v < 0) {
                above.fill(Rect{x, mid + 1, 1, h}, s.red);
            }
        }
        above.fill(Rect{kSparkX, mid, kSparkW, 1}, s.shade);
    }
    // The scroll bar: a click above or below the thumb turns a page.
    layout_scroll_bar(below);
    // Below the table: what the figures mean.
    below.fill(Rect{kMargin, kFooterY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    const std::string hint = known < window && known != 0
                                 ? ui::tr("stats.hint_short", {std::to_string(known), std::to_string(window)})
                                 : ui::tr("stats.hint", {std::to_string(window)});
    above.paragraph(Rect{kMargin + 2, kFooterY, kWidth - 2 * kMargin - 170, 34}, hint, st.dim, kFontSmall, 17);
}

void GoodsWindow::layout_detail(const sim::World& world, const assets::BuildingTable& buildings, sim::PlayerId player,
                                std::uint8_t ware, Painter& below, Painter& above) {
    const GoodsStyle& s = m_style;
    const ui::FormStyle& st = s.form;
    const sim::GoodsScope scope{player, m_island};
    const sim::FlowTotals t = sim::goods_totals(world, scope, ware, minutes());
    const std::uint32_t m = std::max<std::uint32_t>(t.minutes, 1);
    const std::uint64_t stock = sim::goods_stock(world, scope, ware);
    const std::uint64_t room = sim::goods_capacity(world, scope);
    const std::int64_t net = t.net();

    // The good, its stock and balance.
    below.fill(Rect{kMargin, 84, 50, 48}, s.dark);
    above.sprite(Rect{kMargin + 1, 85, 48, 46}, ware_icon(world, ware));
    above.text(Rect{kMargin + 60, 86, 300, 26}, ware_name(world, ware), Align::Left, st.text);
    above.text(Rect{kMargin + 60, 112, 300, 18},
               ui::tr("stats.stock", {std::to_string(tons(stock)), std::to_string(tons(room))}), Align::Left, st.dim,
               kFontSmall);
    above.text(Rect{kWidth - 330, 86, 180, 26}, ui::tr("stats.col.balance"), Align::Right, st.text, kFontMedium);
    above.text(Rect{kWidth - 150, 84, 136, 30}, rate_text(net, m, true) + " " + ui::tr("stats.per_minute"),
               Align::Right,
               net > 0   ? ui::kTextInkGreen
               : net < 0 ? ui::kTextInkRed
                         : st.text);
    std::string outlook;
    if (const std::optional<std::uint32_t> left = sim::minutes_left(stock, t)) {
        outlook = stock == 0 ? ui::tr("stats.gone") : ui::tr("stats.lasts", {lasts_text(left)});
    } else if (net > 0 && room > stock) {
        // Rounded up: a store that is not yet full is never full "in 0 min".
        const auto rise = static_cast<std::uint64_t>(net);
        const std::uint64_t full = std::max<std::uint64_t>(1, ((room - stock) * m + rise - 1) / rise);
        outlook =
            ui::tr("stats.full_in", {lasts_text(static_cast<std::uint32_t>(std::min<std::uint64_t>(full, 99999)))});
    } else if (room != 0 && stock >= room) {
        outlook = ui::tr("stats.full");
    }
    above.text(Rect{kWidth - 330, 112, 316, 18}, outlook, Align::Right, st.dim, kFontSmall);

    // Where it came from and went to: two columns of bars.
    const std::vector<sim::GoodsProducers> producers = sim::goods_producers(world, buildings, scope, ware);
    struct Bar {
        std::string label;
        std::uint64_t value;
    };
    std::vector<Bar> in;
    // Production split over the kinds of buildings by what each made lately.
    std::uint64_t recent = 0;
    for (const sim::GoodsProducers& p : producers) {
        recent += p.recent;
    }
    const std::uint64_t produced = t.of(GoodsFlow::Produced);
    std::uint64_t given = 0;
    for (std::size_t i = 0; i < producers.size(); ++i) {
        const sim::GoodsProducers& p = producers[i];
        std::uint64_t share = 0;
        if (recent != 0) {
            share = i + 1 == producers.size() ? produced - given : produced * p.recent / recent;
        } else if (i == 0) {
            share = produced;
        }
        given += share;
        const std::string name = m_building_names ? m_building_names(p.building) : std::string();
        in.push_back(
            {ui::tr("stats.producer", {name.empty() ? ui::tr("stats.flow.produced") : name, std::to_string(p.count)}),
             share});
    }
    if (producers.empty()) {
        in.push_back({ui::tr("stats.flow.produced"), produced});
    }
    for (const FlowLine& f : kInflows) {
        in.push_back({ui::tr(f.label), t.of(f.flow)});
    }
    std::vector<Bar> out;
    for (const FlowLine& f : kOutflows) {
        out.push_back({ui::tr(f.label), t.of(f.flow)});
    }
    std::uint64_t top = 1;
    for (const Bar& b : in) {
        top = std::max(top, b.value);
    }
    for (const Bar& b : out) {
        top = std::max(top, b.value);
    }
    constexpr std::int32_t kColumnsY = 140;
    constexpr std::int32_t kLineH = 22;
    constexpr std::int32_t kHalf = kWidth / 2;
    const auto column = [&](std::int32_t x0, std::int32_t x1, std::string_view title, std::uint64_t total,
                            const std::vector<Bar>& bars, std::uint8_t colour, std::int32_t ink) {
        above.text(Rect{x0, kColumnsY, 200, kLineH}, ui::tr(title), Align::Left, st.text, kFontMedium);
        above.text(Rect{x1 - 120, kColumnsY, 120, kLineH},
                   rate_text(static_cast<std::int64_t>(total), m) + " " + ui::tr("stats.per_minute"), Align::Right, ink,
                   kFontMedium);
        std::int32_t y = kColumnsY + kLineH + 6;
        const std::int32_t bar_x = x0 + (x1 - x0) * 46 / 100;
        const std::int32_t bar_w = (x1 - x0) * 37 / 100;
        for (const Bar& b : bars) {
            const std::int32_t tone = b.value == 0 ? st.dim : st.text;
            above.text(Rect{x0, y, bar_x - x0 - 6, kLineH}, b.label, Align::Left, tone, kFontMedium);
            below.fill(Rect{bar_x, y + 5, bar_w, kLineH - 10}, s.band, true);
            above.outline(Rect{bar_x, y + 5, bar_w, kLineH - 10}, s.shade);
            above.fill(
                Rect{bar_x + 1, y + 6, static_cast<std::int32_t>(b.value * static_cast<std::uint64_t>(bar_w - 2) / top),
                     kLineH - 12},
                colour);
            above.text(Rect{x1 - 50, y, 50, kLineH},
                       b.value == 0 ? std::string("-") : rate_text(static_cast<std::int64_t>(b.value), m), Align::Right,
                       tone, kFontMedium);
            y += kLineH;
        }
        return y;
    };
    below.fill(Rect{kMargin, kColumnsY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    const std::int32_t in_end = column(kMargin + 2, kHalf - 16, "stats.inflow", t.in(), in, s.green, ui::kTextInkGreen);
    const std::int32_t out_end =
        column(kHalf + 10, kWidth - kMargin - 2, "stats.outflow", t.out(), out, s.red, ui::kTextInkRed);
    const std::int32_t columns_end = std::max(in_end, out_end) + 4;
    below.fill(Rect{kHalf - 3, kColumnsY + 2, 1, columns_end - kColumnsY - 4}, s.shade);
    below.fill(Rect{kMargin, columns_end, kWidth - 2 * kMargin, 1}, s.shade);

    // The chart: in (green) and out (red) per minute as lines, the stock as a brown area.
    const std::int32_t head_y = columns_end + 6;
    above.text(Rect{kMargin + 2, head_y, 200, 24}, ui::tr("stats.history"), Align::Left, st.text, kFontMedium);
    m_form.button(kChartHour, Rect{kWidth - 210, head_y, 90, 24}, ui::tr("stats.chart_60min"), true, !m_long_chart);
    m_form.button(kChartDay, Rect{kWidth - 110, head_y, 90, 24}, ui::tr("stats.chart_12h"), true, m_long_chart);
    const std::int32_t legend_h = 22;
    const std::int32_t chart_x = 50;
    const std::int32_t chart_y = head_y + 34;
    const std::int32_t chart_w = kWidth - chart_x - 54;
    const std::int32_t chart_h = std::max(60, kFooterY - 8 - legend_h - 22 - chart_y);
    above.outline(Rect{chart_x, chart_y, chart_w, chart_h}, s.shade);
    const std::vector<sim::GoodsSample> history = sim::goods_history(world, scope, ware, m_long_chart);
    const std::size_t slots = m_long_chart ? sim::GoodsLedger::kQuarters : sim::GoodsLedger::kMinutes;
    std::uint64_t flow_top = 1;
    std::uint64_t stock_top = 1;
    for (const sim::GoodsSample& g : history) {
        flow_top = std::max({flow_top, g.in(), g.out()});
        stock_top = std::max(stock_top, g.stock);
    }
    // Round the scales up to whole tons (per minute, of stock), in four steps.
    const std::uint64_t flow_scale =
        (flow_top + 4 * assets::kStockUnitsPerTon - 1) / (4 * assets::kStockUnitsPerTon) * 4;
    const std::uint64_t stock_scale =
        (stock_top + 4 * assets::kStockUnitsPerTon - 1) / (4 * assets::kStockUnitsPerTon) * 4;
    const std::int32_t inner_h = chart_h - 2;
    const auto x_of = [&](std::size_t slot) {
        return chart_x + 1 + static_cast<std::int32_t>(slot * static_cast<std::size_t>(chart_w - 2) / (slots - 1));
    };
    const auto y_of = [&](std::uint64_t v, std::uint64_t scale_tons) {
        return chart_y + 1 + inner_h -
               static_cast<std::int32_t>(v * static_cast<std::uint64_t>(inner_h) /
                                         std::max<std::uint64_t>(scale_tons * assets::kStockUnitsPerTon, 1));
    };
    for (std::int32_t k = 0; k <= 4; ++k) {
        const std::int32_t y = chart_y + 1 + inner_h - k * inner_h / 4;
        if (k > 0 && k < 4) {
            for (std::int32_t x = chart_x + 3; x < chart_x + chart_w - 2; x += 6) {
                below.fill(Rect{x, y, 2, 1}, s.shade);
            }
        }
        above.text(Rect{chart_x - 40, y - 9, 34, 18}, std::to_string(flow_scale * static_cast<std::uint64_t>(k) / 4),
                   Align::Right, st.dim, kFontSmall);
        above.text(Rect{chart_x + chart_w + 6, y - 9, 48, 18},
                   ui::tr("stats.tons", {std::to_string(stock_scale * static_cast<std::uint64_t>(k) / 4)}), Align::Left,
                   st.dim, kFontSmall);
    }
    const std::size_t first = slots - std::min(slots, history.size());
    const std::size_t shown = std::min(slots, history.size());
    std::optional<std::pair<std::int32_t, std::int32_t>> last;  // the lines' previous points
    for (std::size_t k = 0; k < shown; ++k) {
        const sim::GoodsSample& g = history[history.size() - shown + k];
        const std::int32_t x = x_of(first + k);
        const std::int32_t next = k + 1 < shown ? x_of(first + k + 1) : x + 1;
        // The stock: a column down to the axis at half strength.
        const std::int32_t level = y_of(g.stock, stock_scale);
        below.fill(Rect{x, level, std::max(1, next - x), chart_y + 1 + inner_h - level}, s.stock, true);
        // The lines: from the previous point down or up to this one, two units thick.
        const std::int32_t yi = y_of(g.in(), flow_scale);
        const std::int32_t yo = y_of(g.out(), flow_scale);
        const auto segment = [&](std::int32_t from, std::int32_t to, std::int32_t from_x, std::uint8_t colour) {
            const std::int32_t lo = std::min(from, to);
            const std::int32_t hi = std::max(from, to);
            above.fill(Rect{from_x, lo - 1, std::max(2, x - from_x + 1), 2}, colour);
            above.fill(Rect{x - 1, lo - 1, 2, hi - lo + 2}, colour);
        };
        if (last) {
            segment(last->first, yi, x_of(first + k - 1), s.green);
            segment(last->second, yo, x_of(first + k - 1), s.red);
        } else {
            above.fill(Rect{x - 1, yi - 1, 2, 2}, s.green);
            above.fill(Rect{x - 1, yo - 1, 2, 2}, s.red);
        }
        last = std::pair{yi, yo};
    }
    if (history.empty()) {
        above.text(Rect{chart_x, chart_y + chart_h / 2 - 12, chart_w, 24}, ui::tr("stats.no_data"), Align::Centre,
                   st.dim, kFontSmall);
    }
    // The time axis and the legend.
    const std::int32_t axis_y = chart_y + chart_h + 2;
    for (std::int32_t k = 0; k <= 4; ++k) {
        const std::size_t slot = (slots - 1) * static_cast<std::size_t>(k) / 4;
        const std::string label = k == 4 ? ui::tr("stats.axis_now")
                                         : ui::tr(m_long_chart ? "stats.axis_hours" : "stats.axis_minutes",
                                                  {std::to_string((m_long_chart ? 12 : 60) * (4 - k) / 4)});
        above.text(Rect{x_of(slot) - 40, axis_y, 80, 18}, label, k == 0 ? Align::Left : Align::Centre, st.dim,
                   kFontSmall);
    }
    std::int32_t lx = chart_x;
    const std::int32_t legend_y = axis_y + 22;
    for (const auto& [colour, label] : {std::pair{s.green, "stats.legend_in"}, std::pair{s.red, "stats.legend_out"},
                                        std::pair{s.stock, "stats.legend_stock"}}) {
        below.fill(Rect{lx, legend_y + 7, 14, 8}, colour);
        above.text(Rect{lx + 18, legend_y, 200, legend_h}, ui::tr(label), Align::Left, st.dim, kFontSmall);
        lx += 210;
    }

    // Below: the inhabitants' supply and the producers at work.
    below.fill(Rect{kMargin, kFooterY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    std::vector<std::pair<std::string, std::string>> facts;
    if (const std::uint64_t demand = t.of(GoodsFlow::Demand); demand != 0) {
        const std::uint64_t percent = std::min<std::uint64_t>(100, t.of(GoodsFlow::Residents) * 100 / demand);
        facts.emplace_back(
            ui::tr("stats.supply"),
            ui::tr("stats.supply_value", {std::to_string(percent), rate_text(static_cast<std::int64_t>(demand), m)}));
    }
    if (!producers.empty()) {
        std::uint32_t count = 0;
        std::uint32_t working = 0;
        std::uint32_t short_of_raw = 0;
        std::uint32_t off = 0;
        for (const sim::GoodsProducers& p : producers) {
            count += p.count;
            working += p.working;
            short_of_raw += p.short_of_raw;
            off += p.off;
        }
        std::string text = ui::tr("stats.working", {std::to_string(working), std::to_string(count)});
        if (short_of_raw != 0) {
            text += ", " + ui::tr("stats.short_of_raw", {std::to_string(short_of_raw)});
        }
        if (off != 0) {
            text += ", " + ui::tr("stats.switched_off", {std::to_string(off)});
        }
        facts.emplace_back(ui::tr("stats.producers"), text);
    }
    for (std::size_t i = 0; i < facts.size() && i < 2; ++i) {
        const std::int32_t y = kFooterY + static_cast<std::int32_t>(i) * 17;
        above.text(Rect{kMargin + 6, y, 210, 17}, facts[i].first, Align::Left, st.dim, kFontSmall);
        above.text(Rect{kMargin + 230, y, kWidth - kMargin - 240, 17}, facts[i].second, Align::Left, st.text,
                   kFontMedium);
    }
}

GoodsWindow::Result GoodsWindow::mouse(std::int32_t x, std::int32_t y, bool down, ui::MouseButton button) {
    const auto id = m_form.mouse(x, y, down, button);
    if (!id) {
        return Result::None;
    }
    const bool forward = m_form.clicked_with() == ui::MouseButton::Left;
    switch (*id) {
        case kClose:
            return Result::Close;
        case kBack:
            m_detail.reset();
            break;
        case kIsland: {
            // All islands, then each of them in turn.
            const std::size_t count = m_islands.size() + 1;
            std::size_t at = 0;
            if (m_island) {
                at = static_cast<std::size_t>(std::ranges::find(m_islands, *m_island) - m_islands.begin()) + 1;
            }
            at = forward ? (at + 1) % count : (at + count - 1) % count;
            m_island = at == 0 ? std::nullopt : std::optional<std::uint16_t>(m_islands[at - 1]);
            m_scroll = 0;
            break;
        }
        case kWindow:
            m_window = forward ? (m_window + 1) % kWindows.size() : (m_window + kWindows.size() - 1) % kWindows.size();
            break;
        case kAllGoods:
            m_all_goods = !m_all_goods;
            break;
        case kChartHour:
            m_long_chart = false;
            break;
        case kChartDay:
            m_long_chart = true;
            break;
        case kPageUp:
            m_scroll = m_scroll > m_visible_rows ? m_scroll - m_visible_rows : 0;
            break;
        case kPageDown:
            m_scroll += m_visible_rows;  // clamped when laid out
            break;
        default:
            if (*id >= kTab && *id < kTab + static_cast<Id>(kTabs)) {
                m_tab = static_cast<Tab>(*id - kTab);
                m_scroll = 0;
            } else if (*id >= kRow && *id < kRow + 256) {
                m_detail = static_cast<std::uint8_t>(*id - kRow);
            } else if (*id >= kProducerRow && static_cast<std::size_t>(*id - kProducerRow) < m_row_wares.size()) {
                m_detail = m_row_wares[static_cast<std::size_t>(*id - kProducerRow)];
            }
            break;
    }
    return Result::None;
}

}  // namespace o1602::app
