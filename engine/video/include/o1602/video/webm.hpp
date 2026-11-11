// SPDX-License-Identifier: GPL-3.0-or-later
//
// WebM videos with AV1 pictures and Opus sound (docs/formats/webm.md): the modern, royalty-free
// format content packages may use for their videos besides the originals' Smacker and Bink 2
// (docs/modding/messages.md). The container is read by our own code; the pictures are decoded by
// dav1d and the sound by libopus (third_party).
//
// As Smacker and Bink it has no platform dependency: the frontend asks for the next frame and turns
// it into a texture, the sound is handed to the mixer as a whole. Only videos of a constant frame
// rate with 8 bits per component are played; the sound is at most stereo (further channels are
// left out).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "o1602/audio/wav.hpp"
#include "o1602/core/result.hpp"

namespace o1602::video {

/// One Opus track as the file describes it.
struct WebmTrack {
    std::uint64_t number = 0;
    std::uint8_t channels = 0;  ///< as coded; the decoded sound has at most two
};

struct WebmInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frames = 0;
    std::uint64_t frame_us = 0;  ///< microseconds per frame
    std::vector<WebmTrack> tracks;
};

class Webm {
public:
    /// Reads the file in memory: its header, the index of the pictures and the sound packets.
    [[nodiscard]] static core::Result<Webm> open(std::vector<std::uint8_t> bytes);
    /// The same from a file, whose pictures are read one by one as they are decoded.
    [[nodiscard]] static core::Result<Webm> open_file(const std::filesystem::path& path);

    Webm(Webm&&) noexcept;
    Webm& operator=(Webm&&) noexcept;
    ~Webm();

    [[nodiscard]] const WebmInfo& info() const noexcept { return m_info; }
    /// Index of the frame next() decodes, 0..frames.
    [[nodiscard]] std::uint32_t position() const noexcept { return m_position; }
    [[nodiscard]] bool finished() const noexcept { return m_position >= m_info.frames; }

    /// Decodes the next frame's picture. Fails at the end and on damaged data.
    core::Status next();
    /// Back to the start (the next frame is frame 0).
    void rewind();

    /// The picture of the last decoded frame as RGBA, `width` x `height` (the matrix and range
    /// the file names; BT.601 below 720 lines and BT.709 from there when it names none).
    void to_rgba(std::vector<std::uint8_t>& out) const;

    /// One sound track for the whole video as 16-bit PCM at 48 kHz.
    [[nodiscard]] core::Result<audio::Pcm> audio(std::size_t track) const;

    struct Source;
    struct Picture;
    struct Frame {
        std::uint64_t offset = 0;
        std::uint32_t size = 0;
    };
    struct Packets;

private:
    Webm();
    core::Status parse();

    std::unique_ptr<Source> m_source;
    std::unique_ptr<Picture> m_picture;  ///< the decoder and its last picture
    WebmInfo m_info;
    std::vector<Frame> m_frames;
    std::vector<Packets> m_packets;  ///< per track of m_info.tracks
    std::uint32_t m_position = 0;
};

}  // namespace o1602::video
