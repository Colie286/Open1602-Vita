# SPDX-License-Identifier: MIT OR Unlicense
#
# stb_vorbis 1.22 (https://github.com/nothings/stb, commit 1ee679c, stb_vorbis.c unchanged): decodes
# the Ogg Vorbis sounds and music of content packages (engine/audio, docs/modding/sounds.md). Only
# the memory API: no stdio, no push data.

enable_language(C)

add_library(o1602_stb_vorbis STATIC stb_vorbis.c)
target_include_directories(o1602_stb_vorbis SYSTEM PUBLIC .)
target_compile_definitions(o1602_stb_vorbis PUBLIC STB_VORBIS_NO_STDIO STB_VORBIS_NO_PUSHDATA_API)
set_target_properties(o1602_stb_vorbis PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(o1602_stb_vorbis PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
