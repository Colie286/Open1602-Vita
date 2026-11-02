# 0010 – Controls and interface

Status: accepted · 2026-10-18 (key bindings confirmed as the default by the project owner)

## Context
The interface of the original has been rebuilt: side panel, build menu, the selection windows. Its
controls date from 1998: the left button gives commands, the right one cancels
(report on selection, to be moved into); many functions have no key, some keys no
function; sinking ships and declaring war happen without confirmation; some things the simulation could do
are not offered by the interface (exploring at the shipyard and the pirate warehouse, U-013).

The project owner wants to rework the interface so that new menus, settings and
information fit in and the controls follow today's real-time strategy games (2026-10-18).

## Decision
1. **The look stays, the controls become modern.** The graphics and layouts of the original are the
   basis; what the original hides or does not offer, the Open1602 interface may offer
   (example: exploring at the pirate warehouse and at the shipyard as a button, U-013). New windows and buttons
   are added as data (ADR 0007).
2. **Right-click gives commands** (default): move, attack, dock, trade depending on the target. A ship
   attacks another player's buildings, as in the original's military mode; their Kontor only when it may
   not trade there (a trade treaty; the pirates keep theirs at war), else it sails there to trade, and F
   first attacks it. The
   left-click controls of the original are a setting. The left button selects (click, box,
   Shift adds); Escape cancels and goes back one page. Dragging with the right or
   middle button still scrolls the map.
3. **Commands via the minimap:** a right-click on the minimap sends the selection there.
4. **Command chains:** Shift + right-click appends a command to the unit's queue
   (waypoints). The queue belongs to the simulation (command with "append", deterministic,
   saved); a trade route stays separate from it.
5. **Freely assignable keys** in the settings menu. The default follows today's strategy games
   (below); keys may be bound twice in different contexts (building, units) if
   these never apply at the same time. The original's key bindings are available as a second preset.
6. **Confirmations** before consequences that cannot be undone: sinking a ship, disbanding a unit
   and declaring war through an attack. The confirmation can be switched off in the settings.
7. **Videos of messages play in the minimap window**, and the game keeps running; a click on the
   video ends it. Intro, credits, victory and defeat stay full-screen.

## Key bindings (default)

**Camera and game**

| Key | Function |
|---|---|
| W A S D, arrow keys | scroll the map (Shift: faster) |
| Q / E | rotate view |
| Mouse wheel, Page Up / Page Down | zoom (free zoom: four notches or two key presses are one level) |
| Ctrl + / Ctrl − | the map twice / half as large |
| F11, Alt+Enter | window or full screen |
| Space | jump to the last message |
| Home | centre view on the selection |
| N | jump to the next own city (History Edition) |
| F3 | pause |
| Pause | pause with the original's pause video (U-018) |
| + / − (QWERTY: = / −), Num+ / Num− | game speed |
| F5 / F9 | quicksave / quickload |
| F10, Escape (when there is nothing to cancel) | game menu |
| F12, Print Screen | screenshot |
| F1 | help for the window under the mouse |
| Ctrl+F / Ctrl+K | frame rate / coordinates of the tile under the mouse in the map's corner (History Edition) |
| Ctrl+B | fertility view: the tiles of the island tinted green where the selected farm's crop bears and red where not (Open1602; with a field, plantation or farm in the build tool, for its crop; never on by itself) |
| M | unit identification: the coat of arms on a pole over each own land unit, on / off (History Edition; none over ships or other players' units) |
| Double-click (or a second click) on an own building with a catchment | the circles of all own buildings of its type on the island lit together, also while building (Open1602; one click: its own circle, as in the original) |

**Windows**

| Key | Function |
|---|---|
| B | build menu |
| K | military mode |
| I | status/info |
| O | options |
| L / Shift+L | ship list / city list |
| V | diplomacy (treaties; German „Verträge“) |
| F2, Tab | goods balance „Warenbilanz“ (Open1602, ADR 0012); with units selected Tab narrows the selection instead |

**Building**

| Key | Function |
|---|---|
| R | rotate building |
| Shift+R | rotate building the other way (History Edition) |
| hold Shift | keep building after placing |
| C | pipette: the building under the mouse as the building order (a grown house as the first house) |
| Delete | demolish tool |
| Ctrl+C | copy tool (Open1602): drag over a piece of the own city, or click a building, and it hangs at the mouse to be built again elsewhere; R turns it |
| Ctrl+S | save the copy tool's piece in hand as a template under a name (Open1602); without one, open the list of templates |
| U | blueprint mode on or off (Open1602): what the build and copy tools place becomes the player's blueprints, built later |
| Shift+U | build the player's blueprints of the city under the mouse |
| Escape | put the tool away (the copy tool: first the piece in hand) |
| Right-click | put the tool away and return to the player status (the copy and move tools: only what is in hand) |

The build menu's demolish button opens a pop-up with the tools, like the other categories: demolish
and the copy tool (Open1602's icons). A copied piece is built as one order: roads and walls first,
then the buildings, then the fields, so what the means do not reach is the end of the order. Houses
come as the first house; fields and trees only with a building of the piece that harvests them.

The pop-up's third entry switches blueprint mode (as later Anno games): placed buildings and copied
pieces become blueprints, free of cost, only in the player's own cities and never a Kontor. Only
their owner sees them, as blue outlines; the demolish tool removes them; anything built over one
takes its place. A click on a blueprint opens its window: its costs and those of all blueprints of
its city, "Build", "Build all in this city", whether the city builds them by itself (every 5 s of
game time, as far as it can pay, in the order they were placed) and "Remove". Every change is a
command, so all computers of a multiplayer game keep the same blueprints; they are saved with the
game.

The fourth entry is the move tool (as later Anno games; the game setting "Moving buildings"): a
click picks up one of the player's buildings, which then hangs at the mouse by its middle, turns
with the building keys and goes where the next click puts it, within its city and also over its
own old place. It keeps its level, inhabitants, stock and production; free by default, or paid as
a new building with the demolish refund for the old place. Kontors, markets, towers, castles, a
shipyard at work and burning or plague-struck buildings stay; the status line says why. A right
click puts the building in hand back.

The fifth entry, in a row above the tools, opens the templates (as later Anno games): pieces the
copy tool picked up, kept under a name with Ctrl+S to be built again in any game. Their window by
the side panel lists each with a small view from above, its size, number of buildings and costs;
"Take" puts it into the copy tool's hand, "Rename" types a new name in its row, "Delete" asks once
more. Each template is a small text file in the folder "templates" of the user's data folder, so
players can pass them on; they are never part of a savegame or a multiplayer game. Buildings the
game does not know (a mod not loaded) are left out when a template is taken.

**Settings dialogs** (start dialog of a game, window „Anzeige“)

| Key | Function |
|---|---|
| Left-click on a value | the next value (after the last the first) |
| Right-click on a value | the previous value (before the first the last) |

**Selection**

| Key | Function |
|---|---|
| Left-click / drag | select / box |
| Shift + left-click | add to or remove from the selection |
| Double-click, Ctrl + left-click on a unit | all visible units of this type |
| Ctrl+1…0 | set group |
| Shift+1…0 | add to group |
| 1…0 | select group (twice: jump there) |
| . (full stop) | next idle trading ship |
| , (comma) | next idle combat unit |
| Tab | cycle through the types in the selection (ships, infantry, cavalry …) |
| Alt+1 / Alt+2 / Alt+3 / Alt+4 | all own swordsmen / musketeers / cavalry / cannons in view (History Edition) |

**Unit commands**

| Key | Function |
|---|---|
| Right-click (also on the minimap) | command depending on the target |
| Shift + right-click | append command |
| F + click | attack-move (attacks on the way; German „Feuer“, "fire"); on a tree or field: shoot it down; on another player's road, bridge or pier: attack it |
| Ctrl + right-click | on a tree or field: shoot it down; on another player's road, bridge or pier: attack it (original: Ctrl + click in military mode) |
| Ctrl + left-click | with own units selected: like right-click, a tree or field included (original: Ctrl + click); e.g. wounded soldiers onto an own doctor, who heals them |
| H | stop (German „Halt“) |
| G | hold position (does not pursue) |
| P + click | patrol |
| C | change formation (line, column, block) |
| Shift+C | spread out |
| Y | raise / lower the white flag |
| X | explore island |
| T | trade / transfer at the warehouse |
| R | edit trade route (ship selected) |
| Delete | disband unit / sink ship (with confirmation) |
| Shift + click on the castle's send-out arrow | all trained units leave and line up in a block around the spot clicked next; units in training stay |
| Shift + click on a ship's cannons in a hold | as many cannons mounted as there are free places and cannons on board |
| Shift + click on a ship's mounted cannons or the button that takes one off | as many cannons back into the holds as they have room for (a hold with cannons first, then an empty one) |
| Shift + click on a city window's trade cart button | another trade cart, up to one per Kontor and market of the player on the island (setting „Trade carts per island“); without Shift the button jumps from cart to cart, the nearest first |

Attack-move, hold position, jumping to idle units and sending a castle's units out all at once do not
exist in the original. The clicks with Shift in windows (send all units out, mount as many cannons as
fit, unmount them all, another trade cart) are listed with the unit keys in the F1 overview and in the
windows' help lines.

No unit key is also a general key (2026-10-18: attack moved from A to F, stop from
S to H, and in exchange hold position on G, formation on C, spread out on Shift+C), so W A S D always
scroll. Only keys in contexts that never apply at the same time are bound twice: R and Delete (building:
rotate, demolish; units: route, disband), C (building: pipette; units: formation) and Tab (general: goods
balance; units: the next type in the selection). Tab no longer steps through all buildings while
building (2026-10-31): that list ignored what the player had unlocked yet.

Keys that matter by their place are bound by scancode (2026-10-19, review point 25): W A S D, Q and E
and the digits of the groups lie where they are on a US keyboard on every layout (AZERTY: Z Q S D, A
and E; the number row without Shift). Keys that stand for a word stay bound by keycode, where the
layout prints them (B building, K military, F attack). In `keys.cfg` a key by its place is written in
brackets, `general scroll_up = [W]`; where a press matches both kinds, the one by place wins.

Done (2026-10-18): table `ui::KeyBindings` with this default and the settings file
`keys.cfg` in the user's data folder (to be edited by hand until the settings menu exists; F1 shows the
bindings in pages with tabs like the History Edition's shortcut list: map and game, windows, building,
units and ships, view; Tab and the arrows or a click on a tab turn the page, `app::key_help_pages`), box selection, pixel-exact like the original, groups with the original's number key,
formations, filtering and Tab (types of the selection one after another; `ui::UnitSelection`), right-click and
minimap commands, command chains in the simulation, the confirmations, the military mode windows, the
message videos in the minimap window, attack-move (ships and soldiers attack enemies in
range, pursue them and then continue; in the savegame `STNC`), hold position (they
only shoot at enemies in range, do not pursue), quicksave and quickload (F5, F9) and the
explore button next to repair and pirates in the ship window (U-013), ship and city list (L,
Shift+L), diplomacy window with tribute (V; confirmation before breaking a peace treaty; with more
than the three partners VERTRAG.GAD has rows for, a strip of their coats of arms in the frames of
PIRATORD.GAD heads the window and the chosen partner's treaties, hand and tribute follow on the same
page; offers not answered yet stay in the status bar as the treaty message icon in the partner's
colour until they are, and a click there opens the window at that partner), the
settings of the side panel and the main menu for single player (scenarios, endless game, savegames),
the end screen after victory or defeat as in the original (ENDE.GAD; after a victory Escape goes
on with the game, which the original ends, with a hint over the buttons; with more players than its
four coats of arms, or players in further colours, these become a strip of all players in the order
of their points, with the rank under each, the scroll arrows of TRANSFER.GAD on both sides and the
shown player's rank under the name; Left and Right go through the players). Buildings are picked by
their pictures as the units are (2026-10-22): a click on any visible pixel of a building selects it,
also where the picture reaches over the tiles behind it, and the front-most picture wins, a building
in front of a unit included (`render::tiles_at_pixel`). Pictures that cannot be selected (ground,
roads, trees, fields) do not hide a building behind them. The pixels come from the sprites' masks,
so buildings of mods are covered without data of their own. Not yet: our own game menu (Escape) with save, load, settings and quit; the original's key
bindings as a second preset; F1 as help for the window under the mouse (today F1 shows the keys). The original
has no game menu: save, load and quit are in the settings of the side panel (done
with a confirmation before „Ende“ ("Quit")).

## Display: window size, full screen, interface and free zoom (2026-10-22)

The original knows 640 x 480, 800 x 600 and 1024 x 768 and three zoom levels. Open1602:

- **Any window size.** The window can be dragged to any size; the settings offer common sizes that
  fit the screen. The size is remembered in `settings.cfg` (`display.window`), `--size` wins over it.
- **Full screen at the desktop's resolution** (F11, Alt+Enter, or the settings): no change of the
  screen's mode, so switching is quick and works the same on Windows, macOS, X11 and Wayland, and
  HiDPI screens get their full resolution. The original's three resolution buttons in the settings
  of the side panel became "Fenster", "Vollbild" and "Anzeige …", the window with the rest.
- **The interface's size**: automatic (as large as the 1024 x 768 design fits, as before), or a
  percentage of the desktop's scale (100 %: the original's pixels on a normal screen, twice that on
  a HiDPI one), never larger than fits. A smaller interface leaves more of the window to the map;
  the side panel's parchment then goes on to the bottom. Full-screen menus (main menu, multiplayer,
  end screen) always fill the window as far as they fit.
- **Free zoom** (default; the original's three levels are a setting): the magnification is screen
  pixels per sprite pixel of the large level, 0.2 to 8. The camera shows it with the most detailed
  level whose sprites need not shrink below 0.75 (`render::zoom_for_magnification`) and scales them
  by the rest. A whole scale draws directly. Any other scale draws the sprites at the next whole
  scale into a texture, which is shrunk onto the screen with linear filtering
  (`render::SdlMapCanvas`): every sprite pixel keeps hard edges and only the screen pixels on its
  border mix two colours, so the art stays sharp without the uneven pixels of plain nearest
  scaling, and nothing shimmers while scrolling. The texture is at most twice the window's pixels
  (else one whole scale less, stretched a little and so a little softer; just above a whole scale
  that is the case), so that weak GPUs stay fluent; without a GPU (SDL's software renderer) at most
  the window's pixels. `O1602_MAP_CANVAS=N` allows N times the window's pixels (0: no limit) to try
  other limits. "einfache Pixel" draws straight at the scale instead. The wheel zooms by a quarter level per notch (touchpads finer), the
  zoom keys and buttons by half a level, Ctrl +/− by a whole one; the zoom glides there in about
  0.15 s and keeps the point under the mouse in place. Picking by the sprites' pixels works at any
  scale.

## Open1602's own windows (2026-10-31)

Every window that has no layout in the original game's files is a `ui::Form` in the shared
`ui::FormStyle` (gold frame, dark ground), so that settings, multiplayer, chat and questions in the
game look and answer the same: title in the heading tone, buttons 44 design units high, the default
button highlighted, Enter and Escape for the default and cancel. The rules for new windows are in
[CONTRIBUTING.md](../../CONTRIBUTING.md#user-interface).

## Consequences
- Input runs through an action → key table (settings file), not through fixed keys in the
  code. The game's default keys are entries of that table (done, `keys.cfg`).
- The simulation needs a command queue per ship and unit as well as group commands with
  an offset per unit.
- Deviations from the original in the controls are not bugs in the sense of ADR 0006, but
  interface settings; the simulation is not affected by them.
