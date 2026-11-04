# Open1602 - open-source engine for Anno 1602
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)

# Checked before project(), which loads the toolchain and builds the vcpkg packages: an unset
# VCPKG_ROOT or a 32-bit Visual Studio prompt would otherwise fail later with a less clear message.
if(CMAKE_TOOLCHAIN_FILE AND NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "Toolchain file \"${CMAKE_TOOLCHAIN_FILE}\" not found. For the vcpkg preset set "
        "VCPKG_ROOT to the vcpkg folder first: \"set VCPKG_ROOT=C:\\vcpkg\" in the Command Prompt, "
        "\"$env:VCPKG_ROOT = 'C:\\vcpkg'\" in PowerShell. Then delete the build folder and configure again.")
endif()
if(CMAKE_HOST_WIN32 AND DEFINED ENV{VSCMD_ARG_TGT_ARCH}
        AND NOT "$ENV{VSCMD_ARG_TGT_ARCH}" MATCHES "^(x64|arm64)$")
    message(FATAL_ERROR "This Visual Studio prompt targets $ENV{VSCMD_ARG_TGT_ARCH}, but Open1602 is 64-bit "
        "only. Open the \"x64 Native Tools Command Prompt for VS 2026\" instead.")
endif()

project(Open1602
    VERSION 0.4.2
    DESCRIPTION "Open-source engine reimplementation for Anno 1602"
    HOMEPAGE_URL "https://github.com/x4rd4s/open1602"
    LANGUAGES CXX)

if(WIN32)
    enable_language(RC)  # the program's icon (engine/app/open1602.rc.in)
endif()

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a build directory or a CMake preset.")
endif()

# Open1602 runs on 64-bit systems only (project decision of 18.10.2026).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Open1602 needs a 64-bit target; 32-bit builds are not supported.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    # Sources are UTF-8. The C library functions MSVC calls deprecated (fopen, getenv, localtime)
    # are the portable ones the engine uses on purpose.
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# The checks of libstdc++ (bounds of operator[], front() of an empty container) in every Debug
# build; other standard libraries ignore the definition. Set here and not in the presets'
# CMAKE_CXX_FLAGS, which would replace MSVC's defaults (/EHsc) and the user's CXXFLAGS.
add_compile_definitions($<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(EmbedFiles)
include(WindowsUtf8)

option(O1602_BUILD_TESTS  "Build unit tests"                         ON)
option(O1602_BUILD_TOOLS  "Build command line tools (o1602-tool)"    ON)
option(O1602_BUILD_GAME   "Build the game executable (needs SDL3)"   OFF)
option(O1602_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"  OFF)

# The programs a player starts (the game, o1602-tool and, on Windows, the DLLs they need) land in
# one folder on every platform. The presets for playing (release, vcpkg) build the game and set it
# to bin/ at the top of the source tree; every other build keeps them in bin/ of its build folder.
set(O1602_OUTPUT_DIR "${PROJECT_BINARY_DIR}/bin" CACHE PATH "Folder the game and o1602-tool are built into")

# Puts a program into O1602_OUTPUT_DIR. The generator expression keeps multi-config generators
# (Visual Studio, Xcode) from adding a Debug/Release subfolder.
function(o1602_set_output_dir target)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${O1602_OUTPUT_DIR}$<0:>")
endfunction()

# zlib packs savegames (.o1602) and PNG screenshots. The game needs it: without it, it could not
# load the compressed savegames other builds write. Libraries, tests and tools build without it.
if(O1602_BUILD_GAME)
    find_package(ZLIB)
    if(NOT ZLIB_FOUND)
        message(FATAL_ERROR "zlib not found, but the game needs it (O1602_BUILD_GAME=ON). Install it: "
            "\"sudo apt install zlib1g-dev\" (Debian, Ubuntu), \"sudo dnf install zlib-devel\" (Fedora); "
            "Arch and macOS bring it along, Windows uses the vcpkg preset.")
    endif()
else()
    find_package(ZLIB QUIET)
    if(NOT ZLIB_FOUND)
        message(WARNING "zlib not found: compressed savegames can neither be written nor read")
    endif()
endif()

add_subdirectory(third_party/enet)
add_subdirectory(third_party/stb_vorbis)
add_subdirectory(third_party/dr_flac)
add_subdirectory(third_party/lua)
add_subdirectory(third_party/dav1d)
add_subdirectory(third_party/opus)
add_subdirectory(engine)

if(O1602_BUILD_TOOLS)
    add_subdirectory(tools/o1602-tool)
endif()

if(O1602_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Says which programs this build makes, so a build without the game does not go unnoticed.
if(O1602_BUILD_GAME)
    message(STATUS "Open1602: the game is built into ${O1602_OUTPUT_DIR}")
else()
    message(STATUS "Open1602: the game is NOT built (O1602_BUILD_GAME=OFF), only the libraries, tests "
        "and o1602-tool. For the game: -DO1602_BUILD_GAME=ON or the release preset.")
endif()
