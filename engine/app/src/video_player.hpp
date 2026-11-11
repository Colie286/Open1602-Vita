// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shows the original's videos: the logos and the intro at the start,
// the videos of messages, victory and defeat, and the pause video. The picture fills the window
// in black with the video in the middle, twice as large as the original at 1024 pixels width and
// grown with the window, never larger than it; the sound goes to GameSound. One video at a time.
// The file is read, opened and its sound decoded on a thread of its own (FB-14): until then the
// window stays black, and the frames go on.
//
// The Königsedition has them as Smacker files (VideoSmk/<number>.smk), the History Edition as
// Bink 2 files of the same names (VideoSmk/<number>.bk2, docs/formats/bk2.md); a Smacker file
// comes first. Content packages may also bring WebM videos (AV1 and Opus, docs/formats/webm.md).
#pragma once
#include <cstdint>
#include <optional>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>
#include <variant>
#include "o1602/assets/game_directory.hpp"
#include "o1602/audio/wav.hpp"
#include "o1602/video/bink.hpp"
#include "o1602/video/smacker.hpp"
#include "o1602/video/webm.hpp"

struct SDL_Renderer;
struct SDL_Texture;
namespace o1602::app {
// One CPU decoder worker; three reusable frames, at most two waiting for presentation.
// The worker owns the original decoder and file. SDL textures remain on the render thread.
class VideoPlayer {
public:
    VideoPlayer(SDL_Renderer*,const assets::GameDirectory&);
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    /// Whether `VideoSmk/<number>.smk` or `.bk2` exists (the installation may miss some, the
    /// original names two that GOG does not have).
    [[nodiscard]] bool available(std::uint16_t number) const;
    /// The packages' video files (MessageTable::package_videos): index i is the video number
    /// kFirstPackageVideo + i.
    void set_package_videos(std::vector<std::filesystem::path> files);

    /// Begins to load a video in the background, with the sound of its tracks (bit i of `tracks`
    /// = track i, 0 = all); a running one stops. False if the file is missing. Until poll() says
    /// Ready the window is black.
    bool start(std::uint16_t number, bool loop, std::uint8_t tracks);
    /// The installation is the History Edition, which lacks the start's logos 8 and 9.
    [[nodiscard]] bool history_edition() const noexcept { return !m_game->localized_root().empty(); }
    enum class Load : std::uint8_t {
        None,     ///< nothing is loading
        Waiting,  ///< still being read
        Ready,    ///< the video plays from its first frame at `now_ms`; audio() holds its sound
        Failed,   ///< the file could not be read or is damaged (said on stderr); nothing plays
    };
    /// Every frame while loading(): whether the video has come.
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
    /// ... and is made again with the new one, the current frame uploaded anew.
    void set_renderer(SDL_Renderer* renderer);

    /// A decoded video of either edition, or a package's WebM video.
    using Clip = std::variant<video::Smacker, video::Bink, video::Webm>;

private:
    struct Frame;
    /// The file of a video number: the installation's, or a package's.
    [[nodiscard]] std::optional<std::filesystem::path> find(std::uint16_t number) const;
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
    std::vector<std::filesystem::path> m_package_videos;
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
