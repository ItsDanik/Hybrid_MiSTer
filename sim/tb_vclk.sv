// Testbench for hybrid_vclk: models of the PLL and of its reconfiguration
// (pll_cfg of the MiSTer framework) that make the clock the registers
// written say, 50MHz * M.K / C. Checks the frequency for every clock in use,
// that `ready` is only up for the clock asked for, and that a PLL that did
// not take its settings is set again.
`timescale 1ns/1ps

module pll_cfg
(
	input             mgmt_clk,
	input             mgmt_reset,
	output            mgmt_waitrequest,
	input             mgmt_read,
	input             mgmt_write,
	output     [31:0] mgmt_readdata,
	input       [5:0] mgmt_address,
	input      [31:0] mgmt_writedata,
	output reg [63:0] reconfig_to_pll,
	input      [63:0] reconfig_from_pll
);
	integer m = 13, c = 10, busy = 0, writes = 0, bad = 0;
	reg [31:0] k = 1;
	reg        ignore = 0;   // set by the testbench: the next start does nothing
	assign mgmt_readdata = 0;
	assign mgmt_waitrequest = (busy != 0);
	initial reconfig_to_pll = $realtobits(65.0);
	always @(posedge mgmt_clk) begin
		if (busy != 0) begin
			if (mgmt_write) begin $display("FAIL: write during waitrequest"); bad = bad + 1; end
			busy = busy - 1;
			if (busy == 0 && !ignore) reconfig_to_pll = $realtobits(50.0 * (m + k / 4294967296.0) / c);
			if (busy == 0) ignore = 0;
		end
		else if (mgmt_write) begin
			writes = writes + 1;
			case (mgmt_address)
				0: if (mgmt_writedata != 0) bad = bad + 1;
				2: busy = 5000;
				3: if (mgmt_writedata != 32'h10000) bad = bad + 1;
				4: begin
					m = mgmt_writedata[15:8] + mgmt_writedata[7:0];
					if (mgmt_writedata[17] != m[0] || mgmt_writedata[15:8] - mgmt_writedata[7:0] != m[0]) bad = bad + 1;
				end
				5: c = mgmt_writedata[15:8] + mgmt_writedata[7:0];
				7: k = mgmt_writedata;
				8: if (mgmt_writedata != 7) bad = bad + 1;
				9: if (mgmt_writedata != 2) bad = bad + 1;
				default: bad = bad + 1;
			endcase
		end
	end
endmodule

module pll_vid
(
	input         refclk,
	input         rst,
	output reg    outclk_0,
	output        locked,
	input  [63:0] reconfig_to_pll,
	output [63:0] reconfig_from_pll
);
	assign locked = 1;
	assign reconfig_from_pll = 0;
	initial outclk_0 = 0;
	always #(500.0 / $bitstoreal(reconfig_to_pll)) outclk_0 = ~outclk_0;
endmodule

module tb_vclk;

reg clk = 0;
always #10 clk = ~clk;

reg  [7:0] code = 8'd100;
wire       clk_vid, ready;
wire [12:0] count;

hybrid_vclk dut(.clk(clk), .refclk(clk), .code(code), .clk_vid(clk_vid), .ready(ready), .count(count));

// the clock while `ready`
real    t_last = 0, period = 0;
integer errors = 0, ready_bad = 0;
always @(posedge clk_vid) begin
	period = $realtime - t_last;
	t_last = $realtime;
	if (ready && (period < 2000.0 / code - 0.02 || period > 2000.0 / code + 0.02)) begin
		if (ready_bad < 5) $display("FAIL: ready with a period of %0f ns, code %0d", period, code);
		ready_bad = ready_bad + 1;
	end
end

task set_code(input integer c);
	integer n;
	begin
		code = c[7:0];
		@(posedge clk);
		#1 if (ready && c != dut.cfg_code) begin $display("FAIL: ready right after a change"); errors = errors + 1; end
		n = 0;
		while (!ready && n < 5000000) begin @(posedge clk); n = n + 1; end
		repeat (9000) @(posedge clk);
		$display("code %0d: %0f MHz, count %0d, ready after %0d us", c, 1000.0 / period, count, n / 50);
		if (!ready || count < c * 40 - 2 || count > c * 40 + 2) begin $display("FAIL: code %0d", c); errors = errors + 1; end
	end
endtask

integer c;
initial begin
	$display("tb_vclk: start");
	// as built the PLL makes 65MHz, 50MHz is asked for
	#1 if (ready) begin $display("FAIL: ready at the start"); errors = errors + 1; end
	set_code(100);
	for (c = 93; c <= 124; c = c + 1) set_code(c);
	set_code(80);
	set_code(130);
	set_code(100);
	// a PLL that did not take its settings: not ready until it was set again
	dut.pll_cfg.ignore = 1;
	set_code(96);
	if (dut.pll_cfg.writes != 38 * 8) begin $display("FAIL: %0d writes to the PLL", dut.pll_cfg.writes); errors = errors + 1; end
	if (dut.pll_cfg.bad != 0 || ready_bad != 0) errors = errors + 1;
	if (errors != 0) $display("FAIL: %0d errors (%0d bad writes)", errors, dut.pll_cfg.bad);
	else $display("PASS");
	$finish;
end

endmodule
