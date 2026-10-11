#!/bin/sh
# Play a log of FM register writes on JTOPL (Verilator) and on Nuked OPL3,
# to compare the core's chip with the real one's behaviour on real music.
# usage: sim/fm_song.sh <log> <out prefix> [seconds]
#   log: from fakecore, FAKECORE_FMLOG=<file>; both paths as seen from the
#   framework's directory (put the files under it)
# Writes <out>_jtopl.raw and <out>_nuked.raw: 16-bit mono, 49716Hz.
cd "$(dirname "$0")/.."
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src mister-hybrid-sim sh -c '
  mkdir -p /tmp/v && verilator -Wno-fatal --x-initial 0 --cc --exe --build -O3 --Mdir /tmp/v \
    --top-module jtopl2 -Irtl/jtopl rtl/jtopl/jtopl2.v /src/sim/fm_song.cpp /src/tools/nuked/opl3.c \
    -CFLAGS "-O2" > /tmp/v.log 2>&1 || { tail -30 /tmp/v.log; exit 1; }
  /tmp/v/Vjtopl2 "$@"' sh "$@"
