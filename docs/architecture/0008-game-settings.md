# 0008 – Game settings

Status: accepted · 2026-10-17

## Context
Later titles of the Anno series let you configure the endless game far more precisely than Anno 1602:
starting capital, opponents, disasters, refunds on demolition and much more. Open1602 should be able to do
this as well. Until now, `sim::Rules` only had switches for deviations from the original
(ADR 0006).

## Decision
1. **All settings of a game live in `sim::Rules`**, whether they switch off a quirk of the
   original or offer a new choice. There is no second place for them.
2. Every setting has an **original value**, which `Rules::original()` returns, and a
   default value for Open1602. Where it is a free choice and not a bug of the original,
   the default is the behaviour of the original.
3. Settings belong to the **savegame** and to the **multiplayer session**. They are chosen before the
   game starts;
if they change during a game, then only via a command
   (ADR 0004), so that all machines compute the same thing.
4. The simulation reads settings only from `Rules`. The interface, the game start dialog and the
   command line fill in `Rules` and do not know the rules themselves.
5. New settings are documented in the code at the place that evaluates them.

## First settings
| Setting | Values | Original | Default |
|---|---|---|---|
| Refund on demolition (`demolish_refund`, `refund_percent`) | none, partial (1–100 %, preset 50 %), full | none | none |
| Moving buildings (`moving`) | free: the building goes to a new place in its city and keeps its level, inhabitants, stock and production; like building anew: the same, but the full costs are paid at the new place and the refund on demolition comes back for the old; off. Kontors, markets, towers, castles, a shipyard building or repairing a ship, burning or plague-struck buildings and ruins do not move | off (the original has no such tool) | free |
| Ships per player (`max_ships_per_player`, L-001) | number, 0 = unlimited | 33 | 100 |
| Units per player (`max_army`, L-008) | number, 0 = unlimited | 100 | 250 |
| Guarantee raw materials (`guarantee_resources`, S-019) | yes/no | no | yes |
| Highest game speed (`max_game_speed`, L-004) | factor | 4 | 8 |
| Droughts (`droughts`) | as the game (difficulty), off, rare, normal, frequent | as the game | as the game |
| Volcanoes (`volcanoes`) | as the game (setting), off, on, rare (as the game, but twice the time between eruptions) | as the game | as the game |
| Plague and fire outbreaks (`plagues`, `fires`) | as the original; rare (half as likely each time a town is checked); off (no plague or fire breaks out in a town by itself; fires from lava and spreading to neighbours go on) | as the original | as the original |
| Pirates (`pirates`) | as the game (setting bit 0; the endless games of the two easy levels have none), off, on | as the game | as the game |
| Pirate nests (`pirate_nests`) | one; one for every four human and computer players begun, each with the original's ships (2 to 4 by the highest level) and the interval between new ships divided by the nests; further nests go to strips of the map across its longer side that have none, new ships appear at the edge of the strip with the fewest pirates, and a pirate sails home to the nearest nest | one | one for every four players (up to four players the original) |
| Free trader prices (`trader_prices`, S-077) | as usual (about half the base price), by the market (as the original intends) | as usual | as usual |
| Free trader's destinations (`trader_reach`) | as the original (the 12 nearest Kontors by profit, a random one of them when none promises any); nearby, as with four players: with more than four human and computer players only the nearest Kontors' share a four-player game would have (Kontors × 4 / players, 3 to 12), and when none promises profit the one no trader has chosen for longest | as the original | nearby (up to four players the original) |
| Large iron veins (`ore_veins`) | as the game file says (settings bit 3: endless in every endless game and about 60 scenarios, finite in the other scenarios); finite (the ore mine digs a 240 t vein down to 120 t, the deep mine on to its end); endless: the same, but the deep mine's part never runs out (the vein stays at 119 t); small veins run out either way, gold never does | as the game file | as the game file |
| Computer opponents play (`computer_players`) | as the game file says (settings bit 1, which no shipped file sets); off (they keep what they have and do nothing; the pirates have their own setting); on | as the game file | as the game file |
| Computers buy ships (`computers_buy_ships`) | on: a computer short of ships buys one that a player at peace offers for at most the yard price, if half its money is left (as the History Edition's computers do); off: only as in the original, where a computer without ships or yard buys the cheapest ship on offer. That, and offering a spare ship for half its price, are the original's way either way | off | on |
| Trade carts per island (`more_carts`) | one per player and island, as the original; one for each Kontor and market of the player on the island, the further ones asked for with Shift on the city window's cart button | one | one per Kontor and market |
| Barren patches (`fertile_fields`) | as the original: the ground under a tile weighs the island's fertility for a crop, so that single tiles stay barren even where the island grows it fully; fertile: every tile has the island's fertility (a crop the island grows partly still carries on as many tiles, one it does not grow still does not, droughts still wither) | as the original | as the original |
| Alternative balance (`revised_bread` and `wider_hunting` together; `endless.balance = alternative`, the older `endless.bread` and `endless.hunting` are still read) | off: as the original; on: the hunter's hut reaches 12 tiles, and the grain farm makes 2 t of grain per field and the bakery 2 t of bread from 2 t of flour for 15 gold a minute instead of 5, so a bread chain makes about as much food as meat at the same running cost ([balance.md](../modding/balance.md#revised-bread-production)); a savegame without the setting plays the original's chain | as the original | revised |
| Towns known from the start (`reveal`) | nothing (every island is discovered by a ship coming near, also one where another player founded a town); as in the original (by ship, but a town a human or computer player founds shows its island to everyone at once, native villages and pirate nests are found by ship); all settlements (every island with a town of a human or computer player, a native village or a pirate nest is discovered for everyone from the start, also when the town is founded later). An island counts as a whole, so a native village on an island with a player's town shows with it | as in the original | as in the original |
| Bugs of the original (`bugs`, `OriginalBugs`), see "Original settings" below | each fixed or as the original | as the original | fixed |
| Victory conditions of the endless game (`victory`, `VictoryConditions`) | inhabitants, residents of a level or higher, treasury, balance per minute, share of settled islands, defeat all opponents, score, prestige buildings (castle, cathedral, both), time limit (then the most points win); all or any one; computers can win or not | none (the endless game never ends) | none |
| Custom world (`WorldSettings`, new game) | size 200–4000 per side or automatic by the players (as multiplayer: 500 × 350 up to four, both sides growing with the square root of the players beyond), 2–12 players, number of islands, island sizes, villages, volcano islands, gold islands (automatic by level, islands and players, or a fixed 1–40 for very large maps; settings.cfg up to 250), starting areas separate/shared, island shapes (the original's templates, mixed, own); with own shapes, on the page "Island world" (presets and a preview of the world the game gets, which keeps its world number; in a multiplayer game the world of the host's world number, which "Another world" changes while the game is created, and in the lobby everyone sees it): share of land 10–45 %, smallest island 16–200 and largest 40–1000 tiles (the map grows on its own where the largest finds no room), spread of sizes, gaps, coast, outline, forest, ore mountains | the three fixed maps | off; on: automatic size (500 × 350 for 4 players), 4 players, normal, mixed, automatic gold, separate, templates; own shapes: 15 % land, 30–100 tiles, many small ones, normal gaps, natural coast, compact, normal forest and mountains |

What two of the choices mean for the player (shown in the start dialog as "Trader's prices" and
"Every raw material"; in German „Preise des Händlers“ and „Jeder Rohstoff“):

- **Free trader's prices** (`trader_prices`): what the free trader pays for the player's goods. "As usual" is about half
  the base price, as players know the original. The original means him to follow the market, but it
  gathers the statistics for that in a way that depends on the frame rate, so they are almost always
  empty (S-077).
  "By the market" does what it intends: he pays up to three quarters of the average price at which
  the Kontors buy the ware, and stops buying a ware once all traders together hold half again as much
  of it as the Kontors want.
- **Every raw material** (`guarantee_resources`): a randomly generated world has at least one deposit
  of every raw material a building needs: every crop, iron and gold, gold if need be instead of a
  second iron vein. The original can generate endless games without any gold (S-019, in 31 % of the
  512 worlds of `Endlosspiel00`).

## Consequences
The Open1602 savegame has its own section for the settings (`RULE` in the chunk
`O1602`, done). Savegames of the original get the original's values for the choice points; this
is satisfied because the default of every free choice is the original. The exception is the bugs
of the original (`bugs`, below; the computer players since 2026-10-22): they are a choice for those
who want the game as they know it, but stay fixed by default, also in savegames of the original. The endless game has a
start dialog after name and banner (`engine/app/src/game_settings_menu.cpp`, since 2026-10-20): the
settings in groups (economy, limits, nature, world, opponents; the disasters on a page of their own), as in later Anno titles, each a button that
cycles its values, with presets for the original's rules and Open1602's defaults; the last choice is
kept in `settings.cfg` (`endless.*`, `endless.world = auto` for the automatic size). The command line (`--refund`, `--droughts`, `--volcanoes`, `--plagues`, `--fires`, `--pirates`, `--pirate-nests`, `--reveal`, `--ore-veins`, `--bread`, `--computers`, `--ai`)
still sets them for games started from it.

### Original settings
Since 2026-10-27 bugs and exploits of the original whose fix changes how the game plays can be
brought back one by one (`Rules::bugs`, `sim::OriginalBugs`). Real bugs stay fixed without a
switch; a bug gets one when players may want the game as they know it. Open1602's fixes are the
default, also in savegames of the original; `Rules::original()` and the dialog's preset "Original
rules" bring all of them back.

| Group | Bug (`OriginalBugs::`) | The original | `RULE` key |
|---|---|---|---|
| Disasters | fire risk (`fire_risk`, S-049) | brick buildings catch fire with 7 in 8, wooden ones with 3 in 8 | 32 |
| Disasters | gallows (`gallows`, S-055) | a robber a gallows turned away tries up to two more houses: a covered town still gets 58 % of the robbers | 31 |
| Disasters | robbers' victims (`robber_victims`, S-056) | only figures walking to the middle of their building: carriers of a building with its door on the edge escape in two of four rotations | 33 |
| Disasters | robbers' flight (`robber_flight`, S-058) | the robber plans 12 tiles and is gone there | 34 |
| Construction | gold sheep (`goldsheep`, S-002) | figures of a demolished building walk on and deliver to what stands on its place when they come home | 35 |
| Construction | building radius (`building_radius`, S-008) | every building, not only the market, needs one full edge in the city; the free land under it joins the city. In the original the land is lost again on loading, in Open1602 it is part of the savegame | 36 |
| Construction | fields everywhere (`infertile_fields`, S-003) | a crop is planted on an island where it does not grow, withered and paid for; it never grows | 37 |
| Ships and trade | trade from afar (`remote_trade`, S-012) | a Kontor's window open for a ship keeps trading with it after it sailed away (the command carries the Kontor, `CargoTransfer::kontor_island`) | 38 |
| Ships and trade | repair on the way (`remote_repair`, S-013) | the repair of a ship that leaves the yard goes on wherever it sails, and it can be ordered for a ship sailing away from the yard | 39 |
| Ships and trade | warships at the small yard (`small_yard_warships`, S-005) | the small shipyard builds the large ships too | 40 |
| Opponents | land of razed villages and towns (`native_land`) | a native village without its chief's house, market and huts keeps its land: the ruins hold it like a destroyed Kontor, nobody can build there; the same for a computer player's town without Kontor, market, tower and houses (fixed: the computer gives it up) | 43 |
| Opponents | computer players (`classic_ai`, S-097 to S-099) | settling, counting and site search with the original's bugs, so they stall after about two hours | 10 (always written) |

The start dialog has a group "More" below the disasters whose second row ("Original settings",
German „Originaleinstellungen“) opens a page of its own, built as the victory conditions' page: the
bugs in the groups of the table (the opponents' group holds only the land of razed villages), each a button with "fixed" or the original's way, and the
buttons "All as the original" and "All fixed". The computer players ("Improved" or "As the original")
are the exception: their row is in the group "Opponents" of the first page, not on this page, but
its two buttons switch them with the others, and its summary counts only the page's bugs. It is offered for every kind of game, scenarios
included, and shown for a savegame. settings.cfg keeps them as `<prefix>bug.<name> = fixed|original`
with the names of the table (the computer players also as the older `<prefix>ai = classic|open1602`),
the lobby passes them on with the other lines, the savegame as the `RULE` keys of the table, written
only when a bug is brought back, so games without them keep their bytes. The bugs that the
interface reaches (S-005, S-012, S-013) read them from `World::original_bugs()`, which the
simulation sets from its rules.

A new bug of this kind is a bool in `OriginalBugs` (and in `OriginalBugs::kAll`), the place in
`engine/sim` that reads it, a `SetupItem` after `BugNativeLand` with an entry in `kBugEntries`
(`engine/app/src/endless_setup.cpp`), its group on the page, its label and values in
`data/locale/*.lang` (`setup.bug.<name>`, `.fixed`, `.original`) and a `RULE` key in `kBugKeys`.

### Victory conditions
Since 2026-10-22 an endless game can be won, as in Anno 1503, 1701 and 1404: the start dialog has a
group below the disasters (since 2026-10-27 "More", before "Victory"), whose button opens a page of its own with the conditions in
four groups (population, wealth, power, end of the game). All are off by default and in both
presets; the presets change the rules and leave the conditions as chosen, "All off" on the page
switches them off. Every value is a plain number in `settings.cfg` (`endless.victory.*`), so any
value works there, the dialog offers a few.

`sim::check_victory()` (`engine/sim/src/victory.cpp`) checks every human and computer that is
still playing every 5 s of game time in `Simulation::players_tick()`, after its mission, from the
synchronised state only (integers, no floating point), so every computer of a multiplayer game
finds the same winner. The first to meet the conditions (all at once, or any one) wins as with a
mission (`GameEvent::MissionAccomplished`, then `GameOver`): alone with message 0x54 "you have
fulfilled your task", with other humans 0x6B + the winner's colour, as the original's
multiplayer races; whoever meets them at the same moment wins too. With a time limit, when the
game time is up the player with the most points (`sim::score_sheet()`) wins, on equal points
the first slot; nobody else. The end screen and the high score list are the missions'. The
conditions are `Rules::victory` and so part of `RULE` (keys 11 to 22, written only when one is
on, so games without them keep their bytes). Scenarios have their own missions and get none; the
mission window shows the conditions after the endless game's text.

Since 2026-10-22 the same dialog serves every new game; what is played (`SetupScope`) decides
what it offers:

| Game | Offers | Kept in `settings.cfg` |
|---|---|---|
| Endless game alone | everything | `endless.*` |
| Scenario (after the mission screen; not the tutorial) | the rules only: economy, limits, disasters, opponents. Its map and deposits are the author's, so no world items and no raw material guarantee | `scenario.*` |
| Multiplayer endless game | everything; the players are those of the lobby, more than four always play on a world of their own, which starts together by default as the original's multiplayer maps | `multiplayer.*` |
| Multiplayer scenario | as a scenario alone | `multiplayer.*` |
| Multiplayer savegame | nothing to change: the lobby shows the rules the game was started with | — |

The victory conditions are offered for both kinds of endless game and shown for a savegame; a
scenario never gets them.

In multiplayer the host chooses on the create screen and, until the start, in the lobby. The
lobby passes the choice on in `Lobby::setup`, the program's own bytes the session does not read, as
`settings.cfg` lines after a line `game = multiplayer|scenario|savegame`; so the protocol stays as
it is. Everyone else opens the dialog from the lobby to look only. The rules themselves reach
every computer with the game, as the savegame's `RULE` section (ADR 0011, point 6).
