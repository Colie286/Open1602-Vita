// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <map>
#include <memory>
#include <vector>
#include <variant>
#include "o1602/assets/game_directory.hpp"
#include "o1602/audio/wav.hpp"
#include "o1602/video/bink.hpp"
#include "o1602/video/smacker.hpp"
struct SDL_Renderer;
struct SDL_Texture;
namespace o1602::app {
// One CPU decoder worker; three reusable frames, at most two waiting for presentation.
// The worker owns the original decoder and file. SDL textures remain on the render thread.
class VideoPlayer {
public:
    VideoPlayer(SDL_Renderer*,const assets::GameDirectory&);
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&)=delete;
    VideoPlayer& operator=(const VideoPlayer&)=delete;
    bool available(std::uint16_t) const;
    bool start(std::uint16_t,bool loop,std::uint8_t tracks);
    bool history_edition() const noexcept {return !m_game->localized_root().empty();}
    enum class Load : std::uint8_t {None,Waiting,Ready,Failed};
    Load poll(std::uint64_t now_ms);
    bool loading() const noexcept {return m_loading&&!m_ready;}
    const std::vector<std::shared_ptr<const audio::Pcm>>& audio() const noexcept {return m_audio;}
    void hold_until(std::uint64_t ms) noexcept;
    void sync_audio(std::optional<std::uint64_t> mixed_ms,unsigned queued_ms) noexcept;
    void stop();
    bool playing() const noexcept {return bool(m_loading);}
    std::uint16_t number() const noexcept {return m_number;}
    enum class Step : std::uint8_t {Playing,Restarted,Ended};
    Step update(std::uint64_t now_ms);
    void draw(std::int32_t width,std::int32_t height);
    void draw_in(std::int32_t x,std::int32_t y,std::int32_t width,std::int32_t height);
    void draw_notice(std::int32_t width,std::int32_t height);
    void presented(); // called only after SDL_RenderPresent returned
    void recreate_texture();
    void release_texture();
    void set_renderer(SDL_Renderer*);
    using Clip=std::variant<video::Smacker,video::Bink>;
private:
    struct Frame;
    struct Loading;
    struct Worker;
    bool begin(std::uint16_t,bool,std::uint8_t,const std::vector<std::shared_ptr<const audio::Pcm>>&);
    bool make_texture();
    bool upload();
    void draw_status(std::int32_t,std::int32_t,std::int32_t,std::int32_t);
    void failed(const char*,const char* phase="playback");
    void diagnose(const char* reason,unsigned due,std::uint64_t elapsed_us,std::uint64_t wall_us);
    SDL_Renderer* m_renderer;
    const assets::GameDirectory* m_game;
    mutable std::map<std::uint16_t,bool> m_available;
    std::unique_ptr<Worker> m_worker;
    std::shared_ptr<Loading> m_loading;
    std::unique_ptr<Frame> m_frame;
    std::vector<std::shared_ptr<const audio::Pcm>> m_audio;
    std::uint16_t m_number=0;
    std::uint8_t m_tracks=0;
    bool m_loop=false,m_ready=false,m_dirty=false,m_failure=false,m_drawn=false,m_presented=false;
    std::uint64_t m_start_ms=0,m_requested_ms=0,m_frame_us=0,m_last_progress_ms=0,m_error_until=0,m_last_decode_ms=0;
    std::optional<std::uint64_t> m_audio_time_us;
    std::uint64_t m_av_log_ms=0;
    std::uint64_t m_state_log_ms=0;
    std::uint32_t m_count=0,m_uploads=0,m_dropped=0,m_last_upload=0,m_seen_decoded=0,m_last_presented=0;
    SDL_Texture* m_texture=nullptr;
    std::int32_t m_texture_w=0,m_texture_h=0;
    bool m_missing_cd=false,m_yuv=false,m_test_texture=false,m_test_upload=false;
};
}
