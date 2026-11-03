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
| Edit islands | New islands: one of the original's templates or one of its own shape, north or south, in the five sizes of the templates (30 × 30 up to 100 × 90); Another one picks another template or shape. Place puts it in hand: its outline follows the mouse, green where it fits on open sea and red where not; a left click puts it down, a right click or Cancel puts it back. Load island lists the islands saved with Save (below) and the original's island files (`.scp`) put into the folder `islands` in Open1602's user data folder, and puts the one chosen in hand the same way; an `.scp` file, which does not tell its climate, gets the climate chosen on the page. |
| Placeholders | A scenario of the original that makes its islands when a game starts has a world plan: each of its islands is a placeholder, drawn as a yellow outline of its size class with its name. The shape Placeholder on Edit islands sets a new one (north or south, a size class, its template drawn when the game starts); a click on a placeholder selects it (below). Islands cannot be put on a placeholder. When a game of the scenario starts, the world keeps what the editor left (its islands, cities, players and ships) and only the placeholders become islands; what is beyond the original's limits does not start with it. |
| Edit terrain | The terrain tools (below). |
| Change map size | Makes the map larger or smaller in steps of 50 tiles, or fits it to its content with some sea around. It grows or is cut at the right and the bottom, so everything keeps its place, and it cannot be cut below the islands, ships, land units and markers. |
| Overlays | What the map shows over the world: the players' land in their colours, the numbers of the islands, the coordinates of the tile under the mouse. |
| Check | The check list: what keeps the scenario from being saved (the world must hold together, its description and markers be right), and what the original cannot have (its map size, more islands, cities, ships, land units, towers or players with a start than it holds, larger islands, more ore deposits, content of a package, a script). The second group is a fault only for a scenario that is to keep to the original's limits. |
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
| Crops | Which of the six crops (tobacco, spices, sugar cane, cotton, wine, cocoa) grow fully on the island. Unticked, the crops of its climate grow half (tobacco, sugar cane and wine in the north, spices, cotton and cocoa in the south), the others not at all. Each tick is a step of undo. |
| Save | Saves the island alone into the folder `islands` (Island 1, Island 2, ...), with its tiles, deposits and fertility, for Load island. |

A selected placeholder has Move and Delete, and its climate (north, south, or by its place on the
map, as the original's plans have it) and size class as two buttons that go through their values.
Each change is a step of undo.

Turn, Copy, Delete and Save need an island without a city, buildings or units of a game (they would have
to go with it, which is for later), and none of these works in a running game opened as a map
(a savegame), whose figures stand on the islands. Every change of an island is one step that Undo
takes back.

## Terrain

**Edit terrain** paints the islands' terrain from a palette of the game's own: ground, forest, rock
and ore mountains, slopes, rivers, beach, surf and sea. **Kind** picks a group of the palette,
**Piece** goes through its pieces (a right click goes back), **Turn** turns a piece that can turn and
**Size** sets the side of the square brush. A white dotted square on the map shows where the tool
lays, and the brush shows its piece there before the click. The tools:

| Tool | What it does |
|---|---|
| Brush | Paints the piece where the left button is pressed and where the mouse goes with it held. A piece of several tiles (a mountain, a river bend) is laid about the mouse. |
| Land | Paints land into the water and lays the coast about it as the game's islands have it: beach and its corners on the land's edge, surf before it, then light, middle and dark sea outwards. Land painted at an island's edge makes the island grow by open sea, and land painted on the open sea far from islands makes a new island there; either is one step of undo with the stroke. |
| Water | Paints water into the land, with the surf and the beach about it. |
| Random fill | Paints pieces of the kind, each tile its own drawn at random and turned at random: ground as the game's islands mix it, forest of the island's climate. |
| Fill | Paints every tile that is joined to the one clicked by its sides and looks like it. |
| Replace | Paints every tile of the island that looks like the one clicked; tiles that turn keep their turn. |
| Pipette | Takes the piece of the tile clicked, with its turn, and goes back to the brush. |

Water, beach and land are three levels of the map's picture, and a piece fits only on its own level:
the brush leaves the coast as it is, and Land and Water change it. Where a coast would take a form the
game has no tiles for (a strip one tile wide, land touching land only at a corner), Land and Water
widen it. Land keeps a tile of water to its island's edge; an island with a city does not grow.

A tile gets the piece on top and as the ground below. A mountain or river bend that a piece covers in
part becomes meadow where it is not covered, and its ore deposit or volcano goes with it. The terrain
tools paint only islands, not open sea, and leave buildings, the land of cities and the tiles under a
ship or a land unit as they are. Each stroke, fill or replacement is one step of undo.

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
