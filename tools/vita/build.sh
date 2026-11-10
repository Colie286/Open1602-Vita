#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/../.."
source tools/vita/env.sh
if [[ $# != 0 ]]; then
    echo "Usage: $0" >&2
    exit 2
fi
test "$(git -C dependencies/zlib rev-parse HEAD)" = 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
cmake -S dependencies/zlib -B build/zlib-vita -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build/zlib-vita --target zlibstatic --parallel "${O1602_BUILD_JOBS:-4}"
mkdir -p dependencies/vita/include dependencies/vita/lib
cp dependencies/zlib/zlib.h build/zlib-vita/zconf.h dependencies/vita/include/
cp build/zlib-vita/libzlibstatic.a dependencies/vita/lib/libz.a
cmake -S . -B build/vita -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DO1602_VITA_WORKER_QA=OFF -DO1602_VITA_NETWORK=ON \
  -DSDL3_DIR="$PWD/dependencies/vita/lib/cmake/SDL3" \
  -DZLIB_INCLUDE_DIR="$PWD/dependencies/vita/include" -DZLIB_LIBRARY="$PWD/dependencies/vita/lib/libz.a"
cmake --build build/vita --parallel "${O1602_BUILD_JOBS:-4}"
python3 tools/vita/verify-vpk.py build/vita/platform/vita/open1602.vpk
