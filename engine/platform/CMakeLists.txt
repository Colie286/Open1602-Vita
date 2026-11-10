# SPDX-License-Identifier: GPL-3.0-or-later
# Platform layer (SDL3): window, renderer, input, timing. Only built with O1602_BUILD_GAME.

find_package(SDL3 CONFIG COMPONENTS SDL3)
if(NOT SDL3_FOUND)
    message(FATAL_ERROR "SDL 3 not found, but the game needs it (O1602_BUILD_GAME=ON). Install it:\n"
        "  Debian 13, Ubuntu 25.04+:  sudo apt install libsdl3-dev\n"
        "  Fedora 41+:                sudo dnf install SDL3-devel\n"
        "  Arch, CachyOS, Manjaro:    sudo pacman -S sdl3\n"
        "  macOS (Homebrew):          brew install sdl3\n"
        "  Windows:                   use the vcpkg preset\n"
        "Or point CMAKE_PREFIX_PATH at an SDL 3 built from source, then configure again. "
        "Without the game (libraries, tests and o1602-tool only): -DO1602_BUILD_GAME=OFF.")
endif()
# SDL3Config marks its headers as non-system; our strict warning flags would then apply to them.
if(TARGET SDL3::Headers)
    set_property(TARGET SDL3::Headers PROPERTY SYSTEM 1)
endif()

add_library(o1602_platform
    src/audio_output.cpp
    src/mouse_cursor.cpp
    src/window.cpp
)
add_library(o1602::platform ALIAS o1602_platform)

target_include_directories(o1602_platform PUBLIC include)
target_link_libraries(o1602_platform PUBLIC o1602::core SDL3::SDL3)
o1602_set_warnings(o1602_platform)

# The window icon (title bar, task bar, macOS dock), rendered by tools/make_icons.py.
o1602_embed_files(o1602_platform o1602::platform::icons
    "${PROJECT_SOURCE_DIR}/data/icons/window/open1602-64.bmp"
    "${PROJECT_SOURCE_DIR}/data/icons/window/open1602-128.bmp")
