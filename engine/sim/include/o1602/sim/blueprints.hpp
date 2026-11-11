// SPDX-License-Identifier: GPL-3.0-or-later
//
// Blueprints (Open1602, as in later Anno games): buildings a player places without paying for
// them, to build later. A blueprint follows the rules of building except gold and goods: the
// ground, the own city, ore, coast and river, the unlocked levels and the limits of castle and
// cathedral. It holds its place for its owner, who alone sees it; the World keeps them in the order
// they were placed (World::blueprints()), and they are saved with the game.
//
// Building blueprints is an ordinary build order of their items in that order, so what the means do
// not reach stays a blueprint. A city can build its blueprints by itself: every 5 s of game time it
// builds what it can pay (World::blueprint_auto()). Whatever is placed over a blueprint, by its
// owner or not, takes its place: the blueprint is gone.
//
// Every change goes through BlueprintOrder, a command like the others (ADR 0004), so all computers
// of a multiplayer game keep the same blueprints.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// The most blueprints a player may have; more are not placed.
inline constexpr std::size_t kMaxBlueprints = 20000;

/// Game time between two tries of the cities that build their blueprints by themselves.
inline constexpr std::uint32_t kBlueprintAutoMs = 5000;

struct BlueprintOrder {
    enum class Kind : std::uint8_t {
        Place,   ///< `items` become blueprints (what may not go there is left out)
        Remove,  ///< the player's blueprints with a tile in the rectangle `from`..`to` go
        Build,   ///< the player's blueprints in the rectangle, or of the city `city`, are built
        Auto,    ///< the city `city` builds its blueprints by itself (`on`) or not
    };
    PlayerId player = kNoPlayer;
    Kind kind = Kind::Place;
    std::uint16_t island = 0;  ///< index into World::islands()
    std::vector<BuildItem> items;
    TilePos from;
    TilePos to;
    std::uint8_t city = MapCell::kNoCity;  ///< Build: kNoCity takes the rectangle
    bool on = false;
};

/// What becomes of a Place order's items: plan_blueprint() of them as one order, without Kontors
/// (they found cities and are paid from a ship), and only in a city of the player.
[[nodiscard]] BuildPlan plan_blueprints(const World& world, const assets::BuildingTable& buildings,
                                        const BuildOrder& order);

/// Carries out an order. Place: the blueprints that may go there (an older one of the player under
/// them is replaced); Remove and Build: the number removed or built; Auto: 1 if it changed.
std::size_t execute_blueprints(World& world, const assets::BuildingTable& buildings, const BlueprintOrder& order,
                               const std::vector<std::string>& city_names, std::vector<ConstructionEvent>* events);

/// Blueprints whose footprint something placed now covers (Placed events) are gone.
void drop_covered_blueprints(World& world, const assets::BuildingTable& buildings,
                             std::span<const ConstructionEvent> events);

/// Builds what the cities that build by themselves can pay, every kBlueprintAutoMs. Returns the
/// buildings built.
std::size_t build_auto_blueprints(World& world, const assets::BuildingTable& buildings,
                                  const std::vector<std::string>& city_names, std::vector<ConstructionEvent>* events);

/// The city a blueprint lies in (under its origin), MapCell::kNoCity if none.
[[nodiscard]] std::uint8_t blueprint_city(const World& world, const Blueprint& blueprint) noexcept;

/// The footprint of a blueprint (as rotated), nothing if its building is unknown.
struct BlueprintFootprint {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t w = 1;
    std::uint32_t h = 1;
    [[nodiscard]] bool covers(std::int32_t tx, std::int32_t ty) const noexcept {
        return tx >= x && ty >= y && tx < x + static_cast<std::int32_t>(w) && ty < y + static_cast<std::int32_t>(h);
    }
};
[[nodiscard]] std::optional<BlueprintFootprint> blueprint_footprint(const assets::BuildingTable& buildings,
                                                                    const Blueprint& blueprint) noexcept;

/// The player's blueprint covering a tile, as an index into World::blueprints().
[[nodiscard]] std::optional<std::size_t> blueprint_at(const World& world, const assets::BuildingTable& buildings,
                                                      PlayerId player, std::uint16_t island, TilePos tile);

/// Whether the city builds its blueprints by itself.
[[nodiscard]] bool blueprint_auto(const World& world, PlayerId player, std::uint16_t island,
                                  std::uint8_t city) noexcept;

/// What the player's blueprints of a city would cost (all of them).
[[nodiscard]] BuildBill blueprint_costs(const World& world, const assets::BuildingTable& buildings, PlayerId player,
                                        std::uint16_t island, std::uint8_t city);

}  // namespace o1602::sim
