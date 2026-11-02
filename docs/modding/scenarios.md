# Scenarios

A content package brings scenarios of its own: a map, its names and briefing in every language, its
difficulty, the money at the start, the goals of the people's mission and a script of its own
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 6, item 7 of
[open-work.md](open-work.md)).

**Status:** Done on 2026-10-31: scenarios of packages in the scenario list of the main menu, with
goals, money and a script, campaigns of them, part by part, with the money and a table of the script
carried from one part to the next, and player slots given to factions of packages.

## The file

```
<user data folder>/mods/<package>/scenarios.toml
```

One table per scenario; its name holds lower case letters, digits and `_` and makes the scenario's
id `<package>:<name>`:

```toml
[north]
file = "maps/north.szs"
script = "north.lua"
stars = 3
money = 25000
name.en = "The North"
name.de = "Der Norden"
briefing.en = "Settle the cold islands of the north. Build a castle and keep 50 t of tools."
briefing.de = "Besiedelt die kalten Inseln des Nordens. Baut ein Schloss und lagert 50 t Werkzeug."
goal.inhabitants = 2000
goal.castle = true
goal.goods.tools = 50
```

| Key | Value |
|---|---|
| `file` | the map, a file of the package: a scenario of the original's format (`.szs`, `.szm`) or a game saved by Open1602 (`.o1602`). Required |
| `script` | a Lua script of the package that runs only in this scenario ([scripts.md](scripts.md)) |
| `stars` | difficulty 0 to 5, shown as the original's stars |
| `money` | money of every human player at the start; without it the map's |
| `name.<language>` | the name in the scenario list; without one the id |
| `briefing.<language>` | the text of the mission screen and of the mission window; without one the map's mission text |
| `goal.…` | the goals, see below |
| `slot.<n>` | the faction of the map's player slot `n` (from 1): a faction of the package by its name, another's by its id (`"anno1602:natives"`, [factions.md](factions.md#factions-of-a-packages-own)). A faction of the settlers' behaviour is steered by the computer unless a person plays the slot; one of another behaviour makes the slot neutral and never takes a person's |

A scenario whose map is missing or outside the package's folder, or with a value that cannot be
read, is left out; the game's log names the file, the line and the key.

## Goals

The goals become the mission of every human player; the computers keep the missions the map gives
them. All goals given must hold at the same time, as in the original's missions
([missions.md](../re/missions.md)). Without any goal the map's own missions stay.

| Key | Goal |
|---|---|
| `goal.inhabitants` | at least so many inhabitants in all own cities together |
| `goal.cities` | at least so many cities |
| `goal.balance` | a balance per minute of at least this |
| `goal.trade_balance` | a trade balance per minute of at least this |
| `goal.defeated` | at least so many opponents defeated |
| `goal.castle`, `goal.cathedral` | `true`: a castle, a cathedral built |
| `goal.goods.<good>` | at least so many tons in the warehouses of all own cities; at most two goods. `<good>` is a name of the original's goods (`tools`), of the package's own (`amber`, [goods.md](goods.md)) or of another package's in quotes (`goal.goods."spice:pepper"`). A good the game does not know when the scenario starts is left out of the goal |
| `goal.city.inhabitants`, `goal.city.class`, `goal.city.count` | one city with at least `inhabitants`, `count` of them of class `class` or higher (0 pioneers … 4 aristocrats, then the classes of packages, [classes.md](classes.md)) |

## In the game

The scenario list of the main menu shows the packages' scenarios under the heading `----Mods----`,
after the game's own, with their stars, names and briefing in the game's language. Their high
scores are kept beside the others' under `<package>_<name>`.

A started scenario loads the map, then sets the money and the mission. Its script runs after the
package's `script.lua`, in a sandbox of its own: its table `state` is saved under the scenario's id
and it posts the package's messages. The game remembers the scenario it was started from (section
`SCEN` of the [savegame](../formats/o1602-save.md)), so a saved game runs the scenario's script
again after loading, as long as the package is installed and turned on.

A map saved by Open1602 (`.o1602`) keeps the packages it was saved with
([packages.md](packages.md#which-packages-a-game-uses)): save it with the package turned on, so its
buildings and goods are part of the map.

## Own scenarios

Scenarios that come without a package, such as maps made with the original's editor or shared by
other players, go into the folder `scenarios` of the user data folder, beside `mods` (Linux
`~/.local/share/Open1602/scenarios/`, macOS `~/Library/Application Support/Open1602/scenarios/`,
Windows `%APPDATA%\Open1602\scenarios\`). The game makes the folder when it shows its menu. Scenarios
in the installation's `Szenes` folder keep working as well.

The folder takes scenarios of the original's format (`.szs`) and games saved by Open1602
(`.o1602`, later also maps of Open1602's own editor). Files whose names differ only in their last
digits make one entry with a choice per digit, as in `Szenes`. The scenario list of the main menu
and of the multiplayer menu shows them under the heading `----Own scenarios----` (German
`----Eigene Szenarien----`), after the game's own and before the packages', in name order with
their stars.

A scenario whose name the list has already is left out and the log says so: the game folder's
scenario wins over an own one of the same name, and an `.szs` over an `.o1602` of the same name.
Two entries of one name would share their high score list. Rename the file to list both.

## Campaigns

```
<user data folder>/mods/<package>/campaigns.toml
```

A campaign plays scenarios of the same package one after the other. One table per campaign; its
name makes the id `<package>:<name>`:

```toml
[frost]
name.en = "Frost"
name.de = "Frost"
briefing.en = "Two winters in the north."
stars = 3
carry.money = true
part.1 = "north"
part.2 = "south"
```

| Key | Value |
|---|---|
| `part.<n>` | the scenario of part `n` (1 to 5, the choices of the mission screen), by its name in the package's `scenarios.toml`. Required: at least `part.1`, and no part left out between the first and the last |
| `name.<language>`, `briefing.<language>` | as a scenario's; a part's own briefing is shown when it has one |
| `stars` | difficulty 0 to 5 |
| `carry.money` | `true`: every human starts a part with the money the player had when winning the part before, instead of the part's `money` |

The campaign is one entry of the scenario list, its parts the choices of the mission screen. At
first only part 1 is open; winning a part alone opens the next, as the original's campaigns do. The
progress is kept with the original's (`progress.cfg` in the user data folder, a line
`package <part> <campaign id>`).

**Carried values.** The script of a part (the scenario's `script`) has a global table `campaign`.
When a part is won, the game keeps what is in it then, with the player's money, in
`campaigns/<package>_<name>.<part>.carry` of the user data folder; the next part's script finds it
in `campaign` after its top level ran, so a top level such as `campaign = {gold = 0}` gives the
first part its start values. The table is saved with the game (section `SCRP`), so a loaded part
goes on with it. A part's script also reads `game.campaign` (the campaign's id) and `game.part`
(the part, from 1).

```lua
campaign = {heroes = 0}

function on_start()
    if game.part > 1 then
        game.message("welcome_back", {heroes = campaign.heroes})
    end
end

function on_player_won(player)
    campaign.heroes = campaign.heroes + 1
end
```
