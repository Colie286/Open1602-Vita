// SPDX-License-Identifier: GPL-3.0-or-later
//
// Island worlds beyond the original's three fixed plans (Weltengenerator Stufe 1, docs/re/worldgen.md
// "Eigene Welten"). The original has no generator for the layout: each endless game file carries a
// plan with every island's place and size class, and only the contents are drawn when the game
// starts. Open1602 makes such a plan itself for any map size and number of players - how many
// islands, which sizes, where they lie, which climate, where every player starts - and then lets the
// original's content passes (templates, villages, crops, ore, treasures; world_generator.hpp) fill it,
// with a pass that gives every start area the same: a big home island with iron, and all crops and
// gold within reach.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/limits.hpp"

namespace o1602::sim {

/// What a new world looks like (the start dialog's choices; ADR 0008 lists the defaults).
struct WorldSettings {
    enum class Amount : std::uint8_t { None, Few, Normal, Many };
    enum class Sizes : std::uint8_t {
        Small,  ///< mostly little and middle islands
        Mixed,  ///< as the original's plans: about a quarter of each class up to big
        Large,  ///< mostly big islands, some of the largest class (lar)
    };
    enum class Starts : std::uint8_t {
        Spread,    ///< every player has a start area of his own, all alike
        Together,  ///< all ships start at one place, as in the original
    };
    static constexpr std::uint32_t kMinSide = 200;
    static constexpr std::uint32_t kMaxSide = 2000;
    static constexpr std::uint8_t kMaxGoldIslands = 250;  ///< one per island at most

    std::uint32_t width = 500;  ///< tiles; the original map is 500 x 350
    std::uint32_t height = 350;
    std::uint8_t players = 4;           ///< humans and computers with a start, 1..sim::kMaxPlayers
    Amount islands = Amount::Normal;    ///< Normal: as dense as the original (one island per 9500 tiles)
    Sizes sizes = Sizes::Mixed;         ///< mix of size classes
    Amount natives = Amount::Normal;    ///< Normal: four villages per 18 islands, as the original
    Amount volcanoes = Amount::Normal;  ///< islands with a volcano; Normal: one per player
    std::uint8_t difficulty = 0;        ///< 0..3: ore, crops and treasures as the original's levels
    Starts starts = Starts::Spread;
    /// Islands with a gold vein. 0: as the original's plans for the level, scaled with the islands
    /// and players, plus one within reach of every start that has none. Otherwise exactly this many,
    /// one vein each, as far as the islands have free mountains; the starts get theirs first.
    std::uint8_t gold_islands = 0;
    std::uint64_t seed = 0;
};

/// Settings for a world of `players` with as much sea and land per player as the original's
/// 500 x 350 for four (multiplayer games of more than four, o1602-tool bench): both sides grow with
/// the square root of the players; four or fewer keep the original's size. The islands follow the
/// area (WorldSettings::islands).
[[nodiscard]] WorldSettings settings_for_players(std::uint8_t players);

/// The start area of one player: his ship's place, his home island and how far "within reach" is.
struct StartArea {
    std::uint32_t x = 0;  ///< world tile of the ship
    std::uint32_t y = 0;
    std::uint8_t home_island = 0;  ///< island number
    std::uint32_t reach = 0;       ///< tiles from the ship to an island centre that count as near
};

/// An island template of the game directory (Nord/Sued, lit..lar).
struct TemplateInfo {
    std::uint8_t climate = 0;  ///< 0 north, 1 south
    std::uint8_t size = 0;     ///< 0 lit .. 4 lar
    std::uint16_t number = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    bool village = false;  ///< a native version exists (NordNat/SuedNat)
    bool volcano = false;
    std::uint8_t big_mountains = 0;  ///< ore mountains for big veins (Erzbergnr 0)
    std::uint8_t small_mountains = 0;
};

/// All island templates of the game directory; files that cannot be read are left out.
[[nodiscard]] std::vector<TemplateInfo> island_templates(const assets::GameDirectory& game,
                                                         const assets::BuildingTable& buildings);

struct WorldPlan {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// Islands with climate and template fixed, natives per climate, ore, crops and treasures, in
    /// the form of the original's SZENE (island flag bit 1: no village on this island).
    assets::ScenarioPlan plan;
    std::vector<StartArea> starts;  ///< one per player (Together: all at the same place)
    bool shared_start = false;      ///< Together: no start area of one's own to make fair
    std::uint8_t gold_islands = 0;  ///< WorldSettings::gold_islands: 0 as the plan, else exactly this many
};

/// Island flag of a plan entry that the generator keeps free of a native village (home islands).
inline constexpr std::uint8_t kPlanNoVillage = 2;

/// Plans a world. The same settings and seed give the same plan.
[[nodiscard]] core::Result<WorldPlan> plan_world(const WorldSettings& settings,
                                                 const std::vector<TemplateInfo>& templates,
                                                 std::vector<std::string>* warnings = nullptr);

}  // namespace o1602::sim
