# WAV – sounds, speech, music

RIFF WAVE files in three folders.
Open1602: `audio::decode_wav` (`engine/audio/src/wav.cpp`).

| Folder | Contents | Format |
|---|---|---|
| `SAMPLES` (106) | sounds | PCM, mono, 8 bit (unsigned, silence 0x80), 22050 Hz (two unused ones at 11025 Hz) |
| `Speech8` (309) | voice messages, `<number>.wav` | PCM, mono, 8 bit, 22050 Hz |
| `MUSIC8` (21) | music, named after the title | custom 4-bit format, stereo, 22050 Hz (below) |

## 4-bit music

The `fmt ` block describes the **unpacked** stream (format tag 1 = PCM, 2 channels, 22050 Hz,
88200 bytes/s, block align 4) except for `wBitsPerSample = 4`: that is the marker for "packed". It is followed by
a `fact` block of 4 bytes ("M.K.", without meaning) and the `data` block.

The data is **one continuous stream** without blocks; the state starts at 0 at the beginning of the file and is
never reset. **One byte is a stereo pair**: the upper nibble left, the lower one right. Per channel:

```
step    = step_table[index]                           (the 89 values of IMA ADPCM, 7 … 32767)
index   = clamp(index + index_table[n], 0, 88)        (−1, −1, −1, −1, 2, 4, 6, 8, twice)
diff    = ((n & 4 ? 4·step : 0) + (n & 2 ? 2·step : 0) + (n & 1 ? step : 0)) >> 2
if n & 8: diff = −diff
value   = clamp(value + diff, −32768, 32767)          (16 bit)
```

Unlike the IMA reference code, the rounding term `step >> 3` is missing. With it, the
theme clips thousands of times and drifts away; without it (as here) it stays clean (checked on the whole "ANNO1602
Theme", 110 s). Length = data bytes / 22050 s.

`Maxsound.dll` unpacks into a ring buffer during playback (`adpcm_decoder 0x28101220`, tables at
`0x28105020`/`0x28105060`).
