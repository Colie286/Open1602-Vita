// SPDX-License-Identifier: GPL-3.0-or-later
//
// The free trader (docs/re/ships.md, "Free trader"): ships of the trader player that sail from
// Kontor to Kontor, buy what the players sell cheaply and sell what they want dearer than they
// bought it. His goods are kept apart from the holds (the original's HANDLER table).
#include <algorithm>
#include <array>

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/ships.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t kTon = kStockUnitsPerTon;
constexpr std::uint32_t kTraderCheckMs = 10'000;  // one pass over the cities (city epoch)
constexpr std::uint8_t kAgeLimit = 5;
constexpr std::size_t kCandidates = 12;

TraderGood* good_of(Ship& ship, std::uint8_t ware) {
    if (ship.trader_goods.size() <= ware) {
        ship.trader_goods.resize(std::size_t{ware} + 1);
    }
    return &ship.trader_goods[ware];
}

std::uint32_t held(const Ship& ship, std::uint8_t ware) {
    return ware < ship.trader_goods.size() ? ship.trader_goods[ware].amount : 0U;
}

void add_good(Ship& ship, std::uint8_t ware, std::uint32_t amount, std::uint16_t price) {
    TraderGood* g = good_of(ship, ware);
    const std::uint64_t value = std::uint64_t{g->price} * g->amount + std::uint64_t{price} * amount;
    g->amount += amount;
    g->price = g->amount != 0 ? static_cast<std::uint16_t>(std::min<std::uint64_t>(1023, value / g->amount)) : 0;
    g->age = 0;
}

std::uint32_t kontor_key(const City& c) {
    return (std::uint32_t{c.island} << 8) | c.index;
}

}  // namespace

namespace {

/// The free trader's Faction::reach_players, 0 without a trader.
std::size_t trader_reach(const World& world) noexcept {
    const Player* trader = world.player(world.first_of(FactionBehaviour::Trader));
    return trader != nullptr ? world.faction(*trader).reach_players : 0U;
}

}  // namespace

std::size_t trader_reach_players(const World& world, const Rules& rules) noexcept {
    const std::size_t reach = trader_reach(world);
    if (rules.trader_reach == TraderReach::Original || reach == 0) {
        return 0;
    }
    std::size_t players = 0;
    for (const Player& p : world.players()) {
        players += p.role == PlayerRole::Human || p.role == PlayerRole::Computer ? 1U : 0U;
    }
    return players > reach ? players : 0;
}

std::size_t trader_candidates(const World& world, const Rules& rules, std::size_t kontors) noexcept {
    const std::size_t players = trader_reach_players(world, rules);
    if (players == 0) {
        return kCandidates;
    }
    // The map grows with the players (settings_for_players), so this keeps him within about the
    // part of it a game of reach_players (four) would have.
    const std::size_t share = (kontors * trader_reach(world) + players - 1) / players;
    return std::clamp<std::size_t>(share, 3, kCandidates);
}

std::uint16_t Fleet::trader_max_price(std::uint8_t ware) const {
    // 0x00454AC0: three quarters of the average price Kontors pay, half the base price without
    // statistics; nothing for a ware the traders already hold plenty of.
    const std::uint16_t base = m_world->goods().price(ware);
    std::uint32_t p = base * 64U / 128U;
    if (m_rules->trader_prices == TraderPrices::Market && ware < m_market.size() && m_market[ware].demand != 0) {
        const Market& m = m_market[ware];
        p = static_cast<std::uint32_t>(std::min<std::uint64_t>(511, m.demand_value * 3 / 4 / m.demand));
        std::uint64_t traders_hold = 0;
        for (const Ship& s : m_world->ships()) {
            traders_hold += s.trader ? held(s, ware) : 0U;
        }
        // The original compares the other way round (S-079).
        if (traders_hold >= 25 * kTon && 2 * traders_hold >= 3 * m.demand) {
            p = 0;
        }
    }
    return static_cast<std::uint16_t>(p * 134U / 128U);
}

std::uint32_t Fleet::trader_buy_amount(const Ship& ship, const City& city, std::uint8_t ware) const {
    // 0x00454B80: from a Kontor that sells above what it keeps, at most 10 t a visit and 50 t held;
    // goods that have not sold are not topped up.
    const City::Trade* t = city.trade_of(ware);
    if (t == nullptr || !t->sell) {
        return 0;
    }
    const std::uint32_t have = held(ship, ware);
    if (have != 0 && ware < ship.trader_goods.size() && ship.trader_goods[ware].age > 1) {
        return 0;
    }
    const Balance& balance = m_world->balance();
    const std::uint32_t max_held = balance.trader_max_held * kTon;
    if (have >= max_held) {
        return 0;
    }
    const std::uint32_t keep = t->keep + city.reserved_of(ware);
    const std::uint32_t stock = city.stock(ware);
    std::uint32_t avail = stock > keep ? stock - keep : 0U;
    avail = std::min(avail, max_held - have);
    if (avail < kTon || t->sell_price > trader_max_price(ware)) {
        return 0;
    }
    return std::min(avail, balance.trader_max_buy * kTon);
}

std::uint32_t Fleet::trader_sell_amount(const Ship& ship, const City& city, std::uint8_t ware) const {
    // 0x00454C10: to a Kontor that buys up to its limit, only above his own price, as far as the
    // owner's money goes.
    const City::Trade* t = city.trade_of(ware);
    const std::uint32_t have = held(ship, ware);
    if (t == nullptr || !t->buy || have == 0) {
        return 0;
    }
    const std::uint32_t stock = city.stock(ware);
    const std::uint32_t limit = t->limit + city.reserved_of(ware);
    std::uint32_t want = limit > stock ? limit - stock : 0U;
    want = std::min({want, have, city.free_space(ware)});
    if (want < kTon) {
        return 0;
    }
    const std::uint16_t avg = ware < ship.trader_goods.size() ? ship.trader_goods[ware].price : 0;
    if (t->buy_price <= avg || t->buy_price == 0) {
        return 0;
    }
    const Player* owner = m_world->player(city.owner);
    const std::int64_t money = owner != nullptr ? std::max<std::int64_t>(0, owner->money) : 0;
    if (std::int64_t{t->buy_price} * want / kTon > money) {
        want = static_cast<std::uint32_t>(money * kTon / t->buy_price);
    }
    return want;
}

void Fleet::trader_leave(Ship& ship) {
    // 0x00454F50: a trader that reaches its Kontor or gives it up is no longer on its way there
    // (city +0x257 bit 1), so the city counts in full again when the next trader chooses. Fix S-119:
    // also when the trader sinks or his order ends otherwise; the original keeps the mark then.
    if (!ship.trader || ship.target.kind != Target::Kind::City) {
        return;
    }
    if (City* c = m_world->city(ship.target.island, ship.target.city)) {
        c->trader_inbound = false;
    }
}

void Fleet::trader_choose(Ship& ship) {
    // 0x004547E0: of the 12 nearest Kontors of human and computer players (not the last one), the
    // one promising the most profit; half for a Kontor another trader is sailing to already.
    // Rules::trader_reach narrows them on the larger maps of more than four players.
    struct Candidate {
        std::size_t city;
        std::int32_t distance;
    };
    std::vector<Candidate> candidates;
    std::size_t kontors = 0;
    for (std::size_t i = 0; i < m_world->cities().size(); ++i) {
        const City& c = m_world->cities()[i];
        if (!c.warehouse_x || !c.warehouse_y) {
            continue;
        }
        const Player* owner = m_world->player(c.owner);
        if (owner == nullptr || (owner->role != PlayerRole::Human && owner->role != PlayerRole::Computer)) {
            continue;
        }
        if (owner->scenario.has(scenario_flag::kNoTrader)) {
            continue;  // a scenario keeps the trader away from this player (player +0x70 bit 2)
        }
        ++kontors;
        if (ship.previous == Target::city_of(c.island, c.index)) {
            continue;
        }
        const WorldIsland& isl = m_world->islands()[c.island];
        const auto kx = static_cast<std::int32_t>(isl.x + *c.warehouse_x);
        const auto ky = static_cast<std::int32_t>(isl.y + *c.warehouse_y);
        candidates.push_back(Candidate{i, sea_distance(kx - ship.x, ky - ship.y)});
    }
    if (candidates.empty()) {
        return;
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    const bool scaled = trader_reach_players(*m_world, *m_rules) != 0;
    const std::size_t reach = trader_candidates(*m_world, *m_rules, kontors);
    if (candidates.size() > reach) {
        candidates.resize(reach);
    }
    std::int64_t best_score = 0;
    std::optional<std::size_t> best;
    for (const Candidate& cand : candidates) {
        const City& c = m_world->cities()[cand.city];
        std::int64_t score = 0;
        for (const std::uint8_t w : m_world->goods().trade_wares()) {
            const City::Trade* t = c.trade_of(w);
            if (t == nullptr) {
                continue;
            }
            if (const std::uint32_t buy = trader_buy_amount(ship, c, w); buy != 0) {
                score += std::int64_t{buy} * (std::int64_t{trader_max_price(w)} * 166 / 128 - t->sell_price);
            }
            if (const std::uint32_t sell = trader_sell_amount(ship, c, w); sell != 0) {
                const std::uint16_t avg = w < ship.trader_goods.size() ? ship.trader_goods[w].price : 0;
                score += std::int64_t{sell} * (std::int64_t{t->buy_price} - avg);
            }
        }
        if (c.trader_inbound) {
            score /= 2;
        }
        if (score > best_score) {
            best_score = score;
            best = cand.city;
        }
    }
    if (!best && scaled) {
        // Nothing promises profit: the one no trader has chosen for longest, so that the traders
        // spread over the players near them instead of crowding where they happen to be.
        std::uint32_t oldest = 0;
        for (const Candidate& cand : candidates) {
            const City& c = m_world->cities()[cand.city];
            if (c.trader_inbound) {
                continue;
            }
            const auto it = m_trader_chosen.find(kontor_key(c));
            const std::uint32_t age = it != m_trader_chosen.end() ? m_trader_choices - it->second : ~0U;
            if (!best || age > oldest) {
                oldest = age;
                best = cand.city;
            }
        }
    }
    if (!best) {
        // Nothing promises profit: one of them at random, not the one it lies at.
        Random& random = m_world->random(RandomStream::Ships);
        for (int attempt = 0; attempt < 4; ++attempt) {
            const Candidate& cand = candidates[random.below(static_cast<std::uint32_t>(candidates.size()))];
            const City& c = m_world->cities()[cand.city];
            best = cand.city;
            if (candidates.size() == 1 ||
                !building_in_reach(*m_world, *m_buildings, c.island, *c.warehouse_x, *c.warehouse_y, ship.x, ship.y)) {
                break;
            }
        }
    }
    const City& chosen = m_world->cities()[*best];
    if (scaled) {
        m_trader_chosen[kontor_key(chosen)] = ++m_trader_choices;
    }
    City* city = m_world->city(chosen.island, chosen.index);
    city->trader_inbound = true;
    ship.target = Target::city_of(chosen.island, chosen.index);
    ship.motion.failures = 0;
}

bool Fleet::trader_trade(Ship& ship, City& city) {
    // 0x00454D90: buys first, then sells, whole tons; the Kontor's owner pays or earns, the
    // trader's own money never changes. Afterwards his goods age.
    bool traded = false;
    Player* owner = m_world->player(city.owner);
    for (const std::uint8_t w : m_world->goods().trade_wares()) {
        std::uint32_t n = trader_buy_amount(ship, city, w);
        n -= n % kTon;
        if (n == 0) {
            continue;
        }
        const std::uint16_t price = city.trade[w].sell_price;
        city.ensure_ware(w);
        const std::uint32_t taken = std::min(n, city.goods[w]);
        city.goods[w] -= taken;
        city.book(w, GoodsFlow::Sold, taken);
        if (owner != nullptr) {
            earn_from_trade(*owner, std::int64_t{price} * n / kTon);
        }
        city.sales += 32 * (std::int64_t{price} * n / kTon);
        const auto earned = static_cast<std::uint32_t>(std::int64_t{price} * n / kTon);
        city.book(w, GoodsFlow::SoldValue, earned);
        city.book(w, GoodsFlow::TraderSoldValue, earned);
        add_good(ship, w, n, price);
        traded = true;
        m_events.push_back(ShipEvent{ShipEvent::Kind::Traded, ship.id, ship.owner, city.island, city.index, w,
                                     std::int64_t{n}, std::int64_t{price} * n / kTon});
    }
    for (const std::uint8_t w : m_world->goods().trade_wares()) {
        std::uint32_t n = trader_sell_amount(ship, city, w);
        n -= n % kTon;
        if (n == 0) {
            continue;
        }
        const std::uint16_t price = city.trade[w].buy_price;
        city.ensure_ware(w);
        city.goods[w] += n;
        city.book(w, GoodsFlow::Bought, n);
        if (owner != nullptr) {
            pay_for_trade(*owner, std::int64_t{price} * n / kTon);
        }
        city.purchases += 32 * (std::int64_t{price} * n / kTon);
        const auto paid = static_cast<std::uint32_t>(std::int64_t{price} * n / kTon);
        city.book(w, GoodsFlow::BoughtValue, paid);
        city.book(w, GoodsFlow::TraderBoughtValue, paid);
        TraderGood* g = good_of(ship, w);
        g->amount -= std::min(n, g->amount);
        g->age = 0;
        traded = true;
        m_events.push_back(ShipEvent{ShipEvent::Kind::Traded, ship.id, ship.owner, city.island, city.index, w,
                                     -std::int64_t{n}, -std::int64_t{price} * n / kTon});
    }
    for (const std::uint8_t w : m_world->goods().trade_wares()) {
        if (w >= ship.trader_goods.size()) {
            break;
        }
        TraderGood& g = ship.trader_goods[w];
        if (g.amount == 0) {
            continue;
        }
        if (++g.age >= kAgeLimit) {
            // Goods nobody wants: half thrown away, the rest cheaper.
            g.amount /= 2;
            g.price = static_cast<std::uint16_t>(g.price * 85U / 128U);
            g.age = static_cast<std::uint8_t>(g.age / 2);
        }
    }
    return traded;
}

void Fleet::trader_restock(Ship& ship, std::uint32_t dt_ms) {
    // Tools and, once someone mines iron, iron ore come to him from nowhere (0x00454C90, 0x00454D20),
    // unless players offer enough of them; the original counts run ends (S-078).
    const std::uint32_t restock_ms = m_world->balance().trader_restock_s * 1000U;
    const auto supplied = [&](std::uint8_t ware, std::uint32_t max_price) {
        if (m_rules->trader_prices != TraderPrices::Market || ware >= m_market.size()) {
            return false;  // the original's statistics are empty: never enough (S-077)
        }
        const Market& m = m_market[ware];
        return m.offer >= 4 * kTon && m.offer > m.demand * 85 / 128 && m.offer != 0 &&
               m.offer_value / m.offer < max_price;
    };
    if (held(ship, ware::kTools) < 7 * kTon) {
        ship.trader_tools_ms += dt_ms;
        if (ship.trader_tools_ms >= restock_ms) {
            ship.trader_tools_ms = 0;
            if (!supplied(ware::kTools, m_world->goods().price(ware::kTools) * 115U / 128U)) {
                const std::uint32_t n = m_tools_given < m_tools_quota ? 4 * kTon : kTon;
                if (m_tools_given < m_tools_quota) {
                    m_tools_given += 4 * kTon;
                }
                add_good(ship, ware::kTools, n,
                         static_cast<std::uint16_t>(m_world->goods().price(ware::kTools) * 64U / 128U));
            }
        }
    } else {
        ship.trader_tools_ms = 0;
    }
    if (held(ship, ware::kOre) < 5 * kTon) {
        ship.trader_ore_ms += dt_ms;
        if (ship.trader_ore_ms >= restock_ms) {
            ship.trader_ore_ms = 0;
            // Set for good when an iron mine is built (0x00481450).
            bool iron_mined = (m_world->game_settings().flags & World::GameSettings::kIronMined) != 0;
            for (const Production& p : m_world->productions()) {
                if (iron_mined) {
                    break;
                }
                if (!p.used || p.island >= m_world->islands().size()) {
                    continue;
                }
                const IslandMap& map = m_world->islands()[p.island].map;
                if (!map.contains(p.x, p.y)) {
                    continue;
                }
                const MapCell& c = map.top(p.x, p.y);
                const assets::BuildingType* b = c.empty() ? nullptr : m_buildings->by_sprite(c.sprite);
                if (b != nullptr && b->ware == ware::kOre && b->prod_kind == prod::kMine) {
                    iron_mined = true;
                    World::GameSettings settings = m_world->game_settings();
                    settings.flags |= World::GameSettings::kIronMined;
                    m_world->set_game_settings(settings);
                }
            }
            if (iron_mined && !supplied(ware::kOre, m_world->goods().price(ware::kOre) * 115U / 128U)) {
                add_good(ship, ware::kOre, 2 * kTon,
                         static_cast<std::uint16_t>(m_world->goods().price(ware::kOre) / 2));
            }
        }
    } else {
        ship.trader_ore_ms = 0;
    }
}

void Fleet::spawn_trader() {
    const PlayerId trader = m_world->first_of(FactionBehaviour::Trader);
    const assets::FigureType* type = m_figures->get(figure::kTrader);
    if (trader == kNoPlayer || type == nullptr) {
        return;
    }
    // A random tile of the map edge (0x0046AB40); Open1602 makes sure it is water.
    Random& random = m_world->random(RandomStream::Ships);
    const auto w = static_cast<std::int32_t>(m_world->width());
    const auto h = static_cast<std::int32_t>(m_world->height());
    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::uint32_t r = random.next();
        std::int32_t x = 0;
        std::int32_t y = 0;
        switch (r & 3U) {
            case 0:
                x = w - 1;
                y = static_cast<std::int32_t>((r >> 2) % static_cast<std::uint32_t>(h));
                break;
            case 2:
                x = 0;
                y = static_cast<std::int32_t>((r >> 2) % static_cast<std::uint32_t>(h));
                break;
            case 3:
                y = 0;
                x = static_cast<std::int32_t>((r >> 2) % static_cast<std::uint32_t>(w));
                break;
            default:
                y = h - 1;
                x = static_cast<std::int32_t>((r >> 2) % static_cast<std::uint32_t>(w));
                break;
        }
        if (!m_sea.sailable(x, y)) {
            continue;
        }
        Ship s;
        s.name = next_ship_name();
        s.place(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        s.figure = figure::kTrader;
        s.owner = trader;
        s.health = type->ship.max_health;
        s.cargo.resize(std::max<std::size_t>(1, type->ship.cargo_slots));
        s.trader = true;
        s.trader_goods.resize(m_world->goods().end());
        m_world->add_ship(std::move(s));
        m_events.push_back(ShipEvent{ShipEvent::Kind::TraderArrived, m_world->ships().back().id, trader});
        return;
    }
}

void Fleet::trader_tick(std::uint32_t dt_ms) {
    for (Ship& s : m_world->ships()) {
        if (s.trader) {
            trader_restock(s, dt_ms);
        }
    }
    m_trader_ms += dt_ms;
    if (m_trader_ms < kTraderCheckMs) {
        return;
    }
    m_trader_ms -= kTraderCheckMs;
    // Market statistics and the number of Kontors (0x004802A0, 0x004801C0).
    m_market.assign(m_world->goods().end(), Market{});
    std::uint32_t kontors = 0;
    for (const City& c : m_world->cities()) {
        if (!c.warehouse_x || !c.warehouse_y) {
            continue;
        }
        ++kontors;
        for (const std::uint8_t w : m_world->goods().trade_wares()) {
            const City::Trade* t = c.trade_of(w);
            if (t == nullptr) {
                continue;
            }
            const std::uint32_t stock = c.stock(w);
            if (t->buy && t->limit > stock) {
                m_market[w].demand += t->limit - stock;
                m_market[w].demand_value += std::uint64_t{t->buy_price} * (t->limit - stock);
            }
            if (t->sell && stock > t->keep) {
                m_market[w].offer += stock - t->keep;
                m_market[w].offer_value += std::uint64_t{t->sell_price} * (stock - t->keep);
            }
        }
    }
    if ((m_world->game_settings().flags & World::GameSettings::kNoTrader) != 0) {
        return;
    }
    // By default one trader for every three Kontors (none with fewer than two), at most one new
    // per pass.
    const Player* trader = m_world->player(m_world->first_of(FactionBehaviour::Trader));
    const std::uint32_t wanted = trader != nullptr ? m_world->faction(*trader).ships.of(kontors) : 0U;
    std::uint32_t traders = 0;
    for (const Ship& s : m_world->ships()) {
        traders += s.trader ? 1U : 0U;  // counted anew: a sunk trader is replaced (S-080)
    }
    if (traders < wanted) {
        spawn_trader();
    }
}

std::string Fleet::next_ship_name() {
    if (m_ship_names.empty()) {
        return {};  // the game shows a name in its language (MD-10)
    }
    const std::string& name = m_ship_names[m_name_index % m_ship_names.size()];
    ++m_name_index;
    return name;
}

}  // namespace o1602::sim
