# engine/assets

Loaders for the original formats (specification in [docs/formats](../../docs/formats/README.md)).
Depends only on `core`; every loader returns `core::Result`.

| Header | Format |
|---|---|
| `chunk_file.hpp`, `chunk_writer.hpp` | Chunk container (basis of almost all binary files), reading and writing |
| `cod.hpp` | Decrypting `.cod` scripts |
| `cod_parser.hpp` | `.cod` interpreter (symbols, expressions, objects, `ObjFill`) |
| `cod_predefined.hpp` | Constants the original game predefines for `haeuser.cod` and `figuren.cod` |
| `building_table.hpp` | Typed building and terrain types from `haeuser.cod`, lookup by id and sprite |
| `figure_table.hpp` | Figure definitions from `figuren.cod` (ships, soldiers, carriers, flags) |
| `text_table.hpp` | Texts of `Text.cod` by section |
| `game_data.hpp` | Numbers of the original's data the engine relies on (wares, building kinds, figures, prices), in one place |
| `palette.hpp` | `.COL` palette |
| `sprite_sheet.hpp` | `.BSH`/`.ZEI` sprites (RLE) |
| `gad.hpp` | Interface layouts `GADDATA/*.GAD` |
| `island.hpp` | Islands from `.SCP`/`.SZS`/`.SZM`/`.GAM`, attribute bits of the tiles |
| `scenario.hpp` | Players and ships of scenarios and savegames |
| `savegame.hpp` | Further game state chunks: cities, warehouse stock, production, deer |
| `high_scores.hpp` | High score lists and campaign progress |
| `game_directory.hpp` | Access to an original installation (`ANNO1602_DIR`) |
| `game_content.hpp` | The fingerprint of the data the simulation computes with and the mod packages in order, shared by game, tools and server |
| `data_fixes.hpp` | Corrections of the original's data, applied after loading and switchable off (ADR 0006) |
| `cursor_resource.hpp` | The original's mouse cursors from the resources of `Language.dll` |

The goal is that no input file crashes a loader; known gaps are tracked in the review findings.
