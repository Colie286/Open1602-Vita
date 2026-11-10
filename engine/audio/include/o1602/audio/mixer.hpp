// SPDX-License-Identifier: GPL-3.0-or-later
//
// Software mixer: sounds, speech and music into one stereo stream (replaces Maxsound.dll). It
// has no platform dependency: the frontend asks it for samples and hands them to the device
// (platform::AudioOutput), so it runs the same in tests.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "o1602/audio/wav.hpp"

namespace o1602::audio {

/// What a voice belongs to; each group has its own volume (the original's options).
enum class Group : std::uint8_t { Effects, Speech, Music };
inline constexpr std::size_t kGroups = 3;

struct PlayOptions {
    Group group = Group::Effects;
    float volume = 1.0F;  ///< 0..1
    float pan = 0.0F;     ///< -1 left .. 1 right
    bool loop = false;
};

class Mixer {
public:
    using VoiceId = std::uint32_t;
    static constexpr VoiceId kNoVoice = 0;

    /// `rate`: output frames per second; `voices`: how many sounds play at once at most.
    explicit Mixer(std::uint32_t rate = 44100, std::size_t voices = 32);

    [[nodiscard]] std::uint32_t rate() const noexcept { return m_rate; }

    /// Starts a sound. When all voices are busy, the oldest effect gives way; kNoVoice if none can.
    VoiceId play(std::shared_ptr<const Pcm> sound, const PlayOptions& options = {});
    void stop(VoiceId voice);
    void stop_group(Group group);
    [[nodiscard]] bool playing(VoiceId voice) const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> position_ms(VoiceId voice) const noexcept;
    [[nodiscard]] std::size_t playing_count(Group group) const noexcept;
    void set_volume(VoiceId voice, float volume, float pan);

    /// Volume of a group and of everything, 0..1.
    void set_group_volume(Group group, float volume) noexcept;
    [[nodiscard]] float group_volume(Group group) const noexcept { return m_group[static_cast<std::size_t>(group)]; }
    void set_master_volume(float volume) noexcept { m_master = clamp01(volume); }

    /// Mixes `frames` stereo frames (interleaved left, right) into `out`, overwriting it.
    void mix(std::int16_t* out, std::size_t frames);

private:
    struct Voice {
        std::shared_ptr<const Pcm> sound;
        VoiceId id = kNoVoice;
        Group group = Group::Effects;
        std::uint64_t pos = 0;   ///< source frame, 16.16 fixed point
        std::uint64_t step = 0;  ///< source frames per output frame, 16.16
        std::int32_t left = 0;   ///< gain, 1/4096
        std::int32_t right = 0;
        bool loop = false;
        std::uint64_t started = 0;  ///< order of starting, for stealing
    };
    static float clamp01(float v) noexcept { return v < 0.0F ? 0.0F : (v > 1.0F ? 1.0F : v); }
    void gains(Voice& v, float volume, float pan) const noexcept;

    std::uint32_t m_rate;
    std::vector<Voice> m_voices;
    std::array<float, kGroups> m_group{1.0F, 1.0F, 1.0F};
    float m_master = 1.0F;
    VoiceId m_next_id = 1;
    std::uint64_t m_started = 0;
    std::vector<std::int32_t> m_accum;
};

}  // namespace o1602::audio
