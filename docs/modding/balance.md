# Balance

Some numbers of the game's balance are values a content package can change instead of constants in
the code ([ADR 0007](../architecture/0007-extensibility-and-modding.md): a mod changes data, not
code). Their defaults are the original's.

**Status:** Done are the values below, the values of the original's buildings ([values.md](values.md)),
saving them with the game and the file `balance.toml` that changes them. More values follow as they
are needed; the needs of the inhabitants and the prices of the wares are still only in the original's
`.cod` files.

## The values

| Table | Field | Default | Limits | Meaning |
|---|---|---|---|---|
| `trader` | `max_held` | 50 | 1–200 | tons of a ware the free trader holds at most |
| `trader` | `max_buy` | 10 | 1–200 | tons of a ware he buys at most in one visit |
| `trader` | `restock_seconds` | 60 | 1–3600 | seconds until he takes on tools or iron ore again when he has few |
| `castle` | `interval_seconds` | 10 | 1–600 | seconds of a castle's training step (one more when it has nothing to do) |
| `castle` | `unload_range` | 6 | 1–16 | tiles from a ship within which soldiers land |
| `disasters` | `fire_spread` | 19 | 0–128 | chance of 128 per epoch that a fire spreads to a building nearby; 0 never |
| `disasters` | `plague_radius` | 4 | 0–16 | tiles a plague reaches to infect a neighbouring house; 0 never |
| `computers` | `steady_below`, `steady_money` | 3791, 379 | 0–1000000, 0–100000 | a computer with less money than the first gets the second at every refresh of its city (every 5.1 s) |
| `computers` | `founding_below`, `founding_money` | 500, 799 | 0–1000000, 0–100000 | a computer that founds a city with less money than the first gets the second |
| `computers` | `founding_bricks` | 8 | 0–100 | tons of bricks a computer's new city gets |
| `computers` | `subsidy_poor_below`, `subsidy_poor_money` | 4733, 797 | 0–1000000, 0–100000 | a computer behind the others with less money than the first gets the second |
| `computers` | `subsidy_below`, `subsidy_money` | 9376, 1793 | 0–1000000, 0–100000 | the same from the settlers on, when it has more than the poor one's limit |
| `computers` | `subsidy_wood`, `subsidy_tools` | 7, 3 | 0–100 | tons of wood and tools a computer behind the others gets while its city has less |
| `computers` | `colony_wood_below`, `colony_wood` | 19, 13 | 0–100 | a young city of a computer behind the others with less wood than the first (tons) gets the second |
| `computers` | `colony_tools_below`, `colony_tools` | 15, 9 | 0–100 | the same with tools, when it has the wood |
| `computers` | `colony_food_below`, `colony_food` | 19, 9 | 0–100 | the same with food, before the computer's second inhabited city |
| `computers` | `colony_bricks_below`, `colony_bricks` | 19, 9 | 0–100 | the same with bricks, from its second inhabited city on |
| `computer_stages` | `pioneers_inhabitants` | 2 | 0–1000000 | a city just founded moves on to the pioneers with this many inhabitants |
| `computer_stages` | `colony_below_land` | 600 | 0–1000000 | ... or becomes a colony when its island has less free land than this (tiles) |
| `computer_stages` | `settlers_inhabitants` | 22 | 0–1000000 | the pioneers move on to the settlers |
| `computer_stages` | `grown_houses`, `grown_settlers`, `grown_seconds` | 24, 95, 3000 | 0–100000, 0–1000000, 0–1000000 | the settlers have grown with this many houses and settlers and above, or after this many seconds in the stage |
| `computer_stages` | `grown_money` | 8001 | 0–10000000 | grown, the city moves on with this much gold (or good incomes), to a second city or the main city |
| `computer_stages` | `main2_settlers`, `main2_money`, `main2_rich_money` | 170, 8001, 15001 | 0–1000000, 0–10000000 | the main city goes on with settlers and above and gold, or with the third value of gold alone |
| `computer_stages` | `citizens_money` | 8001 | 0–10000000 | on to the citizens |
| `computer_stages` | `citizens2_citizens`, `citizens2_money`, `citizens2_rich_money` | 250, 6001, 20001 | as above | the citizens' second stage: citizens and above and gold, or gold alone |
| `computer_stages` | `merchants_citizens`, `merchants_money`, `merchants_rich_money` | 340, 6001, 25001 | as above | on to the merchants the same way |
| `computer_stages` | `merchants_land` | 951 | 0–1000000 | with less free land than this the city stays small instead |
| `computer_stages` | `merchants2_merchants` | 400 | 0–1000000 | the merchants' second stage, with merchants and above (or a Kontor of level 4) |
| `computer_stages` | `aristocrats_merchants`, `aristocrats_money` | 500, 15001 | 0–1000000, 0–10000000 | on to the aristocrats with merchants and above and gold |
| `computer_stages` | `aristocrats_land` | 1101 | 0–1000000 | with less free land than this the city stays small instead |
| `computer_stages` | `aristocrats2_money` | 8001 | 0–10000000 | the aristocrats' second stage, with their luxury goods in stock |
| `computer_stages` | `colony_buildings` | 7 | 0–100000 | a city just founded becomes a colony with more buildings than this ... |
| `computer_stages` | `colony_other_people`, `colony_large_city` | 4, 1200 | 0–1000000 | ... or when the computer has more than one other city of the first number of inhabitants, or one of the second |
| `computer_stages` | `founded_abandon_seconds`, `pioneers_abandon_seconds` | 480, 1200 | 1–1000000 | a city just founded, and the pioneers' city, is given up after so long in its stage |
| `computer_stages` | `colony_animal_tiles`, `colony_animals` | 120, 6 | 1–1000000, 0–1000 | a colony on an island without crops: a sheep farm or cattle farm for every so many tiles of free land, at most the second number |
| `computer_stages` | `colony_luxury_tiles` | 120 | 1–1000000 | a colony on an island with crops: a luxury field for every so many tiles |
| `computer_stages` | `grown_income` | 11 | 0–100000 | grown, the settlers also move on when every city of the computer earns at least so much gold a minute |
| `computer_stages` | `settlers_luxury_settlers`, `settlers_luxury_fields` | 135, 4 | 0–1000000, 0–1000 | settlers that have not grown yet build workshops and up to the second number of luxury fields once they are so many |
| `computer_stages` | `main_idle_money` | 4000 | 0–10000000 | the main city with nothing to do grows on with so much gold |
| `computer_stages` | `citizens_luxury`, `citizens_kontor` | 1, 2 | 0–100, 0–4 | on to the citizens also only with more luxury goods in stock than the first number or a Kontor above the second level |
| `computer_stages` | `merchants2_kontor` | 4 | 1–5 | the merchants' second stage also with a Kontor of this level (5: never) |
| `computer_stages` | `aristocrats2_luxury` | 3 | 0–100 | the aristocrats' second stage with more of their luxury goods in stock than this |

The `computers` values are the original's cheats: what the computer players get from nowhere, on
every difficulty ([computer-players.md](computer-players.md#what-they-get-from-nowhere)). Values of
0 take a gift away. The `computer_stages` values say when a computer's city moves on to its next
stage ([computer-players.md](computer-players.md#when-a-city-moves-on)); inhabitants are counted
of the class named and the classes above it, gold is what the computer has.

A game keeps the values it started with: they are saved with it (section `BALA` of
[o1602-save.md](../formats/o1602-save.md), written only when they differ from the original's; version
2 holds the `computers` values and is written only when one of them differs, version 3 the
`computer_stages` values the same way, version 4 those from `colony_buildings` on), so a
mod installed or removed later does not change a running game, and every computer of a multiplayer
game plays with the host's.

## The buildings' values

A table named after a building of the original, `["anno1602:<name>"]` or `["anno1602:<Id>"]` with
the building's `HAUS` Id from `haeuser.cod` ([names](values.md#names)), changes single values of that building: production, running and build costs,
reach, hit points. These tables belong in `buildings.toml` now; `balance.toml` still takes them, as
it did before. The fields and their limits are in [values.md](values.md), with those of the
figures.

```toml
# A cheaper bakery that makes twice the bread.
["anno1602:bakery"]
produce = 2
running_cost = 10
```

### Revised bread production

The game setting "Bread chain" on the page "Balance changes" (`Rules::revised_bread`, [ADR 0008](../architecture/0008-game-settings.md))
uses the same values, from `data/balance/revised_bread.toml`, compiled into the game: the grain farm
makes 2 t of grain per ripe field, the bakery 2 t of bread from 2 t of flour for 15 gold a minute
instead of 5. Three farms with 21 fields, two mills and a bakery then make about 6.3 t of bread a
minute instead of 1.5 t, for the same 6.3 gold per ton as two cattle farms and a butcher; meat and
fish stay as they are (`tests/data/test_food_chains.cpp` measures it). A package's values for the
same buildings go before the setting's. The setting is on by default; the original's settings
switch it off.

The other settings of that page work the same way: "Hunter's hut reach" (`Rules::wider_hunting`,
`data/balance/wider_hunting.toml`: 12 tiles instead of 8) and "Holds of large ships"
(`Rules::large_ship_holds`, `data/balance/large_ship_holds.toml`, the figures' field `holds`: the
large merchant ship 8 holds and the large warship 6, the original's the other way round).

## Where a mod's changes are

```
<user data folder>/mods/<package>/balance.toml
```

The user data folder and the package's name are as for [messages](messages.md#where-a-mods-messages-are).
Packages are read in their [load order](packages.md#load-order); a later one can change what an earlier one set. The
changes apply to the games started afterwards: new games and scenarios or savegames of the original.
Problems are written to the log (standard error) with the file and the line.

## The file

`balance.toml` is the same part of [TOML](https://toml.io) as `messages.toml`. Each table changes
single fields; a field that is not given keeps its value:

```toml
# A harder game: the trader buys less, fires spread more easily.
[trader]
max_buy = 5

[disasters]
fire_spread = 40
```

A value outside its limits or that is no whole number is passed over with a warning, as are unknown
tables and fields. A file that is no valid data file as a whole changes nothing.

## In the code

`engine/sim/include/o1602/sim/balance.hpp`: `sim::Balance` with the values, `kBalanceFields` with
their names and limits, `apply_balance_file`; `World::balance()` is what the simulation reads.
The buildings' values are `Balance::buildings` with `kBuildingFields`; `apply_building_values` writes
them into the game's building table after restoring the original's, and `rule_building_values`
gives those of the settings (`data/balance/*.toml`).
