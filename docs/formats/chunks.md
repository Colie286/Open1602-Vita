# Chunk container

Almost all binary files consist of a sequence of chunks:

```c
struct Chunk {
    char     name[16];   // NUL-terminated, rest undefined (often memory garbage)
    uint32_t size;       // length of the payload
    uint8_t  data[size];
};
```

There is no file header, no alignment between chunks and no checksum. The file ends
exactly after the last chunk (checked on all examined files).

Important: the bytes after the NUL in the name are not zeroed (e.g. `42 53 48 00 34 a0 40 00 …`
for `BSH`). Compare only up to the first NUL.

## Known chunk names

| Name | Occurs in | Contents |
|---|---|---|
| `BSH` | `.BSH` | sprite collection → [bsh.md](bsh.md) |
| `ZEI` | `.ZEI` | font glyphs (same format as BSH) |
| `COL` | `.COL` | palette → [col.md](col.md) |
| `INSEL3` / `INSEL4` / `INSEL5` | `.SCP .SZS .SZM .GAM` | island header (40 or 116 bytes) → [scp.md](scp.md) |
| `INSELHAUS` | ditto | tiles/buildings of an island, 8 bytes per entry |
| `HIRSCH2`, `PRODLIST2`, `WERFT`, `MILITAR`, `SIEDLER`, `ROHWACHS2`, `STADT4`, `KONTOR2`, `MARKT2` | scenarios, savegames | island-related game state (one set per island) |
| `SZENE_PLAYERMIN`, `SZENE_PLAYERMAX`, `SZENE_GAMEID`, `SZENE_MISSNR`, `SZENE_KAMPAGNE`, `SZENE_RANKING` | scenarios | 4 bytes each (u32); `SZENE_KAMPAGNE` is the campaign of a mission |
| `RANDTAB` | scenarios | 512 random numbers (u16) from which the world plan draws |
| `AUFTRAG4` | scenarios | 2244 bytes – mission goals |
| `SZENE`, `PLAYER4`, `HANDLER`, `SHIP4`, `SOLDAT3`, `TURM`, `TIMERS` | scenarios, savegames | global game state |

The appended digits (`INSEL5`, `PLAYER4`, …) are presumably version numbers of the structure.

## Tools

```sh
o1602-tool chunks "Szenes/Das Atoll.szs"
```
