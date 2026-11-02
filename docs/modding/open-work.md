# Modding: what is done and what is still open

This page compares the requirements of [ADR 0007](../architecture/0007-extensibility-and-modding.md)
with what the engine does today (state of 2026-11-01, after PR #379). It is the list to work from
for milestone M8 ([plan](../00-plan.md#6-milestones)). The table of fixed limits at the end of
ADR 0007 tracks the same items from the code's side.

## Summary

State on 2026-11-01. The details of each item follow under [Open items](#open-items).

### Done

| Item | What a package can do |
|---|---|
| 1. Packages | a manifest with version, dependencies (version ranges) and load order; packages as folders or zip archives; the packages a game uses are kept in its savegame and checked in multiplayer, where the host sends missing ones; the main menu's screen "Mods" |
| 2. Content | change 16 values of the original's buildings and 10 of its figures; add buildings and figures of its own, each copying one of the original, and replace another package's building |
| 3. Goods, classes, levels | add trade goods, change the original's classes and add up to eight in all with their needs and public buildings, change and add unlock levels with messages of their own |
| 4. Graphics and sound | PNG images for buildings, figures, ships and icons; music and sounds as WAV, Ogg Vorbis or FLAC for messages, buildings and figures; Smacker and Bink 2 videos for messages |
| 5. Computer players | they build the chains of a package's goods; their stage orders, public buildings, cheat amounts and when a city moves on are data |
| 6. Lua scripts | a sandboxed Lua 5.4 script per package that hears what happens and reads and changes the game (money, goods, missions, buildings, ships, land units, diplomacy, who wins) |
| 7. Scenarios and campaigns | scenarios with maps, briefings, goals and scripts; campaigns of up to five parts that carry money and a script table on |
| 8. Factions | factions of a package's own that take the base game's slots, and settlers or pirates left wholly to a script |
| 9. Island generator | islands of their own shape and size with slopes, rivers with bends, volcanoes and native villages; kinds of island world as data (`landscapes.toml`); island templates of a package's own, which new own worlds draw with the original's (`islands.toml`, [islands.md](islands.md)) |

### Still open

| Item | What is missing | Size |
|---|---|---|
| 2 | ships of a package's own that shipyards build (soldiers of a package's own are done), and computers that train a package's soldiers | large: shipyard, AI and savegame |
| 4 | videos in a modern format (today only Smacker and Bink 2) | medium: a decoder in `third_party` (e.g. a royalty-free format such as AV1 or VP9 in WebM) |
| 5 | the original's 32 rows of needs (`kNeeds`), its 55 fixed building slots of a city, which building of the original produces what (`producer_slot`), the first colony's crop by the computer's slot (`first_crop`) and the four unit families and ship types the computers build, all still compiled in | large: the AI's core tables |
| 9 | climates of a package's own, with their ground, forest and crops (today north and south) | large: climate tables, the generator, fertility and the start dialog |

### Smaller adjustments

Things found while testing on 2026-11-01 that are not wrong but worth doing. Done on 2026-11-02:
`o1602-tool state` and `o1602-tool islands` read Open1602's own `.o1602` too (with the original's
building table), and islands wider or higher than 255 tiles get native villages, within their first
255 tiles (the village's records hold tile coordinates in bytes).

- Coats of arms and message icons of a package in player colour: an icon of `icons.toml` as a
  template whose pure grey takes on the banner's colour, as flags and soldiers of a package do
  ([flags.md](flags.md#graphics-of-packages-in-player-colour); asked for on 2026-11-02).
- The table of package files above and the table of fixed limits at the end of ADR 0007 must be
  kept up to date with each new file and each limit removed.

### Recommendations

Done on 2026-11-02: the **example package** [`data/mods/example`](../../data/mods/example), which
uses every file of a package once and which the tests load with and without the original's data;
the **first page for modders**, [README.md](README.md); and the **package check**
`o1602-tool check-mod <mods folder>`, which reads the packages as the game does and lists every
problem without starting the game.

- Next item to work on: soldiers and ships of a package's own (item 2), because players see it
  most; then the computers' core tables (item 5), so that computers play a package's content as well
  as the original's.

## What a mod can do today

A mod is a folder `mods/<package>/` or a zip archive `mods/<package>.zip`
([packages.md](packages.md#a-package-as-a-zip-archive)). The folder's or archive's name is the
package's id. Its manifest
`mod.toml` gives a name, a version, the packages it depends on and the ones it loads after; the
packages are read in that order, otherwise in the order of their ids' bytes, the same on every
system ([packages.md](packages.md)).

**Multiplayer.** Before anyone joins, the computers of a multiplayer game compare a fingerprint of
the original data the simulation computes with, `haeuser.cod` and `figuren.cod`
(`assets::content_fingerprint`), and the packages the game uses, one by one with version and hash
([packages.md](packages.md#which-packages-a-game-uses)). A computer that lacks one of them is told
which, and the host offers to send them ([packages.md](packages.md#packages-a-host-sends));
packages it has in addition are not loaded for the game. Factions and balance travel with
the game besides (sections `FACT` and `BALA`), so every computer plays the host's.

| File | Changes | Guide |
|---|---|---|
| `messages.toml` | messages of the status bar: new ones and the original's, text per language, icon, own sound (WAV, Ogg Vorbis, FLAC) or original voice, original video | [messages.md](messages.md) |
| `build_menu.toml` | the build menu: a package's buildings with icon and name, the original's moved or taken out | [build-menu.md](build-menu.md) |
| `factions.toml` | how pirate nests, trader ships and the trader's reach grow with the players | [factions.md](factions.md) |
| `balance.toml` | trader amounts and restocking, castle training and landing range, fire and plague spread | [balance.md](balance.md) |
| `buildings.toml` | values of the original's buildings: production, costs, reach, workers, hit points | [values.md](values.md) |
| `figures.toml` | values of the original's figures: speed, carrying, hit points, damage, range, cannons, price | [values.md](values.md) |
| `own_buildings.toml` | buildings of the package's own, each copying one of the original | [own-buildings.md](own-buildings.md) |
| `own_figures.toml` | figures of the package's own, each copying one of the original, with their own pictures | [own-figures.md](own-figures.md) |
| `goods.toml` | trade goods of the package's own: price, icon, names | [goods.md](goods.md) |
| `classes.toml` | classes of inhabitants: the original's changed, new ones after them, needs of any good | [classes.md](classes.md) |
| `music.toml` | music tracks of the package's own (WAV, Ogg Vorbis, FLAC), normal or battle music | [sounds.md](sounds.md) |
| `sounds.toml` | sounds of the package's own (WAV, Ogg Vorbis, FLAC) for its buildings and figures | [sounds.md](sounds.md#sounds-of-buildings-and-figures) |
| `landscapes.toml` | kinds of island world the start dialog offers for new games | [landscapes.md](landscapes.md) |
| `islands.toml` | island templates that new own worlds draw with the original's | [islands.md](islands.md) |
| `graphics.toml` | PNG images of the package's own, drawn by its buildings at all three zoom levels | [graphics.md](graphics.md) |
| `icons.toml` | PNG icons of the package's own for the build menu, goods and messages | [graphics.md](graphics.md#icons) |
| `figure_images.toml` | the original's figures and ships drawn with a package's images | [graphics.md](graphics.md#figures-and-ships) |
| `levels.toml` | unlock levels: the original's changed, new ones after the 32nd, also of a new class | [levels.md](levels.md) |
| `mod.toml` | the package's name, version, dependencies and load order | [packages.md](packages.md) |
| `scenarios.toml` | scenarios of the package's own: map, names, briefing, stars, money, goals, script | [scenarios.md](scenarios.md) |
| `campaigns.toml` | campaigns of the package's scenarios, with money and a script table carried from part to part | [scenarios.md](scenarios.md#campaigns) |
| `computers.toml` | the order in which computer players build in each stage, with their public buildings | [computer-players.md](computer-players.md#in-which-order) |
| `script.lua` | a Lua script of the package, run in a sandbox every tick | [scripts.md](scripts.md) |

Without a mod file the engine already has banners with tinted flags for any number of players
([flags.md](flags.md)), up to 12 players, worlds and islands beyond the original's sizes
([data-limits.md](data-limits.md)) and its own save format for everything that does not fit the
original's.

The factions and the balance a game started with are saved with it, so a mod installed or removed
later does not change a running game.

## Open items

Each item names the ADR 0007 section it comes from and, where there is one, the finding of the
code review of October 2026 ([PR #109](https://github.com/x4rd4s/open1602/pull/109)).

### 1. Package manifest (ADR 0007 section 2) – done

Done on 2026-10-26 ([packages.md](packages.md)): the manifest `mod.toml` with name, version,
description, dependencies and load order; the hash of a package's files; the packages a game uses
in its savegame (section `PACK`); multiplayer games let in only computers with the game's packages
and load no others for it. Also done: a host sends missing packages to a computer whose player
agrees (kept in `mods-cache`), and the main menu's screen "Mods" turns packages on and off and adds
received ones to the `mods` folder. Packages as zip archives, not only folders, done on
2026-11-01 ([packages.md](packages.md#a-package-as-a-zip-archive)): unpacked into `mods-unpacked`,
with the hash of the same files in a folder.

Dependencies done on 2026-10-31 ([packages.md](packages.md#versions-a-package-needs)), at the
owner's request: `depends.<id>` takes version ranges (`">=1.2, <2"`); turning a package on in the
screen "Mods" turns on what it needs, turning one off names what is not loaded without it; a
package that cannot be loaded says why in the screen (missing, wrong version, turned off, not
loaded itself, circle), not only in the log.
### 2. Content registry with string ids (section 2)

Part a done on 2026-10-26 ([values.md](values.md)): a package changes single values of the
original's buildings (`buildings.toml`, 16 fields: production, costs, reach, workers, hit points)
and figures (`figures.toml`, 10 fields: speed, carrying, work time, hit points, damage, range, shot
time, cannons, price, material), addressed as `["anno1602:bakery"]` by name or `["anno1602:<number>"]` with the `.cod` number;
a game keeps them (sections `BLDV`, `FIGV`).

Part b done on 2026-10-26 ([own-buildings.md](own-buildings.md)): a package adds buildings of its
own in `own_buildings.toml`, each copying a building of the original (`like`) with its own sprites
and footprint; they are named `<package>:<name>` in `buildings.toml`, in `build_menu.toml` and in
the savegame (section `OWNB`), and the engine gives them the highest free `haeuser.cod` Ids in the
packages' load order. Pictures, figures of a package's own and a package that replaces another's
building (`["<package>:<name>"]` in `own_buildings.toml`, 2026-11-01) are done.

Figures of a package's own done on 2026-11-01 ([own-figures.md](own-figures.md)): `own_figures.toml`
declares figures that copy one of the original (`like`), with an image of a package (`image`) and
the highest free figure numbers from 1023 down; `figures.toml` changes their values, a building of a
package names them as its carriers or workers (`figure`), and a game keeps them (section `OWNF`).
Soldiers of a package's own done on 2026-11-02 ([own-figures.md](own-figures.md#soldiers)): a figure
whose `like` names a unit is a unit of four colours; castles train it beside the unit it copies, a
ship's hold carries it, and a game keeps it (`FLEE` version 6). Still open: ships of a package's own
for the shipyards, and computers that train a package's soldiers.

### 3. Free goods list, resident classes and unlock levels (section 2; MD-2, MD-3, MD-8)

PR #137 moved the ware groups, unlock levels and the panels' required buildings into one place
(`assets/game_data.hpp`), and the building table is refused when it names more resident classes or
unlock levels than the rules know. These tables are still compiled in: a package cannot add a ware,
a sixth resident class, a new public service or more than 32 unlock levels. Needed: a content
object (`GameContent`) built from the packages, part of the fingerprint and the save.

Part 1 done on 2026-10-26 ([goods.md](goods.md)): a package adds trade goods in `goods.toml` (price,
the icon of a good of the original, names by language) with ware numbers from 59 in the packages'
load order; its own buildings make and use them (`ware`, `raw`, `raw2`), Kontors, ships, carts and
the free trader handle them, and a game keeps them (section `GOOD`).

Part 2 done on 2026-10-26 ([classes.md](classes.md)): `classes.toml` changes the original's classes
(values, needs of any good) and adds up to eight classes in all; a house of a package belongs to a
class (`class`), and a game keeps its classes (section `CLAS`).

Part 3 done on 2026-10-26 ([levels.md](levels.md)): `levels.toml` changes the original's unlock
levels and adds levels after the 32nd, up to 255, also of a class a package added; a building of a
package needs one (`level`), and a game keeps its levels and what its players reached (sections
`LEVL`, `UNLK`). A good shows an icon of a package's own (`picture`, [goods.md](goods.md)). Public
buildings of a class's own done on 2026-11-01 ([classes.md](classes.md#public-buildings)):
`public = "market, chapel | church"` in `classes.toml`, kept in the save (section `CPUB`). The
goods window's page "Population" has a row (inhabitants, satisfaction, tax) and a chart colour of
its own for each of up to eight classes (2026-11-01). A level names the message it gives when it
is reached (`message` in `levels.toml`, section `LMSG`, 2026-11-01).

### 4. Graphics and sound of a package's own (section 2)

Import of PNG atlases with a description file for buildings, figures and icons, and of OGG, WAV and
FLAC for music and sounds. This is also what the mods' messages need for icons, sounds and videos
of their own (today they use the original's).

Part 1 done on 2026-10-27 ([graphics.md](graphics.md)): `graphics.toml` names PNG images, grids of
sprites that get numbers from 40000 in the packages' load order; a building of a package draws one
(`gfx = "<package>:<name>"`) at all three zoom levels, the smaller ones given or halved from the
larger.

Part 2 done on 2026-10-27 ([sounds.md](sounds.md)): the game reads Ogg Vorbis and FLAC besides WAV
(stb_vorbis and dr_flac in `third_party`); `music.toml` adds a package's tracks to the music, as
normal or battle music, and a message's sound may be any of the three.

Part 3 done on 2026-10-27 ([graphics.md](graphics.md#icons)): `icons.toml` names PNG icons with
sprite numbers from 100000, which every sheet of the interface draws; the build menu, the goods
(`picture`, kept in the savegame section `GPIC`) and the messages name them.

Part 4 done on 2026-10-27 ([graphics.md](graphics.md#figures-and-ships)): `figure_images.toml`
draws the original's figures and ships with a package's images (the image is checked to hold every
sprite the figure draws). Part 5 done on 2026-11-01 ([sounds.md](sounds.md#sounds-of-buildings-and-figures)):
`sounds.toml` numbers a package's sounds from 1000, and its buildings (`build_sound`) and figures
(`sound`, `shot_sound`, `hit_sound`) name them. Part 6 done on 2026-11-01
([messages.md](messages.md#sound-and-video)): a message of a package plays a Smacker or Bink 2
video of the package's own (`video = "<path>"`). Still open: videos in a modern format.

### 5. Computer players from data (section 3; MD-4)

PR #141 (MD-4, part 1) named the Kontor levels, the tower records and the Kontor's size in the AI.
Still compiled in: the 32 rows of needs (`ai_planner.cpp`, `kNeeds`), the 55 fixed building slots
of a city (`ai_state.hpp`), the stage orders, the public buildings it builds (`kPublicTasks`), which
building produces what (`producer_slot`), the first colony's crop by `player % 4` (`ai_core.cpp`),
the four unit families and ship types, and the amounts of its cheats. So computers never build a
package's production chain. Needed: needs and producers derived from the building table, slots as
a table keyed by building family, stage orders and cheat amounts as data with today's values as
defaults. Depends on items 2 and 3.

Part 1 done on 2026-10-31 ([computer-players.md](computer-players.md)): a good of a package that a
class needs gets its producer from the building table, its raw material its own, down to a farm or
a good of the original; a city wants as many as its inhabitants and the producers that use the good
need, at most eight, after the list of its stage. They count in slots after the original's 55
(savegame `AIST` version 5).

Part 2 done on 2026-10-31 ([computer-players.md](computer-players.md#what-they-get-from-nowhere)):
the cheat amounts are values of `balance.toml`'s table `[computers]` (savegame `BALA` version 2).

Part 3 done on 2026-10-31 ([computer-players.md](computer-players.md#in-which-order)): the stage
orders, with the public buildings, are lists of `computers.toml` (savegame `AIOR`); an entry may
name a package's good.

Part 4 done on 2026-11-01 ([computer-players.md](computer-players.md#when-a-city-moves-on)): when a
city moves on to its next stage is `balance.toml`'s table `[computer_stages]` (savegame `BALA`
version 3). Still open: the original's Needs (`kNeeds`), the 55 slots, `producer_slot`, the first
colony's crop and the unit families and ship types as data.

### 6. Lua scripts (section 6)

`engine/script` exists only as a README. Needed: Lua 5.4 with a deterministic, sandboxed interface
(no files, no clock, random numbers only from the game's generator), triggered by events, changing
the game only through commands, with its state in the savegame. Scripts also post the mods'
messages.

Part 1 done on 2026-10-27 ([scripts.md](scripts.md)): Lua 5.4.8 in `third_party/lua` with a fixed
string hash seed, and `script::Sandbox` in `engine/script`: no io, os, package and debug libraries,
no loading of files or binary chunks, no addresses in texts, no finalizers and weak tables,
`math.random` from the game's generator, and limits on memory and on the instructions of each call.

Part 2 done on 2026-10-27 ([scripts.md](scripts.md)): a package's `script.lua` runs in a sandbox of
its own, called by the simulation at the end of every tick (`sim::ScriptHost`) with the tick's
events (`on_start`, `on_second`, buildings placed, houses changed, players won and defeated); it
reads the players (`game.player`) and posts the package's messages (`game.message`); its table
`state` and its random generator are saved with the game (section `SCRP`) and are part of the
game's fingerprint.

Part 3 done on 2026-10-31 ([scripts.md](scripts.md#what-a-script-changes)): a script gives and
takes money (`game.add_money`) and goods of a city (`game.add_goods`, read with `game.cities` and
`game.goods`), lets a player win (`game.win`) and puts one out (`game.defeat`). These changes are
no commands: the simulation makes them at the same point of the tick on every computer
(`Simulation::script_money` and the others).

Part 4 done on 2026-10-31 ([scripts.md](scripts.md#the-script)), the owner's choice of what comes
next: scripts hear what happens to cities (shortage, hunger, levels, population), in disasters,
volcanoes and droughts, to ships (launched, traded, discovered, attacked, sunk, sold), in fights and in
diplomacy (`on_city`, `on_disaster`, `on_volcano`, `on_drought`, `on_ship`, `on_military`,
`on_diplomacy`); they read a city's inhabitants (`game.city`), the ships (`game.ships`) and the
treaties (`game.relation`), and make peace and war and trade treaties (`game.set_peace`,
`game.set_trade`).

Part 5 done on 2026-10-31 ([scripts.md](scripts.md#what-a-script-changes)): scripts build and
demolish for a player as the player would (`game.build`, `game.demolish`, paid and checked like the
build tool), destroy a building to ruins (`game.destroy`) and send ships (`game.move_ship`).

Part 6 done on 2026-10-31 ([scripts.md](scripts.md#what-a-script-changes)): scripts read a player's
mission (`game.mission`) and give a player new goals or none (`game.set_mission`), by the keys of a
scenario's goals.

Part 7 done on 2026-10-31: scripts list land units (`game.units`) and send them as their owners
would (`game.move_unit`, `game.attack_unit`). The scripts' part of item 6 is done.

### 7. Scenarios and campaigns (section 6)

Scenarios as map plus start conditions plus goals plus an optional script, campaigns as a sequence
of scenarios with transitions. The original's scenarios and campaigns play with their missions
(the 18 goal bits of `AUFTRAG4`), but there is no format of
Open1602's own for a package's scenario: goals beyond the original's bits, start conditions for
more players, transitions with carried-over values, texts and videos. Depends on item 6 for
scripted goals.

Part 1 done on 2026-10-31 ([scenarios.md](scenarios.md)): a package's `scenarios.toml` names maps
(the original's `.szs`/`.szm` or Open1602's `.o1602`) with names and briefings per language, stars,
money at the start, goals that become the humans' mission and a script of the scenario's own. The
main menu lists them under a heading of their own; a saved game remembers its scenario (section
`SCEN`) and runs its script again.

Part 2 done on 2026-10-31 ([scenarios.md](scenarios.md#campaigns)): `campaigns.toml` puts a
package's scenarios in a row of up to five parts, each opened by winning the one before. The money
and the table `campaign` of the part's script go on to the next part.

Part 3 done on 2026-10-31 with item 8: `slot.<n>` gives the map's slots to factions of packages.

### 8. Factions of a package's own (section 3)

The base game's four factions are data (`factions.toml` changes their numbers). A new faction needs
slots in a new game and, to act differently from the original's four, a behaviour from a script.
Depends on items 2 and 6.

Done on 2026-10-31 ([factions.md](factions.md#factions-of-a-packages-own)): `factions.toml` adds
factions of the package that act as one of the base game's four; `replaces` gives them its slots in
new games, `slot.<n>` of a package's scenario the slots of its map; scripts read
`game.player(n).faction`. A behaviour of its own done on 2026-11-01
([factions.md](factions.md#a-behaviour-of-its-own)): `scripted = true` leaves a faction of settlers
or pirates to the scripts; the game no longer steers it.

### 9. Island generator, stage 2 (section 5)

Stage 1 builds worlds from the original's island templates.
Stage 2, islands of their own shapes from height and moisture maps with the rules (climate, size,
shape, frequencies) as data. Climates and templates are already in one registry (MD-7, PR #142),
which stage 2 builds on.

Part 1 done on 2026-10-31:
`sim::shapes::make_island` draws an island of its own shape in the size of the template the plan
placed, with its coast from the templates' tiles (beach, its corners and surf by the rules read from
all 114 templates), forest of the climate and as many ore mountains as the template. A world's
setting "island shapes" (`WorldSettings::shapes`, start dialog, `--island-shapes`) chooses the
original's templates (default), islands of their own shape, or both mixed; islands with a native
village or a volcano keep their template (volcanoes no longer since part 5).

Part 2 done on 2026-10-31: with
"Own" island shapes the plan draws every island's size itself (`WorldSettings::Landscape`): the
share of the map that is land (5–45 %), the sides of the smallest and the largest island (16–200 and
40–1000 tiles, four times the original's largest; an island over 255 tiles is kept in Open1602's own
chunks `O1602ISL` and `O1602HAUS`, which the original cannot read, and the map grows on its own where
the largest island finds no room), how the sizes are spread (evenly, many small ones, a few giants), the water between the
islands, the coast (smooth, natural, rugged), the outline (compact, long, branched), forest and ore
mountains. The sea around an island of its own shape has the templates' depths: light water past
the surf, middle, then the dark open sea from about five tiles out, with the band edges turned to the
lighter side. The sizes are drawn first and placed from the largest; as many islands as the villages
and volcanoes need become templates (since part 5 only the villages). The start dialog has a page of its own for it, "Island world",
with presets (Original, Varied, Archipelago, Continents, Few giants, Even) and a preview of the world
the game gets (`sim::preview_world`, "Reroll" draws another world number).

Part 3 done on 2026-10-31: the landscape on the command line of the game and of `o1602-tool
new-world`, with the same names as the setup files (`--island-preset varied|archipelago|continents|
giants|even`, `--island-land`, `--island-min`, `--island-max`, `--island-spread`, `--island-gap`,
`--island-coast`, `--island-outline`, `--island-forest`, `--island-mountains`; `sim::set_landscape`,
`sim::kLandscapePresets`).

Part 4 done on 2026-11-01, from
`o1602-tool template-stats` over all 662 templates: the ore mountains of islands of their own shape
lie in a ring of slopes turned to them, as every mountain of the templates (the mines stand on the
slopes), the ground mixes the templates' fifteen most common kinds of ground tile by tile, and two
of three mountains send a river from a spring on their slopes straight out to a river mouth on a
beach that faces its way.

Part 5 done on 2026-11-01: volcanoes on islands of their own shape. A volcano is the big ore mountain
21108; the big mountains of islands of their own shape are now
always 21109, and a volcano is laid only where it is asked for (`ShapeRequest::volcano`), first, in
its ring of slopes and without a river. With "Own" island shapes the plan gives the volcanoes of the
setting to islands of their own shape (not a home, at least 24 tiles on the shorter side;
`WorldPlan::OwnIsland::volcano`), so only villages still need a template; with "Mixed" a template
island with a volcano gets its own shape with the volcano in it.

Part 6 done on 2026-11-01: native villages on islands of their own shape. With "Own" island shapes
no island keeps a template any more: the plan gives the villages (four per 18 islands) to islands
of their own shape that are no home and 40 to 255 tiles on their sides
(`WorldPlan::OwnIsland::village`), and the generator moves the village part of a native template of
the island's climate (NordNat, SuedNat, in a random order of their own) onto inland that is only
ground and forest, with a tile of room around it. The island's ground stays under the village as
under a template's; the village's tiles follow the island's in its INSELHAUS, and its records
(production, growth, houses, huts, store) move with it. An island where no village finds room after
twelve templates stays without, with a warning.

Part 7 done on 2026-11-01 ([landscapes.md](landscapes.md)): the rules of a world of islands of their
own shape as data, a package's `landscapes.toml` names kinds of island world (land, sizes and their
spread, gaps, coast, outline, forest, mountains) that the start dialog offers after Open1602's
presets.

Part 8 done on 2026-11-01: rivers with bends. `o1602-tool template-stats` over the templates (its maps
now show straight river tiles and bends by their rotation) gives 3.6 bends and 8.6 straight river
tiles to a template. A bend (20904, 20905, kind 17) is a block of 2 x 2 tiles that joins its sides r
and r + 1 at rotation r (north 0, east 1, south 2, west 3), the river coming in and going out by the
two tiles on its diagonal; the templates have a bend right after a spring, and at least one straight
tile between two bends. Rivers of islands of their own shape now plan their way from the spring with
a bend at one of three steps (none right after another), never touching themselves, up to ten times
over, and are laid only when the way ends on a beach that faces it.

Part 9 done on 2026-11-01: the native villages of islands of their own shape find room now. The layer
of a native template holds the beach, surf, forest and ground around the village, often a whole
island; only its roads, ruins, squares and buildings are moved now (checked on the original's data:
the box of the village is 5 to 19 tiles on its longer side, that of the whole layer up to 70), the
free places are found with sums over the free tiles instead of 300 random tries, and 24 templates
are tried. `o1602-tool new-world` makes room for the largest island of its own size
(`fit_landscape`), prints the islands with a volcano, and, before the world is made, refuses more
than four players for a .gam (seven slots less the natives and pirates); an output ending in `.o1602`
takes any number of players.

Part 10 done on 2026-11-02, after players' feedback on large worlds: islands of their own size get
veins by their size, one for every 2000 tiles of their rectangle (at least the original's two; the
slots beyond the island record's two are kept in `O1602ISL`, so such a world has no .gam), the
veins of a world grow with its land as well as with its islands and players, a vein goes to an
island by the slots it has left, and all its ore mountains count, not only the first 40 from the
north. Islands of 48 tiles and more on their shorter side get a lake inland for every 6000 tiles,
with a small mountain beside it whose river runs into it; other rivers try the way to a lake nearby
first. The open sea of an island of its own shape is no longer turned at random and takes the
phase of the world's sea around it, so that no strip of other waves shows between two islands.

Still open: the climates of the islands as data.

## Order of work

Agreed on 2026-10-26:

1. Manifest (item 1), because ids, versions and load order are what every later item refers to.
2. Content registry and content object (items 2 and 3), in three steps: single fields of the
   original's buildings and figures changed by a package, then buildings of a package's own with
   savegames storing ids, then goods, resident classes and unlock levels as data.
3. Graphics and sound import (item 4), right after a package can add buildings, which need it to
   be seen.
4. Lua (item 6), then scenarios and campaigns (item 7) and factions of a package's own (item 8).
5. Computer players from data (item 5), later by the owner's decision of 2026-10-25.
6. Island generator stage 2 (item 9), last, as the [plan](../00-plan.md#7-order-of-work) says.
