#!/bin/sh
# The web version's engine (Source/web/RaWeb.cpp with the core and the simulator) -> WebAssembly, with Zig's
# clang (pip install ziglang): no Emscripten, no JavaScript glue; the page gives it the few WASI calls it needs.
#   tools/web/build_wasm.sh [out.wasm]        (default site/app/ra.wasm)
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-site/app/ra.wasm}
mkdir -p "$(dirname "$OUT")"
python3 -m ziglang c++ -target wasm32-wasi -mexec-model=reactor -O2 -std=c++20 -fno-exceptions -fno-rtti \
  -DNDEBUG -Wall -Wno-unused-function -Wno-unused-const-variable -Wl,--strip-all \
  Source/web/RaWeb.cpp Source/core/RaSeq.cpp Source/core/RaKit.cpp Source/core/RaSysex.cpp Source/core/RaMorph.cpp Source/sim/RaSynth.cpp \
  -o "$OUT"
ls -l "$OUT"
