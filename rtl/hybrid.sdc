# The video clock is not related to any other: what crosses between it and
# the 50MHz in hybrid_host goes through synchronisers or holds still.
set_clock_groups -asynchronous -group [get_clocks {*|pll_vid|pll_vid_inst|altera_pll_i|*[0].*|divclk}]
# The MIDI synthesizer's 100MHz neither: hybrid_midi.sv crosses to and from
# it through FIFOs with Gray pointers, toggles and held registers.
set_clock_groups -asynchronous -group [get_clocks {*|pll_midi|pll_midi_inst|altera_pll_i|*[0].*|divclk}]
