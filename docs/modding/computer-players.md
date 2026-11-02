# Computer players

The computer players build the production chains of the packages' goods without a file of their
own: what they build comes from the building table and the classes' needs. In which order each stage
of a city builds is a package's `computers.toml`; what they get from nowhere is a value of
`balance.toml`
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 3, item 5 of
[open-work.md](open-work.md), MD-4).

**Status:** Part 1 done on 2026-10-31: the producers of a package's goods that a class needs, down
to their farms and to the original's raw materials. Part 2 done on 2026-10-31: what they get from
nowhere, as values of `balance.toml`. Part 3 done on 2026-10-31: the stage orders, with the public
buildings, as data (`computers.toml`). Still open: the original's own Needs and how a stage moves
on to the next (they stay as the original has them).

## What the computers build

A good of a package ([goods.md](goods.md)) that a class of inhabitants needs
([classes.md](classes.md)) gets a producer: the first building in the table that makes it and that
the computers can place:

| The producer | Placed as | Its raw material |
|---|---|---|
| a workshop (`HANDWERK`), any footprint | a workshop beside a road near the Kontor or a market | nothing, a good of the original or a good of a package |
| a plantation (`PLANTAGE`) of 2 x 2 tiles | a farm with its fields | a crop of the original (`anno1602:45` to `anno1602:51`): the island must grow it |
| a pasture (`WEIDETIER`) of 2 x 2 tiles | a farm with its grass | grass (`anno1602:52`) |

The raw material of a workshop that is a package's good gets its producer the same way, and so on
down the chain. A workshop that uses a good of the original waits for the original's producer of it
and asks for one when the city has none (the tools of a package's workshop bring a toolsmith). A
chain that comes round to itself (`a` from `b`, `b` from `a`) does not wait for itself: the first of
its producers is built without its raw material.

Mines, fisheries, hunters and farms of another size are not built for a package's good; the good
then has to come by trade. Only the first raw material (`raw`) is followed, not the second (`raw2`).

## How many

A city wants as many producers of a good as its inhabitants use of it, together with what the
producers that use the good take, at full rate: a producer that makes 1 t every 20 s (3 t a minute)
serves 3 t of the inhabitants' needs a minute. At most eight producers of a good per city. The
values come from the building's `Prodmenge`, `Rohmenge` and `Interval` ([values.md](values.md)) and
the class's `need` ([classes.md](classes.md)).

## When

A city asks for these producers after each list of its stage ([ai.md](../re/ai.md)), once the
original's buildings of the list are there or cannot be built: a producer is planned when the city
can pay for it (a farm also for its fields) and has unlocked it. As with the original's buildings, a
site that cannot be found is tried again a few times, then the city waits until land on its island
becomes free or it starts anew.

The computers count the producers of a package's goods in slots after the original's 55 (section
`AIST` of the savegame, version 5); a game of the original has no such slots, and its computers
play as before.

## When a city moves on

A computer's city goes through its stages ([in which order](#in-which-order)) by its inhabitants,
the computer's gold and the free land of its island: a city just founded becomes the pioneers' with
two inhabitants, the settlers' with 22, and so on. These numbers are values of `balance.toml`'s
table `[computer_stages]` ([balance.md](balance.md#the-values)), with the original's as defaults:

```toml
# mods/eager/balance.toml: computers that hurry on to the merchants.
[computer_stages]
merchants_citizens = 200
merchants_rich_money = 15000
```

A game keeps them (savegame `BALA` version 3).

## In which order

Each stage of a city ([ai.md](../re/ai.md), task 5) goes through its lists of what it needs, in
order, and stops at the first entry that finds something to do. A package's `computers.toml` gives
a list anew (`order`) or adds entries after it (`append`); a list it does not name stays the
original's, and a later package changes what an earlier one gave:

```toml
# mods/pepper/computers.toml: the settlers build their pepper mills early, and no chapel
[settlers]
order = "market 0, woodcutter 2, pepper:pepper 2, sheep 2, weaving_hut 1, fisher 2, houses 18, quarry 1, stonemason 1, houses 24"

# the aristocrats also want a theatre without 8000 gold
[aristocrats]
append = "theatre 1"
```

The lists and when a stage goes through them:

| List | Stage |
|---|---|
| `founded` | a city just founded |
| `colony`, then `colony_materials` | a colony (between them its farms by the island's crops) |
| `pioneers`, `settlers` | the home city's first two stages |
| `second`, `second_luxury`, `second_farming` | a second city: first, then with luxury crops, else farming |
| `main`, then `main_rich` | the main city (`main_rich` holds only with 4000 gold, its entries say so) |
| `main2`, `citizens`, `citizens2`, `citizens_small` | the main city on to the citizens; `citizens_small` with little land |
| `merchants`, `merchants_luxury`, `merchants_tail` | the merchants; `merchants_luxury` once their luxury goods are in stock |
| `merchants2`, `merchants2_tail` | the second stage of merchants |
| `merchants_small`, `merchants_small_tail` | merchants with little land, who stay there |
| `aristocrats`, `aristocrats2` | the aristocrats |

An order is a string of entries separated by commas; an entry is a name, its number and its
conditions, separated by spaces:

| Entry | Builds |
|---|---|
| `woodcutter n`, `hunter`, `grain_farm`, `mill`, `bakery`, `quarry`, `stonemason`, `cotton`, `weaving_hut`, `weaving_mill`, `wine`, `spices`, `cocoa`, `cattle`, `butcher`, `tobacco`, `tobacco_goods`, `iron_mine`, `smelter`, `toolsmith`, `tailor`, `sheep`, `sugar`, `rum`, `fisher`, `swords`, `muskets`, `cannons`, `gold_mine`, `goldsmith` | producers until the city has `n` (the original's Needs: a weaving hut becomes a weaving mill and sheep cotton where cotton grows fully) |
| `chapel n`, `tavern`, `school`, `doctor`, `gallows`, `fire_brigade`, `church`, `bath_house`, `college`, `theatre` | public buildings until the city has `n`; with `covered` also where houses lack its service (the chapel, the gallows and the fire brigade always look) |
| `<package>:<good> n` | producers of a package's good until the city has `n`, if the computers build any ([below](#what-the-computers-build): a class needs it or a producer uses it) |
| `market n` | markets until the land reaches its houses (`n`: the original's parameter, 0 or 1) |
| `houses n` | houses until the city has `n` |
| `kontor n` | the Kontor up to level `n` (1 to 4) |
| `tower n` | towers until the city has `n` |
| `luxury n` | `n` fields of luxury crops; with `one_crop` of one crop only |
| `colony_luxury n` | a colony's luxury fields: one per `n` tiles of its land |
| `colony n` | a new colony (`n`: the original's mode, 1 to 4) |
| `castle`, `monument`, `shipyard` | one of them |
| `processing`, `iron_tools`, `weapons` | the workshops of the raw materials the city has; iron and tools; weapons |
| `roads_if_mason` | stone roads once the city has a stonemason |

Numbers go up to 1000, an order has at most 64 entries. A condition on the money of the computer:
`gold_4000`, `gold_5000` or `gold_8000` after an entry tries it only with as much gold, `never` not
at all; `covered` and `one_crop` take the same endings (`covered_4000`: also where covered, with
4000 gold). An entry that cannot be read is passed over with a warning that names the file and the
line, the rest of the order is kept.

A game keeps the orders that differ from the original's (section `AIOR` of the savegame), so it
plays with them whatever the packages installed later say.

**Large islands.** The numbers of an order were made for the original's islands, where no city
has 2000 tiles of free land. Once a stage's orders have nothing more to do, a city past the settlers
(the lists from `second` and `main` on) with more than 2000 tiles of free land on its island goes
through them again with the numbers of its `houses`, producers, public buildings, `luxury`, `tower`
and package entries multiplied by its land / 1000, up to eight times, rounded up; houses beyond the
order's own number only while at most an eighth of the city's houses hold pioneers, so that the city
moves up before it spreads. A colony does the same with its caps on luxury fields and farms. So a
computer fills an island of 3000 free tiles with up to three times as many houses and the farms and
workshops to feed them, while maps of the original's islands play as before. The original's computer
players (`Rules::bugs.classic_ai`, `--ai classic`) keep the plain numbers.

## What they get from nowhere

The original gives its computer players money and goods on every difficulty: steady money while
they are poor, money and bricks for a new city, and money, wood, tools, food and bricks while they are
behind the others. The amounts are values of the table `[computers]` of a package's
[`balance.toml`](balance.md#the-values), the original's by default:

```toml
# mods/fair/balance.toml: computers without gifts
[computers]
steady_money = 0
founding_money = 0
founding_bricks = 0
subsidy_poor_money = 0
subsidy_money = 0
subsidy_wood = 0
subsidy_tools = 0
colony_wood = 0
colony_tools = 0
colony_food = 0
colony_bricks = 0
```

A game keeps them with its other balance values (section `BALA` version 2 of the savegame).

## In the code

`engine/sim/include/o1602/sim/computer_orders.hpp`: the orders (`ComputerNeed`, `ComputerList`,
`default_order`, `apply_computers_file`), kept in `Balance::computer_orders`; `Engine::run_stage`
and `Engine::first_need` in `ai_planner.cpp` go through them.

`engine/sim/src/ai/ai_chains.hpp`: `package_needs` (the producers from the building table),
`package_wanted` (how many a city wants); `Engine::package_chains` in `ai_planner.cpp` plans them,
the survey counts them (`Engine::package_slot`). The cheats: `Balance::ai_*`
(`engine/sim/include/o1602/sim/balance.hpp`), read by `Engine::cheats()` through `cheats_of`
(`ai_tables.hpp`).
