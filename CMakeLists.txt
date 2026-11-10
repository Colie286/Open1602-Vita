# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(Open1602Master LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
find_package(Threads REQUIRED)
add_subdirectory(third_party/enet)
add_executable(open1602-master
    tools/master-server/main.cpp
    engine/net/src/master_protocol.cpp
    engine/net/src/master_service.cpp
    engine/net/src/enet_transport.cpp)
target_include_directories(open1602-master PRIVATE engine/core/include engine/net/include)
target_link_libraries(open1602-master PRIVATE o1602_enet Threads::Threads)
if(WIN32)
    target_compile_definitions(open1602-master PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
o1602_set_warnings(open1602-master)
install(TARGETS open1602-master RUNTIME DESTINATION bin)
install(FILES LICENSE DESTINATION share/licenses/open1602-master)
install(FILES third_party/enet/LICENSE DESTINATION share/licenses/open1602-master RENAME ENet-LICENSE)
