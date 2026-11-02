# SMK – Smacker videos

The game's videos are Smacker files (RAD Game Tools); the original plays them with
`smackw32.dll`. Open1602 has its own decoder, written from the public
format description ([MultimediaWiki: Smacker](https://wiki.multimedia.cx/index.php/Smacker)), without
third-party code and without a library: `video::Smacker` (`engine/video/src/smacker.cpp`).

## The game's files

| File | Image | Frames/s | Audio |
|---|---|---|---|
| `VideoSmk/*.smk` (51) | 352 × 240 (295: 352 × 216, 10: 432 × 352) | 15 (9: 25) | track 0 stereo, usually track 1 (mono or stereo), for 2500–2530 also track 2; 8 bit, 22050 Hz (9: 44100 Hz), all packed |
| `TOOLGFX/6s.smk` | 320 × 220 | 15 | track 0 stereo |

All are `SMK2` without ring frame, without interlacing and without line doubling; the palette changes
only one to four times per video (cross-fades are painted into the image itself). Even in the
debug build the decoder manages about one frame per millisecond.

## Structure

All numbers little-endian.

| Part | Size |
|---|---|
| Header | 0x68 bytes |
| Frame sizes | 4 bytes per frame (bit 0: keyframe, bit 1 unknown; clear both before use) |
| Frame types | 1 byte per frame (bit 0: palette, bit 1 + n: audio track n) |
| Huffman tables | `TreesSize` bytes, one bit stream |
| Frames | one after another, each frame with the size from the table |

If there is a ring frame (flag bit 0), both tables have one frame more than `Frames`.

### Header

| Offset | Field |
|---|---|
| 0x00 | `SMK2` or `SMK4` |
| 0x04 | width |
| 0x08 | height |
| 0x0C | frames |
| 0x10 | frame rate (signed): > 0 milliseconds per frame, < 0 hundred-thousandths of a second per frame, 0 = 10 frames/s. The game: −6666 (15 frames/s) |
| 0x14 | flags: bit 0 ring frame, bit 1 interlacing, bit 2 line doubling (either: double height on screen) |
| 0x18 | 7 × largest unpacked audio block per track |
| 0x34 | `TreesSize` |
| 0x38 | table sizes of the four 16-bit trees (image masks, two-colour colours, full blocks, block types), in bytes at 4 per entry |
| 0x48 | 7 × audio track: bit 31 packed, 30 present, 29 16 bit, 28 stereo, 27–26 other packing method (unknown, rejected), 0–23 sample rate |
| 0x64 | unused |

### Bit stream

Bits are read starting from the lowest bit of each byte; multi-bit values start with their lowest
bit.

**Byte tree** (8 bit): one bit "present"; if the tree is missing, reading from it gives 0 and consumes no
bits. Otherwise recursively: bit 1 = node (first the 0 branch, then the 1 branch), bit 0 = leaf with an 8-bit
value. Then a terminating 0 bit. When decoding, each bit read selects the branch (0 left,
1 right) until a leaf is reached; a tree consisting of only one leaf consumes no bits.

**16-bit tree**: bit "present" (otherwise: one leaf 0), byte tree of the low bytes, byte tree of the high
bytes, three 16-bit **markers**, then the tree as above, but each leaf is "low byte (read with the
first byte tree) | high byte (read with the second) << 8", then a terminating 0 bit. A leaf
with the value of a marker is a **cache leaf**: it holds no fixed value but one of the three
most recently read ones. At the start of each frame all three are 0. When the decoder reads a value that differs from the
contents of cache leaf 1, cache leaf 2 moves into 3, 1 into 2, and the new value goes into 1.
Markers without a leaf do not count.

The four 16-bit trees lie one after another in this order in the table area.

### A frame

In this order, depending on the frame type:

1. **Palette**: one byte length (size of the whole block including this byte, divided by 4), then commands
   until 256 entries are reached or the block ends (a started command at the end is padding):
   - `1ccccccc`: the next c + 1 entries stay as they were;
   - `01cccccc ssssssss`: copy c + 1 entries from the **previous** palette starting at entry s;
   - `00rrrrrr 00gggggg 00bbbbbb`: a new entry from 6-bit components, widened with 4·c + c/16
     (0 → 0, 16 → 65, 63 → 255).

   The order red, green, blue was checked against the game's videos (the source names blue first;
   with that, skin and parchment would be blue).
2. **Audio blocks** of tracks 0 … 6 with their bit set: 4 bytes length (including this field), for packed
   tracks 4 bytes unpacked length, then the data.
3. **Image**: the rest. The image consists of 4 × 4 blocks, row by row from the top left (width and height
   rounded up to whole blocks). It builds on the previous frame.

### Blocks

A value of the block type tree describes a chain: bits 0–1 type, bits 2–7 index of the chain length
(1 … 59, then 128, 256, 512, 1024, 2048), bits 8–15 extra. The chain reaches at most to the end of the image.

| Type | Name | Contents per block |
|---|---|---|
| 0 | two colours | value of the colour tree (high byte: colour of the 1 bits, low byte: of the 0 bits), value of the mask tree: 16 bits, bit i = pixel i (row by row, left to right) |
| 1 | full | per row two values of the full tree: the first fills pixels 2 and 3 (low byte left), the second pixels 0 and 1 |
| 2 | empty | stays as in the previous frame |
| 3 | solid | all pixels in the colour of the extra byte |

The order of the two values for full blocks was checked against the videos (the source says nothing
about it; reversed, all edges would be jagged). In `SMK4`, chains of full blocks start with bits for a
subtype: `0` as above, `10` double (one value fills two 2 × 2 fields side by side), `11` half (only
every second row, the one below repeats it). The game has no `SMK4` files; these subtypes
are implemented only from the description.

### Audio

Unpacked: PCM (8 bit unsigned or 16 bit). Packed (Huffman DPCM), one bit stream per block:

1. bit "data" (0: the block is silent), bit stereo, bit 16 bit (both must match the header);
2. byte trees: one per channel, for 16 bit two per channel (low, high byte; left before right);
3. start values, **right channel first**, for 16 bit the high byte first; they are the first sample;
4. then per sample and channel (left, right) a difference from the channel's tree (for 16 bit low
   and high byte combined into a 16-bit number), added to the previous value (with overflow), until the unpacked
   length is reached.

The game's tracks cover their videos to the tenth of a second (checked: 911, 282). Open1602 passes
a track as a whole to the mixer.

## Verification

- `o1602-tool smk <file> [--png FOLDER] [--every N] [--wav TRACK OUT.wav]`: header, decode all frames,
  frames as PNG, one track as WAVE.
- `tests/unit/test_video.cpp` builds small files bit by bit (all block types, chains, cache leaves,
  palette commands, the three audio types, damaged files); `tests/data/test_video_data.cpp` decodes
  a video of the game completely.
- Checked on 12 of the 51 videos and `6s.smk` (all frames, samples viewed).
