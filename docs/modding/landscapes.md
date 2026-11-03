# Kinds of island world

A content package may name kinds of island world of its own: presets of the landscape of islands
of their own shape (world generator stage 2, item 9 of [open-work.md](open-work.md)), which the
start dialog's page "Island world" offers after Open1602's (Varied, Archipelago, Continents, Few
giants, Even).

**Status:** Done are the file `landscapes.toml` and its presets in the start dialog. The command
line (`--island-preset`) names Open1602's presets only.

## The file

```
<user data folder>/mods/<package>/landscapes.toml
```

Each table names one preset; its id is `<package>:<name>`, the name of lower case letters, digits
and `_`:

```toml
# mods/seas/landscapes.toml
[atolls]
like = "archipelago"
title = "Atolls"
title.de = "Atolle"
land = 10
max = 40

[pangaea]
title = "Pangaea"
title.de = "Urkontinent"
land = 45
min = 400
spread = "even"
gap = "wide"
```

| Field | Value | Meaning |
|---|---|---|
| `like` | `varied`, `archipelago`, `continents`, `giants`, `even` or `<package>:<name>` of a package loaded before | the preset it starts from; without it the defaults (Varied) |
| `title`, `title.<language>` | 1 to 64 bytes | what the start dialog shows, in every language or in one (`de`, `en`); without one the id |
| `land` | 5 to 45 | percent of the map that is land |
| `min`, `max` | 16 to 1000 | the side of the smallest and the largest island in tiles; when one passes the other, the other follows |
| `spread` | `even`, `mixed`, `giants` | how the sizes are spread between them |
| `gap` | `narrow`, `normal`, `wide` | the water between two islands |
| `coast` | `smooth`, `natural`, `rugged` | the coast |
| `outline` | `round`, `long`, `branched` | the outline |
| `forest`, `mountains` | `little`, `normal`, `much` | forest, and ore mountains for the size of an island |

The fields are those of the command line's `--island-<field>`. A field with a value outside its
range, an unknown field or a `like` that names no preset is passed over with a warning that names the
file and the line; a package names at most 32 presets.

A preset only fills the settings of a new world: the world comes from them and its number, and a
game needs no package for it. A preset that equals one of Open1602's shows as that one.

The settings keep the kind of island world they come from (`<package>:<name>`, also when a setting
of it is changed afterwards; the settings file's line `island_preset`). A climate of a package may
take another share of the islands in a kind of island world (`share.<kind>`,
[climates.md](climates.md)).

## In the code

`engine/sim/include/o1602/sim/package_landscapes.hpp`: `apply_landscapes_file`,
`PackageLandscape`; the start dialog takes them with `app::set_package_island_presets`
(`endless_setup.hpp`), which `MapViewer` calls when it loads the packages.
