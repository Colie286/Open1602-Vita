// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings of a game (ADR 0008): points where Open1602 lets the players choose, like the
// configurable endless games of later Anno titles, and points that differ between Open1602 and the
// original (ADR 0006).
//
// Only points that change balance or feel are switchable. Bugs and exploits of the original are
// fixed; those whose fix changes how the game plays can be brought back one by one (OriginalBugs,
// the dialog's page "Original settings"). Rules are part of the game state and of a multiplayer
// session, so all peers simulate with the same values.
#pragma once

#include <cstdint>

namespace o1602::sim {

/// What a player gets back when he demolishes a building.
enum class Refund : std::uint8_t {
    None,     ///< nothing, as in the original
    Partial,  ///< Rules::refund_percent of the building's costs
    Full,     ///< all the building's costs
};

/// Whether and how players move their buildings (Open1602, as in later Anno games). The original
/// has no such thing.
enum class Moving : std::uint8_t {
    Free,     ///< a building moves for nothing, keeping its level, stock and production
    Rebuild,  ///< as if built anew: the full costs at the new place, the demolish refund for the old
    Off,      ///< not at all, as in the original
};

/// How often droughts come. The original takes it from the difficulty of
/// the game; the rows are its drought tables.
enum class Droughts : std::uint8_t {
    Game,      ///< as the game's difficulty and settings say (original)
    Off,       ///< never (difficulty "easy", or the game setting "no droughts")
    Rare,      ///< 1 in 32 (the original's "average", by a stray byte before its table, S-043)
    Normal,    ///< 8 in 32, 10 minutes ("demanding")
    Frequent,  ///< 10 in 32, 15 minutes ("hard")
};

/// Whether and how often volcanoes erupt. The original has a game setting for whether.
enum class Volcanoes : std::uint8_t {
    Game,  ///< as the game's settings say (original)
    Off,   ///< never
    On,    ///< always, whatever the game says
    Rare,  ///< as the game's settings say, but half as often: twice the time between eruptions
};

/// How often a plague or a fire breaks out in a town by itself. Fires that lava or
/// a burning neighbour start, and the spreading of a plague, are not counted.
enum class Outbreaks : std::uint8_t {
    Original,  ///< as in the original
    Rare,      ///< half as often
    Off,       ///< never
};

/// Which messages set the game back to normal speed. In the original those of disasters and
/// attacks do.
enum class EventSpeed : std::uint8_t {
    Normal,   ///< every one of them (original)
    Attacks,  ///< attacks only: disasters let the game run on at its speed
    Keep,     ///< none: the game keeps its speed
};

/// The cheats (CheatOrder): as in the original, a code typed in the number line unlocks them, and
/// in a game of more than one person only the code that also lifts that block. Open1602 can have
/// them on from the start or off for good.
enum class Cheats : std::uint8_t {
    Code,   ///< unlocked with the code, as in the original
    Start,  ///< on from the start for every player, also in a multiplayer game
    Off,    ///< never, not even with the code
};

/// Whether pirates come. The original has a game setting for it, which the
/// endless games of the two easy levels set: there they never come.
enum class Pirates : std::uint8_t {
    Game,  ///< as the game's settings say (original)
    Off,   ///< never
    On,    ///< always, whatever the game says
};

/// Whether large iron veins run out. The original's
/// game settings say it per game (TIMERS +0xA8 bit 3): every endless game and about 60 scenarios
/// keep them, the other scenarios use them up.
enum class OreVeins : std::uint8_t {
    Game,     ///< as the game's settings say (original)
    Finite,   ///< 240 t, used up
    Endless,  ///< the deep mine digs on for ever, whatever the game says
};

/// Whether the computer opponents play. The original's game settings can switch them off (TIMERS
/// +0xA8 bit 1); no shipped game does.
enum class Computers : std::uint8_t {
    Game,  ///< as the game's settings say (original)
    Off,   ///< they keep what they have and do nothing
    On,    ///< always, whatever the game says
};

/// How many nests the pirates found. The original has one for the whole world,
/// which suits its four players; with more players most of them would never meet a pirate.
enum class PirateNests : std::uint8_t {
    One,     ///< one nest, as in the original
    Scaled,  ///< as the pirates' faction grows them (one for every four players begun), spread over the map
};

/// Which towns players see without a ship having come near their island.
/// An island counts as discovered for every player as soon as it holds a city the setting names,
/// also one founded later; what else is on it shows with it.
enum class Reveal : std::uint8_t {
    None,      ///< nothing: every island is discovered by ship, also those with the towns of others
    Original,  ///< by ship, but a city a human or a computer founds shows its island to every player
               ///< at once, as the original does; native villages and pirate nests by ship
    All,       ///< the cities of human and computer players, native villages and pirate nests, also
               ///< those there from the start
};

/// What the free trader pays for goods.
enum class TraderPrices : std::uint8_t {
    /// As players know it: about half the base price. The original means to follow the market but
    /// gathers its statistics at a rate that depends on the frame rate, so they are almost always
    /// empty (S-077).
    Original,
    /// What the original intends: three quarters of the average price Kontors pay for the ware;
    /// he stops buying a ware all traders together hold half again as much of as Kontors want.
    Market,
};

/// How far the free trader looks for his next Kontor. The
/// original weighs the 12 nearest Kontors by profit alone, whatever the distance; on the larger maps
/// of more than four players he then mostly sails across the world.
enum class TraderReach : std::uint8_t {
    Original,  ///< the 12 nearest Kontors, a random one when none promises profit
    Scaled,    ///< with more than four players (the faction's reach) only as many as four would have
};

/// The prestige buildings a condition asks for.
enum class Monument : std::uint8_t {
    None,
    Castle,     ///< a castle (Schloss)
    Cathedral,  ///< a cathedral
    Both,       ///< a castle and a cathedral
};

/// The conditions of a victory; 0 (or None, false) switches one off.
struct VictoryConditions {
    /// Einwohner of all own cities, as the city list counts them.
    std::uint32_t inhabitants = 0;
    /// With `class_inhabitants`: residents of `resident_class` (0 pioneers .. 4 aristocrats) or a
    /// higher class in all own cities.
    std::uint8_t resident_class = 4;
    std::uint32_t class_inhabitants = 0;
    /// Gold in the treasury.
    std::uint32_t money = 0;
    /// Balance per minute as the status bar shows it.
    std::uint32_t balance = 0;
    /// Every human and computer opponent defeated.
    bool eliminate = false;
    /// Share of all islands with an own city, in percent (1..100).
    std::uint8_t islands_percent = 0;
    /// Points of the score sheet.
    std::uint32_t score = 0;
    /// Prestige buildings built.
    Monument monument = Monument::None;
    /// Minutes of game time; then the player with the most points wins.
    std::uint32_t time_limit_minutes = 0;
    /// All conditions at the same time, or any one of them.
    bool all = true;
    /// Whether computer players can win too (they are measured by the same conditions).
    bool computers = true;

    /// Whether a condition other than the time limit is on.
    [[nodiscard]] constexpr bool goals() const noexcept {
        return inhabitants != 0 || class_inhabitants != 0 || money != 0 || balance != 0 || eliminate ||
               islands_percent != 0 || score != 0 || monument != Monument::None;
    }
    /// Whether the game can end by a win at all.
    [[nodiscard]] constexpr bool active() const noexcept { return goals() || time_limit_minutes != 0; }

    friend constexpr bool operator==(const VictoryConditions&, const VictoryConditions&) = default;
};

/// Bugs and exploits of the original that a game can bring back (ADR 0008, "Original settings"). Each
/// is fixed in Open1602 by default; its original way is a choice for players who want the game as
/// they know it. Only bugs whose fix changes how the game plays belong here; bugs
/// that only break the game (crashes, saving, griefing other players) stay fixed without a choice.
/// A new one is a bool here and in kAll, the place in engine/sim that reads it, a SetupItem on the
/// dialog's page, a line of settings.cfg and a key of the savegame's RULE.
struct OriginalBugs {
    // Disasters
    /// S-049: buildings with more bricks than wood catch fire with 7 in 8, wooden ones with 3 in 8
    /// (fixed: the other way round).
    bool fire_risk = false;
    /// S-055: a robber a gallows turned away tries up to two more houses, so a town covered by
    /// gallows everywhere still gets 58 % of the robbers (fixed: a quarter).
    bool gallows = false;
    /// S-056: robbers only catch figures that walk to the middle of their building; carriers of a
    /// workshop with its door on the edge are safe in two of the four rotations (fixed: every
    /// carrier and cart on its way home).
    bool robber_victims = false;
    /// S-058: a fleeing robber plans 12 tiles and is gone on the road there, loot and all (fixed: he
    /// walks into the house).
    bool robber_flight = false;

    // Construction and economy
    /// S-002: the figures of a demolished building walk on and deliver what they carry to whatever
    /// stands on its place now; demolish a sheep farm, build a goldsmith there, and the wool comes in
    /// as gold (fixed: their walk ends, the load is lost).
    bool goldsheep = false;
    /// S-008: a building needs only one full edge in the territory, the rest of it (on free land)
    /// joins the city; built and demolished at the edge it widens the territory (fixed: the whole
    /// building within the territory).
    bool building_radius = false;
    /// S-003: a field on ground where its crop does not grow is planted withered and paid for
    /// (fixed: refused).
    bool infertile_fields = false;

    // Ships and trade
    /// S-012: the trade window stays open while the ship sails off, and trading goes on with the
    /// Kontor it was opened at (fixed: only within reach of the Kontor).
    bool remote_trade = false;
    /// S-013: a ship being repaired keeps being repaired when it sails off (fixed: only at the yard).
    bool remote_repair = false;
    /// S-005: the small shipyard builds the large ships too when the window still has one chosen
    /// (fixed: the order checks the yard).
    bool small_yard_warships = false;

    // Opponents
    /// A native village that lost its chief's house, market and huts keeps its land: the ruins hold
    /// it as a destroyed Kontor does, and nobody can build there; so does a computer player's city
    /// without Kontor, market, tower and houses (fixed: the village or city is gone and its land is
    /// free).
    bool native_land = false;
    /// S-097 to S-099, the original's computer players: while a settlement ship
    /// sails no city develops, a city whose colony search failed waits for it for ever, an
    /// unreachable site is searched afresh (and found again), the fisher looks in two directions
    /// only, mines and quarries only within seven tiles of the road runs, castles never count and
    /// bakeries are asked for without a mill. They stall after about two hours where Open1602's
    /// grow on.
    bool classic_ai = false;

    /// Every bug, for counting and switching them all.
    static constexpr bool OriginalBugs::*kAll[] = {
        &OriginalBugs::fire_risk,           &OriginalBugs::gallows,      &OriginalBugs::robber_victims,
        &OriginalBugs::robber_flight,       &OriginalBugs::goldsheep,    &OriginalBugs::building_radius,
        &OriginalBugs::infertile_fields,    &OriginalBugs::remote_trade, &OriginalBugs::remote_repair,
        &OriginalBugs::small_yard_warships, &OriginalBugs::native_land,  &OriginalBugs::classic_ai,
    };

    /// How many of them are as in the original.
    [[nodiscard]] constexpr std::uint32_t count() const noexcept {
        std::uint32_t n = 0;
        for (const auto bug : kAll) {
            n += this->*bug ? 1U : 0U;
        }
        return n;
    }
    /// Every one as in the original.
    [[nodiscard]] static constexpr OriginalBugs all() noexcept {
        OriginalBugs b;
        for (const auto bug : kAll) {
            b.*bug = true;
        }
        return b;
    }

    friend constexpr bool operator==(const OriginalBugs&, const OriginalBugs&) = default;
};

struct Rules {
    /// Refund of the build costs (gold and goods) when the player demolishes a building. The goods
    /// go back into the warehouse of the building's city as far as there is room.
    Refund demolish_refund = Refund::None;
    /// Share for Refund::Partial, in percent (1..100). 50 by default: a wrong building costs half
    /// its price, moving one stays affordable.
    std::uint8_t refund_percent = 50;
    /// Moving buildings (the tool in the demolish button's menu): free by default, off with the
    /// original's settings.
    Moving moving = Moving::Free;

    /// L-001: ships per player, shipyard orders included. 0 = unlimited; the original allows 33,
    /// Open1602 100.
    std::uint32_t max_ships_per_player = 100;
    /// L-008: units per player (on land, in castles, on board). 0 = unlimited; the original allows 100,
    /// Open1602 250.
    std::uint32_t max_army = 250;

    /// S-019: random worlds contain at least one deposit of every resource that a building needs
    /// (the original can generate endless games without gold).
    bool guarantee_resources = true;

    /// L-004: highest game speed factor (original: 4, with an unintended 8 via Shift+F8).
    std::uint32_t max_game_speed = 8;

    /// Droughts: as the game says, or chosen for the endless game.
    Droughts droughts = Droughts::Game;
    /// Volcanoes: as the game says, rarer, or switched on or off.
    Volcanoes volcanoes = Volcanoes::Game;
    /// Plagues breaking out in towns: as in the original, half as often, or never.
    Outbreaks plagues = Outbreaks::Original;
    /// Fires breaking out in towns: as in the original, half as often, or never.
    Outbreaks fires = Outbreaks::Original;
    /// Which messages set the game back to normal speed. The host's computer decides for a
    /// multiplayer game, as it does for every change of the speed.
    EventSpeed event_speed = EventSpeed::Normal;
    /// Pirates: as the game says, or switched on or off.
    Pirates pirates = Pirates::Game;
    /// Pirate nests: one, or as the faction scales them. Up to four players both are the original.
    PirateNests pirate_nests = PirateNests::Scaled;
    /// Prices of the free trader.
    TraderPrices trader_prices = TraderPrices::Original;
    /// How far the free trader looks. Up to four players both are the original.
    TraderReach trader_reach = TraderReach::Scaled;
    /// Whether large iron veins run out. Endless: the
    /// ore mine still digs only down to 120 t; below that the deep mine digs on, as in the original,
    /// but the vein stays at the last ton above it instead of being used up. Small veins run out
    /// either way, gold never does.
    OreVeins ore_veins = OreVeins::Game;
    /// Whether the computer opponents play; the pirates have their own setting.
    Computers computer_players = Computers::Game;

    /// The revised bread chain (docs/modding/balance.md, data/balance/revised_bread.toml): a grain
    /// farm makes two tons of grain from a field, a bakery two tons of bread from two tons of flour
    /// at 15 gold a minute instead of 5. The original's chain gives 1.5 t of bread a minute from 20
    /// fields where two cattle farms and a butcher give 2.4 t of meat; revised it gives 6.3 t at
    /// the same running cost per ton as meat. Its values go under those of the mods' balance.toml.
    /// On by default; the original's settings keep the original's chain.
    bool revised_bread = true;

    /// Open1602: computer players buy ships other players offer for sale when they are short of
    /// ships (the History Edition's do; the original's buy one only while they have neither ship
    /// nor yard). Off with the original's settings.
    bool computers_buy_ships = true;

    /// Open1602: a player gets more than one trade cart on an island (Shift on the city window's
    /// cart button), one for each of its Kontors and markets there; the original allows one per
    /// player and island. Off with the original's settings.
    bool more_carts = true;

    /// Open1602: the hunter's hut reaches 12 tiles instead of the original's 8
    /// (data/balance/wider_hunting.toml), so that it finds enough deer. On by default; the
    /// original's settings keep the original's reach.
    bool wider_hunting = true;

    /// Which towns are discovered from the start (players, natives, pirates) or only by ship.
    Reveal reveal = Reveal::Original;

    /// Open1602: poor ground takes nothing from a field. In the original the ground under a tile
    /// weighs the island's fertility for a crop, so that single tiles stay barren even where the
    /// island grows the crop fully; with this setting every tile has the island's fertility. An
    /// island that grows a crop only partly still carries it on as many tiles as before, one that
    /// does not grow it still does not, and droughts still wither. Off by default and with the
    /// original's settings.
    bool fertile_fields = false;

    /// Open1602: every island of a new endless game grows the three crops of its climate fully
    /// (tobacco, sugar cane and wine in the north, spices, cotton and cocoa in the south), where the
    /// original gives some of them only half. The other climate's crops still do not grow. Off by
    /// default and with the original's settings.
    bool climate_crops = false;
    /// Open1602: every iron vein of a new endless game is a big one. Off by default and with the
    /// original's settings.
    bool big_ore_veins = false;

    /// The cheats: unlocked with the code by default, as in the original.
    Cheats cheats = Cheats::Code;

    /// Bugs of the original brought back; none by default, also in savegames of the original.
    OriginalBugs bugs{};

    /// Victory conditions of an endless game (victory.hpp); all off by default, as the original's
    /// endless game, which no one wins.
    VictoryConditions victory{};

    /// Defaults: corrected behaviour.
    [[nodiscard]] static constexpr Rules open1602() noexcept { return Rules{}; }

    /// Behaviour of the original game where it is switchable.
    [[nodiscard]] static constexpr Rules original() noexcept {
        Rules r;
        r.max_ships_per_player = 33;
        r.max_army = 100;
        r.guarantee_resources = false;
        r.max_game_speed = 4;
        r.demolish_refund = Refund::None;
        r.moving = Moving::Off;
        r.droughts = Droughts::Game;
        r.volcanoes = Volcanoes::Game;
        r.plagues = Outbreaks::Original;
        r.fires = Outbreaks::Original;
        r.event_speed = EventSpeed::Normal;
        r.pirates = Pirates::Game;
        r.pirate_nests = PirateNests::One;
        r.trader_prices = TraderPrices::Original;
        r.trader_reach = TraderReach::Original;
        r.ore_veins = OreVeins::Game;
        r.computer_players = Computers::Game;
        r.revised_bread = false;
        r.computers_buy_ships = false;
        r.more_carts = false;
        r.wider_hunting = false;
        r.reveal = Reveal::Original;
        r.fertile_fields = false;
        r.climate_crops = false;
        r.big_ore_veins = false;
        r.bugs = OriginalBugs::all();
        return r;
    }

    /// Whether large iron veins never run out, given what the game's settings say (bit 3).
    [[nodiscard]] constexpr bool endless_veins(bool game_says_endless) const noexcept {
        return ore_veins == OreVeins::Endless || (ore_veins == OreVeins::Game && game_says_endless);
    }
    /// Whether the computer opponents play, given whether the game's settings switch them off (bit 1).
    [[nodiscard]] constexpr bool computers_play(bool game_says_off) const noexcept {
        return computer_players == Computers::On || (computer_players == Computers::Game && !game_says_off);
    }

    /// Share of the build costs a demolition gives back, in percent.
    [[nodiscard]] constexpr std::uint32_t refund_share() const noexcept {
        switch (demolish_refund) {
            case Refund::Full:
                return 100;
            case Refund::Partial:
                return refund_percent > 100 ? 100U : refund_percent;
            case Refund::None:
                break;
        }
        return 0;
    }

    friend constexpr bool operator==(const Rules&, const Rules&) = default;
};

}  // namespace o1602::sim
