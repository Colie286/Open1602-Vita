// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/graphics_preloader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <new>
#include <string>
#include <system_error>
#ifdef O1602_EXPERIMENTAL_VITA
#include <psp2/kernel/threadmgr.h>
#endif

namespace o1602::render {

std::vector<Zoom> preload_order(Zoom current) {
    std::vector<Zoom> order{Zoom::Large, Zoom::Medium, Zoom::Small};
    const int c = static_cast<int>(current);
    std::stable_sort(order.begin(), order.end(), [c](Zoom a, Zoom b) {
        const int da = std::abs(static_cast<int>(a) - c);
        const int db = std::abs(static_cast<int>(b) - c);
        return da != db ? da < db : static_cast<int>(a) > static_cast<int>(b);
    });
    return order;
}

GraphicsPreloader::GraphicsPreloader(Prepare prepare, bool background)
    : m_prepare(std::move(prepare)), m_background(background) {
    // One entry per zoom level at most (request() skips a level that is ready): the worker's
    // emplace_back never allocates, so it cannot throw on the worker thread (FB-20).
    m_ready.reserve(3);
}

GraphicsPreloader::~GraphicsPreloader() {
    {
        const std::lock_guard lock(m_mutex);
        m_stop = true;
        m_queue.clear();
    }
    m_cancel.store(true, std::memory_order_relaxed);
    m_wake.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void GraphicsPreloader::request(std::span<const Zoom> zooms) {
    if (!m_background) {
        return;
    }
    {
        const std::lock_guard lock(m_mutex);
        for (const Zoom z : zooms) {
            const bool known =
                std::find(m_queue.begin(), m_queue.end(), z) != m_queue.end() || m_making == z ||
                std::any_of(m_ready.begin(), m_ready.end(), [z](const Ready& r) { return r.first == z; });
            if (!known) {
                m_queue.push_back(z);
            }
        }
    }
    if (!m_thread.joinable()) {
        try {
            m_thread = std::thread([this] { work(); });
        } catch (const std::system_error&) {
            // No thread: the levels are made when they are needed, as without background.
            const std::lock_guard lock(m_mutex);
            m_queue.clear();
            m_background = false;
            return;
        }
    }
    m_wake.notify_one();
}

void GraphicsPreloader::request_latest(Zoom zoom) {
    if(!m_background)return;
    {
        std::lock_guard lock(m_mutex);
        m_queue.clear();
        if(m_making&&*m_making!=zoom)m_cancel.store(true,std::memory_order_relaxed);
        m_ready.erase(std::remove_if(m_ready.begin(),m_ready.end(),[&](const Ready& r){return r.first==zoom&&!r.second;}),m_ready.end());
        const bool ready=std::any_of(m_ready.begin(),m_ready.end(),[&](const Ready& r){return r.first==zoom;});
        if(!ready&&(!m_making||*m_making!=zoom||m_cancel.load(std::memory_order_relaxed)))m_queue.push_back(zoom);
    }
    if(!m_thread.joinable()){
        try{m_thread=std::thread([this]{work();});}
        catch(const std::system_error&){std::lock_guard lock(m_mutex);m_queue.clear();m_background=false;return;}
    }
    m_wake.notify_one();
}

void GraphicsPreloader::cancel_pending() {
    std::lock_guard lock(m_mutex);
    m_queue.clear();
    if(m_making)m_cancel.store(true,std::memory_order_relaxed);
}

std::optional<GraphicsPreloader::Ready> GraphicsPreloader::take_ready() {
    const std::lock_guard lock(m_mutex);
    if (m_ready.empty()) {
        return std::nullopt;
    }
    Ready r = std::move(m_ready.front());
    m_ready.erase(m_ready.begin());
    return r;
}

core::Result<PreparedGraphics> GraphicsPreloader::take(Zoom zoom, const std::function<void()>& wait_step) {
    std::unique_lock lock(m_mutex);
    for (;;) {
        const auto ready =
            std::find_if(m_ready.begin(), m_ready.end(), [zoom](const Ready& r) { return r.first == zoom; });
        if (ready != m_ready.end()) {
            core::Result<PreparedGraphics> result = std::move(ready->second);
            m_ready.erase(ready);
            return result;
        }
        const auto queued = std::find(m_queue.begin(), m_queue.end(), zoom);
        if (queued == m_queue.end() && m_making != zoom) {
            break;
        }
        if (queued != m_queue.end() && queued != m_queue.begin()) {
            m_queue.erase(queued);
            m_queue.push_front(zoom);  // wanted now: before the others
        }
        lock.unlock();
        if (wait_step) {
            wait_step();
        }
        lock.lock();
        m_made.wait_for(lock, std::chrono::milliseconds(10));
    }
    lock.unlock();
    return prepare(zoom);
}

void GraphicsPreloader::work() {
#ifdef O1602_EXPERIMENTAL_VITA
    sceKernelChangeThreadPriority(0,0x10000130);
#endif
    std::unique_lock lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
        if (m_stop) {
            return;
        }
        const Zoom zoom = m_queue.front();
        m_queue.pop_front();
        m_making = zoom;
        m_cancel.store(false,std::memory_order_relaxed);
        lock.unlock();
        core::Result<PreparedGraphics> result = prepare(zoom);
        lock.lock();
        m_making.reset();
        m_ready.emplace_back(zoom, std::move(result));
        m_made.notify_all();
    }
}

core::Result<PreparedGraphics> GraphicsPreloader::prepare(Zoom zoom) const {
    try {
        return m_prepare(zoom, m_cancel);
    } catch (const std::bad_alloc&) {
        return core::make_error(core::ErrorCode::PlatformError, "not enough memory for the graphics");
    } catch (const std::exception& e) {
        return core::make_error(core::ErrorCode::PlatformError, std::string("graphics: ") + e.what());
    }
}

}  // namespace o1602::render
