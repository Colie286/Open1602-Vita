// SPDX-License-Identifier: GPL-3.0-or-later
//
// The production cycle of workshops, farms, mines, hunters, fishers and markets, once per game
// second (the original's 0x0047DAF0, docs/re/production.md):
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

/// Seconds a building waits when it cannot start a cycle (0x0047DCEB: 11).
inline constexpr std::uint16_t kIdleWaitSeconds = 11;
/// Rates below this (half of the raw material of a cycle) do not start a cycle.
inline constexpr std::uint8_t kMinRate = 64;
/// The statistics window is halved when it grows beyond this many seconds.
inline constexpr std::uint16_t kMaxStatisticsSeconds = 239;

/// One game second of all production buildings of `world`, in list order. `events` may be null.
void advance_production(World& world, const assets::BuildingTable& buildings, std::vector<ProductionEvent>* events);

/// The cycle step of one building (exposed for tests): called when its countdown has run out.
void finish_cycle(Production& p, const assets::BuildingType& b, std::uint32_t index,
                  std::vector<ProductionEvent>* events);

}  // namespace o1602::sim
