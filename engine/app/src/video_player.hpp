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
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "o1602/assets/game_directory.hpp"
#include "o1602/audio/wav.hpp"
#include "o1602/video/bink.hpp"
#include "o1602/video/smacker.hpp"
#include "o1602/video/webm.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace o1602::app {

class VideoPlayer {
public:
    VideoPlayer(SDL_Renderer* renderer, const assets::GameDirectory& game);
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
    [[nodiscard]] bool loading() const noexcept { return m_loading != nullptr; }
    /// The decoded sound of the video's tracks (for GameSound::start_video, also on a restart).
    [[nodiscard]] const std::vector<std::shared_ptr<const audio::Pcm>>& audio() const noexcept { return m_audio; }
    /// The first frame stays until `ms`: the video's sound comes after what the device holds.
    void hold_until(std::uint64_t ms) noexcept;
    void stop();
    /// A video plays or is being loaded.
    [[nodiscard]] bool playing() const noexcept { return m_video.has_value() || m_loading != nullptr; }
    [[nodiscard]] std::uint16_t number() const noexcept { return m_number; }

    enum class Step : std::uint8_t { Playing, Restarted, Ended };
    /// Decodes up to the frame of `now_ms`. The last frame stays for its whole time (the original
    /// closes on it, U-023); a looping video starts again (its sound must be restarted). While
    /// loading: Playing.
    Step update(std::uint64_t now_ms);
    /// Draws the current frame into a window of `width` x `height` pixels.
    void draw(std::int32_t width, std::int32_t height);
    /// Draws the current frame as large as it fits into a rectangle of the screen, on black (the
    /// minimap's window, ADR 0010).
    void draw_in(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
    /// The render device was reset: the texture is made again, the current frame uploaded anew.
    void recreate_texture();
    /// The device was lost: the texture goes before the renderer does ...
    void release_texture();
    /// ... and is made again with the new one, the current frame uploaded anew.
    void set_renderer(SDL_Renderer* renderer);

    /// A decoded video of either edition, or a package's WebM video.
    using Clip = std::variant<video::Smacker, video::Bink, video::Webm>;

private:
    /// The file of a video number: the installation's, or a package's.
    [[nodiscard]] std::optional<std::filesystem::path> find(std::uint16_t number) const;
    struct Loading;
    /// A texture of the video's size; the one before goes.
    void make_texture(std::int32_t w, std::int32_t h);
    SDL_Renderer* m_renderer;
    const assets::GameDirectory* m_game;
    mutable std::map<std::uint16_t, bool> m_available;
    std::vector<std::filesystem::path> m_package_videos;
    std::optional<Clip> m_video;
    /// The load under way; shared with its thread, which may outlive a stop().
    std::shared_ptr<Loading> m_loading;
    std::vector<std::shared_ptr<const audio::Pcm>> m_audio;
    std::uint16_t m_number = 0;
    bool m_loop = false;
    std::uint64_t m_start_ms = 0;
    SDL_Texture* m_texture = nullptr;
    std::int32_t m_texture_w = 0;
    std::int32_t m_texture_h = 0;
    bool m_smooth = false;  ///< the texture is scaled linearly (Bink), else without smoothing
    std::vector<std::uint8_t> m_rgba;
    bool m_dirty = false;
};

}  // namespace o1602::app
