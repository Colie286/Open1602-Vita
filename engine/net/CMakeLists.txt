# SPDX-License-Identifier: GPL-3.0-or-later
# Multiplayer: lockstep through the host over ENet (ADR 0011). Depends on sim only, never on
# platform, render, audio or ui, so that a dedicated server can run headless.

add_library(o1602_net
    src/master_protocol.cpp
    src/master_service.cpp
    src/master_client.cpp
    src/connectivity.cpp
    src/client_session.cpp
    src/enet_transport.cpp
    src/host_session.cpp
    src/lan.cpp
    src/loopback.cpp
    src/protocol.cpp
    src/session.cpp
)
add_library(o1602::net ALIAS o1602_net)

target_include_directories(o1602_net PUBLIC include)
find_package(Threads REQUIRED)  # host names are looked up beside the game (lan.cpp)
target_link_libraries(o1602_net PUBLIC o1602::sim PRIVATE o1602_enet Threads::Threads)
if(O1602_BUILD_VITA)
    target_link_libraries(o1602_net PRIVATE SceNet_stub SceNetCtl_stub)
endif()
o1602_set_warnings(o1602_net)
if(WIN32)
    # enet.h pulls in windows.h: no min/max macros, and only what sockets need.
    target_compile_definitions(o1602_net PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
