// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
namespace o1602::platform::vita {
bool logging_enabled();
void ui_language(const char* code);
void boot_language(const char* code);
const char* boot_ready_text();

void session_data(const std::string&,unsigned,bool);
void session_world(unsigned,unsigned,bool);
void session_complete();
std::string session_profile(std::string);
void archive_session();
void archive_previous_session();
void setup();
void setup_connectivity();
void finish_connectivity();
bool adhoc_loopback_test(); // explicit private emulator test; never normal startup
void finish();
void configure_background_worker(const char* role);
bool cpu_profile(unsigned mhz);
void diagnostics_pause(bool);
// Optional observer only: never cancels work or changes simulation scheduling.
void watch_frame(unsigned frame, std::uint64_t tick, unsigned zoom);
void watch_finish();
const char* watch_phase(const char* literal);
class WatchPhase {
public:
    explicit WatchPhase(const char* literal):m_previous(watch_phase(literal)){}
    ~WatchPhase(){if(m_previous)watch_phase(m_previous);}
private:
    const char* m_previous;
};
void phase(const char* message);
[[noreturn]] void fatal(const char* message);
void first_presented();
void boot_options(bool automatic,const char* fail_phase);
void boot_wait();
void trace(const char* format, ...);
std::uint32_t random_u32();
void test_timing(unsigned frame_ms,unsigned writer_ms);
bool grid_enabled();
using GridCursor = std::function<bool(int,int,float&,float&)>;
struct PadContext { bool world=false, building=false, road=false, dialog=false, loading=false; std::uint32_t tool=0,zoom=0; std::uintptr_t world_id=0; bool popup=false; bool quick_allowed=false; bool world_loaded=false,menu_active=false,modal_active=false,text_active=false,video_active=false,options_active=false; };
void poll(SDL_Window* window,const GridCursor& grid={},PadContext context={},bool scripts_ready=true);
void wait_for_world_scripts();
bool scripts_wait_for_world();
void legacy_controls(bool);
void grid_speed(bool fast);
void stick_sensitivity(unsigned left_percent,unsigned right_percent);
float camera_stick_speed();
void grid_position(unsigned,unsigned,unsigned);
void tool_selected();
bool consume_event(const SDL_Event&);
struct QuickMenuState { bool open,help; unsigned selected,count; };
QuickMenuState quick_menu_state();
const char* quick_action_label(unsigned index);
void position_cursor(SDL_Window*,float,float);
void draw_cursor(SDL_Renderer* renderer);
bool key_held(int scancode);
bool pad_script(const char* path);
bool touch_script(const char* path);
void event(const SDL_Event&);
void touch_action(const char*,unsigned);
void cache_fail_at(unsigned);
bool cache_failure();
bool frame_hash_requested(unsigned frame);
void frame(unsigned frames, bool menu, std::uint64_t tick, std::uint64_t hash);
std::filesystem::path save_path(const std::filesystem::path& path,std::error_code& error);
}
