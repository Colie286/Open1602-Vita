# SPDX-License-Identifier: GPL-3.0-or-later
include("${VITASDK}/share/vita.cmake" REQUIRED)
option(O1602_VITA_NETWORK "Enable original ENet networking on demand" ON)
option(O1602_VITA_WORKER_QA "PRIVATE QA ONLY: unbound parallel ARM worker validation" OFF)
option(O1602_VITA_EXCEPTION_TRACE "Trace C++ allocation failures in private diagnosis builds" OFF)
if(NOT O1602_VITA_DIAGNOSTIC)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/boot_locale.h"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/vita/boot-locale.py"
            "${PROJECT_SOURCE_DIR}/data/locale" "${CMAKE_CURRENT_BINARY_DIR}/boot_locale.h"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/vita/boot-locale.py"
            "${PROJECT_SOURCE_DIR}/data/locale/de.lang" "${PROJECT_SOURCE_DIR}/data/locale/en.lang")
    add_custom_target(vita_boot_locale DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/boot_locale.h")
    add_dependencies(open1602 vita_boot_locale)
    target_include_directories(open1602 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_sources(open1602 PRIVATE client.cpp startup.c session.cpp network.cpp freeze.cpp)
    target_link_options(open1602 PRIVATE "-Wl,--wrap=enet_initialize")
    target_link_options(open1602 PRIVATE "-Wl,--wrap=enet_socket_create,--wrap=enet_socket_destroy,--wrap=enet_socket_bind,--wrap=enet_socket_get_address,--wrap=enet_socket_set_option,--wrap=enet_socket_wait,--wrap=enet_socket_send,--wrap=enet_socket_receive")
    target_link_libraries(open1602 PRIVATE o1602_enet ScePower_stub SceNet_stub SceNetCtl_stub SceCommonDialog_stub)
    if(O1602_VITA_NETWORK)
        target_compile_definitions(open1602 PRIVATE O1602_VITA_NETWORK=1)
    endif()
    execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE vita_commit OUTPUT_STRIP_TRAILING_WHITESPACE)
    target_compile_definitions(open1602 PRIVATE O1602_VITA_COMMIT="${vita_commit}")
    if(O1602_VITA_EXCEPTION_TRACE)
        target_sources(open1602 PRIVATE exception_trace.cpp)
        target_compile_definitions(open1602 PRIVATE O1602_VITA_EXCEPTION_TRACE=1)
        target_link_options(open1602 PRIVATE "-Wl,--wrap=__cxa_throw,--wrap=malloc")
    endif()
    # libstdc++ probes weak pthread_cancel/cond symbols; a normal static -lpthread
    # leaves those unresolved even when pthread_create is pulled in.
    target_link_libraries(open1602 PRIVATE "-Wl,--whole-archive" pthread "-Wl,--no-whole-archive")
    target_link_options(open1602 PRIVATE "-Wl,--wrap=_init_vita_heap,--wrap=__libc_init_array,--wrap=sceKernelAllocMemBlock,--wrap=sceKernelCreateThread,--wrap=sceKernelFreeMemBlock"
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/open1602.map"
        "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/segment-gap.ld")
    set_property(TARGET open1602 APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/segment-gap.ld")
    vita_create_self(open1602.self "${O1602_OUTPUT_DIR}/open1602" UNSAFE)
    add_dependencies(open1602-velf open1602)
    add_executable(open1602_boot launcher.c startup.c)
    add_dependencies(open1602_boot vita_boot_locale)
    target_include_directories(open1602_boot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(open1602_boot PRIVATE O1602_BOOT_LAUNCHER=1)
    target_link_libraries(open1602_boot PRIVATE SceAppMgr_stub SceCtrl_stub SceDisplay_stub SceSysmem_stub SceIofilemgr_stub SceLibKernel_stub SceProcessmgr_stub)
    target_link_options(open1602_boot PRIVATE "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/segment-gap.ld" "-Wl,--wrap=_init_vita_heap,--wrap=__libc_init_array,--wrap=sceKernelAllocMemBlock,--wrap=sceKernelCreateThread,--wrap=sceKernelFreeMemBlock")
    vita_create_self(launcher.self open1602_boot UNSAFE)
    add_dependencies(open1602_boot-velf open1602_boot)
    set(vita_package_name "Open1602")
    if(O1602_VITA_WORKER_QA)
        set(vita_package_name "Open1602 Worker QA")
        set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/client.cpp" TARGET_DIRECTORY open1602 APPEND PROPERTY COMPILE_DEFINITIONS O1602_VITA_WORKER_QA=1)
    endif()
    set(O1602_VITA_ARTWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/livearea/assets" CACHE PATH "LiveArea artwork directory")
    foreach(asset icon0.png bg0.png startup.png)
        if(NOT EXISTS "${O1602_VITA_ARTWORK_DIR}/${asset}")
            message(FATAL_ERROR "Missing LiveArea build asset: ${asset}")
        endif()
    endforeach()
    vita_create_vpk(open1602.vpk O1602VITA launcher.self VERSION 01.60 NAME "${vita_package_name}"
        FILE "${O1602_VITA_ARTWORK_DIR}/icon0.png" "sce_sys/icon0.png"
        FILE "${O1602_VITA_ARTWORK_DIR}/bg0.png" "sce_sys/livearea/contents/bg0.png"
        FILE "${O1602_VITA_ARTWORK_DIR}/startup.png" "sce_sys/livearea/contents/startup.png"
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/livearea/template.xml" "sce_sys/livearea/contents/template.xml"
        FILE "${CMAKE_CURRENT_BINARY_DIR}/open1602.self" "client.self"
        FILE "${PROJECT_SOURCE_DIR}/third_party/dav1d/COPYING" "licenses/dav1d.txt"
        FILE "${PROJECT_SOURCE_DIR}/third_party/opus/COPYING" "licenses/opus.txt"
        FILE "${PROJECT_SOURCE_DIR}/LICENSE" "licenses/Open1602.txt"
        FILE "${PROJECT_SOURCE_DIR}/third_party/enet/LICENSE" "licenses/enet.txt"
        FILE "${PROJECT_SOURCE_DIR}/third_party/lua/LICENSE" "licenses/lua.txt"
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/licenses/SDL3.txt" "licenses/SDL3.txt"
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/licenses/ThirdParty.txt" "licenses/ThirdParty.txt"
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/licenses/zlib.txt" "licenses/zlib.txt")
    add_dependencies(open1602.vpk-vpk open1602.self-self)
    return()
endif()
