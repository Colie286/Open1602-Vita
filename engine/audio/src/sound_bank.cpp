#include "o1602/core/workers.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/profile.hpp"
#include "o1602/audio/sound_bank.hpp"

#include <algorithm>
#include <cctype>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <cstdio>
#include <atomic>
#ifdef O1602_EXPERIMENTAL_VITA
#include <psp2/kernel/threadmgr.h>
#endif

#include "o1602/audio/sound_file.hpp"
#include "o1602/core/file.hpp"

namespace o1602::audio {
namespace {std::atomic<unsigned> test_delay{0};}
void SoundBank::test_read_delay(unsigned ms){test_delay=std::min(ms,2000U);}
struct SoundBank::Async {
    struct Entry {std::shared_ptr<const Pcm> pcm;bool loading=true;std::uint64_t used=0;};
    struct Job {std::string key;};
    assets::GameDirectory game;
    std::mutex mutex;std::condition_variable wake;bool stop=false,working=false;
    std::unordered_map<std::string,Entry> entries;std::deque<Job> jobs;
    std::size_t bytes=0;std::uint64_t clock=0;std::thread worker;
    static std::size_t size(const std::shared_ptr<const Pcm>& p){return p?p->samples.size()*sizeof(std::int16_t):0;}
    explicit Async(const assets::GameDirectory& source):game(source),worker([this]{run();}){}
    ~Async(){ {std::lock_guard lock(mutex);stop=true;jobs.clear();}wake.notify_one();if(worker.joinable())worker.join();core::profile::gauge(core::profile::EffectCacheBytes,0);core::profile::gauge(core::profile::EffectPending,0);}
    void run(){
        core::worker_enter("effect_read");
#ifdef O1602_EXPERIMENTAL_VITA
        sceKernelChangeThreadPriority(0,0x10000110);
#endif
        std::unique_lock lock(mutex);
        while(true){wake.wait(lock,[&]{return stop||!jobs.empty();});if(stop)return;
            auto job=std::move(jobs.front());jobs.pop_front();working=true;lock.unlock();
            std::shared_ptr<const Pcm> pcm;
            try {core::profile::Scope background(core::profile::SoundRead);if(auto path=game.resolve(job.key)){auto decoded=SoundBank::decode_file(*path);if(decoded)pcm=std::make_shared<const Pcm>(std::move(*decoded));}}
            catch(const std::exception& e){std::fprintf(stderr,"SOUND_READ failed=%s reason=%s\n",job.key.c_str(),e.what());}
            lock.lock();working=false;
            auto& e=entries.at(job.key);e.pcm=std::move(pcm);e.loading=false;e.used=++clock;bytes+=size(e.pcm);
            while(bytes>8*1024*1024||entries.size()>128){
                auto victim=entries.end();for(auto it=entries.begin();it!=entries.end();++it)
                    if(it->first!=job.key&&!it->second.loading&&(victim==entries.end()||it->second.used<victim->second.used))victim=it;
                if(victim==entries.end())break;bytes-=size(victim->second.pcm);entries.erase(victim);
            }
            core::profile::gauge(core::profile::EffectCacheBytes,bytes);
            core::profile::gauge(core::profile::EffectPending,jobs.size());
        }
    }
};
SoundBank::SoundBank(const assets::GameDirectory& game):m_game(&game){}
SoundBank::~SoundBank()=default;
bool SoundBank::enable_background(){try{m_async=std::make_unique<Async>(*m_game);return true;}catch(...){return false;}}
std::shared_ptr<const Pcm> SoundBank::request(std::string_view relative,bool& pending){
    pending=false;if(!m_async)return get(relative);
    std::string key(relative);for(auto& c:key)c=c=='\\'?'/':static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto& a=*m_async;std::unique_lock lock(a.mutex,std::try_to_lock);
    if(!lock.owns_lock()){pending=true;return {};}
    if(auto it=a.entries.find(key);it!=a.entries.end()){pending=it->second.loading;it->second.used=++a.clock;return it->second.pcm;}
    if(a.jobs.size()>=16){pending=true;return {};}
    std::fprintf(stderr,"SOUND_REQUEST path=%s\n",key.c_str());
    core::profile::mark("sound_cache_miss");a.entries.emplace(key,Async::Entry{});a.jobs.push_back({key});
    core::profile::gauge(core::profile::EffectPending,a.jobs.size()+unsigned(a.working));
    pending=true;a.wake.notify_one();return {};
}

std::shared_ptr<const Pcm> SoundBank::get(std::string_view relative) {
    std::string key(relative);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return c == '\\' ? '/' : static_cast<char>(std::tolower(c)); });
    if (const auto it = m_cache.find(key); it != m_cache.end()) {
        return it->second;
    }
    core::profile::mark("sound_cache_miss");
    auto sound = load(relative);
    m_cache.emplace(std::move(key), sound);  // a missing file is remembered too
    return sound;
}

std::optional<std::filesystem::path> SoundBank::find(std::string_view relative) const {
    auto path = m_game->resolve(relative);
    if (!path) {
        m_error = std::string(relative) + ": not found";
    }
    return path;
}

core::Result<Pcm> SoundBank::decode_file(const std::filesystem::path& path) {
    const auto start=std::chrono::steady_clock::now();
    core::profile::Scope timer(core::profile::Decode);
    if(const auto ms=test_delay.exchange(0))std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    auto bytes = core::read_file(path);
    if (!bytes) {
        return bytes.error();
    }
    auto pcm = decode_sound(*bytes);
    const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    if(us>=10000)std::fprintf(stderr,"SOUND_READ path=%s bytes=%u us=%llu\n",core::path_to_utf8(path).c_str(),unsigned(bytes->size()),static_cast<unsigned long long>(us));
    if (!pcm) {
        return core::make_error(pcm.error().code, core::path_to_utf8(path) + ": " + pcm.error().message);
    }
    return pcm;
}

std::shared_ptr<const Pcm> SoundBank::load(std::string_view relative) const {
    const auto path = find(relative);
    if (!path) {
        return nullptr;
    }
    auto pcm = decode_file(*path);
    if (!pcm) {
        m_error = pcm.error().message;
        return nullptr;
    }
    return std::make_shared<const Pcm>(std::move(pcm).value());
}

}  // namespace o1602::audio
