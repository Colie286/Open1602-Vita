# Buildings of a package's own

A content package may add buildings the original's `haeuser.cod` does not have
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 2, item 2 of
[open-work.md](open-work.md)). Each one copies a building of the original and changes what it
needs: its own id, its own values, its own place in the build menu.

**Status:** Done are the file `own_buildings.toml`, the values of such a building
([values.md](values.md)), its place in the build menu, its id in savegames, its class and unlock
level, pictures of the package's own ([graphics.md](graphics.md)) and its carriers or workers, also
figures of a package's own ([own-figures.md](own-figures.md)). What such a building makes and uses
may be a good of a package ([goods.md](goods.md)). A later package may
[replace](#replacing-the-building-of-another-package) such a building.

## The file

```
<user data folder>/mods/<package>/own_buildings.toml
```

Each table declares one building. The table's name is the building's name in the package; together
with the package's id it is the building's **string id**, `<package>:<name>`, and that is what
everything else names it by. A name holds lower case letters, digits and `_`.

```toml
# mods/lights/own_buildings.toml
[lighthouse]
like = "anno1602:watchtower"   # the building of the original it copies (required)
gfx = 2100                     # its first sprite in STADTFLD.BSH, one per tile (or "lights:<image>")
size_x = 2                     # its footprint in tiles
size_y = 2
```

| Field | Limits | Meaning |
|---|---|---|
| `like` | a building of the original, `anno1602:<name or number>` ([names](values.md#names)) | every value of that building, as the copy's starting point |
| `gfx` | 0 to 65534, or an image of a package, `<package>:<name>` ([graphics.md](graphics.md)) | the first sprite; the building uses `size_x * size_y` sprites from it, per direction and frame |
| `size_x`, `size_y` | 1 to 255 | the footprint in tiles |
| `level` | an unlock level, `anno1602:<name or number>` or `<package>:<name>` ([levels.md](levels.md)), `""` for always | what the player must have reached to build it |
| `class` | a class of inhabitants, `anno1602:<name or number>` or `<package>:<name>` ([classes.md](classes.md)), for a house only | whose house it is |
| `figure` | one figure: `anno1602:<name or number>` ([names](values.md#names)) or `<package>:<name>` ([own-figures.md](own-figures.md)) | its carriers or workers (`Figurnr`) |
| `ware`, `raw`, `raw2` | a good by string id: `anno1602:<name or number>` or `<package>:<name>` ([goods.md](goods.md)), `""` for none | what it makes, and the first and second good it uses |
| `build_sound` | a sound of a package, `<package>:<name>` ([sounds.md](sounds.md#sounds-of-buildings-and-figures)) | the sound when it is built |
| `service` | `true` or `false` | a public building of a kind of its own: the houses in its reach get its own service instead of that of the building it copies, which classes may need (`public` in [classes.md](classes.md#public-buildings)) and the computers may build ([computer-players.md](computer-players.md#the-slots)); at most two in a game, in the order of the packages, a third one stays of the kind it copies. It needs a `gfx` of its own: the game tells buildings apart by their sprites |

A field that is not given keeps what `like` has. A table without `like`, with a field that is no
field of a building or with a value outside its limits is passed over with a warning that names the
file and the line; so is a name a package used already. A package declares at most 256 buildings.

Packages are read in their [load order](packages.md#load-order). The building's **values**
(production, costs, reach, hit points) are changed as the original's are, in
[`buildings.toml`](values.md), by the package itself or by a later one:

```toml
# mods/harder/buildings.toml
["lights:lighthouse"]
health = 50
cost_bricks = 12
```

Its place in the build menu comes from [`build_menu.toml`](build-menu.md), where `building` names
it by its string id:

```toml
[tower]
building = "lights:lighthouse"
category = 31007              # the harbour buildings
icon = 90
name = "Leuchtturm"
```

## Replacing the building of another package

A table named with the string id of a building an earlier package added, `["<package>:<name>"]`,
replaces that building: it takes the earlier one's place with the same string id, `Id` and record
number, so the build menu, the scripts, `buildings.toml` and savegames name it as before, and only
what it is changes. It starts from the building it replaces, or from `like` when given, and takes
the same fields as a new building:

```toml
# mods/better_lights/own_buildings.toml, loaded after "lights"
["lights:lighthouse"]
gfx = "better_lights:lighthouse"   # a new picture, everything else as "lights" made it
size_x = 3
size_y = 3
```

Only a building of a package loaded before is replaced; a package does not replace its own
buildings, nor the original's, whose values [`buildings.toml`](values.md) changes. A table that names
none of them is passed over with a warning. Replacements do not count towards the 256 buildings a
package declares.

## Ids and savegames

The engine gives every building of a package an `Id` of the building table, the highest free one
downwards from 23500, in the packages' load order and in the order of each file. The same packages
therefore give the same buildings the same Ids on every computer, which is what a multiplayer game
needs, and what a savegame needs to name the buildings on the islands.

A game keeps the string ids with the numbers it gave them (section `OWNB` of
[o1602-save.md](../formats/o1602-save.md)). A savegame loads only when the computer's packages give
those buildings the same numbers again, so a game with such buildings needs its packages
([packages.md](packages.md#which-packages-a-game-uses)); without them it is refused instead of
showing other buildings. Savegames of the original (`.gam`) have none of them.

## In the code

`engine/assets/include/o1602/assets/own_buildings.hpp`: `OwnBuilding` (string id and number),
`apply_own_buildings_file`; `BuildingTable::add` appends a record and gives it its Id and record
number, `BuildingTable::replace` puts a replacement in its place. `sim::World::own_buildings()` carries them through a game and its savegame.
