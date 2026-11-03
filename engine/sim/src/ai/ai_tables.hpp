// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the computer players know of the game's buildings, in one place (MD-4): which slot of a city
// record counts a producer, the Needs that build them, the public buildings and their thresholds,
// the crop of the first colony and the original's cheat amounts. The stage orders that ask the
// Needs are tables in ai_planner.cpp. Builders of a Need pick the record by ware and raw material
// from the building table (`Engine::producer`), so a mod's replacement of a producer is built and
// counted without a change here.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ai_engine.hpp"

#include "o1602/sim/balance.hpp"
#include "o1602/sim/computer_orders.hpp"
#include "o1602/sim/computer_slots.hpp"
#include "o1602/sim/houses.hpp"

namespace o1602::sim::ai {

/// The slot a producer of ware `w` from `raw` (record `building_id`) counts for in the survey: the
/// first slot of `slots` (a game's, computer_slots.hpp) that counts it, those that name a raw
/// material or a record before those that name only the ware.
inline std::optional<std::uint8_t> producer_slot(std::span<const ComputerSlot> slots, std::uint8_t w, std::uint8_t raw,
                                                 std::uint32_t building_id) noexcept {
    for (const bool specific : {true, false}) {
        for (const ComputerSlot& c : slots) {
            if (c.count_ware != 0 && c.count_ware == w && (c.count_raw != 0 || c.count_building != 0) == specific &&
                (c.count_raw == 0 || c.count_raw == raw) &&
                (c.count_building == 0 || c.count_building == building_id)) {
                return c.number;
            }
        }
    }
    return std::nullopt;
}

/// A Need of the production table (table of the Needs).
struct NeedDef {
    std::uint8_t slot;
    std::uint8_t task;
    std::uint8_t ware;    ///< producer of this ware ...
    std::uint8_t raw;     ///< ... from this raw material
    std::uint32_t fixed;  ///< or this building
    std::uint8_t field;   ///< crop of the fields (farm money rule), 0 = none
    NeedType type;
    std::uint8_t attempts;  ///< tried while attempts < this
    std::uint8_t fertile;   ///< the island must grow this crop (0 = no test)
    bool from_start;        ///< mines: the search starts at the first point of segment 0
    /// The producer of its raw material the city must have first, kNoProducer for none.
    std::uint8_t after = kNoProducer;
};

/// The Need of slot `slot` of `slots` (computer_slots.hpp: a producer or a tower); nothing for a
/// slot without one.
inline std::optional<NeedDef> need_def(std::span<const ComputerSlot> slots, std::uint8_t slot) {
    const ComputerSlot* c = computer_slot(slots, slot);
    if (c == nullptr || (c->kind != ComputerSlot::Kind::Producer && c->kind != ComputerSlot::Kind::Tower)) {
        return std::nullopt;
    }
    return NeedDef{
        c->number,   c->task,    c->ware,       c->raw,  c->building, c->field, static_cast<NeedType>(c->wait),
        c->attempts, c->fertile, c->from_start, c->after};
}

static_assert(static_cast<std::uint8_t>(NeedType::D) == static_cast<std::uint8_t>(ProducerWait::Placed) &&
              static_cast<std::uint8_t>(NeedType::C) == static_cast<std::uint8_t>(ProducerWait::Unlock) &&
              static_cast<std::uint8_t>(NeedType::B) == static_cast<std::uint8_t>(ProducerWait::Money));

/// The Need of `slot` with the changes of the packages (Balance::computer_producers): their
/// building, attempts, crop, producer first and wait over the original's. Nothing for no Need.
inline std::optional<NeedDef> need_def(std::span<const ComputerSlot> slots, std::uint8_t slot,
                                       const std::vector<ComputerProducer>& changes) {
    const std::optional<NeedDef> d = need_def(slots, slot);
    if (!d) {
        return std::nullopt;
    }
    NeedDef out = *d;
    if (const ComputerProducer* c = producer_change(changes, slot)) {
        if (c->building) {
            out.fixed = *c->building;
        }
        out.attempts = c->attempts.value_or(out.attempts);
        out.fertile = c->fertile.value_or(out.fertile);
        out.after = c->after.value_or(out.after);
        if (c->wait) {
            out.type = static_cast<NeedType>(*c->wait);  // A to D in the same order
        }
    }
    return out;
}

/// The slot the packages count building `id` for: a producer's changed building or one it counts
/// besides its own (ComputerProducer::counts).
inline std::optional<std::uint8_t> counted_slot(std::uint32_t id, const std::vector<ComputerProducer>& changes) {
    for (const ComputerProducer& c : changes) {
        if (c.building == id || std::find(c.counts.begin(), c.counts.end(), id) != c.counts.end()) {
            return c.slot;
        }
    }
    return std::nullopt;
}

/// A public building task: its building, slot, flag, the service its houses lack and how many
/// such houses a site must reach.
struct PublicTask {
    std::uint8_t task;
    std::uint32_t id;
    std::uint8_t slot;
    std::uint64_t flag;
    assets::ServiceBits service;
    std::int32_t min_houses;
    bool roads_on_fail;
};

/// The public building task `task` of `slots` (computer_slots.hpp), nothing for another task or
/// a removed slot.
inline std::optional<PublicTask> public_task(std::span<const ComputerSlot> slots, std::uint8_t task) {
    for (const ComputerSlot& c : slots) {
        if (c.kind == ComputerSlot::Kind::Public && c.task == task) {
            return PublicTask{c.task, c.building, c.number, c.flag, c.service, c.min_houses, c.roads_on_fail};
        }
    }
    return std::nullopt;
}

/// Open1602: once its stage's orders are done, a city past the settlers with more than kGrowFrom
/// tiles of free land builds its stage's houses, producers, public buildings, luxury fields and
/// towers again, as many times more as it has kGrowLand tiles, up to kGrowMax times. The original's
/// orders were made for its islands (none of its cities has 2000 free tiles) and stop at about 44
/// houses however large the island is.
inline constexpr std::uint32_t kGrowFrom = 2000;
inline constexpr std::uint32_t kGrowLand = 1000;
inline constexpr std::uint32_t kGrowMax = 8;
/// A city at the aristocrats' last stage grows from kGrowLand tiles of free land (the original's
/// large islands) while the computer earns at least this much gold a minute more than it pays.
inline constexpr std::int32_t kGrowIncome = 50;

/// `n` of an order grown for a city with more than `from` tiles of free land (else `n`), in
/// sixteenths so that a little more land already adds a little; rounded up, at most kMaxOrderCount.
inline constexpr std::uint32_t grown_count(std::uint32_t land, std::uint32_t n,
                                           std::uint32_t from = kGrowFrom) noexcept {
    if (land <= from || n == 0) {
        return n;
    }
    const std::uint64_t f = std::min<std::uint64_t>(std::uint64_t{land} * 16 / kGrowLand, 16 * kGrowMax);
    return static_cast<std::uint32_t>(std::min<std::uint64_t>((n * f + 15) / 16, kMaxOrderCount));
}

/// The original's cheats: money and goods the computers get on every difficulty. The defaults are
/// the original's; a game takes them from its balance (`cheats_of`, balance.toml's [computers]).
struct Cheats {
    std::int32_t steady_below = 3791;   ///< task 5: money below this ...
    std::int32_t steady_money = 379;    ///< ... gets this much
    std::int32_t founding_below = 500;  ///< a founding with less money than this ...
    std::int32_t founding_money = 799;  ///< ... gets this much
    std::uint32_t founding_bricks = 8;  ///< tons into a new city
    /// Task 5's subsidy of a computer that is behind, per step: money below the
    /// first number gets the second, below the third (from settlers on) the fourth.
    std::int32_t subsidy_poor_below = 4733, subsidy_poor = 797;
    std::int32_t subsidy_below = 9376, subsidy = 1793;
    /// Tons of wood and tools while the stock is below as much.
    std::uint32_t subsidy_wood = 7, subsidy_tools = 3;
    /// The colony subsidy: one ware per call into a city that is behind, while its stock (tons)
    /// is below the first number, in this order; food before the second inhabited city, bricks after.
    std::uint32_t wood_below = 19, wood = 13;
    std::uint32_t tools_below = 15, tools = 9;
    std::uint32_t food_below = 19, food = 9;
    std::uint32_t bricks_below = 19, bricks = 9;
};

/// The cheats of a game's balance.
inline Cheats cheats_of(const Balance& b) {
    const auto money = [](std::uint32_t v) { return static_cast<std::int32_t>(v); };  // at most 10^6
    Cheats c;
    c.steady_below = money(b.ai_steady_below);
    c.steady_money = money(b.ai_steady_money);
    c.founding_below = money(b.ai_founding_below);
    c.founding_money = money(b.ai_founding_money);
    c.founding_bricks = b.ai_founding_bricks;
    c.subsidy_poor_below = money(b.ai_subsidy_poor_below);
    c.subsidy_poor = money(b.ai_subsidy_poor_money);
    c.subsidy_below = money(b.ai_subsidy_below);
    c.subsidy = money(b.ai_subsidy_money);
    c.subsidy_wood = b.ai_subsidy_wood;
    c.subsidy_tools = b.ai_subsidy_tools;
    c.wood_below = b.ai_colony_wood_below;
    c.wood = b.ai_colony_wood;
    c.tools_below = b.ai_colony_tools_below;
    c.tools = b.ai_colony_tools;
    c.food_below = b.ai_colony_food_below;
    c.food = b.ai_colony_food;
    c.bricks_below = b.ai_colony_bricks_below;
    c.bricks = b.ai_colony_bricks;
    return c;
}

}  // namespace o1602::sim::ai
