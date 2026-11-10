// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tabs of the window "Warenbilanz" besides the goods: the production buildings, the
// inhabitants, the trade at the Kontor and the finances (docs/architecture/0012-goods-statistics.md).
#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

#include "goods_window.hpp"
#include "goods_window_parts.hpp"
#include "production_utilisation.hpp"

#include "o1602/sim/consumption.hpp"
#include "o1602/sim/goods_statistics.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/sim/production.hpp"

namespace o1602::app {

namespace {

using namespace goods_parts;
using sim::CityFigure;

constexpr std::array<std::string_view, GoodsWindow::kTabs> kTabLabels = {
    "stats.tab.goods", "stats.tab.producers", "stats.tab.population", "stats.tab.trade", "stats.tab.finances"};

/// Where a good stands in the table of goods (its group, then its place), for sorting.
std::size_t ware_order(std::uint8_t ware) {
    std::size_t at = 0;
    for (const Group& g : groups()) {
        for (const std::uint8_t w : g.wares) {
            if (w == ware) {
                return at;
            }
            ++at;
        }
    }
    return at + ware;
}

/// A whole number, with a "+" before it when asked and it is above zero.
std::string number_text(std::int64_t v, bool sign = false) {
    return (v > 0 && sign ? "+" : "") + std::to_string(v);
}

/// The step of a chart's scale: 1, 2 or 5 times a power of ten, so that four steps reach `top`.
std::int64_t nice_step(std::int64_t top) {
    const std::int64_t rough = std::max<std::int64_t>(1, (top + 3) / 4);
    std::int64_t power = 1;
    while (power * 10 <= rough) {
        power *= 10;
    }
    for (const std::int64_t f : {1, 2, 5, 10}) {
        if (f * power >= rough) {
            return f * power;
        }
    }
    return 10 * power;
}

}  // namespace

void GoodsWindow::layout_tabs() {
    for (std::size_t i = 0; i < kTabs; ++i) {
        const auto x = kMargin + static_cast<std::int32_t>(i) * (kTabW + 4);
        m_form.button(kTab + static_cast<Id>(i), Rect{x, kTabY, kTabW, kTabH}, ui::tr(kTabLabels[i]), true,
                      static_cast<std::size_t>(m_tab) == i);
    }
}

void GoodsWindow::layout_scroll_bar(Painter& below) {
    if (m_row_count <= m_visible_rows || m_visible_rows == 0) {
        return;
    }
    const auto rows = static_cast<std::int32_t>(m_visible_rows);
    const std::int32_t track = rows * kRowH;
    const auto thumb_h = std::max<std::int32_t>(20, track * rows / static_cast<std::int32_t>(m_row_count));
    const auto thumb_y = kTableY + (track - thumb_h) * static_cast<std::int32_t>(m_scroll) /
                                       static_cast<std::int32_t>(m_row_count - m_visible_rows);
    m_form.row(kPageUp, Rect{kScrollX - 4, kTableY, kScrollW + 8, thumb_y - kTableY}, false);
    m_form.row(kPageDown, Rect{kScrollX - 4, thumb_y + thumb_h, kScrollW + 8, kTableY + track - thumb_y - thumb_h},
               false);
    below.fill(Rect{kScrollX, kTableY, kScrollW, track}, m_style.band, true);
    below.fill(Rect{kScrollX, thumb_y, kScrollW, thumb_h}, m_style.shade);
}

void GoodsWindow::layout_chart(std::int32_t top, const std::vector<Series>& series, Painter& below, Painter& above) {
    const GoodsStyle& s = m_style;
    const ui::FormStyle& st = s.form;
    above.text(Rect{kMargin + 2, top, 200, 24}, ui::tr("stats.history"), Align::Left, st.text, kFontMedium);
    m_form.button(kChartHour, Rect{kWidth - 210, top, 90, 24}, ui::tr("stats.chart_60min"), true, !m_long_chart);
    m_form.button(kChartDay, Rect{kWidth - 110, top, 90, 24}, ui::tr("stats.chart_12h"), true, m_long_chart);
    const std::int32_t legend_h = 20;
    const std::int32_t chart_x = 64;
    const std::int32_t chart_y = top + 32;
    const std::int32_t chart_w = kWidth - chart_x - kMargin - 10;
    const std::int32_t chart_h = std::max(50, kFooterY - 8 - legend_h - 20 - chart_y);
    above.outline(Rect{chart_x, chart_y, chart_w, chart_h}, s.shade);
    const std::size_t slots = m_long_chart ? sim::CityHistory::kQuarters : sim::CityHistory::kMinutes;

    // The scale: from the smallest value (at most 0) to the largest, in four even steps.
    std::int64_t lo = 0;
    std::int64_t hi = 1;
    std::size_t shown = 0;
    for (const Series& line : series) {
        for (const std::int64_t v : line.values) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        shown = std::max(shown, std::min(slots, line.values.size()));
    }
    const std::int64_t step = nice_step(hi - lo);
    const std::int64_t base = lo < 0 ? -((-lo + step - 1) / step) * step : 0;
    const std::int64_t steps = std::max<std::int64_t>(1, (hi - base + step - 1) / step);
    const std::int64_t span = steps * step;
    const std::int32_t inner_h = chart_h - 2;
    const auto y_of = [&](std::int64_t v) {
        return chart_y + 1 + inner_h - static_cast<std::int32_t>((v - base) * inner_h / span);
    };
    const auto x_of = [&](std::size_t slot) {
        return chart_x + 1 + static_cast<std::int32_t>(slot * static_cast<std::size_t>(chart_w - 2) / (slots - 1));
    };
    for (std::int64_t k = 0; k <= steps; ++k) {
        const std::int64_t v = base + k * step;
        const std::int32_t y = y_of(v);
        if (k > 0 && k < steps) {
            for (std::int32_t x = chart_x + 3; x < chart_x + chart_w - 2; x += 6) {
                below.fill(Rect{x, y, 2, 1}, v == 0 ? s.dark : s.shade);
            }
        }
        if (steps <= 8 || k % 2 == 0) {
            above.text(Rect{chart_x - 60, y - 9, 54, 18}, std::to_string(v), Align::Right, st.dim, kFontSmall);
        }
    }
    const std::size_t first = slots - shown;
    for (const Series& line : series) {
        const std::size_t n = std::min(slots, line.values.size());
        std::optional<std::int32_t> last;
        for (std::size_t k = 0; k < n; ++k) {
            const std::int64_t v = line.values[line.values.size() - n + k];
            const std::int32_t x = x_of(first + (shown - n) + k);
            const std::int32_t y = y_of(v);
            if (last) {
                const std::int32_t from_x = x_of(first + (shown - n) + k - 1);
                above.fill(Rect{from_x, *last - 1, std::max(2, x - from_x + 1), 2}, line.colour);
                above.fill(Rect{x - 1, std::min(*last, y) - 1, 2, std::abs(y - *last) + 2}, line.colour);
            } else {
                above.fill(Rect{x - 1, y - 1, 2, 2}, line.colour);
            }
            last = y;
        }
    }
    if (shown == 0) {
        above.text(Rect{chart_x, chart_y + chart_h / 2 - 12, chart_w, 24}, ui::tr("stats.no_data"), Align::Centre,
                   st.dim, kFontSmall);
    }
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
    const std::int32_t legend_y = axis_y + 20;
    const std::int32_t legend_w =
        series.empty() ? 0 : (kWidth - kMargin - chart_x) / static_cast<std::int32_t>(series.size());
    for (const Series& line : series) {
        below.fill(Rect{lx, legend_y + 6, 14, 8}, line.colour);
        above.text(Rect{lx + 18, legend_y, legend_w - 22, legend_h}, line.label, Align::Left, st.dim, kFontSmall);
        lx += legend_w;
    }
}

void GoodsWindow::layout_producers(const sim::World& world, const assets::BuildingTable& buildings,
                                   const assets::FigureTable& figures, sim::PlayerId player, Painter& below,
                                   Painter& above) {
    const GoodsStyle& s = m_style;
    const ui::FormStyle& st = s.form;
    const sim::GoodsScope scope{player, m_island};

    // Every kind of production building of the scope's cities.
    struct Kind {
        std::uint32_t building = 0;
        std::uint8_t ware = 0;
        std::uint32_t count = 0;
        std::uint32_t working = 0;
        std::uint32_t short_of_raw = 0;
        std::uint32_t off = 0;
        std::int64_t utilisation = 0;  ///< summed over the buildings switched on
        std::uint32_t on = 0;
        std::int64_t costs = 0;
    };
    std::vector<Kind> kinds;
    for (const sim::Production& p : world.productions()) {
        if (!p.used || p.island >= world.islands().size()) {
            continue;
        }
        const sim::MapCell& cell = world.islands()[p.island].map.top(p.x, p.y);
        const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
        const sim::City* city = world.city(p.island, cell.city);
        if (b == nullptr || !sim::is_producer(*b) || !world.goods().is_trade(b->ware) || city == nullptr ||
            !scope.covers(*city)) {
            continue;
        }
        auto kind = std::ranges::find(kinds, b->id, &Kind::building);
        if (kind == kinds.end()) {
            kinds.push_back(Kind{b->id, b->ware});
            kind = kinds.end() - 1;
        }
        ++kind->count;
        if (!p.enabled) {
            ++kind->off;
            kind->costs += b->idle_cost;
            continue;
        }
        kind->costs += b->running_cost;
        if (p.rate != 0) {
            ++kind->working;
        } else if (world.goods().is_trade(b->raw_ware) && p.raw < b->raw_amount) {
            ++kind->short_of_raw;
        }
        kind->utilisation += production_utilisation(p, *b, figures);
        ++kind->on;
    }
    std::ranges::sort(kinds, [](const Kind& a, const Kind& b) {
        return std::pair{ware_order(a.ware), a.building} < std::pair{ware_order(b.ware), b.building};
    });
    m_row_count = kinds.size();
    m_visible_rows = kRows;
    m_scroll = std::min(m_scroll, m_row_count > kRows ? m_row_count - kRows : 0);
    m_row_wares.clear();
    for (const Kind& k : kinds) {
        m_row_wares.push_back(k.ware);
    }

    // Name, number, utilisation as a bar, then how many work, wait or are switched off, and the costs.
    constexpr std::int32_t kNameX = 54;
    constexpr std::int32_t kCountR = 300;
    constexpr std::int32_t kBarX = 316;
    constexpr std::int32_t kBarW = 96;
    constexpr std::int32_t kPercentR = 462;
    constexpr std::int32_t kWorkingR = 520;
    constexpr std::int32_t kShortR = 624;
    constexpr std::int32_t kOffR = 662;
    constexpr std::int32_t kCostsR = 722;
    const auto head = [&](std::int32_t right, std::int32_t width, std::string_view key) {
        above.text(Rect{right - width, kHeaderY, width, 20}, ui::tr(key), Align::Right, st.dim, kFontSmall);
    };
    above.text(Rect{kMargin + 2, kHeaderY, 200, 20}, ui::tr("stats.prod.building"), Align::Left, st.dim, kFontSmall);
    head(kCountR, 70, "stats.prod.count");
    above.text(Rect{kBarX, kHeaderY, kPercentR - kBarX, 20}, ui::tr("stats.prod.utilisation"), Align::Left, st.dim,
               kFontSmall);
    head(kWorkingR, 66, "stats.prod.working");
    head(kShortR, 100, "stats.prod.short");
    head(kOffR, 40, "stats.prod.off");
    head(kCostsR, 58, "stats.prod.costs");
    below.fill(Rect{kMargin, kTableY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    if (kinds.empty()) {
        above.text(Rect{kMargin, kTableY + 20, kWidth - 2 * kMargin, 30}, ui::tr("stats.prod.empty"), Align::Centre,
                   st.dim);
    }
    std::uint32_t total = 0;
    std::int64_t total_costs = 0;
    for (const Kind& k : kinds) {
        total += k.count;
        total_costs += k.costs;
    }
    for (std::size_t i = 0; i < kRows && m_scroll + i < kinds.size(); ++i) {
        const Kind& k = kinds[m_scroll + i];
        const std::int32_t y = kTableY + static_cast<std::int32_t>(i) * kRowH;
        const Rect row{kMargin, y + 1, kScrollX - 6 - kMargin, kRowH - 2};
        if ((m_scroll + i) % 2 == 1) {
            below.fill(row, s.band, true);
        }
        m_form.row(kProducerRow + static_cast<Id>(m_scroll + i), row, false);
        below.fill(Rect{kMargin + 2, y + 2, 30, kRowH - 4}, s.dark);
        above.sprite(Rect{kMargin + 3, y + 3, 28, kRowH - 6}, ware_icon(world, k.ware));
        const std::string name = m_building_names ? m_building_names(k.building) : std::string();
        above.text(Rect{kNameX, y, kCountR - 70 - kNameX, kRowH}, name.empty() ? ware_name(world, k.ware) : name,
                   Align::Left, st.text, kFontMedium);
        above.text(Rect{kCountR - 60, y, 60, kRowH}, std::to_string(k.count), Align::Right, st.text, kFontMedium);
        const std::int64_t percent = k.on != 0 ? k.utilisation / k.on : 0;
        below.fill(Rect{kBarX, y + 9, kBarW, kRowH - 18}, s.band, true);
        above.outline(Rect{kBarX, y + 9, kBarW, kRowH - 18}, s.shade);
        above.fill(Rect{kBarX + 1, y + 10, static_cast<std::int32_t>(percent * (kBarW - 2) / 100), kRowH - 20},
                   percent >= 75   ? s.green
                   : percent >= 40 ? s.stock
                                   : s.red);
        above.text(Rect{kPercentR - 46, y, 46, kRowH}, k.on != 0 ? std::to_string(percent) + " %" : std::string("-"),
                   Align::Right, k.on != 0 && percent < 40 ? ui::kTextInkRed : st.text, kFontMedium);
        above.text(Rect{kWorkingR - 50, y, 50, kRowH}, std::to_string(k.working), Align::Right, st.text, kFontMedium);
        above.text(Rect{kShortR - 50, y, 50, kRowH}, k.short_of_raw != 0 ? std::to_string(k.short_of_raw) : "-",
                   Align::Right, k.short_of_raw != 0 ? ui::kTextInkRed : st.dim, kFontMedium);
        above.text(Rect{kOffR - 36, y, 36, kRowH}, k.off != 0 ? std::to_string(k.off) : "-", Align::Right,
                   k.off != 0 ? st.text : st.dim, kFontMedium);
        above.text(Rect{kCostsR - 56, y, 56, kRowH}, std::to_string(k.costs), Align::Right, st.text, kFontMedium);
    }
    layout_scroll_bar(below);
    below.fill(Rect{kMargin, kFooterY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    above.paragraph(Rect{kMargin + 2, kFooterY, kWidth - 2 * kMargin, 34},
                    ui::tr("stats.prod.hint", {std::to_string(total), std::to_string(total_costs)}), st.dim, kFontSmall,
                    17);
}

void GoodsWindow::layout_population(const sim::World& world, const assets::BuildingTable& buildings,
                                    sim::PlayerId player, Painter& below, Painter& above) {
    const GoodsStyle& s = m_style;
    const ui::FormStyle& st = s.form;
    const sim::GoodsScope scope{player, m_island};
    m_row_count = 0;
    const std::size_t classes = std::min(buildings.resident_classes().size(), sim::kCityClasses);

    // Per class: inhabitants, and satisfaction and tax weighted by the inhabitants of each city.
    struct Class {
        std::int64_t inhabitants = 0;
        std::int64_t satisfaction = 0;  ///< summed, times inhabitants
        std::int64_t tax = 0;
    };
    std::vector<Class> list(classes);
    std::int64_t all = 0;  // as the player status counts them, with the workers of farms
    const std::vector<std::uint32_t> workers = sim::city_workers(world, buildings);
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        const sim::City& c = world.cities()[i];
        if (!scope.covers(c)) {
            continue;
        }
        all += sim::city_einwohner(c, i < workers.size() ? workers[i] : 0);
        for (std::size_t k = 0; k < classes; ++k) {
            const std::int64_t n = k < c.inhabitants.size() ? c.inhabitants[k] : 0;
            list[k].inhabitants += n;
            list[k].satisfaction += n * (k < c.satisfaction.size() ? c.satisfaction[k] : 128);
            list[k].tax += n * (k < c.tax_rate.size() ? c.tax_rate[k] : 128);
        }
    }
    const std::vector<sim::CitySample> history = sim::city_history(world, scope, m_long_chart);
    const std::vector<sim::CitySample> hour = m_long_chart ? sim::city_history(world, scope, false) : history;

    // The classes, left.
    constexpr std::int32_t kLeftW = 400;
    constexpr std::int32_t kCountR = 196;
    constexpr std::int32_t kBarX = 210;
    constexpr std::int32_t kBarW = 90;
    constexpr std::int32_t kTaxR = kLeftW;
    constexpr std::int32_t kLineH = 26;
    above.text(Rect{kMargin + 2, kHeaderY, 120, 20}, ui::tr("stats.pop.class"), Align::Left, st.dim, kFontSmall);
    above.text(Rect{kCountR - 90, kHeaderY, 90, 20}, ui::tr("stats.pop.inhabitants"), Align::Right, st.dim, kFontSmall);
    above.text(Rect{kBarX, kHeaderY, 130, 20}, ui::tr("stats.pop.satisfaction"), Align::Left, st.dim, kFontSmall);
    above.text(Rect{kTaxR - 56, kHeaderY, 56, 20}, ui::tr("stats.pop.tax"), Align::Right, st.dim, kFontSmall);
    below.fill(Rect{kMargin, kTableY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    std::int32_t y = kTableY;
    for (std::size_t k = 0; k < classes; ++k) {
        const Class& c = list[k];
        const std::int32_t tone = c.inhabitants == 0 ? st.dim : st.text;
        if (k % 2 == 1) {
            below.fill(Rect{kMargin, y + 1, kLeftW - kMargin + 4, kLineH - 2}, s.band, true);
        }
        below.fill(Rect{kMargin + 4, y + 8, 10, 10}, s.series[k % s.series.size()]);
        above.text(Rect{kMargin + 20, y, kCountR - 50 - kMargin - 20, kLineH}, class_name(buildings, k), Align::Left,
                   tone, kFontMedium);
        above.text(Rect{kCountR - 50, y, 50, kLineH}, std::to_string(c.inhabitants), Align::Right, tone, kFontMedium);
        if (c.inhabitants != 0) {
            const std::int64_t percent = c.satisfaction * 100 / (c.inhabitants * 128);
            below.fill(Rect{kBarX, y + 7, kBarW, kLineH - 14}, s.band, true);
            above.outline(Rect{kBarX, y + 7, kBarW, kLineH - 14}, s.shade);
            above.fill(
                Rect{kBarX + 1, y + 8,
                     static_cast<std::int32_t>(std::min<std::int64_t>(percent, 100) * (kBarW - 2) / 100), kLineH - 16},
                percent >= 100  ? s.green
                : percent >= 70 ? s.stock
                                : s.red);
            above.text(Rect{kBarX + kBarW + 4, y, 46, kLineH}, std::to_string(percent) + " %", Align::Left,
                       percent < 70 ? ui::kTextInkRed : tone, kFontMedium);
            above.text(Rect{kTaxR - 50, y, 50, kLineH},
                       ui::tr("stats.percent", {std::to_string(c.tax / c.inhabitants / 4)}), Align::Right, tone,
                       kFontMedium);
        }
        y += kLineH;
    }
    // All of them and how many more than an hour ago.
    below.fill(Rect{kMargin, y + 2, kLeftW - kMargin + 4, 1}, s.shade);
    y += 6;
    above.text(Rect{kMargin + 20, y, kCountR - 50 - kMargin - 20, kLineH}, ui::tr("stats.pop.total"), Align::Left,
               st.text, kFontMedium);
    above.text(Rect{kCountR - 50, y, 50, kLineH}, std::to_string(all), Align::Right, st.text, kFontMedium);
    if (!hour.empty()) {
        const std::int64_t growth = all - hour.front().of(CityFigure::Inhabitants);
        above.text(Rect{kBarX, y, kLeftW - kBarX, kLineH},
                   ui::tr("stats.pop.growth", {number_text(growth, true), std::to_string(hour.size())}), Align::Left,
                   growth > 0   ? ui::kTextInkGreen
                   : growth < 0 ? ui::kTextInkRed
                                : st.dim,
                   kFontMedium);
    }
    const std::int32_t left_end = y + kLineH;

    // The supply of each need, right.
    constexpr std::int32_t kRightX = kLeftW + 24;
    above.text(Rect{kRightX, kHeaderY, kWidth - kMargin - kRightX, 20}, ui::tr("stats.pop.supply"), Align::Left, st.dim,
               kFontSmall);
    std::int32_t sy = kTableY;
    constexpr std::int32_t kSupplyH = 22;
    const std::int32_t name_w = 110;
    const std::int32_t bar_x = kRightX + name_w;
    const std::int32_t bar_w = kWidth - kMargin - 50 - bar_x;
    bool any = false;
    for (const std::uint8_t w : groups()[0].wares) {
        const sim::FlowTotals t = sim::goods_totals(world, scope, w, minutes());
        const std::uint64_t demand = t.of(GoodsFlow::Demand);
        if (demand == 0) {
            continue;
        }
        any = true;
        const auto percent =
            static_cast<std::int64_t>(std::min<std::uint64_t>(100, t.of(GoodsFlow::Residents) * 100 / demand));
        above.text(Rect{kRightX, sy, name_w - 6, kSupplyH}, ware_name(world, w), Align::Left, st.text, kFontMedium);
        below.fill(Rect{bar_x, sy + 5, bar_w, kSupplyH - 10}, s.band, true);
        above.outline(Rect{bar_x, sy + 5, bar_w, kSupplyH - 10}, s.shade);
        above.fill(Rect{bar_x + 1, sy + 6, static_cast<std::int32_t>(percent * (bar_w - 2) / 100), kSupplyH - 12},
                   percent >= 90   ? s.green
                   : percent >= 60 ? s.stock
                                   : s.red);
        above.text(Rect{kWidth - kMargin - 46, sy, 46, kSupplyH}, std::to_string(percent) + " %", Align::Right,
                   percent < 60 ? ui::kTextInkRed : st.text, kFontMedium);
        sy += kSupplyH;
    }
    if (!any) {
        above.text(Rect{kRightX, sy, kWidth - kMargin - kRightX, kSupplyH}, ui::tr("stats.no_data"), Align::Left,
                   st.dim, kFontSmall);
        sy += kSupplyH;
    }
    below.fill(Rect{kLeftW + 12, kTableY, 1, std::max(left_end, sy) - kTableY}, s.shade);

    // The inhabitants of each class over time.
    const std::int32_t chart_top = std::max(left_end, sy) + 10;
    below.fill(Rect{kMargin, chart_top - 4, kWidth - 2 * kMargin, 1}, s.shade);
    std::vector<Series> series;
    for (std::size_t k = 0; k < classes; ++k) {
        Series line{s.series[k % s.series.size()], class_name(buildings, k), {}};
        bool used = false;
        for (const sim::CitySample& sample : history) {
            line.values.push_back(sample.of_class(k));
            used = used || sample.of_class(k) != 0;
        }
        if (used || history.empty()) {
            series.push_back(std::move(line));
        }
    }
    layout_chart(chart_top, series, below, above);
    below.fill(Rect{kMargin, kFooterY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    above.paragraph(Rect{kMargin + 2, kFooterY, kWidth - 2 * kMargin, 34},
                    ui::tr("stats.pop.hint", {std::to_string(minutes())}), st.dim, kFontSmall, 17);
}

void GoodsWindow::layout_trade(const sim::World& world, sim::PlayerId player, Painter& below, Painter& above) {
    const GoodsStyle& s = m_style;
    const ui::FormStyle& st = s.form;
    const sim::GoodsScope scope{player, m_island};
    const std::uint32_t window = minutes();

    // The goods traded in the time chosen, in the order of the table of goods.
    struct Line {
        std::uint8_t ware;
        sim::FlowTotals totals;
    };
    std::vector<Line> lines;
    std::array<std::int64_t, 2> trader{};  // earned, paid
    std::array<std::int64_t, 2> all{};
    for (const Group& g : groups_of(world, player)) {
        for (const std::uint8_t w : g.wares) {
            const sim::FlowTotals t = sim::goods_totals(world, scope, w, window);
            all[0] += static_cast<std::int64_t>(t.of(GoodsFlow::SoldValue));
            all[1] += static_cast<std::int64_t>(t.of(GoodsFlow::BoughtValue));
            trader[0] += static_cast<std::int64_t>(t.of(GoodsFlow::TraderSoldValue));
            trader[1] += static_cast<std::int64_t>(t.of(GoodsFlow::TraderBoughtValue));
            if (t.of(GoodsFlow::Sold) != 0 || t.of(GoodsFlow::Bought) != 0) {
                lines.push_back(Line{w, t});
            }
        }
    }

    // The partners at the foot, the goods above them.
    constexpr std::int32_t kPartnerH = 22;
    const std::int32_t partners_y = kFooterY - 14 - 4 * kPartnerH;
    m_visible_rows = static_cast<std::size_t>(std::max(1, (partners_y - 10 - kTableY) / kRowH));
    m_row_count = lines.size();
    m_scroll = std::min(m_scroll, m_row_count > m_visible_rows ? m_row_count - m_visible_rows : 0);

    constexpr std::int32_t kNameX = 54;
    constexpr std::int32_t kColW = 80;
    constexpr std::array<std::pair<std::int32_t, std::string_view>, 6> kCols = {{
        {304, "stats.trade.bought"},
        {384, "stats.trade.paid"},
        {464, "stats.trade.price"},
        {564, "stats.trade.sold"},
        {644, "stats.trade.earned"},
        {722, "stats.trade.price"},
    }};
    above.text(Rect{kMargin + 2, kHeaderY, 120, 20}, ui::tr("stats.col.good"), Align::Left, st.dim, kFontSmall);
    for (const auto& [right, key] : kCols) {
        above.text(Rect{right - kColW, kHeaderY, kColW, 20}, ui::tr(key), Align::Right, st.dim, kFontSmall);
    }
    below.fill(Rect{kMargin, kTableY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    if (lines.empty()) {
        above.text(Rect{kMargin, kTableY + 20, kWidth - 2 * kMargin, 30}, ui::tr("stats.trade.empty"), Align::Centre,
                   st.dim);
    }
    const auto price = [](std::uint64_t gold, std::uint64_t units) {
        return units < assets::kStockUnitsPerTon ? std::string("-")
                                                 : std::to_string(gold * assets::kStockUnitsPerTon / units);
    };
    for (std::size_t i = 0; i < m_visible_rows && m_scroll + i < lines.size(); ++i) {
        const Line& l = lines[m_scroll + i];
        const sim::FlowTotals& t = l.totals;
        const std::int32_t y = kTableY + static_cast<std::int32_t>(i) * kRowH;
        const Rect row{kMargin, y + 1, kScrollX - 6 - kMargin, kRowH - 2};
        if ((m_scroll + i) % 2 == 1) {
            below.fill(row, s.band, true);
        }
        m_form.row(kRow + l.ware, row, false);
        below.fill(Rect{kMargin + 2, y + 2, 30, kRowH - 4}, s.dark);
        above.sprite(Rect{kMargin + 3, y + 3, 28, kRowH - 6}, ware_icon(world, l.ware));
        above.text(Rect{kNameX, y, kCols[0].first - kColW - kNameX, kRowH}, ware_name(world, l.ware), Align::Left,
                   st.text, kFontMedium);
        const std::uint64_t bought = t.of(GoodsFlow::Bought);
        const std::uint64_t sold = t.of(GoodsFlow::Sold);
        const std::array<std::string, 6> values = {
            bought != 0 ? std::to_string(tons(bought)) : "-",
            bought != 0 ? std::to_string(t.of(GoodsFlow::BoughtValue)) : "-",
            price(t.of(GoodsFlow::BoughtValue), bought),
            sold != 0 ? std::to_string(tons(sold)) : "-",
            sold != 0 ? std::to_string(t.of(GoodsFlow::SoldValue)) : "-",
            price(t.of(GoodsFlow::SoldValue), sold),
        };
        for (std::size_t c = 0; c < values.size(); ++c) {
            const std::int32_t ink = c == 1 && bought != 0 ? ui::kTextInkRed
                                     : c == 4 && sold != 0 ? ui::kTextInkGreen
                                                           : (values[c] == "-" ? st.dim : st.text);
            above.text(Rect{kCols[c].first - kColW, y, kColW, kRowH}, values[c], Align::Right, ink, kFontMedium);
        }
    }
    layout_scroll_bar(below);

    // Who the city traded with: the free trader and the other players' ships.
    below.fill(Rect{kMargin, partners_y - 4, kWidth - 2 * kMargin, 1}, s.shade);
    constexpr std::int32_t kEarnedR = 420;
    constexpr std::int32_t kPaidR = 540;
    constexpr std::int32_t kBalanceR = 660;
    const auto partner = [&](std::int32_t y, const std::string& name, std::int64_t earned, std::int64_t paid,
                             std::int32_t tone) {
        above.text(Rect{kMargin + 6, y, 260, kPartnerH}, name, Align::Left, tone, kFontMedium);
        above.text(Rect{kEarnedR - 100, y, 100, kPartnerH}, std::to_string(earned), Align::Right,
                   earned != 0 ? ui::kTextInkGreen : st.dim, kFontMedium);
        above.text(Rect{kPaidR - 100, y, 100, kPartnerH}, std::to_string(paid), Align::Right,
                   paid != 0 ? ui::kTextInkRed : st.dim, kFontMedium);
        const std::int64_t balance = earned - paid;
        above.text(Rect{kBalanceR - 100, y, 100, kPartnerH}, number_text(balance, true), Align::Right,
                   balance > 0   ? ui::kTextInkGreen
                   : balance < 0 ? ui::kTextInkRed
                                 : st.dim,
                   kFontMedium);
    };
    above.text(Rect{kMargin + 6, partners_y, 260, kPartnerH}, ui::tr("stats.trade.partner"), Align::Left, st.dim,
               kFontSmall);
    above.text(Rect{kEarnedR - 100, partners_y, 100, kPartnerH}, ui::tr("stats.trade.earned"), Align::Right, st.dim,
               kFontSmall);
    above.text(Rect{kPaidR - 100, partners_y, 100, kPartnerH}, ui::tr("stats.trade.paid"), Align::Right, st.dim,
               kFontSmall);
    above.text(Rect{kBalanceR - 100, partners_y, 100, kPartnerH}, ui::tr("stats.trade.balance"), Align::Right, st.dim,
               kFontSmall);
    partner(partners_y + kPartnerH, ui::tr("stats.trade.trader"), trader[0], trader[1], st.text);
    partner(partners_y + 2 * kPartnerH, ui::tr("stats.trade.players"), all[0] - trader[0], all[1] - trader[1], st.text);
    below.fill(Rect{kMargin + 6, partners_y + 3 * kPartnerH, kBalanceR - kMargin - 6, 1}, s.shade);
    partner(partners_y + 3 * kPartnerH, ui::tr("stats.trade.total"), all[0], all[1], st.text);

    below.fill(Rect{kMargin, kFooterY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    above.paragraph(Rect{kMargin + 2, kFooterY, kWidth - 2 * kMargin, 34},
                    ui::tr("stats.trade.hint", {std::to_string(window)}), st.dim, kFontSmall, 17);
}

void GoodsWindow::layout_finances(const sim::World& world, const assets::BuildingTable& buildings,
                                  const assets::FigureTable& figures, sim::PlayerId player, Painter& below,
                                  Painter& above) {
    const GoodsStyle& s = m_style;
    const ui::FormStyle& st = s.form;
    const sim::GoodsScope scope{player, m_island};
    m_row_count = 0;

    // Now, as the city window computes it.
    const std::vector<std::int32_t> costs = sim::running_costs(world, buildings);
    std::array<std::int64_t, 4> now{};  // taxes, running costs, sales, purchases
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        const sim::City& c = world.cities()[i];
        if (!scope.covers(c)) {
            continue;
        }
        now[0] += sim::tax_income(c, buildings);
        now[1] += i < costs.size() ? costs[i] : 0;
        now[2] += c.sales / 480;
        now[3] += c.purchases / 480;
    }
    // The soldiers and ships belong to the player, not to an island.
    std::optional<std::int64_t> military;
    if (!m_island) {
        const std::vector<std::uint32_t> upkeep = sim::military_upkeep(world, figures);
        military = player < upkeep.size() ? upkeep[player] : 0;
    }
    // On average over the time chosen, from the samples of the minutes.
    const std::vector<sim::CitySample> minutes_list = sim::city_history(world, scope, false);
    const std::size_t n = std::min<std::size_t>(minutes(), minutes_list.size());
    std::array<std::int64_t, 4> average{};
    for (std::size_t k = minutes_list.size() - n; k < minutes_list.size(); ++k) {
        const sim::CitySample& m = minutes_list[k];
        average[0] += m.of(CityFigure::Taxes);
        average[1] += m.of(CityFigure::RunningCosts);
        average[2] += m.of(CityFigure::Sales);
        average[3] += m.of(CityFigure::Purchases);
    }
    for (std::int64_t& v : average) {
        v = n != 0 ? v / static_cast<std::int64_t>(n) : 0;
    }

    constexpr std::int32_t kLineH = 24;
    constexpr std::int32_t kNowR = 420;
    constexpr std::int32_t kAverageR = 560;
    above.text(Rect{kNowR - 110, kHeaderY, 110, 20}, ui::tr("stats.fin.now"), Align::Right, st.dim, kFontSmall);
    above.text(Rect{kAverageR - 130, kHeaderY, 130, 20}, ui::tr("stats.fin.average", {std::to_string(minutes())}),
               Align::Right, st.dim, kFontSmall);
    below.fill(Rect{kMargin, kTableY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    std::int32_t y = kTableY;
    const auto line = [&](std::string_view key, std::optional<std::int64_t> value, std::optional<std::int64_t> avg,
                          int sign, std::uint8_t colour) {
        // sign: +1 income, -1 expense, 0 the balance (coloured by its own sign).
        below.fill(Rect{kMargin + 4, y + 8, 10, 8}, colour);
        above.text(Rect{kMargin + 20, y, 260, kLineH}, ui::tr(key), Align::Left, st.text, kFontMedium);
        const auto ink = [&](std::int64_t v) {
            const std::int64_t signed_v = sign == 0 ? v : sign * v;
            return v == 0 ? st.dim : signed_v > 0 ? ui::kTextInkGreen : ui::kTextInkRed;
        };
        above.text(Rect{kNowR - 100, y, 100, kLineH}, value ? number_text(*value, sign == 0) : std::string("-"),
                   Align::Right, value ? ink(*value) : st.dim, kFontMedium);
        above.text(Rect{kAverageR - 100, y, 100, kLineH},
                   avg && n != 0 ? number_text(*avg, sign == 0) : std::string("-"), Align::Right,
                   avg && n != 0 ? ink(*avg) : st.dim, kFontMedium);
        y += kLineH;
    };
    const std::int64_t balance = now[0] - now[1] - military.value_or(0) + now[2] - now[3];
    const std::int64_t average_balance = average[0] - average[1] + average[2] - average[3];
    line("stats.fin.taxes", now[0], average[0], 1, s.series[0]);
    line("stats.fin.costs", now[1], average[1], -1, s.series[1]);
    if (military) {
        line("stats.fin.military", military, std::nullopt, -1, s.shade);
    }
    line("stats.fin.sales", now[2], average[2], 1, s.series[2]);
    line("stats.fin.purchases", now[3], average[3], -1, s.series[3]);
    below.fill(Rect{kMargin + 20, y + 1, kAverageR - kMargin - 20, 1}, s.shade);
    y += 4;
    line("stats.fin.balance", balance, military ? std::nullopt : std::optional<std::int64_t>(average_balance), 0,
         s.dark);

    // Over time (without the military, which has no history).
    const std::vector<sim::CitySample> history = m_long_chart ? sim::city_history(world, scope, true) : minutes_list;
    std::vector<Series> series = {
        {s.series[0], ui::tr("stats.fin.taxes"), {}},    {s.series[1], ui::tr("stats.fin.costs"), {}},
        {s.series[2], ui::tr("stats.fin.sales"), {}},    {s.series[3], ui::tr("stats.fin.purchases"), {}},
        {s.dark, ui::tr("stats.fin.balance_short"), {}},
    };
    for (const sim::CitySample& m : history) {
        series[0].values.push_back(m.of(CityFigure::Taxes));
        series[1].values.push_back(m.of(CityFigure::RunningCosts));
        series[2].values.push_back(m.of(CityFigure::Sales));
        series[3].values.push_back(m.of(CityFigure::Purchases));
        series[4].values.push_back(std::int64_t{m.of(CityFigure::Taxes)} - m.of(CityFigure::RunningCosts) +
                                   m.of(CityFigure::Sales) - m.of(CityFigure::Purchases));
    }
    const std::int32_t chart_top = y + 10;
    below.fill(Rect{kMargin, chart_top - 4, kWidth - 2 * kMargin, 1}, s.shade);
    layout_chart(chart_top, series, below, above);
    below.fill(Rect{kMargin, kFooterY - 4, kWidth - 2 * kMargin, 1}, s.shade);
    above.paragraph(Rect{kMargin + 2, kFooterY, kWidth - 2 * kMargin, 34}, ui::tr("stats.fin.hint"), st.dim, kFontSmall,
                    17);
}

}  // namespace o1602::app
