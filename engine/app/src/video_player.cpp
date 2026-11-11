// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_player.hpp"
#include "o1602/core/profile.hpp"
#include "o1602/core/workers.hpp"
#include "o1602/core/hash.hpp"
#include "o1602/ui/localization.hpp"
#ifdef O1602_EXPERIMENTAL_VITA
#include "o1602/platform/vita.hpp"
#include <psp2/kernel/sysmem.h>
#include <malloc.h>
#include <psp2/kernel/threadmgr.h>
#endif
#include <SDL3/SDL.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "video_queue.hpp"

namespace o1602::app {
namespace {
void video_log(const char* format,...) {
#ifdef O1602_EXPERIMENTAL_VITA
    if(!platform::vita::logging_enabled())return;
#endif
    char line[1024];va_list ap;va_start(ap,format);std::vsnprintf(line,sizeof(line),format,ap);va_end(ap);
    std::fprintf(stderr,"%s\n",line);
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::trace("%s",line);
#endif
}
void memory(const char* phase,unsigned id,std::uint64_t cpu,std::uint64_t texture) {
#ifdef O1602_EXPERIMENTAL_VITA
    if(!platform::vita::logging_enabled())return;
    SceKernelFreeMemorySizeInfo mem{};mem.size=sizeof(mem);const int rc=sceKernelGetFreeMemorySize(&mem);
    video_log("VIDEO_MEMORY id=%u phase=%s heap=%u cpu_frame_bytes=%llu texture_bytes=%llu free_user=%u free_cdram=%u rc=%d",
        id,phase,unsigned(mallinfo().uordblks),static_cast<unsigned long long>(cpu),static_cast<unsigned long long>(texture),unsigned(mem.size_user),unsigned(mem.size_cdram),rc);
#else
    (void)phase;(void)id;(void)cpu;(void)texture;
#endif
}
std::optional<std::filesystem::path> find_video(const assets::GameDirectory& game,unsigned n) {
#ifdef O1602_EXPERIMENTAL_VITA
    if(n==58&&!std::filesystem::exists("ux0:data/open1602/video-test-history-intro.flag")) {
        return game.resolve("VideoSmk/58.smk");
    }
#endif
    const auto base="VideoSmk/"+std::to_string(n);
    if(auto smk=game.resolve(base+".smk"))return smk;
    return game.resolve(base+".bk2");
}
void copy_row(const std::uint8_t* src,std::uint8_t* dst,std::size_t count){
#if defined(__ARM_NEON)
    while(count>=64){const auto a=vld1q_u8(src),b=vld1q_u8(src+16),c=vld1q_u8(src+32),d=vld1q_u8(src+48);
        vst1q_u8(dst,a);vst1q_u8(dst+16,b);vst1q_u8(dst+32,c);vst1q_u8(dst+48,d);src+=64;dst+=64;count-=64;}
#endif
    std::copy_n(src,count,dst);
}
std::uint32_t position(const VideoPlayer::Clip& clip){return std::visit([](const auto& v){return v.position();},clip);}
int creator_priority() {
#ifdef O1602_EXPERIMENTAL_VITA
    SceKernelThreadInfo info{};info.size=sizeof(info);
    if(sceKernelGetThreadInfo(sceKernelGetThreadId(),&info)>=0)return info.currentPriority;
#endif
    return 0;
}
}
struct VideoPlayer::Frame {
    std::vector<std::uint8_t> pixels;
    unsigned index=0,w=0,h=0;
    bool yuv=false,smooth=false;
};
struct VideoPlayer::Loading {
    enum WorkState : unsigned { Opening, AudioSetup, Waiting, Decoding, Converting, Publishing, Finished, TestStall };
    const bool diagnostics=core::profile::enabled();
    std::atomic<unsigned> work_state{Opening},work_frame{0};
    std::atomic<std::uint64_t> state_ms{0};
    std::atomic<int> thread_id{-1};
    void working(WorkState state,unsigned frame=0) noexcept {
        if(!diagnostics)return;
        work_frame=frame;state_ms=SDL_GetTicks();work_state=state;
    }
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> cancel{false};
    std::atomic<unsigned> phase{0},progress{0},decoded{0};
    std::deque<std::unique_ptr<Frame>> ready;
    std::vector<std::unique_ptr<Frame>> free;
    std::filesystem::path file;
    std::vector<std::shared_ptr<const audio::Pcm>> audio;
    std::string error;
    std::uint64_t per_frame=0;
    unsigned count=0,want=2,id=0,tracks=0,delay_ms=0,stall_after=0;
    bool loaded=false,finished=false,scalar=false,reference_hashes=false,fail_audio=false;
    Loading(){for(unsigned i=0;i<3;++i)free.push_back(std::make_unique<Frame>());}
};
namespace {
class FailedVideoSamples final: public audio::SampleSource {
    std::size_t m_frames;
public:
    explicit FailedVideoSamples(std::size_t frames):m_frames(frames){}
    std::size_t frames() const noexcept override{return m_frames;}
    std::size_t buffered_bytes() const noexcept override{return 0;}
    std::int16_t sample(std::size_t,std::uint8_t) const override{throw std::runtime_error("QA injected video audio read error");}
};
}
struct VideoPlayer::Worker {
    std::mutex mutex;
    std::condition_variable wake;
    std::shared_ptr<Loading> pending,active;
    bool quit=false;
    // libpthread starts at a lower priority than the renderer on Vita. The
    // pre-00.14 decoder ran on the render thread; retain that scheduling class.
    int render_priority=creator_priority();
    std::thread thread;
    Worker():thread([this]{run();}){}
    ~Worker(){
        {std::lock_guard lock(mutex);quit=true;if(active){active->cancel=true;active->wake.notify_all();}if(pending)pending->cancel=true;}
        wake.notify_all();thread.join();
    }
    void submit(const std::shared_ptr<Loading>& state){
        {std::lock_guard lock(mutex);if(pending)pending->cancel=true;pending=state;}wake.notify_one();
    }
    static core::Result<Clip> open(const std::shared_ptr<Loading>& s) {
        video_log("VIDEO_PHASE id=%u phase=open_begin ms=%llu",s->id,static_cast<unsigned long long>(SDL_GetTicks()));
        const auto audio=[&](auto& clip)->core::Status {
            if constexpr(!std::is_same_v<std::decay_t<decltype(clip)>,video::Bink>)
                if(!s->audio.empty())return core::Ok{};
            s->phase=1;s->working(Loading::AudioSetup);video_log("VIDEO_PHASE id=%u phase=audio_begin ms=%llu",s->id,static_cast<unsigned long long>(SDL_GetTicks()));
            for(unsigned i=0;i<8;++i){
                bool present=false;
                if constexpr(std::is_same_v<std::decay_t<decltype(clip)>,video::Bink>)present=i<clip.info().tracks.size();
                else if constexpr(std::is_same_v<std::decay_t<decltype(clip)>,video::Webm>) present=i<clip.info().tracks.size();
                else present=i<video::kSmackerTracks&&clip.info().tracks[i].present;
                if(!present||(s->tracks&&!(s->tracks&(1U<<i))))continue;
                auto pcm=[&] {
                    if constexpr(std::is_same_v<std::decay_t<decltype(clip)>,video::Bink>) return clip.stream_audio(i);
                    else if constexpr(std::is_same_v<std::decay_t<decltype(clip)>,video::Webm>) return clip.audio(i);
                    else return clip.stream_audio(i);
                }();
                if(!pcm)return pcm.error();
                if(pcm->frames()) {
                    if(pcm->source) {
                        if(s->fail_audio)pcm->source=std::make_shared<FailedVideoSamples>(pcm->frames());
                        *pcm=audio::prefetch_pcm(std::move(*pcm),false);
                        video_log("VIDEO_AUDIO_STREAM id=%u track=%u rate=%u channels=%u prebuffer_ms=250",s->id,i,pcm->rate,unsigned(pcm->channels));
                    }
                    s->audio.push_back(std::make_shared<const audio::Pcm>(std::move(*pcm)));
                }
            }
            video_log("VIDEO_PHASE id=%u phase=audio_end ms=%llu tracks=%u",s->id,static_cast<unsigned long long>(SDL_GetTicks()),unsigned(s->audio.size()));
            return core::Ok{};
        };
        auto extension=s->file.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(extension==".bk2"){
            auto clip=video::Bink::open_file(s->file);if(!clip)return clip.error();
            video_log("VIDEO_PHASE id=%u phase=open_end codec=bink2 width=%u height=%u frames=%u ms=%llu",s->id,clip->info().width,clip->info().height,clip->info().frames,static_cast<unsigned long long>(SDL_GetTicks()));
            if(clip->info().width*std::uint64_t(clip->info().height)>=1024*768){
                const int priority=creator_priority();
                const bool parallel=clip->parallel_decode(true,[priority]{
#ifdef O1602_EXPERIMENTAL_VITA
                    const int rc=sceKernelChangeThreadPriority(sceKernelGetThreadId(),priority);
                    video_log("VIDEO_SLICE_THREAD priority=%d rc=%d",priority,rc);
                    platform::vita::configure_background_worker("bink_slice");
#endif
                });
                video_log("VIDEO_PARALLEL id=%u helpers=%u",s->id,parallel?1U:0U);
            }
            if(auto st=audio(*clip);!st)return st.error();
            return Clip{std::move(*clip)};
        }
        if (extension == ".webm") {
            auto clip = video::Webm::open_file(s->file);
            if (!clip) return clip.error();
            if (auto st=audio(*clip); !st) return st.error();
            return Clip{std::move(*clip)};
        }
        std::size_t compressed_budget=8*1024*1024;
#ifdef O1602_EXPERIMENTAL_VITA
        if(std::filesystem::exists("ux0:data/open1602/video-compressed-cache-off.flag"))compressed_budget=0;
#endif
        auto clip=video::Smacker::open_file(s->file,compressed_budget);if(!clip)return clip.error();
        video_log("VIDEO_COMPRESSED_CACHE id=%u bytes=%u budget=8388608",s->id,unsigned(clip->resident_bytes()));
        video_log("VIDEO_PHASE id=%u phase=open_end codec=smacker width=%u height=%u frames=%u ms=%llu",s->id,clip->info().width,clip->info().height,clip->info().frames,static_cast<unsigned long long>(SDL_GetTicks()));
        if(auto st=audio(*clip);!st)return st.error();
        return Clip{std::move(*clip)};
    }
    static void convert(const Clip& clip,Frame& f) {
        std::visit([&](const auto& v){
            using V=std::decay_t<decltype(v)>;
            f.w=v.info().width;
            if constexpr(std::is_same_v<V,video::Bink>){
                f.h=v.info().height;f.smooth=true;
                if(const auto view=v.yuv420()){
                    f.yuv=true;const auto cw=(f.w+1)/2,ch=(f.h+1)/2;
                    f.pixels.resize(std::size_t(f.w)*f.h+2*std::size_t(cw)*ch);std::size_t at=0;
                    for(unsigned i=0;i<3;++i){const unsigned w=i?cw:f.w,h=i?ch:f.h;const auto& p=view->planes[i];
                        for(unsigned y=0;y<h;++y){copy_row(p.bytes.data()+std::size_t(y)*p.stride,f.pixels.data()+at,w);at+=w;}}
                    return;
                }
            }else if constexpr(std::is_same_v<V,video::Smacker>) {f.h=v.info().display_height();f.smooth=false;}
            else {f.h=v.info().height;f.smooth=true;}
            f.yuv=false;v.to_rgba(f.pixels);
        },clip);f.index=position(clip);
    }
    static void decode(const std::shared_ptr<Loading>& s) {
        s->working(Loading::Opening);
        video::bink_simd(!s->scalar);
        core::worker_enter("video_decode");
        video_log("VIDEO_SIMD id=%u requested=%d verified=%d",s->id,!s->scalar,video::bink_simd_verified());
#ifdef O1602_EXPERIMENTAL_VITA
        s->thread_id=sceKernelGetThreadId();
        SceKernelThreadInfo info{};info.size=sizeof(info);
        const int info_rc=sceKernelGetThreadInfo(sceKernelGetThreadId(),&info);
        video_log("VIDEO_THREAD id=%u rc=%d priority=%d affinity=%d cpu=%d",s->id,info_rc,info.currentPriority,info.currentCpuAffinityMask,info.currentCpuId);
#endif
        auto opened=open(s);if(!opened){if(!s->cancel)s->error=opened.error().message;return;}
        auto clip=std::move(*opened);
        s->count=std::visit([](const auto& v){return v.info().frames;},clip);
        s->per_frame=std::visit([](const auto& v)->std::uint64_t{
            if constexpr(std::is_same_v<std::decay_t<decltype(v)>,video::Bink>)return v.info().frame_us();else return v.info().frame_us;
        },clip);
        if(!s->count||!s->per_frame){s->error="invalid video duration";return;}
        while(!s->cancel&&position(clip)<s->count){
            std::unique_ptr<Frame> frame;
            s->working(Loading::Waiting,position(clip)+1);
            {std::unique_lock lock(s->mutex);s->wake.wait(lock,[&]{return s->cancel||(s->ready.size()<2&&!s->free.empty()&&position(clip)<s->want);});
                if(s->cancel)return;
                frame=std::move(s->free.back());s->free.pop_back();}
            #ifdef O1602_EXPERIMENTAL_VITA
            SceKernelThreadInfo before{};before.size=sizeof(before);
            const auto before_rc=sceKernelGetThreadInfo(sceKernelGetThreadId(),&before);
#endif
            if(s->stall_after&&position(clip)==s->stall_after){
                s->working(Loading::TestStall,position(clip)+1);
                std::unique_lock lock(s->mutex);
                s->wake.wait_for(lock,std::chrono::seconds(3),[&]{return s->cancel.load();});
                if(s->cancel)return;
                s->stall_after=0;
            }
            const auto begin=SDL_GetTicks();if(position(clip)==0)video_log("VIDEO_PHASE id=%u phase=first_decode_begin ms=%llu",s->id,static_cast<unsigned long long>(begin));
            s->working(Loading::Decoding,position(clip)+1);
            if(s->delay_ms)std::this_thread::sleep_for(std::chrono::milliseconds(s->delay_ms));
            auto result=std::visit([](auto& v){core::profile::Scope timer(core::profile::VideoDecode);return v.next();},clip);
            if(!result){s->error=result.error().message;return;}if(s->cancel)return;
            if(s->reference_hashes)if(const auto* b=std::get_if<video::Bink>(&clip))if(auto y=b->yuv420()){
                video_log("VIDEO_REFERENCE id=%u position=%u y=%llu u=%llu v=%llu",s->id,position(clip),
                    static_cast<unsigned long long>(core::xxh64(y->planes[0].bytes)),static_cast<unsigned long long>(core::xxh64(y->planes[1].bytes)),static_cast<unsigned long long>(core::xxh64(y->planes[2].bytes)));
            }
            const auto convert_begin=SDL_GetTicksNS();
            s->working(Loading::Converting,position(clip));
            {core::profile::Scope timer(core::profile::VideoConvert);convert(clip,*frame);}
            if(s->reference_hashes&&!frame->yuv)video_log("VIDEO_REFERENCE_RGBA id=%u position=%u rgba=%llu",s->id,frame->index,
                static_cast<unsigned long long>(core::xxh64(frame->pixels)));
            const auto convert_us=(SDL_GetTicksNS()-convert_begin)/1000;
            if(frame->index==1||frame->index%30==0||(s->diagnostics&&(SDL_GetTicks()-begin)*1000>s->per_frame)){
                std::uint64_t read_us=0,codec_us=0;
                if(const auto* b=std::get_if<video::Bink>(&clip)){auto t=b->last_frame_timing();read_us=t.read_us;codec_us=t.decode_us;}
                else if(const auto* smk=std::get_if<video::Smacker>(&clip)){auto t=smk->last_frame_timing();read_us=t.read_us;codec_us=t.decode_us;}
                std::uint64_t cpu_us=0;
#ifdef O1602_EXPERIMENTAL_VITA
                SceKernelThreadInfo after{};after.size=sizeof(after);
                if(before_rc>=0&&sceKernelGetThreadInfo(sceKernelGetThreadId(),&after)>=0)cpu_us=after.runClocks-before.runClocks;
#endif
                video_log("VIDEO_STAGE id=%u position=%u read_us=%llu codec_us=%llu convert_us=%llu thread_run_delta=%llu",s->id,frame->index,
                    static_cast<unsigned long long>(read_us),static_cast<unsigned long long>(codec_us),static_cast<unsigned long long>(convert_us),static_cast<unsigned long long>(cpu_us));
            }
            s->decoded=frame->index;s->phase=2;
            if(frame->index==1||frame->index%30==0)video_log("VIDEO_DECODE id=%u position=%u elapsed_ms=%llu frame_bytes=%u yuv=%d",s->id,frame->index,static_cast<unsigned long long>((SDL_GetTicks()-begin)),unsigned(frame->pixels.size()),frame->yuv);
            s->working(Loading::Publishing,frame->index);
            {std::lock_guard lock(s->mutex);s->ready.push_back(std::move(frame));s->loaded=true;}
        }
    }
    void run(){
#ifdef O1602_EXPERIMENTAL_VITA
        if(render_priority>0){
            const int rc=sceKernelChangeThreadPriority(sceKernelGetThreadId(),render_priority);
            video_log("VIDEO_WORKER_PRIORITY target=%d rc=%d source=render_thread",render_priority,rc);
        }
#endif
        for(;;){std::shared_ptr<Loading> job;
            {std::unique_lock lock(mutex);wake.wait(lock,[&]{return quit||pending;});if(quit)return;active=job=std::move(pending);}
            try{decode(job);}catch(const std::exception& e){job->error=e.what();}catch(...){job->error="unknown decoder failure";}
            {std::lock_guard lock(job->mutex);job->finished=true;}
            job->working(Loading::Finished,job->decoded.load());
            const auto id=job->id,decoded=job->decoded.load();const bool cancelled=job->cancel.load(),error=!job->error.empty();
            {std::lock_guard lock(mutex);active.reset();}
            job.reset();video_log("VIDEO_WORKER_RELEASE id=%u cancelled=%d decoded=%u error=%d ms=%llu",id,cancelled,decoded,error,static_cast<unsigned long long>(SDL_GetTicks()));
        }
    }
};
VideoPlayer::VideoPlayer(SDL_Renderer* renderer,const assets::GameDirectory& game):m_renderer(renderer),m_game(&game){}
VideoPlayer::~VideoPlayer(){stop();m_worker.reset();}
void VideoPlayer::set_package_videos(std::vector<std::filesystem::path> files) {
    m_package_videos = std::move(files);
    m_available.clear();
}
std::optional<std::filesystem::path> VideoPlayer::find(std::uint16_t n) const {
    if (n >= kFirstPackageVideo) {
        const auto index = std::size_t(n - kFirstPackageVideo);
        return index < m_package_videos.size() ? std::optional(m_package_videos[index]) : std::nullopt;
    }
    return find_video(*m_game, n);
}
bool VideoPlayer::available(std::uint16_t n) const {
#ifdef O1602_EXPERIMENTAL_VITA
    if(n==58)return true; // permit a useful missing-file notice through the normal menu
#endif
auto it=m_available.find(n);if(it!=m_available.end())return it->second;return m_available[n]=find(n).has_value();}
bool VideoPlayer::start(std::uint16_t n,bool loop,std::uint8_t tracks){return begin(n,loop,tracks,{});}
bool VideoPlayer::begin(std::uint16_t n,bool loop,std::uint8_t tracks,const std::vector<std::shared_ptr<const audio::Pcm>>& reuse){
    stop();m_missing_cd=false;auto path=find(n);if(!path){
        m_available[n]=false;
#ifdef O1602_EXPERIMENTAL_VITA
        if(n==58){m_number=n;m_missing_cd=true;failed("CD intro missing: ux0:data/open1602/original/VideoSmk/58.smk","cd_intro_missing");}
#endif
        return false;
    }
    video_log("VIDEO_SOURCE id=%u path=%s",unsigned(n),path->string().c_str());
    m_number=n;m_loop=loop;m_tracks=tracks;m_requested_ms=SDL_GetTicks();m_failure=false;m_error_until=0;
#ifdef O1602_EXPERIMENTAL_VITA
    m_test_texture=std::filesystem::exists("ux0:data/open1602/video-test-fail-texture.flag");
    m_test_upload=std::filesystem::exists("ux0:data/open1602/video-test-fail-upload.flag");
#endif
    memory("before_start",n,0,0);video_log("VIDEO_REQUEST id=%u ms=%llu",unsigned(n),static_cast<unsigned long long>(m_requested_ms));
    try{
        if(!m_worker)m_worker=std::make_unique<Worker>();
        auto job=std::make_shared<Loading>();job->file=*path;job->id=n;job->tracks=tracks;job->audio=reuse;
#ifdef O1602_EXPERIMENTAL_VITA
        job->fail_audio=std::filesystem::exists("ux0:data/open1602/video-test-fail-audio.flag");
        job->scalar=std::filesystem::exists("ux0:data/open1602/video-test-scalar.flag");
        job->reference_hashes=std::filesystem::exists("ux0:data/open1602/video-test-reference.flag");
        if(std::filesystem::exists("ux0:data/open1602/video-test-stall-126.flag")){job->stall_after=126;video_log("VIDEO_QA stall_after=126 stall_ms=3000");}
        if(std::filesystem::exists("ux0:data/open1602/video-test-slow-decode.flag")){job->delay_ms=200;video_log("VIDEO_QA slow_decode_ms=200");}
#endif
        m_loading=job;m_worker->submit(job);return true;
    }catch(const std::exception& e){failed(e.what(),"worker_start");stop();return false;}
}
VideoPlayer::Load VideoPlayer::poll(std::uint64_t now){
    if(!loading())return Load::None;
    if(SDL_GetTicks()-m_requested_ms>120000){failed("video preparation timed out (120 seconds)","preparation");stop();return Load::Failed;}
    bool failed_load=false;std::string error;const char* phase="preparation";
    {std::lock_guard lock(m_loading->mutex);
        if(m_loading->ready.empty()){
            if(!m_loading->finished)return Load::Waiting;
            failed_load=true;error=m_loading->error;
        }else {
        for(const auto& sound:m_loading->audio) {
            if(sound->source){
                if(const auto* message=sound->source->error()){failed_load=true;error=message;phase="audio_stream";break;}
                if(!sound->source->prepare(0,std::min(sound->frames(),std::size_t(sound->rate)/4+2)))return Load::Waiting;
            }
        }
        if(!failed_load){
            m_frame=std::move(m_loading->ready.front());m_loading->ready.pop_front();
            m_audio=m_loading->audio;m_count=m_loading->count;m_frame_us=m_loading->per_frame;
        }
        }
    }
    if(failed_load){failed(error.c_str(),phase);stop();return Load::Failed;}
    m_loading->wake.notify_one();m_ready=true;m_start_ms=now;m_dirty=true;m_last_decode_ms=SDL_GetTicks();m_seen_decoded=m_loading->decoded.load();
    if(!make_texture()){stop();return Load::Failed;}
    video_log("VIDEO_READY id=%u width=%u height=%u frames=%u frame_us=%llu tracks=%u texture=1 yuv=%d load_ms=%llu",unsigned(m_number),m_frame->w,m_frame->h,m_count,static_cast<unsigned long long>(m_frame_us),unsigned(m_audio.size()),m_frame->yuv,static_cast<unsigned long long>((SDL_GetTicks()-m_requested_ms)));
    return Load::Ready;
}
bool VideoPlayer::make_texture(){
    release_texture();if(!m_frame)return false;
#ifdef O1602_EXPERIMENTAL_VITA
    if(m_test_texture){failed("QA injected texture allocation failure","texture_create");return false;}
#endif
    auto props=SDL_CreateProperties();
    SDL_SetStringProperty(props,"open1602.texture.role","video");
#ifdef O1602_EXPERIMENTAL_VITA
    SDL_SetBooleanProperty(props,"open1602.qa.force_main_ram",std::filesystem::exists("ux0:data/open1602/video-test-mainram.flag"));
#endif
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,m_frame->yuv?SDL_PIXELFORMAT_IYUV:SDL_PIXELFORMAT_RGBA32);
    // Match the original decoder's matrix directly into display RGB, without
    // adding a second gamut/transfer transform that to_rgba() never applied.
    const auto colorspace=m_frame->yuv?static_cast<SDL_Colorspace>(SDL_DEFINE_COLORSPACE(SDL_COLOR_TYPE_YCBCR,SDL_COLOR_RANGE_LIMITED,SDL_COLOR_PRIMARIES_BT709,SDL_TRANSFER_CHARACTERISTICS_SRGB,SDL_MATRIX_COEFFICIENTS_BT601,SDL_CHROMA_LOCATION_LEFT)):SDL_COLORSPACE_SRGB;
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER,colorspace);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,m_frame->w);SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,m_frame->h);
    const auto begin=SDL_GetTicks();m_texture=SDL_CreateTextureWithProperties(m_renderer,props);SDL_DestroyProperties(props);
    if(!m_texture){failed(SDL_GetError(),"texture_create");return false;}
    SDL_SetTextureScaleMode(m_texture,m_frame->smooth?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(m_texture,m_frame->yuv?SDL_BLENDMODE_NONE:SDL_BLENDMODE_BLEND);
    m_texture_w=int(m_frame->w);m_texture_h=int(m_frame->h);m_yuv=m_frame->yuv;m_dirty=true;
    const auto bytes=m_yuv?std::uint64_t(m_frame->w)*m_frame->h+2*std::uint64_t((m_frame->w+1)/2)*((m_frame->h+1)/2):std::uint64_t(m_frame->w)*m_frame->h*4;
    video_log("VIDEO_TEXTURE id=%u width=%d height=%d format=%s bytes=%llu elapsed_ms=%llu",unsigned(m_number),m_texture_w,m_texture_h,m_yuv?"IYUV_BT601_LIMITED":"RGBA32",static_cast<unsigned long long>(bytes),static_cast<unsigned long long>((SDL_GetTicks()-begin)));memory("texture_ready",m_number,m_frame->pixels.size(),bytes);return true;
}
void VideoPlayer::failed(const char* why,const char* phase){video_log("VIDEO_ERROR id=%u phase=%s message=%s",unsigned(m_number),phase,why);m_failure=true;m_error_until=SDL_GetTicks()+6000;}
void VideoPlayer::release_texture(){if(m_texture){SDL_DestroyTexture(m_texture);m_texture=nullptr;}m_texture_w=m_texture_h=0;}
void VideoPlayer::recreate_texture(){if(m_ready&&!make_texture())m_failure=true;}
void VideoPlayer::set_renderer(SDL_Renderer* r){m_renderer=r;recreate_texture();}
void VideoPlayer::hold_until(std::uint64_t ms) noexcept {m_start_ms=std::max(m_start_ms,ms);}
void VideoPlayer::sync_audio(std::optional<std::uint64_t> mixed_ms,unsigned queued_ms) noexcept {
    if(!mixed_ms){m_audio_time_us.reset();return;}
    const auto audible=(*mixed_ms>queued_ms?*mixed_ms-queued_ms:0)*1000;
    m_audio_time_us=std::max(m_audio_time_us.value_or(0),audible);
}
void VideoPlayer::stop(){
    const unsigned n=m_number;
    if(m_loading){video_log("VIDEO_STOP id=%u position=%u frames=%u uploads=%u dropped=%u",n,m_frame?m_frame->index:0,m_count,m_uploads,m_dropped);m_loading->cancel=true;m_loading->wake.notify_all();}
    m_loading.reset();m_frame.reset();m_audio.clear();release_texture();m_number=0;m_audio_time_us.reset();m_av_log_ms=0;
    m_ready=m_dirty=m_failure=m_drawn=m_presented=false;m_uploads=m_dropped=m_last_upload=m_count=m_last_presented=0;m_state_log_ms=0;
    if(n)memory("after_stop_render_thread",n,0,0);
}
VideoPlayer::Step VideoPlayer::update(std::uint64_t now){
    core::profile::Scope timer(core::profile::Video);
    if(!m_loading)return Step::Ended;
    if(!m_ready)return Step::Playing;
    if(m_failure){stop();return Step::Ended;}
    for(const auto& sound:m_audio)if(sound->source)if(const auto* message=sound->source->error()){
        failed(message,"audio_stream");stop();return Step::Ended;
    }
    if(now<m_start_ms)return Step::Playing;
    const auto wall_elapsed=(now-m_start_ms)*1000;
    const auto duration=std::uint64_t(m_count)*m_frame_us;
    // At nominal EOF the last picture may still be queued. Finish only after
    // its actual present, with a bounded two-second tail and the normal lag guard.
    if(wall_elapsed>=duration&&m_last_presented>=m_count){
        if(m_loop){const auto n=m_number;const auto t=m_tracks;const auto sound=std::vector<std::shared_ptr<const audio::Pcm>>{};begin(n,true,t,sound);return Step::Playing;}
        m_dropped+=m_count-m_last_upload;
        video_log("VIDEO_END id=%u decoded=%u frames=%u elapsed_ms=%llu",unsigned(m_number),m_loading->decoded.load(),m_count,static_cast<unsigned long long>((now-m_start_ms)));stop();return Step::Ended;
    }
    const auto elapsed=m_audio_time_us.value_or(wall_elapsed);
    if(SDL_GetTicks()-m_av_log_ms>=2000){
        m_av_log_ms=SDL_GetTicks();
        video_log("VIDEO_AV_CLOCK id=%u wall_ms=%llu audio_ms=%llu audio_master=%d",unsigned(m_number),
            static_cast<unsigned long long>(wall_elapsed/1000),static_cast<unsigned long long>(elapsed/1000),bool(m_audio_time_us));
    }
    if(wall_elapsed>elapsed+500000){failed("audio stream is over 500 ms behind the playback clock","audio_deadline");stop();return Step::Ended;}
    const auto decoded=m_loading->decoded.load();
    if(decoded!=m_seen_decoded){m_seen_decoded=decoded;m_last_decode_ms=SDL_GetTicks();}
    if(decoded<m_count&&SDL_GetTicks()-m_last_decode_ms>15000){failed("decoder made no progress for 15 seconds","decode_wait");stop();return Step::Ended;}
    const auto due=unsigned(std::min<std::uint64_t>(elapsed/m_frame_us+1,m_count));
    if(m_loading->diagnostics&&due>decoded&&std::uint64_t(due-decoded)*m_frame_us>250000&&SDL_GetTicks()-m_state_log_ms>=500){
        m_state_log_ms=SDL_GetTicks();diagnose("behind",due,elapsed,wall_elapsed);
    }
    if(due>decoded && std::uint64_t(due-decoded)*m_frame_us>2000000) {
        diagnose("decode_deadline",due,elapsed,wall_elapsed);
        failed("picture decoder is over two seconds behind the playback clock","decode_deadline");
        stop();return Step::Ended;
    }
    if(wall_elapsed>duration+2000000){
        diagnose("end_deadline",due,elapsed,wall_elapsed);
        failed("final picture was not presented within two seconds of nominal EOF","end_deadline");stop();return Step::Ended;
    }
    {std::lock_guard lock(m_loading->mutex);
        if(m_loading->finished&&!m_loading->error.empty()){failed(m_loading->error.c_str(),"decode");}
        while(!m_loading->ready.empty()&&m_loading->ready.front()->index<=due){
            if(m_frame)m_loading->free.push_back(std::move(m_frame));
            m_frame=std::move(m_loading->ready.front());m_loading->ready.pop_front();m_dirty=true;
        }
        m_loading->want=std::min(m_count,due+2);
    }
    m_loading->wake.notify_one();return Step::Playing;
}
bool VideoPlayer::upload(){
    if(!m_dirty)return true;
    if(!m_frame||!m_texture)return false;
    const auto begin=SDL_GetTicks();bool ok=false;
#ifdef O1602_EXPERIMENTAL_VITA
    if(m_test_upload){failed("QA injected texture upload failure","texture_upload");return false;}
#endif
    {core::profile::Scope timer(core::profile::VideoUpload);
        if(m_yuv){const auto w=m_frame->w,h=m_frame->h,cw=(w+1)/2,ch=(h+1)/2;const auto* p=m_frame->pixels.data();
            ok=SDL_UpdateYUVTexture(m_texture,nullptr,p,int(w),p+std::size_t(w)*h,int(cw),p+std::size_t(w)*h+std::size_t(cw)*ch,int(cw));
        }else ok=SDL_UpdateTexture(m_texture,nullptr,m_frame->pixels.data(),m_texture_w*4);
    }
    if(!ok){failed(SDL_GetError(),"texture_upload");return false;}
    const auto p=m_frame->index;if(p>m_last_upload){m_dropped+=p-m_last_upload-1;m_last_upload=p;++m_uploads;}
    if(m_uploads==1||m_uploads%30==0)video_log("VIDEO_FRAME id=%u position=%u uploads=%u dropped=%u upload_ms=%llu",unsigned(m_number),p,m_uploads,m_dropped,static_cast<unsigned long long>((SDL_GetTicks()-begin)));
    m_dirty=false;return true;
}
void VideoPlayer::draw_status(int,int,int,int){
    if(SDL_GetTicks()-m_last_progress_ms>=1000){m_last_progress_ms=SDL_GetTicks();video_log("VIDEO_LOADING id=%u phase=%u percent=%u wait_ms=%llu",unsigned(m_number),m_loading?m_loading->phase.load():0,m_loading?m_loading->progress.load():0,static_cast<unsigned long long>((SDL_GetTicks()-m_requested_ms)));}
}
void VideoPlayer::draw_in(int x,int y,int width,int height){
    SDL_SetRenderDrawColor(m_renderer,0,0,0,255);SDL_FRect area{float(x),float(y),float(width),float(height)};SDL_RenderFillRect(m_renderer,&area);
    if(width<=0||height<=0)return;
    if(loading()){draw_status(x,y,width,height);return;}
    if(!m_frame||!m_texture||!upload())return;
    const double scale=std::min(double(width)/m_texture_w,double(height)/m_texture_h);const double w=std::floor(m_texture_w*scale),h=std::floor(m_texture_h*scale);
    SDL_FRect dst{float(x+std::floor((width-w)/2)),float(y+std::floor((height-h)/2)),float(w),float(h)};m_drawn=SDL_RenderTexture(m_renderer,m_texture,nullptr,&dst);
}
void VideoPlayer::draw(int width,int height){
    SDL_SetRenderDrawColor(m_renderer,0,0,0,255);SDL_RenderClear(m_renderer);
    if(loading()){draw_status(0,0,width,height);return;}
    if(!m_frame||!m_texture||!upload())return;
    const double scale=m_number==58?std::min(double(width)/m_texture_w,double(height)/m_texture_h):std::min({2.0*width/1024.0,double(width)/m_texture_w,double(height)/m_texture_h});const double w=std::floor(m_texture_w*scale),h=std::floor(m_texture_h*scale);
    SDL_FRect dst{float(std::floor((width-w)/2)),float(std::floor((height-h)/2)),float(w),float(h)};m_drawn=SDL_RenderTexture(m_renderer,m_texture,nullptr,&dst);
}
void VideoPlayer::draw_notice(int width,int height){
    if(SDL_GetTicks()>=m_error_until)return;
    float sx=1,sy=1;SDL_GetRenderScale(m_renderer,&sx,&sy);SDL_SetRenderScale(m_renderer,1,1);
    SDL_SetRenderDrawColor(m_renderer,255,220,120,255);
    SDL_RenderDebugText(m_renderer,16,float(std::max(16,height-48)),ui::tr_utf8(m_missing_cd?"vita.video.cd_missing":"vita.video.error").c_str());
    SDL_SetRenderScale(m_renderer,sx,sy);(void)width;
}
void VideoPlayer::diagnose(const char* reason,unsigned due,std::uint64_t elapsed,std::uint64_t wall){
    if(!m_loading||!m_loading->diagnostics)return;
    const auto& s=*m_loading;int ready=-1,free=-1,want=-1,finished=-1;
    {std::unique_lock lock(m_loading->mutex,std::try_to_lock);if(lock.owns_lock()){
        ready=int(s.ready.size());free=int(s.free.size());want=int(s.want);finished=s.finished;
    }}
    const char* states[]={"open","audio","wait","decode","convert","publish","finished","qa_stall"};
    const auto state=s.work_state.load();const auto entered=s.state_ms.load();const auto now=SDL_GetTicks();
    video_log("VIDEO_WORKER_STATE id=%u reason=%s ms=%llu due=%u decoded=%u active_frame=%u state=%s state_age_ms=%llu ready=%d free=%d want=%d finished=%d audio_ms=%llu wall_ms=%llu last_upload=%u last_presented=%u",
        unsigned(m_number),reason,static_cast<unsigned long long>(now),due,s.decoded.load(),s.work_frame.load(),state<std::size(states)?states[state]:"unknown",
        static_cast<unsigned long long>(now>=entered?now-entered:0),ready,free,want,finished,static_cast<unsigned long long>(elapsed/1000),static_cast<unsigned long long>(wall/1000),m_last_upload,m_last_presented);
#ifdef O1602_EXPERIMENTAL_VITA
    SceKernelThreadInfo info{};info.size=sizeof(info);const int rc=sceKernelGetThreadInfo(s.thread_id.load(),&info);
    video_log("VIDEO_WORKER_CPU id=%u rc=%d cpu=%d priority=%d affinity=%d run_clocks=%llu",unsigned(m_number),rc,info.currentCpuId,info.currentPriority,info.currentCpuAffinityMask,static_cast<unsigned long long>(info.runClocks));
#endif
}
void VideoPlayer::presented(){if(m_drawn){m_last_presented=m_last_upload;if(!m_presented){m_presented=true;video_log("VIDEO_PRESENTED id=%u position=%u ms=%llu request_to_present_ms=%llu",unsigned(m_number),m_frame?m_frame->index:0,static_cast<unsigned long long>(SDL_GetTicks()),static_cast<unsigned long long>((SDL_GetTicks()-m_requested_ms)));}}m_drawn=false;}
}
