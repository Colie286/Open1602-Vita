# Pictures of a package's own

A content package may bring its own pictures as PNG images
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 2, item 4 of
[open-work.md](open-work.md)). A building the package adds draws them
([own-buildings.md](own-buildings.md)).

**Status:** Done are the file `graphics.toml`, PNG images for the buildings of a package at all three
zoom levels, the field `gfx` of such a building that names an image, the file `icons.toml` with
icons for the build menu, the goods and the messages, the file `figure_images.toml`, which
draws the original's figures and ships with a package's images, and `building_images.toml`, which
does the same for the original's buildings. Coats of arms of a package are in
[flags.md](flags.md#coats-of-arms-of-a-package). Figures and ships of a package's
own (new figure numbers) come with the content registry's figures. Sounds and music are in
[sounds.md](sounds.md).

## The file

```
<user data folder>/mods/<package>/graphics.toml
```

Each table names one image. The table's name together with the package's id is the image's string
id, `<package>:<name>`. A name holds lower case letters, digits and `_`.

```toml
# mods/lights/graphics.toml
[lighthouse]
image = "gfx/lighthouse.png"           # the sprites at the largest zoom level
image_medium = "gfx/lighthouse_m.png"  # optional: the medium zoom level
width = 64                             # one sprite, in pixels, at the largest zoom level
height = 160
count = 16                             # optional: the first 16 sprites of the grid
```

| Field | Limits | Meaning |
|---|---|---|
| `image` | a path in the package, folders split by `/` | the PNG of the largest zoom level (`GFX`) |
| `image_medium`, `image_small` | a path in the package | the PNG of the medium (`MGFX`) and the smallest zoom level (`SGFX`) |
| `width`, `height` | 1 to 1024 | the size of one sprite at the largest zoom level |
| `count` | 1 to the sprites of the grid | how many sprites the image has; all whole cells of the grid if not given |

An image is a grid of sprites of `width` x `height` pixels, read row by row from the top left; what
is left over at the right and the bottom is not used. At the medium zoom level a sprite is half as
wide and high (rounded up), at the smallest a quarter. If the package gives no image for a smaller
level, it is made from the next larger one at half size, as the engine does for the original's
missing sheets.

A sprite stands on its bottom row: the bottom of the cell is the bottom of the tile it is drawn on,
the transparent rows above the picture are left out. A pixel with less than half alpha is
transparent, any other is opaque: the original's sprites have no partial transparency either. The
PNG may have any colour type and bit depth, but no interlacing.

A table that is wrong, an image that cannot be read, or a smaller image with fewer sprites than the
large one is passed over with a warning that names the file and the line. The images of all
packages hold at most 25,535 sprites.

## Sprite numbers

Every sprite of an image gets a number after those of the original's `STADTFLD.BSH`: the first
image of the first package starts at 40000, every further image follows the last one, in the
packages' load order. So the numbers are the same on every computer with the same packages, and
the islands' tiles keep them in a savegame, as they keep the original's.

## Buildings that draw an image

A building of a package names the image its sprites start with:

```toml
# mods/lights/own_buildings.toml
[lighthouse]
like = "anno1602:watchtower"
gfx = "lights:lighthouse"
```

The image holds the sprites in the order the original's sprites of the building `like` have:
first one per tile of the footprint, row by row; then, for a building that can be turned, the same
for each of the other three directions (`Rotate` of `haeuser.cod` sprites further each); then, for
an animated one, the same for each further frame (`AnimAdd` sprites further each). A building whose
image has fewer sprites than it draws is passed over with a warning. A sprite at the largest zoom
level is 64 pixels wide, as the original's tiles are.

## Figures and ships

```
<user data folder>/mods/<package>/figure_images.toml
```

A figure of the original, walking or sailing, is drawn with an image of `graphics.toml`. Each
table names the figure as `figures.toml` does ([values.md](values.md#names)): `["anno1602:<name>"]`
for a unit in all four colours, `["anno1602:<number>"]` for one `FIGUR` of `figuren.cod`.

```toml
# mods/knights/figure_images.toml
["anno1602:swordsman"]
image = "knights:knight"
shadow = false

["anno1602:small_merchant_ship"]
image = "knights:cog"
```

| Field | Limits | Meaning |
|---|---|---|
| `image` | an image of a package, `<package>:<name>` ([graphics.toml](#the-file)) | the sprites the figure draws instead of its own (required) |
| `shadow` | `true` or `false` | whether it keeps its shadow of `SCHATTEN.BSH`, which has the shape of the original's picture; `true` if not given |

The image holds the sprites in the order the original's sheet has them from the figure's `Gfx`:
for each animation (`ANIM` of `figuren.cod`) its frames `AnimAdd` apart and its eight directions
`Rotate` apart, from `AnimOffs` on; for a ship its eight directions `Rotate` apart. An image with
fewer sprites than the figure draws is passed over with a warning that names how many it needs. A
figure's sprite is centred on it and stands on its bottom row, as the original's do. A unit drawn
with a package's image is not recoloured for the players after the fourth: draw its colours into
the image of each of the four figures (`anno1602:<number>`) or keep one look for all.

Only the picture changes: the figure keeps its values, animations and sounds, and a game keeps
nothing of it, so a game looks as the packages of the computer it runs on say. The smoke and the
bow wave of a ship and the effects of combat stay the original's.

## Buildings of the original

```
<user data folder>/mods/<package>/building_images.toml
```

A building of the original is drawn with an image of `graphics.toml`. Each table names the
building as `buildings.toml` does ([values.md](values.md#names)): `["anno1602:<name>"]` or
`["anno1602:<Id>"]` of `haeuser.cod`.

```toml
# mods/stone/building_images.toml
["anno1602:grain_mill"]
image = "stone:mill"

["anno1602:20502"]
image = "stone:bakery"
```

| Field | Limits | Meaning |
|---|---|---|
| `image` | an image of a package, `<package>:<name>` ([graphics.toml](#the-file)) | the sprites the building draws instead of its own (required) |

The image holds the sprites in the order the original's sheet has them from the building's `Gfx`,
as for a [building of a package](#buildings-that-draw-an-image): one per tile of the footprint, row
by row; the same for each of the other three directions; the same for each further frame of its
animation. An image with fewer sprites than the building draws is passed over with a warning that
names how many it needs. A sprite at the largest zoom level is 64 pixels wide and stands on its
bottom row.

Only the picture changes: the islands keep the original's sprite numbers, and the terrain sheet
draws those with the image's sprites. So a game keeps nothing of it and looks as the packages of
the computer it runs on say; a game saved with the package loads without it and shows the
original's buildings. Each table names one record of `haeuser.cod`: a crop's three records
(growing, ripe, withered) and a road's pieces are named one by one. If two packages draw the same
building, the one loaded later wins.

## Icons

```
<user data folder>/mods/<package>/icons.toml
```

Each table names one icon of the interface, a PNG of its own. Its string id is
`<package>:<name>`, as an image's.

```toml
# mods/spice/icons.toml
[pepper]
image = "icons/pepper.png"
```

| Field | Limits | Meaning |
|---|---|---|
| `image` | a path in the package, folders split by `/`; at most 256 x 256 pixels | the icon (required) |
| `tint` | `true` or `false` (default) | the icon is a template in player colour, a coat of arms for instance: where it stands for a player, its pure grey pixels take on the colour of that player's banner ([flags.md](flags.md#graphics-of-packages-in-player-colour)). So far a message's icon (the player it is about) and a soldier's icon in a castle (the castle's owner); elsewhere it is drawn as it is |

An icon is drawn as large as its PNG is, for the interface of screens 1024 pixels wide; the
interface's smaller art (640 and 800 pixels) takes it smaller. Unlike the sprites of the islands it
keeps its alpha: its edges may be soft. An icon larger than the place it is drawn in is made
smaller to fit it; so make it the size of the original's icon it stands beside. A table that is
wrong or an image that cannot be read is passed over with a warning that names the file and the
line. The packages have at most 4096 icons in all.

Three files name an icon by its id:

| File | Field | Shows the icon |
|---|---|---|
| [`build_menu.toml`](build-menu.md) | `icon = "spice:mill"` (or the number of a sprite of the original) | in the pop-up of the build menu and in the building's window |
| [`goods.toml`](goods.md) | `picture = "spice:pepper"` | wherever the good's icon is: Kontor, ships, production chains, goods window |
| [`messages.toml`](messages.md) | `icon = "spice:harvest"` (or one of the original's kinds) | in the status bar |

The icons get sprite numbers from 100000 in the packages' load order; a game keeps only their ids.
A good of a game played without the package of its icon shows the original's icon its `icon` names.

## In the code

`engine/core/include/o1602/core/png_reader.hpp`: the PNG reader.
`engine/assets/include/o1602/assets/package_graphics.hpp`: `apply_graphics_file` and
`PackageImage`. `engine/render/include/o1602/render/package_sprites.hpp`: the sprites of an image
at a zoom level (`PackageImageSource`), which `render::load_world_graphics` places at their numbers
among the terrain's (`GraphicsOptions::package_images`). Figures and ships: `engine/assets/include/o1602/assets/figure_looks.hpp`,
`apply_figure_images_file` and `apply_figure_looks`, which the viewer applies to its figure table;
buildings of the original: `engine/assets/include/o1602/assets/building_looks.hpp`,
`apply_building_images_file`, whose redirects `render::RedirectedSprites` applies to the terrain sheet
(`GraphicsOptions::sprite_redirects`);
`render::land_figure_sprites` and the ships' hull draw such a figure from the terrain sheet. Icons:
`apply_icons_file` and `PackageIcon`;
`engine/ui/include/o1602/ui/package_icons.hpp` holds the game's icons for the interface
(`ui::set_package_icons`, `ui::ware_sprite`), and `render::SdlUiRenderer` draws an icon's sprite
number in any sheet (`render::package_icon_pixels`).
