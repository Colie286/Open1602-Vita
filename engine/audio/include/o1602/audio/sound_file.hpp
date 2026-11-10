// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sound files of any kind the game reads: the original's WAVE files (wav.hpp) and, for content
// packages, Ogg Vorbis and FLAC (docs/modding/sounds.md). The kind is told by the file's first
// bytes, not by its name.
#pragma once

#include <cstdint>
#include <span>

#include "o1602/audio/wav.hpp"
#include "o1602/core/result.hpp"

namespace o1602::audio {

/// The longest sound decoded: a music track of a package may be long, but not fill the memory.
inline constexpr std::uint32_t kMaxSoundSeconds = 20 * 60;
/// The highest sample rate read (the mixer resamples any rate to the device's).
inline constexpr std::uint32_t kMaxSoundRate = 192000;

/// Decodes a WAVE, Ogg Vorbis or FLAC file to 16-bit samples. A file of more than two channels
/// keeps the first two (front left and right); one longer than kMaxSoundSeconds is cut there.
[[nodiscard]] core::Result<Pcm> decode_sound(std::span<const std::uint8_t> bytes);

}  // namespace o1602::audio
