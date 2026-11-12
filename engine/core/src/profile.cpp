// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/profile.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <mutex>
namespace o1602::core::profile {
namespace {
#ifdef __vita__
std::atomic<bool> collecting{false};
#else
std::atomic<bool> collecting{true};
#endif
constexpr std::size_t capacity=2048;
std::array<std::atomic<std::uint64_t>,Count> totals{},calls{};
std::array<std::uint64_t,Count> previous{},baseline{},baseline_calls{};
std::array<std::atomic<std::uint64_t>,GaugeCount> current{},peaks{};
struct LongFrame {std::uint64_t number=0,ns=0;unsigned zoom=0;Scene scene=Menu;std::array<std::uint64_t,Count> stages{};};
struct Event {std::uint64_t frame=0,tick=0;char text[80]{};};
constexpr unsigned before=30, after=60, trace_size=before+after+1;
struct Trace {Event event{};std::array<std::array<std::uint32_t,Count+1>,trace_size> us{};
    std::array<std::uint64_t,trace_size> number{};};
struct SceneStats {std::uint64_t count=0,total=0,over33=0,over50=0,over100=0;
    std::array<std::uint64_t,capacity> ns{};std::array<std::uint64_t,Count> stages{};};
struct State {
    std::array<std::array<std::uint64_t,capacity>,Count+1> samples{};
    std::uint64_t frames=0,total_frame=0,max_frame=0,long_frames=0,over33=0,over100=0;
    std::array<LongFrame,16> slow{};
    std::array<Event,32> events{};unsigned event_count=0;
    std::array<Trace,16> traces{};unsigned critical_count=0,other_count=0;
    std::array<SceneStats,SceneCount> scenes{};Scene scene=Menu;
    std::array<std::uint64_t,Count> totals{},calls{};
    std::array<std::uint64_t,GaugeCount> current{},peaks{};
    std::array<LongFrame,8> worst{};
};
// Static storage: copying/sorting these must not exhaust the Vita's 256 KiB stack.
State live{}, pending{};
std::mutex pending_mutex;
bool pending_valid=false;
const char* names[]={"events","input","simulation_including_ai","render_inclusive","sprites",
 "submit","present_including_driver_wait","explicit_wait","audio_mix_and_control","audio_decode",
 "video_inclusive","file_io","atlas_decode","atlas_pack_inclusive","hit_masks","upload",
 "cache_miss","log_io","ai","render_commands","mixer","texture_batches","background_log_io","atlas_compress","sprite_inflate","music_read_background","effect_read_background","network_host_inclusive","network_client_inclusive","network_fingerprint","network_poll","cursor_ground_query","cursor_unit_pick","hover_status","hud_resources","cursor_ground_compute","video_decode_background","video_convert_background","video_upload","sprite_prefetch_background","present_interval","sdl_input_to_present","sea_pathfinding","ambient_tick","hud_draw","texture_create","sea_fill","sea_scan","sea_search","sprite_wait","sprite_read_main","sprite_read_worker","hud_resources_draw","hud_panel_draw","hud_minimap_draw","hud_status_draw","hud_chat_draw","save_serialize","save_write","event_dispatch","network_select","menu_open","sprite_backing_io","sprite_file_lock","sprite_block_wait","goods_layout","goods_draw","diagnostic_state_hash","map_direct_draw","map_canvas_draw","map_canvas_setup","map_canvas_resolve"};
static_assert(std::size(names)==Count);
}
bool enabled() noexcept {return collecting.load(std::memory_order_relaxed);}
void set_enabled(bool value) noexcept {collecting.store(value,std::memory_order_relaxed);}
void add(Metric m,std::uint64_t ns) noexcept {
    if(!enabled())return;
    totals[m].fetch_add(ns,std::memory_order_relaxed);calls[m].fetch_add(1,std::memory_order_relaxed);
}
void gauge(Gauge g,std::uint64_t value) noexcept {
    if(!enabled())return;
    current[g].store(value,std::memory_order_relaxed);
    auto peak=peaks[g].load(std::memory_order_relaxed);
    while(value>peak&&!peaks[g].compare_exchange_weak(peak,value,std::memory_order_relaxed)){}
}
void increment(Gauge g,std::uint64_t amount) noexcept {
    if(!enabled())return;
    const auto value=current[g].fetch_add(amount,std::memory_order_relaxed)+amount;
    auto peak=peaks[g].load(std::memory_order_relaxed);
    while(value>peak&&!peaks[g].compare_exchange_weak(peak,value,std::memory_order_relaxed)){}
}
void begin() noexcept {
    if(!enabled())return;
    for(unsigned i=0;i<Count;++i){baseline[i]=previous[i]=totals[i].load(std::memory_order_relaxed);baseline_calls[i]=calls[i].load(std::memory_order_relaxed);}
}
void scene(Scene s) noexcept {if(enabled())live.scene=s;}
void mark(const char* text,std::uint64_t tick) noexcept {
    if(!enabled())return;
    auto& e=live.events[live.event_count++%live.events.size()];e.frame=live.frames+1;e.tick=tick;
    std::strncpy(e.text,text,sizeof(e.text)-1);e.text[sizeof(e.text)-1]=0;
    // Reserve half the slots for state/announcement events. Frequent effects may
    // not overwrite the evidence of a city being founded a few frames earlier.
    const bool critical=std::strcmp(text,"city_founded")==0||std::strcmp(text,"own_city_founded")==0||
        std::strcmp(text,"announcement")==0||std::strcmp(text,"discovery")==0||std::strcmp(text,"goods_message")==0;
    auto& tr=live.traces[critical?(live.critical_count++%8):(8+live.other_count++%8)];
    tr={};tr.event=e;
    for(unsigned n=1;n<=before&&n<=live.frames;++n){const auto number=live.frames+1-n;
        const auto slot=(number-1)%capacity;const auto dest=before-n;tr.number[dest]=number;
        for(unsigned m=0;m<=Count;++m)tr.us[dest][m]=static_cast<std::uint32_t>(std::min<std::uint64_t>(UINT32_MAX,live.samples[m][slot]/1000));}
}
void frame(std::uint64_t ns) noexcept {
    if(!enabled())return;
    auto& [samples,frames,total_frame,max_frame,long_frames,over33,over100,slow,events,event_count,traces,critical_count,other_count,scenes,scene,st,sc,cu,pe,worst]=live;
    const auto slot=static_cast<std::size_t>(frames%capacity);
    samples[Count][slot]=ns;total_frame+=ns;max_frame=std::max(max_frame,ns);if(ns>50000000)++long_frames;
    for(unsigned i=0;i<Count;++i){const auto now=totals[i].load(std::memory_order_relaxed);
        samples[i][slot]=now-previous[i];previous[i]=now;
        st[i]=now-baseline[i];sc[i]=calls[i].load(std::memory_order_relaxed)-baseline_calls[i];}
    if(ns>50000000){auto& entry=slow[(long_frames-1)%slow.size()];entry.number=frames+1;entry.ns=ns;entry.zoom=static_cast<unsigned>(current[WorldZoom].load());entry.scene=scene;
        for(unsigned i=0;i<Count;++i)entry.stages[i]=samples[i][slot];}
    if(ns>50000000){
        auto it=std::min_element(worst.begin(),worst.end(),[](const auto& a,const auto& b){return a.ns<b.ns;});
        if(ns>it->ns){it->number=frames+1;it->ns=ns;it->zoom=static_cast<unsigned>(current[WorldZoom].load());it->scene=scene;for(unsigned i=0;i<Count;++i)it->stages[i]=samples[i][slot];}
    }
    auto& scene_stats=scenes[scene];scene_stats.ns[scene_stats.count%capacity]=ns;
    ++scene_stats.count;scene_stats.total+=ns;
    if(ns>33333333){++over33;++scene_stats.over33;}
    if(ns>50000000)++scene_stats.over50;
    if(ns>100000000){++over100;++scene_stats.over100;}
    for(unsigned m=0;m<Count;++m)scene_stats.stages[m]+=samples[m][slot];
    for(auto& tr:traces)if(tr.event.frame&&frames+1>=tr.event.frame&&frames+1<=tr.event.frame+after){
        const auto dest=before+frames+1-tr.event.frame;tr.number[dest]=frames+1;
        for(unsigned m=0;m<=Count;++m)tr.us[dest][m]=static_cast<std::uint32_t>(std::min<std::uint64_t>(UINT32_MAX,samples[m][slot]/1000));}
    ++frames;
}
bool capture() noexcept {
    if(!enabled())return false;
    std::unique_lock lock(pending_mutex,std::try_to_lock);
    if(!lock.owns_lock()||pending_valid)return false;
    pending=live;
    // live.totals/calls belong to the last completed frame. Sampling current
    // atomics here would mix an unfinished frame into the completed-frame count.
    for(unsigned g=0;g<GaugeCount;++g){pending.current[g]=current[g].load();pending.peaks[g]=peaks[g].load();}
    pending_valid=true;return true;
}
std::string captured_summary() {
    if(!enabled())return {};
    std::lock_guard lock(pending_mutex);
    if(!pending_valid)return {};
    pending_valid=false;
    const auto& [samples,frames,total_frame,max_frame,long_frames,over33,over100,slow,events,event_count,traces,critical_count,other_count,scenes,scene,totals,calls,current,peaks,worst]=pending;
    std::string out="{\"frames\":"+std::to_string(frames)+",\"window\":"+std::to_string(std::min<std::uint64_t>(frames,capacity))+
        ",\"long_frames_over_50ms\":"+std::to_string(long_frames)+",\"max_frame_us\":"+std::to_string(max_frame/1000)+",\"metrics\":{";
    const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(frames,capacity));
    for(unsigned m=0;m<=Count;++m){
        std::vector<std::uint64_t> sorted(samples[m].begin(),samples[m].begin()+static_cast<std::ptrdiff_t>(count));
        std::sort(sorted.begin(),sorted.end());
        const auto percentile=[&](unsigned p){return count?sorted[std::min(count-1,(count*p)/100)]/1000:0;};
        const auto total=m==Count?total_frame:totals[m];
        if(m)out+=',';
        out+='"';out+=m==Count?"frame":names[m];out+="\":{\"mean_us\":"+std::to_string(frames?total/frames/1000:0)+
            ",\"median_us\":"+std::to_string(percentile(50))+",\"p95_us\":"+std::to_string(percentile(95))+
            ",\"p99_us\":"+std::to_string(percentile(99))+",\"max_window_us\":"+std::to_string(count?sorted.back()/1000:0)+
            ",\"calls\":"+std::to_string(m==Count?frames:calls[m])+",\"mean_per_call_us\":"+std::to_string(m==Count?(frames?total/frames/1000:0):(calls[m]?total/calls[m]/1000:0))+"}";
    }
    out+="},\"sampled_gauges\":{";
    const char* gauges[]={"libc_used_bytes","libc_arena_bytes","world_gpu_cache_bytes","cache_misses","cache_uploads","cache_evictions","audio_queued_frames","audio_queue_errors","audio_empty_observations","kernel_free_user_bytes","kernel_free_cdram_bytes","map_canvas_color_bytes","map_canvas_allocation_attempts","music_prefetch_not_ready","music_buffer_bytes","sprite_backing_ram_bytes","sprite_backing_disk_reads","effect_cache_bytes","effect_pending","sdk_cpu_mhz","sdk_gpu_mhz","sdk_bus_mhz","sdk_xbar_mhz","sprite_prefetch_rgba_bytes","sprite_prefetch_hits","sprite_prefetch_jobs","ui_panel_cache_bytes","ui_panel_cache_hits","ui_panel_cache_builds","sea_plans","sea_visited","sprite_sync_reads","sprite_prefetch_ready","sprite_prefetch_joined","sprite_prefetch_rejected_jobs","sprite_prefetch_rejected_bytes","sprite_read_calls","sprite_read_bytes","sprite_block_hits","sprite_block_bytes","video_compressed_bytes","world_zoom","ui_window_cache_bytes","ui_window_cache_hits","ui_window_cache_builds"};
    static_assert(std::size(gauges)==GaugeCount);
    for(unsigned g=0;g<GaugeCount;++g){if(g)out+=',';out+='"';out+=gauges[g];out+="\":{\"current\":"+std::to_string(current[g])+",\"max_observed\":"+std::to_string(peaks[g])+"}";}
    out+="},\"recent_long_frames\":[";
    const auto n=std::min<std::uint64_t>(long_frames,slow.size());
    for(std::uint64_t i=long_frames-n;i<long_frames;++i){const auto& e=slow[i%slow.size()];if(i!=long_frames-n)out+=',';
        out+="{\"frame\":"+std::to_string(e.number)+",\"us\":"+std::to_string(e.ns/1000)+",\"zoom\":"+std::to_string(e.zoom)+",\"scene\":"+std::to_string(static_cast<unsigned>(e.scene))+",\"stages_us\":{";
        for(unsigned m=0;m<Count;++m){if(m)out+=',';out+='"';out+=names[m];out+="\":"+std::to_string(e.stages[m]/1000);}out+="}}";
    }
    out+="],\"worst_long_frames\":[";
    auto ordered=worst;std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.ns>b.ns;});
    bool first_worst=true;
    for(const auto& e:ordered)if(e.number){
        if(!first_worst)out+=',';first_worst=false;
        out+="{\"frame\":"+std::to_string(e.number)+",\"us\":"+std::to_string(e.ns/1000)+",\"zoom\":"+std::to_string(e.zoom)+",\"scene\":"+std::to_string(static_cast<unsigned>(e.scene))+",\"stages_us\":{";
        for(unsigned m=0;m<Count;++m){if(m)out+=',';out+='"';out+=names[m];out+="\":"+std::to_string(e.stages[m]/1000);}out+="}}";
    }
    out+="],\"recent_events\":[";
    const auto en=std::min<unsigned>(event_count,static_cast<unsigned>(events.size()));
    for(unsigned i=event_count-en;i<event_count;++i){const auto& e=events[i%events.size()];if(i!=event_count-en)out+=',';
        out+="{\"frame\":"+std::to_string(e.frame)+",\"tick\":"+std::to_string(e.tick)+",\"event\":\"";
        for(const char* p=e.text;*p;++p){if(*p=='"'||*p=='\\')out+='\\';if(static_cast<unsigned char>(*p)>=32)out+=*p;}
        out+="\"}";
    }
    out+="],\"schema\":3,\"long_frames_over_33_3ms\":"+std::to_string(over33)+",\"long_frames_over_100ms\":"+std::to_string(over100)+",\"scenes\":{";
    const char* scene_names[]={"menu","world","video","loading","benchmark_idle","benchmark_cursor"};
    for(unsigned i=0;i<SceneCount;++i){const auto& st=scenes[i];if(i)out+=',';out+='"';out+=scene_names[i];
        auto sorted=std::vector<std::uint64_t>(st.ns.begin(),st.ns.begin()+static_cast<std::ptrdiff_t>(std::min<std::uint64_t>(st.count,capacity)));
        std::sort(sorted.begin(),sorted.end());const auto q=[&](unsigned p){return sorted.empty()?0:sorted[std::min(sorted.size()-1,sorted.size()*p/100)]/1000;};
        out+="\":{\"frames\":"+std::to_string(st.count)+",\"total_us\":"+std::to_string(st.total/1000)+",\"mean_us\":"+std::to_string(st.count?st.total/st.count/1000:0)+
            ",\"median_us\":"+std::to_string(q(50))+",\"p95_us\":"+std::to_string(q(95))+",\"p99_us\":"+std::to_string(q(99))+
            ",\"over_33_3ms\":"+std::to_string(st.over33)+",\"over_50ms\":"+std::to_string(st.over50)+",\"over_100ms\":"+std::to_string(st.over100)+",\"mean_stages_us\":{";
        for(unsigned m=0;m<Count;++m){if(m)out+=',';out+='"';out+=names[m];out+="\":"+std::to_string(st.count?st.stages[m]/st.count/1000:0);}out+="}}";
    }
    out+="},\"event_trace_columns\":[\"frame\",\"frame_us\"";
    for(unsigned m=0;m<Count;++m){out+=",\"";out+=names[m];out+='"';}out+="],\"event_traces\":[";
    bool comma=false;
    for(const auto& tr:traces){if(!tr.event.frame)continue;if(comma)out+=',';comma=true;
        out+="{\"event\":\"";out+=tr.event.text;out+="\",\"frame\":"+std::to_string(tr.event.frame)+",\"tick\":"+std::to_string(tr.event.tick)+",\"frames\":[";
        bool fc=false;for(unsigned f=0;f<trace_size;++f){if(!tr.number[f])continue;if(fc)out+=',';fc=true;
            out+="["+std::to_string(tr.number[f])+","+std::to_string(tr.us[f][Count]);
            for(unsigned m=0;m<Count;++m)out+=","+std::to_string(tr.us[f][m]);
            out+=']';}out+="]}";
    }
    return out+"]}";
}
std::string summary() {
    capture();return captured_summary();
}
}
