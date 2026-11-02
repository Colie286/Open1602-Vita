// SPDX-License-Identifier: GPL-3.0-or-later
//
// The production cycle of workshops, farms, mines, hunters, fishers and markets, once per game
// second:
//
// A switched-on building counts down the seconds of its running cycle. When the cycle ends,
// the raw material it used is taken and its product added to the store (scaled by the cycle's
// rate). Then the next cycle starts if the store has room and raw material is there: the rate is
// the share of raw material for a full cycle (at most 128 = 100 %, below 64 = 50 % no work), the
// cycle lasts Interval * rate / 128 seconds. Without raw material the building waits 11 seconds
// and counts an idle cycle (the "no raw material" icon appears after Maxnorohst of them).
//
// Raw material is brought by carriers, farmers, miners ...: the cycle reports what a building asks
// for (NeedsRawGoods) and the logistics (logistics.hpp) sends the figures.
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

struct ProductionEvent {
    enum class Kind : std::uint8_t {
        Produced,       ///< the store reached a new whole ton (`amount` = tons in store; floating text)
        StoreFull,      ///< the store is full, no new cycle (floating text "store full")
        Started,        ///< a cycle with rate > 0 started after an idle one
        Stopped,        ///< no new cycle after a working one
        NeedsRawGoods,  ///< the building asks for `ware` (carriers, farmers, miners, market carts)
    };
    Kind kind = Kind::Produced;
    std::uint32_t production = 0;  ///< index into World::productions()
    std::uint16_t amount = 0;
    std::uint8_t ware = 0;
};

/// Seconds a building waits when it cannot start a cycle (11).
inline constexpr std::uint16_t kIdleWaitSeconds = 11;
/// Rates below this (half of the raw material of a cycle) do not start a cycle.
inline constexpr std::uint8_t kMinRate = 64;
/// The statistics window is halved when it grows beyond this many seconds.
inline constexpr std::uint16_t kMaxStatisticsSeconds = 239;

/// One game second of all production buildings of `world`, in list order. `events` may be null.
/// `figures` (may be null) gives the walks of farmers, hunters and fishers for the goods statistics.
void advance_production(World& world, const assets::BuildingTable& buildings, std::vector<ProductionEvent>* events,
                        const assets::FigureTable* figures = nullptr);

/// The rate (x/128) of the cycle the building's raw material allows now; 0 if it allows none (less
/// than kMinRate, burning, no product).
[[nodiscard]] std::uint32_t cycle_rate(const Production& p, const assets::BuildingType& b) noexcept;

/// Game seconds the building needs for its product (produce_amount) when it works at full speed,
/// as its window's productivity counts it: the interval, and for farms, hunters and fishers also
/// the walks of their worker for the raw material (the worker's figure from `figures`, if given).
[[nodiscard]] std::uint32_t full_speed_seconds(const assets::BuildingType& b, const assets::FigureTable* figures);

/// Whether a producer stands still only for want of room: its own store is full, it has the raw
/// material for a cycle, and no cart can take its goods away because the warehouse of `city` is
/// full as well. With more room it would go on (the goods statistics count it as production).
[[nodiscard]] bool waits_for_room(const Production& p, const assets::BuildingType& b, const City& city) noexcept;

/// The cycle step of one building (exposed for tests): called when its countdown has run out.
void finish_cycle(Production& p, const assets::BuildingType& b, std::uint32_t index,
                  std::vector<ProductionEvent>* events);

}  // namespace o1602::sim
