// SPDX-License-Identifier: GPL-3.0-or-later
//
// Missions of scenarios: goals, loss conditions, victory and defeat (docs/re/missions.md).
//
// The original keeps four mission slots (AUFTRAG4); each player names one or none. A mission is a
// set of goals that must all hold at the same time, plus up to two loss conditions. The original
// checks a player every 5 s of game time on the computer that owns the player and posts the win
// as a command; Open1602 checks every player on every computer from the synchronised state, so
// all agree on who won (S-105).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/sim/player.hpp"

namespace o1602::sim {

class World;
struct City;

/// Goal bits of a mission (AUFTRAG4 +0x04, Mission_Evaluate 0x00475440).
namespace goal {
inline constexpr std::uint32_t kCity1 = 0x1;  ///< a city at least cities[0]
inline constexpr std::uint32_t kCity2 = 0x2;  ///< another city at least cities[1]
inline constexpr std::uint32_t kCity3 = 0x4;  ///< a third city at least cities[2]
inline constexpr std::uint32_t kCities3 = kCity1 | kCity2 | kCity3;
inline constexpr std::uint32_t kEveryCity = 0x8;         ///< every own city at least every_city
inline constexpr std::uint32_t kTargetCity = 0x10;       ///< a city of the target player at least target_city
inline constexpr std::uint32_t kCityCount = 0x20;        ///< at least min_cities cities
inline constexpr std::uint32_t kImpossible = 0x40;       ///< never met in the original
inline constexpr std::uint32_t kInhabitants = 0x80;      ///< at least min_inhabitants in all cities
inline constexpr std::uint32_t kBalance = 0x100;         ///< balance per minute at least min_balance
inline constexpr std::uint32_t kDefeatedCount = 0x200;   ///< at least min_defeated opponents defeated
inline constexpr std::uint32_t kDefeat = 0x400;          ///< the players in `defeat` are defeated
inline constexpr std::uint32_t kComputers = 0x800;       ///< not a goal: computers may hold the mission
inline constexpr std::uint32_t kGoods1 = 0x1000;         ///< goods[0] in stock
inline constexpr std::uint32_t kGoods2 = 0x2000;         ///< goods[1] in stock
inline constexpr std::uint32_t kIslands1 = 0x4000;       ///< every island with islands[0].resource, alone
inline constexpr std::uint32_t kIslands2 = 0x8000;       ///< every island with islands[1].resource, alone
inline constexpr std::uint32_t kTradeBalance = 0x10000;  ///< trade balance per minute at least min_trade_balance
inline constexpr std::uint32_t kCastle = 0x20000;        ///< a castle (Schloss) built
inline constexpr std::uint32_t kCathedral = 0x40000;     ///< a cathedral built
inline constexpr std::uint32_t kKnown = 0x7FFFFU & ~kImpossible;
}  // namespace goal

/// Loss conditions (AUFTRAG4 +0x08).
namespace loss {
inline constexpr std::uint32_t kProtectedCity = 0x1;   ///< a city of the protected player stays at least protected_city
inline constexpr std::uint32_t kProtectedCount = 0x2;  ///< the protected player keeps protected_min_cities cities
inline constexpr std::uint32_t kKnown = 0x3;
}  // namespace loss

struct Mission {
    /// A city: at least `inhabitants` Einwohner (house inhabitants and workers, as the city list
    /// shows them) and at least `count` inhabitants of `resident_class` or a higher class.
    struct CityGoal {
        std::int32_t inhabitants = 0;
        std::uint8_t resident_class = 0;
        std::uint32_t count = 0;
        friend bool operator==(const CityGoal&, const CityGoal&) = default;
    };
    struct GoodsGoal {
        std::uint8_t ware = 0;
        std::uint32_t amount = 0;  ///< 1/32 t, summed over the warehouses of all own cities
        friend bool operator==(const GoodsGoal&, const GoodsGoal&) = default;
    };
    /// Settle every island that has a resource, with no human or computer opponent on it.
    struct IslandGoal {
        std::uint8_t resource = 0;  ///< 45..58 a crop the island is fertile for, else an ore or gold deposit
        /// The islands that had it when the mission began (indices into World::islands()). The
        /// original counts them at every load and loses those whose deposit ran out (S-110).
        std::vector<std::uint16_t> islands;
        friend bool operator==(const IslandGoal&, const IslandGoal&) = default;
    };

    std::uint32_t goals = 0;
    std::uint32_t losses = 0;
    /// Target player of goal kTargetCity and the protected player of the loss conditions;
    /// kNoPlayer means any player but the mission's owner.
    PlayerId target = kNoPlayer;
    std::uint16_t min_defeated = 0;
    std::vector<PlayerId> defeat;  ///< players to defeat (goal kDefeat)
    std::int32_t min_cities = 0;
    std::int32_t min_inhabitants = 0;
    std::int32_t min_balance = 0;
    std::int32_t min_trade_balance = 0;
    std::int32_t protected_min_cities = 0;
    /// Message for the winner (MELDUNG number; 0x6B shows the winner's colour, 0x6B..0x6E).
    std::uint16_t success_message = 0x54;
    std::string text;  ///< description, Windows-1252
    std::array<GoodsGoal, 2> goods{};
    std::array<IslandGoal, 2> islands{};
    std::array<CityGoal, 3> cities{};  ///< largest first, as the original sorts them at load
    CityGoal every_city;
    CityGoal target_city;
    CityGoal protected_city;
    /// AUFTRAG4 +0x48 and +0x58: kept for the export, read by nobody.
    std::int32_t unused_48 = 0;
    std::int32_t unused_58 = 0;

    friend bool operator==(const Mission&, const Mission&) = default;
};

/// A mission record of the original files as Open1602 keeps it: the city goals sorted, the goal
/// bits of the city goals taken from them, impossible bits and indices out of range dropped
/// with a warning (S-111). The island lists are still empty (prepare_missions()).
[[nodiscard]] Mission mission_from_record(const assets::MissionRecord& record, std::vector<std::string>* warnings);
/// The record of the export (AUFTRAG4) for slot `slot`.
[[nodiscard]] assets::MissionRecord mission_to_record(const Mission& mission, std::uint32_t slot);

/// Load-time steps of a game with missions (0x0048C0F0, TIMERS loader 0x00484EE1): the islands of
/// the island goals that have no list yet, and settings bit 5 ("defeat the pirates": pirates
/// need a nest for new ships and can be defeated) if a mission of any player asks to defeat
/// them (the original looks at the local player only, S-105).
void prepare_missions(World& world);

/// Einwohner of a city as the city list and the missions count them (0x0047F790): house
/// inhabitants of all classes plus the workers of its buildings.
[[nodiscard]] std::uint32_t city_einwohner(const City& city, std::uint32_t workers) noexcept;

/// Workers per city (city + 0x218, Arbeiter of every building with a running cost, tower or
/// castle), indexed like World::cities().
[[nodiscard]] std::vector<std::uint32_t> city_workers(const World& world, const assets::BuildingTable& buildings);
/// Workers of one city, without the index of all cities (for a single city every frame).
[[nodiscard]] std::uint32_t city_workers(const World& world, const assets::BuildingTable& buildings, const City& city);

/// Military upkeep per minute per player (0x00475B00): soldiers, units in castles and on ships,
/// cannon towers. Computer players pay none.
[[nodiscard]] std::vector<std::uint32_t> military_upkeep(const World& world, const assets::FigureTable& figures);

/// Trade balance per minute (0x00474E80).
[[nodiscard]] std::int32_t trade_balance(const Player& player) noexcept;

/// Balance per minute of a player as the status bar shows it (0x00474ED0): taxes less running
/// costs of all own cities, less the military upkeep, plus the trade balance.
[[nodiscard]] std::int32_t player_balance(const World& world, const assets::BuildingTable& buildings,
                                          const assets::FigureTable& figures, PlayerId player);

/// The figures of the info panel "Spielerstatus" (InfoPanel_Refresh 0x00434420), per minute.
struct PlayerFigures {
    std::uint32_t inhabitants = 0;  ///< Einwohner of all own cities
    std::int64_t taxes = 0;
    std::int64_t running_costs = 0;
    std::int64_t military = 0;
    std::int64_t sales = 0;
    std::int64_t purchases = 0;
    std::int64_t balance = 0;  ///< taxes - running costs - military + sales - purchases
};
[[nodiscard]] PlayerFigures player_figures(const World& world, const assets::BuildingTable& buildings,
                                           const assets::FigureTable& figures, PlayerId player);

/// Whether a player owns a city, a ship that is not sinking (the original's player + 0x86, + 0x9A).
[[nodiscard]] bool owns_city(const World& world, PlayerId player);
[[nodiscard]] bool owns_ship(const World& world, PlayerId player);

/// What a check of a player's mission found.
struct MissionCheck {
    std::uint32_t achieved = 0;  ///< goal bits that hold now
    bool won = false;            ///< all goals hold
    bool lost = false;           ///< a loss condition holds
};

/// Checks the mission of `player` (Mission_Evaluate 0x00475440). A player without a mission, or a
/// computer whose mission does not allow computers, neither wins nor loses.
[[nodiscard]] MissionCheck check_mission(const World& world, const assets::BuildingTable& buildings,
                                         const assets::FigureTable& figures, PlayerId player);

/// Something that decides the game (GameEvent of Simulation::take_game_events()).
struct GameEvent {
    enum class Kind : std::uint8_t {
        MissionAccomplished,  ///< `player` won (also players who met their own mission at that moment), by a
                              ///< mission or by the victory conditions of an endless game (victory.hpp)
        GameOver,             ///< a mission was accomplished: `player` triggered the end
        PlayerDefeated,       ///< `player` is out; `by` is the player credited for it (kNoPlayer: nobody)
        ArchAllowed,          ///< `player` may build a triumphal arch for defeating `by` (message 0x32)
    };
    enum class Reason : std::uint8_t {
        None,
        NoPossessions,  ///< no city and no ship
        Bankrupt,       ///< money below -1000 and no positive balance for 200 s
        MissionFailed,  ///< a loss condition of the mission
        Retired,        ///< a computer with no city and no ship (AI root 0x00429070) or pirates without ships and nest
    };
    Kind kind = Kind::GameOver;
    PlayerId player = kNoPlayer;
    PlayerId by = kNoPlayer;
    Reason reason = Reason::None;
    /// Winners: the success message of the mission or of the victory conditions; GameOver: the
    /// first winner's.
    std::uint16_t message = 0;
};

}  // namespace o1602::sim
