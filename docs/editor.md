# The editor

The editor makes and changes maps and scenarios for Open1602 ([ADR 0013](architecture/0013-editor.md)).
It runs in the game's window: **Editor** in the main menu opens it, and so does the command line:

```
open1602 --editor                          a new map
open1602 --editor "Szenes/Das Atoll.szs"   a scenario of the game (a relative path is looked up there)
open1602 --editor my-map.o1602             an Open1602 scenario or savegame
```

The editor shows the map as the game does: it scrolls, turns and zooms with the game's keys and
mouse, and the minimap, compass, rotation and zoom buttons work as in the game. The world never runs
while it is edited: no figure or ship moves, nothing is built, no time passes. Only the pictures go
on as they do (water, flags). The game's mode buttons above the panel are greyed.

## The side panel

Below the minimap the side panel shows the map the editor has open: its name (`*` after it while it
has changes that are not saved), its size, its islands and its players. A scenario of the original
that makes its islands when a game starts (the endless games) shows how many islands its plan
makes; they are made when a game of it starts, as in the original.

| Button | What it does |
|---|---|
| New map | A map of one of the sizes offered (500 × 350 as the original, up to 4000 × 4000): only sea, or with islands, players and their ships as a new endless game makes them. A world number gives the same world again. |
| Open | The own scenarios, then the scenarios of the game. |
| Save | Saves the map as an Open1602 scenario (`.o1602`). A map that has no such file yet asks for a name first. |
| Save as | Saves under a new name in the folder of own scenarios (`scenarios` in Open1602's user data folder), which the scenario list of the main menu shows. A name an own scenario has already replaces it. |
| Undo, Redo | Takes the last change back, or does it again. |
| Edit islands | New islands: one of the original's templates or one of its own shape, north or south, in the five sizes of the templates (30 × 30 up to 100 × 90); Another one picks another template or shape. Place puts it in hand: its outline follows the mouse, green where it fits on open sea and red where not; a left click puts it down, a right click or Cancel puts it back. |
| Overlays | What the map shows over the world: the players' land in their colours, the numbers of the islands, the coordinates of the tile under the mouse. |
| Check | The check list: what keeps the scenario from being saved (the world must hold together, its description and markers be right), and what the original cannot have (its map size, more islands, cities, ships, land units, towers or players with a start than it holds, larger islands, more ore deposits, content of a package, a script). The second group is a fault only for a scenario that is to keep to the original's limits. |
| Leave the editor | Back to the main menu. |

New map, Open and Leave the editor ask before changes that are not saved are lost.

A left click on the map selects what is there: a ship, a land unit, a building, an island or the
sea. The panel then shows its properties (an island its position, size, climate, ore deposits and
cities; a building its island, position, city and owner; a ship its owner, position, cannons and
cargo), and a cross on the map marks it. Back to the map or Escape returns to the map's page.

A selected island has four more buttons:

| Button | What it does |
|---|---|
| Move | Takes the island in hand as a new one: a left click puts it down elsewhere. An island with cities moves with everything on it, and the ships in its rectangle (at its coast) go with it. Another ship where it would lie keeps it from that place (a red outline). |
| Turn | Turns the island a quarter clockwise about its centre, with its deposits, volcano, deer and growing trees. |
| Copy | A copy in hand, with a number of its own: a left click puts it down. Nobody knows the copy yet. |
| Delete | Takes the island out; the islands after it get the numbers one lower. |

Turn, Copy and Delete need an island without a city, buildings or units of a game (they would have
to go with it, which is for later), and none of these works in a running game opened as a map
(a savegame), whose figures stand on the islands. Every change of an island is one step that Undo
takes back.

## Keys

| Key | What it does |
|---|---|
| Ctrl+Z | Undo |
| Ctrl+Y, Ctrl+Shift+Z | Redo |
| Ctrl+S | Save |
| Escape | Back from a page of the side panel |

The view's keys are those of the game (WASD and the arrows, Q and E, Page Up and Page Down, F12 for a
screenshot, F11 for the full screen). F1 shows the keys the editor knows. All of them can be changed
in `keys.cfg`; the editor's are in the context `editor`.

## Files

The editor always saves Open1602's own format. It never writes a file of the original: a scenario of
the game that was opened is saved as an own scenario. The game's installation is never written to.
