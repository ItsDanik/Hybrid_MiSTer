// The MIDI synthesizer's clock (hybrid_midi.sv): 100MHz from the board's
// 50MHz, not related to the core's clock (the two meet only in FIFOs and
// synchronisers).
`timescale 1 ps / 1 ps
module pll_midi (
		input  wire        refclk,
		input  wire        rst,
		output wire        outclk_0,
		output wire        locked
	);

	pll_midi_0002 pll_midi_inst (
		.refclk            (refclk),
		.rst               (rst),
		.outclk_0          (outclk_0),
		.locked            (locked)
	);

endmodule
