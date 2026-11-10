# SPDX-License-Identifier: MIT-0 OR Unlicense
#
# dr_flac 0.13.4 in development (https://github.com/mackron/dr_libs, commit d422edf of 2026-10-24,
# dr_flac.h unchanged; it fixes overflows and crashes on damaged files found after 0.13.3): decodes
# the FLAC sounds and music of content packages (engine/audio, docs/modding/sounds.md). Only the
# memory API: no stdio.

enable_language(C)

add_library(o1602_dr_flac STATIC dr_flac.c)
target_include_directories(o1602_dr_flac SYSTEM PUBLIC .)
target_compile_definitions(o1602_dr_flac PUBLIC DR_FLAC_NO_STDIO DR_FLAC_NO_WCHAR)
set_target_properties(o1602_dr_flac PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
