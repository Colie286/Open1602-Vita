# Open1602's own chunk (`O1602`)

Since 2026-10-18 Open1602 saves in its own format ([o1602-save.md](o1602-save.md),
[ADR 0009](../architecture/0009-own-save-format.md)). The chunk `O1602` remains for games that are
**exported** as `.GAM` for the original, and for older Open1602 savegames; its sections
from `RULE` onwards are the same as in the own format.

Exported savegames are `.GAM` files of the original
with an additional chunk `O1602` after `WIFF`. The original does not know the name and skips
it; if it saves the game again, the chunk is dropped. Open1602 reads it after the chunks of the original and
uses it to replace whatever it contains.

In Open1602: `engine/sim/include/o1602/sim/savegame_extension.hpp`; the sections are written and read
in `engine/sim/src/save/` ([o1602-save.md](o1602-save.md#in-the-code)).

## What it contains and why

It is **authoritative** for everything it contains. Lists are stored in full and in their order,
not just what the original lacks. That way, continuing the game does not depend on how the original
rounds values, combines bits or orders lists:

- game settings (`sim::Rules`) of the match;
- game time to the millisecond, clocks of the houses and of growth, drought date and seed, the
  random generators of the simulation, the next disaster number;
- per island: deer spots with exact time, phase of the island epochs, drought, craters, ore deposits,
  fertility;
- players, cities (stock of all goods, reservations, deliveries, supply with 64-bit counters and
  history, pride, upgrade queue, event date), the production list with its free slots,
  houses, growth entries, shipyards and castles, ships with exact cargo, soldiers with exact
  position, animals;
- fires and plagues (S-001), running volcanic eruptions and lava in flight (S-062);
- the clocks of the simulation and all figures: carriers, carts, farmers, herds, fishermen, deer, hunters,
  doctors, fire brigade, robbers, with path, cargo and reservations, plus the logistics statistics;
- diplomacy, exploring, ships with course and route, soldiers, towers and castles;
- the state of the computer opponents (task stack, plans per city, search, reservations, settlement,
  ship missions, attacks), which the original does not save at all.

**Tiles** are only included where loading the chunks of the original would give something different:
ripe grass and fishing grounds outside the cities, which the original does not save. For this the
writer checks what the written `INSELHAUS` chunks give when loaded, and compares both layers
of every tile.

Result (data test): a game that is saved and loaded continues **exactly as if it had never
been paused**. Even with lava in the air, a fire and the fire brigade on its way, five
minutes later it is byte for byte the same savegame as without interruption.

## Structure

All numbers little-endian. A **string** is a u32 byte count followed by that many bytes, without a
terminating zero. The bytes are **Windows-1252** (not UTF-8), as in the original's chunks: names of
players, cities and ships are kept as the game's fonts draw them.

| Field | Type | Contents |
|---|---|---|
| Identifier | 8 bytes | `O1602SAV` |
| Version | u16 | 1 |
| Flags | u16 | 0 |
| Fingerprint | u64 | FNV-1a (64 bit) over all bytes of the file before the chunk (i.e. chunks of the original and `WIFF`) |
| Sections | … | up to the end of the chunk |

**Section:** u32 identifier (four characters), u16 version, u16 flags, u32 length, data. A reader
skips sections it does not know and versions it cannot read and reports them; for that part, what the
chunks of the original say then applies. Later versions can thus add parts without invalidating older
savegames.

In the chunk the writer emits `RULE` (when the settings are known), `WRLD`, `ISLE`, `TILE`, `PLAY`,
`PSTA`, `MISN`, `CITY`, `CSTA`, `MLCK`, `CPOP`, `CGUP` (only when a city has a count), `GLDG` (only when a city has statistics), `SCOR`, `PROD`, `HOUS`, `GROW`, `COST`, `SHIP`, `ANIM`, `DISA`, `VOLC`,
`FLEE`, `EPOC`, `ARMY`, `DIPL`, `XPLR`, `QUEU`, `STNC` and `SIMU`, in this order. `META`, `MAPW`,
`WRL2` and `ORIG` exist only in the own savegame ([o1602-save.md](o1602-save.md)).

Flag bit 0 (**required**): the section holds state the game cannot do without. A reader that cannot
read it (unknown identifier or later version) does not apply the chunk at all, and the game loads from
the original's chunks alone, with a warning. Open1602 sets it on every section except `META`, `ORIG`
and `SHIP` (the copy for readers older than `FLEE`), so a reader older than a required section (for
example `QUEU` or `STNC`) refuses the chunk. Files written before the flags existed have 0
there; every section of theirs is known. A section that changes in a way an older reader cannot follow
gets the next version number (not a new identifier); the old version can be written next to it for
older readers, marked optional.

| Identifier | Contents |
|---|---|
| `RULE` | settings as pairs (u16 key, u32 value); unknown keys are skipped |
| `WRLD` | game time, city name counter, difficulty and settings word, drought date and seed, random generators (state, increment), clocks of the houses and of growth, next disaster number |
| `ISLE` | per island in the order of the world, with island number for checking |
| `TILE` | differing tiles: island number, x, y, layer, building id, sub-tile, rotation, phase, owner, city, random value |
| `PLAY` `CITY` `PROD` `HOUS` `GROW` `COST` `SHIP` `ANIM` `DISA` `VOLC` | the lists of the world, complete (`SOLD`, the soldiers of older versions, is still read) |
| `PSTA` | per player (order as in `PLAY`): mission, status (playing, defeated, absent), won, defeated opponents, the two counters of the defeat rules, trade statistics |
| `MISN` | the missions (goals, loss conditions, values, text, the islands of the island goals recorded at the start) and the end of the game (time, winner) |
| `CSTA` | trade statistics per city (island, number, purchases, sales) |
| `MLCK` | per city (island, number) the game time the building material was locked or the people last complained about it |
| `CPOP` | per city (island, number) the last population message |
| `CGUP` | per city (island, number) the ships that gave up their way to its Kontor; optional |
| `GLDG` | per city (island, number) its goods statistics: minutes closed, per good the ledger (counts of the running minute and quarter, 60 minute and 48 quarter samples of 16 flows, or 17 in version 2, and the stock, warning given), then the history of the city window's figures if the city keeps one (human players only); optional ([ADR 0012](../architecture/0012-goods-statistics.md)) |
| `SCOR` | the score's counters per player (soldiers killed and lost, ships sunk and lost), then per city the growth of the house looked at last and the game time since it grows |
| `FLEE` | ships in full (position in 1/4096 tile, course, holds with price, trade route, destinations with the original's reference, the trader's goods), next ship number, discovered islands, trade settings and affection of the cities, shipyard fields (order, build points, wood, cloth, tick, repair, failed launches: a byte that was reserved and 0 before); `SHIP` remains for older readers |
| `ARMY` | soldiers in full (position in 1/4096 tile, hit points, home, destination, planning, clocks, patrol, path), next soldier number, cannon towers, building damage, shots in flight, weapons and slots of the castles |
| `EPOC` | phase of the island epochs and, per island, the phase last seen |
| `XPLR` | exploring: per island "growing explored" per player, and per deposit who knows it (bit p = player p; version 1 one u32, version 2 a list of u32 words for more than 32 players) |
| `DIPL` | diplomacy: per player pair peace, trade, time of war, tribute, trade and damage; per player all 32 events and the culprit |
| `QUEU` | command chains: per ship, then per soldier with a queue, the id and up to 16 waiting destinations (Shift+right-click) |
| `STNC` | per ship, then per soldier with a stance, the id and the flags attack move (bit 0) and hold position (bit 1) (ADR 0010) |
| `SIMU` | the state of the running game, below |

Buildings are stored as **Id** (from `haeuser.cod`) and sub-tile, not as image number, so that a savegame
also fits with changed graphics; an unknown Id makes the chunk invalid.

## Section versions

All sections are of version 1, except two that have a version 2 since islands can be wider or
higher than 255 tiles (SC-13), `PSTA`, whose version 2 carries the scenario switches, and `FLEE`,
whose version 2 carries trade routes with eight stops and version 3 stops with eight wares:

| Section | Version 1 | Version 2 |
|---|---|---|
| `ISLE` | x and y of the eight deer spots and of the craters as u8 | the same as u16 |
| `VOLC` | x and y of the last crater and of each eruption's crater as u8 (after the u16 island) | the same as u16 |
| `PSTA` | as in the table above | per player in addition the switches a scenario gives a computer (u32 bits, u16 rival and own inhabitants, u8 own and rival development; `PLAYER4` +0x1C..+0x25) |
| `FLEE` | per ship the route's four stops of the original | per ship the route's eight stops (Open1602's route editor), the same record per stop |
| `EPOC` | per island its number as u8 and the phase | the number as u16 |
| `GLDG` | per good the ledger with 16 flows | the same with 17 flows: after the 16, what producers withheld because the warehouse was full (`GoodsFlow::Withheld`). The writer takes it only when a ledger booked any |

`FLEE` version 3: the eight stops of version 2, each with eight wares instead of the original's
three (one per hold of the ship, Open1602's page of a stop), the same record per ware.
`FLEE` version 4: version 3 with the island number of each discovered-by entry as u16.
`FLEE` version 5: version 4 with a boolean per ship after the white flag: the pirates have already plundered this surrender and leave the ship alone while the flag stays up (Open1602's own; the writer takes it only when a ship under the white flag has it).
`FLEE` version 6: version 5 with a u32 per hold after its price: the red figure of the soldier of a package the hold carries, 0 for none (`CargoSlot::unit`, [own-figures.md](../modding/own-figures.md#soldiers); Open1602's own, the writer takes it only when a hold carries one). A unit there with a ware that is no soldier, or a number of 1024 or more, loads nothing.
`FLEE` version 7: version 6 with a u32 per shipyard after its order: the ship of a package the yard builds, 0 for the original's of the order's type (`CostBuilding::order_figure`, [own-figures.md](../modding/own-figures.md#ships); Open1602's own, the writer takes it only when a yard builds one). A number of 1024 or more, or one without an order, is read as 0.
`ISLE` version 3: version 2 with the island number and the x and y of each deposit as u16.
`PSTA` version 3: version 2 with a boolean per player after the switches: the player closed his city gates to foreign soldiers (Open1602's own; the writer takes it only when a player did).
`PSTA` version 4: version 3 with a u64 per player after the gates: who played it last (`Player::owner`, a verifier of the player ID of that person's installation, 0 for nobody; Open1602's own, the writer takes it only when a player has one).
`PSTA` version 5: version 4 with a u8 per player after the owner: the cheats it unlocked with a code (`Player::cheats`: 0 none, 1 the code 2061, 2 the code 512, which also works with more than one person; the writer takes it only when a player has unlocked them).
`XPLR` version 2: per deposit who knows it as a list of u32 words (u32 count, then the words; bit p of word w = player 32 w + p) instead of one u32 (`assets::SlotSet`; Open1602's own, the writer takes it only when a player beyond the 32nd knows a deposit, which a game of `sim::kMaxPlayers` = 12 never has).
`MAPW` (own savegame only) version 2: see [o1602-save.md](o1602-save.md).

The writer takes version 3 of `ISLE`, version 2 of `EPOC` and version 4 of `FLEE` only when an
island's number is above 255 (more than 256 islands) or, for `ISLE`, a deposit lies beyond 255.
None of them occurs in the chunk of a `.GAM`, whose islands the original numbers in one byte.

The writer takes version 2 of `ISLE` and `VOLC` only when one of these coordinates is above 255,
version 2 of `PSTA` only when a player has scenario switches, version 2 of `FLEE` only when a
route has a stop beyond the fourth, and version 3 only when a stop has a ware beyond the third. A version 1 `PSTA` keeps the
switches the chunks of the original brought (`PLAYER4`). A world whose
coordinates fit one byte (every world of the original, and every one written before) gives the
bytes of version 1 as before, which older readers still load; a reader of this version reads both.
A file with version 2 needs an Open1602 that knows it: an older one refuses it, since these
sections are required. Version 2 of `ISLE` and `VOLC` never occurs in the chunk of a `.GAM`: the
export refuses islands wider or higher than 255 tiles. Version 2 of `FLEE` can: the ship records of the
original (`SHIP4`) then hold the first four stops, and the route from its first stop when the ship
sails to a later one; the original plays the route with these, Open1602 reads all eight from the chunk.
Version 3 too: `SHIP4` then holds the first three wares of each of these stops.

`SIMU` is always written in version 2 (the state in parts, [below](#simu)); version 1 is still read.
An Open1602 from before 2026-10-24 refuses a file with `SIMU` 2, as the section is required.

## Safety when loading

- **Fingerprint:** if it does not match (another tool has changed the chunks of the original), Open1602
  loads only the chunks of the original and reports it.
- **Check before applying:** first everything is read and checked against the world (islands, tiles
  within their island, free slots of the production list, number of random generators, and that `PSTA`,
  `SCOR` and `DIPL` have as many players as `PLAY`, `FLEE` and `ARMY` as many buildings as `COST`, and
  `XPLR` as many islands and deposits as `ISLE`); only if everything is correct is it applied. A
  damaged or truncated chunk changes nothing.
- Every count is checked before memory is reserved: lists against the remaining length, the player
  count of `DIPL` (one entry per pair of players) against the player limit.

## Limits of this version

- A map larger than the original's (own world) carries its size in the chunk `O1602MAP`
  (u32 width, u32 height; after `WIFF`, before `O1602`), with up to 255 islands. Such savegames are loaded only by Open1602.
- The chunk cannot yet carry more than 7 players, 150 ships, 400 soldiers or 350 towers, nor an
  island wider or higher than 255 tiles (the original's records keep island coordinates in one
  byte); the writer rejects such worlds. With Open1602's rules (fleet limit 100 per player,
  unit limit 250) this is reachable.
- Commands not yet executed between two ticks are not saved (saving
  happens between the ticks).

## SIMU

The state of the running game that the world does not hold (`Simulation::save_state`). From
section version 2 (2026-10, DS-3) the body is the u16 `7` followed by parts, each with the header
of a section (tag, u16 version, u16 flags with bit 0 "required", u32 size) and its own version:

| Part | Version | Required | Contents |
|---|---|---|---|
| `SIMC` | 1 | yes | the clocks of production, cities, disasters, upkeep and islands (u32 each, in ms within their periods), the names for new cities and for new ships (u32 count, strings), the speed (u32, not 0) and the pause (u8, 0 or 1) |
| `LOGI` | 2 | yes | the figures (walkers) with their slots and generations, the jobs, the reserved tiles and the statistics; version 1 is without the shipyards' pack donkeys |
| `FLET` | 4 or 5 | yes | the fleet's clocks, the free trader's tools, the pirates' clock, running explorations, the market statistics of the last pass; version 5 with the free traders' choices under `Rules::trader_reach` |
| `MILI` | 3 | yes | the clocks of regeneration (1), the castles (2) and the natives (3) |
| `AIST` | 5 or 6 | no | the plans of the computer opponents (version 6 with 64-bit city flags and services, written only when a public building of a kind of its own lies beyond bit 31); a reader that cannot read this part lets them look at the world anew |

A part of a later version or with an unknown tag is skipped if not required; otherwise the game is
refused with the part's name ("SIMU: LOGI (version 3) needs a newer Open1602"). A part whose body
holds more than its reader takes is damage. Section version 1 held the same in one block, its own
u16 version (1 to 6) first and every later version adding at the end: 2 the shipyards' pack donkeys
and the fleet, 3 the military, 4 the computers (with a u32 version), 5 the names, 6 the speed and
the pause. Open1602 still reads those; `tests/unit/saves` keeps a game of each.

