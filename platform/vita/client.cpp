// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE 1
#include "capunlocker_config.hpp"
#include <fstream>
// Platform bindings only. All menus, actions, rendering, media and simulation are upstream.
#include "o1602/platform/vita.hpp"
#include "o1602/platform/vita_paths.hpp"
#include "startup.h"
#include "o1602/ui/localization.hpp"
#include "o1602/core/profile.hpp"
#include "o1602/core/workers.hpp"
#include "o1602/ui/stick_repeat.hpp"
#include "o1602/ui/stick_state.hpp"
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <exception>
#include <SDL3/SDL.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/ime_dialog.h>
#include <psp2/touch.h>
#include <psp2/kernel/rng.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include "network_lifecycle.hpp"
#include "cpu_policy.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
unsigned int _newlib_heap_size_user = 128 * 1024 * 1024;
extern "C" { unsigned int _pthread_stack_default_user = 256 * 1024; }
extern "C" void SDL_SendTouch(Uint64, SDL_TouchID, SDL_FingerID, SDL_Window*, SDL_EventType, float, float, float);
extern "C" void SDL_SendTouchMotion(Uint64, SDL_TouchID, SDL_FingerID, SDL_Window*, float, float, float);
namespace o1602::platform::vita {
namespace {
std::thread diagnostic_writer;
std::atomic<bool> writer_stop{false}, writer_pause{false};
std::atomic<bool> snapshot_ready{false},analog_ready{false};
unsigned test_frame_ms=0;std::atomic<unsigned> test_writer_ms{0};
bool scripts_after_world=false,diagnostic_frame_hashes=false;
std::uint64_t poll_started=0;
void initialize_workers();
void write_profile(const std::string& json);
void write_analog_snapshot(const char* path="ux0:data/open1602/analog-snapshot.txt");
void start_writer() {
    if(!vita_boot_logging()||diagnostic_writer.joinable())return;
    try {diagnostic_writer=std::thread([] {
        // Below gameplay/SDL threads. No clocks are changed.
        sceKernelChangeThreadPriority(0,0x10000120);
        unsigned cycles=0;
        while(!writer_stop.load(std::memory_order_acquire)) {
            sceKernelDelayThread(100000);
            if(writer_pause.load(std::memory_order_acquire))continue;
            // Separate metric from main-thread log_io: background durations are
            // not frame stalls and may overlap rendering and atlas work.
            try {
            if(analog_ready.exchange(false,std::memory_order_acq_rel))write_analog_snapshot();
            if(snapshot_ready.exchange(false,std::memory_order_acq_rel)){
                core::profile::Scope io(core::profile::LogBackground);
                write_profile(core::profile::captured_summary());
            }
            if(++cycles%10==0){core::profile::Scope io(core::profile::LogBackground);
                if(const auto delay=test_writer_ms.load())sceKernelDelayThread(delay*1000);
                vita_boot_flush();}
            }catch(...){vita_boot_write("WARN diagnostic snapshot skipped; previous profile retained");}
        }
        try {if(snapshot_ready.exchange(false))write_profile(core::profile::captured_summary());}
        catch(...){vita_boot_write("WARN final queued profile skipped");}
        vita_boot_flush();
    });}catch(...){vita_boot_write("WARN diagnostic writer unavailable; RAM log remains bounded");}
}
SDL_Gamepad* pad=nullptr;
std::array<bool,SDL_SCANCODE_COUNT> held{};
std::array<bool,SDL_GAMEPAD_BUTTON_COUNT> previous{};
std::array<bool,6> mouse_down{};
float cursor_x=480, cursor_y=272;
struct PadEvent {unsigned frame;char type;int control,value;};
std::vector<PadEvent> script;
SDL_Joystick* virtual_pad=nullptr;
SDL_JoystickID virtual_id=0;
bool pad_buttons_armed=false,fast_grid=true;
unsigned left_speed=100,right_speed=100;
bool test_pad_valid=true;std::array<int,4> test_complete_axes{};unsigned test_complete_mask=0;
unsigned input_frame=0;
ui::StickRepeat cursor_repeat{170,68},menu_repeat{200,80};
ui::StickState cursor_state,camera_state;
ui::PadFreshness pad_freshness;
PadContext last_context{};
bool last_actions=false,last_front=false,last_snapped=false,analog_debug=false;
unsigned logical_x=0,logical_y=0,logical_tool=0;
std::uint64_t last_pad_event=0,last_grid_step=0,direction_since=0;
std::array<int,4> sdl_axes{};
struct AnalogRecord {std::uint64_t ms,event,step;int raw[4],sdl[4];float x,y,cx,cy;int dx,dy;unsigned tile_x,tile_y,tool,mode,touch,buttons;};
std::array<AnalogRecord,128> analog_ring{},analog_snapshot{};
unsigned analog_at=0,analog_count=0,analog_snapshot_count=0;
std::mutex analog_mutex;
void write_analog_snapshot(const char* path){
    std::array<AnalogRecord,128> copy;unsigned count=0;
    {std::lock_guard lock(analog_mutex);copy=analog_snapshot;count=analog_snapshot_count;}
    if(FILE* f=std::fopen(path,"w")){
        std::fprintf(f,
                     "session=%s version=1.10 commit=%s; raw=-1 means SDL virtual/external; thresholds .30/.22; no "
                     "diagnosis of hardware defect\n",
                     vita_boot_session(), O1602_VITA_COMMIT);
        for(unsigned i=0;i<count;++i){const auto& a=copy[i];std::fprintf(f,"ms=%llu raw=%d,%d,%d,%d sdl=%d,%d,%d,%d norm=%.4f,%.4f direction=%d,%d mode=%u tool=%u cursor=%.2f,%.2f tile=%u,%u event=%llu step=%llu touch=%u buttons=%u\n",(unsigned long long)a.ms,a.raw[0],a.raw[1],a.raw[2],a.raw[3],a.sdl[0],a.sdl[1],a.sdl[2],a.sdl[3],a.x,a.y,a.dx,a.dy,a.mode,a.tool,a.cx,a.cy,a.tile_x,a.tile_y,(unsigned long long)a.event,(unsigned long long)a.step,a.touch,a.buttons);}
        std::fflush(f);std::fclose(f);
    }
}

bool touch_debug=false,front_down=false,free_override=false;
bool pointer_event=false,suppress_front=false;
bool legacy=false,actions_open=false,help_open=false,build_browse=false,focus_pending=false,road_latched=false;
unsigned action_selected=0;
float map_x=390,map_y=270;
struct QuickAction {const char* label;SDL_Keycode key;SDL_Keymod mod=SDL_KMOD_NONE;};
const QuickAction actions[]={
    {"vita.quick.save",SDLK_F5},{"vita.quick.load",SDLK_F9},
    {"vita.quick.rotate_left",SDLK_Q},{"vita.quick.rotate_right",SDLK_E},
    {"vita.quick.faster",SDLK_KP_PLUS},{"vita.quick.slower",SDLK_KP_MINUS},
    {"vita.quick.pause",SDLK_F3},{"vita.quick.ship",SDLK_PERIOD},
    {"vita.quick.ships",SDLK_L},{"vita.quick.cities",SDLK_L,SDL_KMOD_SHIFT},
    {"vita.quick.diplomacy",SDLK_V},{"vita.quick.demolish",SDLK_DELETE},
    {"vita.quick.cursor",0},{"vita.quick.help",0}};

unsigned fail_cache_frame=0;
std::uint64_t last_touch=0;
char touch_line[220]="Touch: waiting";
struct TouchEvent {unsigned frame,port;char kind;float x,y;};
std::vector<TouchEvent> touch_events;
unsigned touch_frame=0;
void raw_touch() {
    static unsigned last[2]={0,0};
    for(unsigned port=0;port<2;++port){SceTouchData data{};
        if(sceTouchPeek(port,&data,1)<0)continue;
        if(data.reportNum||last[port]){
            const unsigned x=data.reportNum?data.report[0].x:0,y=data.reportNum?data.report[0].y:0;
            trace("TOUCH_RAW sensor=%s contacts=%u x=%u y=%u",port?"back":"front",data.reportNum,x,y);
        }
        last[port]=data.reportNum;
    }
}
void key(SDL_Keycode code,SDL_Keymod mod=SDL_KMOD_NONE) {
    SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=code;e.key.which=0x1602;e.key.mod=mod;
    e.key.scancode=SDL_GetScancodeFromKey(code,nullptr);e.key.down=true;SDL_PushEvent(&e);
    e.type=SDL_EVENT_KEY_UP;e.key.down=false;SDL_PushEvent(&e);
}
void mouse(SDL_Window* window, unsigned button, bool down) {
    if(button<mouse_down.size())mouse_down[button]=down;
    SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID=SDL_GetWindowID(window);e.button.button=static_cast<Uint8>(button);
    e.button.down=down;e.button.x=cursor_x;e.button.y=cursor_y;SDL_PushEvent(&e);
}
}
bool logging_enabled(){return vita_boot_logging();}
void trace(const char* format, ...) {
    if(!vita_boot_logging())return;
    char buffer[2048];
    va_list args;va_start(args,format);std::vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
    const bool durable=std::strncmp(buffer,"FAIL ",5)==0;
    if(durable)vita_boot_flush();
    vita_boot_write(buffer);
    vita_boot_client_write(buffer);
    if(durable)vita_boot_flush();
}
void phase(const char* message) {
    if(vita_boot_logging()){const auto info=mallinfo();core::profile::gauge(core::profile::HeapUsed,unsigned(info.uordblks));
    core::profile::gauge(core::profile::HeapArena,unsigned(info.arena));}vita_boot_phase(message);
}
void fatal(const char* message) {vita_boot_flush();vita_boot_fatal(message);}
void first_presented() {vita_boot_presented();start_writer();}
void boot_options(bool automatic,const char* fail_phase) {vita_boot_options(automatic,fail_phase);}
void boot_wait() {vita_boot_wait();}

void setup() {
    core::profile::set_enabled(vita_boot_logging());
    phase("BEGIN main / platform setup");
    std::set_terminate([] { fatal("C++ terminate / uncaught exception (see last startup phase)"); });
    // SDL's Vita backend converts the front touchscreen to normal mouse events.
    SDL_SetLogOutputFunction([](void*,int category,SDL_LogPriority priority,const char* message) {
        trace("SDL_LOG category=%d priority=%d %s",category,int(priority),message);
    },nullptr);
    // Backend allocation diagnostics are restricted to large/video textures,
    // spill and fallback operations; ordinary glyph allocations stay silent.
    SDL_SetLogPriorities(vita_boot_logging()?SDL_LOG_PRIORITY_INFO:SDL_LOG_PRIORITY_CRITICAL);
    if(std::filesystem::exists("ux0:data/open1602/grid-normal.flag"))grid_speed(false);
    analog_debug=vita_boot_logging()&&std::filesystem::exists("ux0:data/open1602/analog-debug.flag");
    touch_debug=vita_boot_logging()&&std::filesystem::exists("ux0:data/open1602/touch-debug.flag");
    phase("BEGIN SDL hints");
    SDL_SetHint(SDL_HINT_VITA_TOUCH_MOUSE_DEVICE,"0");
    SDL_SetHint(SDL_HINT_VITA_ENABLE_FRONT_TOUCH,"1");
    SDL_SetHint(SDL_HINT_VITA_ENABLE_BACK_TOUCH,"1"); // available to optional diagnostic; never generates mouse clicks
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"1");
    phase("OK SDL hints");
    // A previous interrupted run can leave only our own generated cache files.
    std::error_code cache_error;
    for(const auto& entry:std::filesystem::directory_iterator("ux0:data/open1602/cache",cache_error)){
        const auto name=entry.path().filename().string();
        if(name.starts_with("atlas-")&&name.ends_with(".cache"))std::filesystem::remove(entry.path(),cache_error);
    }
    sceIoMkdir("ux0:data/open1602",0777);
    if(vita_boot_logging()){
    sceIoMkdir("ux0:data/open1602/logs",0777);
    phase("BEGIN archive previous session");
    archive_previous_session();
    sceIoRemove("ux0:data/open1602/analog-snapshot.txt");
    sceIoRemove("ux0:data/open1602/analog-final.txt");
    sceIoRemove("ux0:data/open1602/client.previous.log");
    sceIoRename("ux0:data/open1602/client.log","ux0:data/open1602/client.previous.log");
    sceIoRemove("ux0:data/open1602/logs/stderr.previous.log");
    sceIoRename("ux0:data/open1602/logs/stderr.log","ux0:data/open1602/logs/stderr.previous.log");
    if(FILE* file=std::fopen("ux0:data/open1602/client.log","w"))std::fclose(file);
    std::freopen("ux0:data/open1602/logs/stderr.log","w",stderr);
    }
    // Newlib's custom stream appends bytes to the bounded RAM ring. All engine
    // fprintf calls (including large construction bursts) retain their content.
    if(FILE* stream=funopen(nullptr,nullptr,vita_boot_stderr_write,nullptr,nullptr)) {
        std::fflush(stderr);stderr=stream;_GLOBAL_REENT->_stderr=stream;
        std::setvbuf(stream,nullptr,_IONBF,0);
    }else trace("WARN async stderr unavailable");
    if(!vita_boot_logging()){
        if(FILE* stream=funopen(nullptr,nullptr,vita_boot_stderr_write,nullptr,nullptr)){stdout=stream;_GLOBAL_REENT->_stdout=stream;std::setvbuf(stream,nullptr,_IONBF,0);}
    }
    setenv("ANNO1602_DIR","ux0:data/open1602/original",1);
    SceKernelThreadInfo thread{};thread.size=sizeof(thread);
    const int thread_result=sceKernelGetThreadInfo(sceKernelGetThreadId(),&thread);
    trace("MAIN_THREAD result=%d stack_bytes=%u stack_free=%d priority=%d affinity=%d cpu=%d",thread_result,
          thread_result<0?0:unsigned(thread.stackSize),sceKernelGetThreadStackFreeSize(sceKernelGetThreadId()),thread.currentPriority,thread.currentCpuAffinityMask,thread.currentCpuId);
    diagnostic_frame_hashes=vita_boot_logging()&&std::filesystem::exists("ux0:data/open1602/frame-hashes.flag");
    trace("FRAME_HASH_POLICY enabled=%d opt_in=frame-hashes.flag network_fingerprints=unchanged",diagnostic_frame_hashes);
    trace("SESSION id=%s version=1.10 commit=%s", vita_boot_session(), O1602_VITA_COMMIT);
    std::fprintf(stderr, "SESSION id=%s version=1.10 commit=%s\n", vita_boot_session(), O1602_VITA_COMMIT);
    trace("ANALOG_CAPTURE enabled=%d thresholds=.30/.22 grid_delay=%u grid_repeat=%u",analog_debug,fast_grid?170U:200U,fast_grid?68U:80U);
    trace("BOOT full_client=1 upstream=3fde8d11 pointer_bits=%u",unsigned(sizeof(void*)*8));
    trace("NETWORK deferred=1 enabled=%d",
#ifdef O1602_VITA_NETWORK
          1
#else
          0
#endif
    );
    phase("OK platform setup");
    (void)cpu_profile(444);
    initialize_workers();
    setup_connectivity();
}
std::uint32_t random_u32() {
    std::uint32_t value=0;
    if(sceKernelGetRandomNumber(&value,sizeof(value))<0)value=static_cast<std::uint32_t>(sceKernelGetProcessTimeWide());
    return value;
}
bool initialize_network(){
#ifdef O1602_VITA_NETWORK
    extern NetworkLifecycle sdk_network;
    extern const NetworkOps sdk_network_ops;
    if(sdk_network.ready())return true;
    const bool ready=sdk_network.start(sdk_network_ops);
    if(ready)trace("NETWORK net_init_rc=0 ctl_init_rc=0");
    return ready;
#else
    trace("NETWORK disabled_by_build=1");return false;
#endif
}
#ifdef O1602_VITA_NETWORK
NetworkLifecycle sdk_network;
const NetworkOps sdk_network_ops{
    []{return sceSysmoduleLoadModule(SCE_SYSMODULE_NET);},
    []{alignas(64) static std::array<unsigned char,1024*1024> memory;
       SceNetInitParam params{};params.memory=memory.data();params.size=memory.size();return sceNetInit(&params);},
    []{return sceNetCtlInit();},[]{sceNetCtlTerm();},[]{return sceNetTerm();},
    []{return sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);},
    [](const char* name,int rc){trace("NETWORK %s=%d",name,rc);}
};
bool terminate_network(){return sdk_network.stop(sdk_network_ops);}
#else
bool terminate_network(){return true;}
#endif
void boot_language(const char* code){vita_boot_language(code);}
const char* boot_ready_text(){return vita_boot_tr("vita.boot.sdl_ready");}
void ui_language(const char* code){
    vita_boot_language(code);
    trace("UI_LANGUAGE selected=%s quick_save=%s quick_load=%s",code,
          ui::tr_utf8("vita.quick.save").c_str(),ui::tr_utf8("vita.quick.load").c_str());
}
void refresh_stick_repeat(){
    const unsigned speed=legacy?right_speed:left_speed;
    cursor_repeat=ui::StickRepeat((fast_grid?170U:200U)*100/speed,(fast_grid?68U:80U)*100/speed);
}
void grid_speed(bool fast){fast_grid=fast;refresh_stick_repeat();}
void stick_sensitivity(unsigned left,unsigned right){
    left=std::clamp(left,25U,300U);right=std::clamp(right,25U,300U);
    if(left==left_speed&&right==right_speed)return;
    left_speed=left;right_speed=right;refresh_stick_repeat();
    trace("STICK_SPEED left=%u right=%u",left_speed,right_speed);
}
float camera_stick_speed(){return float(legacy?left_speed:right_speed)/100.f;}
void grid_position(unsigned x,unsigned y,unsigned tool){logical_x=x;logical_y=y;logical_tool=tool;}
void legacy_controls(bool on){legacy=on;refresh_stick_repeat();trace("CONTROLS legacy=%d cursor=%s camera=%s",legacy,legacy?"right":"left",legacy?"left":"right");}
void tool_selected(){if(!legacy&&build_browse&&pointer_event)focus_pending=true;}
bool grid_enabled(){return !free_override&&!build_browse&&!actions_open;}
void position_cursor(SDL_Window*,float x,float y){cursor_x=x;cursor_y=y;}
void test_timing(unsigned frame_ms,unsigned writer_ms){test_frame_ms=std::min(frame_ms,300U);test_writer_ms.store(std::min(writer_ms,1000U));trace("TEST_TIMING frame_ms=%u writer_delay_ms=%u",test_frame_ms,test_writer_ms.load());}
void wait_for_world_scripts(){scripts_after_world=true;}
bool scripts_wait_for_world(){return scripts_after_world;}
void poll(SDL_Window* window,const GridCursor& grid,PadContext context,bool scripts_ready) {
    poll_started=SDL_GetTicksNS();
    core::profile::Scope timer(core::profile::Input);
    if(!script.empty()&&!virtual_pad) {
        SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.name="Open1602 native input QA";
        desc.axis_mask=(1U<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.button_mask=(1U<<SDL_GAMEPAD_BUTTON_COUNT)-1;
        virtual_id=SDL_AttachVirtualJoystick(&desc);virtual_pad=SDL_OpenJoystick(virtual_id);
        trace("PAD_SCRIPT attach=%u opened=%d",virtual_id,virtual_pad!=nullptr);
    }
    if(virtual_pad&&scripts_ready) {
        for(const auto& e:script)if(e.frame==input_frame) {
            if(e.type=='a')SDL_SetJoystickVirtualAxis(virtual_pad,e.control,static_cast<Sint16>(e.value));
            else if(e.type=='b')SDL_SetJoystickVirtualButton(virtual_pad,e.control,e.value!=0);
            else if(e.type=='n'){test_complete_axes[e.control]=e.value;test_complete_mask|=1U<<e.control;}
            else if(e.type=='v')test_pad_valid=e.value!=0;
            trace("PAD_SCRIPT frame=%u type=%c control=%d value=%d",input_frame,e.type,e.control,e.value);
        }
        ++input_frame;
    }
    SDL_PumpEvents();
    for(const auto& e:touch_events)if(scripts_ready&&e.frame==touch_frame){
        if(e.kind=='m')SDL_SendTouchMotion(SDL_GetTicksNS(),e.port+1,1,window,e.x,e.y,1);
        else SDL_SendTouch(SDL_GetTicksNS(),e.port+1,1,window,e.kind=='d'?SDL_EVENT_FINGER_DOWN:SDL_EVENT_FINGER_UP,e.x,e.y,e.kind=='d'?1.f:0.f);
        trace("TOUCH_SCRIPT frame=%u port=%u kind=%c x=%.3f y=%.3f",touch_frame,e.port,e.kind,e.x,e.y);
    }
    if(scripts_ready)++touch_frame;
    int fingers=0;SDL_Finger** contacts=SDL_GetTouchFingers(1,&fingers);SDL_free(contacts);front_down=fingers>0;
    if(touch_debug)raw_touch();
    // SDL 3.2.24 leaves a RUNNING IME dialog alive in VITA_HideScreenKeyboard.
    // After a field loses focus, finish that platform dialog as well.
    if(!SDL_TextInputActive(window)&&SDL_ScreenKeyboardShown(window)) {
        const int abort=sceImeDialogAbort(),term=sceImeDialogTerm();
        trace("IME_CLOSE abort=%d term=%d",abort,term);
    }
    if(pad&&!SDL_GamepadConnected(pad)){
        cursor_state.interrupt();camera_state.interrupt();cursor_repeat.reset();held.fill(false);
        if(road_latched){key(SDLK_ESCAPE);road_latched=false;}
        for(unsigned b=1;b<mouse_down.size();++b)if(mouse_down[b])mouse(window,b,false);
        previous.fill(false);SDL_CloseGamepad(pad);pad=nullptr;trace("PAD_DISCONNECTED state=neutral");
    }
    if(!pad) {
        if(!SDL_WasInit(SDL_INIT_GAMEPAD))SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        int count=0;SDL_JoystickID* ids=SDL_GetGamepads(&count);
        if(count>0){pad=SDL_OpenGamepad(virtual_id?virtual_id:ids[0]);trace("GAMEPAD open=%d name=%s",pad!=nullptr,pad?SDL_GetGamepadName(pad):SDL_GetError());}
        SDL_free(ids);
    }
    if(!pad){held.fill(false);cursor_repeat.reset();return;}
    std::array<float,4> axes{};std::array<int,4> raw_axes{-1,-1,-1,-1};
    for(unsigned a=0;a<4;++a){sdl_axes[a]=SDL_GetGamepadAxis(pad,static_cast<SDL_GamepadAxis>(a));axes[a]=float(sdl_axes[a])/32768.f;}
    bool sample_valid=test_pad_valid,native_sample=false;std::uint32_t native_buttons=0;
    if(virtual_pad)for(unsigned a=0;a<4;++a)if(test_complete_mask&(1U<<a))axes[a]=float(test_complete_axes[a])/32768.f;
    // The SDL Vita backend only publishes changed axis bytes and keeps its old
    // values on failed peeks. The handheld is sampled as a complete fresh state;
    // virtual pads/external SDL devices keep the ordinary SDL full-state path.
    if(!virtual_pad&&SDL_GetGamepadName(pad)&&std::strcmp(SDL_GetGamepadName(pad),"PSVita Controller")==0){
        SceCtrlData raw{};const int result=sceCtrlPeekBufferPositive2(0,&raw,1);
        sample_valid=pad_freshness.sample(result,raw.timeStamp,SDL_GetTicks());
        native_sample=true;native_buttons=raw.buttons;
        raw_axes={raw.lx,raw.ly,raw.rx,raw.ry};
        for(unsigned a=0;a<4;++a)axes[a]=sample_valid?(raw_axes[a]>=128?float(raw_axes[a]-128)/127.f:float(raw_axes[a]-127)/127.f):0.f;
    }
    const bool context_changed=context.world!=last_context.world||context.dialog!=last_context.dialog||
        context.loading!=last_context.loading||context.tool!=last_context.tool||context.zoom!=last_context.zoom||
        context.world_id!=last_context.world_id||front_down!=last_front||actions_open!=last_actions;
    if(context_changed){cursor_state.interrupt();camera_state.interrupt();cursor_repeat.reset();menu_repeat.reset();}
    last_context=context;last_front=front_down;last_actions=actions_open;
    const auto physical_button=[&](SDL_GamepadButton b){
        if(!native_sample)return SDL_GetGamepadButton(pad,b);
        unsigned mask=0;switch(b){
            case SDL_GAMEPAD_BUTTON_SOUTH:mask=SCE_CTRL_CROSS;break;case SDL_GAMEPAD_BUTTON_EAST:mask=SCE_CTRL_CIRCLE;break;
            case SDL_GAMEPAD_BUTTON_WEST:mask=SCE_CTRL_SQUARE;break;case SDL_GAMEPAD_BUTTON_NORTH:mask=SCE_CTRL_TRIANGLE;break;
            case SDL_GAMEPAD_BUTTON_BACK:mask=SCE_CTRL_SELECT;break;case SDL_GAMEPAD_BUTTON_START:mask=SCE_CTRL_START;break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:mask=SCE_CTRL_L1;break;case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:mask=SCE_CTRL_R1;break;
            case SDL_GAMEPAD_BUTTON_DPAD_UP:mask=SCE_CTRL_UP;break;case SDL_GAMEPAD_BUTTON_DPAD_DOWN:mask=SCE_CTRL_DOWN;break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:mask=SCE_CTRL_LEFT;break;case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:mask=SCE_CTRL_RIGHT;break;
            default:break;
        }return (native_buttons&mask)!=0;
    };
    if(!sample_valid)pad_buttons_armed=false;
    else if(!pad_buttons_armed){bool down=false;for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b)down|=physical_button(static_cast<SDL_GamepadButton>(b));if(!down)pad_buttons_armed=true;}
    const auto button=[&](SDL_GamepadButton b){return sample_valid&&pad_buttons_armed&&physical_button(b);};
    const bool view_buttons = !legacy && context.world && !context.dialog && !context.editor && !actions_open;
    const bool cursor_dpad = !legacy && !view_buttons;
    const auto [left_x,left_y]=cursor_state.sample(axes[0]+(cursor_dpad?int(button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT))-int(button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)):0),axes[1]+(cursor_dpad?int(button(SDL_GAMEPAD_BUTTON_DPAD_DOWN))-int(button(SDL_GAMEPAD_BUTTON_DPAD_UP)):0),sample_valid);
    const auto [right_x,right_y]=camera_state.sample(axes[2],axes[3],sample_valid);
    std::array<float,4> filtered{left_x,left_y,right_x,right_y};
    const auto axis=[&](SDL_GamepadAxis a){return filtered[unsigned(a)];};
    if(!legacy){
        const auto pressed=[&](SDL_GamepadButton b){return button(b)&&!previous[b];};
        const auto cancel_drag=[&]{if(road_latched){key(SDLK_ESCAPE);mouse(window,SDL_BUTTON_LEFT,false);road_latched=false;trace("ROAD_CANCEL");}};
        if(!sample_valid){held.fill(false);cursor_repeat.reset();menu_repeat.reset();cancel_drag();}
        if(road_latched&&front_down){cancel_drag();suppress_front=true;trace("ROAD_TOUCH_CANCEL");}
        if(!front_down)suppress_front=false;
        if(road_latched&&(!context.road||context.dialog||!context.world))cancel_drag();
        if(context.loading){
            held.fill(false);cancel_drag();
            if(pressed(SDL_GAMEPAD_BUTTON_EAST))key(SDLK_ESCAPE);
            for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b)previous[b]=button(static_cast<SDL_GamepadButton>(b));
            return;
        }
        if(!context.quick_allowed){actions_open=false;help_open=false;}
        const bool native_dialog=pressed(SDL_GAMEPAD_BUTTON_BACK)&&sceImeDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING;
        if(pressed(SDL_GAMEPAD_BUTTON_BACK)){
            const bool text=context.text_active||SDL_TextInputActive(window);
            const char* reason=context.menu_active?"menu":context.loading?"loading":!context.world_loaded?"no_world":
                context.video_active?"video":context.modal_active?"modal":text?"text":native_dialog?"system_dialog":
                context.options_active?"options":!context.world?"hidden_world":!context.quick_allowed?"context":"none";
            trace("SELECT_CONTEXT button_received=1 world_loaded=%d world_visible=%d menu_active=%d modal_active=%d text_input_active=%d video_active=%d options_active=%d native_dialog=%d quick_menu_open=%d block_reason=%s",
                context.world_loaded,context.world,context.menu_active,context.modal_active,text,context.video_active,context.options_active,native_dialog,actions_open,reason);
        }
        if(pressed(SDL_GAMEPAD_BUTTON_BACK)&&context.editor&&!context.dialog&&!native_dialog&&!SDL_TextInputActive(window))key(SDLK_S,SDL_KMOD_CTRL);
        if(pressed(SDL_GAMEPAD_BUTTON_BACK)&&context.quick_allowed&&!native_dialog&&!SDL_TextInputActive(window)){cancel_drag();actions_open=!actions_open;help_open=false;cursor_state.interrupt();camera_state.interrupt();cursor_repeat.reset();menu_repeat.reset();filtered.fill(0);trace("QUICK_MENU open=%d",actions_open);}

        if(actions_open){
            held.fill(false);
            const auto [sx,sy]=menu_repeat.poll(axis(SDL_GAMEPAD_AXIS_LEFTX),axis(SDL_GAMEPAD_AXIS_LEFTY),SDL_GetTicks());
            (void)sx;
            if(sy)action_selected=static_cast<unsigned>((int(action_selected)+sy+int(std::size(actions)))%int(std::size(actions)));
            if(pressed(SDL_GAMEPAD_BUTTON_EAST)){if(help_open)help_open=false;else actions_open=false;}
            if(pressed(SDL_GAMEPAD_BUTTON_SOUTH)){
                if(help_open)help_open=false;
                else if(action_selected==std::size(actions)-1)help_open=true;
                else {const auto& a=actions[action_selected];actions_open=false;
                    if(a.key)key(a.key,a.mod);else {free_override=!free_override;trace("GRID free_override=%d",free_override);}}
                trace("QUICK_ACTION selected=%u",action_selected);
            }
        }else{
            menu_repeat.reset();
            const bool camera=context.world&&!context.dialog;
            held[SDL_SCANCODE_LEFT]=camera&&axis(SDL_GAMEPAD_AXIS_RIGHTX)<-.2f;
            held[SDL_SCANCODE_RIGHT]=camera&&axis(SDL_GAMEPAD_AXIS_RIGHTX)>.2f;
            held[SDL_SCANCODE_UP]=camera&&axis(SDL_GAMEPAD_AXIS_RIGHTY)<-.2f;
            held[SDL_SCANCODE_DOWN]=camera&&axis(SDL_GAMEPAD_AXIS_RIGHTY)>.2f;
            // Upstream #450 changes the tool already on CATEGORY press. Keep the
            // pending return until its popup closes, so the pad can still choose an icon.
            if(focus_pending&&!context.popup&&!button(SDL_GAMEPAD_BUTTON_SOUTH)&&!mouse_down[SDL_BUTTON_LEFT]){
                focus_pending=false;build_browse=false;position_cursor(window,map_x,map_y);
                SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=cursor_x;e.motion.y=cursor_y;SDL_PushEvent(&e);
                trace("BUILD_FOCUS map=1");
            }

            float ax=axis(SDL_GAMEPAD_AXIS_LEFTX),ay=axis(SDL_GAMEPAD_AXIS_LEFTY);
            float gx=cursor_x,gy=cursor_y;
            const bool snapped=!front_down&&grid_enabled()&&grid&&grid(0,0,gx,gy);
            if(snapped!=last_snapped){
                cursor_repeat.reset();cursor_state.interrupt();
                if(std::hypot(ax,ay)<.22f)cursor_state.sample(0,0);else ax=ay=0;
                last_snapped=snapped;trace("INPUT_MODE grid=%d release_to_rearm=%d",snapped,!cursor_state.armed());
            }
            float dx=0,dy=0;
            if(snapped){const auto [sx,sy]=cursor_repeat.poll(ax,ay,SDL_GetTicks());if(sx||sy){grid(sx,sy,gx,gy);last_grid_step=SDL_GetTicks();}dx=gx-cursor_x;dy=gy-cursor_y;}
            else {cursor_repeat.reset();dx=(std::abs(ax)<.244f?0.f:ax)*10.f*float(legacy?right_speed:left_speed)/100.f;dy=(std::abs(ay)<.244f?0.f:ay)*10.f*float(legacy?right_speed:left_speed)/100.f;}
            if(!front_down&&(dx||dy)){
                cursor_x=std::clamp(cursor_x+dx,0.f,959.f);cursor_y=std::clamp(cursor_y+dy,0.f,543.f);
                SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=SDL_GetWindowID(window);
                e.motion.x=cursor_x;e.motion.y=cursor_y;e.motion.xrel=dx;e.motion.yrel=dy;SDL_PushEvent(&e);
            }
            if(pressed(SDL_GAMEPAD_BUTTON_EAST)){key(SDLK_ESCAPE);if(road_latched){mouse(window,SDL_BUTTON_LEFT,false);road_latched=false;trace("ROAD_CANCEL");}build_browse=false;focus_pending=false;}
            if(pressed(SDL_GAMEPAD_BUTTON_NORTH)&&context.world&&!context.dialog){
                if(context.editor)key(SDLK_R);
                else {
                    cancel_drag();map_x=std::min(cursor_x,760.f);map_y=std::clamp(cursor_y,40.f,480.f);
                    build_browse=true;focus_pending=false;key(SDLK_B);position_cursor(window,850,425);
                    SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=cursor_x;e.motion.y=cursor_y;SDL_PushEvent(&e);
                    trace("BUILD_FOCUS hud=1");
                }
            }
            if(pressed(SDL_GAMEPAD_BUTTON_SOUTH)&&!front_down){
                if(SDL_TextInputActive(window))key(SDLK_RETURN);
                else if(context.road&&snapped){road_latched=!road_latched;mouse(window,SDL_BUTTON_LEFT,road_latched);trace(road_latched?"ROAD_BEGIN":"ROAD_COMMIT");}
                else mouse(window,SDL_BUTTON_LEFT,true);
            }
            if(!button(SDL_GAMEPAD_BUTTON_SOUTH)&&previous[SDL_GAMEPAD_BUTTON_SOUTH]&&!road_latched&&mouse_down[SDL_BUTTON_LEFT])mouse(window,SDL_BUTTON_LEFT,false);
            if(pressed(SDL_GAMEPAD_BUTTON_WEST)&&!front_down){if(context.building&&!build_browse)key(SDLK_R);else mouse(window,SDL_BUTTON_RIGHT,true);}
            if(!button(SDL_GAMEPAD_BUTTON_WEST)&&previous[SDL_GAMEPAD_BUTTON_WEST]&&mouse_down[SDL_BUTTON_RIGHT])mouse(window,SDL_BUTTON_RIGHT,false);
            if(pressed(SDL_GAMEPAD_BUTTON_START)&&(!context.editor||!context.dialog)){cancel_drag();if(context.editor)key(SDLK_O,SDL_KMOD_CTRL);else key(SDLK_F10);}
            if (view_buttons) {
                if(pressed(SDL_GAMEPAD_BUTTON_DPAD_UP))key(SDLK_PAGEUP);
                if(pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN))key(SDLK_PAGEDOWN);
                if(pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT))key(SDLK_Q);
                if(pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT))key(SDLK_E);
                if(pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))key(SDLK_U);
                if(pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))key(SDLK_U,SDL_KMOD_SHIFT);
            } else {
                if(pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))key(SDLK_PAGEDOWN);
                if(pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))key(SDLK_PAGEUP);
            }
        }
        if(analog_debug){
            const auto now=SDL_GetTicks();auto& a=analog_ring[analog_at++%analog_ring.size()];analog_count=std::min(analog_count+1,unsigned(analog_ring.size()));
            a={};a.ms=now;a.event=last_pad_event;a.step=last_grid_step;a.x=axes[0];a.y=axes[1];const auto direction=cursor_repeat.direction();a.dx=direction.first;a.dy=direction.second;a.cx=cursor_x;a.cy=cursor_y;a.tile_x=logical_x;a.tile_y=logical_y;a.tool=context.tool;a.mode=grid_enabled()&&context.building?1:0;a.touch=unsigned(std::max(fingers,0));a.buttons=unsigned(mouse_down[1])|unsigned(road_latched)<<1;
            for(unsigned i=0;i<4;++i){a.raw[i]=raw_axes[i];a.sdl[i]=sdl_axes[i];}
            if(!left_x&&!left_y)direction_since=now;
            if(now-direction_since>=3000&&!analog_ready.load()){
                std::unique_lock lock(analog_mutex,std::try_to_lock);if(lock.owns_lock()){
                    analog_snapshot_count=analog_count;for(unsigned i=0;i<analog_count;++i)analog_snapshot[i]=analog_ring[(analog_at-analog_count+i)%analog_ring.size()];
                    analog_ready.store(true);direction_since=now;
                }
            }
        }
        for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b){const bool now=button(static_cast<SDL_GamepadButton>(b));if(now!=previous[b])trace("PAD button=%d down=%d modifier=0",b,now);previous[b]=now;}
        return;
    }
    held[SDL_SCANCODE_LEFT]=axis(SDL_GAMEPAD_AXIS_LEFTX)<-.2f;
    held[SDL_SCANCODE_RIGHT]=axis(SDL_GAMEPAD_AXIS_LEFTX)>.2f;
    held[SDL_SCANCODE_UP]=axis(SDL_GAMEPAD_AXIS_LEFTY)<-.2f;
    held[SDL_SCANCODE_DOWN]=axis(SDL_GAMEPAD_AXIS_LEFTY)>.2f;
    const bool modifier=button(SDL_GAMEPAD_BUTTON_BACK);
    static ui::StickRepeat repeat{260,110};
    static unsigned repeat_speed=100;
    if(repeat_speed!=right_speed){repeat_speed=right_speed;repeat=ui::StickRepeat(26000/right_speed,11000/right_speed);}
    static bool override_previous=false;
    const bool toggle=modifier&&button(SDL_GAMEPAD_BUTTON_START);
    if(toggle&&!override_previous){free_override=!free_override;trace("GRID free_override=%d",free_override);}
    override_previous=toggle;
    const float ax=float(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTX))/32768.f+(!modifier?int(button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT))-int(button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)):0);
    const float ay=float(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTY))/32768.f+(!modifier?int(button(SDL_GAMEPAD_BUTTON_DPAD_DOWN))-int(button(SDL_GAMEPAD_BUTTON_DPAD_UP)):0);
    float gx=cursor_x,gy=cursor_y;
    const bool snapped=!front_down&&!free_override&&grid&&grid(0,0,gx,gy);
    float dx=0,dy=0;
    if(snapped){
        const auto [sx,sy]=repeat.poll(ax,ay,SDL_GetTicks());
        if(sx||sy)grid(sx,sy,gx,gy);
        dx=gx-cursor_x;dy=gy-cursor_y;
    }else{repeat.reset();dx=(std::abs(ax)<.244f?0.f:ax)*10.f*float(legacy?right_speed:left_speed)/100.f;dy=(std::abs(ay)<.244f?0.f:ay)*10.f*float(legacy?right_speed:left_speed)/100.f;}
    if(!front_down&&(dx!=0||dy!=0)) {
        cursor_x=std::clamp(cursor_x+dx,0.f,959.f);cursor_y=std::clamp(cursor_y+dy,0.f,543.f);
        SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=SDL_GetWindowID(window);
        e.motion.x=cursor_x;e.motion.y=cursor_y;e.motion.xrel=dx;e.motion.yrel=dy;SDL_PushEvent(&e);
    }
    // Escape must reach the original tool before a simultaneous X release;
    // otherwise that release could accidentally commit a cancelled road drag.
    const bool cancel=!modifier&&button(SDL_GAMEPAD_BUTTON_EAST)&&!previous[SDL_GAMEPAD_BUTTON_EAST];
    if(cancel)key(SDLK_ESCAPE);
    for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b) {
        const bool now=button(static_cast<SDL_GamepadButton>(b));
        if(now==previous[b])continue;
        trace("PAD button=%d down=%d modifier=%d",b,now,modifier);
        if(b==SDL_GAMEPAD_BUTTON_SOUTH&&(!front_down||!now)&&(!modifier||(!now&&mouse_down[SDL_BUTTON_LEFT])))mouse(window,SDL_BUTTON_LEFT,now);
        if(b==SDL_GAMEPAD_BUTTON_WEST&&(!front_down||!now)&&(!modifier||(!now&&mouse_down[SDL_BUTTON_RIGHT])))mouse(window,SDL_BUTTON_RIGHT,now);
        if(now) {
            if(b==SDL_GAMEPAD_BUTTON_EAST&&modifier)key(SDLK_R);
            if(b==SDL_GAMEPAD_BUTTON_NORTH)key(modifier?SDLK_F9:SDLK_B);
            if(b==SDL_GAMEPAD_BUTTON_START&&!modifier)key(SDLK_O);
            if(b==SDL_GAMEPAD_BUTTON_SOUTH&&modifier)key(SDLK_RETURN);
            if(b==SDL_GAMEPAD_BUTTON_WEST&&modifier)key(SDLK_F5);
            if(b==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)key(modifier?SDLK_Q:SDLK_PAGEDOWN);
            if(b==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)key(modifier?SDLK_E:SDLK_PAGEUP);
            if(modifier&&b==SDL_GAMEPAD_BUTTON_DPAD_UP)key(SDLK_KP_PLUS);
            if(modifier&&b==SDL_GAMEPAD_BUTTON_DPAD_DOWN)key(SDLK_KP_MINUS);
            if(modifier&&b==SDL_GAMEPAD_BUTTON_DPAD_LEFT)key(SDLK_F3);
            if(modifier&&b==SDL_GAMEPAD_BUTTON_DPAD_RIGHT)key(SDLK_PERIOD);
        }
        previous[b]=now;
    }
}
bool key_held(int scancode) {return scancode>=0&&scancode<SDL_SCANCODE_COUNT&&held[scancode];}
void cache_fail_at(unsigned frame){fail_cache_frame=frame;}
bool cache_failure(){if(fail_cache_frame&&touch_frame>=fail_cache_frame){fail_cache_frame=0;return true;}return false;}
void touch_action(const char* context,unsigned kind){
    if(touch_debug&&last_touch&&sceKernelGetProcessTimeWide()-last_touch<500000){
        trace("TOUCH_ACTION context=%s kind=%u",context,kind);
    }
}
namespace { void resume_cpu(); void suspend_cpu(); }
void event(const SDL_Event& e) {
    if(e.type==SDL_EVENT_WILL_ENTER_BACKGROUND)suspend_cpu();
    if(e.type==SDL_EVENT_DID_ENTER_FOREGROUND)resume_cpu();
    if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST||e.type==SDL_EVENT_DID_ENTER_BACKGROUND){cursor_state.interrupt();camera_state.interrupt();cursor_repeat.reset();held.fill(false);}
    if(e.type==SDL_EVENT_GAMEPAD_AXIS_MOTION||e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||e.type==SDL_EVENT_GAMEPAD_BUTTON_UP)last_pad_event=SDL_GetTicks();
    pointer_event=e.type==SDL_EVENT_MOUSE_BUTTON_DOWN||e.type==SDL_EVENT_MOUSE_BUTTON_UP;
    // Controller motion is already sent as normal SDL events. Warping as well
    // would enqueue an older duplicate which can re-pick the previous grid tile.
    // Synchronize the displayed cursor with touch and pointer events.
    if(e.type==SDL_EVENT_MOUSE_MOTION){cursor_x=e.motion.x;cursor_y=e.motion.y;}
    if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN||e.type==SDL_EVENT_MOUSE_BUTTON_UP){cursor_x=e.button.x;cursor_y=e.button.y;}
    if(!touch_debug)return;
    if(e.type==SDL_EVENT_FINGER_DOWN||e.type==SDL_EVENT_FINGER_UP||e.type==SDL_EVENT_FINGER_MOTION){
        std::snprintf(touch_line,sizeof(touch_line),"%s SDL=%u norm %.3f,%.3f screen %.1f,%.1f",e.tfinger.touchID==1?"FRONT":"BACK",unsigned(e.type),e.tfinger.x,e.tfinger.y,e.tfinger.x*960,e.tfinger.y*544);
        trace("TOUCH_SDL %s",touch_line);
    } else if((e.type==SDL_EVENT_MOUSE_BUTTON_DOWN||e.type==SDL_EVENT_MOUSE_BUTTON_UP)&&e.button.which==SDL_TOUCH_MOUSEID)
        {last_touch=sceKernelGetProcessTimeWide();trace("TOUCH_MOUSE type=%u button=%u x=%.1f y=%.1f",unsigned(e.type),unsigned(e.button.button),e.button.x,e.button.y);}
}
bool consume_event(const SDL_Event& e){
    // First contact cancels a pad-latched road. Consume that contact so its
    // ordinary mouse-up cannot accidentally confirm the abandoned draft.
    if(suppress_front&&((e.type==SDL_EVENT_MOUSE_MOTION&&e.motion.which==SDL_TOUCH_MOUSEID)||
        ((e.type==SDL_EVENT_MOUSE_BUTTON_DOWN||e.type==SDL_EVENT_MOUSE_BUTTON_UP)&&e.button.which==SDL_TOUCH_MOUSEID)||
        ((e.type==SDL_EVENT_FINGER_DOWN||e.type==SDL_EVENT_FINGER_UP||e.type==SDL_EVENT_FINGER_MOTION)&&e.tfinger.touchID==1)))return true;
    if(!actions_open)return false;
    // Adapter actions still pass through the original Open1602 key table.
    if((e.type==SDL_EVENT_KEY_DOWN||e.type==SDL_EVENT_KEY_UP)&&e.key.which==0x1602)return false;
    if(e.type==SDL_EVENT_MOUSE_BUTTON_UP&&e.button.which==SDL_TOUCH_MOUSEID&&!help_open){
        const int row=int((e.button.y-80)/28);
        if(e.button.x>=90&&e.button.x<=870&&row>=0&&row<int(std::size(actions))){
            action_selected=unsigned(row);if(actions[action_selected].key){auto a=actions[action_selected];actions_open=false;key(a.key,a.mod);}
            else if(action_selected==std::size(actions)-1)help_open=true;
            else {free_override=!free_override;actions_open=false;}
        }
    }
    return e.type==SDL_EVENT_MOUSE_MOTION||e.type==SDL_EVENT_MOUSE_BUTTON_DOWN||e.type==SDL_EVENT_MOUSE_BUTTON_UP||
        e.type==SDL_EVENT_MOUSE_WHEEL||e.type==SDL_EVENT_KEY_DOWN||e.type==SDL_EVENT_KEY_UP||e.type==SDL_EVENT_TEXT_INPUT||
        e.type==SDL_EVENT_FINGER_DOWN||e.type==SDL_EVENT_FINGER_UP||e.type==SDL_EVENT_FINGER_MOTION;
}
QuickMenuState quick_menu_state(){return {actions_open,help_open,action_selected,unsigned(std::size(actions))};}
const char* quick_action_label(unsigned index){return index<std::size(actions)?actions[index].label:"";}
bool touch_script(const char* path) {
    FILE* f=std::fopen(path,"r");if(!f)return false;TouchEvent e{};
    while(std::fscanf(f,"%u %u %c %f %f",&e.frame,&e.port,&e.kind,&e.x,&e.y)==5){
        if(touch_events.size()>=2048||e.port>1||(e.kind!='d'&&e.kind!='m'&&e.kind!='u')||!(e.x>=0&&e.x<=1&&e.y>=0&&e.y<=1)){std::fclose(f);touch_events.clear();return false;}
        touch_events.push_back(e);
    }
    std::fclose(f);touch_debug=true;return !touch_events.empty();
}
void draw_cursor(SDL_Renderer* renderer) {
    if(touch_debug){SDL_SetRenderDrawColor(renderer,0,0,0,220);SDL_FRect bg{0,520,960,24};SDL_RenderFillRect(renderer,&bg);
        SDL_SetRenderDrawColor(renderer,255,255,0,255);SDL_RenderDebugText(renderer,4,526,touch_line);}

    if(!SDL_CursorVisible())return;
    const SDL_FPoint points[]={{cursor_x,cursor_y},{cursor_x,cursor_y+17},{cursor_x+4,cursor_y+13},
        {cursor_x+8,cursor_y+21},{cursor_x+11,cursor_y+19},{cursor_x+7,cursor_y+12},
        {cursor_x+14,cursor_y+12},{cursor_x,cursor_y}};
    SDL_SetRenderDrawColor(renderer,255,255,255,255);
    SDL_RenderLines(renderer,points,int(std::size(points)));
    const SDL_FPoint inner[]={{cursor_x+1,cursor_y+3},{cursor_x+1,cursor_y+14},{cursor_x+4,cursor_y+10},
        {cursor_x+9,cursor_y+19},{cursor_x+5,cursor_y+10},{cursor_x+10,cursor_y+10},{cursor_x+1,cursor_y+3}};
    SDL_SetRenderDrawColor(renderer,0,0,0,255);
    SDL_RenderLines(renderer,inner,int(std::size(inner)));
}
bool pad_script(const char* path) {
    FILE* file=std::fopen(path,"r");if(!file)return false;
    PadEvent event{};
    while(std::fscanf(file,"%u %c %d %d",&event.frame,&event.type,&event.control,&event.value)==4) {
        const int limit=event.type=='v'?1:event.type=='n'?4:event.type=='a'?SDL_GAMEPAD_AXIS_COUNT:SDL_GAMEPAD_BUTTON_COUNT;
        if(script.size()>=2048||event.control<0||event.control>=limit||(event.type!='a'&&event.type!='b'&&event.type!='n'&&event.type!='v')||event.value< -32768||event.value>32767) {std::fclose(file);script.clear();return false;}
        script.push_back(event);
    }
    std::fclose(file);trace("PAD_SCRIPT records=%u",unsigned(script.size()));return !script.empty();
}
namespace {
void write_profile(const std::string& captured) {
    if(!vita_boot_logging())return;
    const auto json=session_profile(captured);
    if(json.empty())return;
    constexpr const char* temporary="ux0:data/open1602/profile.tmp";
    constexpr const char* target="ux0:data/open1602/profile.json";
    constexpr const char* previous_profile="ux0:data/open1602/profile.previous.json";
    sceIoRemove(temporary);
    const int fd=sceIoOpen(temporary,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    unsigned done=0;
    if(fd>=0){while(done<json.size()){
        const int n=sceIoWrite(fd,json.data()+done,static_cast<unsigned>(json.size())-done);
        if(n<=0)break;done+=static_cast<unsigned>(n);
    }sceIoSyncByFd(fd,0);sceIoClose(fd);}
    if(done==json.size()){
        sceIoRemove(previous_profile);sceIoRename(target,previous_profile);
        const int renamed=sceIoRename(temporary,target);
        if(renamed<0)trace("PROFILE_WRITE_FAILED rename=%d",renamed);
    }else trace("PROFILE_WRITE_FAILED fd=%d written=%u expected=%u",fd,done,unsigned(json.size()));
    vita_boot_write("PROFILE_WRITER snapshot saved");
}
}
namespace {
CpuClockLease cpu_lease;
unsigned requested_cpu=0;
bool apply_cpu(unsigned mhz,const char* reason){
    const auto result=cpu_lease.apply(int(mhz),[]{return scePowerGetArmClockFrequency();},[](int value){return scePowerSetArmClockFrequency(value);});
    static constexpr const char* states[]={"unchanged","applied","external_higher_preserved","read_error","set_error","readback_unverified"};
    trace("CPU_PROFILE phase=%s requested=%u before=%d rc=%d after=%d state=%s owned=%d gpu=%d bus=%d xbar=%d",reason,mhz,result.before,result.rc,result.after,
        states[unsigned(result.state)],cpu_lease.owned(),scePowerGetGpuClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuXbarClockFrequency());
    return result.state==CpuClockLease::State::Unchanged||result.state==CpuClockLease::State::Applied||result.state==CpuClockLease::State::ExternalHigher;
}
void resume_cpu(){(void)apply_cpu(requested_cpu,"resume");}
void suspend_cpu(){(void)apply_cpu(0,"suspend_restore");}
}
bool cpu_profile(unsigned mhz){
    (void)mhz;requested_cpu=444;return apply_cpu(444,"fixed_start");
}
namespace {
bool extra_cpu_verified=false;
SceUID worker_main_thread=-1;
unsigned worker_cpu(const char* role,unsigned index){
    if(std::strcmp(role,"graphics_decode")==0)return 1+index%2;
    if(std::strcmp(role,"sprite_prefetch")==0)return index%2?2:3;
    if(std::strcmp(role,"music_read")==0||std::strcmp(role,"effect_read")==0)return 1;
    if(std::strcmp(role,"video_decode")==0)return 2;
    return 3;
}
void bind_worker(const char* role,unsigned index){
    if(!extra_cpu_verified)return;
    const unsigned cpu=worker_cpu(role,index);const int mask=0x10000<<cpu;
    const auto tid=sceKernelGetThreadId();if(tid==worker_main_thread)return;
    if(std::strcmp(role,"graphics_decode")==0||std::strcmp(role,"sprite_prefetch")==0)sceKernelChangeThreadPriority(tid,0x10000110);
    const int before=sceKernelGetThreadCpuAffinityMask(tid);
    if(before<0)return;
    const int rc=sceKernelChangeThreadCpuAffinityMask(tid,mask);
    if(rc>=0)sceKernelDelayThread(1);
    SceKernelThreadInfo info{};info.size=sizeof(info);const int status=sceKernelGetThreadInfo(tid,&info);
    const bool used=rc>=0&&sceKernelGetThreadCpuAffinityMask(tid)==mask&&status>=0&&info.currentCpuId==int(cpu);
    if(!used)sceKernelChangeThreadCpuAffinityMask(tid,before);
    trace("WORKER_AFFINITY role=%s index=%u target_cpu=%u set_rc=%d verified=%d actual_cpu=%d",role,index,cpu,rc,used,status>=0?info.currentCpuId:-1);
}
void initialize_workers(){
    worker_main_thread=sceKernelGetThreadId();
    bool configured=false,file=false;
    for(const char* config:{"ur0:tai/config.txt","ux0:tai/config.txt"}){
        std::ifstream in(config,std::ios::binary);if(!in)continue;
        std::string text(65537,'\0');in.read(text.data(),static_cast<std::streamsize>(text.size()));
        if(in.gcount()>65536)continue;text.resize(static_cast<std::size_t>(in.gcount()));
        const auto path=capunlocker_entry(text);if(path.empty())continue;configured=true;
        SceIoStat stat{};if(sceIoGetstat(path.c_str(),&stat)>=0&&SCE_S_ISREG(stat.st_mode)&&stat.st_size>0){file=true;break;}
    }
    // Probe on a short-lived thread; never change the main thread's affinity.
    if(configured&&file)try{std::thread probe([]{
        const auto tid=sceKernelGetThreadId();const int rc=sceKernelChangeThreadCpuAffinityMask(tid,0x80000);
        if(rc>=0)sceKernelDelayThread(1);
        SceKernelThreadInfo info{};info.size=sizeof(info);
        extra_cpu_verified=rc>=0&&sceKernelGetThreadCpuAffinityMask(tid)==0x80000&&sceKernelGetThreadInfo(tid,&info)>=0&&info.currentCpuId==3;
    });probe.join();}catch(...){extra_cpu_verified=false;}
    // The serial benchmark flag cannot bypass the CPU3 probe.
    const bool serial=std::filesystem::exists("ux0:data/open1602/worker-serial.flag");
    const bool accessible=extra_cpu_verified;
#ifdef O1602_VITA_WORKER_QA
    // QA builds exercise parallel jobs on SDK-managed cores without binding.
    core::set_worker_policy(serial?1:3,nullptr);
    trace("WORKER_QA unbound_parallelism=%u actual_plugin_test=0",core::worker_parallelism());
#else
    core::set_worker_policy(extra_cpu_verified&&!serial?3:1,extra_cpu_verified?bind_worker:nullptr);
#endif
    trace("WORKER_POLICY configured=%d plugin_file=%d cpu3_verified=%d parallelism=%u serial_test=%d",configured,file,accessible,core::worker_parallelism(),serial);
}
}
void configure_background_worker(const char* role){core::worker_enter(role);}

void diagnostics_pause(bool paused){writer_pause.store(paused,std::memory_order_release);}
void finish() {
    watch_finish();
    (void)apply_cpu(0,"exit_restore");
    std::fflush(stderr);
    writer_stop.store(true,std::memory_order_release);
    if(diagnostic_writer.joinable())diagnostic_writer.join();
#ifdef O1602_VITA_NETWORK
    // run_program has destroyed the viewer, sessions, transports and their workers.
    // Partial startup follows the same resource cleanup path.
    finish_connectivity();
#endif
    // Called only after the client loop/destructors. Final snapshot is synchronous
    // so the last complete profile survives a clean exit.
    if(analog_debug){
        {std::lock_guard lock(analog_mutex);analog_snapshot_count=analog_count;for(unsigned i=0;i<analog_count;++i)analog_snapshot[i]=analog_ring[(analog_at-analog_count+i)%analog_ring.size()];}
        write_analog_snapshot("ux0:data/open1602/analog-final.txt");
    }
    session_complete();
    try {if(core::profile::capture())write_profile(core::profile::captured_summary());}
    catch(...){vita_boot_write("WARN final profile skipped; previous profile retained");}
    vita_boot_flush();
    archive_session();
}
bool frame_hash_requested(unsigned frame){return vita_boot_logging()&&diagnostic_frame_hashes&&(frame==1||frame%120==0);}
void frame(unsigned frames, bool menu,std::uint64_t tick,std::uint64_t hash) {
    if(test_frame_ms){const auto elapsed=SDL_GetTicksNS()-poll_started;const auto target=std::uint64_t{test_frame_ms}*1000000;
        if(elapsed<target){core::profile::Scope wait(core::profile::Wait);SDL_DelayNS(target-elapsed);}}
    if(!vita_boot_logging())return;
    static std::uint64_t clock_ms=0; const auto now_ms=SDL_GetTicks();
    if(frames==1||now_ms-clock_ms>=1000){
        clock_ms=now_ms; const int cpu=scePowerGetArmClockFrequency(),gpu=scePowerGetGpuClockFrequency(),bus=scePowerGetBusClockFrequency(),xbar=scePowerGetGpuXbarClockFrequency();
        if(cpu>0)core::profile::gauge(core::profile::CpuMHz,unsigned(cpu));
        if(gpu>0)core::profile::gauge(core::profile::GpuMHz,unsigned(gpu));
        if(bus>0)core::profile::gauge(core::profile::BusMHz,unsigned(bus));
        if(xbar>0)core::profile::gauge(core::profile::XbarMHz,unsigned(xbar));
        trace("CLOCK_READ frame=%u ms=%llu tick=%llu cpu=%d gpu=%d bus=%d xbar=%d source=sdk_getters hardware_accuracy_unverified=1",frames,(unsigned long long)now_ms,(unsigned long long)tick,cpu,gpu,bus,xbar);
    }
    const auto info=mallinfo();core::profile::gauge(core::profile::HeapUsed,unsigned(info.uordblks));
    core::profile::gauge(core::profile::HeapArena,unsigned(info.arena));
    if(frames==1||frames%120==0){
        if(frame_hash_requested(frames))trace("FRAME n=%u menu=%d tick=%llu hash=%llu heap=%u",frames,menu,(unsigned long long)tick,(unsigned long long)hash,unsigned(mallinfo().uordblks));
        else trace("FRAME n=%u menu=%d tick=%llu hash_omitted=1 heap=%u",frames,menu,(unsigned long long)tick,unsigned(mallinfo().uordblks));
    }
    if(frames%600==0){
        SceKernelFreeMemorySizeInfo memory{};memory.size=sizeof(memory);
        if(sceKernelGetFreeMemorySize(&memory)>=0){core::profile::gauge(core::profile::FreeUser,memory.size_user);core::profile::gauge(core::profile::FreeCdram,memory.size_cdram);}
        // A bounded memory copy only; sorting, JSON formatting, sync and rotation
        // run on diagnostic_writer. Never wait if its previous snapshot is busy.
        core::profile::Scope timer(core::profile::LogIO);
        if(core::profile::capture())snapshot_ready.store(true,std::memory_order_release);
    }
}
std::filesystem::path save_path(const std::filesystem::path& path,std::error_code& error,bool scenario) {
    return checked_save_path(path,error,scenario);
}
}

// Link-time boundary: the original ENet library and wire/session code stay intact.
extern "C" int __real_enet_initialize(void);
extern "C" int __wrap_enet_initialize(void){
    if(!o1602::platform::vita::initialize_network())return -1;
    return __real_enet_initialize();
}
