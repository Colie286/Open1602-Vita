// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/paged_sprite_cache.hpp"
#include "o1602/core/profile.hpp"
#include "o1602/core/workers.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <map>
#include <tuple>
#include <thread>
#include <mutex>
#include <deque>
#include <optional>
#include <filesystem>
#include <chrono>
#ifdef O1602_EXPERIMENTAL_VITA
#include "o1602/platform/vita.hpp"
#endif
namespace o1602::render {
struct PagedSpriteCache::ReadQueue {
    static constexpr std::size_t limit=2*1024*1024;
    struct Entry {
        std::uint64_t generation=0;
        std::uint32_t sprite=0;
        std::size_t bytes=0;
        bool running=false,cancelled=false,done=false;
        std::optional<RgbaImage> image;
        std::function<core::Result<RgbaImage>()> read;
    };
    std::mutex mutex;
    // Only idle workers wait here. The renderer must never join a speculative
    // read: a worker may itself be stalled in storage or a platform wait.
    SDL_Semaphore* work_available=nullptr;
    std::map<std::uint32_t,std::shared_ptr<Entry>> entries;
    std::deque<std::shared_ptr<Entry>> queue;
    std::uint64_t generation=0,hits=0,jobs=0;
    std::size_t used=0;
    bool quit=false;
    unsigned test_delay_ms=0;
    bool test_hold_queue=false;
    std::atomic<bool> test_active{false};
    std::vector<std::thread> threads;
    ReadQueue(){
        work_available=SDL_CreateSemaphore(0);
        if(!work_available)throw std::runtime_error("no sprite work semaphore available");
        const auto count=std::min(2U,core::worker_parallelism());
        for(unsigned i=0;i<count;++i)try{threads.emplace_back([this,i]{core::worker_enter("sprite_prefetch",i);run();});}catch(...){break;}
        if(threads.empty()){SDL_DestroySemaphore(work_available);throw std::runtime_error("no sprite worker available");}
    }
    ~ReadQueue(){
        {std::lock_guard lock(mutex);quit=true;}
        for(unsigned i=0;i<threads.size();++i)SDL_SignalSemaphore(work_available);
        for(auto& thread:threads)thread.join();
        SDL_DestroySemaphore(work_available);
        core::profile::gauge(core::profile::SpritePrefetchBytes,0);
    }
    void gauges(){core::profile::gauge(core::profile::SpritePrefetchBytes,used);core::profile::gauge(core::profile::SpritePrefetchHits,hits);core::profile::gauge(core::profile::SpritePrefetchJobs,jobs);}
    void reset(std::uint64_t identity){
        std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()||generation==identity)return;generation=identity;
        for(auto& [id,e]:entries){e->cancelled=true;if(!e->running)used-=e->bytes;}
        entries.clear();queue.clear();gauges();
    }
    void request(const SpriteAtlas& atlas,std::uint32_t sprite){
        const auto& r=atlas.rect(sprite);const std::size_t size=std::size_t(r.width)*r.height*4;
        if(!size||size>limit)return;
        {std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()||atlas.identity()!=generation||entries.contains(sprite))return;
        if(entries.size()>=64){core::profile::increment(core::profile::PrefetchRejectedJobs);return;}}
        auto read=atlas.reader(sprite);if(!read)return;
        std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()||atlas.identity()!=generation||entries.contains(sprite))return;
        if(entries.size()>=64){core::profile::increment(core::profile::PrefetchRejectedJobs);return;}
        std::erase_if(queue,[](const auto& e){return e->cancelled;});
        if(queue.size()>=64){core::profile::increment(core::profile::PrefetchRejectedJobs);return;}
        for(auto it=entries.begin();used+size>limit&&it!=entries.end();){
            if(it->second->done){used-=it->second->bytes;it=entries.erase(it);}else ++it;
        }
        if(used+size>limit){core::profile::increment(core::profile::PrefetchRejectedBytes);return;}
        auto e=std::make_shared<Entry>();e->generation=generation;e->sprite=sprite;e->bytes=size;e->read=std::move(read);
        entries[sprite]=e;queue.push_back(e);used+=size;gauges();
        lock.unlock();SDL_SignalSemaphore(work_available);
    }
    std::optional<RgbaImage> take(std::uint32_t sprite,std::uint64_t identity){
        std::unique_lock lock(mutex,std::try_to_lock);
        if(!lock.owns_lock()||generation!=identity)return {};
        auto it=entries.find(sprite);if(it==entries.end())return {};
        auto e=it->second;
        // Ready work is used immediately. Queued/running work is retired and
        // the normal checked reader supplies this required sprite independently.
        // Keep a running job's byte reservation until its worker returns.
        if(e->done)core::profile::increment(core::profile::PrefetchReady);
        entries.erase(sprite);
        if(e->done){used-=e->bytes;if(e->image)++hits;gauges();return std::move(e->image);}
        e->cancelled=true;if(!e->running)used-=e->bytes;gauges();return {};
    }

    void run(){
        for(;;){std::shared_ptr<Entry> e;
            unsigned delay=0;bool hold_queue=false;
            // Counted work signals (native SDK semaphores on Vita), separate
            // from completion. A timeout also rechecks queued work after reset.
            (void)SDL_WaitSemaphoreTimeout(work_available,50);
            {std::unique_lock lock(mutex);if(quit)return;if(queue.empty())continue;
                e=std::move(queue.front());queue.pop_front();if(e->cancelled)continue;e->running=true;
                delay=test_delay_ms;test_delay_ms=0;hold_queue=test_hold_queue;}
            if(delay){
                std::unique_lock test_lock(mutex,std::defer_lock);if(hold_queue)test_lock.lock();
                test_active.store(true,std::memory_order_release);
                std::fprintf(stderr,"SPRITE_PREFETCH_TEST phase=begin delay_ms=%u sprite=%u time_ms=%llu hold_queue=%d\n",delay,e->sprite,static_cast<unsigned long long>(SDL_GetTicks()),hold_queue);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                std::fprintf(stderr,"SPRITE_PREFETCH_TEST phase=end time_ms=%llu\n",static_cast<unsigned long long>(SDL_GetTicks()));
                test_active.store(false,std::memory_order_release);
            }
            std::optional<RgbaImage> image;
            try{core::profile::Scope work(core::profile::SpritePrefetch);auto value=e->read();if(value&&value->pixels.size()==e->bytes)image=std::move(*value);}catch(...){/* synchronous checked path handles errors */}
            // Releasing the last backing owner can close a file. Never do that
            // while holding the queue mutex used by the rendering thread.
            e->read={};
            {std::lock_guard lock(mutex);++jobs;e->running=false;
                if(e->cancelled||e->generation!=generation)used-=e->bytes;
                else {e->image=std::move(image);e->done=true;}
                gauges();}
        }
    }
};
std::shared_ptr<PagedSpriteCache> PagedSpriteCache::acquire(SDL_Renderer* renderer,unsigned size,unsigned count) {
    static std::map<std::tuple<SDL_Renderer*,unsigned,unsigned>,std::weak_ptr<PagedSpriteCache>> caches;
    const auto key=std::make_tuple(renderer,size,count);
    if(auto cache=caches[key].lock())return cache;
    auto cache=std::shared_ptr<PagedSpriteCache>(new PagedSpriteCache(renderer,size,count));caches[key]=cache;return cache;
}
PagedSpriteCache::PagedSpriteCache(SDL_Renderer* renderer,unsigned size,unsigned count)
    :m_renderer(renderer),m_size(size),m_pages(count) {
#ifdef O1602_EXPERIMENTAL_VITA
    m_storage_order=!std::filesystem::exists("ux0:data/open1602/sprite-storage-order-off.flag");
    if(!std::filesystem::exists("ux0:data/open1602/sprite-prefetch-off.flag")){
        try {m_reads=std::make_unique<ReadQueue>();}catch(const std::exception& e){std::fprintf(stderr,"SPRITE_PREFETCH disabled: %s\n",e.what());}
    }
    if(m_reads&&std::filesystem::exists("ux0:data/open1602/sprite-prefetch-test-stall.flag"))test_prefetch_delay(6500);
    if(m_reads&&std::filesystem::exists("ux0:data/open1602/sprite-prefetch-test-lock.flag"))test_prefetch_delay(6500,true);
    std::fprintf(stderr,"SPRITE_PREFETCH enabled=%d rgba_budget=2097152 compressed_scratch_limit=2359296 queue_limit=64 workers=%u completion_wait=0\n",m_reads!=nullptr,m_reads?unsigned(m_reads->threads.size()):0U);
#endif
}
PagedSpriteCache::~PagedSpriteCache(){for(auto& page:m_pages)if(page.texture)SDL_DestroyTexture(page.texture);}
void PagedSpriteCache::enable_prefetch(bool enabled){if(enabled&&!m_reads)m_reads=std::make_unique<ReadQueue>();else if(!enabled)m_reads.reset();}
void PagedSpriteCache::test_prefetch_delay(unsigned ms,bool hold){if(m_reads){std::lock_guard lock(m_reads->mutex);m_reads->test_delay_ms=ms;m_reads->test_hold_queue=hold;}}
bool PagedSpriteCache::prefetch_test_active()const{return m_reads&&m_reads->test_active.load(std::memory_order_acquire);}
void PagedSpriteCache::prefetch(const SpriteAtlas& atlas,const std::vector<std::uint32_t>& sprites){if(m_reads){m_reads->reset(atlas.identity());for(auto sprite:sprites)m_reads->request(atlas,sprite);}}
PagedSpriteCache::PrefetchStats PagedSpriteCache::prefetch_stats() const {
    if(!m_reads)return {};
    std::lock_guard lock(m_reads->mutex);
    unsigned running=0;for(const auto& [id,e]:m_reads->entries)running+=e->running;
    return {m_reads->used,m_reads->hits,m_reads->jobs,m_reads->queue.size(),running};
}

void PagedSpriteCache::invalidate(){
    std::fill(m_valid.begin(),m_valid.end(),false);
    for(auto& page:m_pages){
        if(page.texture)SDL_SetBooleanProperty(SDL_GetTextureProperties(page.texture),"open1602.texture.overwrite_pending",true);
        page.x=page.y=page.height=0;page.pinned=false;page.sprites.clear();
    }
}
std::uint64_t PagedSpriteCache::bytes() const noexcept {
    std::uint64_t n=0;for(const auto& p:m_pages)if(p.texture)n+=std::uint64_t{m_size}*m_size*4;return n;
}
const AtlasRect& PagedSpriteCache::rect(std::uint32_t sprite) const {
    static const AtlasRect missing{};return sprite<m_entries.size()&&m_valid[sprite]?m_entries[sprite]:missing;
}
SDL_Texture* PagedSpriteCache::texture(unsigned page) const {return page<m_pages.size()?m_pages[page].texture:nullptr;}
bool PagedSpriteCache::prepare(const SpriteAtlas& atlas,const std::vector<DrawCommand>& commands,
                               double ox,double oy,double scale,int width,int height) try {
    core::profile::Scope timer(core::profile::Sprites);
    m_error.clear();++m_frame;
    if(m_fail_next){m_fail_next=false;m_error="injected cache failure (QA only)";return false;}
    if(m_identity!=atlas.identity()){
        invalidate();m_identity=atlas.identity();m_entries.resize(atlas.sprite_count());m_valid.assign(atlas.sprite_count(),false);
    }
    // Retry a generation reset if an optional worker held the queue last frame.
    // take() checks the identity as well, so a deferred reset cannot mix zooms.
    if(m_reads)m_reads->reset(m_identity);
    m_needed.clear();m_seen.assign(atlas.sprite_count(),false);
    auto& needed=m_needed;auto& seen=m_seen;
    for(const auto& command:commands){
        if(command.overlay()||command.sprite>=seen.size()||seen[command.sprite])continue;
        const auto& r=atlas.rect(command.sprite);
        if(r.empty()||(command.x+r.width)*scale-ox<0||command.x*scale-ox>=width||
           command.y*scale-oy<0||(command.y-r.height)*scale-oy>=height)continue;
        seen[command.sprite]=true;needed.push_back(command.sprite);
    }
    if(prepare_impl(atlas,needed,true)){
        // Prepare command-list fringe sprites for a following camera frame.
        // Only CPU data is speculative; visibility and GPU publication still use
        // the exact original command on the render thread.
        if(m_reads){
            unsigned requested=0;
            for(const auto& command:commands){
                if(command.overlay()||command.sprite>=seen.size()||seen[command.sprite]||m_valid[command.sprite])continue;
                const auto& r=atlas.rect(command.sprite);
                if(r.empty()||(command.x+r.width)*scale-ox < -128||command.x*scale-ox>=width+128||
                   command.y*scale-oy < -128||(command.y-r.height)*scale-oy>=height+128)continue;
                seen[command.sprite]=true;m_reads->request(atlas,command.sprite);
                if(++requested==32)break;
            }
        }
        return true;
    }
    // An interrupted upload never publishes invalid entries. All GPU objects stay
    // valid; the old zoom can repopulate the same bounded storage on its next frame.
    invalidate();return false;
} catch(const std::exception& e) {m_error=e.what();invalidate();return false;}
bool PagedSpriteCache::prepare_impl(const SpriteAtlas& atlas,const std::vector<std::uint32_t>& needed,bool compact) {
    const bool diagnose=core::profile::enabled();
    const auto start=diagnose?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    for(auto& page:m_pages)page.pinned=false;
    for(const auto sprite:needed)if(m_valid[sprite]){auto& page=m_pages[m_entries[sprite].page];page.pinned=true;page.used=m_frame;}
    m_missing.clear();auto& missing=m_missing;
    for(const auto sprite:needed){
        if(m_valid[sprite])continue;
        const auto& source=atlas.rect(sprite);
        if(unsigned(source.width)+1>m_size||unsigned(source.height)+1>m_size){m_error="sprite exceeds GPU cache page";return false;}
        unsigned chosen=static_cast<unsigned>(m_pages.size()),px=0,py=0,ph=0;
        for(unsigned i=0;i<m_pages.size();++i){
            auto& p=m_pages[i];unsigned x=p.x,y=p.y,h=p.height;
            if(x+source.width+1>m_size){y+=h;x=0;h=0;}
            if(y+source.height+1<=m_size){chosen=i;px=x;py=y;ph=h;break;}
        }
        if(chosen==m_pages.size()){
            for(unsigned i=0;i<m_pages.size();++i)if(!m_pages[i].pinned&&
                (chosen==m_pages.size()||m_pages[i].used<m_pages[chosen].used))chosen=i;
            if(chosen==m_pages.size()){
                if(compact){invalidate();return prepare_impl(atlas,needed,false);}
                m_error="visible sprites exceed the bounded GPU cache";return false;
            }
            if(m_pages[chosen].texture)SDL_SetBooleanProperty(SDL_GetTextureProperties(m_pages[chosen].texture),"open1602.texture.overwrite_pending",true);
            ++m_evictions;auto& p=m_pages[chosen];for(auto id:p.sprites)m_valid[id]=false;p.sprites.clear();p.x=p.y=p.height=0;
        }
        auto& page=m_pages[chosen];page.x=px+source.width+1;page.y=py;page.height=std::max(ph,unsigned(source.height)+1);
        page.pinned=true;page.used=m_frame;page.sprites.push_back(sprite);
        missing.push_back({sprite,AtlasRect{static_cast<std::uint16_t>(chosen),static_cast<std::uint16_t>(px),static_cast<std::uint16_t>(py),source.width,source.height}});
    }
    // Placement and later drawing retain painter order. Only independent reads
    // and uploads to the already assigned, disjoint rectangles are reordered.
    if(m_storage_order&&missing.size()>1)std::sort(missing.begin(),missing.end(),[&](const auto& a,const auto& b){
        return std::pair{atlas.read_offset(a.sprite),a.sprite}<std::pair{atlas.read_offset(b.sprite),b.sprite};
    });
    // Decode upcoming sprites during GPU uploads; each job retains its backing and atlas ID.
    if(m_reads)for(const auto& item:missing)m_reads->request(atlas,item.sprite);
    // Decode each missing sprite separately to bound the temporary RGBA buffer.
    for(const auto& item:missing){
        core::profile::Scope miss(core::profile::CacheMiss);++m_misses;
        auto prefetched=[&]{
#ifdef O1602_EXPERIMENTAL_VITA
            platform::vita::WatchPhase watch("sprite_prefetch_take_ready");
#endif
            return m_reads?m_reads->take(item.sprite,atlas.identity()):std::optional<RgbaImage>{};}();
        auto image=prefetched?core::Result<RgbaImage>{std::move(*prefetched)}:[&]{
#ifdef O1602_EXPERIMENTAL_VITA
            platform::vita::WatchPhase watch("sprite_read_sync");
#endif
            core::profile::increment(core::profile::SpriteSyncReads);return atlas.read_sprite(item.sprite);}();
        if(!image){m_error=image.error().message;return false;}
        auto& page=m_pages[item.dest.page];
        if(!page.texture){
            core::profile::Scope creation(core::profile::TextureCreate);
            core::profile::Scope uploading(core::profile::Upload);
            auto properties=SDL_CreateProperties();
            SDL_SetStringProperty(properties,"open1602.texture.role","world_sprite");
            SDL_SetNumberProperty(properties,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_RGBA32);
            SDL_SetNumberProperty(properties,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_STATIC);
            SDL_SetNumberProperty(properties,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,m_size);
            SDL_SetNumberProperty(properties,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,m_size);
            page.texture=SDL_CreateTextureWithProperties(m_renderer,properties);
            SDL_DestroyProperties(properties);
            if(!page.texture){m_error=std::string("cache SDL_CreateTexture: ")+SDL_GetError();return false;}
            SDL_SetTextureBlendMode(page.texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(page.texture,SDL_SCALEMODE_NEAREST);
        }
        const SDL_Rect destination{item.dest.x,item.dest.y,item.dest.width,item.dest.height};
        {
#ifdef O1602_EXPERIMENTAL_VITA
         platform::vita::WatchPhase watch("sprite_gpu_upload");
#endif
         core::profile::Scope uploading(core::profile::Upload);
         if(!SDL_UpdateTexture(page.texture,&destination,image->pixels.data(),static_cast<int>(image->width*4))){m_error=SDL_GetError();return false;}}
        ++m_uploads;
    }
    for(const auto& item:missing){m_entries[item.sprite]=item.dest;m_valid[item.sprite]=true;}
    core::profile::gauge(core::profile::GpuCacheBytes,bytes());
    core::profile::gauge(core::profile::CacheMisses,m_misses);
    core::profile::gauge(core::profile::CacheUploads,m_uploads);
    core::profile::gauge(core::profile::CacheEvictions,m_evictions);
    if(diagnose&&!missing.empty()){
        const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        if(us>33333){
#ifdef O1602_EXPERIMENTAL_VITA
            platform::vita::trace("SPRITE_REFILL atlas=%llu needed=%u missing=%u elapsed_us=%llu storage_order=%d gpu_bytes=%llu",
                (unsigned long long)atlas.identity(),unsigned(needed.size()),unsigned(missing.size()),(unsigned long long)us,m_storage_order,(unsigned long long)bytes());
#else
            std::fprintf(stderr,"SPRITE_REFILL needed=%u missing=%u elapsed_us=%llu storage_order=%d\n",unsigned(needed.size()),unsigned(missing.size()),(unsigned long long)us,m_storage_order);
#endif
        }
    }
    return true;
}
}
