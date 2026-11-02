# Figures of a package's own

A content package may add figures the original's `figuren.cod` does not have
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 2, item 2 of
[open-work.md](open-work.md)). Each one copies a figure of the original and may draw an image of
the package: a monk who carries the goods of a monastery, a donkey for a mill.

**Status:** Done are the file `own_figures.toml`, the figure's own picture, its values
([values.md](values.md)), the buildings of a package that name it as their carriers or workers
([own-buildings.md](own-buildings.md)), its number in savegames and soldiers of a package's own,
which castles train ([below](#soldiers)). Still open: ships of a package's own for the shipyards,
and computers that train a package's soldiers.

## The file

```
<user data folder>/mods/<package>/own_figures.toml
```

Each table declares one figure. The table's name is the figure's name in the package; together with
the package's id it is the figure's **string id**, `<package>:<name>`, and that is what everything
else names it by. A name holds lower case letters, digits and `_`.

```toml
# mods/monks/own_figures.toml
[monk]
like = "anno1602:carrier"   # the figure of the original it copies (required)
image = "monks:monk"        # drawn with this image of graphics.toml
shadow = false
```

| Field | Limits | Meaning |
|---|---|---|
| `like` | one figure: of the original, `anno1602:<name or number>` ([names](values.md#names)), or of a package, `<package>:<name>` | every value, animation and sound of that figure, as the copy's starting point. A unit's name (`anno1602:musketeer`, or a soldier of a package) makes a [soldier](#soldiers) of four colours; one colour of a unit, named by its number, is an ordinary figure |
| `image` | an image of a package, `<package>:<name>` ([graphics.md](graphics.md)) | the sprites the figure draws, in the order the original's sheet has them from the figure's `Gfx` ([graphics.md](graphics.md#figures-and-ships)); without it the picture of the figure it copies |
| `shadow` | `true` or `false` | whether it keeps the shadow of the figure it copies; `true` if not given |
| `sound` | a sound of a package, `<package>:<name>` ([sounds.md](sounds.md#sounds-of-buildings-and-figures)) | the sound of each of its animations, at the end of the cycle |
| `shot_sound`, `hit_sound` | a sound of a package, `<package>:<name>` | the sound when it shoots, and when its shot hits |

A table without `like`, with a field that is no field of a figure, with a value outside its limits
or with an image of fewer sprites than the figure draws is passed over with a warning that names
the file and the line; so is a name a package used already. A package declares at most 64 figures.

The figure's **values** (speed, carrying, hit points) are changed as the original's are, in
[`figures.toml`](values.md), by the package itself or by a later one:

```toml
# mods/monks/figures.toml
["monks:monk"]
speed = 40
max_carry = 2
```

A building of a package names it as its carriers or workers with `figure`
([own-buildings.md](own-buildings.md)):

```toml
# mods/monks/own_buildings.toml
[abbey]
like = "anno1602:bakery"
figure = "monks:monk"
```

## Soldiers

A figure whose `like` names a unit, `anno1602:swordsman`, `anno1602:cavalry`,
`anno1602:musketeer`, `anno1602:cannoneer` or a soldier of a package, is a soldier of the package:
four figures, one for each of the original's player colours (red, blue, yellow, white), copied from
the four of the unit. Castles train it beside the original's unit of its kind: a click on the
castle's button of that unit picks it, the next click the next soldier of a package beside it, and
after the last the original's again. Its values come from `figures.toml` as every figure's.

```toml
# mods/guards/own_figures.toml
[pikeman]
like = "anno1602:swordsman"
image = "guards:pikeman"    # once for every colour, or four times: red, blue, yellow, white
tint = true                 # the image is a template: pure grey takes on the player's colour
icon = "guards:pike"        # icons.toml: the castle's button and places
name.en = "Pikeman"
name.de = "Pikenier"
```

| Field | Limits | Meaning |
|---|---|---|
| `image` | an image of a package | the sprites the soldier draws, as for every figure; when the image holds them four times, the colours red, blue, yellow and white one after the other, each colour draws its own, otherwise all four draw the same |
| `icon` | an icon of a package, `<package>:<name>` ([graphics.md](graphics.md)) | the castle's button of the unit while the soldier is picked, and its places in training; without it the icon of the original's unit it copies |
| `name.<language>` | a text | its name, in the help of the castle's button |
| `tint` | `true` or `false` (default) | the image is a template in player colour: once for every colour, its pure grey pixels take on the colour of the owner's banner, for every banner ([flags.md](flags.md#graphics-of-packages-in-player-colour)); needs `image`. A soldier that copies it without an image of its own is drawn so too |

Without `tint`, an image that holds the sprites four times is drawn as is for the first four
banners and gives the template for the others, as the original's units do; an image that holds
them once looks the same for every player.

`icon`, `name` and `tint` are fields of a soldier only; another figure passes them over with a warning.
A soldier takes four figure numbers in a row, from the top down as every figure of a package. On
board a ship a soldier of a package fills a hold as the original's units do; the hold shows the
original's unit of its kind (`Kind`) and keeps which soldier it holds (version 6 of `FLEE`,
[o1602-save.md](../formats/o1602-save.md)). Computers train only the original's units so far.

## Ships

A figure whose `like` names a ship that shipyards build, `anno1602:small_merchant_ship`,
`anno1602:large_merchant_ship`, `anno1602:small_warship`, `anno1602:large_warship` or a ship of a
package, is a ship of the package: two figures, the ship and after it its wreck, copied from the
ship it names and its wreck. Shipyards build it beside the original's ship of its type: in the
type choice of the shipyard's window a click on that type's button again picks it, the next click
the next ship of a package of that type, and after the last the original's again. A small yard
builds only the small types, as for the original's. Its values come from `figures.toml` as every
figure's: the material it is built from (`material`, `material_amount`), holds, cannons, speed and
hit points.

```toml
# mods/cogs/own_figures.toml
[cog]
like = "anno1602:small_merchant_ship"
image = "cogs:cog"          # the ship's sprites in every direction
tint = true                 # the image is a template: pure grey takes on the owner's colour
icon = "cogs:cog_button"    # icons.toml: the shipyard's button while the cog is picked
name.en = "Cog"
name.de = "Kogge"
```

`icon`, `name.<language>` and `tint` work as for soldiers. A shipyard keeps which ship of a
package it builds (version 7 of `FLEE`, [o1602-save.md](../formats/o1602-save.md)). A script sets
one down with `game.add_ship` ([scripts.md](scripts.md)). Computers build only the original's ships
so far.

## Numbers and savegames

The engine gives every figure of a package a number of the figure table, the highest free one
downwards from 1023, in the packages' load order and in the order of each file. The same packages
therefore give the same figures the same numbers on every computer, which is what a multiplayer
game needs.

A game keeps the string ids with the numbers it gave them (section `OWNF` of
[o1602-save.md](../formats/o1602-save.md)). A savegame loads only when the computer's packages give
those figures their numbers again, so a game with such figures needs its packages
([packages.md](packages.md#which-packages-a-game-uses)). Savegames of the original (`.gam`) have
none of them, and a game with them cannot be saved for the original.

## In the code

`engine/assets/include/o1602/assets/own_figures.hpp`: `OwnFigure` (string id and number), `OwnUnit`
(a soldier: its red figure, the original's unit beside which it stands, icon and names),
`apply_own_figures_file`, `figure_number`; `FigureTable::add` gives a copy its number,
`FigureTable::add_unit_family` a soldier its four, and `FigureTable::unit_families` and `unit_of`
know them. `OwnShip` is a ship of a package, `FigureTable::add_ship` gives it and its wreck their
numbers and `FigureTable::ship_type_of` names the type it stands beside; `sim::CostBuilding::order_figure`
is the ship of a package a shipyard builds (`sim::yard_ship`). `sim::CargoSlot::unit` names a
soldier of a package in a hold.
`sim::World::own_figures()` carries them through a game and its savegame.
