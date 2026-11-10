# SPDX-License-Identifier: GPL-3.0-or-later
#
# Engine modules and what each links (an arrow points from a module to what it uses):
#
#   assets -> core              sim -> assets               net -> sim (+ ENet)
#   render -> sim, assets       audio -> sim, assets        video -> audio
#   ui -> sim, assets           platform -> core (+ SDL3)   render_sdl -> render, platform, ui
#   script -> sim, assets (+ Lua)
#   menus -> sim, assets, audio, render, ui, net
#   app (open1602) -> sim, render_sdl, platform, audio, video, ui, net, menus, script
#
# Only the game build (O1602_BUILD_GAME) adds platform, render_sdl and app; everything else builds
# and is tested without SDL.
#
# 'sim' must never depend on platform, render, audio or ui: the simulation has to stay
# deterministic and runnable headless (tests, dedicated server, replays). The ctest "layering"
# (cmake/CheckLayering.cmake) checks the includes of every module against these links.

add_subdirectory(core)
add_subdirectory(assets)
add_subdirectory(sim)
add_subdirectory(net)
add_subdirectory(render)
add_subdirectory(audio)
add_subdirectory(video)
add_subdirectory(ui)
add_subdirectory(script)
add_subdirectory(app/menus)

if(O1602_BUILD_GAME)
    add_subdirectory(platform)
    add_subdirectory(render/sdl)
    add_subdirectory(app)
endif()
