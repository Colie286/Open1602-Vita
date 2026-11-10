// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which video of the original plays when and where (docs/re/videos.md, ADR 0010): the waiting
// videos, the one playing, and the pause video. Needs no platform, so that the tests have it;
// VideoController plays what it decides.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

namespace o1602::app {

/// The pause video (VideoSmk/6.smk); it loops while the game stands still.
inline constexpr std::uint16_t kPauseVideo = 6;

/// The first video of a fulfilled mission (0x5F: 2000); every later number ends a game.
inline constexpr std::uint16_t kFirstMissionVideo = 2000;

/// Whether a video plays in the minimap's window. Messages play there; the logos and the intro,
/// the credits (10), the pause (6, looping) and the ends of the game (defeat 998, 1010, 2020;
/// missions fulfilled 2000–2120 and from 2500, the end of the campaigns 2990) fill the screen and
/// halt the game, as in the original (seen with 2080 on 2026-10-27). Without a minimap
/// on screen (the main menu, a game without the panel) a message's video fills the screen too:
/// else its sound would play without a picture.
[[nodiscard]] constexpr bool video_plays_in_minimap(std::uint16_t number, bool loop, bool intro,
                                                    bool minimap_shown) noexcept {
    const bool full = intro || loop || number == 10 || number == 998 || number == 1010 || number >= kFirstMissionVideo;
    return !full && minimap_shown;
}

struct VideoRequest {
    std::uint16_t number = 0;
    std::uint8_t tracks = 0;  ///< the sound tracks (bit i = track i, 0 = all)
    bool loop = false;
    bool intro = false;     ///< the logos and the intro: they queue after each other
    bool in_frame = false;  ///< in the picture frame of the main menu (the credits)
    /// Waits behind the videos before it instead of replacing them: the original's chain after the
    /// running video (Video_Update 0x00492410, next video 0x005B7014), e.g. 2990 after the success.
    bool follows = false;
    /// In the minimap's window while the game goes on (video_plays_in_minimap); set when the
    /// video starts, by whether there is a minimap on screen then.
    bool in_minimap = false;
};

/// The videos waiting and the one playing.
class VideoQueue {
public:
    /// Video_Request 0x00488C00: the latest request wins, it replaces the waiting ones; the logos
    /// and the intro (`intro`) and a chained video (`follows`) only queue after the others. True if
    /// the running video must stop for it. An identical active message keeps playing;
    /// `playing` must be false after end/skip, allowing a later replay.
    bool request(const VideoRequest& request, bool playing = false);
    /// The next waiting video, taken from the queue.
    [[nodiscard]] std::optional<VideoRequest> take_next();
    /// `request` started playing; `minimap_shown`: a minimap is on screen now.
    void started(const VideoRequest& request, bool minimap_shown);
    /// The video playing, or the one played last.
    [[nodiscard]] const VideoRequest& now() const noexcept { return m_now; }
    /// Escape: skipping a logo or the intro skips the whole start (0x00492690).
    void skip_start();
    [[nodiscard]] bool waiting() const noexcept { return !m_waiting.empty(); }
    void clear() { m_waiting.clear(); }

private:
    std::deque<VideoRequest> m_waiting;
    VideoRequest m_now;
};

/// The pause of the original (Pause key): its video loops while the game stands still; without
/// the video the game still stops (U-018).
class PauseVideo {
public:
    /// The Pause key: on, or off again. It follows the game: paused another way (F3, another
    /// player), the key resumes it. Returns whether the pause is on now.
    bool toggle(bool game_paused);
    /// Every frame: true once the pause ended another way (F3, another player); the video goes too.
    bool ended_elsewhere(bool game_paused);
    [[nodiscard]] bool on() const noexcept { return m_on; }
    void reset() noexcept {
        m_on = false;
        m_seen = false;
    }

private:
    bool m_on = false;    ///< the original's pause is on
    bool m_seen = false;  ///< and the game has stood still since (the command arrived)
};

}  // namespace o1602::app
