// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/consumption.hpp"

#include <algorithm>
#include <array>
#include <optional>

#include "o1602/assets/savegame.hpp"
#include "o1602/assets/unlock_levels.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/mission.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint8_t kFull = kFullSupply;  // supply level / satisfaction of a fully provided good
constexpr std::uint64_t kSupplyUnit = 256;   // A and B count 1/256 of a stock unit (1/32 t)
constexpr std::uint8_t kHungerLevel = 76;    // "Euer Volk hungert" at or below
constexpr std::uint32_t kNativeKeep = 96;    // natives keep 96/128 of their store
constexpr std::uint32_t kNativeGoodwillFloor = 49;
// Natives' few needs (0x0047EB30): one unit of cloth, alcohol and jewellery per tick.
constexpr std::array<std::uint8_t, 3> kNativeGoods = {ware::kCloth, ware::kAlcohol, ware::kJewellery};
// Pirates (0x0047EEF0): twice per tick, up to 3 t of a random ware with at least 10 t.
constexpr std::uint32_t kPirateMin = 10 * kStockUnitsPerTon;
constexpr std::uint32_t kPirateTake = 3 * kStockUnitsPerTon;

struct Knot {
    std::int32_t x;
    std::int32_t y;
};
// Knots of the tax curve (0x00478630).
constexpr std::array<Knot, 10> kTaxKnots = {{{0, 76},
                                             {76, 89},
                                             {102, 102},
                                             {115, 108},
                                             {128, 128},
                                             {140, 147},
                                             {166, 213},
                                             {179, 256},
                                             {192, 352},
                                             {254, 640}}};

std::array<std::uint32_t, 256> build_tax_curve() {
    std::array<std::uint32_t, 256> t{};
    for (std::size_t k = 0; k + 1 < kTaxKnots.size(); ++k) {
        const Knot a = kTaxKnots[k];
        const Knot b = kTaxKnots[k + 1];
        // 0x004033D0: fixed point with 8 fractional bits.
        const std::int32_t step = ((b.y - a.y) << 8) / (b.x - a.x);
        for (std::int32_t x = a.x; x < b.x; ++x) {
            t[static_cast<std::size_t>(x)] = static_cast<std::uint32_t>(((a.y << 8) + (x - a.x) * step) >> 8);
        }
        t[static_cast<std::size_t>(b.x)] = static_cast<std::uint32_t>(b.y);
    }
    // The original never writes t[255] (0, which would mean "no demand"); the UI never gets there.
    t[255] = t[254];
    return t;
}

std::uint32_t available(const City& c, std::uint8_t ware) {
    return c.available(ware);
}

/// The inhabitants (settlers, natives, pirates) use goods of the warehouse.
void take(City& c, std::uint8_t ware, std::uint32_t amount) {
    c.ensure_ware(ware);
    const std::uint32_t taken = std::min(amount, c.goods[ware]);
    c.goods[ware] -= taken;
    c.book(ware, GoodsFlow::Residents, taken);
}

}  // namespace

std::uint32_t tax_curve(std::uint32_t index) noexcept {
    static const std::array<std::uint32_t, 256> kCurve = build_tax_curve();
    return kCurve[std::min<std::uint32_t>(index, 255)];
}

std::vector<std::uint8_t> consumer_goods(const assets::BuildingTable& buildings) {
    std::vector<std::uint8_t> goods{buildings.food_ware()};
    for (const assets::ResidentClass& c : buildings.resident_classes()) {
        for (const assets::ResidentClass::Need& n : c.needs) {
            goods.push_back(n.ware);
        }
    }
    std::sort(goods.begin(), goods.end());
    goods.erase(std::unique(goods.begin(), goods.end()), goods.end());
    return goods;
}

std::uint8_t satisfaction_of(const City& city, const assets::ResidentClass& rc, std::uint8_t tax_rate) {
    std::uint64_t sum = 0;
    for (const assets::ResidentClass::Need& n : rc.needs) {
        sum += n.ware < city.supply.size() ? city.supply[n.ware].ratio : kFull;
    }
    // 0x0047F3B0: curve[tax'] * Prozent * goods / 128, the tax lowered by the bonus of a castle or
    // cathedral: tax' = (128 - bonus / 32) * tax / 128. The original goes below the curve for a bonus
    // above 4096 (S-038); Open1602 stops at 0.
    const std::uint32_t relief = std::min<std::uint32_t>(kFull, city.satisfaction_bonus >> 5);
    const std::uint32_t index = (kFull - relief) * tax_rate / kFull;
    const std::uint64_t asked = std::uint64_t{tax_curve(index)} * rc.share * rc.needs.size() / kFull;
    return asked == 0 ? kFull : static_cast<std::uint8_t>(std::min<std::uint64_t>(kFull, sum * kFull / asked));
}

void consume(City& city, const assets::BuildingTable& buildings, std::vector<CityEvent>* events) {
    const auto& classes = buildings.resident_classes();
    const std::vector<std::uint8_t> goods = consumer_goods(buildings);
    for (const std::uint8_t w : goods) {
        city.ensure_ware(w);
    }
    if (city.supply.size() < city.goods.size()) {
        city.supply.resize(city.goods.size());
    }
    city.inhabitants.resize(std::max(city.inhabitants.size(), classes.size()), 0);
    city.satisfaction.resize(std::max(city.satisfaction.size(), classes.size()), kFull);
    city.tax_rate.resize(std::max(city.tax_rate.size(), classes.size()), kFull);

    // 1. Take what the demand is ahead of the supply; supply levels; falling supply.
    std::uint8_t worst = 0;
    std::uint8_t worst_ratio = 0xFF;
    for (const std::uint8_t w : goods) {
        City::Supply& s = city.supply[w];
        if (s.demand > s.supplied) {
            // The original always asks for one unit more than missing (kept: it is part of the
            // balance, docs/re/consumption.md).
            const std::uint64_t want = (s.demand - s.supplied) / kSupplyUnit + 1;
            const auto got = static_cast<std::uint32_t>(std::min<std::uint64_t>(want, available(city, w)));
            city.book(w, GoodsFlow::Demand, static_cast<std::uint32_t>(std::min<std::uint64_t>(want, UINT32_MAX)));
            if (got != 0) {
                take(city, w, got);
                s.supplied += std::uint64_t{got} * kSupplyUnit;
            }
        }
        s.history = {s.ratio, s.history[0], s.history[1]};
        // Fix S-027: 64 bits, the original overflows from about 66,000 inhabitants.
        s.ratio = s.demand == 0
                      ? kFull
                      : static_cast<std::uint8_t>(std::min<std::uint64_t>(kFull, s.supplied * kFull / s.demand));
        if (s.ratio < s.history[0] && s.history[0] < s.history[1]) {
            if (events != nullptr) {
                if (s.history[2] >= kFull) {
                    CityEvent e{CityEvent::Kind::Shortage, city.island, city.index, city.owner, w};
                    e.warned = city.ledger.warned(w);
                    events->push_back(e);
                } else if (w == buildings.food_ware() && s.ratio <= kHungerLevel) {
                    events->push_back(CityEvent{CityEvent::Kind::Hunger, city.island, city.index, city.owner, w});
                }
            }
            if (s.ratio < worst_ratio) {
                worst_ratio = s.ratio;
                worst = w;
            }
        }
    }
    city.food_ratio = city.supply[buildings.food_ware()].ratio;
    city.worst_good = worst;

    // 2. Per class (highest first, like the original; the order does not matter): satisfaction
    //    from the supply of the goods it wants, then its demand.
    for (std::size_t c = classes.size(); c-- > 0;) {
        const assets::ResidentClass& rc = classes[c];
        city.satisfaction[c] = satisfaction_of(city, rc, city.tax_rate[c]);
        if (city.inhabitants[c] == 0) {
            city.tax_rate[c] = kFull;  // an empty class goes back to the normal rate
        }
        // The house queued to rise into class c already counts for it, and no longer for the class
        // below (n_eff of 0x0047F8A0).
        const auto queued = [&](std::size_t k) -> std::uint64_t {
            return k < city.upgrade_queue.size() ? city.upgrade_queue[k].inhabitants : 0;
        };
        const std::int64_t n_eff = std::int64_t{city.inhabitants[c]} + static_cast<std::int64_t>(queued(c)) -
                                   static_cast<std::int64_t>(queued(c + 1));
        if (n_eff <= 0) {
            continue;
        }
        const auto n = static_cast<std::uint64_t>(n_eff);
        for (const assets::ResidentClass::Need& need : rc.needs) {
            city.supply[need.ware].demand += std::uint64_t{need.amount} * n;
        }
        city.supply[buildings.food_ware()].demand += std::uint64_t{buildings.food_need()} * n;
    }

    // 3. Decay.
    for (const std::uint8_t w : goods) {
        City::Supply& s = city.supply[w];
        s.demand = s.demand * 15 / 16;
        s.supplied = s.supplied * 15 / 16;
    }
    city.satisfaction_bonus = static_cast<std::uint16_t>(std::uint32_t{city.satisfaction_bonus} * 255 / 256);
}

void consume_natives(City& city) {
    const std::uint32_t keep = city.capacity * kNativeKeep / kFull;
    for (std::uint8_t w = ware::kFirstTrade; w <= ware::kLastTrade && w < city.goods.size(); ++w) {
        if (city.goods[w] >= keep && city.goods[w] >= kStockUnitsPerTon) {
            take(city, w, std::min(city.goods[w], keep / 4));
        }
    }
    for (const std::uint8_t w : kNativeGoods) {
        take(city, w, std::min<std::uint32_t>(available(city, w), 1));
    }
    // Their goodwill fades towards 49 (0x0047EB30): 1/128 every 10 s while above it.
    for (std::uint32_t& g : city.goodwill) {
        if (g > kNativeGoodwillFloor) {
            g = static_cast<std::uint32_t>(std::uint64_t{g} * 127 / 128);
        }
    }
}

void consume_pirates(City& city, Random& random) {
    for (int i = 0; i < 2; ++i) {
        const auto w = static_cast<std::uint8_t>(random.below(ware::kLastTrade + 1));
        if (w >= ware::kFirstTrade && city.stock(w) >= kPirateMin) {
            take(city, w, std::min(city.stock(w), kPirateTake));
        }
    }
}

std::int32_t tax_income(const City& city, const assets::BuildingTable& buildings) {
    const auto& classes = buildings.resident_classes();
    std::int64_t total = 0;
    for (std::size_t c = 0; c < classes.size() && c < city.inhabitants.size(); ++c) {
        const std::int64_t per_minute = std::int64_t{classes[c].tax} * city.inhabitants[c] * 6 / 32;
        const std::int64_t rate = c < city.tax_rate.size() ? city.tax_rate[c] : kFull;
        total += per_minute * rate / kFull;
    }
    return static_cast<std::int32_t>(total);
}

namespace {

/// Adds the running costs of every building to `add(city slot, cost)`; with `only`, of the
/// buildings on that island alone.
template <typename Add>
void for_each_cost(const World& world, const assets::BuildingTable& buildings, std::optional<std::uint16_t> only,
                   Add&& add) {
    const auto& cities = world.cities();
    const auto city_at = [&](std::uint16_t island, std::uint16_t x, std::uint16_t y) -> std::optional<std::size_t> {
        if (only && island != *only) {
            return std::nullopt;
        }
        const IslandMap& map = world.islands()[island].map;
        if (!map.contains(x, y)) {
            return std::nullopt;
        }
        const City* city = world.city(island, map.top(x, y).city);
        if (city == nullptr) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(city - cities.data());
    };
    const auto type_at = [&](std::uint16_t island, std::uint16_t x, std::uint16_t y) -> const assets::BuildingType* {
        const MapCell& cell = world.islands()[island].map.top(x, y);
        return cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
    };
    for (const Production& p : world.productions()) {
        if (!p.used) {
            continue;
        }
        const auto i = city_at(p.island, p.x, p.y);
        const assets::BuildingType* b = i ? type_at(p.island, p.x, p.y) : nullptr;
        if (b != nullptr) {
            add(*i, static_cast<std::int32_t>(p.enabled ? b->running_cost : b->idle_cost));
        }
    }
    for (const CostBuilding& c : world.cost_buildings()) {
        const auto i = city_at(c.island, c.x, c.y);
        const assets::BuildingType* b = i ? type_at(c.island, c.x, c.y) : nullptr;
        if (b == nullptr) {
            continue;
        }
        if (c.kind == CostBuilding::Kind::Castle) {
            const Player* owner = world.player(cities[*i].owner);
            if (owner != nullptr && owner->role == PlayerRole::Human) {
                add(*i, static_cast<std::int32_t>(b->running_cost));
            }
        } else {
            add(*i, static_cast<std::int32_t>(c.enabled ? b->running_cost : b->idle_cost));
        }
    }
}

}  // namespace

std::vector<std::int32_t> running_costs(const World& world, const assets::BuildingTable& buildings) {
    std::vector<std::int32_t> costs(world.cities().size(), 0);
    for_each_cost(world, buildings, std::nullopt, [&](std::size_t i, std::int32_t cost) { costs[i] += cost; });
    return costs;
}

std::int32_t running_cost(const World& world, const assets::BuildingTable& buildings, const City& city) {
    const auto& cities = world.cities();
    if (cities.empty() || &city < cities.data() || &city >= cities.data() + cities.size()) {
        return 0;
    }
    const auto slot = static_cast<std::size_t>(&city - cities.data());
    std::int32_t total = 0;
    for_each_cost(world, buildings, city.island, [&](std::size_t i, std::int32_t cost) {
        if (i == slot) {
            total += cost;
        }
    });
    return total;
}

void unlock_levels(World& world, const assets::BuildingTable& buildings, const City& city,
                   std::vector<CityEvent>* events) {
    Player* owner = world.player(city.owner);
    if (owner == nullptr) {
        return;
    }
    const auto& levels = buildings.infra_levels();
    for (std::size_t n = 1; n < levels.size() && n <= assets::kMaxUnlockLevel; ++n) {
        const auto level = static_cast<std::uint32_t>(n);
        if (owner->unlocked(level)) {
            continue;
        }
        // Inhabitants of the class and all higher ones, of this one city.
        std::uint32_t inhabitants = 0;
        for (std::size_t c = levels[n].resident_class; c < city.inhabitants.size(); ++c) {
            inhabitants += city.inhabitants[c];
        }
        if (inhabitants < levels[n].min_inhabitants) {
            continue;
        }
        // The original posts the whole mask as a value, so two cities unlocking in the same tick
        // could undo each other for a while (M-009); Open1602 only adds the bit.
        owner->set_unlocked(level, true);
        if (events != nullptr) {
            CityEvent e;
            e.kind = CityEvent::Kind::Unlocked;
            e.island = city.island;
            e.city = city.index;
            e.owner = city.owner;
            e.infra = static_cast<std::uint8_t>(n);
            events->push_back(e);
        }
    }
}

void allow_monument(World& world, City& city, std::vector<CityEvent>* events) {
    Player* owner = world.player(city.owner);
    if (owner == nullptr || city.house_growth <= 0) {
        return;
    }
    std::uint32_t inhabitants = 0;
    for (std::size_t c = scoring::kMonumentFirstClass; c < city.inhabitants.size(); ++c) {
        inhabitants += city.inhabitants[c];
    }
    if (inhabitants < scoring::kMonumentMinInhabitants ||
        world.time_ms() < city.growing_since_ms + scoring::kMonumentIntervalMs) {
        return;
    }
    city.growing_since_ms = world.time_ms();
    add_count(owner->monuments_allowed, 1);
    owner->set_unlocked(infra::kMonument, true);  // op 0x55: the level only an allowance reaches
    if (events != nullptr) {
        CityEvent e;
        e.kind = CityEvent::Kind::Monument;
        e.island = city.island;
        e.city = city.index;
        e.owner = city.owner;
        events->push_back(e);
    }
}

std::uint8_t population_message(std::uint32_t einwohner) noexcept {
    const std::uint32_t hundreds = std::min<std::uint32_t>(einwohner / 100, 100);
    if (hundreds >= 10) {
        return static_cast<std::uint8_t>(0x15 + std::min<std::uint32_t>(hundreds / 10, 9) - 1);
    }
    if (hundreds >= 5) {
        return 0x14;
    }
    if (hundreds >= 3) {
        return 0x13;
    }
    return hundreds == 0 ? 0 : static_cast<std::uint8_t>(0x10 + hundreds);
}

void announce_population(City& city, std::uint32_t workers, std::vector<CityEvent>* events) {
    const std::uint8_t message = population_message(city_einwohner(city, workers));
    if (message <= city.population_message) {
        return;
    }
    city.population_message = message;
    if (events != nullptr) {
        CityEvent e;
        e.kind = CityEvent::Kind::Population;
        e.island = city.island;
        e.city = city.index;
        e.owner = city.owner;
        e.message = message;
        events->push_back(e);
    }
}

void remind_material_lock(const World& world, City& city, std::vector<CityEvent>* events) {
    if (!city.material_lock) {
        return;
    }
    const std::uint64_t now = world.time_ms();
    if (city.material_reminder_ms == 0) {
        city.material_reminder_ms = now;
        return;
    }
    if (now < city.material_reminder_ms + kMaterialReminderMs) {
        return;
    }
    city.material_reminder_ms = now;
    if (events != nullptr) {
        CityEvent e;
        e.kind = CityEvent::Kind::MaterialLocked;
        e.island = city.island;
        e.city = city.index;
        e.owner = city.owner;
        events->push_back(e);
    }
}

void advance_cities(World& world, const assets::BuildingTable& buildings, std::vector<CityEvent>* events) {
    const std::vector<std::int32_t> costs = running_costs(world, buildings);
    const std::vector<std::uint32_t> workers = city_workers(world, buildings);
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        const City& c = world.cities()[i];
        City* city = world.city(c.island, c.index);
        Player* owner = world.player(city->owner);
        if (owner == nullptr) {
            continue;
        }
        switch (world.behaviour(*owner)) {
            case FactionBehaviour::Settlers:
                consume(*city, buildings, events);
                // Money (0x0047F8A0): a sixth of the balance per minute, truncated towards zero
                // every 10 s (kept: small balances cost nothing, as in the original).
                owner->money =
                    money_plus(owner->money, static_cast<std::int64_t>((tax_income(*city, buildings) - costs[i]) / 6));
                unlock_levels(world, buildings, *city, events);
                announce_population(*city, workers[i], events);
                allow_monument(world, *city, events);
                remind_material_lock(world, *city, events);
                break;
            case FactionBehaviour::Natives:
                consume_natives(*city);
                break;
            case FactionBehaviour::Pirates:
                consume_pirates(*city, world.random(RandomStream::Pirates));
                break;
            case FactionBehaviour::Trader:
                break;  // the free trader consumes nothing
        }
        // The trade statistics fade (city + 0x1F0, + 0x1F4).
        city->purchases = city->purchases * 89 / 90;
        city->sales = city->sales * 89 / 90;
        city->epoch = static_cast<std::uint8_t>((city->epoch + 1) & 7U);
    }
}

}  // namespace o1602::sim
