# The video clock is not related to any other: what crosses between it and
# the 50MHz in hybrid_host goes through synchronisers or holds still.
set_clock_groups -asynchronous -group [get_clocks {*|pll_vid|pll_vid_inst|altera_pll_i|*[0].*|divclk}]
# The MIDI synthesizer's 100MHz neither: hybrid_midi.sv crosses to and from
# it through FIFOs with Gray pointers, toggles and held registers. Its PLL is
# in hybrid_host's generate block (midi_synth.pll_midi), and not there at all in a
# core built with MIDI_SYNTH 0.
set midi_clk [get_clocks -nowarn {*pll_midi|pll_midi_inst|altera_pll_i|*[0].*|divclk}]
if {[get_collection_size $midi_clk] > 0} {
	set_clock_groups -asynchronous -group $midi_clk
}
