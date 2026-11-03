# Open1602 savegame (`.o1602`)

Open1602 saves games in its own format ([ADR 0009](../architecture/0009-own-save-format.md)).
It contains the complete state of a game, depends on no other file and knows none of the
limits of the original (players, ships, soldiers, towers, map size). A saved game continues
after loading **exactly as if it had never been paused**; data tests check this with a
real savegame (lava in the air, a fire with the fire brigade) and with an endless game full of
computer opponents.

In Open1602: `engine/sim/include/o1602/sim/save_game.hpp` (`write_game`, `read_game`, `save_info`).
The format shares its sections with the extra chunk of exported `.GAM` files
([o1602-chunk.md](o1602-chunk.md)); they are written and read in `engine/sim/src/save/` (see
[In the code](#in-the-code)).

Open1602 still reads the files of the original (`.SZS`, `.SZM`, `.GAM`). A game can be
exported as `.GAM` as long as it fits within the limits of the original.

## Structure

All numbers little-endian. A **string** is a u32 byte count followed by that many bytes, without a
terminating zero. The bytes are **Windows-1252**, not UTF-8: names of players, cities and ships are
kept as the game's fonts draw them, the same encoding as the original's savegames and texts. Typed
text is converted from UTF-8 when it is entered (`core::utf8_to_cp1252`); a reader that shows a name
elsewhere converts it back (`core::cp1252_to_utf8`).

| Offset | Type | Contents |
|---|---|---|
| 0 | 8 bytes | identifier `OPEN1602` |
| 8 | u16 | format version, currently 1 |
| 10 | u16 | flags: bit 0 = the sections are a zlib stream |
| 12 | u32 | 0 |
| 16 | u64 | size of the sections (unpacked) |
| 24 | u32 | CRC-32 (IEEE, as in zlib and PNG) of the unpacked sections |
| 28 | u32 | size of the data after the header (packed or not) |
| 32 | … | the sections, packed with zlib (level 6) if bit 0 is set |

Without zlib in the build, Open1602 writes the sections unpacked; such a build refuses to load a packed file
with a clear message.

**Section:** u32 identifier (four characters), u16 version, u16 flags, u32 length, data. A reader skips
sections it does not know and versions it cannot read, and reports each in the load's warnings. Later
versions can thus add parts without invalidating older savegames. Flag bit 0 marks a section as
**required**: a reader that cannot read such a section loads nothing (a game without it would run
differently, e.g. desynchronise in multiplayer). All sections are required except `META` and `ORIG`;
see [o1602-chunk.md](o1602-chunk.md) for the rules. A file is refused if a section comes twice, if a
section it reads has bytes left over after its data, or if `WRLD`, `ISLE` or `PLAY` is missing.

| Identifier | Contents |
|---|---|
| `META` | width, height, game time, fingerprint of the building data (FNV-1a over Id, image and size of every building from `haeuser.cod`), name of the first human player: for the lists of savegames (`save_info` unpacks only the head of the file) |
| `MAPW` | the whole map: world size, per island the header (identifier, number, size, position, template, climate), the position on the world map and **both layers of all tiles**. The values are ordered by kind (first the building ids of all tiles row by row, then all sub-tiles, rotation and phase, owner, city, random value); zlib packs that well. Version 2 (written since 2026-10-24, version 1 is still read): number, width and height of the header as u16, and each column of a layer (all building ids, all sub-tiles, ...) as a byte 0 and its values, or as a byte 1, the number of runs (u32) and per run its length (u32) and value, whichever is shorter. The runs of a column must together be exactly as long as the column, and the islands of the section together no larger than the world |
| `RULE` | settings (`sim::Rules`) as pairs (u16 key, u32 value); unknown keys are skipped |
| `WRLD` | game time, city name counter, difficulty and settings word, drought date and seed, random generators, clocks of the houses and of growth, next disaster number |
| `WRL2` | further settings of the world: interval between two pirate ships |
| `TUTR` | one byte, 1: the game is the tutorial (`TIMERS` +0xA5); written only then, optional, so a reader that does not know it loads the game as an ordinary one |
| `ISLE` | per island: deer spots, drought, craters, deposits, fertility (version 2: 16-bit coordinates, version 3: in addition 16-bit island numbers and deposit coordinates, see [o1602-chunk.md](o1602-chunk.md#section-versions)) |
| `PLAY` `CITY` `PROD` `HOUS` `GROW` `COST` `ANIM` `DISA` `VOLC` | players, cities, production list with free slots, houses, growth, shipyards and castles, animals, fires and plagues, volcanoes with lava in flight |
| `FACT` | the world's factions (id, behaviour, the numbers they grow with: pirate nests, trader ships, trader reach; [factions.md](../modding/factions.md)) and the faction of each player (order as in `PLAY`). Written only when the factions are not the base game's, optional: a reader without it plays the base game's factions that the role byte of `PLAY` gives (0 human, 1 computer, 2 free trader, 3 pirates, 4 natives; a faction of a content package goes there as the one it acts as). Version 2 adds after each faction a byte: scripts steer it (`scripted`); it is written only when a faction is, and the section is required then |
| `TILE` | not in the own savegame (`MAPW` holds every tile); only in the chunk of an exported `.GAM` |
| `PSTA` | per player (order as in `PLAY`): mission, status (playing, defeated, absent), won, defeated opponents, the two counters of the defeat rules, trade statistics (purchases, sales); version 2 also the scenario switches of a computer, version 3 also whether the player closed his city gates, version 4 who played it last, version 5 the cheats it unlocked ([o1602-chunk.md](o1602-chunk.md#section-versions)) |
| `CSTA` | per city (island, number): trade statistics of the warehouse (purchases, sales, as in `STADT4`); if the section is missing, the values from `STADT4` apply |
| `MLCK` | per city (island, number): the game time the building material was locked or the people last complained about it (message 0x48 every 18 minutes); if the section is missing, the clock starts at the next city tick |
| `CPOP` | per city (island, number): the last population message (`STADT4` +0x05; 0x11 = 100 inhabitants … 0x1D = 9000, each comes once); if the section is missing, the value from `STADT4` applies |
| `CGUP` | per city (island, number): the ships that gave up their way to its Kontor since a route last stopped there (at four a computer owner complains); written only when a city has some, optional: without it the counts start at 0 |
| `BALA` | the values of the balance a package changed (seven `u32` in the order of `kBalanceFields`: the trader's most held, most bought and restock seconds, a castle's interval and unload range, the chance of fire spreading and the reach of a plague; version 2 adds 19 `u32`, the computers' cheats in the order of `kBalanceFields`, and is written only when one of them is not the original's; version 3 adds 23 `u32`, the computers' stages (`computer_stages`), the same way; [balance.md](../modding/balance.md)). Written only when they are not the original's, required: a reader without it would compute another game; a value outside its limits loads nothing. Does not exist in the original |
| `BLDV` | the values of the original's buildings that a package or a setting changed (`sim::BuildingValue`: u32 building Id, u8 field in the order of `kBuildingFields`, u32 value, sorted by Id and field; [values.md](../modding/values.md)). Written only when there are any, required: a reader without it would compute another game; an unknown field or building or a value outside its limits loads nothing. Does not exist in the original |
| `FIGV` | the values of the original's figures that a package changed (`sim::FigureValue`: u32 figure number, u8 field in the order of `kFigureFields`, u32 value, sorted by number and field; [values.md](../modding/values.md)). Written only when there are any, required like `BLDV`; an unknown field, a number of 1024 or more or a value outside its limits loads nothing. Does not exist in the original |
| `AIOR` | the computers' build orders a package changed (`sim::ComputerOrder`, [computer-players.md](../modding/computer-players.md#in-which-order)): a list of orders, each u8 list (`sim::ComputerList`) and a list of entries, each u8 kind, u8 slot (a public building's or producer's slot of `ai_state.hpp`, a package's ware number), u32 number, u8 flag and u8 condition (`ComputerNeed::When`); sorted by list, each once. Written only when there are any, required: the computers would build otherwise; an unknown list, kind or condition, a wrong slot or a number out of its limits loads nothing. Does not exist in the original |
| `CLIM` | the packages' climates and the islands that have one (`sim::Climate`, [climates.md](../modding/climates.md)): a list of climates, each string id `<package>:<name>`, u8 like (0 north, 1 south), its names as a list of language code and Windows-1252 text, u32 crops (bits of the island header's fertility), its forest as a u32 count of u32 building Ids and u8 share in percent; then a list of islands, each u16 island index and u8 climate (1 for the first of the list). Written only when there are climates, required: their islands' crops would grow otherwise. More than 32 climates, an id twice, a like other than north or south, a crop other than the six of the plantations, a share above 100, more than 16 trees, an island climate of 0 or beyond the list or an island that does not exist loads nothing. Does not exist in the original |
| `OWNF` | the figures the packages added and the numbers this game gave them (string id `<package>:<name>`, u32 `figuren.cod` number; [own-figures.md](../modding/own-figures.md)). Written only when there are any, and required: the buildings' carriers and workers name those numbers, so a game loads only where its packages give the same figures the same numbers. Does not exist in the original |
| `OWNB` | the buildings the packages added and the Ids this game gave them (string id `<package>:<name>`, u32 `haeuser.cod` Id; [own-buildings.md](../modding/own-buildings.md)). Written only when there are any, and required: the islands' tiles name those Ids, so a reader whose packages give other Ids must not load the file. Does not exist in the original |
| `CLAS` | the classes of inhabitants as packages changed and added them (`assets::ResidentClass`: u32 count, then per class its string id, empty for the original's, u8 the original's class it looks like, u32 share (Prozent * 128 / 100), u32 Maxwohn, u32 tax (trunc(Steuer * 16 / 3)), u32 Hiscore, its needs as a u32 count of u8 ware and u32 amount per inhabitant and 10 s in 1/8192 t, and its names as a u32 count of language code and Windows-1252 text; [classes.md](../modding/classes.md)). Written only when a package changed or added a class, and required: houses name the classes and the needs drive the economy; no class, more than eight, a look beyond the original's five, a Maxwohn of 0 or above 255 or a need of food or of no good loads nothing. Does not exist in the original |
| `CPUB` | the public buildings classes of their own need (`assets::ResidentClass::public_needs`: u32 count, then per class its u8 number and a u32 count of u16 choices, each the bits of the buildings any one of which will do, as `House::services` has them and bit 15 for the market; [classes.md](../modding/classes.md#public-buildings)). Written only when `classes.toml` gave a class `public`, and required: it decides which houses rise; a choice of 0 or of unknown bits, a class beyond eight or more than eight choices loads nothing. Does not exist in the original |
| `LEVL` | the unlock levels as packages changed and added them (`assets::InfraLevel`: u32 count, then per level by INFRA number, 0 first, its string id, empty for the original's, u8 class and u32 inhabitants; [levels.md](../modding/levels.md)). Written only when a package changed or added a level, and required: buildings name the levels; no level, more than 256 entries, a class beyond the eighth or more than 10,000,000 inhabitants loads nothing. Does not exist in the original |
| `LMSG` | the messages `levels.toml` gave unlock levels (`assets::InfraLevel::message`: u32 count, then per level its u8 number and the message's string id, `<package>:<name>` of messages.toml or empty for none; [levels.md](../modding/levels.md#the-message-of-a-level)). Written only when a level has one, and optional: without it the levels give the original's messages; a number that is no level of `LEVL` is passed over. Does not exist in the original |
| `UNLK` | the levels 33 to 255 the players reached (`Player::infra_more`: u32 count, then per player u8 its index in `PLAY` and seven u32 words, bit (n - 33) % 32 of word (n - 33) / 32 for level n). Written only when a player reached one, and required: without it the players lose what they may build. Does not exist in the original |
| `GOOD` | the goods the packages added (`assets::OwnGood`: u32 count, then per good its u8 ware number, its string id `<package>:<name>`, u16 base price, u8 the original's good whose icon it shows, and its names as a u32 count of language code and Windows-1252 text; [goods.md](../modding/goods.md)). Written only when there are any, and required: stocks, holds and buildings name those ware numbers; a number below 59 or taken twice, a price of 0 or above 10000 or an icon that is no trade good of the original loads nothing. Does not exist in the original |
| `GPIC` | the icons of packages the packages' goods show (`assets::OwnGood::picture`: u32 count, then per good its u8 ware number and the icon's string id `<package>:<name>`; [goods.md](../modding/goods.md)). Written only when a good shows one, and optional: without it, or where no package has the icon, the goods show the original's icons `GOOD` names; a number that is no good of `GOOD` is passed over. Does not exist in the original |
| `PACK` | the content packages the game uses, in their load order (`assets::ContentPackage`: u32 count, then per package its id and its version as u32 length and UTF-8 bytes, and the u64 hash of its files; [packages.md](../modding/packages.md)). Written whenever the world names its packages, which every game started since 2026-10-26 does, even with none; optional: the packages decide only what a computer loads beside the game. A file without it plays with the packages installed. Does not exist in the original |
| `SCRP` | the state of the packages' scripts (`sim::ScriptState`: u32 count, then per package with a script its id (a scenario's script: the scenario's id) as u32 length and UTF-8 bytes, the state and increment of its random generator as two u64, and its table `state` as u32 length and bytes in the format of `script::Sandbox::save_table`; version 2, written only when a script keeps one, adds its table `campaign` the same way; [scripts.md](../modding/scripts.md)). Written only when the game runs scripts, and optional: without it the scripts start anew. Does not exist in the original |
| `SCEN` | the id of the package's scenario the game was started from (`<package>:<name>`, u32 length and UTF-8 bytes; [scenarios.md](../modding/scenarios.md)), so that its script runs again after loading. Written only for such a game, and optional: without it the game runs no scenario's script. Does not exist in the original |
| `CAMP` | the part of a package's campaign the game is (`sim::World::CampaignPart`: the campaign's id as u32 length and UTF-8 bytes, the u8 part counted from 0, and what the part before left to the script's table `campaign` as u32 length and bytes; [scenarios.md](../modding/scenarios.md#campaigns)). Written only for such a game, and optional: without it a won part opens no next part. Does not exist in the original |
| `SCNI` | the description of a scenario of the editor (`sim::ScenarioInfo`, [ADR 0013](../architecture/0013-editor.md)): u8 stars (0 to 5), u8 flags (bit 0: the scenario keeps to the original's limits; other bits load nothing), its names as a u32 count of language code and UTF-8 text, the author and the version as UTF-8 strings, and its briefings as a u32 count of language code and UTF-8 text. Right after `META`, so that the lists of scenarios read it from the head of the file (`save_info`). Written only for a scenario, and optional: without it the file is an ordinary savegame. A language twice or not a code, a text that is too long, not UTF-8 or has control characters, or more than five stars loads nothing. Does not exist in the original |
| `MARK` | the named markers of a scenario that scripts find by their names (`sim::ScenarioMarker`: u32 count, then per marker its name, u8 shape, 0 a point and 1 an area, and its x, y, width and height in world tiles as u32). Written only when there are any, and optional: without it a script finds no marker. A name that is not 1 to 64 lower case letters, digits and `_`, a name twice, a point that is not one tile, a rectangle outside the world or more than 4096 markers loads nothing. Does not exist in the original |
| `SLUA` | the script of the scenario (UTF-8 Lua source as u32 length and bytes, at most 1 MiB). Written only when there is one, and required: the script decides how the game goes. Does not exist in the original |
| `EDIT` | the state of the editor (u32 length and bytes, at most 4 MiB): what the editor keeps of its own work on the scenario, which the game does not read. Written only when there is any and only in the own savegame, optional. Does not exist in the original |
| `PLAN` | the plan of islands still to be made when the game starts (`sim::PlannedIslands`, [ADR 0013](../architecture/0013-editor.md)): a scenario of the original that makes its islands at the start, opened in the editor. u8 flags (bit 0: a table follows; other bits load nothing), the plan's values as the original's `SZENE` has them (two u32 native villages, u32 crops per island, u32 smallest and largest size class for gold, then 4 ores, 12 crops and 4 treasures, each u32 building Id, u16 count and u32 type), its islands as a u32 count and per island u8 climate, u8 size class, u8 flags, u8 number, u16 template, u32 x and u32 y, then the 512 u16 of its `RANDTAB` if bit 0 is set. Written only for such a world, and required: without it the game would have no islands. More than 50 islands, a number twice, an unknown climate or size class or an island outside the world loads nothing. When the game starts, `roll_planned_islands` makes the islands from the scenario the world came from (`ORIG`) with this plan. Does not exist in the original |
| `UGRP` | the groups the players stored with Ctrl + 1 .. 0 (`sim::UnitGroups`: u32 count, then per player by its index in `PLAY` ten groups, keys 1 .. 9 and 0, each a u32 count and per unit a u8 flag ship (1) or land unit (0), its u32 id and its place in the group's shape as two i16 in 1/8 tile from the centre). Written only when a player stored a group, and optional: without it the groups are empty. Not part of the game's fingerprint: in a multiplayer game every computer knows only its own player's groups. Does not exist in the original, which forgets the groups |
| `GLDG` | per city (island, number) that has goods statistics: the minutes closed, then per good ever booked its ledger (warning given, the running minute's and quarter's counts, the last 60 minutes and the last 48 quarters of an hour as samples of 16 flows, 17 in version 2, and the stock), then whether the city keeps the figures of its window and if so their history (minutes closed, 60 minutes and 48 quarters of 13 figures, the sums of the running quarter; [ADR 0012](../architecture/0012-goods-statistics.md)); written only when a city has statistics, optional: without it the statistics start empty. Does not exist in the original |
| `SCOR` | per player (order as in `PLAY`): soldiers killed and lost, ships sunk and lost (`PLAYER4` +0x14..+0x1A; u16 in version 1, u32 in version 2, which is written only when a counter is above 65 535); then per city (island, number): growth of the house looked at last and the game time since it grows (monument allowances); if the section is missing, the counters from `PLAYER4` apply and the cities start without growth |
| `MISN` | the missions (goals, loss conditions, values, text, the islands of the island goals recorded at the start) and the end of the game (time, winner) |
| `FLEE` `ARMY` `DIPL` `EPOC` `XPLR` | ships with course, route and cargo, shipyard orders, trade settings (`FLEE` version 2: routes with eight stops, version 3: stops with eight wares, version 4: 16-bit island numbers, version 5: a surrender the pirates have dealt with, version 6: the soldier of a package a hold carries, version 7: the ship of a package a shipyard builds, see [o1602-chunk.md](o1602-chunk.md#section-versions)); soldiers, towers, building damage, shots in flight, castle slots; diplomacy; island epochs (`EPOC` version 2: 16-bit island numbers); exploring |
| `QUEU` `STNC` | command chains of the ships and units (Shift+right-click, up to 16 destinations each); attack move and "hold position" (ADR 0010, does not exist in the original) |
| `SIMU` | the state of the running game: clocks of the simulation, all active figures, the state of the computer opponents, the names for new cities and ships, the game speed and the pause. Version 2 holds it in parts with their own versions ([o1602-chunk.md](o1602-chunk.md#simu)) |
| `ORIG` | the file of the original the game comes from (savegame, scenario or the template of a new game), as chunks without the island tiles (`INSELHAUS`), about 12 KB packed. Only for the export as `.GAM`: it takes from it the bytes that Open1602 does not track, exactly as for a game loaded directly from the file. If the section is missing, the export writes defaults like a new game |

The writer emits the sections in this order: `META`, `SCNI` (only for a scenario), `MAPW`, `RULE`, `WRLD`, `WRL2`, `TUTR`, `ISLE`, `PLAY`,
`FACT` (only with factions of their own), `BALA` (only with values of a package), `BLDV` (only with building values), `FIGV` (only with figure values), `AIOR` (only with build orders of a package), `CLIM` (only with climates of a package), `GOOD` (only with goods a package added), `GPIC` (only when such a good shows an icon of a package), `CLAS` (only with classes a package changed or added), `CPUB` (only with a class that names its public buildings), `LEVL` (only with levels a package changed or added), `LMSG` (only when a level names its message), `UNLK` (only when a player reached a level after the 32nd), `OWNB` (only with buildings a package added), `OWNF` (only with figures a package added), `PACK` (only when the world names its packages), `PSTA`, `MISN`, `CITY`, `CSTA`, `MLCK`, `CPOP`, `CGUP` (only when a city has a count), `GLDG` (only when a city has statistics), `SCOR`, `PROD`, `HOUS`, `GROW`, `COST`, `ANIM`, `DISA`, `VOLC`, `FLEE`,
`EPOC`, `ARMY`, `DIPL`, `XPLR`, `QUEU`, `STNC`, `ORIG` (only for a game that came from a file of the
original), `SCRP` (only when the game runs scripts), `SCEN` (only for a package's scenario), `CAMP` (only for a part of a package's campaign), `MARK` (only with markers), `SLUA` (only with a script of the scenario), `EDIT` (only with a state of the editor), `PLAN` (only with islands still to be made), `UGRP` (only when a player stored a group) and `SIMU` (`engine/sim/src/save/sections.cpp`, `encode_sections`). `SHIP` and `SOLD`,
the lists of older versions, are only read. The details of the sections from `RULE` onwards are in
[o1602-chunk.md](o1602-chunk.md).

**Buildings** are stored as Id from `haeuser.cod` with sub-tile, never as image number. If the fingerprint
of the building data does not match, Open1602 loads anyway and reports it; an Id that the data does not know
makes the savegame unreadable (instead of wrong buildings).

## Properties

- **Canonical:** the same state gives the same bytes, no matter where the game came from. Saving,
  loading and saving again yields the file unchanged; two games can be compared byte for byte
  (the tests do that).
- **Robust:** the loader first checks header, size and CRC-32, then reads all sections and checks them
  against the world (islands, tiles within their island, ships in the world, units on known
  islands, random generators). Then `validate(World)` (`validate.hpp`) checks the finished world as a whole,
  as after every load, also of a file of the original: islands on the map, cities, buildings, units,
  craters and deer spots on known islands and within their tiles, owners that exist. If the loader is given the
  figure table (the game does that), it also reads `SIMU` as a trial run
  (`Simulation::state_readable`). Only if everything is correct is the world created; a damaged, truncated
  or newer savegame loads nothing. Every count is checked before memory is reserved: lists against
  the remaining length (they grow with the records actually read), the world size against 4096 tiles
  per side, the player count of `DIPL` (one entry per pair of players) against the player limit.
  Writing goes first to a side file, which is then renamed. A fuzz test
  (`tests/unit/test_save_fuzz.cpp`) changes the sections of a saved game at random (bits, bytes, counts,
  sizes, sections cut, left out, doubled or moved) with a correct checksum: every such file loads
  nothing or a world that validates, saves again and runs.
- **Fixed bytes:** `tests/unit/test_save_format.cpp` writes a world with every saved field set to a
  value of its own, as the own savegame and as the chunk of a `.GAM` (`ISLE` in version 1 and 3,
  `VOLC` in version 1 and 2), and compares every section with its recorded size and CRC-32; reading those bytes and
  writing them again gives the same bytes. A change of a section's bytes needs a new version of it.
- **Small:** Merlin04 (130 minutes, two cities) needs 248 KB instead of 885 KB as a `.GAM` with the extra chunk.

## In the code

`engine/sim/src/save/` holds the sections, a file per group: `map.cpp` (`MAPW`, `TILE`), `world.cpp`
(`RULE`, `WRLD`, `EPOC`), `islands.cpp` (`ISLE`, `XPLR`), `players.cpp` (`PLAY`, `FACT`, `PSTA`, `MISN`,
`SCOR`, `DIPL`), `cities.cpp` (`CITY`, `CSTA`, `MLCK`, `CPOP`, `CGUP`, `GLDG`, `PROD`, `HOUS`, `GROW`, `COST`), `ships.cpp` (`SHIP`,
`FLEE`), `army.cpp` (`SOLD`, `ARMY`), `nature.cpp` (`ANIM`, `DISA`, `VOLC`) and `orders.cpp` (`QUEU`,
`STNC`). Each record is described once, as a function `io_...(a, record)` that writes it with a
`save::Writer` and reads it with a `save::Reader` (`archive.hpp`); the method names give the width in
the file (`a.u16(x)`). What only the reader does (checks, values rebuilt from others) stands in an
`if constexpr (A::kReading)` block in the description. Written by hand on each side are only `RULE`
(keys and values; the reader skips unknown keys and clamps), `META` (the reader takes only the
fingerprint), the old lists `SHIP` (holds that are filled only) and `SOLD` (read only), the raw
bytes of `ORIG` and `SIMU`, and the editor's sections `SCNI`, `MARK`, `SLUA`, `EDIT` and `PLAN` (`sections.cpp`;
what they may hold is checked by `scenario_data.cpp`). `sections.cpp` puts the sections together (order, versions, flags) and
reads them all; `apply.cpp` checks what was read against the world and applies it.
`savegame_extension.cpp` wraps them as the chunk of a `.GAM`, `save_game.cpp` as the own file.

## In the viewer

- **F5** saves to `quicksave.o1602` in Open1602's user folder (Linux:
  `~/.local/share/Open1602/saves/`, Windows: `%APPDATA%\Open1602\saves\`), never into the installation.
- `--save FILE` saves on exit: in the own format, or with the extension `.gam` as a savegame of the
  original (if the game fits).
- `--load FILE` loads an Open1602 savegame (or a file of the original via its path).

## Limits of this version

- A world has at most 4096 tiles per side (a larger one is refused when reading). The unpacked
  sections have at most 20 bytes per tile of the world (`MAPW` needs that much for land) plus
  128 MiB, and never more than 1 GiB; a file without `META` at most 256 MiB. A larger game is not
  written (refused before it is packed) and a file promising more is refused before it is unpacked.
- Islands are addressed by their number: every island needs a number of its own (`validate` refuses
  two with the same number). Numbers above 255 (more than 256 islands) use version 3 of `ISLE`,
  version 2 of `EPOC` and version 4 of `FLEE`; `MAPW` version 2 holds them always. An island can be
  larger than 255 × 255 tiles (up to the world size): its craters and deer spots then use version 2
  of `ISLE` and `VOLC`, its deposits version 3 of `ISLE`. Such a game cannot be exported as `.GAM`,
  whose records keep island numbers and coordinates in one byte; the writer refuses it. The world
  generator still plans at most 250 islands (`kMaxPlannedIslands`): it builds the original's island
  records, whose tiles carry the number in eight bits.
- Commands not yet executed between two ticks are not saved (saving
  happens between the ticks).
- The `.GAM` export of a game loaded from an `.o1602` equals that of a directly loaded one
  byte for byte (test over game00 and game10), except for the six bits of unknown meaning per island tile
  (`INSELHAUS` +4 bits 26–31), which `ORIG` does not carry along. A savegame without `ORIG` (older
  version) exports players and island templates correctly, the rest with defaults; the first export
  without both crashed in the original after ten seconds (all players red, island templates 0).
