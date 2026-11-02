// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/platform/window.hpp"

#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <string_view>

#include "o1602/core/embedded_file.hpp"

namespace o1602::platform {

// data/icons/window/*.bmp, compiled in by o1602_embed_files (engine/platform/CMakeLists.txt).
namespace icons {
extern const core::EmbeddedFile kEmbeddedFiles[];
extern const std::size_t kEmbeddedFileCount;
}  // namespace icons

namespace {

SDL_Surface* load_embedded_bmp(std::string_view name) {
    for (std::size_t i = 0; i < icons::kEmbeddedFileCount; ++i) {
        const core::EmbeddedFile& file = icons::kEmbeddedFiles[i];
        if (name == file.name) {
            return SDL_LoadBMP_IO(SDL_IOFromConstMem(file.data, file.size), true);
        }
    }
    SDL_SetError("no embedded icon %.*s", static_cast<int>(name.size()), name.data());
    return nullptr;
}

}  // namespace

SDL_Surface* load_app_icon() {
    SDL_Surface* icon = load_embedded_bmp("open1602-64");
    if (icon == nullptr) {
        return nullptr;
    }
    // The larger picture is for HiDPI screens; without it SDL scales the 64 px one up.
    if (SDL_Surface* large = load_embedded_bmp("open1602-128"); large != nullptr) {
        SDL_AddSurfaceAlternateImage(icon, large);
        SDL_DestroySurface(large);  // the icon holds its own reference
    }
    return icon;
}

core::Error sdl_error(const std::string& what) {
    return core::make_error(core::ErrorCode::PlatformError, what + ": " + SDL_GetError());
}

core::Result<std::unique_ptr<SdlSystem>> SdlSystem::init() {
    SDL_SetAppMetadata("Open1602", "0.1.0", "org.open1602.Open1602");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return sdl_error("SDL_Init");
    }
    return std::unique_ptr<SdlSystem>(new SdlSystem());
}

SdlSystem::~SdlSystem() {
    SDL_Quit();
}

core::Result<std::unique_ptr<Window>> Window::create(const WindowConfig& config) {
    std::unique_ptr<Window> window(new Window());
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;  // no mode set: the desktop's resolution
    }
    window->m_window = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
    if (window->m_window == nullptr) {
        return sdl_error("SDL_CreateWindow");
    }
    if (config.fullscreen) {
        // Wayland applies the full screen only after SDL_CreateWindow returns; the software
        // renderer would make its surface at the window's size before, and keep it (P29).
        (void)SDL_SyncWindow(window->m_window);
    }
    // Title bar and task bar; on Windows the program file's icon (open1602.rc) shows before this.
    // Not fatal: some video drivers have no window icons.
    if (SDL_Surface* icon = load_app_icon(); icon != nullptr) {
        SDL_SetWindowIcon(window->m_window, icon);
        SDL_DestroySurface(icon);
    } else {
        std::fprintf(stderr, "window icon: %s\n", SDL_GetError());
    }
    window->m_vsync = config.vsync;
    if (auto status = window->make_renderer(); !status) {
        return status.error();
    }
    return window;
}

core::Status Window::make_renderer() {
    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (m_renderer == nullptr) {
        return sdl_error("SDL_CreateRenderer");
    }
    if (m_vsync) {
        SDL_SetRenderVSync(m_renderer, 1);  // not fatal if unsupported
    }
    return core::Ok{};
}

core::Status Window::recreate_renderer() {
    if (m_renderer != nullptr) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    return make_renderer();
}

RenderReset Window::render_reset(const SDL_Event& event) const {
    if (event.type != SDL_EVENT_RENDER_DEVICE_RESET && event.type != SDL_EVENT_RENDER_DEVICE_LOST) {
        return RenderReset::None;
    }
    if (event.render.windowID != SDL_GetWindowID(m_window)) {
        return RenderReset::None;
    }
    return event.type == SDL_EVENT_RENDER_DEVICE_LOST ? RenderReset::Renderer : RenderReset::Textures;
}

Window::~Window() {
    if (m_renderer != nullptr) {
        SDL_DestroyRenderer(m_renderer);
    }
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
    }
}

void Window::output_size(std::int32_t& width, std::int32_t& height) const {
    int w = 0;
    int h = 0;
    SDL_GetRenderOutputSize(m_renderer, &w, &h);
    width = w;
    height = h;
}

float Window::pixel_density() const {
    const float density = SDL_GetWindowPixelDensity(m_window);
    return density > 0.0F ? density : 1.0F;
}

float Window::display_scale() const {
    const float scale = SDL_GetWindowDisplayScale(m_window);
    return scale > 0.0F ? scale : pixel_density();
}

void Window::set_title(const std::string& title) {
    SDL_SetWindowTitle(m_window, title.c_str());
}

std::string Window::renderer_name() const {
    const char* name = SDL_GetRendererName(m_renderer);
    return name != nullptr ? name : "unknown";
}

std::int32_t Window::max_texture_size() const {
    const SDL_PropertiesID props = SDL_GetRendererProperties(m_renderer);
    return static_cast<std::int32_t>(SDL_GetNumberProperty(props, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0));
}

namespace {

// The fixed clock: its step (0 = the real clock) and where it stands. Set before the game starts
// and advanced by its main loop; atomic so that a thread reading the clock meanwhile is safe.
std::atomic<std::uint32_t> g_fixed_step_ms{0};
std::atomic<std::uint64_t> g_fixed_now_ms{0};

}  // namespace

std::uint64_t ticks_ms() {
    return g_fixed_step_ms.load(std::memory_order_relaxed) != 0 ? g_fixed_now_ms.load(std::memory_order_relaxed)
                                                                : SDL_GetTicks();
}

std::uint64_t real_ticks_ms() {
    return SDL_GetTicks();
}

void use_fixed_clock(std::uint32_t step_ms) noexcept {
    g_fixed_now_ms.store(0, std::memory_order_relaxed);
    g_fixed_step_ms.store(step_ms, std::memory_order_relaxed);
}

bool fixed_clock() noexcept {
    return g_fixed_step_ms.load(std::memory_order_relaxed) != 0;
}

void advance_fixed_clock() noexcept {
    g_fixed_now_ms.fetch_add(g_fixed_step_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void Window::set_fullscreen(bool on) {
    if (on == fullscreen()) {
        return;
    }
    SDL_SetWindowFullscreenMode(m_window, nullptr);  // borderless at the desktop's resolution
    if (!SDL_SetWindowFullscreen(m_window, on)) {
        std::fprintf(stderr, "full screen: %s\n", SDL_GetError());
    }
}

bool Window::fullscreen() const {
    return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::window_size(std::int32_t& width, std::int32_t& height) const {
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    width = w;
    height = h;
}

void Window::set_window_size(std::int32_t width, std::int32_t height) {
    if (fullscreen() || maximized()) {
        SDL_RestoreWindow(m_window);
    }
    SDL_SetWindowSize(m_window, width, height);
    // Kept on the screen: centred if it would reach beyond the usable area.
    std::int32_t uw = 0;
    std::int32_t uh = 0;
    usable_area(uw, uh);
    if (uw > 0 && uh > 0 && (width >= uw || height >= uh)) {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(m_window);
        const auto centred = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        SDL_SetWindowPosition(m_window, centred, centred);
    }
}

void Window::usable_area(std::int32_t& width, std::int32_t& height) const {
    width = 0;
    height = 0;
    SDL_Rect r{};
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(m_window), &r)) {
        width = r.w;
        height = r.h;
    }
}

void Window::set_mouse_grab(bool on) {
    if (on == m_mouse_grab) {
        return;
    }
    m_mouse_grab = on;
    if (!SDL_SetWindowMouseGrab(m_window, on)) {
        std::fprintf(stderr, "mouse grab: %s\n", SDL_GetError());
    }
}

bool Window::mouse_confined() const {
    constexpr SDL_WindowFlags kFocus = SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS;
    return SDL_GetWindowMouseGrab(m_window) && (SDL_GetWindowFlags(m_window) & kFocus) == kFocus;
}

bool Window::maximized() const {
    return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0;
}

void Window::present() {
    SDL_RenderPresent(m_renderer);
    if (!m_vsync) {
        return;
    }
    float hz = 0.0F;
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(m_window)); mode != nullptr) {
        hz = mode->refresh_rate;
    }
    std::uint64_t now = SDL_GetTicksNS();
    if (m_last_present_ns != 0) {
        if (const std::uint64_t hold = frame_hold_ns(now - m_last_present_ns, hz); hold != 0) {
            SDL_DelayPrecise(hold);
            now = SDL_GetTicksNS();
        }
    }
    m_last_present_ns = now;
}

std::uint64_t frame_hold_ns(std::uint64_t since_last_ns, float refresh_hz) noexcept {
    // A frame waited for by the screen comes a whole refresh after the last; only one that comes
    // much sooner (below 90 % of the refresh) is held back.
    if (!(refresh_hz >= 24.0F && refresh_hz <= 1000.0F)) {
        refresh_hz = 60.0F;
    }
    const auto least = static_cast<std::uint64_t>(0.9e9 / static_cast<double>(refresh_hz));
    return since_last_ns < least ? least - since_last_ns : 0;
}

void Window::set_vsync(bool on) {
    m_vsync = on;
    if (m_renderer != nullptr) {
        SDL_SetRenderVSync(m_renderer, on ? 1 : 0);
    }
}

}  // namespace o1602::platform
