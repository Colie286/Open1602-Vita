// SPDX-License-Identifier: GPL-3.0-or-later
//
// Platform layer on SDL3: the only module that talks to the operating system directly
// (ADR 0005). Everything here is RAII; SDL handles never leak into sim or assets.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "o1602/core/result.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Surface;
union SDL_Event;

namespace o1602::platform {

/// Error from the last failed SDL call (SDL_GetError) with some context.
[[nodiscard]] core::Error sdl_error(const std::string& what);

/// SDL_Init / SDL_Quit for the video subsystem. Create exactly one, before any window.
class SdlSystem {
public:
    static core::Result<std::unique_ptr<SdlSystem>> init();
    ~SdlSystem();
    SdlSystem(const SdlSystem&) = delete;
    SdlSystem& operator=(const SdlSystem&) = delete;

private:
    SdlSystem() = default;
};

/// The application icon compiled into the game (data/icons/window): 64 px with a 128 px alternate
/// for HiDPI screens. nullptr if it cannot be read (SDL_GetError says why); free it with
/// SDL_DestroySurface. Window::create sets it on every window.
[[nodiscard]] SDL_Surface* load_app_icon();

struct WindowConfig {
    std::string title = "Open1602";
    std::int32_t width = 1280;  ///< in window coordinates (may be scaled on HiDPI screens)
    std::int32_t height = 800;
    bool vsync = true;
    bool fullscreen = false;  ///< the whole screen at the desktop's resolution
};

/// What a render event asks of everything that draws with the window's renderer.
enum class RenderReset : std::uint8_t {
    None,      ///< another event, another window's, or the render targets were reset
    Textures,  ///< the device was reset: every texture lost its contents
    Renderer,  ///< the device was lost: the renderer is made again (Window::recreate_renderer)
};

/// A resizable, HiDPI-aware window with an SDL_Renderer (GPU accelerated where available).
class Window {
public:
    static core::Result<std::unique_ptr<Window>> create(const WindowConfig& config);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] SDL_Window* sdl_window() const noexcept { return m_window; }
    /// Another one after recreate_renderer(): keep neither it nor its textures past that.
    [[nodiscard]] SDL_Renderer* sdl_renderer() const noexcept { return m_renderer; }

    /// What an event of the queue asks for this window's renderer.
    [[nodiscard]] RenderReset render_reset(const SDL_Event& event) const;
    /// Destroys the renderer and makes a new one (the device was lost). Every texture of the old
    /// one must be destroyed before. On an error the window has no renderer.
    core::Status recreate_renderer();

    /// Drawable size in physical pixels.
    void output_size(std::int32_t& width, std::int32_t& height) const;
    /// Physical pixels per window coordinate (2 on typical HiDPI screens).
    [[nodiscard]] float pixel_density() const;
    /// The size the desktop wants things drawn at, per window coordinate times the pixel density:
    /// 2 on a HiDPI screen, 1.5 on Windows at 150 % (SDL_GetWindowDisplayScale).
    [[nodiscard]] float display_scale() const;
    void set_title(const std::string& title);
    /// The whole screen at the desktop's resolution (no change of the screen's mode, so switching
    /// is quick and other windows stay as they are), or the window.
    void set_fullscreen(bool on);
    [[nodiscard]] bool fullscreen() const;
    /// The window's size in window coordinates (not in full screen: then the screen's).
    void window_size(std::int32_t& width, std::int32_t& height) const;
    /// Resizes the window (window coordinates), kept on its screen.
    void set_window_size(std::int32_t width, std::int32_t height);
    /// The part of the window's screen that windows may use (without task bars), window
    /// coordinates; 0 if unknown.
    void usable_area(std::int32_t& width, std::int32_t& height) const;
    /// Whether the window is maximised (its size then is not the one to remember).
    [[nodiscard]] bool maximized() const;
    /// Keeps the mouse inside the window while it has the focus (SDL lets it go when another
    /// program gets the focus and takes it again when the window gets it back). Cheap to call
    /// every frame: SDL is only asked when the wish changes.
    void set_mouse_grab(bool on);
    /// The mouse is kept inside the window now and is over it: it cannot leave it.
    [[nodiscard]] bool mouse_confined() const;
    /// Frames wait for the screen (vertical sync); kept when the renderer is made again.
    void set_vsync(bool on);
    void input_received(std::uint64_t timestamp_ns);
    /// Shows the frame drawn. With vertical sync on, a frame that comes back much sooner than the
    /// screen's refresh (a hidden window, a screen switched off: the compositor then does not
    /// wait) is held back to about the refresh rate, so the program does not spin at thousands of
    /// frames a second. With vertical sync off frames are not limited.
    /// Vita preparation can omit its drawn pointer for this frame only.
    void present(bool show_cursor = true);
    /// Name of the active render backend (e.g. "vulkan", "opengl", "direct3d11", "software").
    [[nodiscard]] std::string renderer_name() const;
    /// Largest texture the renderer accepts (0 = unknown).
    [[nodiscard]] std::int32_t max_texture_size() const;

private:
    Window() = default;
    core::Status make_renderer();
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    std::uint64_t m_actual_present_ns=0,m_present_count=0;
    std::uint64_t m_input_ns=0;
    bool m_vsync = true;
    bool m_mouse_grab = false;
    std::uint64_t m_last_present_ns = 0;
};

/// How long Window::present() holds back a frame shown `since_last_ns` after the last one, on a
/// screen of `refresh_hz` (0 or a value out of 24..1000: 60 assumed): up to 90 % of a refresh.
[[nodiscard]] std::uint64_t frame_hold_ns(std::uint64_t since_last_ns, float refresh_hz) noexcept;

/// Milliseconds since SDL_Init (monotonic).
[[nodiscard]] std::uint64_t ticks_ms();

}  // namespace o1602::platform
