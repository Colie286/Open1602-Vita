// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/audio/mixer.hpp"

#include "o1602/core/profile.hpp"
#include <algorithm>
#include <cmath>

namespace o1602::audio {

namespace {

constexpr std::int32_t kUnity = 4096;  // gain 1.0

}  // namespace

Mixer::Mixer(std::uint32_t rate, std::size_t voices) : m_rate(std::max<std::uint32_t>(rate, 1)) {
    m_voices.resize(std::max<std::size_t>(voices, 1));
}

void Mixer::gains(Voice& v, float volume, float pan) const noexcept {
    // Constant power panning, so a sound passing the screen keeps its loudness.
    const float p = std::clamp(pan, -1.0F, 1.0F);
    const float angle = (p + 1.0F) * 0.25F * 3.14159265F;
    const float g = clamp01(volume) * m_group[static_cast<std::size_t>(v.group)] * m_master;
    // Centre is full volume on both sides (cos 45 deg scaled back to 1).
    v.left = static_cast<std::int32_t>(std::lround(g * std::cos(angle) * 1.41421356F * kUnity));
    v.right = static_cast<std::int32_t>(std::lround(g * std::sin(angle) * 1.41421356F * kUnity));
    v.left = std::min(v.left, kUnity);
    v.right = std::min(v.right, kUnity);
}

Mixer::VoiceId Mixer::play(std::shared_ptr<const Pcm> sound, const PlayOptions& options) {
    if (sound == nullptr || sound->frames() == 0 || sound->rate == 0) {
        return kNoVoice;
    }
    Voice* slot = nullptr;
    for (Voice& v : m_voices) {
        if (v.sound == nullptr) {
            slot = &v;
            break;
        }
    }
    if (slot == nullptr) {
        // The oldest effect makes room; speech and music are never cut off for an effect.
        for (Voice& v : m_voices) {
            if (v.group == Group::Effects && (slot == nullptr || v.started < slot->started)) {
                slot = &v;
            }
        }
        if (slot == nullptr) {
            return kNoVoice;
        }
    }
    Voice v;
    v.sound = std::move(sound);
    v.id = m_next_id++;
    if (m_next_id == kNoVoice) {
        m_next_id = 1;
    }
    v.group = options.group;
    v.step = (std::uint64_t{v.sound->rate} << 16) / m_rate;
    v.loop = options.loop;
    v.started = m_started++;
    gains(v, options.volume, options.pan);
    *slot = std::move(v);
    return slot->id;
}

void Mixer::stop(VoiceId voice) {
    for (Voice& v : m_voices) {
        if (v.id == voice && v.sound != nullptr) {
            v = Voice{};
        }
    }
}

void Mixer::stop_group(Group group) {
    for (Voice& v : m_voices) {
        if (v.sound != nullptr && v.group == group) {
            v = Voice{};
        }
    }
}

bool Mixer::playing(VoiceId voice) const noexcept {
    return voice != kNoVoice && std::any_of(m_voices.begin(), m_voices.end(),
                                            [&](const Voice& v) { return v.sound != nullptr && v.id == voice; });
}

std::size_t Mixer::playing_count(Group group) const noexcept {
    return static_cast<std::size_t>(std::count_if(
        m_voices.begin(), m_voices.end(), [&](const Voice& v) { return v.sound != nullptr && v.group == group; }));
}

void Mixer::set_volume(VoiceId voice, float volume, float pan) {
    for (Voice& v : m_voices) {
        if (v.id == voice && v.sound != nullptr) {
            gains(v, volume, pan);
        }
    }
}

void Mixer::set_group_volume(Group group, float volume) noexcept {
    // Applies to sounds started from now on and to the music on its next start; playing effects
    // are short.
    m_group[static_cast<std::size_t>(group)] = clamp01(volume);
}

std::optional<std::uint64_t> Mixer::position_ms(VoiceId voice) const noexcept {
    for(const auto& v:m_voices)if(v.id==voice&&v.sound&&v.sound->rate)
        return v.pos*1000/(std::uint64_t(v.sound->rate)<<16);
    return {};
}

void Mixer::mix(std::int16_t* out, std::size_t frames) {
    core::profile::Scope timer(core::profile::Mixer);
    m_accum.assign(frames * 2, 0);
    for (Voice& v : m_voices) {
        if (v.sound == nullptr) {
            continue;
        }
        const Pcm& s = *v.sound;
        const auto source_frames=s.frames();
        const std::uint64_t length = std::uint64_t{source_frames} << 16;
        if(length&&v.pos>=length&&!v.loop){v=Voice{};continue;}
        if(s.source&&length){
            const auto first=static_cast<std::size_t>((v.pos%length)>>16);
            const auto available=v.loop?source_frames:source_frames-first;
            if(!s.source->prepare(first,std::min(available,static_cast<std::size_t>((frames*v.step>>16)+2))))continue;
        }
        const bool stereo = s.channels == 2;
        for (std::size_t f = 0; f < frames; ++f) {
            if (v.pos >= length) {
                if (!v.loop || length == 0) {  // an empty looping sound has nothing to repeat
                    v = Voice{};
                    break;
                }
                v.pos %= length;
            }
            // Linear interpolation between neighbouring source frames.
            const auto i = static_cast<std::size_t>(v.pos >> 16);
            // 64 bit: a full-scale step (65535) times frac (up to 65535) overflows 32 bits.
            const auto frac = static_cast<std::int64_t>(v.pos & 0xFFFF);
            const std::size_t j = i + 1 < source_frames ? i + 1 : (v.loop ? 0 : i);
            std::int32_t l = 0;
            std::int32_t r = 0;
            if (stereo) {
                const std::int32_t l0 = s.sample(i,0);
                const std::int32_t r0 = s.sample(i,1);
                l = l0 + static_cast<std::int32_t>(((s.sample(j,0) - l0) * frac) >> 16);
                r = r0 + static_cast<std::int32_t>(((s.sample(j,1) - r0) * frac) >> 16);
            } else {
                const std::int32_t m0 = s.sample(i,0);
                l = r = m0 + static_cast<std::int32_t>(((s.sample(j,0) - m0) * frac) >> 16);
            }
            m_accum[2 * f] += (l * v.left) / kUnity;
            m_accum[2 * f + 1] += (r * v.right) / kUnity;
            v.pos += v.step;
        }
    }
    for (std::size_t i = 0; i < frames * 2; ++i) {
        out[i] = static_cast<std::int16_t>(std::clamp(m_accum[i], -32768, 32767));
    }
}

}  // namespace o1602::audio
