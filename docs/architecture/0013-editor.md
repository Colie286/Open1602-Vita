# 0013 – Editor: a mode in the game, scenarios as `.o1602`, the original's limits as an option

Status: accepted · 2026-11-02

Open1602 needs an editor for maps, scenarios and savegames. Players want to build the worlds that
the engine allows: a map of up to 4000 x 4000 tiles, islands far larger than the original's, more
players, and buildings, goods and scripts of content packages. Others want to build scenarios that
keep to the original's limits, as the original's own scenarios do. The scenarios the original came
with, and those made for it, must open in the editor too.

## Decision

1. **A mode in the game.** The editor is started from the main menu and runs in the game's window
   with the game's renderer, interface building blocks (`ui::Form`, `FormStyle`) and data. It is not
   a program of its own. A scenario can be played from the editor and edited again afterwards. The
   world the editor shows has a simulation that is built but never runs (`sim::Simulation::ViewOnly`:
   nothing a load does for the game to run happens to the world), and the document edits that world
   in place (`editor::Document::edit`). The editor's panel takes the place of the game's modes below
   the head of the side panel (`app::EditorPanel`); the map takes no selection, tool or order of the
   game, and only the view's, the program's and the editor's keys count (key context `editor`).
2. **Always `.o1602`.** The editor saves only Open1602's own format ([ADR 0009](0009-own-save-format.md),
   [o1602-save.md](../formats/o1602-save.md)). It never writes a scenario of the original (`.szs`,
   `.szm`). Files of the original are opened and saved as `.o1602`.
3. **The original's limits as an option.** A scenario can say it keeps to the original's limits
   (`ScenarioInfo::original_limits`): the map of 500 x 350 tiles, at most 50 islands of at most 255
   tiles a side and two deposits each, 75 cities, 150 ships, 400 soldiers, 350 towers, four players
   with a start, the original's buildings, figures and goods only, no values of a package and no
   script (`sim/original_limits.hpp`, `editor::check_original_limits`). The editor then refuses what
   goes beyond them and the check names each problem. Without the option the editor follows
   Open1602's limits (`sim/limits.hpp`). The largest island is as large as the world, once a load
   test with such islands has passed.
4. **What makes a file a scenario** is stored in sections of its own, so that every older reader
   still loads the game:
   - `SCNI`, the description: names and briefings per language (UTF-8), stars, author, version and
     the option above. It comes right after `META`, so that the scenario list reads it from the head
     of the file (`save_info`). A file without it is an ordinary savegame.
   - `MARK`, named points and areas of the map that scripts find by their names.
   - `SLUA`, the scenario's own Lua script, embedded in the file. It is required, because a reader
     that cannot run it would play another game.
   - `EDIT`, what the editor keeps of its own work (layers shown, tools, views). The game does not
     read it, and the chunk of an exported `.GAM` leaves it out.
   - `PLAN`, the plan of a scenario of the original that makes its islands at the start (the
     endless games): each island's number, place, size class and climate, the crops, ores and
     treasures to hand out, and the table the original draws from. The editor opens such a file
     without making the islands (`WorldStart::keep_plan`), so the plan stays what the scenario is
     and can be changed. When a game of it starts, `roll_planned_islands` makes the islands from
     the scenario the world came from (`ORIG`) with the plan as it is now, as the original makes
     them, and keeps the scenario's description, markers and script.
5. **Editing steps with undo (`o1602::editor`).** A `Document` holds a `sim::World` and the history
   of the steps done to it. Each step is an `Operation` that applies itself to the world and can take
   itself back. It keeps what it changed, not a copy of the world, so a step on a map of 4000 x 4000
   tiles costs only the tiles it touched. The history keeps steps up to a memory limit (256 MiB) and
   forgets the oldest ones first. The newest step is always kept. A document is modified when its
   history leads to another state than the one saved last; undoing back to that state makes it
   unmodified again. Saving checks the world (`sim::validate`) and the scenario's own parts first and
   refuses, with nothing changed, what would not load again.
6. **Savegames can be edited.** A savegame opened in the editor and saved again is marked as edited.
   Such a game gets no entry in the high score lists.
7. **Steps.** The editor is built in this order: the library, the sections and
   `o1602-tool scenario` (`info`, `convert`, `check`) first, then keeping the world plan of a
   scenario, then the editor mode with the map, the islands, the players, the missions and the
   scripts. Each step is usable on its own.

## Consequences

- The editor shares the game's code for loading, drawing and checking a world, so what it shows is
  what the game plays.
- Scenarios made with Open1602 do not open in the original. Scenarios that keep to the original's
  limits can still be played with the original's rules in Open1602.
- `editor` is a module on `sim` and `assets` ([ADR 0005](0005-module-structure.md)). It has no
  platform code and is tested without a window.
- `SaveMeta::scenario` holds the head of `SCNI`. `save_info` unpacks 16 KiB of the sections instead
  of 4 KiB, so that a description with long names in many languages still fits.
