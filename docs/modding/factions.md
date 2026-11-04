# Factions

Besides the players who found cities, a game of Anno 1602 has the free trader, the pirates and the
natives. In Open1602 these are **factions as data**
([ADR 0007](../architecture/0007-extensibility-and-modding.md), section 3): who steers a player slot
(a person, the computer or nobody) is apart from the faction the slot belongs to, and the numbers a
faction grows with when more people play are values a mod can change.

**Status:** Done are the base game's four factions as data in every world, the numbers below, saving
them with the game, the file `factions.toml` that changes them and, since 2026-10-31, factions of a
package's own: they act as one of the base game's four, take its slots in new games or the slots a
package's scenario gives them, and scripts see them, and since 2026-11-01 a behaviour of their
own: with `scripted` the game no longer steers a faction, its package's script does
([below](#a-behaviour-of-its-own)).

## The base game's factions

| Id | Behaviour |
|---|---|
| `anno1602:settlers` | the people and computer players: found cities, trade, make treaties |
| `anno1602:trader` | the free trader: sails from Kontor to Kontor, buys and sells |
| `anno1602:pirates` | at war with everyone but the trader, hunt ships from their nests |
| `anno1602:natives` | villages on the islands, trade with the settlers |

A game keeps the factions it started with: they are saved with it (section `FACT` of
[o1602-save.md](../formats/o1602-save.md), written only when they differ from the base game's), so a
mod installed or removed later does not change a running game, and every computer of a multiplayer
game plays with the host's factions.

## The numbers

Each number grows with what the game has: `base`, and one more for each `per` of what is counted
(begun), held between `min` and `max`; below `from` it is 0. `per = 0` keeps it at `base`.

| Field | Counted | Default | Meaning |
|---|---|---|---|
| `nests` (pirates) | human and computer players | `per = 4`, `min = 1` | how many nests the pirates aim for, each with the original's ships; only with the setting "Pirate nests: one for every four players" (the original has one) |
| `ships` (free trader) | Kontors in the world | `per = 3`, `from = 2` | how many trader ships sail (the original's one for every three Kontors, none with fewer than two) |
| `reach_players` (free trader) | – | `4` | with more human and computer players than this the trader keeps to as many Kontors as a game of this many would have (setting "Trader's reach"); 0 never |

A field a faction's behaviour does not use (the settlers' `nests`) does nothing.

## Where a mod's changes are

```
<user data folder>/mods/<package>/factions.toml
```

The user data folder and the package's name are as for [messages](messages.md#where-a-mods-messages-are).
Packages are read in their [load order](packages.md#load-order); a later one can change what an earlier one set. The
changes apply to the games started afterwards: new games and scenarios or savegames of the original.
Problems are written to the log (standard error) with the file and the line.

## The file

`factions.toml` is the same part of [TOML](https://toml.io) as `messages.toml`. Each table names a
faction by its id and changes single fields; dotted keys reach the parts of a number:

```toml
# Large games: a pirate nest for every three players, at most five, and more traders.
["anno1602:pirates"]
nests.per = 3
nests.max = 5

["anno1602:trader"]
ships.per = 2
reach_players = 6
```

Fields: `nests.base`, `nests.per`, `nests.from`, `nests.min`, `nests.max`, the same for `ships`, and
`reach_players`, each a number from 0 to 65535. An unknown field or a wrong value is passed over with
a warning and the rest is used; a table for another package's faction that does not exist is passed
over; a file that is no valid data file is passed over as a whole.

## Factions of a package's own

A table whose name is a new faction of the package (`[corsairs]` in the package `seas` is
`seas:corsairs`) adds that faction. It acts as one of the base game's factions, which `behaviour`
names, and starts with that faction's numbers:

```toml
# Corsairs instead of the pirates: more nests in large games.
[corsairs]
behaviour = "pirates"
replaces = "anno1602:pirates"
nests.per = 2
```

| Field | Value |
|---|---|
| `behaviour` | `"settlers"`, `"trader"`, `"pirates"` or `"natives"`: what the simulation does for the faction's players. Required for a new faction; it does not change later |
| `name.<language>` | the faction's name as the editor shows it, by language code (`name.de = "Liga"`, `name.en = "League"`); without one the editor shows the name of its table (`league`) |
| `replaces` | a faction of the same behaviour whose slots the new one takes in the games started afterwards (new games and the original's scenarios and savegames); the first package that replaces a faction gets its slots |
| the numbers | as above |

A package's scenario gives its slots to factions with `slot.<n>` ([scenarios.md](scenarios.md)).
The faction of a slot goes into the game with it (section `FACT`). Scripts read it as
`game.player(n).faction` and act for the faction's players with their functions
([scripts.md](scripts.md)): money and goods, victory and defeat, messages. The pirates', trader's
and natives' behaviours look for the first slot of their behaviour, so a second slot of the same
behaviour beside it does little; to change them, a faction replaces theirs.

## A behaviour of its own

`scripted = true` gives a faction of settlers or of pirates a behaviour of its own: the game no
longer steers its players, and scripts do ([scripts.md](scripts.md)). How it stands to the others
stays that of its `behaviour`: scripted pirates are at war with everyone but the trader, scripted
settlers make treaties.

| Behaviour | What the game no longer does |
|---|---|
| settlers | the computer player: it does not found, build, trade or fight for the faction's slots |
| pirates | no ships come for them, their ships do not sail, hunt or plunder on their own, and their nests make no offers |

The faction's units and ships still defend themselves and fight what they meet, as every unit
does. The script finds its players by `game.player(n).faction` and acts for them each second
(`on_second`) with `game.build`, `game.move_ship`, `game.move_unit`, `game.attack_unit`,
`game.set_peace` and the rest of [what a script changes](scripts.md#what-a-script-changes). The
cities and ships it starts with come from the map, for example a package's scenario that gives
the faction its slot (`slot.<n>`, [scenarios.md](scenarios.md)).

```toml
# mods/seas/factions.toml
[corsairs]
behaviour = "pirates"
replaces = "anno1602:pirates"
scripted = true
```

```lua
-- mods/seas/script.lua: the corsairs' ships sail to the middle of the map.
function on_second(seconds)
  if seconds % 30 ~= 0 then return end
  for _, n in ipairs(game.players()) do
    if game.player(n).faction == "seas:corsairs" then
      for _, ship in ipairs(game.ships(n)) do
        game.move_ship(ship.id, 60, 40)
      end
    end
  end
end
```

The free trader and the natives are always steered by the game; `scripted` is passed over for
them with a warning. A game keeps the field with its factions (section `FACT`).
