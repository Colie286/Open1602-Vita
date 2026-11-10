// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the inhabitants of a city consume (docs/re/consumption.md).
//
// Every 10 s each city takes goods straight from its warehouse; there is no market stock and no
// supply per house (0x0047F8A0). Per consumer good the city keeps a demand A and a supply B, both
// decaying by 15/16 per tick: the city takes what A is ahead of B, and the supply level C =
// B * 128 / A (128 = fully supplied) tells how well the good is provided. Each class of
// inhabitants adds its needs to A, and its satisfaction follows the supply levels of the goods it
// wants measured against its Prozent and the tax rate.
#pragma once

#include <cstdint>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// Game time between two ticks of a city (the original's 10-s epochs).
inline constexpr std::uint32_t kCityTickMs = 10'000;

struct CityEvent {
    enum class Kind : std::uint8_t {
        Shortage,        ///< the supply of `ware` fell twice in a row after it was complete ("Es fehlt an ...")
        Hunger,          ///< the food supply fell twice in a row and is at most 76 ("Euer Volk hungert")
        Unlocked,        ///< the city reached build level `infra` for its owner ("Das Volk wünscht ...")
        Monument,        ///< the owner may build one more monument ("Das treue Volk will Euch ein Denkmal setzen!")
        MaterialLocked,  ///< the owner still keeps the building material from the people (message 0x48)
        Population,      ///< the city passed a mark of inhabitants: `message` 0x11 (100) .. 0x1D (9000)
        RunningLow,      ///< Open1602: `ware` lasts only `minutes` more (goods_statistics.hpp, early warning)
    };
    Kind kind = Kind::Shortage;
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint8_t city = 0;     ///< city on the island
    PlayerId owner = kNoPlayer;
    std::uint8_t ware = 0;
    std::uint8_t infra = 0;     ///< Unlocked: INFRA number
    std::uint8_t message = 0;   ///< Population: MELDUNG number for the owner
    std::uint16_t minutes = 0;  ///< RunningLow: game minutes the stock lasts at the current balance
    bool warned = false;        ///< Shortage: the early warning (RunningLow) came before
};

/// Unlocks the build levels a city reached for its owner (0x0047F8A0): level n once the city has
/// at least Minwohn inhabitants of class BGruppe or higher. Levels stay unlocked.
void unlock_levels(World& world, const assets::BuildingTable& buildings, const City& city,
                   std::vector<CityEvent>* events);

/// The people want a monument (op 0x55 from the city tick 0x0047FD18): a growing city with at
/// least 200 inhabitants of class 2 or higher allows its owner one more monument every 30
/// minutes of game time it keeps growing.
void allow_monument(World& world, City& city, std::vector<CityEvent>* events);

/// The population message for `einwohner` inhabitants (table 0x0054A200, by hundreds): 0x11 from
/// 100, 0x12 from 200, 0x13 from 300, 0x14 from 500, then one more per thousand from 1000 (0x15)
/// up to 0x1D from 9000; 0 below 100. The message for 10000 (0x1E) is never reached.
[[nodiscard]] std::uint8_t population_message(std::uint32_t einwohner) noexcept;

/// Announces a city's population once it passes its next mark (city tick 0x00480016, op 0x48):
/// residents plus `workers` (the Einwohner of the city list). Each mark comes once per city.
void announce_population(City& city, std::uint32_t workers, std::vector<CityEvent>* events);

/// Game time between two complaints about the material lock (10800 in 1/10 s, 0x0047FD67).
inline constexpr std::uint64_t kMaterialReminderMs = 18ULL * 60 * 1000;

/// The people complain while the building material is locked (message 0x48 from the city tick
/// 0x0047FD49): 18 minutes of game time after the lock was switched on, and again every 18
/// minutes. A city without a clock (an original savegame) starts one now.
void remind_material_lock(const World& world, City& city, std::vector<CityEvent>* events);

/// The tax curve (0x0055E780): how much supply a class asks for at a tax rate (128 = normal ->
/// 128; lower taxes ask for less, higher ones for more).
[[nodiscard]] std::uint32_t tax_curve(std::uint32_t index) noexcept;

/// Food and every good some class needs, in ware order (the original: wares 14..21).
[[nodiscard]] std::vector<std::uint8_t> consumer_goods(const assets::BuildingTable& buildings);

/// Satisfaction of a class (0..128) from the supply levels of the goods it wants: their sum
/// against curve[tax] * Prozent * number of goods / 128. 128 for classes that want nothing.
[[nodiscard]] std::uint8_t satisfaction_of(const City& city, const assets::ResidentClass& resident_class,
                                           std::uint8_t tax_rate);

/// One tick of a city of settlers (human or computer player): consumption, supply levels,
/// satisfaction, new demand, decay.
void consume(City& city, const assets::BuildingTable& buildings, std::vector<CityEvent>* events);

/// One tick of a natives' village (0x0047EB30): they dump what fills their store beyond 75 % and
/// use a little cloth, alcohol and jewellery.
void consume_natives(City& city);

/// One tick of a pirates' nest (0x0047EEF0): twice, a random ware with at least 10 t loses up to
/// 3 t.
void consume_pirates(City& city, Random& random);

/// Taxes of a city in gold per minute (0x0047F740): per class trunc(trunc(K * n * 6 / 32) * tax
/// / 128) with K = trunc(Steuer * 16 / 3).
[[nodiscard]] std::int32_t tax_income(const City& city, const assets::BuildingTable& buildings);

/// Running costs per minute of every city (index into World::cities()), as the original keeps them
/// in city + 0x1D8 / + 0x1DC: production buildings and shipyards with their switched-on or -off
/// costs; castles only for human players (computer players never pay them).
[[nodiscard]] std::vector<std::int32_t> running_costs(const World& world, const assets::BuildingTable& buildings);
/// The running costs of one city of `world` (an element of World::cities()), looking only at the
/// buildings of its island; 0 for a city that is not in the list.
[[nodiscard]] std::int32_t running_cost(const World& world, const assets::BuildingTable& buildings, const City& city);

/// Ticks every city of `world` by the role of its owner, including the money: every 10 s the
/// owner gets (taxes - running costs) / 6, truncated like the original.
void advance_cities(World& world, const assets::BuildingTable& buildings, std::vector<CityEvent>* events);

}  // namespace o1602::sim
