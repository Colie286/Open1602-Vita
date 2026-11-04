# WebM videos of content packages (AV1 and Opus)

Not a format of the original: the modern, royalty-free video format content packages may use for
the videos of their messages, besides Smacker (`.smk`, [smk.md](smk.md)) and Bink 2 (`.bk2`,
[bk2.md](bk2.md)). Code: `engine/video/include/o1602/video/webm.hpp`, `engine/video/src/webm.cpp`.

## What is played

| Part | Played | Not played |
|---|---|---|
| Container | WebM and Matroska (`DocType` `webm` or `matroska`), clusters of known or unknown size, `SimpleBlock` and `BlockGroup` | laced pictures and laced sound packets, compressed or encrypted tracks (`ContentEncodings`) |
| Picture | the first video track, AV1 (`V_AV1`), 8 bits per component, 4:2:0, 4:2:2, 4:4:4 and grey, 1 to 4096 pixels wide and high | 10 and 12 bits, other codecs (VP8, VP9) |
| Sound | Opus tracks (`A_OPUS`), mono and stereo, or more channels with a channel mapping (the first two are kept) | Vorbis in WebM |
| Timing | a constant frame rate: the track's `DefaultDuration`, else the mean distance of the pictures' timestamps | a variable frame rate is played at its mean rate |

The pictures are decoded by **dav1d** and the sound by **libopus** (both in `third_party`, the
portable C code without assembly). The sound of a track is decoded whole when the video opens, at
48 kHz, without the encoder's delay (`pre-skip` of the `OpusHead`) and the padding at its end
(`DiscardPadding`), with the header's output gain. The pictures are read one by one from the file.

Colours: the matrix the sequence header names (BT.709, BT.601, BT.2020), else BT.601 below 720
lines and BT.709 from there; video range unless the header says full range.

## Making one

With ffmpeg (any build with an AV1 encoder and libopus):

```sh
ffmpeg -i in.mp4 -c:v libsvtav1 -crf 35 -pix_fmt yuv420p -c:a libopus -b:a 96k out.webm
```

(`libaom-av1` instead of `libsvtav1` works as well.) The decoder has no assembly, so a picture of
1280 × 720 is a good size; the message videos play in the minimap's window anyway.

`o1602-tool webm <file.webm> [--png DIR] [--every N] [--wav TRACK OUT.wav]` prints the header,
decodes every frame (with the time it took) and writes frames and a sound track.

## Elements read

`EBML` (`DocType`), `Segment`, `Info` (`TimestampScale`), `Tracks`, `TrackEntry` (`TrackNumber`,
`TrackType`, `CodecID`, `CodecPrivate`, `DefaultDuration`, `ContentEncodings`, `Video` with
`PixelWidth` and `PixelHeight`), `Cluster` (`Timestamp`, `SimpleBlock`, `BlockGroup` with `Block`
and `DiscardPadding`). Everything else (`SeekHead`, `Cues`, `Tags`, `Chapters`, `Void`, ...) is
skipped. A file cut short plays the frames before the cut.
