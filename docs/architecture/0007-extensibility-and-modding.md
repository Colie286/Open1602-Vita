# 0007 – Extensibility, content and modding

Status: accepted · 2026-10-16

## Requirements

Set by the project owner (2026-10-16). Open1602 should allow:

1. **New content:** buildings, colours, production chains, islands, landscapes, factions,
   animations and graphics, music and sounds.
2. **Custom campaigns and scenarios.**
3. **New island types and island sizes**, for example huge islands.
4. **A more flexible, more varied island generator.**
5. **No fixed player count.** Instead of four players plus traders, pirates and natives, it should
   be possible, for example, to play with five in multiplayer. The game then scales accordingly,
   for instance for pirates and travelling traders.
6. **Extensible colours and flags** with a flag selection menu as in Anno 1503.

These requirements apply to all new code, even if the feature concerned only comes later.
Fixed numbers of the original may only appear in the loaders and savers of the original formats.

## Decision

### 1. Two layers: original format and engine model

| Layer | Modules | Content |
|---|---|---|
| Original format | `assets` | Reads the files of the original as they are: 7 player slots, colours 0–6, owner 7 = nobody, world 500 × 350, at most 255 islands, `.cod` numbers |
| Engine model | `sim`, `render`, `ui`, `net`, `script` | Knows none of these limits. Players, banners (colour and flag), buildings, goods and figures are entries in tables of any size |

The translation happens exclusively when loading (e.g. `sim::load_world`) and later when
saving in the original format. Savegames of the original stay loadable. Saving uses our own format.
An export to the original format is only possible if the state fits into it; otherwise there is
a clear message.

### 2. Content as packages with stable ids

- **Base game = one content package** (`anno1602`). It is generated at runtime from the original installation
  (`.cod`, `.bsh`, `.scp`, sounds), so that no original data is distributed (ADR 0002).
- **Mods are further packages** (folder or archive) with a manifest: id, version,
  dependencies, load order. They contain data (TOML/JSON), graphics (PNG with
  atlas description), audio (OGG/WAV/FLAC) and scripts (Lua).
- **Ids are strings** with a package prefix (`anno1602:kontor1`, `meinmod:leuchtturm`).
  A package can add entries, modify them (overlay on individual fields) or replace them.
- **At runtime only indices count.** After loading, the tables become flat arrays.
  Strings are only needed for loading, saving and multiplayer reconciliation. This costs
  nothing in the simulation (performance requirement).

| Content type | Source in the original | Extension |
|---|---|---|
| Buildings, production, goods | `haeuser.cod` (`HAUS`, `HAUS_PRODTYP`, `HAUS_BAUKOST`) | custom records and production chains, custom goods list (no fixed number of goods) |
| Figures, animations | `figuren.cod`, `*.BSH` | custom figures, sprites from PNG atlases, any number of frames |
| Banners (colour + flags) | colours 0–6, flag sprites in `SHIP.BSH` | any number of banners (section 4) |
| Factions | hard-coded in the program: settlers, traders, pirates, natives | factions as data with AI behaviour and scaling rules (section 3) |
| Climate zones, landscapes | `NORD`, `SUED`, `*NAT`, island templates | custom climate zones, ground and vegetation sets |
| Islands | 677 templates (`.scp`) in fixed sizes | any size, custom templates, generator rules (section 5) |
| Music, sounds | `MUSIC8`, `SAMPLES`, `Speech8` | custom tracks, sounds per building/figure/event |
| Messages | `[MELDUNG]` in `Text.cod`, icons hard-coded | custom messages and changes to the original's: text per language, icon, sound, video (`messages.toml`, [messages.md](../modding/messages.md)) |
| Scenarios, campaigns | `.szs`/`.szm`, hard-wired goals | section 6 |

### 3. Players and factions

- `sim::PlayerId` is an index into the match's player list (8 bits). `sim::kNoPlayer` (0xFF) stands for
  "nobody". This leaves room for 255 slots (0 to 254). The game itself holds at most
  `sim::kMaxPlayers` (12) players with a start (`engine/sim/include/o1602/sim/limits.hpp`); the
  lobby, the start dialogs and the world generator use that.
- **Control and faction are separate.** A slot is controlled by a human, an AI or
  nobody (neutral). It belongs to a faction: settlers, traders, pirates, natives
  or a faction from a mod. `sim::PlayerRole` is the control (human, computer, neutral), `Player::faction`
  an index into the world's factions (`sim::World::factions()`, `sim::Faction`); a faction names the
  behaviour the simulation gives it (one of the original's four until scripts can give one,
  `sim::FactionBehaviour`). The world saves its factions with the game (section `FACT`), so a game keeps
  them whatever is installed later and every computer of a multiplayer game gets the host's.
- **Neutral factions scale with the player count.** Their data contains rules such as "pirates:
  ships = base value + k · settler players" or "traders: one route per n warehouses". This way the
  world adapts to five or eight players.
- Everything that applies per player (money, stock, statistics, diplomacy, discovery) lives in containers
  that grow with the player count. Matrices such as diplomacy have size *n × n*, not 7 × 7.

### 4. Banners: colours and flags

- A **banner** bundles a player's identifying features: colour (flag, minimap,
  ownership view, interface), flag type and later the colour variants of the soldiers. Players refer
  to a banner (`sim::BannerId`), not to a fixed colour number.
- **Player flags are a single flag that is tinted** (proposal by the project owner,
  2026-10-16). In the flag menu you choose one of twelve preset colours or any RGB colour, as
  in OpenRA; the engine tints the flag when loading the graphics (`render::TintedFlagSource`, a set
  of 8 images per banner and zoom level). New players need no new images.
- The template follows a simple rule: pure grey (R = G = B) is cloth, grey 128 is exactly the
  player colour, darker and lighter greys shade it. Everything else stays as it is
  (guide for modders: [docs/modding/flags.md](../modding/flags.md)).
- The base game makes the template from the white original flag. Red, blue, yellow and white are tinted in
  the colours determined from the original images; anyone who wants the original images switches
  them on (`BannerOptions::original_player_flags`, `--original-flags`).
- **Units in player colour** (soldiers, cavalry, cannoneers) follow the same rule. Because the four
  original versions are drawn individually and are not recolourings, red, blue, yellow and white keep the
  original images (`Banner::unit_variant`); every further colour tints a template generated at load time
  (`render::TintedUnitSource`). Soldiers only store their unit type, not the colour.
- Banners with their own motif keep finished images (`FlagStyle::Sprites`, e.g. the pirates'
  skull); natives and traders have no flag (`FlagStyle::None`).
- In a match each banner is assigned at most once. The menu warns about colours that are too similar.

### 5. World, islands and island generator

- **The world size is a property of the map** (`sim::World::width()/height()`). Original maps
  have 500 × 350 tiles.
- **Island limits** (`sim/limits.hpp`, [data-limits.md](../modding/data-limits.md)): a world is at
  most 4096 tiles on a side (`kMaxWorldSide`). Island indexes in the world and in commands, island
  numbers and sizes in the island header and in Open1602's own savegame are 16 bits (LC-2, LC-10):
  a world there holds up to 65535 islands, each up to the world's size. The original's files keep
  numbers and sizes in one byte (L-003), so a `.GAM` export refuses more, and the generator, which
  builds the original's island records, places at most 250 islands (`kMaxPlannedIslands`).
- **Island generator:** deterministic from a seed (multiplayer, repeatability). It
  works with height and moisture maps from noise functions and generates coasts, mountains,
  rivers, forests and raw materials from them. The rules (climate zone, size, shape, frequencies) are data.
  The original templates (`.scp`) remain as one source among several.
- Huge islands and maps must run smoothly. The renderer only draws the visible
  section (this is already done); the simulation works with regions instead of
  passes over the whole map.

### 6. Scenarios and campaigns

- **Scenario** = map (template or generator with seed) + starting conditions (players, banners,
  goods, buildings) + goals + optionally a script.
- **Campaign** = sequence of scenarios with transitions (conditions, carried-over values, texts, videos).
- **Scripts** are Lua with a deterministic, sandboxed interface: no file access,
  no clock time, randomness only from the game RNG. Events (tick, building, trade, combat) trigger them,
  and changes run as a `Command` (ADR 0004). The script state belongs to the savegame.
- The scenarios of the original are loaded; their hard-wired goals are translated into this model.

### 7. Multiplayer and savegames

- A game names the content packages it uses, with versions and checksums; the list is part of the
  savegame and the lobby. Every participant needs these packages, otherwise they cannot join.
  Packages a participant has installed in addition are not loaded for that game, so more mods on
  one computer never keep it out of a game (amended 2026-10-26).
- Savegames store ids (strings), not runtime indices. That way they stay valid
  when a package adds entries.

## Consequences

- Some places need more effort at first (tables instead of constants). In return, a mod changes
  data instead of code.
- New features are designed as data and tables first. Fixed numbers in engine code must be
  justified (a comment referring to this ADR).

## Fixed limits in the code and their removal

The open items of this ADR, in the order to work on them, are in
[docs/modding/open-work.md](../modding/open-work.md).

| Place | Limit | Status |
|---|---|---|
| `sim::MapCell::owner`, `sim::Ship::owner` | owner 0–6, 7 = nobody | ✅ `sim::PlayerId`, `kNoPlayer`; translation when loading |
| `sim::Player` | colour as `assets::PlayerColor` (0–6), kind as `assets::PlayerKind` | ✅ `BannerId` and `PlayerRole` |
| `render::player_rgb`, `FigureTable::flag_for_color` in the renderer | 7 fixed colours, 5 flags | ✅ `render::BannerTable` with tinted single flag, any colours |
| Overlay colour `DrawCommand::tint` | index into the 7 colours | ✅ RGBA directly in the command |
| `sim::World::kWidth/kHeight`, `Cell` with 8-bit island number and coordinates | 500 × 350, 255 islands, islands up to 255 × 255 | 🟡 size per world up to 4096², island indexes, numbers and sizes 16 bits in the own savegame (LC-2); 8 bits in the original's files and `.GAM`, the generator up to 250 islands |
| Buildings, figures | `.cod` numbers as index | ✅ named `<package>:<name>`, a package's own get the highest free numbers in load order and are kept by name in the savegame (`OWNB`, `OWNF`; [own-buildings.md](../modding/own-buildings.md), [own-figures.md](../modding/own-figures.md)); soldiers and ships of a package's own open |
| Goods | fixed list of the original | ✅ a package adds goods after the original's 58 (`goods.toml`, [goods.md](../modding/goods.md)), classes and unlock levels likewise ([classes.md](../modding/classes.md), [levels.md](../modding/levels.md)) |
| Factions, scaling of neutral players | – | ✅ factions as data in the world with their numbers (`factions.toml`, [factions.md](../modding/factions.md)); factions of a package's own take the base game's slots, and settlers or pirates can be left wholly to a script |
| Player count at game start, saving | 4 players at start, 7 players, 150 ships, 400 soldiers, 350 towers in the savegame | ⬜ the game supports 12 players (`sim::kMaxPlayers`); saving beyond the original limits is done with the own format `.o1602` ([ADR 0009](0009-own-save-format.md)) |
| Island generator | – | 🟡 stage 1: custom worlds from the original templates (200–4000 tiles); stage 2: islands of their own shape and size with slopes, rivers with bends, lakes, veins by their size, volcanoes and native villages, kinds of island world as data (`landscapes.toml`, [landscapes.md](../modding/landscapes.md)); island templates of a package's own (`islands.toml`, [islands.md](../modding/islands.md)); climates of a package's own open |
| Messages of the status bar | MELDUNG numbers 0–0x6F, texts only from `Text.cod` | ✅ `app::MessageTable`: the original's at their numbers, packages' after them, from `mods/<package>/messages.toml` ([messages.md](../modding/messages.md)); a package's id is its folder's name until manifests come |
| Public buildings of a house, slots of a computer's city | 16 bits of services (two free), 55 fixed slots | ✅ 64 bits of services, up to 50 public buildings of a kind of their own (`service = true`, [own-buildings.md](../modding/own-buildings.md)); slots named and changed by `slots.toml` ([computer-players.md](../modding/computer-players.md#the-slots)) |
| Numbers of the balance in the code | trader, castles, disasters as constants | ✅ `sim::Balance` in the world, changed by `balance.toml` ([balance.md](../modding/balance.md)) |
| Lua, campaigns, content packages | – | ✅ packages with a manifest, load order and the packages of each game, also as zip archives ([packages.md](../modding/packages.md)); Lua scripts ([scripts.md](../modding/scripts.md)); scenarios and campaigns ([scenarios.md](../modding/scenarios.md)) |
