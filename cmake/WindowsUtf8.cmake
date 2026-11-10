# SPDX-License-Identifier: GPL-3.0-or-later
# o1602_utf8_manifest(<target>) - on Windows (MSVC) the program runs with UTF-8 as its ANSI code
# page (utf8.manifest), so narrow paths and arguments are UTF-8 as on Linux and macOS.

function(o1602_utf8_manifest target)
    if(MSVC)
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/cmake/utf8.manifest")
    endif()
endfunction()
