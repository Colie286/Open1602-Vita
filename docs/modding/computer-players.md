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
buildings, as data (`computers.toml`). Part 6 done on 2026-11-03: the original's producers
(`[producer.<name>]`): which building each builds, how often it tries, the crop and the producer it
needs first, how a stage goes on without it and which buildings count as it.

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

A city asks for these producers after each list of its stage, once the
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

The same table holds the rest of a stage's numbers: when a city just founded becomes a colony or is
given up, how many farms and fields a colony lays out, when the settlers start on workshops and
luxury, and the luxury goods in stock and Kontor levels the later stages wait for. A game keeps them
(savegame `BALA` version 3, version 4 for those from `colony_buildings` on).

## In which order

Each stage of a city (task 5) goes through its lists of what it needs, in
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

**The first colony.** One more list is no stage: `first_crops`, the crop the island of a computer's
first colony has to grow. The computer of player `p` takes entry `p` modulo their number; without
entries it takes any island. Its entries are `crop_ore` (an island with ore), `crop_tobacco`,
`crop_spices`, `crop_sugar`, `crop_cotton`, `crop_wine` and `crop_cocoa`, without numbers or
conditions; the original's list is `crop_ore, crop_spices, crop_tobacco, crop_wine`. A crop in
another list or something else in this one is passed over with a warning.

```toml
# mods/sweet/computers.toml: every computer founds its first colony on sugar or cocoa
[first_crops]
order = "crop_sugar, crop_cocoa"
```

**Large islands.** The numbers of an order were made for the original's islands, where no city
has 2000 tiles of free land. Once a stage's orders have nothing more to do, a city past the settlers
(the lists from `second` and `main` on) with more than 2000 tiles of free land on its island goes
through them again with the numbers of its `houses`, producers, public buildings, `luxury`, `tower`
and package entries multiplied by its land / 1000, up to eight times, rounded up; houses beyond the
order's own number only while at most an eighth of the city's houses hold pioneers, so that the city
moves up before it spreads. A colony does the same with its caps on luxury fields and farms. So a
computer fills an island of 3000 free tiles with up to three times as many houses and the farms and
workshops to feed them. On the original's large islands a city at the aristocrats' last stage that
earns at least 50 gold a minute more than it pays grows the same way from 1000 tiles of free land,
once there is no next stage its growing could hold up. The original's computer players (`Rules::bugs.classic_ai`, `--ai classic`) keep the plain numbers.

## The producers

Each producer an order names (`woodcutter` to `goldsmith`) is one of the original's Needs: a
building, how often a stage tries to place it, the crop its island must grow, the producer of its
raw material the city must have first and how the stage goes on when it is not built. A table
`[producer.<name>]` of `computers.toml` changes these; a field it leaves out stays the original's,
and a later package changes the fields an earlier one gave:

```toml
# mods/bread/computers.toml: the computers bake in a package's bakery, three tries a stage, and
# count the original's bakery as theirs too
[producer.bakery]
building = "bread:stone_oven"
attempts = 3
after = "mill"
counts = "anno1602:bakery"
```

| Field | Value | Meaning |
|---|---|---|
| `building` | `anno1602:<name or number>` or `<package>:<name>` ([own-buildings.md](own-buildings.md)) | the building it builds, placed as the producer's own is (a fisher's at the coast, a mine's on its deposit); without it the original's (for most producers the first building that makes its good from its raw material) |
| `attempts` | 1 to 255 | how often a stage tries to place it before it goes on (the original: 2, quarry, stonemason and gold mine 1) |
| `fertile` | `grain`, `tobacco`, `spices`, `sugar`, `cotton`, `wine`, `cocoa` or `""` | the crop the city's island must grow; `""` none |
| `after` | another producer, or `""` | the producer the city must have first: the original's workshops wait for their farms (`weaving_hut` for `sheep`, `butcher` for `cattle`, `bakery` for `mill`, the iron workshops for `smelter`, ...); `""` none |
| `wait` | `none`, `money`, `unlock` or `placed` | how the stage goes on without it: `none` goes on to the next entry when the producer is locked or too dear; `money` waits while the money lacks; `unlock` also while it is locked; `placed` also goes on when it found no place (the original's types A to D) |
| `counts` | a list of up to 16 buildings | buildings the city counts as this producer besides its own, such as a package's second bakery |

A `building` should be one the stage that orders it can build: a building that is still locked
there is never built, and a city whose next stage waits for that producer stays where it is (a
weaving hut replaced by the tailor, locked for settlers, keeps the computers at the settlers).

The survey counts a production building as the producer whose `building` or `counts` names it,
before it looks at its good and raw material. A field that cannot be read or an unknown producer is
passed over with a warning that names the file and the line.

The rules of single producers are fields of their slots (below). A game keeps the changed
producers (section `AIPR` of the savegame).

## The slots

A computer's city keeps what it has in slots: one for each producer an order names, for each public
building, for its Kontor, market, houses, towers and castle, and the counters `food` and `alcohol`
that its stages look at. A package's `slots.toml` changes the rules of a slot or removes it; a table
names the slot by its name in the orders, a field it leaves out stays as it was:

```toml
# mods/rome/slots.toml: no weaving mill, olive oil counts as food, a church needs 20 houses
[weaving_mill]
remove = true

[food]
counts = "rome:oil"

[church]
min_houses = 20

# two stonemasons for each quarry at most, three with the original's rule
[stonemason]
process_max = 3
```

`remove = true` removes a producer, a tower or a public building: the computers neither build nor
count it, the orders that name it go on to their next entry, and the rules of other slots that name
it are dropped (a producer that waited for it waits for none). A removed slot keeps its number, so
the orders can still name it; a later table cannot change it.

| Field | Slots | Meaning |
|---|---|---|
| `counts` | `food`, `alcohol` | a list of up to 16 goods (`<package>:<name>` or `anno1602:<name>`) whose producers count as the city's food or alcohol producers besides the original's |
| `min_houses` | public buildings | how many houses a site must reach that lack its service (the original: chapel 3, fire brigade 6, tavern, school, doctor and bath house 8, college, theatre and gallows 10, church 16) |
| `roads_on_fail` | public buildings | `true`: the computer lays roads where it found no site |
| `instead`, `instead_crop`, `instead_unlocked` | producers | build producer `instead` where the crop `instead_crop` (grain, tobacco, spices, sugar, cotton, wine, cocoa) grows fully, once the building of `instead_unlocked` is unlocked (without it: at once); the original: `cotton` for `sheep`, `weaving_mill` for `weaving_hut`; `instead = ""` none |
| `single_below_stock`, `single_below_users` | producers | at most one unless the city has as many tons of its raw material and as many producers that wait for it (`after`); the original's smelter: 30 and 3; 0 no limit |
| `raw_stock` | producers | built only while the city has more tons of its raw material (the original's goldsmith: 1); 0 no test |
| `process_from`, `process_per`, `process_round_up`, `process_rank` | producers | the order `processing` builds one for every `process_per` producers of `process_from`, rounded down or up, in the order of their ranks (1 first; 0 not in it); the original: mill, bakery, weaving hut, weaving mill, butcher, tobacco goods, rum, stonemason |
| `process_max` | producers | at most so many for each producer of `process_from` (the original's stonemason: 2); 0 no limit |
| `subsidy_ware`, `subsidy_tons`, `subsidy_above` | producers | a city of 50 citizens and above gets `subsidy_tons` of the good while it has less than 3 t and more than `subsidy_above` of these producers (the original: a ton of spices, tobacco, alcohol, sugar, wool and cocoa above 3 farms, 4 t of ore above none); `subsidy_ware = ""` none |
| `subsidy_class`, `subsidy_people` | producers | the subsidy only with as many inhabitants of this class and above (`anno1602:pioneers` to `anno1602:aristocrats`; the original's cocoa: 100 merchants); 0 no test |

### Public buildings of a kind of their own

A table with a name of its own and `kind = "public"` adds a public building the computers plan: a
building of a package with a service of its own (`service = true` in
[own-buildings.md](own-buildings.md)), at most two in a game. The orders name it by the table's name
(`court 1 covered`) once the slots are read; packages read their `slots.toml` before any
`computers.toml`.

```toml
# mods/rome/slots.toml: the computers build courts where 8 houses lack one
[court]
kind = "public"
building = "rome:court"
min_houses = 8          # the default
roads_on_fail = true    # the default

# mods/rome/computers.toml
[citizens]
append = "court 1 covered"
```

A new house that lacks its service asks for it again, as for the chapel, school, tavern and doctor.

What stays in the code: the mines look for their deposits, and the Kontor, market, houses, shipyard,
towers and castle are counted by their kind of building. The producers of packages' goods
([goods.md](goods.md)) are derived from the building table and count in slots after the original's.
A field that cannot be read or an unknown slot is passed over with a warning that names the file and
the line. A game keeps the changed slots (section `SLOT` of the savegame).

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
and `Engine::first_need` in `ai_planner.cpp` go through them. The producers' changes
(`ComputerProducer`) are kept in `Balance::computer_producers`; `need_def` and `counted_slot`
(`ai_tables.hpp`) lay them over the Needs and the survey of the slots.

`engine/sim/include/o1602/sim/computer_slots.hpp`: the slots (`ComputerSlot`,
`original_computer_slots`, `apply_slots_file`), kept in `Balance::computer_slots` (empty: the
original's) and read by the computers through `Engine::slots()`.

`engine/sim/src/ai/ai_chains.hpp`: `package_needs` (the producers from the building table),
`package_wanted` (how many a city wants); `Engine::package_chains` in `ai_planner.cpp` plans them,
the survey counts them (`Engine::package_slot`). The cheats: `Balance::ai_*`
(`engine/sim/include/o1602/sim/balance.hpp`), read by `Engine::cheats()` through `cheats_of`
(`ai_tables.hpp`).
