# SPDX-License-Identifier: GPL-3.0-or-later
# o1602_set_warnings(<target>) - applies the project's warning policy to a target.

function(o1602_set_warnings target)
    if(MSVC)
        set(warnings /W4 /permissive- /w14242 /w14254 /w14263 /w14265 /w14287 /w14296 /w14311 /w14545
                     /w14546 /w14547 /w14549 /w14555 /w14619 /w14640 /w14826 /w14905 /w14906 /w14928)
        if(O1602_WARNINGS_AS_ERRORS)
            list(APPEND warnings /WX)
        endif()
    else()
        set(warnings -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
                     -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion -Wnull-dereference
                     -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            list(APPEND warnings -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op)
        endif()
        if(O1602_WARNINGS_AS_ERRORS)
            list(APPEND warnings -Werror)
        endif()
    endif()
    target_compile_options(${target} PRIVATE ${warnings})
endfunction()
