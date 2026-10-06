//============================================================================
//
//  MiSTer hybrid core host - video clock
//
//  The clock of the video output, from a PLL of its own that is set to
//  another frequency while the core runs: hybrid_host asks for 50MHz (the
//  15kHz video modes and those at 31kHz), a little more or less (the CRT
//  option "Horizontal Size"), 40MHz (800x600) or 65MHz (1024x768).
//
//  `code` is the frequency in units of 0.5MHz, 80..130. The PLL makes
//  50MHz * M.K / 10, so M.K = code / 10: M is written to the PLL's M counter
//  and K to its fractional part. The other settings are the ones Main_MiSTer
//  uses for the HDMI PLL, which is of the same kind.
//
//  The clock is measured against the 50MHz all the time and `ready` is only
//  1 while it is the one asked for: hybrid_host keeps the video output (and
//  with it the sync signals) off otherwise, so that no screen is given a line
//  frequency it was not meant to get, whatever the PLL does.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//============================================================================

module hybrid_vclk
(
	input             clk,      // 50MHz
	input             refclk,   // 50MHz for the PLL (CLK_50M)
	input       [7:0] code,     // the clock wanted, in units of 0.5MHz

	output            clk_vid,
	output            ready,    // clk_vid is measured to be the clock wanted
	// clk_vid periods in 4000 periods of clk: 40 * (MHz * 2)
	output reg [12:0] count
);

//////////////////////////////////////////////////////////////////
// PLL and its reconfiguration

wire [63:0] reconfig_to_pll, reconfig_from_pll;
wire        locked;
wire        cfg_waitrequest;
reg         cfg_write = 0;
reg   [5:0] cfg_address;
reg  [31:0] cfg_data;

pll_vid pll_vid
(
	.refclk(refclk),
	.rst(1'b0),
	.outclk_0(clk_vid),
	.locked(locked),
	.reconfig_to_pll(reconfig_to_pll),
	.reconfig_from_pll(reconfig_from_pll)
);

pll_cfg pll_cfg
(
	.mgmt_clk(clk),
	.mgmt_reset(1'b0),
	.mgmt_waitrequest(cfg_waitrequest),
	.mgmt_read(1'b0),
	.mgmt_readdata(),
	.mgmt_write(cfg_write),
	.mgmt_address(cfg_address),
	.mgmt_writedata(cfg_data),
	.reconfig_to_pll(reconfig_to_pll),
	.reconfig_from_pll(reconfig_from_pll)
);

// a counter of the PLL as its two halves, with the flag for an odd count
function [31:0] pll_div(input [7:0] n);
	pll_div = {14'd0, n[0], 1'b0, 8'd0 + n[7:1] + n[0], 1'b0, n[7:1]};
endfunction

reg   [7:0] cfg_code = 8'd130;  // what the PLL is set to; 65MHz as built
reg   [7:0] new_code;
// code / 10 with 32 fractional bits. The factor is rounded up, so that a
// whole number is that number and a small K, never the number below and a K
// of nearly 1, which the PLL does not like
wire [39:0] mk = new_code * 40'd429496730;

reg   [4:0] step = 0;           // 0: idle
reg   [8:0] bad = 0;            // windows the clock was not the one set

always @(posedge clk) begin
	// a write is one clock long, at a time when the PLL side takes one
	cfg_write <= 0;
	if (step == 0) begin
		// set the PLL again as well if it does not get there (20ms)
		if (code != cfg_code || bad[8]) begin
			new_code <= code;
			step     <= 1;
		end
	end
	else if (!cfg_waitrequest) begin
		step <= step + 1'd1;
		// every write is followed by a clock without one
		case (step)
			 1: begin cfg_address <= 0; cfg_data <= 0;                    cfg_write <= 1; end // waitrequest mode
			 3: begin cfg_address <= 4; cfg_data <= pll_div(mk[39:32]);   cfg_write <= 1; end // M
			 5: begin cfg_address <= 3; cfg_data <= 32'h10000;            cfg_write <= 1; end // N: bypassed
			 7: begin cfg_address <= 5; cfg_data <= pll_div(8'd10);       cfg_write <= 1; end // C0
			 9: begin cfg_address <= 9; cfg_data <= 2;                    cfg_write <= 1; end // charge pump
			11: begin cfg_address <= 8; cfg_data <= 7;                    cfg_write <= 1; end // bandwidth
			13: begin cfg_address <= 7; cfg_data <= mk[31:0];             cfg_write <= 1; end // K
			15: begin cfg_address <= 2; cfg_data <= 0;                    cfg_write <= 1; end // start
			// waitrequest stays up from here until the PLL has locked
			19: begin cfg_code <= new_code; step <= 0; end
			default: ;
		endcase
	end
end

//////////////////////////////////////////////////////////////////
// Measurement: a Gray counter on clk_vid, read every 4000 periods of clk

reg  [12:0] vbin = 0, vgray = 0;
wire [12:0] vnext = vbin + 1'd1;
always @(posedge clk_vid) begin
	vbin  <= vnext;
	vgray <= vnext ^ (vnext >> 1);
end

reg  [12:0] gray_s1, gray_s2;
reg  [12:0] bin, bin_prev = 0;
reg  [11:0] window = 0;
reg   [1:0] locked_s = 0;
reg   [1:0] good = 0;           // windows in a row with the clock that was set
reg         measured = 0;

integer i;
always @(*) begin
	bin[12] = gray_s2[12];
	for (i = 11; i >= 0; i = i - 1) bin[i] = bin[i + 1] ^ gray_s2[i];
end

wire [12:0] expected = {cfg_code, 5'd0} + {2'd0, cfg_code, 3'd0};
wire [12:0] off = count - expected + 13'd12;

always @(posedge clk) begin
	gray_s1  <= vgray;
	gray_s2  <= gray_s1;
	locked_s <= {locked_s[0], locked};

	measured <= 0;
	window   <= window + 1'd1;
	if (window == 12'd3999) begin
		window   <= 0;
		count    <= bin - bin_prev;
		bin_prev <= bin;
		measured <= 1;
	end

	if (measured) begin
		// within 0.15MHz: the next frequency in use is 0.5MHz away
		if (off <= 13'd24 && locked_s[1]) begin
			if (good != 2'd3) good <= good + 1'd1;
			bad <= 0;
		end else begin
			good <= 0;
			if (!bad[8]) bad <= bad + 1'd1;
		end
	end

	// while the PLL is being set nothing counts, nor does the window it ended in
	if (step != 0) begin
		good <= 0;
		bad  <= 0;
	end
end

assign ready = (step == 0) && (cfg_code == code) && (good == 2'd3);

endmodule
