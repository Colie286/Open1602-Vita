#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
source tools/vita/env.sh
test "$(git -C dependencies/SDL rev-parse HEAD)" = a8589a84226a6202831a3d49ff4edda4acab9acd
overwrite_patch="$PWD/platform/vita/patches/sdl3-atlas-overwrite.patch"
if git -C dependencies/SDL apply --reverse --check "$overwrite_patch" 2>/dev/null; then
    git -C dependencies/SDL apply --reverse "$overwrite_patch"
fi
# Temporarily remove our last layer to verify the pinned earlier layers on every build.
video_patch="$PWD/platform/vita/patches/sdl3-video-memory.patch"
if git -C dependencies/SDL apply --reverse --check "$video_patch" 2>/dev/null; then
    git -C dependencies/SDL apply --reverse "$video_patch"
fi
patch="$PWD/platform/vita/patches/sdl3-hardware-start.patch"
if git -C dependencies/SDL apply --check "$patch" 2>/dev/null; then
    git -C dependencies/SDL apply "$patch"
else
    git -C dependencies/SDL apply --reverse --check "$patch"
fi
patch="$PWD/platform/vita/patches/sdl3-debug-latin1.patch"
if git -C dependencies/SDL apply --check "$patch" 2>/dev/null; then
    git -C dependencies/SDL apply "$patch"
else
    git -C dependencies/SDL apply --reverse --check "$patch"
fi
patch="$PWD/platform/vita/patches/sdl3-ime-cancel.patch"
if git -C dependencies/SDL apply --check "$patch" 2>/dev/null; then
    git -C dependencies/SDL apply "$patch"
else
    git -C dependencies/SDL apply --reverse --check "$patch"
fi
git -C dependencies/SDL apply --check "$video_patch"
git -C dependencies/SDL apply "$video_patch"
git -C dependencies/SDL apply --check "$overwrite_patch"
git -C dependencies/SDL apply "$overwrite_patch"
# SDL3 already has an upstream Vita/GXM backend. Keep this independent of the SDK installation.
cmake -S dependencies/SDL -B build/sdl-vita \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/dependencies/vita" -DSDL_SHARED=OFF -DSDL_STATIC=ON \
  -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
cmake --build build/sdl-vita --parallel "${O1602_BUILD_JOBS:-4}"
cmake --install build/sdl-vita
