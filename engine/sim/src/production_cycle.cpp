// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/production_cycle.hpp"

#include <algorithm>

#include "o1602/assets/savegame.hpp"
#include "o1602/sim/game_data.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t kRateShift = 7;                          // rates are x/128
constexpr std::uint8_t kSpecialFlag = Production::kBurningFlag;  // +0x0F bit 7: burning, no new cycle
constexpr std::uint8_t kMaxIdleCycles = 15;
constexpr std::uint32_t kNoRaw2Fill = 0x180;  // fill of a missing second raw material
constexpr std::uint32_t kFullFill = 0x100;    // asks for more up to twice a cycle

constexpr std::uint16_t scale(std::uint32_t amount, std::uint32_t rate) noexcept {
    return static_cast<std::uint16_t>((amount * rate) >> kRateShift);
}

/// Share (x/128) of a full cycle that `have` covers.
constexpr std::uint32_t fill(std::uint32_t have, std::uint32_t per_cycle) noexcept {
    return (have << kRateShift) / per_cycle;
}

void emit(std::vector<ProductionEvent>* events, ProductionEvent::Kind kind, std::uint32_t index,
          std::uint16_t amount = 0, std::uint8_t ware = 0) {
    if (events != nullptr) {
        events->push_back(ProductionEvent{kind, index, amount, ware});
    }
}

/// Which production kinds fetch their main raw material (the switch at 0x0047DF0B): workshops
/// (carriers), farms and fishers between cycles (farmers, fishing boats), mines (from the deposit),
/// cattle farms, markets and warehouses (carts) and two kinds that need city money.
bool asks_for_main_raw(const assets::BuildingType& b, const Production& p) noexcept {
    switch (b.prod_kind) {
        case prod::kWorkshop:
        case prod::kMine:
        case prod::kPasture:
        case prod::kMarket:
        case prod::kKontor:
        case prod::kKlinik:  // (kinds 0x16, 0x1A: only if the city has money, +0x1FE - checked by the logistics)
        case prod::kBrunnen:
        case prod::kHaupt:
            return true;
        case prod::kFarm:
        case prod::kFisher:
            return p.rate == 0;
        default:
            return false;
    }
}

}  // namespace

void finish_cycle(Production& p, const assets::BuildingType& b, std::uint32_t index,
                  std::vector<ProductionEvent>* events) {
    // End of the running cycle: take the raw material, store the product.
    if (p.rate != 0) {
        const std::uint16_t before = p.stock;
        // At most what is in store: a building upgraded during the cycle, or a damaged save, may
        // have taken less for the cycle than the building now needs.
        p.raw = static_cast<std::uint16_t>(p.raw - std::min<std::uint32_t>(p.raw, scale(b.raw_amount, p.rate)));
        p.raw2 = static_cast<std::uint16_t>(p.raw2 - std::min<std::uint32_t>(p.raw2, scale(b.raw2_amount, p.rate)));
        p.stock = static_cast<std::uint16_t>(p.stock + scale(b.produce_amount, p.rate));
        if (p.stock / assets::kStockUnitsPerTon > before / assets::kStockUnitsPerTon) {
            emit(events, ProductionEvent::Kind::Produced, index,
                 static_cast<std::uint16_t>(p.stock / assets::kStockUnitsPerTon), b.ware);
        }
    }

    // The next cycle.
    std::uint16_t wait = kIdleWaitSeconds;
    std::uint32_t rate = 0;
    if (p.stock < b.max_stock) {
        if (p.raw != 0 && (p.flags & kSpecialFlag) == 0 && b.ware != 0) {
            rate = b.raw_amount == 0 ? Production::kFullRate
                                     : std::min<std::uint32_t>(Production::kFullRate, fill(p.raw, b.raw_amount));
            if (b.raw2_amount != 0) {
                rate = std::min(rate, fill(p.raw2, b.raw2_amount));
            }
            if (rate < kMinRate) {
                rate = 0;
            } else {
                wait = static_cast<std::uint16_t>((b.interval * rate) >> kRateShift);
                p.produced = static_cast<std::uint16_t>(p.produced + scale(b.produce_amount, rate));
                p.idle_cycles = 0;
            }
        }
        if (rate == 0 && p.idle_cycles < kMaxIdleCycles) {
            ++p.idle_cycles;
        }
    } else {
        emit(events, ProductionEvent::Kind::StoreFull, index);
    }
    if ((p.rate != 0) != (rate != 0)) {
        emit(events, rate != 0 ? ProductionEvent::Kind::Started : ProductionEvent::Kind::Stopped, index);
    }
    p.elapsed = static_cast<std::uint16_t>(p.elapsed + wait);
    p.rate = static_cast<std::uint8_t>(rate);
    p.countdown = wait;
    if (p.elapsed > kMaxStatisticsSeconds) {
        p.elapsed = static_cast<std::uint16_t>(p.elapsed >> 1);
        p.produced = static_cast<std::uint16_t>(p.produced >> 1);
    }

    // The doctor and the fire brigade look for work at the end of every cycle, i.e. every 11 s
    // (0x0047DF07: 0x0044AF90, 0x0044B470).
    if (b.prod_kind == prod::kKlinik || b.prod_kind == prod::kBrunnen) {
        emit(events, ProductionEvent::Kind::NeedsRawGoods, index, 0, 0);
        return;
    }

    // What the building asks for (0x0047DE60 ff.): the second raw material if it is the one that
    // is shorter, otherwise the main one - the original dispatches carriers, farmers, miners or
    // market carts depending on the production kind.
    if (p.rate != 0 || p.stock < b.max_stock) {
        const std::uint32_t fill2 = b.raw2_amount == 0 ? kNoRaw2Fill : fill(p.raw2, b.raw2_amount);
        const bool main_short =
            b.raw_amount != 0 && fill(p.raw, b.raw_amount) <= kFullFill && fill(p.raw, b.raw_amount) <= fill2;
        if (!main_short) {
            if (fill2 <= kFullFill) {
                emit(events, ProductionEvent::Kind::NeedsRawGoods, index, 0, b.raw2_ware);
            }
        } else if (asks_for_main_raw(b, p)) {
            emit(events, ProductionEvent::Kind::NeedsRawGoods, index, 0, b.raw_ware);
        }
    }
}

namespace {

/// What an ended cycle made and used, in its city's goods statistics (GoodsLedger). Markets and
/// warehouses only pass goods on.
void book_cycle(World& world, const Production& before, const Production& after, const assets::BuildingType& b,
                std::uint8_t city_index) {
    if (!is_producer(b)) {
        return;
    }
    City* city = world.city(after.island, city_index);
    if (city == nullptr) {
        return;
    }
    if (after.stock > before.stock && world.goods().is_trade(b.ware)) {
        city->book(b.ware, GoodsFlow::Produced, after.stock - before.stock);
    }
    if (before.raw > after.raw && world.goods().is_trade(b.raw_ware)) {
        city->book(b.raw_ware, GoodsFlow::Workshops, before.raw - after.raw);
    }
    if (before.raw2 > after.raw2 && world.goods().is_trade(b.raw2_ware)) {
        city->book(b.raw2_ware, GoodsFlow::Workshops, before.raw2 - after.raw2);
    }
}

}  // namespace

void advance_production(World& world, const assets::BuildingTable& buildings, std::vector<ProductionEvent>* events) {
    std::vector<Production>& list = world.productions();
    for (std::size_t i = 0; i < list.size(); ++i) {
        Production& p = list[i];
        if (!p.used || !p.enabled) {
            continue;
        }
        if (p.countdown != 0) {
            --p.countdown;
            if (p.countdown != 0) {
                continue;
            }
        }
        const sim::MapCell& cell = world.islands()[p.island].map.top(p.x, p.y);
        const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
        if (b == nullptr) {
            continue;
        }
        const Production before = p;
        finish_cycle(p, *b, static_cast<std::uint32_t>(i), events);
        book_cycle(world, before, p, *b, cell.city);
    }
}

}  // namespace o1602::sim
