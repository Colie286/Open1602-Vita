// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/audio/sound_file.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <string>

#include "dr_flac.h"
#include "stb_vorbis.h"

namespace o1602::audio {

namespace {

using core::ErrorCode;
using core::make_error;

bool starts_with(std::span<const std::uint8_t> bytes, const char* magic) noexcept {
    const std::size_t n = std::strlen(magic);
    return bytes.size() >= n && std::memcmp(bytes.data(), magic, n) == 0;
}

core::Result<Pcm> decode_vorbis(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > static_cast<std::size_t>(INT_MAX)) {
        return make_error(ErrorCode::LimitExceeded, "Ogg Vorbis: the file is too large");
    }
    int error = 0;
    const std::unique_ptr<stb_vorbis, void (*)(stb_vorbis*)> vorbis(
        stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr), stb_vorbis_close);
    if (!vorbis) {
        return make_error(ErrorCode::InvalidFormat,
                          "Ogg Vorbis: damaged or no Vorbis stream (" + std::to_string(error) + ")");
    }
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis.get());
    if (info.channels < 1 || info.sample_rate == 0 || info.sample_rate > kMaxSoundRate) {
        return make_error(ErrorCode::Unsupported, "Ogg Vorbis: " + std::to_string(info.channels) + " channels at " +
                                                      std::to_string(info.sample_rate) + " Hz");
    }
    Pcm pcm;
    pcm.rate = info.sample_rate;
    pcm.channels = static_cast<std::uint8_t>(std::min(info.channels, 2));
    const std::size_t max_frames = std::size_t{pcm.rate} * kMaxSoundSeconds;
    constexpr int kChunk = 4096;
    std::vector<short> chunk(static_cast<std::size_t>(kChunk) * pcm.channels);
    while (pcm.frames() < max_frames) {
        // Vorbis gives the front channels first; more than two are mixed into them.
        const int frames = stb_vorbis_get_frame_short_interleaved(vorbis.get(), pcm.channels, chunk.data(),
                                                                  static_cast<int>(chunk.size()));
        if (frames <= 0) {
            break;
        }
        const std::size_t take = std::min(static_cast<std::size_t>(frames), max_frames - pcm.frames());
        pcm.samples.insert(pcm.samples.end(), chunk.begin(),
                           chunk.begin() + static_cast<std::ptrdiff_t>(take * pcm.channels));
    }
    if (pcm.samples.empty()) {
        return make_error(ErrorCode::InvalidFormat, "Ogg Vorbis: the stream holds no sound");
    }
    return pcm;
}

core::Result<Pcm> decode_flac(std::span<const std::uint8_t> bytes) {
    const std::unique_ptr<drflac, void (*)(drflac*)> flac(drflac_open_memory(bytes.data(), bytes.size(), nullptr),
                                                          drflac_close);
    if (!flac) {
        return make_error(ErrorCode::InvalidFormat, "FLAC: damaged or no FLAC stream");
    }
    if (flac->channels < 1 || flac->sampleRate == 0 || flac->sampleRate > kMaxSoundRate) {
        return make_error(ErrorCode::Unsupported, "FLAC: " + std::to_string(flac->channels) + " channels at " +
                                                      std::to_string(flac->sampleRate) + " Hz");
    }
    Pcm pcm;
    pcm.rate = flac->sampleRate;
    pcm.channels = static_cast<std::uint8_t>(std::min<unsigned>(flac->channels, 2));
    const std::size_t channels = flac->channels;
    const std::size_t max_frames = std::size_t{pcm.rate} * kMaxSoundSeconds;
    constexpr std::size_t kChunk = 4096;
    std::vector<drflac_int16> chunk(kChunk * channels);
    while (pcm.frames() < max_frames) {
        const auto frames = static_cast<std::size_t>(drflac_read_pcm_frames_s16(flac.get(), kChunk, chunk.data()));
        if (frames == 0) {
            break;
        }
        const std::size_t take = std::min(frames, max_frames - pcm.frames());
        for (std::size_t f = 0; f < take; ++f) {
            // FLAC's channel order starts with front left and right.
            for (std::size_t c = 0; c < pcm.channels; ++c) {
                pcm.samples.push_back(chunk[f * channels + c]);
            }
        }
    }
    if (pcm.samples.empty()) {
        return make_error(ErrorCode::InvalidFormat, "FLAC: the stream holds no sound");
    }
    return pcm;
}

}  // namespace

core::Result<Pcm> decode_sound(std::span<const std::uint8_t> bytes) {
    if (starts_with(bytes, "OggS")) {
        return decode_vorbis(bytes);
    }
    if (starts_with(bytes, "fLaC")) {
        return decode_flac(bytes);
    }
    if (starts_with(bytes, "RIFF")) {
        return decode_wav(bytes);
    }
    return make_error(ErrorCode::Unsupported, "not a sound file (WAV, Ogg Vorbis or FLAC)");
}

}  // namespace o1602::audio
