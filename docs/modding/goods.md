# Goods of a package's own

A content package may add trade goods the original does not have
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 2, item 3 of
[open-work.md](open-work.md)). Kontors store them, carts fetch them, ships carry them and the free
trader buys and sells them; a building of a package's own makes and uses them.

**Status:** Done are the file `goods.toml`, the ware numbers a game gives the goods, their place in
the Kontor and ship windows, the goods window and the savegame. Still open, with the next steps of
item 3: residents who need such a good (resident classes as data) and the pirates' and natives'
wishes, which keep to the original's goods so that games of the original play as they did. A good
shows an icon of a package's own with `picture` ([graphics.md](graphics.md#icons)).

## The file

```
<user data folder>/mods/<package>/goods.toml
```

Each table declares one good. Its name in the package and the package's id make its **string id**,
`<package>:<name>`, which other files name it by. A name holds lower case letters, digits and `_`.

```toml
# mods/spice/goods.toml
[pepper]
price = 70                    # base price per ton, as the original's goods have
icon = "anno1602:spices"      # the original's good whose icon it shows without `picture`
picture = "spice:pepper"      # optional: an icon of a package (icons.toml)
name.en = "Pepper"
name.de = "Pfeffer"
```

| Field | Limits | Meaning |
|---|---|---|
| `price` | 1 to 10000 | base price per ton; the trader and the trade lists take it as the original's base prices |
| `icon` | a trade good of the original, `anno1602:<name or number>` (names below) | the icon the good shows, or where no package has its `picture` |
| `picture` | an icon of a package, `<package>:<name>` ([icons.toml](graphics.md#icons)) | the icon the good shows instead; none if not given |
| `name.<language>` | a text | the name in that language (`en`, `de`, ...); a language without one shows the English name |

A good needs a price, an icon and at least one name. A table without them, with a field that is no
field of a good or with a value outside its limits is passed over with a warning that names the file
and the line; so is a name the package used already.

The original's trade goods by name: `ore` 2, `gold` 3, `wool` 4, `sugar` 5, `tobacco` 6, `cattle` 7,
`grain` 8, `flour` 9, `iron` 10, `swords` 11, `muskets` 12, `cannons` 13, `food` 14,
`tobacco_goods` 15, `spices` 16, `cocoa` 17, `alcohol` 18, `cloth` 19, `clothes` 20,
`jewellery` 21, `tools` 22, `wood` 23, `bricks` 24.

## Buildings that make them

A building of a package's own ([own-buildings.md](own-buildings.md)) names what it makes and uses by
string id:

```toml
# mods/spice/own_buildings.toml
[pepper_farm]
like = "anno1602:tobacco_plantation"
gfx = "spice:pepper_farm"     # a picture of its own (graphics.md)
ware = "spice:pepper"         # what it makes
raw = ""                      # what it uses: "" for nothing
```

A building that uses nothing (`raw = ""`, and no `raw2`) makes its good from nothing, at the pace
of its `interval`: a farm of that kind needs no fields, and the computers place it as they place a
workshop ([computer-players.md](computer-players.md)). Give a building that makes another good than
the one it copies a `gfx` of its own: the map tells buildings apart by their sprites, so with the
copied building's sprites it shows, and the game reads, the copied building (a pepper farm with the
plantation's sprites would be a tobacco plantation without fields to the computers, which tear it
down).

A good is declared before the buildings that name it: the engine reads every package's
`goods.toml` first, then the buildings.

## Ware numbers and savegames

The original numbers its wares in a byte: trade goods 2 to 24, then soldiers and fields up to 58.
The engine gives the packages' goods the numbers from 59 upwards, in the packages' load order and
the order of each file, so at most 197 in a game. The same packages give the same goods the same
numbers on every computer.

A game keeps its goods with their numbers, prices, icons and names (sections `GOOD` and `GPIC` of
[o1602-save.md](../formats/o1602-save.md)), so a running game does not change when a package is
changed or removed. Such a game cannot be saved as a savegame of the original (`.gam`): its records
have no room for those goods.

## In the panels

The Kontor and the ship's cargo list the packages' goods after the original's; when there are more
goods than places, the Kontor has a second page, turned by clicking the page number at the right
end of its title. The goods window
lists them under "Further goods".

## In the code

`engine/assets/include/o1602/assets/goods.hpp`: `OwnGood`, `Goods` (which ware numbers are trade
goods, their prices and icons), `apply_goods_file` and `ware_of`. `sim::World::goods()` carries them
through a game; the simulation asks it instead of the original's fixed list.
