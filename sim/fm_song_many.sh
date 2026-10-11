#!/bin/sh
# As fm_song.sh for every <dir>/*.log (path under the framework's directory)
cd "$(dirname "$0")/.."
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src mister-hybrid-sim sh -c '
  mkdir -p /tmp/v && verilator -Wno-fatal --x-initial 0 --cc --exe --build -O3 --Mdir /tmp/v \
    --top-module jtopl2 -Irtl/jtopl rtl/jtopl/jtopl2.v /src/sim/fm_song.cpp /src/tools/nuked/opl3.c \
    -CFLAGS "-O2" > /tmp/v.log 2>&1 || { tail -30 /tmp/v.log; exit 1; }
  for f in "$1"/*.log; do /tmp/v/Vjtopl2 "$f" "${f%.log}" 2>/dev/null; done' sh "$@"
