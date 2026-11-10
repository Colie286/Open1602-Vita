// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sounds of the original: RIFF WAVE files (docs/formats/wav.md). SAMPLES and Speech8 hold
// 8-bit mono PCM, MUSIC8 4-bit stereo music.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::audio {

/// A bounded, read-only sample source. The mixer still owns timing, gains and resampling.
class SampleSource {
public:
    virtual ~SampleSource()=default;
    [[nodiscard]] virtual std::size_t frames() const noexcept=0;
    [[nodiscard]] virtual std::int16_t sample(std::size_t frame,std::uint8_t channel) const=0;
    [[nodiscard]] virtual std::size_t buffered_bytes() const noexcept=0;
    // Stable after publication; asynchronous sources report a permanent read failure.
    [[nodiscard]] virtual const char* error() const noexcept { return nullptr; }
    // Pin a cyclic range before mixing. False means data is not ready yet;
    // callers keep the voice position, rather than block or skip samples.
    [[nodiscard]] virtual bool prepare(std::size_t, std::size_t) const { return true; }
};

/// Decoded sound: 16-bit samples, interleaved when stereo.
struct Pcm {
    std::uint32_t rate = 22050;
    std::uint8_t channels = 1;
    std::vector<std::int16_t> samples;
    std::shared_ptr<const SampleSource> source;

    [[nodiscard]] std::size_t frames() const noexcept { return source?source->frames():channels==0?0:samples.size()/channels; }
    [[nodiscard]] std::int16_t sample(std::size_t frame,std::uint8_t channel) const {
        return source?source->sample(frame,channel):samples[frame*channels+channel];
    }
    /// Length in milliseconds.
    [[nodiscard]] std::uint64_t duration_ms() const noexcept {
        return rate == 0 ? 0 : std::uint64_t{frames()} * 1000 / rate;
    }
};

/// Decodes a WAVE file. Fails on damaged files and encodings it does not know.
[[nodiscard]] core::Result<Pcm> decode_wav(std::span<const std::uint8_t> bytes);
/// PCM8/PCM16 and the original's IMA-SSI music, decoded through the same IMA step routine,
/// keeping at most 64 KiB encoded/decoded samples. File is closed with the last source owner.
[[nodiscard]] core::Result<Pcm> stream_wav(const std::filesystem::path& path);
/// Bounded read-ahead on a worker. The source is owned/read only by that worker;
/// prepare/sample never perform file I/O and never wait for its mutex.
[[nodiscard]] Pcm prefetch_pcm(Pcm pcm,bool cyclic=true);
/// Opt-in QA: delay a single read after eight blocks; zero in normal runs.
void test_stream_delay(unsigned ms);

}  // namespace o1602::audio
