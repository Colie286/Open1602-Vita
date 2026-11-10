// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cities and their buildings: CITY, CSTA (trade statistics), MLCK (the clocks of the material
// locks), CPOP (the population messages), PROD (the production list with its free slots), HOUS, GROW and COST
// (shipyards and castles).
#include <utility>

#include "section_io.hpp"

namespace o1602::sim::save {

namespace {

template <class A, class C>
void io_city(A& a, C& c) {
    a.u16(c.island);
    a.u8(c.index);
    a.u8(c.owner);
    a.string(c.name);
    a.u32s(c.goods);
    // The warehouse: whether the city has one, and its position (0, 0 without one).
    bool store = c.warehouse_x.has_value() && c.warehouse_y.has_value();
    std::uint16_t x = c.warehouse_x.value_or(0);
    std::uint16_t y = c.warehouse_y.value_or(0);
    a.boolean(store);
    a.u16(x);
    a.u16(y);
    if constexpr (A::kReading) {
        if (store) {
            c.warehouse_x = x;
            c.warehouse_y = y;
        }
    }
    a.u32s(c.reserved);
    a.u32s(c.incoming);
    a.u32(c.capacity);
    a.u32s(c.inhabitants);
    a.u8s(c.satisfaction);
    a.u8s(c.tax_rate);
    a.list(c.supply, 20, [&](auto& s) {
        a.u64(s.demand);
        a.u64(s.supplied);
        a.u8(s.ratio);
        for (auto& h : s.history) {
            a.u8(h);
        }
    });
    a.u8(c.food_ratio);
    a.u16(c.satisfaction_bonus);
    a.list(c.upgrade_queue, 8, [&](auto& u) {
        a.u32(u.inhabitants);
        a.u16(u.x);
        a.u16(u.y);
    });
    a.u8(c.epoch);
    a.boolean(c.material_lock);
    a.u8(c.worst_good);
    a.u64(c.next_event_ms);
}

/// CSTA: the statistics of a City (writing) or of Decoded::CityStats (reading), which has the same
/// names.
template <class A, class C>
void io_city_stats(A& a, C& c) {
    a.u16(c.island);
    a.u8(c.index);
    a.i64(c.purchases);
    a.i64(c.sales);
}

/// MLCK: the clock of a City's material lock (writing) or of Decoded::MaterialClock (reading).
template <class A, class C>
void io_material_clock(A& a, C& c) {
    a.u16(c.island);
    a.u8(c.index);
    a.u64(c.material_reminder_ms);
}

/// CPOP: the last population message of a City (writing) or of Decoded::PopulationMark (reading).
template <class A, class C>
void io_population_mark(A& a, C& c) {
    a.u16(c.island);
    a.u8(c.index);
    a.u8(c.population_message);
}

/// CGUP: the ships that gave up their way to the Kontor of a City (writing) or of
/// Decoded::GiveUpCount (reading).
template <class A, class C>
void io_give_up_count(A& a, C& c) {
    a.u16(c.island);
    a.u8(c.index);
    a.u8(c.ships_gave_up);
}

/// GLDG: one good of a GoodsLedger.
template <class A, class W>
void io_ledger_ware(A& a, W& w) {
    a.boolean(w.warned);
    for (auto& c : w.current) {
        a.u32(c);
    }
    for (auto& c : w.quarter) {
        a.u32(c);
    }
    const auto samples = [&](auto& ring) {
        for (auto& s : ring) {
            for (auto& f : s.flow) {
                a.u16(f);
            }
            a.u32(s.stock);
        }
    };
    samples(w.minutes);
    samples(w.quarters);
}

/// GLDG: a city's CityHistory.
template <class A, class S>
void io_city_history(A& a, S& s) {
    a.u32(s.closed);
    const auto samples = [&](auto& ring) {
        for (auto& sample : ring) {
            for (auto& v : sample.value) {
                a.i32(v);
            }
        }
    };
    samples(s.minutes);
    samples(s.quarters);
    for (auto& v : s.quarter) {
        a.i64(v);
    }
}

template <class A, class P>
void io_production(A& a, P& p) {
    a.u16(p.island);
    a.u16(p.x);
    a.u16(p.y);
    a.boolean(p.used);
    a.boolean(p.enabled);
    a.u8(p.rate);
    a.u8(p.anim_phase);
    a.u8(p.idle_cycles);
    a.u8(p.flags);
    a.u16(p.countdown);
    a.u16(p.raw);
    a.u16(p.raw2);
    a.u16(p.stock);
    a.u16(p.produced);
    a.u16(p.elapsed);
    a.u16(p.reserved);
    if constexpr (A::kReading) {
        if (!p.used) {
            p.enabled = false;  // a free slot never runs (World::remove_production switches it off)
        }
    }
}

/// The production list and its free slots; a free slot must name an unused entry.
template <class A, class L, class F>
void io_productions(A& a, L& list, F& free) {
    a.list(list, 26, [&](auto& p) { io_production(a, p); });
    a.u32s(free);
    if constexpr (A::kReading) {
        for (const std::uint32_t f : free) {
            if (f >= list.size() || list[f].used) {
                a.fail();
            }
        }
    }
}

template <class A, class H>
void io_house(A& a, H& h) {
    a.u16(h.island);
    a.u16(h.x);
    a.u16(h.y);
    a.u8(h.city);
    a.u8(h.resident_class);
    a.u32(h.inhabitants64);
    a.u8(h.timer_group);
    a.u8(h.epoch);
    a.u8(h.queue_epoch);
    a.u8(h.market_distance);
    a.boolean(h.market);
    a.boolean(h.services_complete);
    a.u16(h.services);
}

template <class A, class E>
void io_growth(A& a, E& e) {
    a.u16(e.island);
    a.u16(e.x);
    a.u16(e.y);
    a.u8(e.growth_class);
    a.u8(e.seen);
    a.u8(e.stage);
}

/// COST holds where the buildings stand; FLEE and ARMY hold their state in the same order.
template <class A, class B>
void io_cost_building(A& a, B& b) {
    a.u8(b.kind);
    a.u16(b.island);
    a.u16(b.x);
    a.u16(b.y);
    a.boolean(b.enabled);
}

}  // namespace

void write_cities(Writer& out, const World& w) {
    out.list(w.cities(), 16, [&](const City& c) { io_city(out, c); });
}

std::vector<City> read_cities(Reader& in) {
    std::vector<City> list;
    in.list(list, 16, [&](City& c) { io_city(in, c); });
    return list;
}

void write_city_state(Writer& out, const World& w) {
    out.list(w.cities(), 19, [&](const City& c) { io_city_stats(out, c); });
}

std::vector<Decoded::CityStats> read_city_state(Reader& in) {
    std::vector<Decoded::CityStats> list;
    in.list(list, 19, [&](Decoded::CityStats& c) { io_city_stats(in, c); });
    return list;
}

void write_material_clocks(Writer& out, const World& w) {
    out.list(w.cities(), 11, [&](const City& c) { io_material_clock(out, c); });
}

void write_population_marks(Writer& out, const World& w) {
    out.list(w.cities(), 4, [&](const City& c) { io_population_mark(out, c); });
}

void write_give_up_counts(Writer& out, const World& w) {
    out.list(w.cities(), 4, [&](const City& c) { io_give_up_count(out, c); });
}

std::vector<Decoded::GiveUpCount> read_give_up_counts(Reader& in) {
    std::vector<Decoded::GiveUpCount> list;
    in.list(list, 4, [&](Decoded::GiveUpCount& c) { io_give_up_count(in, c); });
    return list;
}

void write_ledgers(Writer& out, const World& w) {
    std::vector<const City*> kept;
    for (const City& c : w.cities()) {
        if (c.ledger.minutes_closed() != 0 || !c.ledger.wares().empty() || c.history.minutes_closed() != 0) {
            kept.push_back(&c);
        }
    }
    out.list(kept, 12, [&](const City* c) {
        out.u16(c->island);
        out.u8(c->index);
        out.u32(c->ledger.minutes_closed());
        // Only the goods that ever moved or were in store.
        std::vector<std::uint8_t> used;
        for (std::size_t i = 0; i < c->ledger.wares().size(); ++i) {
            if (c->ledger.wares()[i].used) {
                used.push_back(static_cast<std::uint8_t>(i));
            }
        }
        out.list(used, 1, [&](std::uint8_t ware) {
            out.u8(ware);
            io_ledger_ware(out, c->ledger.wares()[ware]);
        });
        // The figures of the city window: only human players' cities have them.
        const bool history = c->history.minutes_closed() != 0;
        out.boolean(history);
        if (history) {
            CityHistory::State s = c->history.state();
            io_city_history(out, s);
        }
    });
}

std::vector<Decoded::CityLedger> read_ledgers(Reader& in) {
    std::vector<Decoded::CityLedger> list;
    in.list(list, 12, [&](Decoded::CityLedger& c) {
        in.u16(c.island);
        in.u8(c.index);
        std::uint32_t closed = 0;
        in.u32(closed);
        std::vector<std::pair<std::uint8_t, GoodsLedger::Ware>> read;
        in.list(read, 1, [&](std::pair<std::uint8_t, GoodsLedger::Ware>& e) {
            in.u8(e.first);
            io_ledger_ware(in, e.second);
            e.second.used = true;
        });
        std::vector<GoodsLedger::Ware> wares;
        for (auto& [ware, w] : read) {
            if (ware >= wares.size()) {
                wares.resize(std::size_t{ware} + 1);
            }
            wares[ware] = std::move(w);
        }
        c.ledger.restore(std::move(wares), closed);
        bool history = false;
        in.boolean(history);
        if (history) {
            CityHistory::State s;
            io_city_history(in, s);
            c.history.restore(std::move(s));
        }
    });
    return list;
}

std::vector<Decoded::PopulationMark> read_population_marks(Reader& in) {
    std::vector<Decoded::PopulationMark> list;
    in.list(list, 4, [&](Decoded::PopulationMark& c) { io_population_mark(in, c); });
    return list;
}

std::vector<Decoded::MaterialClock> read_material_clocks(Reader& in) {
    std::vector<Decoded::MaterialClock> list;
    in.list(list, 11, [&](Decoded::MaterialClock& c) { io_material_clock(in, c); });
    return list;
}

void write_productions(Writer& out, const World& w) {
    io_productions(out, w.productions(), w.free_productions());
}

Decoded::ProductionPart read_productions(Reader& in) {
    Decoded::ProductionPart part;
    io_productions(in, part.list, part.free);
    return part;
}

void write_houses(Writer& out, const World& w) {
    out.list(w.houses(), 20, [&](const House& h) { io_house(out, h); });
}

std::vector<House> read_houses(Reader& in) {
    std::vector<House> list;
    in.list(list, 20, [&](House& h) { io_house(in, h); });
    return list;
}

void write_growth(Writer& out, const World& w) {
    out.list(w.growth().entries, 9, [&](const GrowthEntry& e) { io_growth(out, e); });
}

std::vector<GrowthEntry> read_growth(Reader& in) {
    std::vector<GrowthEntry> list;
    in.list(list, 9, [&](GrowthEntry& e) { io_growth(in, e); });
    return list;
}

void write_costs(Writer& out, const World& w) {
    out.list(w.cost_buildings(), 8, [&](const CostBuilding& b) { io_cost_building(out, b); });
}

std::vector<CostBuilding> read_costs(Reader& in) {
    std::vector<CostBuilding> list;
    in.list(list, 8, [&](CostBuilding& b) { io_cost_building(in, b); });
    return list;
}

}  // namespace o1602::sim::save
