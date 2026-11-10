#include "o1602/core/workers.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/audio/wav.hpp"
#include "o1602/core/profile.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>
#ifdef O1602_EXPERIMENTAL_VITA
#include <psp2/kernel/threadmgr.h>
#endif
namespace o1602::audio {
namespace {
// 16 * 8192 stereo S16 frames = 512 KiB (2.73 s at 48 kHz).
// Only published slots are visible to the mixer. A pinned slot cannot be replaced.
class Prefetched final : public SampleSource {
    static constexpr std::size_t block_frames=8192, slots=16, none=std::numeric_limits<std::size_t>::max();
    struct Slot { std::size_t block=none; bool pinned=false; std::vector<std::int16_t> samples; };
    Pcm m_pcm;
    bool m_cyclic;
    mutable std::array<Slot,slots> m_slots;
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_wake;
    mutable std::size_t m_first=0;
    mutable std::size_t m_sample_block=none;
    mutable const std::int16_t* m_samples=nullptr;
    mutable std::uint64_t m_misses=0;
    bool m_stop=false;
    std::thread m_worker;
    std::array<char,256> m_error{};
    std::atomic<bool> m_failed{false};
    void fail(const char* message) noexcept {
        std::snprintf(m_error.data(),m_error.size(),"%s",message);
        m_failed.store(true,std::memory_order_release);
        std::fprintf(stderr,"MUSIC_PREFETCH failed: %s\n",message);
    }
    void work() {
        core::worker_enter("music_read");
#ifdef O1602_EXPERIMENTAL_VITA
        // More important than speculative atlas preparation; no clock changes.
        sceKernelChangeThreadPriority(0,0x10000100);
#endif
        std::vector<std::int16_t> decoded(block_frames*m_pcm.channels);
        const auto blocks=(frames()+block_frames-1)/block_frames;
        std::unique_lock lock(m_mutex);
        while(!m_stop) {
            std::size_t wanted=none;
            // Current request, then contiguous look-ahead, wrapping at track end.
            for(std::size_t n=0;n<std::min(slots,blocks);++n){
                const auto next=m_first/block_frames+n;
                if(!m_cyclic&&next>=blocks)break;
                const auto block=next%blocks;
                if(std::none_of(m_slots.begin(),m_slots.end(),[&](const Slot& s){return s.block==block;})){wanted=block;break;}
            }
            if(wanted==none){m_wake.wait(lock);continue;}
            lock.unlock();
            {
                core::profile::Scope timer(core::profile::MusicRead);
                const auto start=wanted*block_frames,count=std::min(block_frames,frames()-start);
                for(std::size_t i=0;i<count;++i)for(std::uint8_t c=0;c<m_pcm.channels;++c)
                    decoded[i*m_pcm.channels+c]=m_pcm.sample(start+i,c);
            }
            lock.lock();
            // Evict the unpinned block farthest behind the current look-ahead.
            Slot* target=nullptr;std::size_t farthest=0;
            for(auto& s:m_slots)if(!s.pinned){
                const auto distance=s.block==none?blocks:(s.block+blocks-m_first/block_frames)%blocks;
                if(!target||distance>=farthest){target=&s;farthest=distance;}
            }
            if(target){target->samples.swap(decoded);target->block=wanted;}
            else m_wake.wait(lock);
        }
    }
public:
    explicit Prefetched(Pcm pcm,bool cyclic):m_pcm(std::move(pcm)),m_cyclic(cyclic) {
        for(auto& s:m_slots)s.samples.resize(block_frames*m_pcm.channels);
        m_worker=std::thread([this]{try{work();}catch(const std::exception& e){fail(e.what());}catch(...){fail("unknown sample source failure");}});
    }
    ~Prefetched() override {
        {std::lock_guard lock(m_mutex);m_stop=true;}m_wake.notify_one();m_worker.join();
    }
    const char* error() const noexcept override {return m_failed.load(std::memory_order_acquire)?m_error.data():nullptr;}
    std::size_t frames() const noexcept override {return m_pcm.frames();}
    std::size_t buffered_bytes() const noexcept override {
        return (slots+1)*block_frames*m_pcm.channels*2+(m_pcm.source?m_pcm.source->buffered_bytes():m_pcm.samples.capacity()*2);
    }
    bool prepare(std::size_t first,std::size_t count) const override {
        if(first>=frames())return false;
        count=std::min(count,frames());
        // A previously pinned range is immutable. This fast path needs no
        // mutex, including the repeated prepare performed by Mixer after tests
        // or another caller has already pinned the same samples.
        const auto pinned=[&](std::size_t start,std::size_t n){
            if(!n)return true;
            for(auto block=start/block_frames;block<=(start+n-1)/block_frames;++block)
                if(std::none_of(m_slots.begin(),m_slots.end(),[&](const Slot& s){return s.pinned&&s.block==block;}))return false;
            return true;
        };
        const auto tail_count=std::min(count,frames()-first);
        if(pinned(first,tail_count)&&pinned(0,count-tail_count))return true;
        std::unique_lock lock(m_mutex,std::try_to_lock);
        if(!lock.owns_lock()){core::profile::gauge(core::profile::MusicStarves,++m_misses);return false;}
        const bool moved=m_first/block_frames!=first/block_frames;
        m_first=first;m_sample_block=none;m_samples=nullptr;
        for(auto& s:m_slots)s.pinned=false;
        bool ready=true;
        // At most two range pieces at a loop boundary. No allocation in this path.
        const auto pin=[&](std::size_t start,std::size_t n){
            if(!n)return;
            for(auto block=start/block_frames;block<=(start+n-1)/block_frames;++block){
                auto it=std::find_if(m_slots.begin(),m_slots.end(),[&](const Slot& s){return s.block==block;});
                if(it==m_slots.end())ready=false;else it->pinned=true;
            }
        };
        const auto tail=std::min(count,frames()-first);pin(first,tail);pin(0,count-tail);
        if(!ready)core::profile::gauge(core::profile::MusicStarves,++m_misses);
        core::profile::gauge(core::profile::MusicBufferBytes,buffered_bytes());
        lock.unlock();if(moved||!ready)m_wake.notify_one();return ready;
    }
    std::int16_t sample(std::size_t frame,std::uint8_t c) const override {
        // prepare() pins all reads until the next prepare. Worker metadata can
        // change on other slots; do not read those concurrently.
        if(frame>=frames()||c>=m_pcm.channels)return 0;
        const auto block=frame/block_frames;
        if(block!=m_sample_block){
            m_samples=nullptr;m_sample_block=block;
            for(const auto& s:m_slots)if(s.pinned&&s.block==block){m_samples=s.samples.data();break;}
        }
        return m_samples?m_samples[(frame%block_frames)*m_pcm.channels+c]:0;
    }
};
}
Pcm prefetch_pcm(Pcm pcm,bool cyclic) {
    if(!pcm.frames()||!pcm.source)return pcm;
    Pcm out;out.rate=pcm.rate;out.channels=pcm.channels;
    out.source=std::make_shared<Prefetched>(std::move(pcm),cyclic);return out;
}
}
