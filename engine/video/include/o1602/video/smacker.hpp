// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smacker videos (RAD Game Tools, "SMK2"/"SMK4"; docs/formats/smk.md): the original plays them
// with smackw32.dll. This decoder is written from the public format description: 8-bit frames of
// 4x4 blocks with Huffman-coded block types, colours and pixel maps, a palette that changes from
// frame to frame and up to seven audio tracks of Huffman-coded DPCM.
//
// It has no platform dependency: the frontend asks for the next frame and turns it into a
// texture, audio can be decoded in bounded packets for the existing mixer.
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <memory>
#include <vector>

#include "o1602/audio/wav.hpp"
#include "o1602/core/result.hpp"

namespace o1602::video {

namespace detail {
class BitReader;
}

inline constexpr std::size_t kSmackerTracks = 7;

/// One audio track as the header describes it.
struct SmackerTrack {
    bool present = false;     ///< the track has data
    bool compressed = false;  ///< Huffman DPCM (else plain PCM)
    bool stereo = false;
    bool bits16 = false;  ///< 16-bit samples (else 8-bit unsigned)
    std::uint32_t rate = 0;
};

struct SmackerInfo {
    std::uint8_t version = 2;  ///< 2 = SMK2, 4 = SMK4
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frames = 0;    ///< logical frames (without the ring frame)
    std::uint32_t frame_us = 0;  ///< microseconds per frame
    bool ring_frame = false;     ///< one more frame that leads back to the first (looping)
    bool interlaced = false;     ///< shown twice as high, every second line empty
    bool doubled = false;        ///< shown twice as high, every line twice
    std::array<SmackerTrack, kSmackerTracks> tracks{};

    /// Height of the picture on screen (twice the stored height for interlaced and doubled files).
    [[nodiscard]] std::uint32_t display_height() const noexcept { return interlaced || doubled ? height * 2 : height; }
};

class Smacker {
public:
    /// Reads the header and the Huffman tables; the frames are decoded one by one later.
    [[nodiscard]] static core::Result<Smacker> open(std::vector<std::uint8_t> bytes);
    /// The same from a file, of which only the header and the Huffman tables are read now: each
    /// frame is read when it is decoded, so a 48 MB video does not sit in memory whole (FB-14).
    [[nodiscard]] static core::Result<Smacker> open_file(const std::filesystem::path& path,std::size_t resident_limit=0);
    [[nodiscard]] static core::Result<Smacker> open_shared(std::shared_ptr<const std::vector<std::uint8_t>> bytes);
    [[nodiscard]] std::size_t resident_bytes() const noexcept {return m_shared?m_shared->size():0;}
    struct FrameTiming {std::uint64_t read_us=0,decode_us=0;};
    [[nodiscard]] FrameTiming last_frame_timing() const noexcept {return m_timing;}

    [[nodiscard]] const SmackerInfo& info() const noexcept { return m_info; }
    /// Index of the frame next() decodes, 0..frames.
    [[nodiscard]] std::uint32_t position() const noexcept { return m_position; }
    [[nodiscard]] bool finished() const noexcept { return m_position >= m_info.frames; }

    /// Decodes the next frame (palette and picture). Fails at the end and on damaged data; after a
    /// failure the picture is undefined until rewind().
    core::Status next();
    /// Back to the start (the next frame is frame 0, decoded from a black picture).
    void rewind();

    /// The picture of the last decoded frame, palette indices, `stride()` bytes per line (the
    /// width rounded up to whole blocks); lines below `height` are padding.
    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept { return m_frame; }
    [[nodiscard]] std::uint32_t stride() const noexcept { return m_stride; }
    /// Palette of the last decoded frame, 256 x RGB.
    [[nodiscard]] const std::array<std::uint8_t, 768>& palette() const noexcept { return m_palette; }
    /// Whether the last frame changed the palette.
    [[nodiscard]] bool palette_changed() const noexcept { return m_palette_changed; }

    /// The picture as RGBA, `width` x `display_height` (doubled or interlaced lines filled in).
    void to_rgba(std::vector<std::uint8_t>& out) const;

    /// One audio track for the whole video as 16-bit PCM (8-bit tracks are widened). Empty when
    /// the track has no data.
    /// Independent, bounded audio packet reader.
    [[nodiscard]] core::Result<audio::Pcm> stream_audio(std::size_t track) const;

    [[nodiscard]] core::Result<audio::Pcm> audio(std::size_t track, const std::function<bool(std::uint32_t,std::uint32_t)>& progress={}) const;

private:
    /// Huffman tree of 16-bit values with the format's three cached "recent value" leaves.
    struct BigTree {
        std::vector<std::uint32_t> nodes;  ///< leaf: value; node: kNode | index of the 1 branch
        std::array<std::size_t, 3> recent{};
        void reset() noexcept;
        std::uint32_t decode(detail::BitReader& in) noexcept;
    };

    Smacker() = default;
    /// Header, frame table and trees from `head` (the start of a file of `size` bytes).
    core::Status parse(std::span<const std::uint8_t> head, std::uint64_t size);
    core::Status decode_palette(std::span<const std::uint8_t> chunk);
    core::Status decode_video(std::span<const std::uint8_t> chunk);
    /// The bytes of a physical frame (from a file: valid until the next call).
    [[nodiscard]] core::Result<std::span<const std::uint8_t>> frame_bytes(std::uint32_t frame) const;

    std::vector<std::uint8_t> m_bytes;          ///< the whole file (open), empty for open_file
    std::shared_ptr<const std::vector<std::uint8_t>> m_shared;
    FrameTiming m_timing;
    mutable std::uint64_t m_file_offset=~std::uint64_t{0};
    std::filesystem::path m_path;
    mutable std::ifstream m_file;               ///< open_file: the frames are read from here
    mutable std::vector<std::uint8_t> m_chunk;  ///< open_file: the frame read last
    SmackerInfo m_info;
    std::vector<std::uint64_t> m_offsets;  ///< start of each physical frame
    std::vector<std::uint32_t> m_sizes;
    std::vector<std::uint8_t> m_types;
    std::array<BigTree, 4> m_trees;  ///< pixel maps, mono colours, full blocks, block types
    std::uint32_t m_position = 0;
    std::uint32_t m_stride = 0;
    std::uint32_t m_lines = 0;  ///< height rounded up to whole blocks
    std::vector<std::uint8_t> m_frame;
    std::array<std::uint8_t, 768> m_palette{};
    bool m_palette_changed = false;
};

}  // namespace o1602::video
