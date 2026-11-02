# BSH – sprite collections

One chunk `BSH` (or `ZEI` for fonts). Payload:

```c
uint32_t offsets[n];      // offset of each sprite relative to the start of the payload
                          // n is not stored: the table ends where the
                          // first (lowest) sprite record begins -> n = min(offsets)/4
// followed by the records:
struct SpriteRecord {
    uint32_t width;
    uint32_t height;
    uint32_t type;        // 1 or 6: RLE, 2: raw, 13: true colour RLE (History Edition, below)
    uint32_t length;      // size of the record incl. these 16 bytes, rounded up to 4 bytes
    uint8_t  rle[];       // see below
};
```

Several table entries can point to the same record (e.g. the first four entries of
`GFX/STADTFLD.BSH`), and the offsets are not monotonically increasing.

`length` is exact in all 47 294 distinct records of the original's 61 `.bsh`/`.zei` files:
16 + the RLE bytes up to and including `0xFF`, padded to 4 bytes with `0xFE`. Records need
not touch: in `MAEHER`, `SOLDAT`, `TIERE`, `TRAEGER` and part of `SHIP` 16 unreferenced
bytes lie between two of them. Open1602 refuses a record whose `length` is below 16 or
leaves the chunk, and decodes nothing beyond it (HI-8).
A sprite's height is no bound on its data: transparent rows at the bottom get no `0xFE`, and
the 100 empty sprites are just `0xFF`. The original claims at most 3 418 pixels per RLE byte;
Open1602 refuses more than 16 384 (HI-7).

## Record type

The blitters test the low byte of `type`:

| Type | Data | Drawn by |
|---|---|---|
| 1, 6 | RLE (below) | every blitter |
| 2 | `width × height` palette indices row by row, index 0 transparent | only the plain blitter; the recolouring one (owner colours, shading) skips it |
| 13 | true colour RLE (below), History Edition only | – (the History Edition's own renderer) |
| other | – | nothing |

Every record of the original's files has type 1. Open1602 decodes types 1, 6, 2 (type 2 also
when recoloured) and 13, and gives a transparent image for any other type. A type 2 record shorter
than its pixels is refused.

### Type 13: the History Edition's true colour sprites

Ubisoft's *Anno 1602 History Edition* redrew the main menu: in its `ToolGfx/START.BSH` (and the
one of each language in `Loca/<language>/ToolGfx/`) the background and the buttons (sprites 0 and
2–13) have type 13, and so have five new buttons of `TOOLS.BSH` (513–517). The data is the RLE
below in little-endian 32-bit words instead of bytes, each pixel one word with the bytes blue,
green, red, 0:

```
loop:
    w = next uint32
    0xFF        -> end of image
    0xFE        -> new row
    otherwise   -> skip w transparent pixels,
                   n = next uint32, then n pixels of 4 bytes (B, G, R, 0)
```

`length` is exact here too. The fourth byte is 0 in every pixel of both files. The History
Edition's START.BSH has two sprites more than the Königsedition's (a sixth main menu button and
its pressed image), TOOLS.BSH five more from 513 on; its GAD files are changed to match.

## RLE encoding

```
loop:
    b = next byte
    0xFF        -> end of image
    0xFE        -> new row (x = 0, y += 1)
    otherwise   -> skip b transparent pixels,
                   n = next byte, then n palette indices (opaque pixels)
```

A row can contain several `[skip][n][pixel…]` sequences. Pixels outside
`width × height` never occur in BSH; in some ZEI glyphs they do (see below).

## Zoom levels and contents

| Folder | Zoom | Ground tile |
|---|---|---|
| `GFX/` | large | 64 × 31 |
| `MGFX/` | medium | (halved) |
| `SGFX/` | small | (quartered) |

`STADTFLD.BSH` contains 5964 sprites in all three levels (terrain, coasts, rivers, buildings in all
build levels/rotations). The indices correspond to the `Gfx` values from `haeuser.cod`
(`GFXBODEN = 0`, `GFXHANG = GFXBODEN+432`, …). Further files: `SHIP` (368), `TRAEGER` (1784),
`SOLDAT`, `TIERE`, `FISCHE`, `EFFEKTE`, `GAUKLER`, `MAEHER`, `SCHATTEN`, `NUMBERS`;
in `TOOLGFX/`: `START` (main menu, 90), `TOOLS` (interface), `BAUHAUS`, `BAUSHIP`, `EDITOR`, `SYMBOL`
with variants `*6`/`*8` (presumably for 640×480 and 800×600 respectively).

Verified by: the C++ decoder decodes all sprites of `GFX/MGFX/SGFX STADTFLD`, `SHIP`, `TRAEGER`,
`START` without errors (`tests/data/test_original_data.cpp`); visual check of the exported PNGs.

## ZEI (fonts)

Same format, chunk name `ZEI`. **Glyph = code − 0x20** (Windows-1252, 224 glyphs; the large fonts
ZEI28V/ZEI24V only 33: codes 32–64). All glyphs of a font have the same height (the line height); the
advance is the width, without spacing. Individual glyphs (e.g. index 14 in `ZEI20V.ZEI`) set one or two
pixels beyond `width`: they overlap the next glyph (the original clips nothing;
`SpriteSheet::decode(i, extra_width)` keeps them). The pixels only use palette indices 1 (body)
and 7 (shadow), which are recoloured when drawing. Open1602: `ui::Font`.

## Tools

```sh
o1602-tool bsh-info GFX/SHIP.BSH
o1602-tool bsh-export GFX/TRAEGER.BSH out/ --sheet --count 96 --palette TOOLGFX/STADTFLD.COL
```
