# COL – palette

`TOOLGFX/STADTFLD.COL` (1044 bytes) = one chunk `COL` with 1024 bytes of payload:

```c
struct ColEntry { uint8_t r, g, b, unused; };   // unused is always 0
ColEntry palette[256];
```

Entry 0 is black. The same palette applies to all sprites from `GFX/`, `MGFX/`, `SGFX/`
and `TOOLGFX/` (checked visually: terrain, buildings, ships, carriers, main menu).

Transparency is not represented by a palette entry but by the RLE encoding of the sprites
(see [bsh.md](bsh.md)).

**No palette animation.** The original sets the palette only when the display mode is set and when
the screen is restored (full screen all 256 entries, in a window entries 10–245, the rest are
Windows' system colours), never per frame. Moving water, fire and smoke are sprite animations.
