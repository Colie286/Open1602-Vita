// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/goods_statistics.hpp"

#include <algorithm>

#include "o1602/sim/mission.hpp"
#include "o1602/sim/production.hpp"

namespace o1602::sim {

std::optional<std::uint32_t> minutes_left(std::uint64_t stock, const FlowTotals& totals) noexcept {
    const std::int64_t net = totals.net();
    if (net >= 0 || totals.minutes == 0) {
        return std::nullopt;
    }
    // stock / (deficit / minutes), rounded down.
    const auto deficit = static_cast<std::uint64_t>(-net);
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(stock * totals.minutes / deficit, UINT32_MAX));
}

namespace {

/// The early warning for one city (after its minute was closed).
void warn_of_shortages(City& city, const std::vector<std::uint8_t>& consumer_goods, std::vector<CityEvent>* events) {
    if (city.ledger.minutes_closed() < kWarnMinMinutes) {
        return;
    }
    for (const std::uint8_t w : consumer_goods) {
        const FlowTotals t = city.ledger.totals(w, kWarnWindowMinutes);
        const std::optional<std::uint32_t> left = minutes_left(city.stock(w), t);
        if (city.ledger.warned(w)) {
            if (!left || *left >= kWarnClearMinutes) {
                city.ledger.set_warned(w, false);
            }
            continue;
        }
        // Only what the people use, and before it is gone (an empty store has its own message).
        if (t.of(GoodsFlow::Residents) == 0 || city.stock(w) == 0 || !left || *left >= kWarnBelowMinutes) {
            continue;
        }
        city.ledger.set_warned(w, true);
        if (events != nullptr) {
            CityEvent e{CityEvent::Kind::RunningLow, city.island, city.index, city.owner, w};
            e.minutes = static_cast<std::uint16_t>(std::min<std::uint32_t>(*left, 0xFFFF));
            events->push_back(e);
        }
    }
}

}  // namespace

void close_goods_minute(World& world, const assets::BuildingTable& buildings, std::vector<CityEvent>* events) {
    const std::vector<std::uint8_t> goods = consumer_goods(buildings);
    std::optional<std::vector<std::int32_t>> costs;  // only once a human player's city needs them
    std::optional<std::vector<std::uint32_t>> workers;
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        const City& c = world.cities()[i];
        City& city = *world.city(c.island, c.index);
        city.ledger.close_minute(city.goods);
        const Player* owner = world.player(city.owner);
        if (owner == nullptr || owner->role != PlayerRole::Human) {
            continue;  // nobody ever sees the figures of computer players and natives
        }
        warn_of_shortages(city, goods, events);
        if (!costs) {
            costs = running_costs(world, buildings);
            workers = city_workers(world, buildings);
        }
        CitySample s;
        s.set(CityFigure::Taxes, tax_income(city, buildings));
        s.set(CityFigure::RunningCosts, i < costs->size() ? (*costs)[i] : 0);
        s.set(CityFigure::Sales, city.sales / 480);
        s.set(CityFigure::Purchases, city.purchases / 480);
        s.set(CityFigure::Inhabitants, city_einwohner(city, i < workers->size() ? (*workers)[i] : 0));
        for (std::size_t k = 0; k < city.inhabitants.size(); ++k) {
            s.set_class(k, city.inhabitants[k]);
        }
        city.history.close_minute(s);
    }
}

std::vector<CitySample> city_history(const World& world, const GoodsScope& scope, bool quarters) {
    // The cities' samples of the same minute added up, aligned at the newest.
    std::vector<CitySample> sum;
    for (const City& c : world.cities()) {
        if (!scope.covers(c)) {
            continue;
        }
        const std::vector<CitySample> own = quarters ? c.history.last_quarters() : c.history.last_minutes();
        if (own.size() > sum.size()) {
            sum.insert(sum.begin(), own.size() - sum.size(), CitySample{});
        }
        const std::size_t offset = sum.size() - own.size();
        for (std::size_t k = 0; k < own.size(); ++k) {
            for (std::size_t f = 0; f < kCityFigures; ++f) {
                sum[offset + k].value[f] += own[k].value[f];
            }
        }
    }
    return sum;
}

std::vector<std::uint16_t> goods_islands(const World& world, PlayerId player) {
    std::vector<std::uint16_t> islands;
    for (const City& c : world.cities()) {
        if (c.owner == player) {
            islands.push_back(c.island);
        }
    }
    std::sort(islands.begin(), islands.end());
    islands.erase(std::unique(islands.begin(), islands.end()), islands.end());
    return islands;
}

FlowTotals goods_totals(const World& world, const GoodsScope& scope, std::uint8_t ware, std::uint32_t minutes) {
    FlowTotals sum;
    for (const City& c : world.cities()) {
        if (scope.covers(c)) {
            sum += c.ledger.totals(ware, minutes);
        }
    }
    return sum;
}

std::uint64_t goods_stock(const World& world, const GoodsScope& scope, std::uint8_t ware) {
    std::uint64_t sum = 0;
    for (const City& c : world.cities()) {
        if (scope.covers(c)) {
            sum += c.stock(ware);
        }
    }
    return sum;
}

std::uint64_t goods_capacity(const World& world, const GoodsScope& scope) {
    std::uint64_t sum = 0;
    for (const City& c : world.cities()) {
        if (scope.covers(c)) {
            sum += c.capacity;
        }
    }
    return sum;
}

std::uint64_t GoodsSample::in() const noexcept {
    std::uint64_t sum = 0;
    for (std::size_t f = 0; f < kGoodsFlows; ++f) {
        sum += is_inflow(static_cast<GoodsFlow>(f)) ? flow[f] : 0;
    }
    return sum;
}

std::uint64_t GoodsSample::out() const noexcept {
    std::uint64_t sum = 0;
    for (std::size_t f = 0; f < kGoodsFlows; ++f) {
        sum += is_outflow(static_cast<GoodsFlow>(f)) ? flow[f] : 0;
    }
    return sum;
}

std::vector<GoodsSample> goods_history(const World& world, const GoodsScope& scope, std::uint8_t ware, bool quarters) {
    std::vector<GoodsSample> out;
    for (const City& c : world.cities()) {
        if (!scope.covers(c)) {
            continue;
        }
        const std::vector<GoodsLedger::Sample> samples =
            quarters ? c.ledger.last_quarters(ware) : c.ledger.last_minutes(ware);
        if (samples.size() > out.size()) {
            out.insert(out.begin(), samples.size() - out.size(), GoodsSample{});
        }
        // Aligned at the newest end: every city closes the same minute.
        const std::size_t offset = out.size() - samples.size();
        for (std::size_t i = 0; i < samples.size(); ++i) {
            GoodsSample& s = out[offset + i];
            for (std::size_t f = 0; f < kGoodsFlows; ++f) {
                s.flow[f] += samples[i].flow[f];
            }
            s.stock += samples[i].stock;
        }
    }
    return out;
}

std::vector<GoodsProducers> goods_producers(const World& world, const assets::BuildingTable& buildings,
                                            const GoodsScope& scope, std::uint8_t ware) {
    std::vector<GoodsProducers> kinds;
    for (const Production& p : world.productions()) {
        if (!p.used || p.island >= world.islands().size()) {
            continue;
        }
        const MapCell& cell = world.islands()[p.island].map.top(p.x, p.y);
        const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
        const City* city = world.city(p.island, cell.city);
        if (b == nullptr || b->ware != ware || !is_producer(*b) || city == nullptr || !scope.covers(*city)) {
            continue;
        }
        auto kind = std::ranges::find(kinds, b->id, &GoodsProducers::building);
        if (kind == kinds.end()) {
            kinds.push_back(GoodsProducers{b->id});
            kind = kinds.end() - 1;
        }
        ++kind->count;
        if (!p.enabled) {
            ++kind->off;
        } else if (p.rate != 0) {
            ++kind->working;
        } else if (world.goods().is_trade(b->raw_ware) && p.raw < b->raw_amount) {
            ++kind->short_of_raw;
        }
        if (p.elapsed != 0) {
            kind->recent += std::uint64_t{p.produced} * 3600 / p.elapsed;
        }
    }
    std::ranges::stable_sort(kinds,
                             [](const GoodsProducers& a, const GoodsProducers& b) { return a.recent > b.recent; });
    return kinds;
}

}  // namespace o1602::sim
