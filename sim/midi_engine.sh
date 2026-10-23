#!/bin/bash
# The MIDI synthesizer's RTL (Verilator) against the C model, frame by frame.
# usage: sim/midi_engine.sh <font.sf2> <song.mid> [seconds] [fast] [ahead=N] [lat=N]
#   (sim/midi_engine.cpp)
# a slot of this PC's simulations, hybrid/tools/jobs.py shows them
[ -n "$DNK_JOB" ] || exec "$(dirname "$0")/../tools/job.sh" sim "$(basename "$0" .sh)" "$0" "$@"
font=$(realpath "$1") song=$(realpath "$2")
shift 2
cd "$(dirname "$0")/.."
docker run --rm ${DNK_JOB:+--label dnk.job=$DNK_JOB} -e VFLAGS -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -v "$font:$font:ro" -v "$song:$song:ro" \
  -w /src mister-hybrid-sim sh -c '
  mkdir -p /tmp/e && cd /tmp/e &&
  gcc -O2 -c /src/sim/midi_engine_gen.c -o gen.o &&
  gcc -O2 -c /src/tools/midisynth/midisynth.c -o ms.o &&
  gcc -O2 -c /src/tools/midisynth/smf.c -o smf.o &&
  gcc -O2 -c /src/hps/mister_midi.c -o midi.o &&
  cd /src && verilator -Wno-fatal --x-initial 0 $VFLAGS --cc --exe --build -O3 --Mdir /tmp/e/v \
    -GTABLES="\"rtl/midi/\"" --top-module hybrid_midi rtl/hybrid_midi.sv rtl/hybrid_midi_fetch.sv \
    rtl/hybrid_midi_ctrl.sv rtl/hybrid_midi_coef.sv rtl/hybrid_midi_fx.sv rtl/hybrid_midi_lib.sv /src/sim/midi_engine.cpp \
    -CFLAGS "-O2 -I/src/sim" -LDFLAGS "/tmp/e/gen.o /tmp/e/ms.o /tmp/e/smf.o /tmp/e/midi.o -lm" > /tmp/e/v.log 2>&1 \
    || { grep -i error /tmp/e/v.log | head -30; tail -5 /tmp/e/v.log; exit 1; }
  /tmp/e/v/Vhybrid_midi "$@"' sh "$font" "$song" "$@"
