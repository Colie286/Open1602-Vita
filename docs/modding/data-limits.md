# Limits of the data files

Open1602 reads the original's `haeuser.cod`, `figuren.cod` and `.gad` layouts, and modded copies
of them. A modded file can hold any number, so the loaders check the ones that index tables or
size memory and refuse or cut what lies outside. These are the limits.

| File | Value | Limit | Outside the limit |
|---|---|---|---|
| `haeuser.cod` | `Id` | 20000 to 23500 (as in the original) | the file is refused |
| `haeuser.cod` | `Nummer` | 0 to 65535 | the file is refused |
| `haeuser.cod` | `Size` | 1 to 255 tiles each way (an island of the original is at most 255 wide) | above: the file is refused; 0 or less is read as 1 |
| `haeuser.cod` | `Ware`, `Rohstoff`, `Workstoff`, `BGruppe`, `Erzbergnr`, `Ruinenr` | 0 to 255 (a negative `Ruinenr` is no ruin) | the file is refused |
| `haeuser.cod` | `BGruppe` of a house (resident classes) | 5 classes, Pioniere to Aristokraten (the needs of each class are the rules' table) | the file is refused |
| `haeuser.cod` | `Bauinfra`, `INFRA_*` levels | 0 to 32 (the levels a player has reached are one 32-bit mask) | the file is refused |
| `haeuser.cod` | `Figurnr` | 0 to 65535 | the file is refused |
| `haeuser.cod` | `Gfx` and the sprites of the building after it (one a tile) | below 65535 (the island map keeps 16-bit sprite numbers, 0xFFFF is an empty tile) | the file is refused |
| `haeuser.cod` | `Money` of `HAUS_BAUKOST` | 0 or more (a negative cost would pay for building) | the file is refused |
| `haeuser.cod` | `Maxlager`, `Prozent` (and every amount scaled to 1/32 t) | the range of a 32-bit unsigned number after scaling | saturated |
| `haeuser.cod` | `Wegspeed`, `Randwachs` | 0 to 400 and 0 to 200 before the scaling (no signed overflow) | clamped |
| `haeuser.cod` | `Arbeiter`, `Figuranz`, `RandAnz`, `RandAdd` | the range of the field | clamped |
| `figuren.cod` | `Nummer` | 0 to 1023 | the file is refused |
| both | `ObjFill` | 10000 copies | clamped, with a warning |
| both | `ObjFill`, all statements of a file | 200000 records made, nested records counted | the statement that would pass it and every later `ObjFill` are ignored, with a warning |
| `.gad` | `Nummer` of a gadget | 0 to 4095 (4096 gadgets per file; the original's `MAXGADGET` was 300, [gad.md](../formats/gad.md)) | gadgets above it are left out, with a warning |
| both | a number with a decimal point | the range of a 32-bit integer | clamped |
| both | times, amounts, health, radius (`Worktime`, `Maxenergy`, ...) | the range of the field | clamped (`core::saturating_cast`) |
| `.gam`, `.szs` | width and height of an island | at least 1 tile | the island is left out, with a warning |
| `build_menu.toml` | `row`, `position` of a building | 6 rows a pop-up, 4 buildings a row; `id` a record of `haeuser.cod` | the table is passed over, with a warning |
| `classes.toml` | classes of inhabitants | 8 in a game, the original's five among them ([classes.md](classes.md)) | the table is passed over, with a warning |
| `levels.toml` | unlock levels | 255 in a game, the original's 32 among them; `min_inhabitants` 0 to 10,000,000 ([levels.md](levels.md)) | the table is passed over, with a warning |
| `music.toml`, `sounds.toml`, sounds | sound files of the packages; at most 1024 in `sounds.toml` of all packages | WAV, Ogg Vorbis, FLAC; up to 192,000 Hz, 20 minutes, the first two channels; a title of 64 bytes ([sounds.md](sounds.md)) | the track is passed over, with a warning; a longer file is cut |
| `graphics.toml` | images of the packages | 25,535 sprites in all, sprite numbers 40000 to 65534; a sprite at most 1024 x 1024 ([graphics.md](graphics.md)) | the table is passed over, with a warning |
| `icons.toml` | icons of the packages | 4096 in all, sprite numbers from 100000; an icon at most 256 x 256 ([graphics.md](graphics.md#icons)) | the table is passed over, with a warning |
| PNG images | width and height | 16384 a side, 64 megapixels; no interlacing ([graphics.md](graphics.md)) | the image is passed over, with a warning |
| `goods.toml` | goods the packages declare | 197 in a game: ware numbers 59 to 255 ([goods.md](goods.md)) | the table is passed over, with a warning |
| `own_buildings.toml` | buildings a package declares | 256 per package; `Id` 20000 to 23500 in all, the highest free ones ([own-buildings.md](own-buildings.md)) | the table is passed over, with a warning |
| `.gad` | `Include` | 4 levels deep, 1 MiB of text in all | the rest is left out, with a warning |
| both | diagnostics of a file | 1000 messages | one more says that more follow; the rest are not listed |
| `.bsh`, `.zei` | rows of a sprite | no more than the bytes that follow its header, plus one | the file is refused |
| `.bsh`, `.zei` | pixels of all sprites of a sheet | 256 M | the file is refused |
| `.smk` | time per frame | above 0 and at most 1 s | the video is refused |
| `.smk` | sound of a track | what its frames last plus a second, at most 64 M samples | the sound is refused, the video plays without it |

The rule behind the table (LC-15): a number that indexes a table, names something (a ware, a
figure, a record) or sizes memory is refused when it lies outside, because a cut-down value would
silently mean something else; an amount (stock, cost, time, share) is saturated or clamped to its
field. Settings and command-line options follow their own rule (a bad option stops with the
usage, a bad line of `settings.cfg` falls back to its default), and saves, snapshots and network
messages are refused whole when anything in them is out of range.

Decimal numbers become integers by truncating toward zero and clamping, never by a plain cast:
a cast of a value out of range is undefined in C++ and gives different results on x86 and ARM,
and every computer of a multiplayer game must read the same data the same way.

The game numbers the engine relies on (ware, building kind, production kind and figure numbers,
and the haeuser.cod records it places by Id: meadow, ore mountain, ruins, the crops of a new
world, the landscape list) are named in one place,
`engine/assets/include/o1602/assets/game_data.hpp`. A data set that renumbers them needs that
header changed too; a data loader for that is planned
([ADR 0007](../architecture/0007-extensibility-and-modding.md)). The test `game_numbers`
(`cmake/CheckGameNumbers.cmake`) fails when a record Id (20001 to 23999) is written anywhere else
in the engine; the build menu's table is the one exception left.

Three records cannot be missing: the meadow that demolished forest becomes (Id 22722), the big
ore mountain that volcanoes erupt from (21108) and the first ruin (22601). A haeuser.cod without
one of them is refused at loading with the Id it lacks (`id::kRequired`), instead of a game that
fails later. Without a meadow record (only possible in a hand-made table), demolished forest
returns to its forest ground.

The score of the end screen and the monument allowance are named there too, in
`assets::scoring`: the city and fight curves as tables of straight segments, the money steps, the
monument and defeat points, and the rule that lets a growing city build a monument (every 30
minutes of game time with 200 inhabitants of class Bürger and higher). A mod changes them in that
header.

Limits of the game itself that more than one part checks (players with a start, island numbers
and indexes, cities per island, the world's side) are defined once, in
`engine/sim/include/o1602/sim/limits.hpp`; the network's and the menus' limits derive from them.
When one is exceeded the player sees which ("more than the game holds: 1 to 12 players"), not
"the file is damaged".
