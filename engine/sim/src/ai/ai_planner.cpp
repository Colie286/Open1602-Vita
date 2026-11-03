// SPDX-License-Identifier: GPL-3.0-or-later
//
// The city planner of the computer players: task 5 refreshes the
// current city (Kontor settings, production switches, money) or runs the handler of its stage,
// which asks a fixed list of Needs and queues the first building that is missing.
#include <algorithm>
#include <array>
#include <span>

#include "ai_engine.hpp"
#include "ai_tables.hpp"

#include "o1602/sim/consumption.hpp"

namespace o1602::sim::ai {

namespace {

constexpr std::uint64_t kRefreshMs = 5100;  // a refresh every 5.1 s

}  // namespace

// ---------------------------------------------------------------------------------------------
// Task 5

void Engine::task_develop(AiPlayer& p) {
    if (now_ms() - p.refresh_ms >= kRefreshMs) {
        refresh_city(p);
        return;  // the task stays; the next step runs the stage handler
    }
    pop(p);
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr || peaceful(p)) {
        return;
    }
    if (p.yard_wanted) {
        p.yard_wanted = false;
        if (!p.yard_valid && need_shipyard(p)) {
            return;
        }
    }
    run_stage(p, *c, *r);
}

Cheats Engine::cheats() const {
    return cheats_of(m_world.balance());
}

void Engine::tutorial_money(AiPlayer& p) {
    // At most once a second, money below 3791 gets 379.
    if (now_ms() - p.refresh_ms <= 1000) {
        return;
    }
    p.refresh_ms = now_ms();
    Player& pl = player(p);
    if (pl.money < cheats().steady_below) {
        pl.money = money_plus(pl.money, std::int64_t{cheats().steady_money});
    }
}

bool Engine::peaceful(AiPlayer& p) {
    // A computer a scenario keeps quiet builds nothing while every
    // human holds a peace treaty with it and it likes each of them at least -30. Once one does not,
    // the switch goes for good (op 0x3A).
    Player& self = player(p);
    if (!self.scenario.has(scenario_flag::kPassive)) {
        return false;
    }
    const Diplomacy& d = m_world.diplomacy();
    for (std::size_t h = 0; h < m_world.players().size(); ++h) {
        const Player& other = m_world.players()[h];
        if (other.role != PlayerRole::Human || other.status != PlayerStatus::Playing) {
            continue;
        }
        const auto b = static_cast<PlayerId>(h);
        if (d.relation(p.player, b).peace != Treaty::Signed || d.mood(m_world, p.player, b, 0, now_ms()) < -30) {
            self.scenario.flags &= ~scenario_flag::kPassive;
            log(p, "the scenario's peace is over");
            return false;
        }
    }
    return true;
}

void Engine::refresh_city(AiPlayer& p) {
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c != nullptr && r != nullptr) {
        buy_settings(p, *c, *r);
        sell_settings(p, *c, *r);
        production_switches(p, *c);
    }
    p.refresh_ms = now_ms();
    Player& pl = player(p);
    if (pl.money < cheats().steady_below) {
        pl.money = money_plus(
            pl.money, std::int64_t{cheats().steady_money});  // the original's steady money (cheat), on every difficulty
    }
    if (!threatened(p) && behind(p)) {
        subsidy(p, 1);
    }
}

void Engine::subsidy(AiPlayer& p, std::int32_t m) {
    // Money and goods into the current city while the computer is behind.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return;
    }
    Player& pl = player(p);
    if (pl.money < cheats().subsidy_poor_below) {
        pl.money = money_plus(pl.money, static_cast<std::int64_t>(cheats().subsidy_poor * m));
    } else if (pl.money < cheats().subsidy_below && civ_level(p) >= 1) {
        pl.money = money_plus(pl.money, static_cast<std::int64_t>(cheats().subsidy * m));
    }
    const auto& mat = m_buildings.materials();
    const auto u = static_cast<std::uint32_t>(m);
    if (p.score >= 1 && p.present[kSlotWoodcutter] && c->stock(mat.wood) < cheats().subsidy_wood * kStockUnitsPerTon) {
        add_goods(*c, mat.wood, cheats().subsidy_wood * kStockUnitsPerTon * u);
    }
    if (p.score >= 2 && c->stock(mat.tools) < cheats().subsidy_tools * kStockUnitsPerTon) {
        add_goods(*c, mat.tools, cheats().subsidy_tools * kStockUnitsPerTon * u);
    }
    // The bricks of the original need a score of 20, which never comes.
    if (population(*c, 2) >= 50) {
        for (const ComputerSlot& k : slots()) {
            if (k.subsidy_tons != 0 && r->slots[k.number].count > k.subsidy_above &&
                c->stock(k.subsidy_ware) < 3 * kTon &&
                (k.subsidy_people == 0 || population(*c, k.subsidy_class) >= k.subsidy_people)) {
                add_goods(*c, k.subsidy_ware, k.subsidy_tons * kTon * u);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Kontor settings and production switches

void Engine::buy_settings(AiPlayer& p, City& c, CityPlan& r) {
    const std::uint32_t level = kontor_level(c);
    if (level == 0 || !c.warehouse_x || !c.warehouse_y) {
        return;
    }
    const Tile k = tile(c.island, *c.warehouse_x, *c.warehouse_y);
    const std::uint32_t max_entries = k.valid() ? k.type->max_wares2 : 0;
    struct Want {
        std::uint8_t ware;
        std::uint32_t limit;
    };
    std::vector<Want> wanted;
    std::uint32_t counted = 0;
    const auto want = [&](std::uint8_t w, std::uint32_t target, bool counts = true) {
        if (counts && counted >= max_entries) {
            return;
        }
        if (c.stock(w) < target) {
            wanted.push_back({w, target + target / 4});
            counted += counts ? 1U : 0U;
        }
    };
    // Luxury goods by demand: ten minutes' worth, at least 21 t (below 20 t it is raised, as in the original).
    const auto lux = [&](std::uint8_t w, std::uint32_t factor) {
        std::uint32_t t = factor * demand(c, w);
        if (t < 20 * kTon) {
            t = 21 * kTon;
        }
        return t;
    };
    const std::uint8_t food = m_buildings.food_ware();
    const auto& m = m_buildings.materials();
    const std::uint32_t civ = civ_level(p);
    if (r.stage != stage::kColony) {
        want(food, std::max(8 * demand(c, food), std::uint32_t{8 * kTon}));
    }
    want(m.tools, (population(c) >= 2 && r.slots[kSlotToolsmith].count == 0) ? 20U * kTon : 10U * kTon);
    if (r.slots[kSlotSmelter].count < 2 && c.stock(m.wood) < 20 * kTon && counted < max_entries) {
        wanted.push_back({m.wood, 25 * kTon});
        ++counted;
    }
    if (r.slots[kSlotStonemason].count < 2) {
        want(m.bricks, (civ + 1) * 10 * kTon);
    }
    if (population(c) >= 25) {
        want(ware::kCloth, std::max(8 * demand(c, ware::kCloth), std::uint32_t{10 * kTon}));
    }
    if (population(c, 1) >= 170) {
        if (player(p).money > 2000) {
            want(ware::kOre, (r.slots[kSlotSmelter].count != 0 ? 15U : 10U) * kTon);
            want(ware::kIron, (r.slots[kSlotToolsmith].count != 0 ? 15U : 10U) * kTon);
        }
        want(ware::kSpices, lux(ware::kSpices, 10));
        want(ware::kTobaccoGoods, lux(ware::kTobaccoGoods, 10));
        want(ware::kAlcohol, lux(ware::kAlcohol, 10));
    }
    if (population(c, 2) >= 350) {
        want(ware::kCocoa, lux(ware::kCocoa, 10));
    }
    if (population(c, 3) >= 400) {
        want(ware::kClothes, lux(ware::kClothes, 10));
        if (c.stock(ware::kGold) < 11 * kTon && counted < max_entries) {
            wanted.push_back({ware::kGold, 13 * kTon + 3 * kTon / 4});
            ++counted;
        }
        want(ware::kJewellery, lux(ware::kJewellery, 4), false);
    }
    for (const std::uint8_t w : m_world.goods().trade_wares()) {
        const City::Trade* t = c.trade_of(w);
        const auto it = std::find_if(wanted.begin(), wanted.end(), [&](const Want& x) { return x.ware == w; });
        TradeSetting s;
        s.player = p.player;
        s.island = c.island;
        s.city = c.index;
        s.ware = w;
        if (it == wanted.end()) {
            if (t != nullptr && t->buy) {
                s.mode = TradeSetting::Mode::None;
                execute(s);
            }
            continue;
        }
        // The original writes the limit only when it switches buying on (S-101); Open1602 keeps it
        // up to date.
        if (t == nullptr || !t->buy || t->limit != it->limit) {
            s.mode = TradeSetting::Mode::Buy;
            s.amount = it->limit;
            execute(s);
        }
    }
}

void Engine::sell_settings(AiPlayer& p, City& c, CityPlan& r) {
    (void)r;
    if (!player(p).unlocked(infra::kLevel2C)) {
        return;
    }
    if (kontor_level(c) == 0 || !c.warehouse_x || !c.warehouse_y) {
        return;
    }
    const Tile k = tile(c.island, *c.warehouse_x, *c.warehouse_y);
    const std::uint32_t max_entries = k.valid() ? k.type->max_wares : 0;
    struct Offer {
        std::uint8_t ware;
        std::uint32_t keep;
    };
    std::vector<Offer> offers;
    std::uint32_t counted = 0;
    const auto offer = [&](std::uint8_t w, std::uint32_t keep, std::uint32_t above, bool counts = true) {
        if (counts && counted >= max_entries) {
            return;
        }
        const City::Trade* t = c.trade_of(w);
        if (t != nullptr && t->buy) {
            return;  // never sells what it buys
        }
        if (c.stock(w) > above) {
            offers.push_back({w, keep});
            counted += counts ? 1U : 0U;
        }
    };
    const auto by_demand = [&](std::uint8_t w) { return std::max(15 * demand(c, w), std::uint32_t{17 * kTon}); };
    const std::uint32_t civ = civ_level(p);
    struct Lux {
        std::uint8_t ware;
        std::uint8_t slot;
    };
    constexpr std::array<Lux, 5> kLux{{{ware::kTobaccoGoods, kSlotTobaccoGoods},
                                       {ware::kSpices, kSlotSpices},
                                       {ware::kAlcohol, kSlotAlcohol},
                                       {ware::kCocoa, kSlotCocoa},
                                       {ware::kClothes, kSlotTailor}}};
    for (const Lux& l : kLux) {
        if (p.present[l.slot]) {
            const std::uint32_t keep = by_demand(l.ware);
            offer(l.ware, keep, keep + 8 * kTon);
        }
    }
    const auto& m = m_buildings.materials();
    if (p.present[kSlotToolsmith]) {
        const std::uint32_t keep = (civ + 1) * 15 * kTon;
        offer(m.tools, keep, keep + 8 * kTon);
    }
    offer(ware::kOre, (civ + 1) * 10 * kTon, (civ + 1) * 10 * kTon + 8 * kTon);
    offer(ware::kIron, (civ + 1) * 15 * kTon, (civ + 1) * 15 * kTon + 8 * kTon);
    offer(ware::kGold, 25 * kTon, 30 * kTon);
    offer(ware::kJewellery, 25 * kTon, 30 * kTon);
    offer(ware::kCloth, 17 * kTon, 25 * kTon);
    offer(m.bricks, (civ + 1) * 15 * kTon, (civ + 1) * 15 * kTon + 8 * kTon);
    offer(m.wood, (civ + 1) * 15 * kTon, (civ + 1) * 15 * kTon + 8 * kTon);
    const std::uint8_t food = m_buildings.food_ware();
    offer(food, by_demand(food), by_demand(food) + 8 * kTon, false);
    for (const std::uint8_t w : m_world.goods().trade_wares()) {
        const City::Trade* t = c.trade_of(w);
        const auto it = std::find_if(offers.begin(), offers.end(), [&](const Offer& x) { return x.ware == w; });
        TradeSetting s;
        s.player = p.player;
        s.island = c.island;
        s.city = c.index;
        s.ware = w;
        if (it == offers.end()) {
            if (t != nullptr && t->sell) {
                s.mode = TradeSetting::Mode::None;
                execute(s);
            }
            continue;
        }
        if (t == nullptr || !t->sell || t->keep != it->keep) {
            s.mode = TradeSetting::Mode::Sell;
            s.amount = it->keep;
            execute(s);
        }
    }
}

void Engine::production_switches(AiPlayer& p, City& c) {
    // Every producer of the current city, by the ware it makes. Each check looks at the
    // building's switch first: one that is on can only go off, one that is off only on.
    // An off building is not ticked, so its count of cycles without raw
    // material stays until it is switched on again and must not keep it off.
    const IslandMap& map = island_of(c.island).map;
    const std::uint32_t cap = c.capacity;
    const auto& m = m_buildings.materials();
    std::vector<std::pair<std::uint32_t, bool>> changes;  // production index, on; in the original's order
    for (std::size_t i = 0; i < m_world.productions().size(); ++i) {
        const Production& pr = m_world.productions()[i];
        if (!pr.used || pr.island != c.island || !map.contains(pr.x, pr.y)) {
            continue;
        }
        const MapCell& cell = map.top(pr.x, pr.y);
        if (cell.city != c.index || cell.empty()) {
            continue;
        }
        const assets::BuildingType* b = m_buildings.by_sprite(cell.sprite);
        if (b == nullptr || b->prod_kind == prod::kMarket || b->prod_kind == prod::kKontor ||
            b->prod_kind < prod::kWorkshop || b->prod_kind > prod::kFisher) {
            continue;
        }
        const auto index = static_cast<std::uint32_t>(i);
        bool enabled = pr.enabled;
        const auto set = [&](bool on) {
            changes.emplace_back(index, on);
            enabled = on;
        };
        // NoRawGoods: a Maxnorohst of 0 counts as starved at once.
        const bool starved = pr.idle_cycles >= b->max_idle_cycles;
        const std::uint8_t w = b->ware;
        const auto raw = [&] {
            // Raw materials: off when the warehouse is full, on below its room less 4 t.
            if (enabled) {
                if (c.stock(w) >= cap) {
                    set(false);
                }
            } else if (c.stock(w) + 4 * kTon < cap) {
                set(true);
            }
        };
        const auto workshop = [&] {
            // Workshops: off when starved, on with more than 4 t of raw material. A starved one with
            // raw material goes off now and on at the next refresh, which clears its idle count.
            if (enabled) {
                if (starved) {
                    set(false);
                }
            } else if (c.stock(b->raw_ware) > 4 * kTon) {
                set(true);
            }
        };
        const auto capped = [&](std::uint32_t limit) {
            if (enabled) {
                if (starved || c.stock(w) >= limit) {
                    set(false);
                }
            } else if (c.stock(b->raw_ware) > kTon && c.stock(w) < limit) {
                set(true);
            }
        };
        if (w == ware::kIron) {
            capped(60 * kTon);
        } else if (w == ware::kSwords || w == ware::kMuskets) {
            capped(10 * kTon);
        } else if (w == m.cannons) {
            capped(25 * kTon);
        } else if (w == m.tools) {
            capped(40 * kTon);
        } else if (b->prod_kind == prod::kWorkshop && b->raw_ware != 0 && b->raw_ware < ware::kCornField &&
                   w != m.bricks) {
            workshop();
            if (w == ware::kAlcohol) {
                raw();  // the rum distillery is asked twice, as workshop and as producer of alcohol
            }
        } else {
            raw();  // farms, pastures, mines, hunters, fishers and the workshops without a raw ware
        }
    }
    for (const auto& [index, on] : changes) {
        const Production& pr = m_world.productions()[index];
        ProductionSwitch s;
        s.player = p.player;
        s.island = pr.island;
        s.x = pr.x;
        s.y = pr.y;
        s.on = on;
        execute(s);
    }
}

// ---------------------------------------------------------------------------------------------
// Stages and their handlers

void Engine::set_stage(AiPlayer& p, CityPlan& r, std::uint8_t s) {
    log(p, "city %u/%u: stage 0x%x -> 0x%x", r.island, r.city, r.stage, s);
    r.stage = s;
    r.stage_ms = now_ms();
    if (s > p.max_stage) {
        p.max_stage = s;
        if (s > stage::kPioneers) {
            p.home_valid = true;
            p.home_island = r.island;
            p.home_city = r.city;
        }
    }
    const std::uint8_t score = s >= stage::kMerchants2  ? 4
                               : s >= stage::kCitizens2 ? 3
                               : s >= stage::kSecond    ? 2
                               : s >= stage::kSettlers  ? 1
                                                        : 0;
    p.score = std::max(p.score, score);
}

void Engine::nothing_to_do(AiPlayer& p) {
    // With several cities the visit ends.
    if (p.cities > 1) {
        p.budget = 0;
    }
}

std::int32_t Engine::net_income(const AiPlayer& p) const {
    const std::vector<std::int32_t> costs = running_costs(m_world, m_buildings);
    std::int32_t sum = 0;
    for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
        const City& c = m_world.cities()[i];
        if (c.owner == p.player) {
            sum += tax_income(c, m_buildings) - costs[i];
        }
    }
    return sum;
}

bool Engine::incomes_ok(const AiPlayer& p, std::int32_t minimum) const {
    // Every inhabited city of the computer earns at least `minimum` per minute.
    const std::vector<std::int32_t> costs = running_costs(m_world, m_buildings);
    for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
        const City& c = m_world.cities()[i];
        if (c.owner == p.player && population(c) > 0 && tax_income(c, m_buildings) - costs[i] < minimum) {
            return false;
        }
    }
    return true;
}

std::int32_t Engine::land_rank(const AiPlayer& p) const {
    // -1 another own city has no land measured, 0 another has more, 1 the largest.
    const CityPlan* here = nullptr;
    for (const CityPlan& r : p.plans) {
        if (r.island == p.island && r.city == p.city) {
            here = &r;
        }
    }
    if (here == nullptr) {
        return 1;
    }
    std::int32_t rank = 1;
    for (const CityPlan& r : p.plans) {
        if (&r == here) {
            continue;
        }
        const City* c = city(r.island, r.city);
        if (c == nullptr || c->owner != p.player) {
            continue;
        }
        if (r.land == 0) {
            return -1;
        }
        if (r.land > here->land) {
            rank = 0;
        }
    }
    return rank;
}

std::uint32_t Engine::luxury_in_stock(const AiPlayer& p, const City& c, const CityPlan& r, std::uint32_t level) const {
    // Luxury goods the city has enough of (4 t with an own producer, else 20 t).
    const auto t = [](bool own) { return (own ? 4U : 20U) * kTon; };
    std::uint32_t n = 0;
    if (level >= 2) {
        n += c.stock(ware::kSpices) > t(r.slots[kSlotSpices].count != 0) ? 1U : 0U;
        n += c.stock(ware::kTobaccoGoods) >= t(r.slots[kSlotTobaccoGoods].count != 0) ? 1U : 0U;
        n += c.stock(ware::kAlcohol) >= t(r.slots[kSlotWine].count != 0 || r.slots[kSlotRum].count != 0) ? 1U : 0U;
    }
    if (level >= 3) {
        n += c.stock(ware::kCocoa) >= t(r.slots[kSlotCocoa].count != 0) ? 1U : 0U;
    }
    if (level >= 4) {
        n += c.stock(ware::kClothes) >= t(p.present[kSlotTailor]) ? 1U : 0U;
        n += c.stock(ware::kJewellery) >= t(p.present[kSlotGoldsmith]) ? 1U : 0U;
    }
    return n;
}

// ---------------------------------------------------------------------------------------------
// Build orders of the stages (task 5)
//
// Each stage of a city tries what it needs in a fixed order and stops at the first entry that
// finds something to do. The orders are data (computer_orders.hpp): the original's, or a package's
// from its computers.toml.

std::uint32_t Engine::grown(const CityPlan& r, std::uint32_t n, bool any_stage) const {
    if (classic() || (r.stage < stage::kSecond && !any_stage)) {
        return n;
    }
    return grown_count(r.land, n, any_stage || m_grow_from == 0 ? kGrowFrom : m_grow_from);
}

std::uint32_t Engine::houses_wanted(const City& c, const CityPlan& r, std::uint32_t n) const {
    const std::uint32_t wanted = grown(r, n);
    if (wanted <= n) {
        return n;
    }
    // Houses beyond the original's number only while the city's houses move up: a new house starts
    // with the pioneers, and a city that keeps building them grows wide instead of tall, its
    // goods spread over houses that never reach the next class.
    std::uint32_t houses = 0;
    std::uint32_t pioneers = 0;
    for (const House& h : m_world.houses()) {
        if (h.island == c.island && h.city == c.index) {
            ++houses;
            pioneers += h.resident_class == 0 ? 1U : 0U;
        }
    }
    return pioneers * 8 <= houses ? wanted : n;
}

bool Engine::first_need(AiPlayer& p, const City& c, const CityPlan& r, std::int32_t money,
                        std::span<const StageNeed> needs, bool grow) {
    const auto count = [&](std::uint32_t n) { return grow ? grown(r, n) : n; };
    const auto holds = [money](StageNeed::When w) {
        switch (w) {
            case StageNeed::When::Never:
                return false;
            case StageNeed::When::Always:
                return true;
            case StageNeed::When::Gold4000:
                return money >= 4000;
            case StageNeed::When::Gold5000:
                return money >= 5000;
            case StageNeed::When::Gold8000:
                return money >= 8000;
        }
        return false;
    };
    for (const StageNeed& n : needs) {
        if (!holds(n.when)) {
            continue;
        }
        bool found = false;
        switch (n.kind) {
            case StageNeed::Kind::Building:
                found = need(p, n.slot, count(n.n));
                break;
            case StageNeed::Kind::Market:
                found = need_market(p, n.n);
                break;
            case StageNeed::Kind::Houses:
                found = need_houses(p, grow ? houses_wanted(c, r, n.n) : n.n);
                break;
            case StageNeed::Kind::Public:
                found = need_public(p, n.slot, count(n.n), holds(n.flag));
                break;
            case StageNeed::Kind::Kontor:
                found = need_kontor(p, n.n);
                break;
            case StageNeed::Kind::Castle:
                found = need_castle(p);
                break;
            case StageNeed::Kind::Monument:
                found = need_denkmal(p);
                break;
            case StageNeed::Kind::Shipyard:
                found = need_shipyard(p);
                break;
            case StageNeed::Kind::Processing:
                found = processing(p);
                break;
            case StageNeed::Kind::IronTools:
                found = iron_tools(p);
                break;
            case StageNeed::Kind::Weapons:
                found = weapons(p);
                break;
            case StageNeed::Kind::Luxury:
                found = luxury(p, count(n.n), holds(n.flag));
                break;
            case StageNeed::Kind::ColonyLuxury:
                found = colony_luxury(p, c, r, n.n);
                break;
            case StageNeed::Kind::Tower:
                found = need_tower(p, 1, count(n.n));
                break;
            case StageNeed::Kind::RoadsIfMason:
                found = roads_if_mason(p);
                break;
            case StageNeed::Kind::Colony:
                found = want_colony(p, static_cast<std::uint8_t>(n.n), false, false);
                break;
            case StageNeed::Kind::Package:
                found = package_order(p, c, r, n.slot, count(n.n));
                break;
            case StageNeed::Kind::Crop:
                break;  // first_crops only: nothing a stage builds
        }
        if (found) {
            return true;
        }
    }
    return false;
}

void Engine::unload_ship(AiPlayer& p, City& c) {
    if (!c.warehouse_x || !c.warehouse_y) {
        return;
    }
    for (Ship* s : own_ships(p)) {
        if (!s->target.empty() || s->total_load() == 0 || s->id == p.settle.ship) {
            continue;
        }
        if (building_in_reach(m_world, m_buildings, c.island, *c.warehouse_x, *c.warehouse_y, s->x, s->y)) {
            unload_at(c, *s, false, false);
            return;
        }
    }
}

bool Engine::colony_luxury(AiPlayer& p, const City& c, const CityPlan& r, std::uint32_t tiles_per_field) {
    // Luxury crops of a colony by the highest stage the player reached (stage 1, 0x17).
    const std::uint32_t n = r.land / tiles_per_field;
    const std::uint32_t q = full_crops(c.island);
    switch (p.max_stage) {
        case stage::kSettlers:
        case stage::kMain:
        case stage::kMain2:
            return luxury(p, std::min(n, grown(r, 4, true)), true);
        case stage::kCitizens:
            return luxury(p, std::min(n, grown(r, 6, true)), true);
        case stage::kCitizens2:
            return luxury(p, std::min(n, grown(r, 4, true)), false);
        case stage::kCitizensSmall:
        case stage::kMerchantsSmall:
        case stage::kMerchants:
        case stage::kMerchants2:
        case stage::kAristocrats:
        case stage::kAristocrats2: {
            const std::uint32_t cap = (p.max_stage == stage::kMerchants || p.max_stage == stage::kMerchants2) ? 6U
                                      : (p.max_stage == stage::kAristocrats || p.max_stage == stage::kAristocrats2)
                                          ? 10U
                                          : 8U;
            const std::uint32_t k = std::min(n, grown(r, cap, true));
            if (q == 1) {
                if (luxury(p, k, false)) {
                    return true;
                }
                const City* home = home_city(p);
                return home != nullptr && home->stock(m_buildings.food_ware()) < 8 * kTon && need(p, kSlotCattle, k);
            }
            return luxury(p, k, false);
        }
        default:
            return false;
    }
}

bool Engine::farm_animals(AiPlayer& p, const CityPlan& r) {
    // A second city without luxury crops keeps sheep and cattle, by the highest stage reached.
    const std::uint32_t n = r.land / 128;
    switch (p.max_stage) {
        case stage::kMain:
        case stage::kMain2:
            return need(p, kSlotSheep, std::min(n, grown(r, 4U, true)));
        case stage::kCitizens:
            return need(p, kSlotSheep, std::min(n, grown(r, 6U, true)));
        case stage::kCitizens2:
            return need(p, kSlotCattle, std::min(n, grown(r, 4U, true))) ||
                   need(p, kSlotSheep, std::min(n, grown(r, 4U, true)));
        case stage::kCitizensSmall:
        case stage::kMerchantsSmall:
        case stage::kAristocrats:
        case stage::kAristocrats2:
            return need(p, kSlotCattle, std::min(n, grown(r, 8U, true))) ||
                   need(p, kSlotSheep, std::min(n, grown(r, 8U, true)));
        case stage::kMerchants:
        case stage::kMerchants2:
            return need(p, kSlotCattle, std::min(n, grown(r, 6U, true))) ||
                   need(p, kSlotSheep, std::min(n, grown(r, 6U, true)));
        default:
            return false;
    }
}

void Engine::run_stage(AiPlayer& p, City& c, CityPlan& r) {
    const std::int32_t money = player(p).money;
    // When the city moves on (balance.toml [computer_stages]; the original's numbers by default).
    const Balance& at = m_world.balance();
    const auto has = [money](std::uint32_t gold) { return std::int64_t{money} >= std::int64_t{gold}; };
    // The stage's own Needs first, then the producers of the packages' goods its inhabitants want.
    // The lists this visit went through; once there is nothing to do, Open1602 goes through them
    // again with their numbers grown by the city's land, so that a large island fills up without
    // holding up the way to the next stage.
    std::vector<ComputerList> asked;
    const auto needs = [&](ComputerList list) {
        asked.push_back(list);
        return first_need(p, c, r, money, order_of(m_world.balance().computer_orders, list)) || package_chains(p, c, r);
    };
    const auto idle = [&] {
        if (!classic()) {
            // A city at its last stage that earns well grows on the original's large islands too
            // (from kGrowLand tiles); before, it would spend what its next stage needs.
            m_grow_from = r.stage == stage::kAristocrats2 && net_income(p) >= kGrowIncome ? kGrowLand : kGrowFrom;
            for (const ComputerList list : asked) {
                if (first_need(p, c, r, money, order_of(m_world.balance().computer_orders, list), true)) {
                    m_grow_from = 0;
                    return;
                }
            }
            m_grow_from = 0;
        }
        nothing_to_do(p);
    };

    switch (r.stage) {
        case stage::kFounded: {
            unload_ship(p, c);
            r.land = free_land(c.island, c.index, &r.land_percent);
            if (population(c) >= at.ai_pioneers_inhabitants) {
                set_stage(p, r, stage::kPioneers);
                return;
            }
            std::uint32_t big_cities = 0;
            bool large = false;
            for (const City& o : m_world.cities()) {
                if (o.owner == p.player && &o != &c) {
                    big_cities += population(o) >= at.ai_colony_other_people ? 1U : 0U;
                    large = large || population(o) >= at.ai_colony_large_city;
                }
            }
            if (r.land < at.ai_colony_below_land || building_count(c) > at.ai_colony_buildings || big_cities > 1 ||
                large) {
                set_stage(p, r, stage::kColony);
                return;
            }
            if (needs(ComputerList::Founded)) {
                return;
            }
            if (now_ms() - r.stage_ms >= std::uint64_t{at.ai_founded_abandon_seconds} * 1000) {
                r.flags |= city_flag::kAbandon;
            }
            return;
        }
        case stage::kColony: {
            if (needs(ComputerList::Colony)) {
                return;
            }
            if (full_crops(c.island) == 0) {
                const std::uint32_t n =
                    std::min(r.land / at.ai_colony_animal_tiles, grown(r, at.ai_colony_animals, true));
                if (need(p, kSlotSheep, n) || need(p, kSlotCattle, n)) {
                    return;
                }
            } else if (colony_luxury(p, c, r, at.ai_colony_luxury_tiles)) {
                return;
            }
            if (needs(ComputerList::ColonyMaterials)) {
                return;
            }
            idle();
            return;
        }
        case stage::kPioneers: {
            unload_ship(p, c);
            if (population(c) >= at.ai_settlers_inhabitants) {
                set_stage(p, r, stage::kSettlers);
                return;
            }
            if (needs(ComputerList::Pioneers)) {
                return;
            }
            if (now_ms() - r.stage_ms >= std::uint64_t{at.ai_pioneers_abandon_seconds} * 1000) {
                r.flags |= city_flag::kAbandon;
            }
            return;
        }
        case stage::kSettlers: {
            const bool grown =
                (r.slots[kSlotHouses].count >= at.ai_grown_houses && population(c, 1) >= at.ai_grown_settlers) ||
                now_ms() - r.stage_ms > std::uint64_t{at.ai_grown_seconds} * 1000;
            if (grown) {
                // A first colony before the city grows on (the original waits for it forever if no
                // island is found, S-097; Open1602 goes on once the search has failed). Not if the
                // scenario forbids colonies.
                if (kontor_level(c) == 1 && p.cities < 2 && (!p.colony_failed || classic()) &&
                    !player(p).scenario.has(scenario_flag::kNoColonies)) {
                    want_colony(p, 1, true, true);
                } else if (has(at.ai_grown_money) || incomes_ok(p, static_cast<std::int32_t>(at.ai_grown_income))) {
                    const std::int32_t rank = land_rank(p);
                    if (rank == 0) {
                        set_stage(p, r, stage::kSecond);
                        return;
                    }
                    if (rank == 1) {
                        set_stage(p, r, stage::kMain);
                        return;
                    }
                }
            }
            if (needs(ComputerList::Settlers)) {
                return;
            }
            if (population(c, 1) >= at.ai_settlers_luxury_settlers) {
                (void)(processing(p) || luxury(p, at.ai_settlers_luxury_fields, true));
            }
            return;
        }
        case stage::kSecond: {
            if (needs(ComputerList::Second)) {
                return;
            }
            set_stage(p, r, full_crops(c.island) != 0 ? stage::kSecondLuxury : stage::kSecondFarming);
            return;
        }
        case stage::kSecondLuxury: {
            if (needs(ComputerList::SecondLuxury)) {
                return;
            }
            idle();
            return;
        }
        case stage::kSecondFarming: {
            if (needs(ComputerList::SecondFarming) || farm_animals(p, r) || need_tower(p, 1, 6)) {
                return;
            }
            idle();
            return;
        }
        case stage::kMain: {
            if ((population(c, 1) >= at.ai_main2_settlers && has(at.ai_main2_money)) || has(at.ai_main2_rich_money)) {
                if (kontor_level(c) < 3 && p.cities < 3 && want_colony(p, 2, false, true)) {
                    return;
                }
                set_stage(p, r, stage::kMain2);
                return;
            }
            if (needs(ComputerList::Main) || needs(ComputerList::MainRich)) {
                return;
            }
            if (has(at.ai_main_idle_money)) {
                idle();
            }
            return;
        }
        case stage::kMain2: {
            if (has(at.ai_citizens_money) && !want_colony(p, 2, false, true) &&
                (luxury_in_stock(p, c, r, 2) > at.ai_citizens_luxury || kontor_level(c) > at.ai_citizens_kontor)) {
                set_stage(p, r, stage::kCitizens);
                return;
            }
            if (needs(ComputerList::Main2)) {
                return;
            }
            idle();
            return;
        }
        case stage::kCitizens: {
            if ((population(c, 2) >= at.ai_citizens2_citizens && has(at.ai_citizens2_money)) ||
                has(at.ai_citizens2_rich_money)) {
                set_stage(p, r, stage::kCitizens2);
                return;
            }
            if (needs(ComputerList::Citizens)) {
                return;
            }
            idle();
            return;
        }
        case stage::kCitizens2: {
            if ((population(c, 2) >= at.ai_merchants_citizens && has(at.ai_merchants_money)) ||
                has(at.ai_merchants_rich_money)) {
                set_stage(p, r, r.land < at.ai_merchants_land ? stage::kCitizensSmall : stage::kMerchants);
                return;
            }
            if (needs(ComputerList::Citizens2)) {
                return;
            }
            idle();
            return;
        }
        case stage::kCitizensSmall: {
            if (needs(ComputerList::CitizensSmall)) {
                return;
            }
            idle();
            return;
        }
        case stage::kMerchants: {
            if (population(c, 3) >= at.ai_merchants2_merchants || kontor_level(c) >= at.ai_merchants2_kontor) {
                set_stage(p, r, stage::kMerchants2);
                return;
            }
            if (needs(ComputerList::Merchants)) {
                return;
            }
            if (luxury_in_stock(p, c, r, 3) > 2 && needs(ComputerList::MerchantsLuxury)) {
                return;
            }
            if (needs(ComputerList::MerchantsTail)) {
                return;
            }
            idle();
            return;
        }
        case stage::kMerchants2:
        case stage::kMerchantsSmall: {
            const bool small = r.stage == stage::kMerchantsSmall;
            if (!small && has(at.ai_aristocrats_money) && population(c, 3) >= at.ai_aristocrats_merchants) {
                set_stage(p, r, r.land < at.ai_aristocrats_land ? stage::kMerchantsSmall : stage::kAristocrats);
                return;
            }
            if (small ? needs(ComputerList::MerchantsSmall) || needs(ComputerList::MerchantsSmallTail)
                      : needs(ComputerList::Merchants2) || needs(ComputerList::Merchants2Tail)) {
                return;
            }
            idle();
            return;
        }
        case stage::kAristocrats: {
            if (want_colony(p, 4, false, false)) {
                return;
            }
            if (luxury_in_stock(p, c, r, 4) > at.ai_aristocrats2_luxury && has(at.ai_aristocrats2_money)) {
                set_stage(p, r, stage::kAristocrats2);
                return;
            }
            if (needs(ComputerList::Aristocrats)) {
                return;
            }
            idle();
            return;
        }
        case stage::kAristocrats2: {
            if (needs(ComputerList::Aristocrats2)) {
                return;
            }
            idle();
            return;
        }
        default:
            set_stage(p, r, stage::kFounded);
            return;
    }
}

// ---------------------------------------------------------------------------------------------
// Needs

bool Engine::plan_building(AiPlayer& p, std::uint8_t task, const assets::BuildingType& b, std::uint8_t slot, bool force,
                           bool from_start) {
    if (!push(p, task, force)) {
        return false;
    }
    start_search(p, b, slot, from_start);
    return true;
}

void Engine::reserve(AiPlayer& p, const assets::BuildingType& b, std::int64_t extra_money) {
    // The costs leave the player and the city while the site is searched. The
    // original overwrites the building's gold with the fields' gold for farms (S-096); Open1602
    // keeps both.
    refund(p);
    City* c = current_city(p);
    if (c == nullptr) {
        return;
    }
    const auto& m = m_buildings.materials();
    Reservation& r = p.reserved;
    r.island = c->island;
    r.city = c->index;
    r.money = std::int64_t{b.build_cost.money} + extra_money;
    player(p).money = money_plus(player(p).money, -std::int64_t{r.money});
    r.wood = remove_goods(*c, m.wood, b.build_cost.wood);
    r.bricks = remove_goods(*c, m.bricks, b.build_cost.bricks);
    r.tools = remove_goods(*c, m.tools, b.build_cost.tools);
    r.cannons = remove_goods(*c, m.cannons, b.build_cost.cannons);
}

void Engine::refund(AiPlayer& p) {
    Reservation& r = p.reserved;
    if (r.empty()) {
        return;
    }
    if (Player* pl = m_world.player(p.player)) {
        pl->money = money_plus(pl->money, std::int64_t{r.money});
    }
    if (City* c = city(r.island, r.city); c != nullptr && c->owner == p.player) {
        const auto& m = m_buildings.materials();
        add_goods(*c, m.wood, r.wood);
        add_goods(*c, m.bricks, r.bricks);
        add_goods(*c, m.tools, r.tools);
        add_goods(*c, m.cannons, r.cannons);
    }
    r = Reservation{};
}

bool Engine::need(AiPlayer& p, std::uint8_t which, std::uint32_t n, bool cov) {
    (void)cov;
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    // Some Needs choose another building first (ComputerSlot::instead): wool and cloth from cotton
    // where it grows fully.
    const std::vector<ComputerProducer>& changes = m_world.balance().computer_producers;
    if (const ComputerSlot* k = computer_slot(slots(), which);
        k != nullptr && k->instead != kNoProducer && fertility(c->island, k->instead_crop) == 128) {
        // With `instead_unlocked`, only once its building is unlocked.
        bool open = k->instead_unlocked == kNoProducer;
        if (!open) {
            const auto first = need_def(slots(), k->instead_unlocked, changes);
            const assets::BuildingType* b = first ? by_id(first->fixed) : nullptr;
            open = b != nullptr && unlocked(p, *b);
        }
        if (open) {
            which = k->instead;
        }
    }
    const ComputerSlot* slot = computer_slot(slots(), which);
    if (slot != nullptr && slot->deposit == ware::kOre) {
        return need_iron_mine(p, n);
    }
    if (slot != nullptr && slot->deposit == ware::kGold) {
        return need_gold_mine(p);
    }
    const auto d = need_def(slots(), which, changes);
    if (!d || slot == nullptr) {
        return false;
    }
    SlotState& s = r->slots[which];
    std::uint32_t wanted = n;
    if (slot->single_below_stock != 0) {
        // More than one only with the raw material in stock and as many producers that need it.
        std::uint32_t users = 0;
        for (const ComputerSlot& k : slots()) {
            users += k.after == which && r->slots[k.number].count != 0 ? 1U : 0U;
        }
        if (!(c->stock(slot->raw) >= slot->single_below_stock * kStockUnitsPerTon &&
              users >= slot->single_below_users)) {
            wanted = std::min<std::uint32_t>(wanted, 1);
        }
    }
    if (s.count >= wanted || s.attempts >= d->attempts) {
        return false;
    }
    // Processors only where their raw material is produced (NeedDef::after, a package may change
    // it). The original bounds the raw producers from above instead (ceil(raw / 2) <= n), so more
    // farms stop the workshop, and asks for bakeries even where no mill feeds them (S-098).
    const ComputerProducer* change = producer_change(changes, which);
    const bool original_bakery = which == kSlotBakery && classic() && (change == nullptr || !change->after);
    if (d->after != kNoProducer && r->slots[d->after].count == 0 && !original_bakery) {
        return false;
    }
    // Conditions besides the count that stay the original's.
    if (slot->process_max != 0 && slot->process_from != kNoProducer &&
        n > std::uint32_t{slot->process_max} * r->slots[slot->process_from].count) {
        return false;
    }
    if (slot->raw_stock != 0 && c->stock(slot->raw) <= slot->raw_stock * kStockUnitsPerTon) {
        return false;
    }
    if (d->fertile != 0 && fertility(c->island, d->fertile) == 0) {
        return false;
    }
    const assets::BuildingType* b = d->fixed != 0 ? by_id(d->fixed) : producer(d->ware, d->raw);
    if (b == nullptr) {
        return false;
    }
    if (!unlocked(p, *b)) {
        return d->type == NeedType::C;
    }
    std::int64_t field_money = 0;
    if (d->field != 0) {
        if (const assets::BuildingType* f = producer(d->field, 0)) {
            field_money = 64 * std::int64_t{f->build_cost.money};
        }
    }
    const bool afford =
        affordable(p, *c, *b) && (d->field == 0 || std::int64_t{player(p).money} > field_money + b->build_cost.money);
    if (!afford) {
        return d->type == NeedType::B || d->type == NeedType::C;
    }
    const bool pushed = plan_building(p, d->task, *b, which, false, d->from_start);
    if (pushed) {
        reserve(p, *b, field_money);
    }
    return d->type == NeedType::D ? pushed : true;
}

std::optional<std::uint8_t> Engine::package_slot(std::uint8_t w) const {
    for (const PackageNeed& n : m_package_needs) {
        if (n.ware == w) {
            return n.slot;
        }
    }
    return std::nullopt;
}

bool Engine::package_chains(AiPlayer& p, const City& c, const CityPlan& r) {
    if (m_package_needs.empty()) {
        return false;
    }
    std::vector<std::uint32_t> use(m_package_needs.size());
    for (std::size_t i = 0; i < use.size(); ++i) {
        use[i] = demand(c, m_package_needs[i].ware);
    }
    const std::vector<std::uint32_t> wanted = package_wanted(m_package_needs, use);
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        if (wanted[i] != 0 && package_need(p, c, r, m_package_needs[i], wanted[i])) {
            return true;
        }
    }
    return false;
}

bool Engine::package_order(AiPlayer& p, const City& c, const CityPlan& r, std::uint8_t w, std::uint32_t wanted) {
    // An order's entry for a good of a package: as many producers as it says, where the computers
    // have a producer for the good (package_needs).
    for (const PackageNeed& n : m_package_needs) {
        if (n.ware == w) {
            return package_need(p, c, r, n, wanted);
        }
    }
    return false;
}

bool Engine::package_need(AiPlayer& p, const City& c, const CityPlan& r, const PackageNeed& n, std::uint32_t wanted) {
    const SlotState& s = r.slots[n.slot];
    if (s.count >= wanted || s.attempts >= 2) {
        return false;
    }
    // A workshop waits for the producer of its raw material. The original's Need of it is asked
    // for one; one of a package's comes in its own turn, as the city wants it too.
    if (n.raw_slot != kNoSlot && r.slots[n.raw_slot].count == 0) {
        if (n.raw_slot >= kSlots) {
            return false;
        }
        const std::size_t before = p.stack.size();
        (void)need(p, n.raw_slot, 1);
        return p.stack.size() > before;
    }
    if (n.fertile != 0 && fertility(c.island, n.fertile) == 0) {
        return false;
    }
    const assets::BuildingType* b = by_id(n.building);
    if (b == nullptr || !unlocked(p, *b)) {
        return false;
    }
    std::int64_t field_money = 0;
    if (n.field != 0) {
        if (const assets::BuildingType* f = producer(n.field, 0)) {
            field_money = 64 * std::int64_t{f->build_cost.money};
        }
    }
    if (!affordable(p, c, *b) || std::int64_t{player(p).money} <= field_money + b->build_cost.money) {
        return false;
    }
    if (!plan_building(p, n.task, *b, n.slot, false, false)) {
        return false;
    }
    reserve(p, *b, field_money);
    return true;
}

bool Engine::need_iron_mine(AiPlayer& p, std::uint32_t n) {
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    const std::optional<NeedDef> found = need_def(slots(), kSlotIronMine, m_world.balance().computer_producers);
    if (!found) {
        return false;  // a package removed the iron mine
    }
    const NeedDef& def = *found;
    SlotState& s = r->slots[kSlotIronMine];
    const std::uint32_t wanted = civ_level(p) >= 2 ? n : 1;
    const std::uint8_t blocked = std::max<std::uint8_t>(2, def.attempts);
    if (s.count >= wanted || s.attempts >= def.attempts) {
        return false;
    }
    if (c->stock(ware::kOre) >= 8 * kTon) {
        s.attempts = blocked;  // enough ore
        return false;
    }
    bool usable = false;
    std::int32_t free_x = -1;
    std::int32_t free_y = -1;
    for (const auto& d : island_of(c->island).header.deposits) {
        if (d.ware == ware::kOre && d.amount > 0 &&
            (d.kind == 1 || d.amount >= 120 * kTon || player(p).unlocked(infra::kLevel3F))) {
            usable = usable || reachable_deposit(p, d, free_x, free_y);
        }
    }
    if (!usable && free_x < 0) {
        s.attempts = blocked;
        return false;
    }
    const assets::BuildingType* b = by_id(def.fixed);
    if (b == nullptr || !unlocked(p, *b)) {
        return false;
    }
    if (!usable) {
        return claim_land(p, kSlotIronMine, free_x, free_y);
    }
    if (!affordable(p, *c, *b)) {
        return false;
    }
    if (plan_building(p, kIronMine, *b, kSlotIronMine, false, true)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_gold_mine(AiPlayer& p) {
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    const std::optional<NeedDef> found = need_def(slots(), kSlotGoldMine, m_world.balance().computer_producers);
    if (!found) {
        return false;  // a package removed the gold mine
    }
    const NeedDef& def = *found;
    SlotState& s = r->slots[kSlotGoldMine];
    if (s.count != 0 || s.attempts >= def.attempts) {
        return false;
    }
    bool usable = false;
    std::int32_t free_x = -1;
    std::int32_t free_y = -1;
    for (const auto& d : island_of(c->island).header.deposits) {
        if (d.ware == ware::kGold && d.amount > 0) {
            usable = usable || reachable_deposit(p, d, free_x, free_y);
        }
    }
    if (!usable && free_x < 0) {
        s.attempts = std::max<std::uint8_t>(2, def.attempts);
        return false;
    }
    const assets::BuildingType* b = by_id(def.fixed);
    if (b == nullptr || !unlocked(p, *b)) {
        return false;
    }
    if (!usable) {
        return claim_land(p, kSlotGoldMine, free_x, free_y);
    }
    if (!affordable(p, *c, *b)) {
        return false;
    }
    if (plan_building(p, kGoldMine, *b, kSlotGoldMine, false, true)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::reachable_deposit(AiPlayer& p, const assets::IslandHeader::Deposit& d, std::int32_t& free_x,
                               std::int32_t& free_y) {
    // The original's mine search binds any deposit of the island to the mountain it finds (S-093),
    // so for it every deposit counts. Open1602's stays where it lies: one in another city's land
    // is not searched for, one on free land is claimed first (claim_land).
    if (classic()) {
        return true;
    }
    std::int32_t x = -1;
    std::int32_t y = -1;
    switch (deposit_access(p, d, x, y)) {
        case DepositAccess::Own:
            return true;
        case DepositAccess::Free:
            if (free_x < 0) {
                free_x = x;
                free_y = y;
            }
            return false;
        case DepositAccess::Closed:
            return false;
    }
    return false;
}

bool Engine::claim_land(AiPlayer& p, std::uint8_t slot, std::int32_t x, std::int32_t y) {
    // Open1602: a market that brings the free slope (x, y) into the city's land, or comes nearer
    // to it (task kClaimLand). A failed search counts against the mine's slot, so a deposit that
    // cannot be reached is given up like a mine without a site.
    City* c = current_city(p);
    const assets::BuildingType* b = by_id(id::kMarket);
    if (c == nullptr || b == nullptr || !affordable(p, *c, *b)) {
        return false;
    }
    if (!push(p, kClaimLand, false)) {
        return false;
    }
    start_search(p, *b, slot, true);
    p.search.target_x = x;
    p.search.target_y = y;
    p.search.extra = market_distance(p, x, y);
    reserve(p, *b);
    log(p, "claim land towards %d,%d for slot %u (nearest market %d)", x, y, slot, p.search.extra);
    return true;
}

bool Engine::need_houses(AiPlayer& p, std::uint32_t n) {
    // Houses while affordable, with half a ton of food and more than 1000 gold.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    SlotState& s = r->slots[kSlotHouses];
    if (s.count >= n || s.attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(id::kHouse);
    if (b == nullptr || !unlocked(p, *b) || !affordable(p, *c, *b) || c->stock(m_buildings.food_ware()) <= kTon / 2 ||
        player(p).money <= 1000) {
        return false;
    }
    if (plan_building(p, kHouses, *b, kSlotHouses, false)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_market(AiPlayer& p, std::uint32_t n) {
    // A market while fewer than n stand or the market flag is clear; no unlock test,
    // pushed even without budget.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    SlotState& s = r->slots[kSlotMarket];
    if (!(s.count < n || (r->flags & city_flag::kMarket) == 0) || s.attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(id::kMarket);
    if (b == nullptr) {
        return false;
    }
    if (!affordable(p, *c, *b)) {
        // The original waits for the market, even in a city without a woodcutter that lacks the wood
        // for it: the woodcutter comes later in the order, so the city waits for good. Open1602 goes
        // on with the order then.
        return classic() || r->slots[kSlotWoodcutter].count != 0;
    }
    if (plan_building(p, kMarket, *b, kSlotMarket, true)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_kontor(AiPlayer& p, std::uint32_t level) {
    // The next Kontor up to `level`.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr || level > 4) {
        return false;
    }
    const std::uint32_t have = kontor_level(*c);
    if (have == 0 || have >= level || r->slots[kSlotKontor].attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(id::kontor(have + 1));
    if (b == nullptr || !unlocked(p, *b)) {
        return false;
    }
    if (!affordable(p, *c, *b)) {
        return true;
    }
    if (push(p, kKontorUpgrade, false)) {
        start_search(p, *b, kSlotKontor, false);
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_stone_roads(AiPlayer& p) {
    // Paving with three quarters of the warehouse in bricks and money to spare.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    const assets::BuildingType* road = by_id(id::kRoadStone);
    if (c == nullptr || r == nullptr || road == nullptr || r->slots[kSlotStoneRoads].attempts >= 2) {
        return false;
    }
    const auto& m = m_buildings.materials();
    const std::int32_t money = player(p).money;
    if (c->stock(m.bricks) < c->capacity * 3 / 4 || money <= 2000 || money <= 7 * road->build_cost.money ||
        c->stock(m.bricks) <= 7 * road->build_cost.bricks || !unlocked(p, *road)) {
        return false;
    }
    return push(p, kPaving, false);
}

bool Engine::roads_if_mason(AiPlayer& p) {
    const CityPlan* r = current_plan(p);
    return r != nullptr && r->slots[kSlotStonemason].count != 0 && need_stone_roads(p);
}

bool Engine::need_shipyard(AiPlayer& p) {
    // A yard in the home city once the computer has ships and a weaving mill.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    if (!own_ships(p).empty() && !(p.home_valid && p.home_island == p.island && p.home_city == p.city)) {
        return false;
    }
    if (r->slots[kSlotWebstube].count == 0 && r->slots[kSlotWeberei].count == 0) {
        return false;
    }
    SlotState& s = r->slots[kSlotShipyard];
    if (s.attempts > 3) {
        return false;
    }
    const assets::BuildingType* big = by_id(id::kShipyardBig);
    const assets::BuildingType* small = by_id(id::kShipyardSmall);
    if (s.count == 0) {
        if (slot_present(p, kSlotShipyard) != 0) {
            return false;  // a yard elsewhere (or not every city surveyed yet)
        }
        if (small == nullptr || !unlocked(p, *small)) {
            return false;
        }
        const assets::BuildingType* b = small;
        if (big != nullptr && unlocked(p, *big) && s.attempts <= 1 && affordable(p, *c, *big)) {
            b = big;
        }
        if (!affordable(p, *c, *b)) {
            return false;
        }
        if (plan_building(p, kShipyard, *b, kSlotShipyard, false)) {
            reserve(p, *b);
        }
        return true;
    }
    if (s.count == 1 && big != nullptr && unlocked(p, *big) && affordable(p, *c, *big)) {
        if (plan_building(p, kShipyardUpgrade, *big, kSlotShipyard, false)) {
            reserve(p, *big);
        }
        return true;
    }
    return false;
}

bool Engine::need_tower(AiPlayer& p, std::uint8_t type, std::uint32_t n) {
    // 0 wooden, 1 stone, 2 pirate tower.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    const std::uint8_t slot = type == 0 ? kSlotWoodTower : type == 1 ? kSlotStoneTower : kSlotPirateTower;
    const std::uint32_t building = type == 0 ? id::kWoodTower : type == 1 ? id::kStoneTower : id::kPirateTower;
    SlotState& s = r->slots[slot];
    if (s.count >= n || s.attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(building);
    if (b == nullptr || !unlocked(p, *b) || (!p.pirates && !affordable(p, *c, *b))) {
        return false;
    }
    if (!push(p, kTowers, false)) {
        return false;
    }
    start_search(p, *b, slot, false);
    if (!p.pirates) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_denkmal(AiPlayer& p) {
    // While the player has monuments allowed.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    const Player& pl = player(p);
    if (c == nullptr || r == nullptr || pl.monuments_built >= pl.monuments_allowed ||
        r->slots[kSlotDenkmal].attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(id::kDenkmal);
    if (b == nullptr || !unlocked(p, *b) || !affordable(p, *c, *b)) {
        return false;
    }
    if (plan_building(p, kDenkmal, *b, kSlotDenkmal, false)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_castle(AiPlayer& p) {
    // A castle in the home city while the computer trails the leading human.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr || (m_world.game_settings().flags & World::GameSettings::kNoMilitary) != 0) {
        return false;
    }
    if (!(p.home_valid && p.home_island == p.island && p.home_city == p.city)) {
        return false;
    }
    if (p.castles.size() >= 3 || r->slots[kSlotCastle].attempts >= 2) {
        return false;
    }
    if (!p.castles.empty()) {
        bool war = false;
        for (std::size_t h = 0; h < m_world.players().size(); ++h) {
            if (m_world.players()[h].role == PlayerRole::Human &&
                m_world.diplomacy().relation(p.player, static_cast<PlayerId>(h)).peace != Treaty::Signed) {
                war = true;
            }
        }
        if (p.castles.size() > 1 || !war) {
            return false;
        }
    }
    const assets::BuildingType* b = nullptr;
    for (const std::uint32_t id : {id::kCastle3, id::kCastle2, id::kCastle1}) {
        const assets::BuildingType* t = by_id(id);
        if (t != nullptr && unlocked(p, *t)) {
            b = t;
            break;
        }
    }
    if (b == nullptr || !affordable(p, *c, *b) || !behind(p)) {
        return false;
    }
    const assets::BuildingType* square = by_id(id::kSquare);
    const std::uint32_t ring = square != nullptr ? (b->size_x + b->size_y + 2) * 2 * square->build_cost.bricks : 0;
    const auto& m = m_buildings.materials();
    if (c->stock(m.bricks) < b->build_cost.bricks + ring) {
        return false;
    }
    if (plan_building(p, kCastle, *b, kSlotCastle, false)) {
        reserve(p, *b);
        p.reserved.bricks += remove_goods(*c, m.bricks, ring);
    }
    return true;
}

bool Engine::need_public(AiPlayer& p, std::uint8_t which, std::uint32_t n, bool cov) {
    // A public building while fewer than n stand or houses lack it.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    // The public building of the slot (computer_slots.hpp); none for a removed one.
    const ComputerSlot* def = computer_slot(slots(), which);
    if (def == nullptr || def->kind != ComputerSlot::Kind::Public) {
        return false;
    }
    // The Kapelle always checks its flag; gallows and fountains ignore `cov`.
    if (which == kSlotKapelle || which == kSlotGalgen || which == kSlotBrunnen) {
        cov = true;
    }
    if (!cov) {
        r->flags |= def->flag;  // count only
    }
    SlotState& s = r->slots[which];
    if (!(s.count < n || (r->flags & def->flag) == 0) || s.attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(def->building);
    if (b == nullptr || !unlocked(p, *b) || !affordable(p, *c, *b)) {
        return false;
    }
    if (plan_building(p, def->task, *b, which, false)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::processing(AiPlayer& p) {
    // Workshops for the raw materials the city has.
    const CityPlan* r = current_plan(p);
    if (r == nullptr) {
        return false;
    }
    // In the order of their ranks (of equal ranks by number): one for every so many producers of
    // their raw material.
    std::vector<const ComputerSlot*> by_rank;
    for (const ComputerSlot& k : slots()) {
        if (k.process_rank != 0 && k.process_from != kNoProducer && k.process_per != 0) {
            by_rank.push_back(&k);
        }
    }
    std::ranges::stable_sort(by_rank, {}, &ComputerSlot::process_rank);
    for (const ComputerSlot* k : by_rank) {
        const std::uint32_t from = r->slots[k->process_from].count;
        const std::uint32_t want =
            k->process_round_up ? (from + k->process_per - 1) / k->process_per : from / k->process_per;
        if (r->slots[k->number].count < want) {
            return need(p, k->number, want);
        }
    }
    return false;
}

bool Engine::iron_tools(AiPlayer& p) {
    const City* c = current_city(p);
    if (c == nullptr || c->stock(ware::kOre) <= kTon) {
        return false;
    }
    return need(p, kSlotSmelter, 2) || need(p, kSlotToolsmith, 1);
}

bool Engine::weapons(AiPlayer& p) {
    // The weapon workshops in the home city.
    if ((m_world.game_settings().flags & World::GameSettings::kNoMilitary) != 0 ||
        !(p.home_valid && p.home_island == p.island && p.home_city == p.city)) {
        return false;
    }
    if (!p.castles.empty() && (need(p, kSlotSwords, 1) || need(p, kSlotMuskets, 1))) {
        return true;
    }
    return need(p, kSlotCannons, 1);
}

bool Engine::luxury(AiPlayer& p, std::uint32_t n, bool exclusive) {
    // Luxury raw materials by the island's fertility, what the home city lacks and
    // what the computer can afford to run.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    City* home = home_city(p);
    if (c == nullptr || r == nullptr || home == nullptr || p.max_stage < stage::kSettlers) {
        return false;
    }
    std::int64_t budget = net_income(p);
    if (budget < std::int64_t{p.cities} * 25) {
        if (player(p).money < 7000) {
            return false;
        }
        budget = player(p).money / 100;
    }
    const std::uint32_t cap = home->capacity / 2;
    const auto upkeep = [&](std::uint8_t w, std::uint8_t raw) -> std::int64_t {
        const assets::BuildingType* b = producer(w, raw);
        return b != nullptr ? b->running_cost : 0;
    };
    // 1. Iron
    if (p.score >= 2 && fertility(c->island, ware::kOre) >= 64 && home->stock(ware::kOre) < 10 * kTon) {
        static constexpr std::array<std::uint32_t, 4> kMines{1, 2, 2, 3};
        if (need_iron_mine(p, kMines[std::min<std::uint32_t>(difficulty(), 3)])) {
            return true;
        }
    }
    // 2. Gold
    if (p.score >= 4 && fertility(c->island, ware::kGold) >= 64) {
        const auto gold = need_def(slots(), kSlotGoldMine, m_world.balance().computer_producers);
        const assets::BuildingType* mine = gold ? by_id(gold->fixed) : nullptr;
        if (mine != nullptr && mine->running_cost < budget && need_gold_mine(p)) {
            return true;
        }
    }
    struct Crop {
        std::uint8_t slot;
        std::uint8_t field;
        std::uint8_t good;  ///< what the home city must lack
        std::int64_t upkeep;
        bool eligible;
    };
    bool wine_island = false;
    const std::array<Crop, 6> crops{{
        {kSlotSpices, ware::kSpiceField, ware::kSpices, upkeep(ware::kSpices, ware::kSpiceField), true},
        {kSlotTobacco, ware::kTobaccoField, ware::kTobaccoGoods,
         upkeep(ware::kTobacco, ware::kTobaccoField) + upkeep(ware::kTobaccoGoods, ware::kTobacco), true},
        {kSlotWine, ware::kWineField, ware::kAlcohol, upkeep(ware::kAlcohol, ware::kWineField), true},
        {kSlotCocoa, ware::kCocoaField, ware::kCocoa, upkeep(ware::kCocoa, ware::kCocoaField),
         p.score >= 3 || m_state.max_score >= 3},
        {kSlotCotton, ware::kCottonField, ware::kCloth,
         upkeep(ware::kWool, ware::kCottonField) + upkeep(ware::kCloth, ware::kWool), p.max_stage >= stage::kCitizens},
        {kSlotSugar, ware::kSugarField, ware::kAlcohol,
         upkeep(ware::kSugar, ware::kSugarField) + upkeep(ware::kAlcohol, ware::kSugar), true},
    }};
    for (const Crop& crop : crops) {
        if (!crop.eligible || fertility(c->island, crop.field) != 128) {
            continue;
        }
        if (crop.slot == kSlotWine) {
            wine_island = true;
        }
        if (crop.slot == kSlotSugar && wine_island) {
            continue;
        }
        if (r->slots[crop.slot].count >= n || home->stock(crop.good) >= cap || crop.upkeep >= budget) {
            continue;
        }
        if (need(p, crop.slot, n)) {
            return true;
        }
        if (exclusive && crop.slot != kSlotSugar) {
            return false;
        }
    }
    return false;
}

}  // namespace o1602::sim::ai
