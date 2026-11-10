// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_queue.hpp"

#include <algorithm>

namespace o1602::app {

bool VideoQueue::request(const VideoRequest& request, bool playing) {
    // Repeated announcements must not tear down the same active message clip.
    // A completed/skipped clip may be requested again; other clips still replace it.
    if(playing && !request.intro && !request.follows && !request.loop && !request.in_frame &&
       !m_now.intro && !m_now.loop && !m_now.in_frame && request.number==m_now.number &&
       request.tracks==m_now.tracks){m_waiting.clear();return false;}
    const bool replaces = !request.intro && !request.follows;
    if (replaces) {
        m_waiting.clear();
    }
    m_waiting.push_back(request);
    return replaces;
}

std::optional<VideoRequest> VideoQueue::take_next() {
    if (m_waiting.empty()) {
        return std::nullopt;
    }
    VideoRequest r = m_waiting.front();
    m_waiting.pop_front();
    return r;
}

void VideoQueue::started(const VideoRequest& request, bool minimap_shown) {
    m_now = request;
    // A message's video plays in the minimap; with none on screen (the main menu, also after
    // --video N) it fills the screen.
    m_now.in_minimap =
        !request.in_frame && video_plays_in_minimap(request.number, request.loop, request.intro, minimap_shown);
}

void VideoQueue::skip_start() {
    if (m_now.intro) {
        std::erase_if(m_waiting, [](const VideoRequest& r) { return r.intro; });
    }
}

bool PauseVideo::toggle(bool game_paused) {
    m_on = !game_paused && !m_on;
    m_seen = false;
    return m_on;
}

bool PauseVideo::ended_elsewhere(bool game_paused) {
    if (!m_on) {
        return false;
    }
    if (game_paused) {
        m_seen = true;
        return false;
    }
    if (m_seen) {
        m_on = false;
        return true;
    }
    return false;
}

}  // namespace o1602::app
