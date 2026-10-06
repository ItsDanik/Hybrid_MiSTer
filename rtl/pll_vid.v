// Video clock PLL of the hybrid cores: fractional, reconfigured at run time by
// hybrid_vclk.sv. 50MHz * M.K / 10; as built 65MHz (M = 13), the highest
// clock in use, which is what the timing analysis has to see.
`timescale 1 ps / 1 ps
module pll_vid (
		input  wire        refclk,
		input  wire        rst,
		output wire        outclk_0,
		output wire        locked,
		input  wire [63:0] reconfig_to_pll,
		output wire [63:0] reconfig_from_pll
	);

	pll_vid_0002 pll_vid_inst (
		.refclk            (refclk),
		.rst               (rst),
		.outclk_0          (outclk_0),
		.reconfig_to_pll   (reconfig_to_pll),
		.reconfig_from_pll (reconfig_from_pll),
		.locked            (locked)
	);

endmodule
