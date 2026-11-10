// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/goods_ledger.hpp"

#include <algorithm>
#include <climits>

namespace o1602::sim {

namespace {

std::uint16_t clamp_sample(std::uint64_t v) noexcept {
    return static_cast<std::uint16_t>(std::min<std::uint64_t>(v, GoodsLedger::kMaxSample));
}

}  // namespace

std::uint64_t FlowTotals::in() const noexcept {
    std::uint64_t sum = 0;
    for (std::size_t f = 0; f < kGoodsFlows; ++f) {
        sum += is_inflow(static_cast<GoodsFlow>(f)) ? flow[f] : 0;
    }
    return sum;
}

std::uint64_t FlowTotals::out() const noexcept {
    std::uint64_t sum = 0;
    for (std::size_t f = 0; f < kGoodsFlows; ++f) {
        sum += is_outflow(static_cast<GoodsFlow>(f)) ? flow[f] : 0;
    }
    return sum;
}

FlowTotals& FlowTotals::operator+=(const FlowTotals& other) noexcept {
    for (std::size_t f = 0; f < kGoodsFlows; ++f) {
        flow[f] += other.flow[f];
    }
    minutes = std::max(minutes, other.minutes);
    return *this;
}

void CitySample::set(CityFigure f, std::int64_t v) noexcept {
    value[static_cast<std::size_t>(f)] = static_cast<std::int32_t>(std::clamp<std::int64_t>(v, INT32_MIN, INT32_MAX));
}

void CitySample::set_class(std::size_t c, std::int64_t v) noexcept {
    if (c < kCityClasses) {
        set(static_cast<CityFigure>(static_cast<std::size_t>(CityFigure::Class) + c), v);
    }
}

void CityHistory::close_minute(const CitySample& sample) {
    State& s = m_state;
    s.minutes[s.closed % kMinutes] = sample;
    for (std::size_t f = 0; f < kCityFigures; ++f) {
        s.quarter[f] += sample.value[f];
    }
    if ((s.closed + 1) % kQuarterMinutes == 0) {
        CitySample q;
        for (std::size_t f = 0; f < kCityFigures; ++f) {
            q.value[f] = static_cast<std::int32_t>(s.quarter[f] / static_cast<std::int64_t>(kQuarterMinutes));
        }
        s.quarters[(s.closed / kQuarterMinutes) % kQuarters] = q;
        s.quarter = {};
    }
    ++s.closed;
}

std::vector<CitySample> CityHistory::last_minutes() const {
    const State& s = m_state;
    const std::uint32_t n = std::min(s.closed, static_cast<std::uint32_t>(kMinutes));
    std::vector<CitySample> out;
    out.reserve(n);
    for (std::uint32_t k = n; k >= 1; --k) {
        out.push_back(s.minutes[(s.closed - k) % kMinutes]);
    }
    return out;
}

std::vector<CitySample> CityHistory::last_quarters() const {
    const State& s = m_state;
    const std::uint32_t done = s.closed / kQuarterMinutes;
    const std::uint32_t n = std::min(done, static_cast<std::uint32_t>(kQuarters));
    std::vector<CitySample> out;
    out.reserve(n);
    for (std::uint32_t k = n; k >= 1; --k) {
        out.push_back(s.quarters[(done - k) % kQuarters]);
    }
    return out;
}

GoodsLedger::Ware& GoodsLedger::ware(std::uint8_t ware) {
    if (ware >= m_wares.size()) {
        m_wares.resize(std::size_t{ware} + 1);
    }
    return m_wares[ware];
}

void GoodsLedger::add(std::uint8_t ware, GoodsFlow flow, std::uint32_t amount) {
    if (amount == 0) {
        return;
    }
    Ware& w = this->ware(ware);
    std::uint32_t& count = w.current[static_cast<std::size_t>(flow)];
    count = amount > UINT32_MAX - count ? UINT32_MAX : count + amount;
    w.used = true;
}

void GoodsLedger::set_warned(std::uint8_t ware, bool on) {
    if (on || ware < m_wares.size()) {
        this->ware(ware).warned = on;
    }
}

void GoodsLedger::close_minute(const std::vector<std::uint32_t>& stock) {
    // Goods in store that were never booked get samples too: their stock shows in the history.
    if (m_wares.size() < stock.size()) {
        m_wares.resize(stock.size());
    }
    const std::size_t slot = m_closed % kMinutes;
    const bool quarter_ends = (m_closed + 1) % kQuarterMinutes == 0;
    const std::size_t quarter_slot = (m_closed / kQuarterMinutes) % kQuarters;
    for (std::size_t i = 0; i < m_wares.size(); ++i) {
        Ware& w = m_wares[i];
        const std::uint32_t in_store = i < stock.size() ? stock[i] : 0;
        w.used = w.used || in_store != 0;
        Sample s;
        for (std::size_t f = 0; f < kGoodsFlows; ++f) {
            s.flow[f] = clamp_sample(w.current[f]);
            w.quarter[f] += s.flow[f];
        }
        s.stock = in_store;
        w.minutes[slot] = s;
        w.current = {};
        if (quarter_ends) {
            Sample q;
            for (std::size_t f = 0; f < kGoodsFlows; ++f) {
                q.flow[f] = clamp_sample(w.quarter[f] / kQuarterMinutes);
            }
            q.stock = in_store;
            w.quarters[quarter_slot] = q;
            w.quarter = {};
        }
    }
    ++m_closed;
}

FlowTotals GoodsLedger::totals(std::uint8_t ware, std::uint32_t minutes) const {
    FlowTotals t;
    if (ware >= m_wares.size() || minutes == 0) {
        return t;
    }
    const Ware& w = m_wares[ware];
    if (minutes <= kMinutes) {
        const std::uint32_t n = std::min({minutes, m_closed, static_cast<std::uint32_t>(kMinutes)});
        for (std::uint32_t k = 1; k <= n; ++k) {
            const Sample& s = w.minutes[(m_closed - k) % kMinutes];
            for (std::size_t f = 0; f < kGoodsFlows; ++f) {
                t.flow[f] += s.flow[f];
            }
        }
        t.minutes = n;
        return t;
    }
    const std::uint32_t done = m_closed / kQuarterMinutes;
    const auto wanted = static_cast<std::uint32_t>((minutes + kQuarterMinutes - 1) / kQuarterMinutes);
    const std::uint32_t n = std::min({wanted, done, static_cast<std::uint32_t>(kQuarters)});
    for (std::uint32_t k = 1; k <= n; ++k) {
        const Sample& s = w.quarters[(done - k) % kQuarters];
        for (std::size_t f = 0; f < kGoodsFlows; ++f) {
            t.flow[f] += std::uint64_t{s.flow[f]} * kQuarterMinutes;
        }
    }
    t.minutes = n * static_cast<std::uint32_t>(kQuarterMinutes);
    return t;
}

std::vector<GoodsLedger::Sample> GoodsLedger::last_minutes(std::uint8_t ware) const {
    std::vector<Sample> out;
    if (ware >= m_wares.size()) {
        return out;
    }
    const std::uint32_t n = std::min(m_closed, static_cast<std::uint32_t>(kMinutes));
    out.reserve(n);
    for (std::uint32_t k = n; k >= 1; --k) {
        out.push_back(m_wares[ware].minutes[(m_closed - k) % kMinutes]);
    }
    return out;
}

std::vector<GoodsLedger::Sample> GoodsLedger::last_quarters(std::uint8_t ware) const {
    std::vector<Sample> out;
    if (ware >= m_wares.size()) {
        return out;
    }
    const std::uint32_t done = m_closed / kQuarterMinutes;
    const std::uint32_t n = std::min(done, static_cast<std::uint32_t>(kQuarters));
    out.reserve(n);
    for (std::uint32_t k = n; k >= 1; --k) {
        out.push_back(m_wares[ware].quarters[(done - k) % kQuarters]);
    }
    return out;
}

}  // namespace o1602::sim
