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
while it is edited: nothing moves, nothing is built, no time passes.

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
| Leave the editor | Back to the main menu. |

New map, Open and Leave the editor ask before changes that are not saved are lost.

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
