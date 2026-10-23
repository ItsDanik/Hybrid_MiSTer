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
docker run --rm ${DNK_JOB:+--label dnk.job=$DNK_JOB} -v "$PWD":/src mister-hybrid-sim sh -c '
  FM="sim/jtopl_sh_sim.v rtl/hybrid_fm.sv $(ls rtl/jtopl/*.v | grep -v /jtopl_sh.v)"
  iverilog -g2012 -DSIMULATION -o /tmp/tbf sim/tb_fm.sv $FM && vvp -n /tmp/tbf &&
  iverilog -g2012 -o /tmp/tb sim/tb_host.sv rtl/hybrid_host.sv rtl/hybrid_fm.sv sim/jtopl2_stub.v sim/hybrid_midi_stub.sv template/core/sys/mt32pi.sv && vvp -n /tmp/tb &&
  iverilog -g2012 -Wall -o /tmp/tbv sim/tb_vclk.sv rtl/hybrid_vclk.sv && vvp -n /tmp/tbv; r=$?
  rm -f opl_wr.log jtopl.raw; exit $r'
