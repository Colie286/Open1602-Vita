// SPDX-License-Identifier: GPL-3.0-or-later
//
// Island worlds beyond the original's three fixed plans (Weltengenerator Stufe 1
// "Eigene Welten"). The original has no generator for the layout: each endless game file carries a
// plan with every island's place and size class, and only the contents are drawn when the game
// starts. Open1602 makes such a plan itself for any map size and number of players - how many
// islands, which sizes, where they lie, which climate, where every player starts - and then lets the
// original's content passes (templates, villages, crops, ore, treasures; world_generator.hpp) fill it,
// with a pass that gives every start area the same: a big home island with iron, and all crops and
// gold within reach.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/island_shapes.hpp"
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
    /// The coasts of the islands (world generator stage 2, island_shapes.hpp).
    enum class Shapes : std::uint8_t {
        Templates,  ///< the original's island templates, as in the original
        Mixed,      ///< about half the islands of their own shape, in the templates' sizes
        /// Every island of its own shape and size as `landscape` says, native villages and
        /// volcanoes on them; `islands` and `sizes` do not count then.
        Own,
    };
    /// The islands of a world of islands of their own shape (Shapes::Own).
    struct Landscape {
        /// How the sizes are spread between the smallest and the largest side.
        enum class Spread : std::uint8_t {
            Even,       ///< any size as often
            Mixed,      ///< small ones more often, as the original's plans
            FewGiants,  ///< mostly small islands and a few of the largest
        };
        enum class Gap : std::uint8_t { Narrow, Normal, Wide };  ///< water between two islands
        enum class Outline : std::uint8_t {
            Round,     ///< about as wide as high, one body of land
            Long,      ///< up to three times as long as wide
            Branched,  ///< arms and peninsulas
        };
        static constexpr std::uint8_t kMinLand = 5;  ///< percent of the map
        static constexpr std::uint8_t kMaxLand = 45;

        std::uint8_t land = 15;        ///< percent of the map that is land (the original's plans: about 13)
        std::uint16_t min_side = 30;   ///< tiles, at least shapes::kMinSide
        std::uint16_t max_side = 100;  ///< tiles, at most shapes::kMaxSide
        Spread spread = Spread::Mixed;
        Gap gap = Gap::Normal;
        shapes::Roughness coast = shapes::Roughness::Natural;
        Outline outline = Outline::Round;
        shapes::Share forest = shapes::Share::Normal;
        shapes::Share mountains = shapes::Share::Normal;  ///< ore mountains for the size of an island

        friend bool operator==(const Landscape&, const Landscape&) = default;

        /// Per mille of an island's rectangle that is land (shapes::ShapeRequest::land): as the
        /// templates up to a quarter of the map, fuller above, so that much land fits the map.
        [[nodiscard]] std::uint16_t fill() const;
    };
    static constexpr std::uint32_t kMinSide = 200;
    /// Room for islands of shapes::kMaxSide among the start areas (fit_landscape); a world loads up
    /// to kMaxWorldSide.
    static constexpr std::uint32_t kMaxSide = 4000;
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
    Shapes shapes = Shapes::Templates;
    Landscape landscape;
    std::uint64_t seed = 0;
};

/// The names of the landscape's choices, as setup files and the command line write them, in the
/// order of the enums.
namespace landscape_names {
inline constexpr std::array<std::string_view, 3> kSpread = {"even", "mixed", "giants"};
inline constexpr std::array<std::string_view, 3> kGap = {"narrow", "normal", "wide"};
inline constexpr std::array<std::string_view, 3> kCoast = {"smooth", "natural", "rugged"};
inline constexpr std::array<std::string_view, 3> kOutline = {"round", "long", "branched"};
inline constexpr std::array<std::string_view, 3> kShare = {"little", "normal", "much"};
/// The keys of set_landscape().
inline constexpr std::array<std::string_view, 9> kKeys = {"land",  "min",     "max",    "spread",   "gap",
                                                          "coast", "outline", "forest", "mountains"};
}  // namespace landscape_names

/// A kind of world of islands of their own shape, by name: as they come (the Landscape's
/// defaults), many small ones, a few large bodies of land, a few giants among small ones, and
/// islands all of about one size.
struct LandscapePreset {
    std::string_view name;
    WorldSettings::Landscape landscape;
};
[[nodiscard]] constexpr WorldSettings::Landscape make_landscape(
    std::uint8_t land, std::uint16_t min_side, std::uint16_t max_side, WorldSettings::Landscape::Spread spread,
    WorldSettings::Landscape::Gap gap, shapes::Roughness coast, WorldSettings::Landscape::Outline outline) {
    WorldSettings::Landscape l;
    l.land = land;
    l.min_side = min_side;
    l.max_side = max_side;
    l.spread = spread;
    l.gap = gap;
    l.coast = coast;
    l.outline = outline;
    return l;
}
namespace detail {
using L = WorldSettings::Landscape;
}  // namespace detail
inline constexpr std::array<LandscapePreset, 5> kLandscapePresets = {{
    {"varied", detail::L{}},
    {"archipelago", make_landscape(20, 16, 60, detail::L::Spread::Even, detail::L::Gap::Narrow,
                                   shapes::Roughness::Rugged, detail::L::Outline::Round)},
    {"continents", make_landscape(35, 150, 400, detail::L::Spread::Even, detail::L::Gap::Normal,
                                  shapes::Roughness::Natural, detail::L::Outline::Round)},
    {"giants", make_landscape(30, 25, 500, detail::L::Spread::FewGiants, detail::L::Gap::Wide,
                              shapes::Roughness::Natural, detail::L::Outline::Round)},
    {"even", make_landscape(20, 50, 60, detail::L::Spread::Even, detail::L::Gap::Normal, shapes::Roughness::Smooth,
                            detail::L::Outline::Round)},
}};

/// Sets one setting of `landscape` from text: `key` one of landscape_names::kKeys, `value` a
/// number (land: percent, min and max: tiles) or a name of landscape_names. When the smallest
/// island would be larger than the largest, the other side follows. False, and nothing changed,
/// for an unknown key or a value out of range.
bool set_landscape(WorldSettings::Landscape& landscape, std::string_view key, std::string_view value);

/// Settings for a world of `players` with as much sea and land per player as the original's
/// 500 x 350 for four (multiplayer games of more than four, o1602-tool bench): both sides grow with
/// the square root of the players; four or fewer keep the original's size. The islands follow the
/// area (WorldSettings::islands).
[[nodiscard]] WorldSettings settings_for_players(std::uint8_t players);

/// An automatic map size for islands of their own size (Shapes::Own): `settings`' size, larger
/// where the largest island would find no room - the shorter side at least one and a half times the
/// largest island and the edges, the area at least sixteen times the largest island's (the start
/// areas and their islands come first, a giant needs room between them) - keeping the shape, at
/// most kMaxSide. Other settings keep their size.
[[nodiscard]] WorldSettings fit_landscape(WorldSettings settings);

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
    /// WorldSettings::shapes: which islands get a shape of their own instead of their template (of
    /// the template's size and with as many ore mountains), drawn from `seed`.
    WorldSettings::Shapes shapes = WorldSettings::Shapes::Templates;
    std::uint64_t seed = 0;
    /// Shapes::Own: the islands of their own shape (plan flag kPlanOwnShape) with their sides, and
    /// how they look.
    struct OwnIsland {
        std::uint8_t number = 0;
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        bool volcano = false;  ///< the island has a volcano
        bool village = false;  ///< the island has a native village (of a native template)
    };
    std::vector<OwnIsland> own_islands;
    WorldSettings::Landscape landscape;
};

/// Island flag of a plan entry that the generator keeps free of a native village (home islands).
inline constexpr std::uint8_t kPlanNoVillage = 2;
/// Island flag of a plan entry that is an island of its own shape and size (WorldPlan::own_islands).
inline constexpr std::uint8_t kPlanOwnShape = 4;

/// Plans a world. The same settings and seed give the same plan.
[[nodiscard]] core::Result<WorldPlan> plan_world(const WorldSettings& settings,
                                                 const std::vector<TemplateInfo>& templates,
                                                 std::vector<std::string>* warnings = nullptr);

}  // namespace o1602::sim
