// SPDX-License-Identifier: GPL-3.0-or-later
//
// Players of a game. Unlike the original (7 fixed slots, colours 0..6, owner 7 = nobody) the number
// of players and banners is open (ADR 0007); the original values are translated when loading.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "o1602/assets/scenario.hpp"
#include "o1602/sim/faction.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/limits.hpp"

namespace o1602::sim {

/// Index into World::players(). 0xFF means "nobody", so a game can have up to 254 players.
using PlayerId = std::uint8_t;
inline constexpr PlayerId kNoPlayer = 0xFF;
/// Most players a world can hold (every PlayerId but kNoPlayer).
inline constexpr std::size_t kMaxPlayerCount = kNoPlayer;

/// Banner of a player: display colour and flag sprites (render::BannerTable). The base game
/// provides the original colours 0..6 as banners 0..6; mods add more.
using BannerId = std::uint16_t;

/// Who steers a player slot (ADR 0007, section 3). What the slot is, settlers or one of the
/// neutral factions, is its faction (Player::faction).
enum class PlayerRole : std::uint8_t {
    Human,
    Computer,
    Neutral,  ///< nobody: the faction's own behaviour (the trader, the pirates, the natives)
};

/// Whether a player is still in the game (the original's controller 9 and 10).
enum class PlayerStatus : std::uint8_t {
    Playing,
    Defeated,  ///< lost: no city and no ship, bankrupt, or a mission's loss condition
    Absent,    ///< nobody plays the slot: it had no city and no ship when the game started
};

/// The switches a scenario sets for a computer player (PLAYER4 +0x1C, runtime +0x70;
/// docs/re/ai.md and docs/re/diplomacy.md). New games and the endless game have none.
namespace scenario_flag {
inline constexpr std::uint32_t kNoColonies = 0x002;      ///< founds no further cities (0x00426290)
inline constexpr std::uint32_t kNoTrader = 0x004;        ///< the free trader passes its Kontors by (0x004547E0)
inline constexpr std::uint32_t kPassive = 0x008;         ///< no building while at peace with every human (0x0040EEF0)
inline constexpr std::uint32_t kRivalSize = 0x010;       ///< minds humans with more inhabitants than rival_inhabitants
inline constexpr std::uint32_t kFixedArmy = 0x020;       ///< recruits up to a fixed strength (0x00425580)
inline constexpr std::uint32_t kNoFleet = 0x040;         ///< no supply and patrol ships (0x00424BF0)
inline constexpr std::uint32_t kOwnSize = 0x080;         ///< sulks at humans while below own_inhabitants
inline constexpr std::uint32_t kOwnDevelopment = 0x100;  ///< sulks at everyone while below own_development
inline constexpr std::uint32_t kRivalDevelopment = 0x200;  ///< minds humans beyond rival_development
}  // namespace scenario_flag

/// A scenario's switches for a player and the thresholds of its attitude (PLAYER4 +0x1C .. +0x25).
/// Inhabitants are those of all the player's cities; a development is the best city's, 32 per class
/// reached plus the share of its highest class (0x0047F220, 0 .. 159).
struct ScenarioSwitches {
    std::uint32_t flags = 0;              ///< scenario_flag bits (+0x1C)
    std::uint16_t rival_inhabitants = 0;  ///< +0x20, for kRivalSize
    std::uint16_t own_inhabitants = 0;    ///< +0x22, for kOwnSize
    std::uint8_t own_development = 0;     ///< +0x24, for kOwnDevelopment
    std::uint8_t rival_development = 0;   ///< +0x25, for kRivalDevelopment

    [[nodiscard]] bool has(std::uint32_t flag) const noexcept { return (flags & flag) != 0; }
    friend bool operator==(const ScenarioSwitches&, const ScenarioSwitches&) = default;
};

/// Index into World::missions().
using MissionId = std::uint8_t;
inline constexpr MissionId kNoMission = 0xFF;

struct Player {
    PlayerRole role = PlayerRole::Human;
    BannerId banner = 0;
    std::int32_t money = 0;
    std::string name;
    /// Index into World::factions(): the settlers unless the slot is one of a neutral faction.
    FactionId faction = faction::kSettlers;
    /// Unlocked build levels: bit n - 1 = INFRA number n (BuildingTable::infra_levels()). A level
    /// stays unlocked once one city of the player reached it (0x0047F8A0).
    std::uint32_t infra_mask = 0;
    static_assert(infra::kMaxLevel <= 32, "infra_mask holds one bit per level");
    /// Unlocked levels a package added after the original's (33 to 255, levels.toml): bit
    /// (n - 33) % 32 of word (n - 33) / 32. The original has none.
    std::array<std::uint32_t, 7> infra_more{};
    /// Buildings the player may build only once or by allowance (record Bauinfra SCHLOSS,
    /// KATHETRALE, TRIUMPH, DENKMAL).
    std::uint8_t castles_built = 0;
    std::uint8_t cathedrals_built = 0;
    std::uint16_t arches_allowed = 0;
    std::uint16_t arches_built = 0;
    std::uint16_t monuments_allowed = 0;
    std::uint16_t monuments_built = 0;

    /// The player's mission (scenarios; docs/re/missions.md), kNoMission for none.
    MissionId mission = kNoMission;
    PlayerStatus status = PlayerStatus::Playing;
    /// Accomplished the mission (or met it when someone else did); doubles the score.
    bool won = false;
    /// Opponents this player defeated (goal 0x200). The original counts the triumphal arch
    /// allowances (player + 0x8E) instead, which a scenario may preset; loading such a file takes
    /// that value.
    std::uint16_t opponents_defeated = 0;
    /// Fights for the score (PLAYER4 +0x14 .. +0x1A, runtime +0x92 ..): soldiers this player killed
    /// and lost, ships it sank and lost. A sunk ship counts its soldiers aboard as killed and lost
    /// too. Kills count only for humans and computers (the pirates earn nothing).
    /// 32 bits (the original's 16 stop at 65 535, below the kills of a game with 100 000 soldiers);
    /// the .GAM export writes them saturated to 16.
    std::uint32_t soldiers_killed = 0;
    std::uint32_t soldiers_lost = 0;
    std::uint32_t ships_sunk = 0;
    std::uint32_t ships_lost = 0;
    /// 5-s checks in a row with money below -1000 and no positive balance (40 = bankrupt) and
    /// with no city and no ship in a multiplayer game (40 = defeated). The original keeps one
    /// counter for both and never resets it (S-107).
    std::uint8_t bankrupt_checks = 0;
    std::uint8_t empty_checks = 0;
    /// Trade statistics (player + 0x74, + 0x78): every purchase and sale with a Kontor adds 32
    /// times its value, every 5 s they shrink by 1/180. Divided by 480 they are a moving
    /// average per minute (the trade balance of missions and of the status bar).
    std::int64_t purchases = 0;
    std::int64_t sales = 0;
    /// What the scenario asks of this player. Only humans and computers have it; of a human only
    /// kNoTrader counts.
    ScenarioSwitches scenario{};
    /// The player closed the gates of his cities (GateOrder): foreign soldiers cannot pass them.
    /// The original's gates have no state; any soldier walks through their middle.
    bool gates_closed = false;

    [[nodiscard]] bool active() const noexcept { return status == PlayerStatus::Playing; }

    [[nodiscard]] bool unlocked(std::uint32_t level) const noexcept {
        if (level == infra::kNone) {
            return true;
        }
        if (level <= infra::kMaxLevel) {
            return (infra_mask >> (level - 1) & 1U) != 0;
        }
        if (level > std::numeric_limits<std::uint8_t>::max()) {
            return false;  // a building keeps its level in a byte
        }
        const std::uint32_t more = level - infra::kMaxLevel - 1;
        return (infra_more[more / 32] >> (more % 32) & 1U) != 0;
    }

    /// Sets or clears an unlocked level. The levels of the triumphal arch and the monument ask for
    /// 25 000 aristocrats in one city (BAUINFRA), so in practice only an allowance sets them
    /// (op 3 sub 9, 0x55, 0x57) and building the last allowed one clears them (op 0x4D).
    constexpr void set_unlocked(std::uint32_t level, bool on) noexcept {
        if (level == infra::kNone || level > std::numeric_limits<std::uint8_t>::max()) {
            return;
        }
        std::uint32_t* word = &infra_mask;
        std::uint32_t index = level - 1;
        if (level > infra::kMaxLevel) {
            index = level - infra::kMaxLevel - 1;
            word = &infra_more[index / 32];
        }
        const std::uint32_t bit = 1U << (index % 32);
        *word = on ? *word | bit : *word & ~bit;
    }

    /// The step of infra::kShipSteps the player reached (0 below all): how large the pirates' and
    /// the computers' fleets grow.
    [[nodiscard]] std::uint32_t ship_step() const noexcept {
        for (const infra::ShipStep& s : infra::kShipSteps) {
            if (unlocked(s.level)) {
                return s.ships;
            }
        }
        return 0;
    }
};

/// Adds to one of a player's counters, which stop at their largest value.
constexpr void add_count(std::uint16_t& counter, std::uint32_t n) noexcept {
    counter = static_cast<std::uint16_t>(std::min<std::uint32_t>(std::uint32_t{counter} + n, 0xFFFF));
}
constexpr void add_count(std::uint32_t& counter, std::uint32_t n) noexcept {
    counter = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{counter} + n, 0xFFFFFFFFU));
}

/// Owner number of the original files (island tile attribute, SHIP4, PLAYER4 slot): 0..6 are the
/// slots of PLAYER4, everything else (7 = nobody) has no owner.
[[nodiscard]] constexpr PlayerId player_from_original(std::uint8_t owner) noexcept {
    return owner < assets::kOriginalPlayerSlots ? owner : kNoPlayer;
}

[[nodiscard]] constexpr PlayerRole role_from_original(assets::PlayerKind kind) noexcept {
    switch (kind) {
        case assets::PlayerKind::Defeated:
        case assets::PlayerKind::Absent:
            break;  // the role is lost; status_from_original() tells the rest
        case assets::PlayerKind::Computer:
            return PlayerRole::Computer;
        case assets::PlayerKind::Trader:
        case assets::PlayerKind::Pirate:
        case assets::PlayerKind::Natives:
            return PlayerRole::Neutral;
        case assets::PlayerKind::Human:
            break;
    }
    return PlayerRole::Human;
}

/// The base faction of a PLAYER4 slot (base_factions()).
[[nodiscard]] constexpr FactionId faction_from_original(assets::PlayerKind kind) noexcept {
    switch (kind) {
        case assets::PlayerKind::Trader:
            return faction::kTrader;
        case assets::PlayerKind::Pirate:
            return faction::kPirates;
        case assets::PlayerKind::Natives:
            return faction::kNatives;
        case assets::PlayerKind::Human:
        case assets::PlayerKind::Computer:
        case assets::PlayerKind::Defeated:
        case assets::PlayerKind::Absent:
            break;
    }
    return faction::kSettlers;
}

/// Makes `player` a slot of the neutral faction `faction`, steered by its own behaviour.
constexpr void make_neutral(Player& player, FactionId faction) noexcept {
    player.role = PlayerRole::Neutral;
    player.faction = faction;
}

[[nodiscard]] constexpr PlayerStatus status_from_original(assets::PlayerKind kind) noexcept {
    return kind == assets::PlayerKind::Defeated ? PlayerStatus::Defeated
           : kind == assets::PlayerKind::Absent ? PlayerStatus::Absent
                                                : PlayerStatus::Playing;
}

/// Money a player gets from or pays for a trade with a Kontor (0x00475AB0, 0x00475A20): it
/// counts in the trade statistics, unlike taxes, costs or tributes.
/// Money after adding `value`, held within the range of the original's 32-bit field.
[[nodiscard]] inline std::int32_t money_plus(std::int32_t money, std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(std::int64_t{money} + value,
                                                              std::numeric_limits<std::int32_t>::min(),
                                                              std::numeric_limits<std::int32_t>::max()));
}
inline void earn_from_trade(Player& p, std::int64_t value) noexcept {
    p.money = money_plus(p.money, value);
    p.sales += 32 * value;
}
inline void pay_for_trade(Player& p, std::int64_t value) noexcept {
    p.money = money_plus(p.money, -value);
    p.purchases += 32 * value;
}

/// The original has seven slots: four players (red, blue, yellow, white), then the free trader,
/// the pirates and the natives. A new world with more players puts the fifth and those after it
/// behind them, with banners of their own from kFirstExtraBanner on (render::BannerTable).
inline constexpr PlayerId kOriginalPlayers = 4;
inline constexpr PlayerId kFirstExtraPlayer = 7;
inline constexpr BannerId kFirstExtraBanner = 7;

/// Of the four player colours the original has some things in (unit figure types, the spoken
/// "the red player won" messages), the one for a banner: the colours of players five on and of
/// the other banners count as the fourth. Pictures are recoloured instead (render::BannerArt).
[[nodiscard]] constexpr std::int32_t four_colour_index(BannerId banner) noexcept {
    return banner < kOriginalPlayers ? static_cast<std::int32_t>(banner) : kOriginalPlayers - 1;
}

/// The slot of the n-th player with a start (0-based): 0-3, then 7 on.
[[nodiscard]] constexpr PlayerId player_slot(std::size_t n) noexcept {
    return static_cast<PlayerId>(n < kOriginalPlayers ? n : kFirstExtraPlayer + (n - kOriginalPlayers));
}
/// Slots a world of kMaxPlayers holds: the highest player's slot and those before it.
inline constexpr std::size_t kMaxPlayerSlots = std::size_t{player_slot(kMaxPlayers - 1)} + 1;
static_assert(kMaxPlayers >= kOriginalPlayers && kMaxPlayerSlots < kMaxPlayerCount,
              "every player with a start has a slot of its own below kNoPlayer");

/// Original colours map 1:1 to the banners of the base game.
[[nodiscard]] constexpr BannerId banner_from_original(assets::PlayerColour colour) noexcept {
    return static_cast<BannerId>(colour);
}

[[nodiscard]] const char* role_name(PlayerRole role) noexcept;

}  // namespace o1602::sim
