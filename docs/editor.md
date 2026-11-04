# The editor

The editor makes and changes maps and scenarios for Open1602 ([ADR 0013](architecture/0013-editor.md)).
It runs in the game's window: **Editor** in the main menu opens it, and so does the command line:

```
open1602 --editor                          a new map
open1602 --editor "Szenes/Das Atoll.szs"   a scenario of the game (a relative path is looked up there)
open1602 --editor my-map.o1602             an Open1602 scenario or savegame
```

The editor shows the map as the game does: it scrolls, turns and zooms with the game's keys and
mouse. The world never runs while it is edited: no figure or ship moves, nothing is built, no time
passes. Only the pictures go on as they do (water, flags).

## The editor's screen

The editor has a frame of its own around the map instead of the game's panel:

- **The menu bar** above the map: **File** (new map, open, save, save as, test play, save the map as a picture, leave the editor), **Edit**
  (undo, redo, find), **View** (the overlays), **Map** (islands, map size, random elements, dice preview),
  **Players**, **Scenario** (description, rules, goals), **Check** and **Help** (the keys). The menus name each command's key. At
  its right end it shows the map's name, with `*` while it has changes that are not saved.
- **The tool bar** down the map's left edge: a symbol for each tool, in groups: select; island and
  placeholder; land, water and tidy coast; the terrain pieces' brush, random fill, fill, replace,
  pipette, area and paste; buildings, building templates, ships, land units, resources and remove.
  The mouse over a symbol shows the tool's name and key in the status line.
- **The column** on the right: the minimap at its top, below it the page of the tool chosen or of
  the command (its settings, or what is selected).
- **The status line** below the map: what happened last (saved, or why something was refused), and
  the tile under the mouse with its island.
- **Dialogs** in the window's middle for the pages of the whole map: Players (with Diplomacy), Rules
  (with Random elements), Map size and Check. A dialog takes every click until it is closed with
  its button **Close** or with Escape; a tool chosen meanwhile closes it too.

The window's title names the map instead of the game's time.

## The pages

Without a tool chosen, the column shows the map the editor has open: its name, its size, its
islands and its players. A scenario of the original that makes its islands when a game starts (the
endless games) shows how many islands its plan makes; they are made when a game of it starts, as in
the original. The menu bar's commands and the tools:

| Command or tool | What it does |
|---|---|
| New map | A map of one of the sizes offered (500 × 350 as the original, up to 4000 × 4000): only sea, or with islands, players and their ships as a new endless game makes them. Kind of islands chooses them as the endless game's setup does: the original's templates (with the packages' island templates), one of Open1602's kinds of islands of their own shape, or a package's (`landscapes.toml`); islands of their own shape may make the map larger where the largest would find no room. A world number gives the same world again. |
| Open | The own scenarios, then the scenarios of the game. |
| Save | Saves the map as an Open1602 scenario (`.o1602`). A map that has no such file yet asks for a name first. |
| Save as | Saves under a new name in the folder of own scenarios (`scenarios` in Open1602's user data folder), which the scenario list of the main menu shows. A name an own scenario has already replaces it. |
| Undo, Redo | Takes the last change back, or does it again. |
| Edit islands | New islands: one of the original's templates (or a package's, from `islands.toml`) or one of its own shape, north, south or of a package's climate (`climates.toml`; a placeholder only north or south), in the five sizes of the templates (30 × 30 up to 100 × 90); Another one picks the next template, the panel saying which (its number, or a package's island by name), or another shape. Place puts it in hand: its outline follows the mouse, green where it fits on open sea and red where not; a left click puts it down, a right click or Cancel puts it back. Load island lists the islands saved with Save (below) and the original's island files (`.scp`) put into the folder `islands` in Open1602's user data folder, and puts the one chosen in hand the same way; an `.scp` file, which does not tell its climate, gets the climate chosen on the page. |
| Placeholders | A scenario of the original that makes its islands when a game starts has a world plan: each of its islands is a placeholder, drawn as a yellow outline of its size class with its name. The shape Placeholder on Edit islands sets a new one (north or south, a size class, its template drawn when the game starts); a click on a placeholder selects it (below). Islands cannot be put on a placeholder. When a game of the scenario starts, the world keeps what the editor left (its islands, cities, players and ships) and only the placeholders become islands; what is beyond the original's limits does not start with it. |
| Terrain tools | Land, water, tidy coast and the pieces' tools (below). |
| Object tools | Buildings, templates, ships, land units, resources, remove (below). |
| Change map size | Makes the map larger or smaller in steps of 50 tiles, or fits it to its content with some sea around. It grows or is cut at the right and the bottom, so everything keeps its place, and it cannot be cut below the islands, ships, land units and markers. |
| Overlays | What the map shows over the world: the players' land in their colours, the numbers of the islands, the coordinates of the tile under the mouse. |
| Check | The check list: what keeps the scenario from being saved (the world must hold together, its description and markers be right), and what the original cannot have (its map size, more islands, cities, ships, land units, towers or players with a start than it holds, larger islands, more ore deposits, content of a package, a script). With two starts or more, the start areas: for each player, the islands up to 60 tiles around its start (its first warehouse, else its first ship), their land, the crops they are fertile for, their ore and gold, and how far the nearest rival starts; a warning where a start has much less land than the richest (below 70 percent), or lacks a crop, ore or gold that others have. The original's limits are a fault only for a scenario that is to keep to them. A click on a line leads to what it is about: the island on the map, or the dialog of the description, the goals (on the mission), the players or the map size. |
| Leave the editor | Back to the main menu. |

New map, Open and Leave the editor ask before changes that are not saved are lost.

A left click on the map selects what is there: a ship, a land unit, a building, an island or the
sea. The panel then shows its properties (an island its position, size, climate, ore deposits and
cities; a building its island, position, city and owner; a ship its owner, position, cannons and
cargo), and a cross on the map marks it. Back to the map or Escape returns to the map's page.

A selected island has six more buttons:

| Button | What it does |
|---|---|
| Move | Takes the island in hand as a new one: a left click puts it down elsewhere. An island with cities moves with everything on it, and the ships in its rectangle (at its coast) go with it. Another ship where it would lie keeps it from that place (a red outline). |
| Turn | Turns the island a quarter clockwise about its centre, with its deposits, volcano, deer and growing trees. |
| Copy | A copy in hand, with a number of its own: a left click puts it down. Nobody knows the copy yet. |
| Delete | Takes the island out; the islands after it get the numbers one lower. |
| Crops | Which of the six crops (tobacco, spices, sugar cane, cotton, wine, cocoa) grow fully on the island. Unticked, the crops of its climate grow half (tobacco, sugar cane and wine in the north, spices, cotton and cocoa in the south, a package's climate those it names), the others not at all. Each tick is a step of undo. |
| Save | Saves the island alone into the folder `islands` (Island 1, Island 2, ...), with its tiles, deposits and fertility, for Load island. |

A selected placeholder has Move and Delete, and its climate (north, south, or by its place on the
map, as the original's plans have it) and size class as two buttons that go through their values.
Each change is a step of undo.

Turn, Copy, Delete and Save need an island without a city, buildings or units of a game (they would have
to go with it, which is for later), and none of these works in a running game opened as a map
(a savegame), whose figures stand on the islands. Every change of an island is one step that Undo
takes back.

## Terrain

The terrain tools of the tool bar paint the islands' terrain from a palette of the game's own: ground,
forest, rock and ore mountains, slopes, rivers, beach, surf and sea. The column shows the tool's name
and only the settings it uses. **Kind** picks a group of the palette, **Piece** goes through its pieces
(a right click goes back) and **Turn** turns a piece that can turn. **Size** sets the brush's side,
from 1 to 17 tiles: with the buttons - and +, the slider between them, the keys + and -, or Ctrl and
the mouse wheel. **Shape** makes the brush round, square or a diamond (Land, Water, Tidy coast, Random
fill and the brush of a one-tile piece). A white dotted outline on the map shows the tiles the tool
lays, and the brush shows its piece there before the click.

Land, Water, the brush and Random fill draw the other way with the right button held: water where
they lay land or pieces, land where Water lays water, so that a mistake is painted over without
changing the tool; the outline turns orange meanwhile. A right click without dragging still drags
the map or puts back what is in hand. The tools:

| Tool | What it does |
|---|---|
| Brush | Paints the piece where the left button is pressed and where the mouse goes with it held. A piece of several tiles (a mountain, a river bend) is laid about the mouse. |
| Land | Paints land into the water and lays the coast about it as the game's islands have it: beach and its corners on the land's edge, surf before it, then light, middle and dark sea outwards. Land painted at an island's edge makes the island grow by open sea, and land painted on the open sea far from islands makes a new island there; either is one step of undo with the stroke. |
| Water | Paints water into the land, with the surf and the beach about it. |
| Tidy coast | Lays the coast under the brush anew as Land and Water lay it: beach, surf and their corners fitting the land, beach left inland becomes meadow. |
| Random fill | Paints pieces of the kind, each tile its own drawn at random and turned at random: ground as the game's islands mix it, forest of the island's climate. |
| Fill | Paints every tile that is joined to the one clicked by its sides and looks like it. |
| Replace | Paints every tile of the island that looks like the one clicked; tiles that turn keep their turn. |
| Pipette | Takes the piece of the tile clicked, with its turn, and goes back to the brush. |
| Select | Drags a rectangle over an island (light blue). **Copy** keeps its land and inland pieces as the copy; **Turn copy** turns it a quarter clockwise, **Mirror copy** mirrors it left to right. |
| Paste | Lays the copy about the mouse, its land and water, the coast about its land laid anew, then its pieces; one step of undo. The map shows it before the click; where it cannot go, its frame turns red. |

Water, beach and land are three levels of the map's picture, and a piece fits only on its own level:
the brush leaves the coast as it is, and Land and Water change it. Where a coast would take a form the
game has no tiles for (a strip one tile wide, land touching land only at a corner), Land and Water
widen it on the tiles next to the brush, or refuse where they cannot. Land keeps a tile of water to its
island's edge; an island with a city does not grow. Land painted within three tiles of another island's
land joins both islands into one, so that the coast between them is one. If one of them has a city or
buildings, they are not joined: the brush stops four tiles short of the other island, and a notice says why.

A tile gets the piece on top and as the ground below. A mountain or river bend that a piece covers in
part becomes meadow where it is not covered, and its ore deposit or volcano goes with it. The terrain
tools paint only islands, not open sea, and leave buildings, the land of cities and the tiles under a
ship or a land unit as they are. Each stroke, fill or replacement is one step of undo.

## Objects

**Place objects** puts down the buildings of the build menu, ships and land units for a
player, without costs, and takes them away again. Each click, or each drag, is one step of undo.

| Setting | What it does |
|---|---|
| Tool | **Build**: a left click puts the building down about the mouse, for the owner; dragged, the next one goes where it no longer covers the last, so roads, walls and fields are dragged out. **Template**: a left click builds one of the templates saved in games (the copy tool's pieces, kept in the folder "templates" of the user's data folder) about the mouse, for the owner; what does not fit is left out. **Ship**: a left click puts a new, empty ship on the water. **Unit**: a left click puts a land unit on an island. **Resource**: a left click puts a vein into an ore mountain, a treasure under open ground or deer into a forest. **Remove**: a left click takes the ship or unit under the mouse away, else the treasure, deer or vein there, else the building, as its owner demolishes it. |
| Group | The groups of the build menu (with the buildings packages add to it), then the natives' and pirates' buildings (the chieftain's hut, the pyramid, the pirates' warehouse, houses and tower, ...); not for ships and units. |
| Building, Template, Ship, Unit, Resource | The group's buildings by name; the templates by name, read again from their folder each time the tool is chosen; the four ships of the shipyards (with those packages add), the pirates' ship and the free trader's ship, the four land units of the castle (with those packages add); a big or small iron vein, gold, a treasure or deer. |
| Rotation | A quarter clockwise, for a building that turns or a template; the way a ship or unit faces. |
| Owner | The player the building, template, ship or unit is for: one of the map's humans and computers, or the natives or the pirates where the map has them; for a ship also the free trader. |
| Placed lately | The last six objects placed, the last first: a click takes one in hand again (its tool, building or type and rotation), for the owner chosen now. |

The object stays in hand after it is placed, so the next click places another. A right click
without dragging, or Escape, puts it away and the tool Select takes over. Where the tool would not
place, the status line below the map and the column's note say why (no land of the owner there, no water for a ship, no
mountain for a vein, ...) before the click. With Shift held, a drag of Build places along a straight
row: across or down from where it began, whichever way the mouse went further.

A building is placed as the game builds it, only without gold, goods, unlocked levels or limits
(castle, cathedral, monuments): it needs its ground, the owner's land (a Kontor at the coast founds a
city and claims its land), a coast, river or rock face where the game asks for one. A building on a
buried treasure leaves it in the ground. The outline under the mouse is green where the building can
stand and red where not; a Kontor, shipyard, mill or mine finds the coast, river or rock face near
the mouse and turns to it. A refused click says why. A Kontor goes only once its city has nothing
else, and its piers go with it.

Each owner founds one city on an island, the natives and the pirates as well. Their cities are named
after what they are ("Native village", "Pirates' nest", with a number from the second on); the
others take the next name of the game's list of cities.

A template is built as one step, each of its buildings as above; it hangs at the mouse by its
middle, and the outline shows each of its buildings green where it can stand and red where not.

A ship goes on water ships sail on, with no other ship on the tiles about it; it is new as a
shipyard launches it, at full strength with empty holds, and gets its name when a game of the map
starts. A land unit goes on ground soldiers walk on (open ground, roads, squares, ruins), one to a
tile, at full health. Only the free trader sails the trader's ship, and he sails no other; one placed sails from Kontor to Kontor when the game starts, its goods empty, and the game sends more as the Kontors grow. A computer disbands its units on an island without a city of its own while it is at peace; the editor says so when one is placed there. Orders of other ships and units that name a ship or unit taken away are
forgotten with it.

Resources lie where a new game puts them, and nobody knows them at first:

| Resource | Where it goes |
|---|---|
| Iron ore, big vein | 240 t in a big ore mountain, which the deep mine digs. |
| Iron ore, small vein | 80 t in any ore mountain. |
| Gold | 50 t in any ore mountain. |
| Treasure | 1000 gold under a tile of open ground; whoever builds on it finds it. |
| Deer | A spot in a forest where deer live, at most 8 on an island. |

A mountain holds one vein; a click anywhere on it puts the vein where the mines look for it. While the
page is open the map names the veins, treasures and deer of every island.

## Cities

A building clicked on the map, outside the pages Terrain and Objects, shows its city's
buttons on the Selection page, each change one step of undo:

| Button | What it does |
|---|---|
| Name | Types a new name for the city, up to 40 characters, as a player may give it. |
| Stock | The city's trade goods with the tons in its warehouses: + and - change them by 10 t, with the right button by 1 t, up to the room its Kontor, markets and warehouses give (the game would lose what is above). |
| Owner | Gives the city with all its land and buildings to the next human or computer (the right button: the one before), as when a player takes a city over; a player with a city on the island already is passed over. |

## Players

**Players** lists the slots of the humans and computers, one row each (the free trader, the pirates
and the natives keep theirs and are not listed); a click on a value changes it, the right button
steps it back. Each change is one step of undo.

| Setting | What it does |
|---|---|
| Kind | Human or computer. |
| Colour | The player's colour; one another player has goes to it in exchange. |
| Starting money | The gold the player starts with: 1000 a click, 10 000 above 50 000; the right button takes it off again. |
| Monuments, Arches | How many monuments and triumphal arches the player may build (0 to 9). |
| Name | A click on it renames the player; left empty, the game calls the player after its colour. |
| Add, Remove | A computer player in the next slot (the fifth and those after it go behind the trader, the pirates and the natives), with the money of the first computer; the last slot taken away again, as long as it owns no city, ship or soldier. The first four slots stay. |

A player without a city and a ship is out of the game (defeated) as soon as it starts: give a new player a ship or a Kontor on the
page **Place objects**.

The tab **Diplomacy** shows how the players stand with each other when the game starts, as a
table: the players down its left and, by their numbers, along its top. A click on a cell steps it
from war to peace to peace with a trade treaty and back to war; the right button goes the other
way. A change applies to both sides, and clears what
the two remember of each other (tribute, trade, damage). A trade treaty needs peace; war ends it.
Adding a player keeps the relations of the others; the new one starts at peace with everyone.

## Description

**Description** (menu Scenario) sets what the scenario list shows of the scenario: its name and
introduction in German and English, the difficulty in stars (0 to 5), the author and a version.
A click on a text opens a field to type it (Shift+Enter breaks a line in an introduction);
an empty text takes that language away. Without a
name the list shows the file's name, which still finds the scenario. Texts in other languages,
written by other tools, are kept. **The original's limits** switches the check of the original's
size, islands, players and buildings on or off.

The introduction is the page shown before the scenario starts; the missions' own texts are set
under Goals.

## Markers

**Set markers** (the last tool, M) shows the scenario's markers on the map and lists them in the
column: named points and areas the scenario's script finds by their names (`game.marker`,
[scripts.md](modding/scripts.md)). A click on the open map sets a point, a drag an area; a click on
a marker, or on its row, selects it. A new marker is named `marker_1`, `marker_2`, ...; **Rename**
gives it a name of lower case letters, digits and `_`, **Delete** takes it away. Each is a step of
undo.

## Script

**Script** (menu Scenario) gives the scenario a Lua script of its own, saved with it, which runs in
the game like a package's script ([scripts.md](modding/scripts.md)), after the packages'. **Load**
lists the `.lua` files in the folder `scripts` in Open1602's user data folder; **Reload** reads the
file loaded last again after it was changed elsewhere (the scenario keeps where it came from, so
this works after it is opened again); **Remove** takes the script away. The dialog
shows the script's size and its first syntax error with the line; an error while the game runs is
shown in the status line with its line, as for a package's script. Test play runs the script too.

## Rules

**Rules** (menu Scenario) sets the game settings the scenario is made for, the same ones the start dialog offers
for a scenario: disasters, construction, ships and trade, limits, opponents, and the bugs of the
original. **The player's rules** leaves them to the player, as for any scenario; a click switches
to **The map's own rules**, which begin from those the player last started a scenario with. **As
original** sets every one as in the original, **Open1602** as Open1602 has them. Each setting goes
to its next value with the left button and back with the right one; one that does not matter with
the others as they are is greyed out. The settings stand in two columns, each beside its name. Each change is one step of undo.

When a player starts the scenario, the start dialog begins from its own rules; the player can still
change them, and they are not kept as the player's own for the next scenario.

### Random elements

On a map with a world plan, **Random elements** lists what the plan spreads at random when a game
starts: the native villages of the north and the south, iron (small and large veins), gold, the
treasures, and the islands fertile for each crop. Each entry reads "x at random, y fixed": x is
what the plan places at the start, y what the map already has in place. The left button adds one
to the random ones, the right button takes one away; each change is one step of undo.

**Dice preview** makes the islands as one start of a game makes them and shows them on the map.
**Roll again** makes another start, **Back to the plan** brings the plan back. The preview is never
saved and cannot be edited; undo, save and the other pages wait until the plan is back.

## Goals

**Goals** (menu Scenario) sets the scenario's missions. A mission is a set of goals that must all
hold at the same time, and up to two loss conditions; each human or computer holds one mission or
none, and several players can hold the same one. The tabs at the top choose the mission, **+** adds
one (up to 16), **-** takes the one shown away. Below them, a switch for each player says who holds
it; **Change text** types the text the player reads about the mission (Shift+Enter breaks a line).

| Goal | What it asks for |
|---|---|
| Number of cities | At least so many cities. |
| Inhabitants in all | At least so many inhabitants in all own cities together. |
| Balance, Trade balance | At least so much per minute (0 counts: no loss). |
| Opponents defeated | At least so many opponents out of the game. |
| Defeat player | That player out of the game; natives when their last village is destroyed by the mission's holder. |
| Build a castle, Build a cathedral | One built. |
| Good 1 and 2 in stock, Amount | So many tons of the good in the warehouses of all own cities. |
| All islands with 1 and 2 | Every island fertile for the crop, or with iron ore or gold, settled, with no human or computer opponent on it. |
| City 1 to 3, Every city | A city (or every own city) with so many inhabitants, of whom so many from a level up. The three cities are distinct ones. |
| Target player, City of the target player | A city of that player (or of any opponent) so large. |
| Lost: city below, Lost: cities below | The mission is lost when no city of the target player stays that large, or the target player keeps fewer cities. |
| Computers win too | Computers holding the mission can win by it too. |
| Message on winning | The message and its video for the winner: "Mission accomplished", or the multiplayer win in the winner's colour. |

Each value goes on with the left button and back with the right one, through **Off**; a detail
that does not matter as the rest is set is greyed out. Each change is one step of undo. A mission
without any goal never ends the game, as an endless game.

**Check** lists what the missions cannot meet or meet at once before the game starts: a mission
nobody holds, a human without one while others have one, an island goal no island can meet (it
counts as met at once), a good or a player that is not in the game, more opponents to defeat than
there are, a target player who starts without the cities the loss conditions ask for.

## Keys

| Key | What it does |
|---|---|
| Ctrl+N | New map |
| Ctrl+O | Open |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as |
| Ctrl+Z | Undo |
| Ctrl+Y, Ctrl+Shift+Z | Redo |
| V | Select |
| I | Island |
| H | Placeholder |
| L | Draw land |
| O | Draw water |
| K | Tidy coast |
| P | Brush for terrain pieces |
| Z | Random fill |
| F | Fill |
| Shift+F | Replace |
| C | Pipette |
| B | Select an area to copy |
| Ctrl+V | Paste |
| G | Place buildings |
| T | Place a building template |
| Shift+S | Place ships |
| U | Place land units |
| Shift+R | Place resources |
| X | Remove |
| M | Set markers |
| R | Turn the piece or object in hand |
| + and -, Ctrl and the mouse wheel | Larger and smaller brush |
| Right button dragged | Land, Water, the brush and Random fill draw the other way (water for land) |
| Right click | Puts back what is in hand (an island being placed, an object) |
| Shift and Build dragged | Places along a straight row |
| F5 | Test play; in the test game F5 leads back |
| Ctrl+F | Find an island, a city or a ship |
| Ctrl+1 to Ctrl+0 | Keeps the view as a bookmark (for this session) |
| 1 to 0 | Goes to the bookmark |
| F1 | The keys |
| Escape | Closes a menu, or goes back from a page of the column |

The view's keys are those of the game (WASD and the arrows, Q and E, Page Up and Page Down, F12 for a
screenshot, F11 for the full screen). F1 shows the keys the editor knows. All of them can be changed
in `keys.cfg`; the editor's are in the context `editor`.

## Find

**Find** (menu Edit, Ctrl+F) lists, while a name is typed, the islands (by their number), the
cities and the ships whose names hold it. A click on one, or Enter for the first, moves the view
there and selects it.

## Test play

**Test play** (menu File, F5) starts a game of the map as it is now, as the scenario list would
start it from its file: with the scenario's rules, its random elements rolled, the missions held
as set. Nothing has to be saved first. In the test game F5 (the quick save's key) leads back to
the editor as it was: the same map, its undo and the same view. The main menu's **Editor** leads
back too, also after another game was loaded meanwhile. The test game is not saved automatically, and ending it in the game's menu leads back to the editor without keeping it as `lastgame`. Test play needs a human player.

**Save map as picture** (menu File) writes the map as a PNG into the screenshots folder: two pixels a tile, in the colours of the new game's preview, with the players' starts as white squares.

## Saved games

**Open** also lists the saved games of the game's save list (**Saved games**; not the last game,
which is never saved into by hand). A saved game opens with its running state: the cities, ships
and soldiers, the time and the money as they were. It is changed with the same tools as a
scenario, except what the running game holds fixed (the terrain, the map's size, islands that
are in use). **Save** writes it back to its slot as a saved game; the game goes on from there
when it is loaded. Saved by the editor, the game is marked as edited (section `EDTD`): the save
lists say so, and the game enters no high score list, also after it is saved again in the game.
A test game counts for no high score list either. **Save as** makes a scenario of it instead.

## Recovery

A minute after a change, and before every test game, the editor keeps a copy of the map in the
user's data folder (`editor-recovery.o1602`). It goes once the map is saved or its changes are
discarded. If the editor ended without either (a crash, a power cut), the next time the editor is
opened from the main menu it offers the copy. A recovered map counts as not saved until it is.

## Files

The editor always saves Open1602's own format. It never writes a file of the original: a scenario of
the game that was opened is saved as an own scenario. The game's installation is never written to.
