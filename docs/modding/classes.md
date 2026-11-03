# Classes of inhabitants

A content package may change the original's five classes of inhabitants (pioneers to aristocrats,
`BGRUPPE` of `haeuser.cod`) and add classes after them
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 2, item 3 of
[open-work.md](open-work.md)). A class's needs may name goods a package added
([goods.md](goods.md)), and a house a package added may belong to a new class
([own-buildings.md](own-buildings.md)).

**Status:** Done are the file `classes.toml`, houses of a new class, the house window, the goods
window, the savegame, the public buildings a class needs (`public`, 2026-11-01) and the page
"Population" of the goods window, which has a row with inhabitants, satisfaction and tax and a line
of its own colour in the chart for each of up to eight classes; levels a new class unlocks are in
[levels.md](levels.md).
The computer players build the producers of the packages' goods a class needs
([computer-players.md](computer-players.md)).

## The file

```
<user data folder>/mods/<package>/classes.toml
```

A table names the class it changes, or declares a new one:

- `["anno1602:<name or number>"]` changes a class of the original: `pioneers` 0, `settlers` 1,
  `citizens` 2, `merchants` 3, `aristocrats` 4.
- `["<package>:<name>"]` changes a class a package added, this one or one loaded before it.
- `[<name>]` adds the class `<package>:<name>` after the last. It starts as a copy of the last class
  (or of `like`) and takes the fields given. A name holds lower case letters, digits and `_`.

```toml
# mods/spice/classes.toml
["anno1602:merchants"]
need."spice:pepper" = 400     # merchants want pepper too
need."anno1602:alcohol" = 0   # and no longer alcohol

[governors]
like = "anno1602:aristocrats"
max_per_house = 60
tax = 300
need."spice:pepper" = 600
name.en = "Governors"
name.de = "Statthalter"
```

| Field | Limits | Meaning |
|---|---|---|
| `max_per_house` | 1 to 255 | inhabitants a house of the class holds (`Maxwohn`) |
| `tax` | 0 to 10000 | what an inhabitant pays, times 100 (`Steuer`: 1.4 is 140) |
| `share` | 0 to 100 | the supply of its goods, in percent, it asks for at the normal tax (`Prozent`) |
| `hiscore` | 0 to 10000 | points per inhabitant for the score (`Hiscore`) |
| `public` | a text, as `"market, chapel \| church"` | the public buildings a house of the class needs in its reach ([below](#public-buildings)); without it those of the original's class it copies |
| `need."<good>"` | 0 to 100000 | kg of the good per 100 inhabitants and minute (`Ware`: 0.6 t is 600); 0 takes the need away |
| `name.<language>` | a text | the name in that language; a new class needs one |
| `like` | a class, `anno1602:<name or number>` or `<package>:<name>` | for a new class: the class it copies |

A good is named by its string id, `anno1602:<name or number>` ([goods.md](goods.md#the-file)) or
`<package>:<name>`, quoted because of the colon. Food is no need of this file: every class eats it, as
`haeuser.cod`'s `Nahrung` says. A table that names no class, with a field that is no field of a class
or with a value outside its limits is passed over with a warning that names the file and the line.
A game has at most eight classes: the goods statistics keep eight.

## Houses of a new class

A class is reached when a house of the class below grows beyond its room and the city has what the
next class needs: the house is rebuilt as the first house of that class. A new class needs a house
of a package that belongs to it:

```toml
# mods/spice/own_buildings.toml
[palace]
like = "anno1602:20651"       # the first house of the aristocrats
class = "spice:governors"
gfx = 2200
```

Without such a house nobody rises into the class. A new class shows the faces of the class it
copies in the house window, and has its needs, public buildings, values and name of its own.

## Public buildings

A house rises into a class only when the public buildings the class needs are in its reach, and
it is content only while they are. `public` names them: choices separated by commas, the buildings
of a choice by `|`, any one of which will do. `""` needs none.

```toml
["anno1602:settlers"]
public = "market, chapel | church, tavern"   # settlers also want a tavern

[governors]
like = "anno1602:aristocrats"
public = "market, church, college, theatre, doctor"
```

| Name | Building of the original | Also counts |
|---|---|---|
| `market` | marketplace | |
| `kontor` | Kontor (the pioneers take it for a market: `market \| kontor`) | |
| `chapel` | chapel | |
| `church` | church | cathedral |
| `tavern` | tavern | |
| `school` | school | |
| `college` | college | |
| `bathhouse` | bath house | |
| `theatre` | theatre | |
| `doctor` | doctor | |
| `gallows` | gallows | |

A building of a package counts as the building it copies (`like` in
[own-buildings.md](own-buildings.md)), unless it is of a kind of its own (`service = true`): then a
class names it by its id, as `"market, chapel, rome:court"`. At most eight choices. Without `public`, a class needs what
the original's needs: pioneers `market | kontor`; settlers `market, chapel | church`; citizens also
`tavern, school | college`; merchants `market, tavern, church, school | college, bathhouse`;
aristocrats `market, tavern, church, college, bathhouse, theatre`. A new class needs what the class
it copies needs. The house window shows a choice by its first building; the doctor and the gallows
have no place in it.

## Savegames

A game keeps its classes, the original's as the packages changed them and the new ones, with
their public buildings (sections `CLAS` and `CPUB` of [o1602-save.md](../formats/o1602-save.md)),
so a running game does not change when a package is changed or removed. A game with more than five classes cannot be saved as a savegame of
the original (`.gam`).

## In the code

`engine/assets/include/o1602/assets/resident_classes.hpp`: `apply_classes_file`, `class_of` and
`class_name`; `ResidentClass` (building_table.hpp) carries the id, the look and the names of a
package's class. `sim::World::resident_classes()` carries a game's classes; the game puts them into
its building table (`sim::apply_resident_classes`), which the simulation reads them from.
