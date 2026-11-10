# SPDX-License-Identifier: GPL-3.0-or-later
# Rendering. The software path (island_renderer) has no platform dependency and is used by tools
# and tests; the SDL3/GPU backend will be added as a separate target.

add_library(o1602_render
    src/land_figures.cpp
    src/walker_figures.cpp
    src/ambient.cpp
    src/animation_clock.cpp
    src/banner.cpp
    src/camera.cpp
    src/cart_figures.cpp
    src/combat_effects.cpp
    src/figure_animation.cpp
    src/flag_tint.cpp
    src/dither.cpp
    src/graphics_preloader.cpp
    src/island_renderer.cpp
    src/sprite_atlas.cpp
    src/sprite_hit_mask.cpp
    src/ship_trail.cpp
    src/tick_trail.cpp
    src/shape_tint.cpp
    src/package_sprites.cpp
    src/sprite_source.cpp
    src/unit_tint.cpp
    src/variant_tint.cpp
    src/world_graphics.cpp
    src/world_renderer.cpp
)
add_library(o1602::render ALIAS o1602_render)

target_include_directories(o1602_render PUBLIC include)
find_package(Threads REQUIRED)  # the zoom levels are loaded beside the game (graphics_preloader.cpp)
target_link_libraries(o1602_render PUBLIC o1602::sim o1602::assets PRIVATE Threads::Threads)
o1602_set_warnings(o1602_render)
