// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fires and plagues (docs/re/disasters.md).
//
// Every 60 to 67 seconds each city of a human or computer player may catch the plague (big cities
// of citizens and better), send out a robber (logistics.hpp) or catch fire (pioneers and settlers).
// A disaster sits on one building and lasts a number of 10-s epochs; every epoch it may spread: a
// plague to a house of the same city within 4 tiles over roads, a fire across buildings, walls and
// forest within 2 or 3 tiles. At the end a plague is over and a fire burns the building down.
// Doctors and firemen (logistics.hpp) end them earlier.
//
// While a house has a disaster, its inhabitants leave (houses.cpp); while a city has one, no house
// grows. A burning workshop does not work.
//
// Unlike the original, disasters are part of the world and do not vanish when a game is loaded
// (S-001), the count of a city follows its buildings when they change city, and every computer
// draws the same random numbers.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// Epoch of the disasters (0x0047A020).
inline constexpr std::uint32_t kDisasterEpochMs = 10'000;
inline constexpr std::uint8_t kPlagueDuration = 20;       ///< epochs
inline constexpr std::uint8_t kFireDuration = 25;         ///< a fire that broke out
inline constexpr std::uint8_t kSpreadDuration = 20;       ///< a fire or plague that spread
inline constexpr std::uint32_t kCityEventMinMs = 60'000;  ///< city events every 60 + 0..7 s

struct DisasterEvent {
    enum class Kind : std::uint8_t {
        Started,     ///< a disaster broke out or spread
        Ended,       ///< a plague is over, or a doctor or fireman ended it
        BurnedDown,  ///< a fire destroyed the building
    };
    Kind kind = Kind::Started;
    Disaster::Kind disaster = Disaster::Kind::Fire;
    std::uint16_t island = 0;
    std::uint16_t x = 0;  ///< origin of the building
    std::uint16_t y = 0;
    std::uint8_t city = MapCell::kNoCity;
    PlayerId owner = kNoPlayer;
    bool spread = false;  ///< Started: caught from a neighbour
};

/// A robber the city tick sends out from a house (the logistics walks him).
struct RobberSpawn {
    std::uint16_t island = 0;
    TilePos house;  ///< origin of the house
    std::uint8_t city = MapCell::kNoCity;
};

/// The disaster on the building with origin (x, y), if any.
[[nodiscard]] const Disaster* disaster_at(const World& world, std::uint16_t island, std::uint16_t x, std::uint16_t y);

/// Starts a disaster on the building covering `tile` (0x00479CA0): one that is already there is
/// replaced. A house shows it, a workshop stops working. False if there is no building or it is a
/// destroyed one.
bool start_disaster(World& world, const assets::BuildingTable& buildings, std::uint16_t island, TilePos tile,
                    Disaster::Kind kind, std::uint8_t duration, std::vector<DisasterEvent>* events,
                    bool spread = false);

/// Ends a disaster of `kind` on the building with origin `origin` (cure, extinguish, the end of a
/// plague): the house and the workshop are well again. False if there was none.
bool end_disaster(World& world, std::uint16_t island, TilePos origin, Disaster::Kind kind,
                  std::vector<DisasterEvent>* events);

/// The building with origin `origin` is gone: its disaster goes with it.
void forget_disaster(World& world, std::uint16_t island, TilePos origin);

/// Counts the disasters of every city by the city the building belongs to now (City::disasters).
void recount_disasters(World& world);

/// One epoch of all disasters (0x0047A020): each lasts one epoch longer and may spread; at its end
/// a plague is over and a fire burns the building down (`construction` gets what that removed).
/// `rules` say whether wood or brick catches fire more easily (OriginalBugs::fire_risk).
void advance_disasters(World& world, const assets::BuildingTable& buildings, const Rules& rules,
                       std::vector<DisasterEvent>* events, std::vector<ConstructionEvent>* construction);

/// The event check of the cities whose time has come (0x0047F8A0): plague, robbers, fire. `rules`
/// say how the gallows and the fire table work (OriginalBugs).
void advance_city_events(World& world, const assets::BuildingTable& buildings, const Rules& rules,
                         std::vector<DisasterEvent>* events, std::vector<RobberSpawn>* robbers);

}  // namespace o1602::sim
