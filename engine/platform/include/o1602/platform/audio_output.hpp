// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sound device: 16-bit stereo frames queued from the main loop (audio::Mixer makes them). No
// audio thread of our own: the frontend tops the queue up every frame, so nothing is shared
// between threads.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "o1602/core/result.hpp"

struct SDL_AudioStream;

namespace o1602::platform {

class AudioOutput {
public:
    /// Opens the default playback device. Fails if there is none (the game then runs silent).
    static core::Result<std::unique_ptr<AudioOutput>> open(std::uint32_t rate = 44100);
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    [[nodiscard]] std::uint32_t rate() const noexcept { return m_rate; }
    /// Frames waiting to be played.
    [[nodiscard]] std::size_t queued_frames() const noexcept;
    /// Queues interleaved stereo frames.
    void push(const std::int16_t* frames, std::size_t count);

private:
    AudioOutput(SDL_AudioStream* stream, std::uint32_t rate) : m_stream(stream), m_rate(rate) {}
    SDL_AudioStream* m_stream;
    std::uint32_t m_rate;
#ifdef O1602_EXPERIMENTAL_VITA
    std::uint64_t m_output_frames=0,m_nonzero_samples=0;
    mutable std::uint64_t m_empty_observations=0;
    unsigned m_peak=0,m_queue_errors=0;
#endif
};

}  // namespace o1602::platform
