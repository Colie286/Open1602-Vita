# Making a mod

This page is where a modder starts. It shows how a package is made, step by step, and points to
the guide of each file. What a package can do and what is still to come is in
[open-work.md](open-work.md); the decisions behind it are in
[ADR 0007](../architecture/0007-extensibility-and-modding.md).

## The example package

The repository has a package that uses every file once: [`data/mods/example`](../../data/mods/example).
It adds pepper, a farm that grows it and a lighthouse drawn with images of its own, sounds and
music, a script, a scenario with a campaign, a kind of island world and an island template, and changes a few values
of the original. Copy it, rename the folder and change it, or read it next to the guides below.
The tests load it with and without the original's data and expect no warning, so it always works
with the version of Open1602 it comes with.

## Step by step

### 1. Make the folder

A package is a folder in the `mods` folder of Open1602's user data folder:

| System | Folder |
|---|---|
| Linux | `~/.local/share/Open1602/mods/<package>/` |
| macOS | `~/Library/Application Support/Open1602/mods/<package>/` |
| Windows | `%APPDATA%\Open1602\mods\<package>\` |

The folder's name is the package's id, for example `lighthouse`: lower-case letters, digits, `_`
and `-`, at most 64 bytes. `anno1602` is the original's id and cannot be used. Everything the
package adds is named `<package>:<name>` (`lighthouse:beacon`).

### 2. Describe it

`mod.toml` gives the package a name, a version and the packages it needs
([packages.md](packages.md#the-manifest)):

```toml
name = "Lighthouse"
version = "1.0"
description = "A lighthouse for every coast."
```

The files of a package are a small part of [TOML](https://toml.io): tables (`[name]`), keys with
dots (`name.de`), strings, numbers and `true`/`false`. A line starting with `#` is a comment.

### 3. Add what the package changes

Each file does one thing; a package has only the files it needs.

| What | File | Guide |
|---|---|---|
| values of the original's buildings and figures | `buildings.toml`, `figures.toml` | [values.md](values.md) |
| buildings of the package's own | `own_buildings.toml` | [own-buildings.md](own-buildings.md) |
| figures of the package's own | `own_figures.toml` | [own-figures.md](own-figures.md) |
| trade goods | `goods.toml` | [goods.md](goods.md) |
| classes of inhabitants and their needs | `classes.toml` | [classes.md](classes.md) |
| unlock levels | `levels.toml` | [levels.md](levels.md) |
| the build menu | `build_menu.toml` | [build-menu.md](build-menu.md) |
| messages of the status bar | `messages.toml` | [messages.md](messages.md) |
| images of buildings, figures and ships; icons | `graphics.toml`, `figure_images.toml`, `icons.toml` | [graphics.md](graphics.md) |
| music and sounds | `music.toml`, `sounds.toml` | [sounds.md](sounds.md) |
| numbers of the game: trader, disasters, computers' gifts | `balance.toml` | [balance.md](balance.md) |
| what the computer players build | `computers.toml` | [computer-players.md](computer-players.md) |
| the neutral factions: pirates, trader, natives | `factions.toml` | [factions.md](factions.md) |
| kinds of island world for new games | `landscapes.toml` | [landscapes.md](landscapes.md) |
| island templates of the package's own | `islands.toml` | [islands.md](islands.md) |
| a script that hears and changes the game | `script.lua` | [scripts.md](scripts.md) |
| scenarios and campaigns | `scenarios.toml`, `campaigns.toml` | [scenarios.md](scenarios.md) |

The original's buildings, figures and goods are named `anno1602:<name>`, for example
`anno1602:bakery` ([names](values.md#names)). Images, sounds, maps and scripts are files in the
package's folder, named by their path with `/` (`gfx/lighthouse.png`).

### 4. Check it

`o1602-tool check-mod` reads the packages of a folder as the game does and lists every problem
with the file and the line, without starting the game:

```
$ ANNO1602_DIR=/path/to/anno1602 o1602-tool check-mod ~/.local/share/Open1602/mods
package lighthouse 1.0
problem: lighthouse/figure_images.toml line 2: [anno1602:small_merchant_ship]: its image lighthouse:cog holds 8 sprites, the figure draws 47
1 packages, 1 problems
```

It needs the original's installation (`ANNO1602_DIR`), since a package builds on its buildings and
figures, and exits with 1 when there is a problem. The game writes the same lines to its log.

### 5. Play it

A new game uses every package that is turned on in the main menu's screen "Mods"
([packages.md](packages.md#the-screen-mods)). The game keeps the packages it was started with in
its savegame; a multiplayer game lets in only computers with the same packages, and the host can
send them.

### 6. Share it

Pack the folder as a zip archive (`lighthouse.zip`) and put it into another player's `mods`
folder; the game reads it as the folder ([packages.md](packages.md#a-package-as-a-zip-archive)).
Raise the version in `mod.toml` with each release, so that savegames and multiplayer games can tell
the releases apart.

## Limits

What a package cannot change yet, and the fixed limits of the data, are in
[open-work.md](open-work.md#still-open) and [data-limits.md](data-limits.md).
