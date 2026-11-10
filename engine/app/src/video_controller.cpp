// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_controller.hpp"

#include <SDL3/SDL.h>
#include <cstdio>

#include "game_sound.hpp"
#include "hud.hpp"

#include "o1602/platform/window.hpp"

namespace o1602::app {

VideoController::VideoController(std::unique_ptr<VideoPlayer> player, GameSound& sound)
    : m_player(std::move(player)), m_sound(sound) {}

bool VideoController::available(std::uint16_t number) const {
    return m_player && m_player->available(number);
}

void VideoController::request(std::uint16_t number, std::uint8_t tracks, bool loop, bool intro, bool in_frame) {
    if (!m_player) {
        return;
    }
    VideoRequest r;
    r.number = number;
    r.tracks = tracks;
    r.loop = loop;
    r.intro = intro;
    r.in_frame = in_frame;
    if (m_queue.request(r, m_player->playing()) && m_player->playing()) {
        stop();
    }
}

void VideoController::follow(std::uint16_t number) {
    if (!m_player) {
        return;
    }
    VideoRequest r;
    r.number = number;
    r.follows = true;
    m_queue.request(r);
}

bool VideoController::frame(std::uint64_t now_ms, bool minimap_shown) {
    // Messages of the scenery and the sound itself may ask for a video too.
    if (const auto n = m_sound.take_video()) {
        request(*n);
    }
    if (!m_player) {
        return false;
    }
    bool video = m_player->playing() || start_next(minimap_shown);
    while (video && m_player->loading()) {
        switch (m_player->poll(now_ms)) {
            case VideoPlayer::Load::None:
            case VideoPlayer::Load::Waiting:
                return true;  // the player draws its loading status while the worker prepares
            case VideoPlayer::Load::Ready:
                // The picture waits for the sound, which plays after what the device holds.
                m_sound.prefill();
                m_sound.start_video(m_player->audio());
                m_player->hold_until(platform::ticks_ms() + m_sound.queued_ms());
                break;
            case VideoPlayer::Load::Failed:
                // A damaged file goes on with the next one, as a missing one does.
                video = start_next(minimap_shown);
                break;
        }
    }
    if (video) {
        m_player->sync_audio(m_sound.video_position_ms(),m_sound.queued_ms());
        switch (m_player->update(now_ms)) {
            case VideoPlayer::Step::Playing:
                break;
            case VideoPlayer::Step::Restarted:
                m_sound.start_video(m_player->audio());
                break;
            case VideoPlayer::Step::Ended:
                m_sound.stop_video();
                video = start_next(minimap_shown);
                break;
        }
    }
    return video;
}

bool VideoController::start_next(bool minimap_shown) {
    while (m_player) {
        const auto r = m_queue.take_next();
        if (!r) {
            break;
        }
        // A missing file goes on with the next one (the original drops the rest of the chain, U-022).
        // The file loads in the background; frame() starts the sound once it is there.
        if (m_player->start(r->number, r->loop, r->tracks)) {
            m_queue.started(*r, minimap_shown);
            return true;
        }
        // The History Edition has no logos (8, 9) before the intro: that is no fault to report.
        if (!(r->intro && m_player->history_edition())) {
            std::fprintf(stderr, "video %u is missing\n", static_cast<unsigned>(r->number));
        }
    }
    return false;
}

void VideoController::stop() {
    if (m_player) {
        m_player->stop();
    }
    m_sound.stop_video();
}

void VideoController::reset() {
    stop();
    m_queue.clear();
    m_pause.reset();
}

void VideoController::draw(std::int32_t width, std::int32_t height) {
    if (m_player) {
        m_player->draw(width, height);
    }
}

void VideoController::draw_in(const ui::Rect& area) {
    if (m_player) {
        m_player->draw_in(area.x, area.y, area.w, area.h);
    }
}

void VideoController::draw_notice(std::int32_t w,std::int32_t h){if(m_player)m_player->draw_notice(w,h);}

void VideoController::presented() {if(m_player)m_player->presented();}

void VideoController::recreate_texture() {
    if (m_player) {
        m_player->recreate_texture();
    }
}

void VideoController::release_texture() {
    if (m_player) {
        m_player->release_texture();
    }
}

void VideoController::set_renderer(SDL_Renderer* renderer) {
    if (m_player) {
        m_player->set_renderer(renderer);
    }
}

VideoController::Input VideoController::route(const SDL_Event& event, const Hud* hud) {
    if (full_screen()) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_RENDER_DEVICE_RESET:
            case SDL_EVENT_RENDER_DEVICE_LOST:
                return Input::Game;
            case SDL_EVENT_KEY_DOWN:
                if(m_queue.now().intro) {
                    m_queue.skip_start();
                    stop();
                    return Input::Taken;
                }
                if (event.key.key == SDLK_PAUSE || (event.key.key == SDLK_ESCAPE && m_pause.on())) {
                    return Input::TogglePause;
                }
                if (event.key.key == SDLK_ESCAPE) {
                    m_queue.skip_start();
                    stop();
                }
                return Input::Taken;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if(m_queue.now().intro && event.button.button==SDL_BUTTON_LEFT) {
                    m_queue.skip_start();
                    stop();
                }
                return event.button.button == SDL_BUTTON_LEFT ? Input::LeftReleased : Input::Taken;
            default:
                return Input::Taken;  // clicks on the game are swallowed while a video plays
        }
    }
    if (in_frame() && (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                       (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))) {
        stop();  // the credits end with a click or Escape
        return Input::Taken;
    }
    if (in_minimap() &&
        ((event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) ||
         (hud != nullptr && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          hud->minimap_area().contains(static_cast<std::int32_t>(event.button.x),
                                      static_cast<std::int32_t>(event.button.y))))) {
        stop();  // Circle/Escape or a click on the video ends it, including preparation
        return Input::Taken;
    }
    return Input::Game;
}

bool VideoController::pause_video_playing() const noexcept {
    return playing() && m_player->number() == kPauseVideo && m_queue.now().loop;
}

bool VideoController::toggle_pause(bool game_paused) {
    const bool on = m_pause.toggle(game_paused);
    if (on) {
        if (available(kPauseVideo)) {
            request(kPauseVideo, 0, true);
        }
    } else if (pause_video_playing()) {
        stop();
    }
    return on;
}

void VideoController::follow_pause(bool game_paused) {
    if (m_pause.ended_elsewhere(game_paused) && pause_video_playing()) {
        stop();
    }
}

}  // namespace o1602::app
