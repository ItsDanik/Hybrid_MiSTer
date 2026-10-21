#!/bin/bash
# The MIDI synthesizer's control engine and filter coefficients (RTL, in
# Verilator) against the C model, at every control update of the songs.
# usage: sim/midi_unit.sh <font.sf2> <song.mid>...
files=() vols=()
for f in "$@"; do
  f=$(realpath "$f")
  files+=("$f")
  vols+=(-v "$f:$f:ro")
done
cd "$(dirname "$0")/.."
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src "${vols[@]}" -w /src mister-hybrid-sim sh -c '
  mkdir -p /tmp/u && cd /tmp/u &&
  gcc -O2 -c -I/src/sim /src/sim/midi_unit_shim.c -o shim.o &&
  gcc -O2 -c -Dmain=render_main /src/tools/midisynth/render.c -o render.o &&
  gcc -O2 -c /src/tools/midisynth/smf.c -o smf.o &&
  gcc -O2 -c /src/hps/mister_midi.c -o midi.o &&
  cd /src && verilator -Wno-fatal --x-initial 0 --cc --exe --build -O3 --Mdir /tmp/u/v \
    --top-module tb_midi_unit sim/tb_midi_unit.sv rtl/hybrid_midi_ctrl.sv rtl/hybrid_midi_coef.sv \
    rtl/hybrid_midi_lib.sv /src/sim/midi_unit.cpp -CFLAGS "-O2 -I/src/sim" \
    -LDFLAGS "/tmp/u/shim.o /tmp/u/render.o /tmp/u/smf.o /tmp/u/midi.o -lm" > /tmp/u/v.log 2>&1 \
    || { tail -30 /tmp/u/v.log; exit 1; }
  /tmp/u/v/Vtb_midi_unit "$@"' sh "${files[@]}"
