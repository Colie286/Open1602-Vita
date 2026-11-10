// SPDX-License-Identifier: GPL-3.0-or-later
//
// Makes the CPU side of the zoom levels' graphics (files, sprite decoding, atlas packing, hit
// masks) on a background thread, so that the first switch to a zoom level does not stand the game
// still for seconds. Only the upload to the GPU is left for the thread that owns the renderer.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "o1602/core/result.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/render/world_graphics.hpp"

namespace o1602::render {

/// The zoom levels in the order they are wanted after `current`: the current one, then those one
/// step away (the smaller sprites first: they are ready sooner), then the rest.
[[nodiscard]] std::vector<Zoom> preload_order(Zoom current);

class GraphicsPreloader {
public:
    /// Makes one zoom level. Called on the background thread: it must own everything it reads
    /// (copies, not references to state the caller changes). It may give up with an error once
    /// `cancel` is set (GraphicsOptions::cancel): nobody waits for the level any more.
    using Prepare = std::function<core::Result<PreparedGraphics>(Zoom, const std::atomic<bool>& cancel)>;
    using Ready = std::pair<Zoom, core::Result<PreparedGraphics>>;

    /// Without `background` (or if no thread can be started) nothing is made ahead: take() makes
    /// the level on the calling thread.
    GraphicsPreloader(Prepare prepare, bool background);
    /// Cancels the level being made and waits for the thread (the levels still queued are dropped).
    ~GraphicsPreloader();
    GraphicsPreloader(const GraphicsPreloader&) = delete;
    GraphicsPreloader& operator=(const GraphicsPreloader&) = delete;

    [[nodiscard]] bool background() const noexcept { return m_background; }

    /// Queues levels for the background thread (started on the first call). Levels already
    /// queued, being made or made and not taken yet are skipped.
    void request(std::span<const Zoom> zooms);
    /// Interactive handheld zoom: discard obsolete queued work and cancel a different in-flight level.
    void request_latest(Zoom zoom);
    void cancel_pending();
    /// A level made in the background and not taken yet (also a failed one), oldest first.
    [[nodiscard]] std::optional<Ready> take_ready();
    /// The level `zoom`: made in the background (it goes first if still queued; meanwhile
    /// `wait_step` is called every few milliseconds, e.g. to feed the sound), else made now.
    [[nodiscard]] core::Result<PreparedGraphics> take(Zoom zoom, const std::function<void()>& wait_step = {});

private:
    void work();
    /// m_prepare, with exceptions (out of memory) as errors.
    core::Result<PreparedGraphics> prepare(Zoom zoom) const;

    Prepare m_prepare;
    bool m_background;
    std::mutex m_mutex;
    std::condition_variable m_wake;  ///< the worker: work queued or stop
    std::condition_variable m_made;  ///< take(): a level is ready
    std::deque<Zoom> m_queue;
    std::optional<Zoom> m_making;
    std::vector<Ready> m_ready;
    bool m_stop = false;
    std::atomic<bool> m_cancel{false};  ///< read by Prepare while it runs
    std::thread m_thread;               ///< touched only by the owning thread
};

}  // namespace o1602::render
