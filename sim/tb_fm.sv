`timescale 1ns/1ps
// Testbench for hybrid_fm: writes the classic AdLib test note into the chip
// and looks at what comes out. Checks that a write waits for its audio
// frame, that every write is taken once, that the chip then sounds and that
// it is silent again without its enable.
module tb_fm;

reg clk = 0;
always #10 clk = ~clk;

reg         en = 0;
reg  [31:0] frame = 0;
reg         wr_valid = 0;
reg  [31:0] wr_frame = 0;
reg   [7:0] wr_reg = 0, wr_data = 0;
wire        wr_take;
wire signed [15:0] snd;

hybrid_fm dut
(
	.clk(clk), .en(en), .volume(7'd32), .frame(frame),
	.wr_valid(wr_valid), .wr_frame(wr_frame), .wr_reg(wr_reg), .wr_data(wr_data), .wr_take(wr_take),
	.snd(snd)
);

integer errors = 0, takes = 0, i;
integer hi = 0, lo = 0;
always @(posedge clk) begin
	if (wr_take) takes = takes + 1;
	if (snd > hi) hi = snd;
	if (snd < lo) lo = snd;
end

task put(input [7:0] r, input [7:0] d, input integer at);
	begin
		@(posedge clk);
		wr_reg <= r; wr_data <= d; wr_frame <= at; wr_valid <= 1;
		@(posedge clk);
		while (!wr_take) @(posedge clk);
		wr_valid <= 0;
	end
endtask

initial begin
	repeat (100) @(posedge clk);
	if (snd !== 0) begin $display("FAIL: sound without enable"); errors = errors + 1; end
	en = 1;

	// a write for frame 5 is not made at frame 0 ...
	wr_reg = 8'h20; wr_data = 8'h01; wr_frame = 5; wr_valid = 1;
	repeat (20000) @(posedge clk);
	if (takes != 0) begin $display("FAIL: write made before its frame"); errors = errors + 1; end
	// ... and is made at frame 5
	frame = 5;
	while (!wr_take) @(posedge clk);
	wr_valid = 0;
	if (takes != 0 && takes != 1) begin $display("FAIL: takes %0d", takes); errors = errors + 1; end

	// what every AdLib program does first: the nine channels' feedback and
	// connection registers have no reset of their own
	for (i = 0; i < 9; i = i + 1) put(8'hC0 + i[7:0], 8'h00, 5);

	// the rest of the note, all due (frame 3 is past, and so is 5)
	put(8'h40, 8'h10, 3);
	put(8'h60, 8'hF0, 5);
	put(8'h80, 8'h77, 5);
	put(8'hA0, 8'h98, 5);
	put(8'h23, 8'h01, 5);
	put(8'h43, 8'h00, 5);
	put(8'h63, 8'hF0, 5);
	put(8'h83, 8'h77, 5);
	put(8'hB0, 8'h31, 5);
	repeat (10) @(posedge clk);
	if (takes != 19) begin $display("FAIL: 19 writes, %0d taken", takes); errors = errors + 1; end

	hi = 0; lo = 0;
	repeat (400000) @(posedge clk);      // 8ms
	$display("fm: note on, sound from %0d to %0d", lo, hi);
	if (hi < 1000 || lo > -1000) begin $display("FAIL: the chip does not sound"); errors = errors + 1; end

	en = 0;
	repeat (100) @(posedge clk);
	if (snd !== 0) begin $display("FAIL: sound after the enable went"); errors = errors + 1; end

	if (errors != 0) $display("FAILED: %0d errors", errors);
	else $display("PASS");
	$finish;
end

endmodule
