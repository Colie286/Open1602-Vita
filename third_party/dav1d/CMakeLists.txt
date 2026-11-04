# SPDX-License-Identifier: BSD-2-Clause
# dav1d (AV1 decoder, BSD-2-Clause) built without its meson files: the portable C code only (no
# assembly, every ARCH_* switch off) and only 8 bits per component, as videos of content packages
# need (docs/modding/messages.md). The sources are dav1d's own, unchanged; config.h and
# include/vcs_version.h stand in for the files meson generates.

enable_language(C)

include(CheckCSourceCompiles)
include(CheckIncludeFile)
include(CheckSymbolExists)
include(TestBigEndian)

test_big_endian(DAV1D_BIG_ENDIAN_RESULT)
set(DAV1D_BIG_ENDIAN 0)
if(DAV1D_BIG_ENDIAN_RESULT)
    set(DAV1D_BIG_ENDIAN 1)
endif()

macro(dav1d_flag name)
    if(${name}_RESULT)
        set(${name} 1)
    else()
        set(${name} 0)
    endif()
endmacro()

check_include_file(sys/types.h DAV1D_HAVE_SYS_TYPES_H_RESULT)
check_include_file(unistd.h DAV1D_HAVE_UNISTD_H_RESULT)
check_include_file(io.h DAV1D_HAVE_IO_H_RESULT)
set(DAV1D_HAVE_CLOCK_GETTIME_RESULT 0)
set(DAV1D_HAVE_POSIX_MEMALIGN_RESULT 0)
set(DAV1D_HAVE_MEMALIGN_RESULT 0)
set(DAV1D_HAVE_ALIGNED_ALLOC_RESULT 0)
if(NOT WIN32)
    set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
    check_symbol_exists(clock_gettime time.h DAV1D_HAVE_CLOCK_GETTIME_RESULT)
    check_symbol_exists(posix_memalign stdlib.h DAV1D_HAVE_POSIX_MEMALIGN_RESULT)
    check_symbol_exists(memalign malloc.h DAV1D_HAVE_MEMALIGN_RESULT)
    check_symbol_exists(aligned_alloc stdlib.h DAV1D_HAVE_ALIGNED_ALLOC_RESULT)
    unset(CMAKE_REQUIRED_DEFINITIONS)
endif()
check_c_source_compiles("int main(void) { int x = _Generic(0, default: 0); return x; }" DAV1D_HAVE_C11_GENERIC_RESULT)
foreach(flag SYS_TYPES_H UNISTD_H IO_H CLOCK_GETTIME POSIX_MEMALIGN MEMALIGN ALIGNED_ALLOC C11_GENERIC)
    dav1d_flag(DAV1D_HAVE_${flag})
endforeach()
configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

set(DAV1D_SOURCES
    src/cdf.c src/cpu.c src/ctx.c src/data.c src/decode.c src/dequant_tables.c src/getbits.c
    src/intra_edge.c src/itx_1d.c src/lf_mask.c src/lib.c src/log.c src/mem.c src/msac.c src/obu.c
    src/pal.c src/picture.c src/qm.c src/ref.c src/refmvs.c src/scan.c src/tables.c
    src/thread_task.c src/warpmv.c src/wedge.c
)
# The templates, built once for 8 bits per component. Only they see BITDEPTH, as in dav1d's meson
# files: the common files take the pixel and coefficient buffers as untyped pointers without it
# (GCC 14 and later refuse decode.c with BITDEPTH set).
set(DAV1D_TEMPLATES
    src/cdef_apply_tmpl.c src/cdef_tmpl.c src/fg_apply_tmpl.c src/filmgrain_tmpl.c
    src/ipred_prepare_tmpl.c src/ipred_tmpl.c src/itx_tmpl.c src/lf_apply_tmpl.c
    src/loopfilter_tmpl.c src/looprestoration_tmpl.c src/lr_apply_tmpl.c src/mc_tmpl.c
    src/recon_tmpl.c
)
set_source_files_properties(${DAV1D_TEMPLATES} PROPERTIES COMPILE_DEFINITIONS BITDEPTH=8)
list(APPEND DAV1D_SOURCES ${DAV1D_TEMPLATES})
if(WIN32)
    list(APPEND DAV1D_SOURCES src/win32/thread.c)
endif()

add_library(o1602_dav1d STATIC ${DAV1D_SOURCES})
add_library(o1602::dav1d ALIAS o1602_dav1d)
set_target_properties(o1602_dav1d PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                                              POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(o1602_dav1d PRIVATE _GNU_SOURCE)
target_include_directories(o1602_dav1d
    SYSTEM PUBLIC include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} include/dav1d)
if(MSVC)
    # MSVC before the C11 atomics of VS 2022 17.5 needs dav1d's shim; it is harmless after.
    target_include_directories(o1602_dav1d PRIVATE include/compat/msvc)
    target_compile_definitions(o1602_dav1d PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(o1602_dav1d PRIVATE /W0)
else()
    target_compile_options(o1602_dav1d PRIVATE -w)
endif()
find_package(Threads REQUIRED)
target_link_libraries(o1602_dav1d PRIVATE Threads::Threads)
