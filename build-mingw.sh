#!/usr/bin/env bash
# Cross-compile both plug-in DLLs with MinGW-w64 (Linux, MSYS2 or WSL), then run the
# portable core tests on the host.
#
#   ./build-mingw.sh          -> dist/LinerNotes64.dll, dist/LinerNotes32.dll
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p dist build
SRC="src/core/*.cpp src/win/*.cpp"
FLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -finput-charset=UTF-8 -fexec-charset=UTF-8
       -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -shared -static -static-libgcc -static-libstdc++ -s"
LIBS="-lole32 -loleaut32 -luuid -lwinhttp -lcomctl32 -lshell32 -ladvapi32 -luser32 -lgdi32"
for arch in x86_64 i686; do
  suffix=$([ "$arch" = x86_64 ] && echo 64 || echo 32)
  echo "Building LinerNotes${suffix}.dll ($arch)"
  $arch-w64-mingw32-windres -I res -I src/win -i src/win/LinerNotes.rc -o build/res${suffix}.o
  # shellcheck disable=SC2086
  $arch-w64-mingw32-g++ $FLAGS -o dist/LinerNotes${suffix}.dll $SRC build/res${suffix}.o src/win/LinerNotes.def \
      -Wl,--kill-at,--enable-stdcall-fixup,--no-insert-timestamp $LIBS
done
echo "Running core tests on the host"
g++ -std=c++17 -O2 -Wall -Wextra -o build/test_core tests/test_core.cpp src/core/*.cpp && ./build/test_core
