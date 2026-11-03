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
| New map | A map of one of the sizes offered (500 × 350 as the original, up to 4000 × 4000): only sea, or with islands, players and their ships as a new endless game makes them. Kind of islands chooses them as the endless game's setup does: the original's templates (with the packages' island templates), one of Open1602's kinds of islands of their own shape, or a package's (`landscapes.toml`); islands of their own shape may make the map larger where the largest would find no room. A world number gives the same world again. |
| Open | The own scenarios, then the scenarios of the game. |
| Save | Saves the map as an Open1602 scenario (`.o1602`). A map that has no such file yet asks for a name first. |
| Save as | Saves under a new name in the folder of own scenarios (`scenarios` in Open1602's user data folder), which the scenario list of the main menu shows. A name an own scenario has already replaces it. |
| Undo, Redo | Takes the last change back, or does it again. |
| Edit islands | New islands: one of the original's templates (or a package's, from `islands.toml`) or one of its own shape, north, south or of a package's climate (`climates.toml`; a placeholder only north or south), in the five sizes of the templates (30 × 30 up to 100 × 90); Another one picks the next template, the panel saying which (its number, or a package's island by name), or another shape. Place puts it in hand: its outline follows the mouse, green where it fits on open sea and red where not; a left click puts it down, a right click or Cancel puts it back. Load island lists the islands saved with Save (below) and the original's island files (`.scp`) put into the folder `islands` in Open1602's user data folder, and puts the one chosen in hand the same way; an `.scp` file, which does not tell its climate, gets the climate chosen on the page. |
| Placeholders | A scenario of the original that makes its islands when a game starts has a world plan: each of its islands is a placeholder, drawn as a yellow outline of its size class with its name. The shape Placeholder on Edit islands sets a new one (north or south, a size class, its template drawn when the game starts); a click on a placeholder selects it (below). Islands cannot be put on a placeholder. When a game of the scenario starts, the world keeps what the editor left (its islands, cities, players and ships) and only the placeholders become islands; what is beyond the original's limits does not start with it. |
| Edit terrain | The terrain tools (below). |
| Place objects | The objects' tools (below). |
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

**Players** lists the slots of the humans and computers, up to twelve (the free trader, the pirates
and the natives keep theirs and are not listed). Each change is one step of undo.

| Setting | What it does |
|---|---|
| Player | The slot shown, by name. |
| Kind | Human or computer. |
| Colour | The player's colour; one another player has goes to it in exchange. |
| Starting money | The gold the player starts with: 1000 a click, 10 000 above 50 000; the right button takes it off again. |
| Monuments, Arches | How many monuments and triumphal arches the player may build (0 to 9). |
| Name | The player's name; left empty, the game calls the player after its colour. |
| Add, Remove | A computer player in the next slot (the fifth and those after it go behind the trader, the pirates and the natives), with the money of the first computer; the last slot taken away again, as long as it owns no city, ship or soldier. The first four slots stay. |

A player without a city and a ship is out of the game (defeated) as soon as it starts: give a new player a ship or a Kontor on the
page **Place objects**.

**Diplomacy** sets how the player shown stands with each other player when the game starts: at
peace or at war, with a trade treaty or without. A change applies to both sides, and clears what
the two remember of each other (tribute, trade, damage). A trade treaty needs peace; war ends it.
Adding a player keeps the relations of the others; the new one starts at peace with everyone.

## Rules

**Rules** sets the game settings the scenario is made for, the same ones the start dialog offers
for a scenario: disasters, construction, ships and trade, limits, opponents, and the bugs of the
original. **The player's rules** leaves them to the player, as for any scenario; a click switches
to **The map's own rules**, which begin from those the player last started a scenario with. **As
original** sets every one as in the original, **Open1602** as Open1602 has them. Each setting goes
to its next value with the left button and back with the right one; one that does not matter with
the others as they are is greyed out. Each change is one step of undo.

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
