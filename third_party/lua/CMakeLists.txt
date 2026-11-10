# SPDX-License-Identifier: MIT
#
# Lua 5.4.8 (https://www.lua.org, sources from https://github.com/lua/lua tag v5.4.8, unchanged): the
# language of mod scripts (engine/script, docs/modding/scripts.md). Only the core and the libraries
# a sandboxed script may use: no io, os, package and debug libraries and no stand-alone interpreter.
# lstate.c is built through lstate_fixed_seed.c, which fixes the string hash seed (determinism).

enable_language(C)

add_library(o1602_lua STATIC
    lapi.c
    lauxlib.c
    lbaselib.c
    lcode.c
    lcorolib.c
    lctype.c
    ldebug.c
    ldo.c
    ldump.c
    lfunc.c
    lgc.c
    llex.c
    lmathlib.c
    lmem.c
    lobject.c
    lopcodes.c
    lparser.c
    lstate_fixed_seed.c
    lstring.c
    lstrlib.c
    ltable.c
    ltablib.c
    ltm.c
    lundump.c
    lutf8lib.c
    lvm.c
    lzio.c
)
target_include_directories(o1602_lua SYSTEM PUBLIC .)
set_target_properties(o1602_lua PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(o1602_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(NOT WIN32)
    target_link_libraries(o1602_lua PUBLIC m)
endif()
