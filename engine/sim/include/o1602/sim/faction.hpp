// SPDX-License-Identifier: GPL-3.0-or-later
//
// Factions as data (ADR 0007, section 3; docs/modding/factions.md). A player slot is steered by a
// person, the computer or nobody (PlayerRole) and belongs to a faction: the settlers, the free
// trader, the pirates, the natives, or one of a content package. A faction names the behaviour
// the simulation gives it and the numbers it grows with when more people play.
//
// The world holds its factions (World::factions()) and saves them with the game, so a game keeps
// its factions whatever packages are installed later, and every computer of a multiplayer game
// gets the host's with the savegame.
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace o1602::sim {

/// Index into World::factions().
using FactionId = std::uint8_t;

/// What the simulation does for a faction's players. Until scripts can give a faction behaviour
/// of its own (M8), every faction acts as one of the original's.
enum class FactionBehaviour : std::uint8_t {
    Settlers,  ///< found cities, trade and take part in diplomacy (people and computer players)
    Trader,    ///< the free trader: sails from Kontor to Kontor and buys and sells
    Pirates,   ///< at war with everyone but the trader, hunt ships from their nests
    Natives,   ///< villages on the islands, trade with the settlers
};
inline constexpr FactionBehaviour kLastBehaviour = FactionBehaviour::Natives;

/// A number that grows with the game: `base`, and one more for each `per` of what is counted
/// (begun), held between `min` and `max`; below `from` counted it is 0.
struct Scaling {
    std::uint16_t base = 0;
    std::uint16_t per = 0;  ///< 0: always `base`
    std::uint16_t from = 0;
    std::uint16_t min = 0;
    std::uint16_t max = std::numeric_limits<std::uint16_t>::max();

    [[nodiscard]] constexpr std::uint32_t of(std::uint32_t count) const noexcept {
        if (count < from) {
            return 0;
        }
        const std::uint32_t grown = per == 0 ? 0U : (count + per - 1U) / per;
        return std::min<std::uint32_t>(std::max<std::uint32_t>(base + grown, min), max);  // a mod may cross them
    }
    friend constexpr bool operator==(const Scaling&, const Scaling&) = default;
};

struct Faction {
    /// "anno1602:pirates", "mymod:corsairs": the package and the faction's name in it.
    std::string id;
    FactionBehaviour behaviour = FactionBehaviour::Settlers;
    /// Pirates: their nests by the settler players of the game (Rules::pirate_nests). The original
    /// has one; by default one for every four players begun.
    Scaling nests{0, 4, 0, 1};
    /// The free trader: his ships by the Kontors of the world. The original's: one for every three
    /// Kontors begun, none with fewer than two.
    Scaling ships{0, 3, 2, 0};
    /// The free trader: with more settler players than this he keeps to as many Kontors as a game
    /// of this many players would have (Rules::trader_reach); 0 never.
    std::uint16_t reach_players = 4;

    friend bool operator==(const Faction&, const Faction&) = default;
};

/// The base game's factions, at these indices of base_factions().
namespace faction {
inline constexpr FactionId kSettlers = 0;
inline constexpr FactionId kTrader = 1;
inline constexpr FactionId kPirates = 2;
inline constexpr FactionId kNatives = 3;
}  // namespace faction

/// The package of the original's factions.
inline constexpr std::string_view kBaseFactionPackage = "anno1602";

/// The factions of the original game, as a world without others has them.
[[nodiscard]] std::vector<Faction> base_factions();

/// The base faction that acts as `behaviour` (its index in base_factions()).
[[nodiscard]] constexpr FactionId base_faction(FactionBehaviour behaviour) noexcept {
    return static_cast<FactionId>(behaviour);
}

/// For logs and tools: "settlers", "free trader", "pirates", "natives".
[[nodiscard]] const char* behaviour_name(FactionBehaviour behaviour) noexcept;

/// The file of a content package with its changes to the factions.
inline constexpr std::string_view kFactionFile = "factions.toml";

/// Changes `factions` field by field as the `factions.toml` of `package` (UTF-8) says
/// (docs/modding/factions.md). Problems go to `warnings` with the file and the line; a field with a
/// wrong value is passed over, a file that is no valid data file as a whole.
void apply_faction_file(std::vector<Faction>& factions, std::string_view package, std::string_view utf8,
                        std::vector<std::string>& warnings);

}  // namespace o1602::sim
