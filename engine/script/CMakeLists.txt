# SPDX-License-Identifier: GPL-3.0-or-later
# Scripts of content packages: sandboxed Lua 5.4 states that the simulation runs every tick
# (docs/modding/scripts.md). No platform dependency, so tests run it headless.

add_library(o1602_script
    src/game_scripts.cpp
    src/sandbox.cpp
)
add_library(o1602::script ALIAS o1602_script)

target_include_directories(o1602_script PUBLIC include)
target_link_libraries(o1602_script PUBLIC o1602::sim o1602::assets o1602::core PRIVATE o1602_lua)
o1602_set_warnings(o1602_script)
