#!/bin/sh
# Run the hybrid_host testbench in the simulation container
# a slot of this PC's simulations, hybrid/tools/jobs.py shows them
[ -n "$DNK_JOB" ] || exec "$(dirname "$0")/../tools/job.sh" sim "$(basename "$0" .sh)" "$0" "$@"
cd "$(dirname "$0")/.."
docker image inspect mister-hybrid-sim > /dev/null 2>&1 || docker build -t mister-hybrid-sim toolchain/sim
# The FM chip has a testbench of its own (tb_fm.sv). jtopl is simulated with
# SIMULATION defined (it then starts its counters at zero) and with a shift
# register that starts empty (jtopl_sh_sim.v). The hybrid_host testbench has a
# stand-in for the chip (jtopl2_stub.v): the real one would take hours there,
# and one for the MIDI synthesizer (hybrid_midi_stub.sv; sim/midi_engine.sh
# tests the real one).
# `sim/run.sh host [-D<define> ...] [+<plusarg> ...]` runs the hybrid_host
# testbench alone, built with those defines and run with those plusargs (see
# tb_host.sv): -DNO_INTRO (a frontend's core without the intro, a few
# minutes), +introonly (the intro and the handover after it), -DNO_SYNTH
# +trigonly, +mt32only, +fieldrate (host version 18: field rate alone).
if [ "$1" = host ]; then
  shift
  DEFS= ARGS=
  for a in "$@"; do
    case $a in
      -D*) DEFS="$DEFS $a" ;;
      +*) ARGS="$ARGS $a" ;;
      *) echo "run.sh host: -D<define> or +<plusarg>, not $a" >&2; exit 2 ;;
    esac
  done
  exec docker run --rm ${DNK_JOB:+--label dnk.job=$DNK_JOB} -e DEFS="$DEFS" -e ARGS="$ARGS" -v "$PWD":/src mister-hybrid-sim sh -c '
  iverilog -g2012 $DEFS -o /tmp/tb sim/tb_host.sv rtl/hybrid_host.sv rtl/hybrid_fm.sv sim/jtopl2_stub.v sim/hybrid_midi_stub.sv template/core/sys/mt32pi.sv && vvp -n /tmp/tb $ARGS'
fi
docker run --rm ${DNK_JOB:+--label dnk.job=$DNK_JOB} -v "$PWD":/src mister-hybrid-sim sh -c '
  FM="sim/jtopl_sh_sim.v rtl/hybrid_fm.sv $(ls rtl/jtopl/*.v | grep -v /jtopl_sh.v)"
  iverilog -g2012 -DSIMULATION -o /tmp/tbf sim/tb_fm.sv $FM && vvp -n /tmp/tbf &&
  iverilog -g2012 -o /tmp/tb sim/tb_host.sv rtl/hybrid_host.sv rtl/hybrid_fm.sv sim/jtopl2_stub.v sim/hybrid_midi_stub.sv template/core/sys/mt32pi.sv && vvp -n /tmp/tb &&
  iverilog -g2012 -Wall -o /tmp/tbv sim/tb_vclk.sv rtl/hybrid_vclk.sv && vvp -n /tmp/tbv; r=$?
  rm -f opl_wr.log jtopl.raw; exit $r'
