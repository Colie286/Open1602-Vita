// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bink 2 videos (RAD Game Tools, "KB2"; docs/formats/bk2.md): the History Edition of Anno 1602
// ships its videos so, in place of the Königsedition's Smacker files. The container holds a
// frame index; each frame carries one packet per audio track (Bink Audio) and the picture
// (YUV 4:2:0 in blocks of 32 x 32 pixels). The decoders are ports of free ones, see
// src/bink2_video.hpp and src/bink_audio.hpp.
//
// As Smacker it has no platform dependency: the frontend asks for the next frame and turns it into
// a texture, the audio tracks are handed to the mixer as a whole.
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <vector>

#include "o1602/audio/wav.hpp"
#include "o1602/core/result.hpp"

namespace o1602::video {

// Per-decoder-thread switch for a controlled scalar/SIMD comparison.
void bink_simd(bool enabled) noexcept;
bool bink_simd_verified() noexcept;
bool bink_simd_enabled() noexcept;

namespace bink {
class Bink2Video;
}

/// One audio track as the header describes it.
struct BinkTrack {
    std::uint32_t rate = 0;
    bool stereo = false;
    bool dct = false;  ///< DCT coded (else RDFT)
    std::uint32_t id = 0;
};

struct BinkInfo {
    char revision = 0;  ///< the letter after "KB2"
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frames = 0;
    std::uint32_t fps_num = 0;
    std::uint32_t fps_den = 1;
    std::uint32_t video_flags = 0;
    std::vector<BinkTrack> tracks;

    /// Microseconds per frame.
    [[nodiscard]] std::uint64_t frame_us() const noexcept {
        return fps_num == 0 ? 0 : std::uint64_t{fps_den} * 1'000'000 / fps_num;
    }
};

struct Yuv420View {
    struct Plane { std::span<const std::uint8_t> bytes; std::uint32_t stride=0; };
    std::uint32_t width=0,height=0;
    std::array<Plane,3> planes;
};

class Bink {
public:
    /// One persistent helper for independent slices; no extra frame planes.
    /// The optional callback configures that helper on its own thread.
    bool parallel_decode(bool enabled, std::function<void()> worker_setup = {});
    /// Reads the header and the frame index of a file in memory.
    [[nodiscard]] static core::Result<Bink> open(std::vector<std::uint8_t> bytes);
    /// The same from a file, whose frames are read one by one as they are decoded (the History
    /// Edition's intro has 66 MB).
    [[nodiscard]] static core::Result<Bink> open_file(const std::filesystem::path& path);

    Bink(Bink&&) noexcept;
    Bink& operator=(Bink&&) noexcept;
    ~Bink();

    [[nodiscard]] const BinkInfo& info() const noexcept { return m_info; }
    /// Index of the frame next() decodes, 0..frames.
    [[nodiscard]] std::uint32_t position() const noexcept { return m_position; }
    [[nodiscard]] bool finished() const noexcept { return m_position >= m_info.frames; }

    /// Decodes the next frame's picture. Fails at the end and on damaged data.
    core::Status next();
    struct FrameTiming { std::uint64_t read_us=0, decode_us=0; };
    [[nodiscard]] FrameTiming last_frame_timing() const noexcept { return m_timing; }
    /// Back to the start (the next frame is frame 0, a key frame).
    void rewind();

    /// The picture of the last decoded frame as RGBA, `width` x `height` (BT.601, video range).
    void to_rgba(std::vector<std::uint8_t>& out) const;
    /// Original BT.601 limited-range samples, valid until next()/rewind()/destruction.
    /// Alpha clips retain the RGBA path; no alpha is discarded.
    [[nodiscard]] std::optional<Yuv420View> yuv420() const noexcept;

    /// One audio track for the whole video as 16-bit PCM. Empty when the file has no such track.
    [[nodiscard]] core::Result<audio::Pcm> audio(std::size_t track, const std::function<bool(std::uint32_t,std::uint32_t)>& progress={}) const;
    /// Sequential bounded audio source. Wrap with audio::prefetch_pcm before
    /// handing it to the mixer; no file I/O occurs on the render/mixer thread.
    [[nodiscard]] core::Result<audio::Pcm> stream_audio(std::size_t track) const;

private:
    Bink();
    core::Status parse(std::span<const std::uint8_t> head, std::uint64_t size);
    /// The bytes of a frame (from a file: valid until the next call).
    [[nodiscard]] core::Result<std::span<const std::uint8_t>> frame_bytes(std::uint32_t frame) const;
    /// Where the picture starts in a frame: after the packets of the audio tracks.
    [[nodiscard]] core::Result<std::size_t> skip_audio(std::span<const std::uint8_t> frame) const;

    std::vector<std::uint8_t> m_bytes;          ///< the whole file (open), empty for open_file
    std::filesystem::path m_path;
    mutable std::uint64_t m_file_offset=~std::uint64_t{0};
    mutable std::ifstream m_file;               ///< open_file: the frames are read from here
    mutable std::vector<std::uint8_t> m_chunk;  ///< open_file: the frame read last
    BinkInfo m_info;
    std::vector<std::uint64_t> m_offsets;  ///< start of each frame
    std::vector<std::uint32_t> m_sizes;
    std::vector<bool> m_key;
    std::uint32_t m_position = 0;
    std::unique_ptr<bink::Bink2Video> m_video;
    FrameTiming m_timing;
};

}  // namespace o1602::video
