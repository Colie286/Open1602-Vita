# Islands of a package

A package can bring island templates of its own. New worlds of their own draw them alongside the
original's templates of the same climate and size class
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 5, item 9 of
[open-work.md](open-work.md)).

**Status:** Done are the file `islands.toml`, islands in the original's island format (`.scp`),
drawing them in new own worlds with the original's templates (island shapes "Templates" or
"Mixed"), `o1602-tool export-island` to make such a file from an island of its own shape, and
`o1602-tool new-world --mods`. Still open: native villages on a package's islands, islands for the
original's scenarios with planned islands, islands larger than 100 x 90 tiles, and an island editor
(the editor's island phase).

## The file

```
<user data folder>/mods/<package>/islands.toml
<user data folder>/mods/<package>/islands/lagoon.scp
```

Each table names one island. Together with the package's id, the table's name is the island's id,
`<package>:<name>`. A name holds lower-case letters, digits and `_`.

```toml
# mods/atoll/islands.toml
[lagoon]
file = "islands/lagoon.scp"   # an island file of the package (required)
climate = "south"             # "north", "south" or a package's climate; the file's own if not given
name.en = "Lagoon"
name.de = "Lagune"
```

| Field | Limits | Meaning |
|---|---|---|
| `file` | a path in the package, folders split by `/` | the island: a file of the original's island format (chunks `INSEL5` and `INSELHAUS`) with all its tiles (required) |
| `climate` | `"north"`, `"south"` or a package's climate `"<package>:<name>"` ([climates.md](climates.md#how-a-world-uses-them)) | the climate whose islands it stands among; the file's (`INSEL5` +0x64) if not given, and for a package's climate, whose islands drawn from it take that climate |
| `name.<language>` | a text | its name, for lists such as the editor's; the id if there is none |

The file's name may end in anything; the game reads its chunks. The example package names its
island `lagoon.island`, since the repository keeps no `.scp` files, the original's ending.

The island's size decides its size class: the smallest class whose box it fits. The boxes are
30 x 30 (lit), 40 x 40 (mit), 50 x 52 (med), 70 x 60 (big) and 100 x 90 (lar). A larger island,
a file that holds no island, a path outside the package or a field that is wrong is passed over
with a warning that names the file and the line. The packages have at most 1024 islands in all.

## How a world uses them

A new world of its own (the start dialog's "Own world", `o1602-tool new-world`) draws a template
for each island of its plan from those of the island's climate and size class. A package's islands
are among them and are numbered after the original's templates, in the packages' load order. The
plan's wishes hold for them too: a home island takes one without a volcano and with ore mountains,
and no two islands of a world take the same template. Ores, crops and treasures are given as on the
original's templates. With the island shapes "Mixed", any island may still get a shape of its own
instead.

A world keeps all tiles of a package's island, as it keeps an island of its own shape. A
savegame or a multiplayer game therefore needs no package for its islands; the package only adds
them to new worlds. A package's island gets no native village, since a package has no native
version of it.

## Making an island

`o1602-tool export-island` writes an island of its own shape, with all its tiles, as such a file:

```
o1602-tool new-world islands.gam --island-shapes own --villages none --island-min 30 --island-max 60
o1602-tool islands islands.gam                      # sizes and positions of the islands
o1602-tool export-island islands.gam 0 lagoon.scp   # island 0
```

An island that keeps only its changes to one of the original's templates (every island of a
world from templates) is refused: its ground lies in the original's files. The example package
`data/mods/example` has one such island ([README.md](README.md)). `o1602-tool new-world --mods
<mods folder>` makes a world that draws the islands of the packages in that folder, and
`o1602-tool check-mod` reads `islands.toml` with the other files.

## In the code

`engine/sim/include/o1602/sim/package_islands.hpp`: `PackageIsland`, `apply_islands_file`,
`load_package_islands` and the template numbers after the original's (`package_template_number`,
`package_template`). `sim::island_templates` lists them with the original's templates for the
planner. `WorldStart::package_islands` gives them to the generator, which stores their tiles in the
world (`INSEL5` +0x65).
