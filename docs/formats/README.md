# File formats of Anno 1602

Status legend: ✅ verified (code + test against original data) · 🟡 partly · ⬜ open

| Format | Files | Status | Docs | Implementation |
|---|---|---|---|---|
| Chunk container | `.BSH .ZEI .COL .SCP .SZS .SZM .GAM` | ✅ | [chunks.md](chunks.md) | `engine/assets/chunk_file.*` |
| COD scripts | `haeuser.cod figuren.cod` | ✅ encoding, grammar, objects | [cod.md](cod.md) | `engine/assets/cod.*`, `cod_parser.*` |
| Texts | `Text.cod Editor.cod` | 🟡 encoding, sections; mapping of the lines only where needed (city names, player names) | [cod.md](cod.md#7-textcod--editorcod) | `engine/assets/text_table.*` |
| Palette | `TOOLGFX/STADTFLD.COL` | ✅ | [col.md](col.md) | `engine/assets/palette.*` |
| Sprites | `GFX/ MGFX/ SGFX/ TOOLGFX/ *.BSH` | ✅ | [bsh.md](bsh.md) | `engine/assets/sprite_sheet.*` |
| Fonts | `TOOLGFX/*.ZEI` | ✅ glyphs, metrics, colours, line wrapping | [bsh.md](bsh.md#zei-fonts) | `engine/assets/sprite_sheet.*`, `engine/ui/font.*` |
| Islands | `NORD/ SUED/ NORDNAT/ SUEDNAT/ NOKLIMA/ *.SCP` | ✅ tiles, attributes, island header | [scp.md](scp.md) | `engine/assets/island.*`, `engine/sim/island_map.*` |
| Scenarios / savegames | `Szenes/*.SZS *.SZM`, `SAVEGAME/*.GAM` | ✅ all chunks of the savegame read and written (byte for byte round trip); every field of `TIMERS`, `SZENE` and `AUFTRAG4` known or shown to be unused | [gam.md](gam.md) | `engine/assets/scenario.*`, `savegame.*`, `engine/sim/world.*`, `savegame_writer.*` |
| Open1602 savegame | `*.o1602` | ✅ (Open1602, own format) | [o1602-save.md](o1602-save.md) | `engine/sim/save_game.*` |
| Own chunk | `O1602`, `O1602MAP` in exported `.GAM` | ✅ (Open1602) | [o1602-chunk.md](o1602-chunk.md) | `engine/sim/savegame_extension.*` |
| Figures | `figuren.cod` (FIGUR) | 🟡 drawing, walking and combat data, `FORMATION` (chosen with the formation key, ADR 0010) | [gam.md](gam.md#how-ships-are-drawn) | `engine/assets/figure_table.*` |
| UI gadgets | `GADDATA/*.GAD`, `*.INC` | ✅ all 45 layouts of the game (not the editor's `Gadedit/`) | [gad.md](gad.md) | `engine/assets/gad.*`, `engine/ui/panel.*` |
| Videos | `VideoSmk/*.SMK`, `TOOLGFX/6s.smk` | ✅ Smacker 2 (image, palette, all audio types; `SMK4` only from the description) | [smk.md](smk.md) | `engine/video/smacker.*` |
| History Edition videos | `VideoSmk/*.bk2`, `Loca/<language>/VideoSmk/*.bk2` | 🟡 Bink 2 from revision `g` (all of the History Edition's), Bink Audio; older revisions and Bink 1 refused | [bk2.md](bk2.md) | `engine/video/bink.*` |
| Videos of content packages | `*.webm` of a package | ✅ (Open1602) AV1 with 8 bits and Opus in WebM | [webm.md](webm.md) | `engine/video/webm.*` |
| Audio | `SAMPLES/ Speech8/ MUSIC8/ *.wav` | ✅ PCM and the custom 4-bit music (an IMA variant) | [wav.md](wav.md) | `engine/audio/wav.*` |
| History Edition archive and texts | `ahe/a1he0.rda` (`data/texts.xml`), `Loca/<language>/` | 🟡 directory of stored blocks, texts.xml; the rest of the archive (its UI) is not used | [rda.md](rda.md) | `engine/assets/rda_archive.*`, `history_texts.*`, `game_directory.*` |

All binary formats are little-endian. Texts are Windows-1252.
