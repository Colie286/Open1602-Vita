// SPDX-License-Identifier: GPL-3.0-or-later
//
// The videos of the running program: the logos and the intro at the start,
// the videos of messages, victory and defeat, the credits and the pause. Decides with VideoQueue
// which one plays and where (the whole window, the minimap's window, the main menu's picture
// frame), plays it with VideoPlayer and its sound with GameSound, and takes the input while one
// fills the window.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "video_player.hpp"
#include "video_queue.hpp"

#include "o1602/ui/panel.hpp"

union SDL_Event;

namespace o1602::app {

class GameSound;
class Hud;

class VideoController {
public:
    /// `player` null: no videos (--no-videos); requests are then ignored. `sound` outlives this.
    VideoController(std::unique_ptr<VideoPlayer> player, GameSound& sound);
    VideoController(const VideoController&) = delete;
    VideoController& operator=(const VideoController&) = delete;

    [[nodiscard]] bool enabled() const noexcept { return m_player != nullptr; }
    /// Whether the installation has `VideoSmk/<number>.smk` (or the History Edition's `.bk2`).
    [[nodiscard]] bool available(std::uint16_t number) const;
    /// The packages' video files (MessageTable::package_videos), from kFirstPackageVideo on.
    void set_package_videos(const std::vector<std::filesystem::path>& files);

    /// A new request replaces the running video and the waiting ones, `intro` ones only queue after
    /// each other (VideoQueue::request).
    void request(std::uint16_t number, std::uint8_t tracks = 0, bool loop = false, bool intro = false,
                 bool in_frame = false);
    /// Queues `number` to play after the running and the waiting videos (the original's chains).
    void follow(std::uint16_t number);
    /// Every frame: the video the sound asked for, the next waiting one, the running one decoded up
    /// to `now_ms`. True while a video plays. `minimap_shown`: a minimap is on screen, for where a
    /// video that starts now plays.
    bool frame(std::uint64_t now_ms, bool minimap_shown);
    void stop();
    /// A new game: no video of the old one plays or waits, and its pause is over.
    void reset();

    [[nodiscard]] bool playing() const noexcept { return m_player && m_player->playing(); }
    /// A video plays or waits (the end screen waits for them).
    [[nodiscard]] bool busy() const noexcept { return playing() || m_queue.waiting(); }
    /// The running video plays in the minimap's window.
    [[nodiscard]] bool in_minimap() const noexcept { return playing() && m_queue.now().in_minimap; }
    /// ... in the main menu's picture frame.
    [[nodiscard]] bool in_frame() const noexcept { return playing() && m_queue.now().in_frame; }
    /// The running video fills the window: the game behind it stands still.
    [[nodiscard]] bool full_screen() const noexcept { return playing() && !in_minimap() && !in_frame(); }

    /// Draws the running video over the whole window of `width` x `height` pixels.
    void draw(std::int32_t width, std::int32_t height);
    /// ... into a rectangle of the screen (the minimap's window, the menu's picture frame).
    void draw_in(const ui::Rect& area);
    /// The render device was reset: the texture is made again.
    void recreate_texture();
    /// The render device was lost: the texture goes before the renderer does, and comes again
    /// from the new one.
    void release_texture();
    void set_renderer(SDL_Renderer* renderer);

    /// What an input event does while a video is on screen.
    enum class Input : std::uint8_t {
        Game,          ///< goes on to the game as usual
        TogglePause,   ///< the Pause key (or Escape in the pause): toggle_pause() with the game's state
        LeftReleased,  ///< the left button went up under a video that fills the window
        Taken,         ///< the video took it (a skip) or swallowed it
    };
    /// A video that fills the window takes the input: Escape skips (the logos and the intro
    /// together), Pause and Escape end the pause video, clicks on the game are swallowed; only the
    /// window's events go on. The credits in the menu's frame end with a click or Escape, a
    /// message's video in the minimap's window with a click on it. `hud` may be null.
    [[nodiscard]] Input route(const SDL_Event& event, const Hud* hud);

    /// The pause of the original (Pause key): the pause video loops; without it the game still
    /// stops (U-018). Returns whether the pause is on now; the caller pauses or resumes the game.
    bool toggle_pause(bool game_paused);
    /// Every frame of the game: the pause of the video ended another way (F3, another player), so
    /// the video goes too.
    void follow_pause(bool game_paused);

private:
    /// Starts the next waiting video, if any; false if none plays.
    bool start_next(bool minimap_shown);
    /// The pause video is the one playing.
    [[nodiscard]] bool pause_video_playing() const noexcept;

    std::unique_ptr<VideoPlayer> m_player;  ///< null without videos
    GameSound& m_sound;
    VideoQueue m_queue;
    PauseVideo m_pause;
};

}  // namespace o1602::app
