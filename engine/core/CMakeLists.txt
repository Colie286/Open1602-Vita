# SPDX-License-Identifier: GPL-3.0-or-later

add_library(o1602_core
    src/block_reader.cpp
    src/workers.cpp
    src/compress.cpp
    src/data_file.cpp
    src/file.cpp
    src/profile.cpp
    src/hash.cpp
    src/png_reader.cpp
    src/png_writer.cpp
    src/text.cpp
    src/zip_archive.cpp
)
add_library(o1602::core ALIAS o1602_core)

target_include_directories(o1602_core PUBLIC include)
target_compile_features(o1602_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(o1602_core PRIVATE Threads::Threads)
o1602_set_warnings(o1602_core)

if(ZLIB_FOUND)
    target_link_libraries(o1602_core PRIVATE ZLIB::ZLIB)
    target_compile_definitions(o1602_core PRIVATE O1602_HAVE_ZLIB=1)
endif()
