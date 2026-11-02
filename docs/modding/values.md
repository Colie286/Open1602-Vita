# Values of buildings and figures

A content package can change single values of the original's buildings (`haeuser.cod`) and
figures (`figuren.cod`: carriers, soldiers, ships) without shipping changed `.cod` files
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 2, item 2 of
[open-work.md](open-work.md)).

**Status:** Done are the fields below, the files `buildings.toml` and `figures.toml` and saving
the values with the game, and [names](#names) for the original's buildings and figures. A table of
`buildings.toml` may also name a building a package added
([own-buildings.md](own-buildings.md)), a table of `figures.toml` a figure a package added
([own-figures.md](own-figures.md)).

## The files

```
<user data folder>/mods/<package>/buildings.toml
<user data folder>/mods/<package>/figures.toml
```

Each table is named after an entry of the original, `["anno1602:<name>"]` with one of the
[names](#names) below, or `["anno1602:<number>"]`: for a building its `HAUS` Id from
`haeuser.cod` (`20503` is the bakery), for a figure its `FIGUR` number from `figuren.cod`. The
number may be decimal or `0x` hex. A field that is not given keeps its value.

```toml
# buildings.toml: a bakery that makes twice the bread and reaches further.
["anno1602:bakery"]
produce = 2
radius = 10
```

```toml
# figures.toml: tougher swordsmen, bigger ships.
["anno1602:swordsman"]
health = 30

["anno1602:small_merchant_ship"]
cannons = 10
```

Packages are read in their [load order](packages.md#load-order); a later one can change what an
earlier one set. A value outside its limits or that is no whole number is passed over with a
warning, as are unknown fields and tables of another package or of a number the data does not
have. A file that is no valid data file as a whole changes nothing. Problems go to the log with
the file and the line.

The buildings' tables may also stand in `balance.toml`, as they did before `buildings.toml`
existed ([balance.md](balance.md#the-buildings-values)).

## Names

The buildings are those of the build menu, named after `Text.cod` in its order, and a few of the
natives and pirates. A unit's name stands for its figures in the four player colours; to change one
colour only, write its number. Names are lower case.

| Build menu | Names and Ids |
|---|---|
| military (`MILITAR`) | `palisade` 22521, `wooden_gate` 21404, `wooden_watchtower` 21405, `wall` 22501, `gate` 20304, `watchtower` 20305, `city_gate` 20303, `castle` 20311, `large_castle` 20313, `fortress` 20315 |
| roads (`STRASSE`) | `dirt_road` 20411, `paved_road` 20401, `wooden_bridge` 21601, `stone_bridge` 21603, `square_1` 20421, `square_2` 20422, `square_3` 20423, `ornamental_tree` 20424 |
| mines (`BERGWERK`) | `quarry` 22407, `ore_mine` 22401, `deep_ore_mine` 22403, `gold_mine` 22405 |
| workshops (`HANDWERK`) | `weaving_hut` 20507, `butcher` 20511, `grain_mill` 20501, `bakery` 20503, `tobacco_products` 20525, `rum_distillery` 20519, `tailor` 20505, `weaving_mill` 20531, `stonemason` 20513, `water_mill` 20509, `ore_smelter` 20521, `toolmaker` 20515, `goldsmith` 20533, `swordsmith` 20517, `musket_maker` 20529, `cannon_foundry` 20527 |
| farms (`FARM`) | `hunters_hut` 22003, `forester` 22001, `forest` 21303, `grain_farm` 21503, `grain_field` 21301, `sheep_farm` 21515, `cattle_farm` 21513, `tobacco_plantation` 21507, `tobacco_field` 21335, `spice_plantation` 21501, `spice_field` 21341, `winery` 21517, `vines` 21343, `sugarcane_plantation` 21511, `sugarcane_field` 21339, `cotton_plantation` 21505, `cotton_field` 21331, `cocoa_plantation` 21509, `cocoa_field` 21337 |
| harbour (`HAFEN`) | `warehouse_1` 22101, `warehouse_2` 22103, `warehouse_3` 22105, `warehouse_4` 22107, `fishers_hut` 21075, `pier` 21073, `small_shipyard` 22115, `large_shipyard` 22111 |
| public (`DIVERS`) | `house` 20605, `marketplace` 20809, `chapel` 20803, `church` 20805, `cathedral` 20801, `tavern` 20807, `doctor` 20817, `school` 20821, `college` 20819, `bathhouse` 20813, `theatre` 20815, `fire_station` 20811, `palace` 20831, `gallows` 20841, `monument` 20851, `triumphal_arch` 20853 |
| others (`LANDSCHAFT`) | `chieftains_hut` 22231, `aztec_pyramid` 22211, `pirate_warehouse` 22221, `pirate_house_1` 22222, `pirate_house_2` 22223, `pirate_tower` 22241, `mixed_forest` 21304, `palm_forest` 22802 |

| Name | Numbers | `figuren.cod` |
|---|---|---|
| `swordsman` | 1–4 | `SOLDAT1..4` |
| `cavalry` | 5–8 | `KAVALERIE1..4` |
| `musketeer` | 9–12 | `MUSKETIER1..4` |
| `cannoneer` | 13–16 | `KANONIER1..4` |
| `small_merchant_ship` | 21 | `HANDEL1` |
| `large_merchant_ship` | 23 | `HANDEL2` |
| `small_warship` | 25 | `KRIEG1` |
| `large_warship` | 27 | `KRIEG2` |
| `free_trader` | 29 | `HANDLER` |
| `pirate_ship` | 31 | `PIRAT` |
| `spearman` | 33–36 | `SPEER1..4` |
| `harvester` | 74 | `MAEHER` |
| `picker` | 75 | `PFLUECKER` |
| `picker_2` | 76 | `PFLUECKER2` |
| `woodcutter` | 77 | `HOLZFAELLER` |
| `stonecutter` | 78 | `STEINKLOPFER` |
| `hunter` | 79 | `JAEGER` |
| `carrier` | 80 | `TRAEGER` |
| `carrier_2` | 81 | `TRAEGER2` |
| `cart` | 82 | `KARREN` |
| `butcher` | 83 | `FLEISCHER` |
| `doctor` | 84 | `ARZT` |
| `firefighter` | 85 | `LOESCH` |
| `pack_donkey` | 86 | `PACKESEL` |
| `fisherman` | 90 | `FISCHER` |

Every other entry keeps only its number. The names are in
`engine/assets/src/original_names.cpp`.

## The buildings' fields

| Field | `haeuser.cod` | Limits | Meaning |
|---|---|---|---|
| `produce` | `Prodmenge` | 1–64 | tons made in one cycle |
| `raw` | `Rohmenge` | 1–64 | tons of the first raw material one cycle uses |
| `raw2` | `Workmenge` | 1–64 | tons of the second raw material one cycle uses |
| `interval` | `Interval` | 1–3600 | length of a cycle, in the original's units |
| `max_stock` | `Maxlager` | 1–255 | tons the building stores at most |
| `running_cost` | `Kosten` (first) | 0–1000 | gold per minute while it works |
| `idle_cost` | `Kosten` (second) | 0–1000 | gold per minute while it is idle |
| `cost_money` | `Money` | 0–100000 | gold to build it |
| `cost_tools` | `Werkzeug` | 0–255 | tons of tools to build it |
| `cost_wood` | `Holz` | 0–255 | tons of wood to build it |
| `cost_bricks` | `Ziegel` | 0–255 | tons of bricks to build it |
| `cost_cannons` | `Kanon` | 0–255 | tons of cannons to build it |
| `radius` | `Radius` | 1–32 | tiles its carriers and workers reach |
| `workers` | `Arbeiter` | 0–1000 | workers in the city's statistics |
| `max_idle` | `Maxnorohst` | 1–1000 | cycles without raw material until it reports a shortage |
| `health` | `Maxenergy` | 0–10000 | hit points against attacks; 0: the first hit destroys it |

## The figures' fields

`figuren.cod` gives soldiers and ships the same fields; each value below applies to whichever the
figure is.

| Field | `figuren.cod` | Limits | Meaning |
|---|---|---|---|
| `speed` | `Speed` | 1–10000 | walking or sailing speed, as the number in `figuren.cod` |
| `max_carry` | `Maxtrag` | 1–64 | tons a carrier or cart takes at once |
| `work_time` | `Worktime` | 1–600000 | milliseconds of one work step (harvesting a field, felling a tree) |
| `health` | `Maxenergy` | 1–10000 | hit points of a soldier or ship |
| `hit` | `Hitpoint` | 0–1000 | damage of one blow or shot |
| `range` | `Shotradius` | 1–32 | tiles a shot reaches |
| `shot_time` | `Shottime` | 1–60000 | milliseconds between two shots of a ship; for a soldier from the shot to the hit |
| `cannons` | `Maxkanon` | 0–100 | cannons a ship carries at most |
| `price` | `Preis` | 0–100000 | what a ship is worth (sales, computer players) |
| `material` | `Rohmenge` | 0–255 | tons of material a shipyard builds the ship from; of weapons a castle needs for the unit |
| `holds` | `Maxware` | 1–8 | cargo holds of 50 t a ship has; ships already at sea keep theirs, or get more |

## In the game

The values apply to the games started afterwards: new games and scenarios or savegames of the
original. A game keeps the values it started with (sections `BLDV` and `FIGV` of
[o1602-save.md](../formats/o1602-save.md)), so a package installed or removed later does not change
it, and every computer of a multiplayer game plays with the host's. Each game starts from the
original's tables and sets its own values on them; the next game starts from the original's again.

## In the code

`engine/sim/include/o1602/sim/balance.hpp`: `Balance::buildings` with `kBuildingFields` and
`Balance::figures` with `kFigureFields`; `apply_buildings_file` and `apply_figures_file` read the
files, `apply_building_values` and `apply_figure_values` write a game's values into its tables.
