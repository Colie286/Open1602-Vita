// SPDX-License-Identifier: GPL-3.0-or-later
//
// The goods statistics put together (docs/architecture/0012-goods-statistics.md): the cities'
// ledgers (goods_ledger.hpp) closed every game minute, the early warning of a shortage, and the
// sums the window "Warenbilanz" shows for a player's cities, all of them or one island's.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/goods_ledger.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// Game time between two samples of the ledgers.
inline constexpr std::uint64_t kGoodsMinuteMs = 60'000;

/// The early warning: a good the inhabitants use and whose stock, at the balance of the last
/// kWarnWindowMinutes, lasts less than kWarnBelowMinutes. It is given once and taken back when the
/// stock lasts kWarnClearMinutes again (or grows), so that it does not repeat every minute.
inline constexpr std::uint32_t kWarnWindowMinutes = 5;
inline constexpr std::uint32_t kWarnBelowMinutes = 5;
inline constexpr std::uint32_t kWarnClearMinutes = 10;
/// Minutes a ledger must have before it warns (a game just loaded from the original's file).
inline constexpr std::uint32_t kWarnMinMinutes = 2;

/// Minutes the stock lasts at the balance `totals` (net per minute over its minutes); nothing if it
/// does not fall.
[[nodiscard]] std::optional<std::uint32_t> minutes_left(std::uint64_t stock, const FlowTotals& totals) noexcept;

/// The game minute ended: every city's ledger takes its sample, and the cities of human players
/// warn of goods running short (CityEvent::Kind::RunningLow).
void close_goods_minute(World& world, const assets::BuildingTable& buildings, std::vector<CityEvent>* events);

/// Which cities the statistics sum up: a player's, all of them or those on one island.
struct GoodsScope {
    PlayerId player = kNoPlayer;
    std::optional<std::uint16_t> island;

    [[nodiscard]] bool covers(const City& city) const noexcept {
        return city.owner == player && (!island || city.island == *island);
    }
};

/// The figures of the scope's cities by minute (or quarter), added up, oldest first; only human
/// players' cities keep them.
[[nodiscard]] std::vector<CitySample> city_history(const World& world, const GoodsScope& scope, bool quarters);
/// The islands where the player has a city, in island order.
[[nodiscard]] std::vector<std::uint16_t> goods_islands(const World& world, PlayerId player);
/// The flows of a ware over the last `minutes` game minutes in the scope.
[[nodiscard]] FlowTotals goods_totals(const World& world, const GoodsScope& scope, std::uint8_t ware,
                                      std::uint32_t minutes);
/// The ware in the scope's warehouses now, 1/32 t.
[[nodiscard]] std::uint64_t goods_stock(const World& world, const GoodsScope& scope, std::uint8_t ware);
/// The room for the ware in the scope's warehouses, 1/32 t.
[[nodiscard]] std::uint64_t goods_capacity(const World& world, const GoodsScope& scope);
/// One sample of the scope: the cities' samples of the same minute added up.
struct GoodsSample {
    std::array<std::uint64_t, kGoodsFlows> flow{};
    std::uint64_t stock = 0;

    [[nodiscard]] std::uint64_t of(GoodsFlow f) const noexcept { return flow[static_cast<std::size_t>(f)]; }
    [[nodiscard]] std::uint64_t in() const noexcept;
    [[nodiscard]] std::uint64_t out() const noexcept;
};
/// The samples of the last hour (or, `quarters`, of the last twelve hours by quarters), oldest
/// first; cities with a shorter history count from the newest end.
[[nodiscard]] std::vector<GoodsSample> goods_history(const World& world, const GoodsScope& scope, std::uint8_t ware,
                                                     bool quarters);

/// The buildings of the scope that make a ware, of one kind (a fishery, a hunter's hut).
struct GoodsProducers {
    std::uint32_t building = 0;  ///< the kind (HAUS Id)
    std::uint32_t count = 0;
    std::uint32_t working = 0;       ///< in a cycle now
    std::uint32_t short_of_raw = 0;  ///< switched on, idle, its raw material (a trade good) missing
    std::uint32_t off = 0;           ///< switched off by the player
    /// What they made lately, 1/32 t per hour: each building's own statistics window (the last
    /// two to four minutes, as its window shows its productivity).
    std::uint64_t recent = 0;
};
/// The kinds of buildings making the ware, the most productive first.
[[nodiscard]] std::vector<GoodsProducers> goods_producers(const World& world, const assets::BuildingTable& buildings,
                                                          const GoodsScope& scope, std::uint8_t ware);

}  // namespace o1602::sim
