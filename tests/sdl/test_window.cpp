// SPDX-License-Identifier: GPL-3.0-or-later
// The window and its renderer (engine/platform/window.cpp) on SDL's dummy video driver.

#include <SDL3/SDL.h>
#include <cstring>
#include <string>

#include "../test_framework.hpp"
#include "sdl_support.hpp"

#include "o1602/platform/window.hpp"
#include "o1602/render/camera.hpp"

using namespace o1602;

TEST_CASE("window: a window has a renderer whose output is the window's size in pixels") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    auto window = platform::Window::create(platform::WindowConfig{.title = "test", .width = 320, .height = 200});
    REQUIRE(window.ok());
    const platform::Window& w = **window;
    REQUIRE(w.sdl_window() != nullptr);
    REQUIRE(w.sdl_renderer() != nullptr);
    CHECK(SDL_GetRenderWindow(w.sdl_renderer()) == w.sdl_window());

    // The dummy driver has no HiDPI: one pixel per window coordinate.
    CHECK(w.pixel_density() == 1.0F);
    std::int32_t width = 0;
    std::int32_t height = 0;
    w.output_size(width, height);
    CHECK(width == 320);
    CHECK(height == 200);
    CHECK(!w.renderer_name().empty());
    CHECK(w.renderer_name() != "unknown");
    CHECK(w.max_texture_size() >= 0);
}

TEST_CASE("window: the output size follows a resize") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    auto window = platform::Window::create(platform::WindowConfig{.width = 320, .height = 200, .vsync = false});
    REQUIRE(window.ok());
    REQUIRE(SDL_SetWindowSize((*window)->sdl_window(), 400, 300));
    SDL_SyncWindow((*window)->sdl_window());
    std::int32_t width = 0;
    std::int32_t height = 0;
    (*window)->output_size(width, height);
    CHECK(width == 400);
    CHECK(height == 300);
}

TEST_CASE("window: the display scale is positive and gives the game a pixel scale of 1 on a plain screen") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    auto window = platform::Window::create(platform::WindowConfig{.width = 320, .height = 200});
    REQUIRE(window.ok());
    const float scale = (*window)->display_scale();
    CHECK(scale > 0.0F);
    // SDL reports the display's content scale; without one, the pixel density stands in.
    const float content = SDL_GetWindowDisplayScale((*window)->sdl_window());
    CHECK(scale == (content > 0.0F ? content : (*window)->pixel_density()));
    CHECK(render::pixel_scale_for(scale, 4) == 1);
}

TEST_CASE("window: set_title changes the window's title") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    auto window = platform::Window::create(platform::WindowConfig{.title = "first"});
    REQUIRE(window.ok());
    CHECK(std::strcmp(SDL_GetWindowTitle((*window)->sdl_window()), "first") == 0);
    (*window)->set_title("Open1602 - second");
    CHECK(std::strcmp(SDL_GetWindowTitle((*window)->sdl_window()), "Open1602 - second") == 0);
}

TEST_CASE("window: what the renderer draws reaches the window's pixels") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    auto window = platform::Window::create(platform::WindowConfig{.width = 64, .height = 48, .vsync = false});
    REQUIRE(window.ok());
    SDL_Renderer* renderer = (*window)->sdl_renderer();
    SDL_SetRenderDrawColor(renderer, 10, 20, 30, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 200, 100, 50, 255);
    const SDL_FRect rect{8.0F, 4.0F, 16.0F, 8.0F};
    SDL_RenderFillRect(renderer, &rect);

    SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
    REQUIRE(shot != nullptr);
    CHECK(shot->w == 64);
    CHECK(shot->h == 48);
    const auto pixel = [&](int x, int y) {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        SDL_ReadSurfacePixel(shot, x, y, &r, &g, &b, nullptr);
        return assets::Rgba{r, g, b, 255};
    };
    CHECK(pixel(8, 4) == (assets::Rgba{200, 100, 50, 255}));
    CHECK(pixel(23, 11) == (assets::Rgba{200, 100, 50, 255}));
    CHECK(pixel(24, 11) == (assets::Rgba{10, 20, 30, 255}));
    CHECK(pixel(8, 12) == (assets::Rgba{10, 20, 30, 255}));
    SDL_DestroySurface(shot);
    CHECK(SDL_RenderPresent(renderer));
}

TEST_CASE("window: ticks_ms counts up from SDL_Init") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    const std::uint64_t before = platform::ticks_ms();
    SDL_Delay(5);
    const std::uint64_t after = platform::ticks_ms();
    CHECK(after >= before + 5);
}

TEST_CASE("window: render events say whether textures or the renderer must be made again") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    auto window = platform::Window::create(platform::WindowConfig{.width = 64, .height = 48, .vsync = false});
    REQUIRE(window.ok());
    const SDL_WindowID id = SDL_GetWindowID((*window)->sdl_window());
    // As the GPU backends of SDL send them: pushed to the queue, read by the main loop.
    const auto delivered = [&](SDL_EventType type, SDL_WindowID window_id) {
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
        SDL_Event event{};
        event.type = type;
        event.render.windowID = window_id;
        REQUIRE(SDL_PushEvent(&event));
        // SDL_Event::type is a Uint32, SDL_EventType an enum of signed type on MSVC.
        const auto wanted = static_cast<Uint32>(type);
        SDL_Event polled{};
        while (SDL_PollEvent(&polled)) {
            if (polled.type == wanted) {
                return (*window)->render_reset(polled);
            }
        }
        REQUIRE(polled.type == wanted);  // the event did not arrive
        return platform::RenderReset::None;
    };
    CHECK(delivered(SDL_EVENT_RENDER_DEVICE_LOST, id) == platform::RenderReset::Renderer);
    CHECK(delivered(SDL_EVENT_RENDER_DEVICE_RESET, id) == platform::RenderReset::Textures);
    CHECK(delivered(SDL_EVENT_RENDER_TARGETS_RESET, id) == platform::RenderReset::None);
    CHECK(delivered(SDL_EVENT_RENDER_DEVICE_LOST, id + 1) == platform::RenderReset::None);
    CHECK(delivered(SDL_EVENT_USER, id) == platform::RenderReset::None);
}

TEST_CASE("window: after a lost device the window draws with a new renderer") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    auto window = platform::Window::create(platform::WindowConfig{.width = 64, .height = 48, .vsync = false});
    REQUIRE(window.ok());
    platform::Window& w = **window;
    const std::string name = w.renderer_name();
    REQUIRE(w.recreate_renderer().ok());
    REQUIRE(w.sdl_renderer() != nullptr);
    CHECK(SDL_GetRenderWindow(w.sdl_renderer()) == w.sdl_window());
    CHECK(w.renderer_name() == name);
    int vsync = -1;
    CHECK(SDL_GetRenderVSync(w.sdl_renderer(), &vsync));
    CHECK(vsync == 0);
    std::int32_t width = 0;
    std::int32_t height = 0;
    w.output_size(width, height);
    CHECK(width == 64);
    CHECK(height == 48);

    test::clear(w.sdl_renderer(), assets::Rgba{10, 20, 30, 255});
    SDL_SetRenderDrawColor(w.sdl_renderer(), 200, 100, 50, 255);
    const SDL_FRect rect{8.0F, 4.0F, 16.0F, 8.0F};
    SDL_RenderFillRect(w.sdl_renderer(), &rect);
    test::Pixels pixels;
    pixels.read(w.sdl_renderer());
    CHECK(pixels.at(8, 4) == (assets::Rgba{200, 100, 50, 255}));
    CHECK(pixels.at(24, 11) == (assets::Rgba{10, 20, 30, 255}));
    CHECK(SDL_RenderPresent(w.sdl_renderer()));
}

TEST_CASE("window: the application icon is compiled in, with a larger picture for HiDPI screens") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    SDL_Surface* icon = platform::load_app_icon();
    REQUIRE(icon != nullptr);
    CHECK(icon->w == 64);
    CHECK(icon->h == 64);
    CHECK(SDL_SurfaceHasAlternateImages(icon));
    int count = 0;
    SDL_Surface** images = SDL_GetSurfaceImages(icon, &count);
    REQUIRE(images != nullptr);
    CHECK(count == 2);
    if (count == 2) {
        CHECK(images[1]->w == 128);
    }
    SDL_free(static_cast<void*>(images));

    // Round medallion: the corners are transparent, the centre opaque.
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
    REQUIRE(SDL_ReadSurfacePixelFloat(icon, 0, 0, &r, &g, &b, &a));
    CHECK(a == 0.0F);
    REQUIRE(SDL_ReadSurfacePixelFloat(icon, 32, 32, &r, &g, &b, &a));
    CHECK(a == 1.0F);
    SDL_DestroySurface(icon);
}

TEST_CASE("window: with vertical sync on, frames that do not wait for the screen are held to its rate") {
    // A frame a whole refresh after the last waited for the screen: nothing more.
    CHECK(platform::frame_hold_ns(16'700'000, 60.0F) == 0);
    CHECK(platform::frame_hold_ns(15'000'000, 60.0F) == 0);  // 90 % of a refresh
    // One that came at once (a hidden window, a screen switched off) waits for 90 % of it.
    CHECK(platform::frame_hold_ns(0, 60.0F) == 15'000'000);
    CHECK(platform::frame_hold_ns(1'000'000, 240.0F) == 2'750'000);
    // An unknown refresh rate counts as 60 Hz.
    CHECK(platform::frame_hold_ns(0, 0.0F) == 15'000'000);
    CHECK(platform::frame_hold_ns(0, 5000.0F) == 15'000'000);
}

TEST_CASE("window: a fixed clock moves only by its step, the real one goes on beside it") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    CHECK(!platform::fixed_clock());

    platform::use_fixed_clock(16);
    CHECK(platform::fixed_clock());
    CHECK(platform::ticks_ms() == 0);
    const std::uint64_t real = platform::real_ticks_ms();
    SDL_Delay(5);
    CHECK(platform::ticks_ms() == 0);  // real time passing does not move it
    CHECK(platform::real_ticks_ms() >= real + 5);
    for (int frame = 0; frame < 300; ++frame) {
        platform::advance_fixed_clock();
    }
    CHECK(platform::ticks_ms() == 300 * 16);

    // Set again, it starts at 0 again: two runs in one program see the same times.
    platform::use_fixed_clock(16);
    platform::advance_fixed_clock();
    CHECK(platform::ticks_ms() == 16);

    // The real clock back: advancing does nothing, the time is SDL's.
    platform::use_fixed_clock(0);
    CHECK(!platform::fixed_clock());
    platform::advance_fixed_clock();
    const std::uint64_t before = platform::real_ticks_ms();
    const std::uint64_t now = platform::ticks_ms();
    CHECK(now >= before);
    CHECK(now <= platform::real_ticks_ms());
}
