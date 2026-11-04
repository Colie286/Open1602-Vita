// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_player.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

#include "video_queue.hpp"

namespace o1602::app {

namespace {

std::string video_path(std::uint16_t number, const char* extension) {
    return "VideoSmk/" + std::to_string(number) + extension;
}

/// The Königsedition's Smacker file, else the History Edition's Bink 2 file.
std::optional<std::filesystem::path> find_video(const assets::GameDirectory& game, std::uint16_t number) {
    if (auto smk = game.resolve(video_path(number, ".smk"))) {
        return smk;
    }
    return game.resolve(video_path(number, ".bk2"));
}

template <typename V>
constexpr bool kIsBink = std::is_same_v<V, video::Bink>;
template <typename V>
constexpr bool kIsSmacker = std::is_same_v<V, video::Smacker>;

std::uint64_t frame_us(const VideoPlayer::Clip& clip) {
    return std::visit(
        []<typename V>(const V& v) -> std::uint64_t {
            if constexpr (kIsBink<V>) {
                return v.info().frame_us();
            } else {
                return v.info().frame_us;
            }
        },
        clip);
}

std::uint32_t frames(const VideoPlayer::Clip& clip) {
    return std::visit([](const auto& v) { return v.info().frames; }, clip);
}

std::uint32_t position(const VideoPlayer::Clip& clip) {
    return std::visit([](const auto& v) { return v.position(); }, clip);
}

bool next(VideoPlayer::Clip& clip) {
    return std::visit([](auto& v) { return static_cast<bool>(v.next()); }, clip);
}

/// Opens the file and decodes the sound of the chosen tracks (bit i = track i, 0 = all).
core::Result<VideoPlayer::Clip> open_clip(const std::filesystem::path& file, std::uint8_t tracks,
                                          std::vector<std::shared_ptr<const audio::Pcm>>& sound) {
    const auto wanted = [tracks](std::size_t t) { return tracks == 0 || (t < 8 && (tracks & (1U << t)) != 0); };
    const auto add = [&sound](core::Result<audio::Pcm> pcm) {
        if (pcm && !pcm->samples.empty()) {
            sound.push_back(std::make_shared<const audio::Pcm>(std::move(*pcm)));
        }
    };
    std::string extension = file.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".bk2") {
        auto video = video::Bink::open_file(file);
        if (!video) {
            return video.error();
        }
        for (std::size_t t = 0; t < video->info().tracks.size(); ++t) {
            if (wanted(t)) {
                add(video->audio(t));
            }
        }
        return VideoPlayer::Clip{std::move(*video)};
    }
    if (extension == ".webm") {
        auto video = video::Webm::open_file(file);
        if (!video) {
            return video.error();
        }
        for (std::size_t t = 0; t < video->info().tracks.size(); ++t) {
            if (wanted(t)) {
                add(video->audio(t));
            }
        }
        return VideoPlayer::Clip{std::move(*video)};
    }
    auto video = video::Smacker::open_file(file);
    if (!video) {
        return video.error();
    }
    for (std::size_t t = 0; t < video::kSmackerTracks; ++t) {
        if (video->info().tracks[t].present && wanted(t)) {
            add(video->audio(t));
        }
    }
    return VideoPlayer::Clip{std::move(*video)};
}

}  // namespace

struct VideoPlayer::Loading {
    std::mutex mutex;
    bool done = false;
    std::optional<Clip> video;
    std::vector<std::shared_ptr<const audio::Pcm>> audio;
    std::string error;
};

VideoPlayer::VideoPlayer(SDL_Renderer* renderer, const assets::GameDirectory& game)
    : m_renderer(renderer), m_game(&game) {}

VideoPlayer::~VideoPlayer() {
    if (m_texture != nullptr) {
        SDL_DestroyTexture(m_texture);
    }
}

void VideoPlayer::set_package_videos(std::vector<std::filesystem::path> files) {
    m_package_videos = std::move(files);
    m_available.clear();
}

std::optional<std::filesystem::path> VideoPlayer::find(std::uint16_t number) const {
    if (number >= kFirstPackageVideo) {
        const std::size_t i = number - kFirstPackageVideo;
        if (i < m_package_videos.size()) {
            return m_package_videos[i];
        }
        return std::nullopt;
    }
    return find_video(*m_game, number);
}

bool VideoPlayer::available(std::uint16_t number) const {
    const auto it = m_available.find(number);
    if (it != m_available.end()) {
        return it->second;
    }
    const bool found = find(number).has_value();
    m_available.emplace(number, found);
    return found;
}

bool VideoPlayer::start(std::uint16_t number, bool loop, std::uint8_t tracks) {
    stop();
    const auto path = find(number);
    if (!path) {
        m_available[number] = false;
        return false;
    }
    auto state = std::make_shared<Loading>();
    // Reading 48 MB and decoding the sound stood the frames still for over 100 ms (FB-14). The
    // thread has its own copies; a stopped load ends unseen. The frames are read from the file as
    // they are shown, only the sound is decoded whole.
    std::thread([state, file = *path, tracks] {
        std::optional<Clip> video;
        std::vector<std::shared_ptr<const audio::Pcm>> sound;
        std::string error;
        if (auto opened = open_clip(file, tracks, sound); !opened) {
            error = opened.error().message;
        } else {
            video.emplace(std::move(*opened));
        }
        const std::lock_guard lock(state->mutex);
        state->video = std::move(video);
        state->audio = std::move(sound);
        state->error = std::move(error);
        state->done = true;
    }).detach();
    m_loading = std::move(state);
    m_number = number;
    m_loop = loop;
    return true;
}

VideoPlayer::Load VideoPlayer::poll(std::uint64_t now_ms) {
    if (!m_loading) {
        return Load::None;
    }
    {
        const std::lock_guard lock(m_loading->mutex);
        if (!m_loading->done) {
            return Load::Waiting;
        }
        if (!m_loading->video) {
            std::fprintf(stderr, "video %u: %s\n", static_cast<unsigned>(m_number), m_loading->error.c_str());
        } else {
            m_video = std::move(m_loading->video);
            m_audio = std::move(m_loading->audio);
        }
    }
    m_loading.reset();
    if (!m_video) {
        m_number = 0;
        return Load::Failed;
    }
    m_start_ms = now_ms;
    const auto [w, h, smooth] = std::visit(
        []<typename V>(const V& v) {
            if constexpr (!kIsSmacker<V>) {
                // The History Edition's and packages' WebM videos are large already: scaled smoothly.
                return std::tuple{static_cast<std::int32_t>(v.info().width), static_cast<std::int32_t>(v.info().height),
                                  true};
            } else {
                return std::tuple{static_cast<std::int32_t>(v.info().width),
                                  static_cast<std::int32_t>(v.info().display_height()), false};
            }
        },
        *m_video);
    if (m_texture == nullptr || m_texture_w != w || m_texture_h != h || m_smooth != smooth) {
        m_smooth = smooth;
        make_texture(w, h);
    }
    if (next(*m_video)) {
        m_dirty = true;
    }
    return Load::Ready;
}

void VideoPlayer::make_texture(std::int32_t w, std::int32_t h) {
    if (m_texture != nullptr) {
        SDL_DestroyTexture(m_texture);
    }
    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (m_texture != nullptr) {
        // DirectDraw doubled the frames without smoothing.
        SDL_SetTextureScaleMode(m_texture, m_smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }
    m_texture_w = w;
    m_texture_h = h;
}

void VideoPlayer::recreate_texture() {
    if (m_texture == nullptr) {
        return;
    }
    make_texture(m_texture_w, m_texture_h);
    m_dirty = m_video.has_value();
}

void VideoPlayer::release_texture() {
    if (m_texture != nullptr) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
}

void VideoPlayer::set_renderer(SDL_Renderer* renderer) {
    m_renderer = renderer;
    if (m_texture_w > 0 && m_texture_h > 0) {
        make_texture(m_texture_w, m_texture_h);
    }
    m_dirty = m_video.has_value();
}

void VideoPlayer::hold_until(std::uint64_t ms) noexcept {
    m_start_ms = std::max(m_start_ms, ms);
}

void VideoPlayer::stop() {
    m_video.reset();
    m_loading.reset();
    m_audio.clear();
    m_number = 0;
}

VideoPlayer::Step VideoPlayer::update(std::uint64_t now_ms) {
    if (!m_video) {
        return m_loading ? Step::Playing : Step::Ended;
    }
    if (now_ms < m_start_ms) {
        return Step::Playing;  // held on the first frame
    }
    const std::uint32_t count = frames(*m_video);
    const std::uint64_t per_frame = frame_us(*m_video);
    if (per_frame == 0) {
        stop();
        return Step::Ended;
    }
    const std::uint64_t elapsed_us = (now_ms - m_start_ms) * 1000;
    const std::uint64_t length_us = std::uint64_t{count} * per_frame;
    if (elapsed_us >= length_us) {
        if (!m_loop) {
            stop();
            return Step::Ended;
        }
        // The pause video starts again from its first frame.
        std::visit([](auto& v) { v.rewind(); }, *m_video);
        m_start_ms = now_ms;
        if (next(*m_video)) {
            m_dirty = true;
        }
        return Step::Restarted;
    }
    // Late frames are decoded (each builds on the one before) but only the last is shown.
    const auto due = static_cast<std::uint32_t>(std::min<std::uint64_t>(elapsed_us / per_frame + 1, count));
    while (position(*m_video) < due) {
        if (!next(*m_video)) {
            std::fprintf(stderr, "video %u: damaged frame %u\n", static_cast<unsigned>(m_number), position(*m_video));
            stop();
            return Step::Ended;
        }
        m_dirty = true;
    }
    return Step::Playing;
}

void VideoPlayer::draw_in(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    const SDL_FRect area{static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
                         static_cast<float>(height)};
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(m_renderer, &area);
    if (!m_video || m_texture == nullptr || width <= 0 || height <= 0) {
        return;
    }
    if (m_dirty) {
        std::visit([this](const auto& v) { v.to_rgba(m_rgba); }, *m_video);
        SDL_UpdateTexture(m_texture, nullptr, m_rgba.data(), m_texture_w * 4);
        m_dirty = false;
    }
    const double scale = std::min(static_cast<double>(width) / m_texture_w, static_cast<double>(height) / m_texture_h);
    const double w = std::floor(m_texture_w * scale);
    const double h = std::floor(m_texture_h * scale);
    const SDL_FRect dst{static_cast<float>(x + std::floor((width - w) / 2.0)),
                        static_cast<float>(y + std::floor((height - h) / 2.0)), static_cast<float>(w),
                        static_cast<float>(h)};
    SDL_RenderTexture(m_renderer, m_texture, nullptr, &dst);
}

void VideoPlayer::draw(std::int32_t width, std::int32_t height) {
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);
    if (!m_video || m_texture == nullptr) {
        return;
    }
    if (m_dirty) {
        std::visit([this](const auto& v) { v.to_rgba(m_rgba); }, *m_video);
        SDL_UpdateTexture(m_texture, nullptr, m_rgba.data(), m_texture_w * 4);
        m_dirty = false;
    }
    // Twice the size at 1024 pixels width (the original's stretch blit), growing with the window,
    // within it.
    const double vw = m_texture_w;
    const double vh = m_texture_h;
    double scale = 2.0 * static_cast<double>(width) / 1024.0;
    scale = std::min({scale, static_cast<double>(width) / vw, static_cast<double>(height) / vh});
    const double w = std::floor(vw * scale);
    const double h = std::floor(vh * scale);
    const SDL_FRect dst{static_cast<float>(std::floor((width - w) / 2.0)),
                        static_cast<float>(std::floor((height - h) / 2.0)), static_cast<float>(w),
                        static_cast<float>(h)};
    SDL_RenderTexture(m_renderer, m_texture, nullptr, &dst);
}

}  // namespace o1602::app
