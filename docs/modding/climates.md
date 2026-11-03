# Climates

A content package can name climates of its own. A climate of a package is like the original's
north or south and differs in its name, the crops its islands grow, the trees of their forest and
how many islands of a new own world take it (item 9 of [open-work.md](open-work.md)).

**Status:** Done are the file `climates.toml`, its crops, forest and ground, its share of the
islands of new own worlds, also per kind of island world, island templates of its own
(`islands.toml`), its name and crops in the status line over grass and trees, and the island
editor's choice of it for new islands.

## The file

```
<user data folder>/mods/<package>/climates.toml
```

Each table names one climate; its id is `<package>:<name>`, the name of lower case letters,
digits and `_`:

```toml
# mods/highlands/climates.toml
[highlands]
like = "north"
name = "Highlands"
name.de = "Hochland"
crops = "tobacco, wine"
forest = "anno1602:mixed_forest"
ground = "anno1602:22722, anno1602:22724"
share = 25
share.archipelago = 60
share."seas:atolls" = 0
```

| Field | Value | Meaning |
|---|---|---|
| `like` | `"north"` or `"south"` (required) | the original's climate it is like: its island templates, animals and the trees players plant (palms in the south) |
| `name`, `name.<language>` | 1 to 64 bytes | what the status line names it, in every language or in one (`de`, `en`); without one the id |
| `crops` | a list of `tobacco`, `spices`, `sugar`, `cotton`, `wine`, `cocoa` | the crops its islands grow; without it the like's three |
| `forest` | a list of up to 16 trees, `anno1602:<name or number>` ([names](values.md#names)) or `<package>:<name>` ([own-buildings.md](own-buildings.md)) | the trees of the forest of its islands of their own shape; without it the like's |
| `ground` | a list of up to 16 kinds of ground, named as the trees | the ground of its islands of their own shape, each kind as often; without it the original's mix |
| `share` | 0 to 100 | percent of the islands of `like` that take this climate in a new own world, not the players' home islands; 0 without it |
| `share.<kind>` | 0 to 100 | the percent in a world of islands of their own shape of one kind of island world instead of `share`: Open1602's `varied`, `archipelago`, `continents`, `giants`, `even`, or a package's preset as `share."<package>:<name>"` ([landscapes.md](landscapes.md)); up to 32 |

A climate's crop grows half on each of its islands and fully on those the world gives it to, as
the original's three crops of a climate do; the other crops do not grow there. Grain grows
everywhere, as in the original.

A field with a value outside its range, a crop or tree that is none or an unknown field is passed
over with a warning that names the file and the line; a table without `like` is passed over. The
packages name at most 32 climates in all. A tree must be forest (`Kind` WALD), such as
`anno1602:mixed_forest` or `anno1602:palm_forest`, or a number of the forests the original's islands
grow: 21304 to 21324 and 22802 to 22822, even numbers. A kind of ground must be ground (`Kind`
BODEN), such as the meadow 22722.

## How a world uses them

A new own world (the start dialog's "Own world", `o1602-tool new-world --mods`) draws for each
island of the north or the south, other than a home island, whether a climate of a package that is
like it takes it, by the climates' shares in their load order; the shares of the climates like one
climate together take at most all of its islands. These draws have a random stream of their own, so
a world without such climates is the same as before. A world of islands of their own shape
(island shapes "Own" or "Mixed") takes a climate's `share.<kind>` for the kind of island world its
start dialog chose; a preset changed there keeps its kind, and settings without a kind (the command
line's, a settings file of before) are the kind of Open1602's preset with the same settings. A
world of the original's islands takes `share`. The world gives an island of such a climate
only its crops (sugar cane and wine still never on one island), and every start still gets each
crop within reach where an island can grow it. An island of its own shape (island shapes "Own" or
"Mixed") grows the climate's forest on the climate's ground; an island drawn from a template keeps
the template's trees and ground.

**Templates of a climate.** An island template of a package (`islands.toml`, [islands.md](islands.md))
may name a package's climate: `climate = "<package>:<name>"`. A new own world draws it among the
templates of the file's climate (which should be the climate's `like`), and the island drawn from it
takes the package's climate, a player's home island excepted; without that climate among the
packages the island keeps the original's.

An island of a package's climate keeps the original's climate in its header, so everything the
original decides by the climate stays as it is. A game keeps its climates and which island has
which (section `CLIM` of the savegame), so a savegame or a multiplayer game needs no package for
them. A world saved as a `.gam` keeps only the original's climate.

## In the code

`engine/sim/include/o1602/sim/climates.hpp`: `Climate`, `apply_climates_file`, kept in
`Balance::climates`; `WorldIsland::climate` numbers an island's climate from 1. `island_fertility`
and `climate_crops` (`construction.hpp`) grow the crops; the world generator takes the climates
from `WorldStart::climates` (`Generator::own_climate`, `crop_candidates`, and
`Generator::load_package_island` for `PackageIsland::own_climate`) and the island shapes their
trees and ground (`ShapeRequest::trees`, `ShapeRequest::ground`).
