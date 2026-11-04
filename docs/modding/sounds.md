# Sounds and music of a package's own

A content package may bring its own music and the sounds of its messages as WAV, Ogg Vorbis or
FLAC files ([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 2, item 4 of
[open-work.md](open-work.md)).

**Status:** Done are the file `music.toml` (tracks of a package beside the original's `MUSIC8`, as
normal or as battle music, or in place of one of them), sounds of messages in all three formats
([messages.md](messages.md)) and `sounds.toml`, the sounds a package's buildings and figures make
([own-buildings.md](own-buildings.md), [own-figures.md](own-figures.md)) and sounds in place of the
original's `SAMPLES`. The voice of an original message is replaced in `messages.toml`
(`["anno1602:<number>"]` with `sound`, [messages.md](messages.md)).

## The formats

| Format | Read |
|---|---|
| WAV | PCM of 8 or 16 bits, and the original's 4-bit music |
| Ogg Vorbis (`.ogg`) | any quality |
| FLAC (`.flac`) | any bit depth |

The game tells the format by the file's first bytes, not by its name. Any sample rate up to
192,000 Hz is played; the mixer converts it to the device's. A file of more than two channels keeps
its front left and right, and a file longer than 20 minutes is cut there.

## Music

```
<user data folder>/mods/<package>/music.toml
```

Each table names one track:

```toml
# mods/shanty/music.toml
[sea]
file = "music/sea.ogg"
title = "Sea Shanty"

[storm]
file = "music/storm.flac"
battle = true
```

| Field | Limits | Meaning |
|---|---|---|
| `file` | a path in the package, folders split by `/` | the sound file of the track (required) |
| `title` | 1 to 64 bytes | what the music list in the options shows; the table's name if not given |
| `battle` | `true` or `false` | battle music, played while shots fall near the middle of the view as the original's `Fight` tracks; `false` if not given |
| `replaces` | 1 to 64 bytes | the title of a track of `MUSIC8` (its file name without `.wav`, case ignored), which this one plays in place of |

The packages' tracks come after the original's in the music list, in the packages' load order, and
are played at random or in order with them; the player turns them off there as the original's. A
track whose file is missing or is no sound file, or whose title another track has, is passed over
with a warning that names the file and the line. A track is decoded when it starts, beside the
game, as the original's are.

### Replacing a track of the original

```toml
# mods/shanty/music.toml
[calm]
file = "music/calm.ogg"
replaces = "Greensleaves"

[battle]
file = "music/battle.flac"
replaces = "Fight1"
```

A track with `replaces` takes the original track's place: the music list keeps the original's title
and order, the player turns it off there as before, and a `Fight` track stays battle music; its own
`title` and `battle` are not used. If two packages replace the same track, the one loaded later
wins. A title `MUSIC8` has not is passed over with a warning on the console.

## Sounds of messages

A message of a package names its sound with `sound = "<path>"` ([messages.md](messages.md)); the
file may be WAV, Ogg Vorbis or FLAC. It is decoded once, when the game starts.

## Sounds of buildings and figures

```
<user data folder>/mods/<package>/sounds.toml
```

Each table names one sound; its id is `<package>:<name>`:

```toml
# mods/lighthouse/sounds.toml
[horn]
file = "sounds/horn.ogg"

[splash]
file = "sounds/splash.wav"
```

| Field | Limits | Meaning |
|---|---|---|
| `file` | a path in the package, folders split by `/` | the sound file (required) |

The sounds get numbers from 1000 on, in the packages' load order and the order of each file, at
most 1024 for all packages; the game decodes them once when the packages are loaded. A sound
whose file is missing or is no sound file is passed over with a warning that names the line.

A building or figure of the package's own names them:

| File | Field | When it sounds |
|---|---|---|
| `own_buildings.toml` | `build_sound` | when the building is built, instead of the sound of the building it copies |
| `own_figures.toml` | `sound` | every animation of the figure, at the end of its cycle, instead of the copied figure's sounds |
| `own_figures.toml` | `shot_sound` | when the unit or ship shoots |
| `own_figures.toml` | `hit_sound` | when its shot hits |

```toml
# mods/lighthouse/own_buildings.toml
[beacon]
like = "anno1602:watchtower"
build_sound = "lighthouse:horn"
```

### Replacing a sound of the original

A sound with `replaces` plays in place of a file of the original's `SAMPLES` folder, wherever the
game plays it: buildings, figures, ships, battles, scenery and the interface.

```toml
# mods/cannons/sounds.toml
[boom]
file = "sounds/boom.ogg"
replaces = "kanone5"
```

| Field | Limits | Meaning |
|---|---|---|
| `replaces` | the name of a file of `SAMPLES` without `.wav`, case ignored | the original's sound this one plays in place of |

A file that two sound numbers share (`schwertb`, `steinmtz`) is replaced for both. If two packages
replace the same file, the one loaded later wins. A name `SAMPLES` has not gives a warning; the
sound then plays only where a package names it by its id.

## In the code

`engine/audio/include/o1602/audio/sound_file.hpp`: `decode_sound` (the decoders are stb_vorbis and
dr_flac in `third_party`). `engine/audio/include/o1602/audio/package_music.hpp`: `apply_music_file`
and `MusicTrack`, which `audio::Music` plays beside `MUSIC8`'s tracks
(`app::GameSound::set_package_music`). `engine/assets/include/o1602/assets/package_sounds.hpp`:
`apply_sounds_file` and `PackageSound`; `audio::Effects::set_package_sounds` plays them by their
numbers beside the original's (`app::GameSound::set_package_sounds`), and
`audio::Effects::set_replaced_sounds` in place of the original's (`audio::samples_named`).
