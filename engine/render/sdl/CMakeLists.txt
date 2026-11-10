# SPDX-License-Identifier: GPL-3.0-or-later
# SDL3 backend of the renderer. Separate target so tools and tests stay free of SDL.

add_library(o1602_render_sdl
    src/paged_sprite_cache.cpp
    src/sdl_map_canvas.cpp
    src/sdl_sprite_renderer.cpp
    src/sdl_ui_renderer.cpp
)
add_library(o1602::render_sdl ALIAS o1602_render_sdl)

target_include_directories(o1602_render_sdl PUBLIC include)
target_link_libraries(o1602_render_sdl PUBLIC o1602::render o1602::platform o1602::ui)
o1602_set_warnings(o1602_render_sdl)
