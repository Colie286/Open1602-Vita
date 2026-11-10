// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/platform/audio_output.hpp"

#include <SDL3/SDL.h>
#include "o1602/core/profile.hpp"

#include "o1602/platform/window.hpp"
#ifdef O1602_EXPERIMENTAL_VITA
#include "o1602/platform/vita.hpp"
#include <algorithm>
#include <cstdlib>
#endif

namespace o1602::platform {

core::Result<std::unique_ptr<AudioOutput>> AudioOutput::open(std::uint32_t rate) {
    // A small device buffer (about 12 ms at 44.1 kHz): the game keeps its own short queue ahead of
    // it, and a large one would only delay every sound. Drivers may round it up.
#ifdef O1602_EXPERIMENTAL_VITA
    vita::phase("BEGIN SDL audio init");
#endif
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return sdl_error("SDL_InitSubSystem(audio)");
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(rate);
#ifdef O1602_EXPERIMENTAL_VITA
    vita::phase("BEGIN SDL audio device stream");
#endif
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        auto error = sdl_error("SDL_OpenAudioDeviceStream");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return error;
    }
    SDL_ResumeAudioStreamDevice(stream);
#ifdef O1602_EXPERIMENTAL_VITA
    vita::trace("AUDIO_OPEN rate=%u channels=2 format=s16 buffer_frames=512",rate);
#endif
    return std::unique_ptr<AudioOutput>(new AudioOutput(stream, rate));
}

AudioOutput::~AudioOutput() {
#ifdef O1602_EXPERIMENTAL_VITA
    vita::trace("AUDIO_CLOSE frames=%llu nonzero_samples=%llu peak=%u queue_errors=%u",
        (unsigned long long)m_output_frames,(unsigned long long)m_nonzero_samples,m_peak,m_queue_errors);
#endif
    SDL_DestroyAudioStream(m_stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

std::size_t AudioOutput::queued_frames() const noexcept {
    const int bytes = SDL_GetAudioStreamQueued(m_stream);
#ifdef O1602_EXPERIMENTAL_VITA
    if(vita::logging_enabled()){
    if(bytes==0&&m_output_frames)++m_empty_observations;
    core::profile::gauge(core::profile::AudioQueuedFrames,bytes>0?unsigned(bytes)/4:0);
    core::profile::gauge(core::profile::AudioEmptyObservations,m_empty_observations);
    }
#endif
    return bytes > 0 ? static_cast<std::size_t>(bytes) / 4 : 0;
}

void AudioOutput::push(const std::int16_t* frames, std::size_t count) {
    if (count != 0) {
        const bool queued=SDL_PutAudioStreamData(m_stream, frames, static_cast<int>(count * 4));
#ifdef O1602_EXPERIMENTAL_VITA
        if(vita::logging_enabled()){
        if(!queued)++m_queue_errors;
        else {
            m_output_frames+=count;
            for(std::size_t i=0;i<count*2;++i) {
                m_nonzero_samples+=frames[i]!=0;
                m_peak=std::max(m_peak,unsigned(std::abs(int(frames[i]))));
            }
        }
        core::profile::gauge(core::profile::AudioQueueErrors,m_queue_errors);
        }
#else
        (void)queued;
#endif
    }
}

}  // namespace o1602::platform
