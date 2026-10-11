`timescale 1ns/1ps
// Not part of run.sh: plays one bright note (strong modulation, full
// feedback) on the chip and writes its samples, one a line, to fm_tone.txt,
// to be compared with a software chip given the same writes
// (tools/fm_tone.c).
module tb_fm_tone;

reg clk = 0;
always #10 clk = ~clk;

reg         en = 0;
reg         wr_valid = 0;
reg   [7:0] wr_reg = 0, wr_data = 0;
wire        wr_take;
wire signed [15:0] snd;

hybrid_fm dut
(
	.clk(clk), .en(en), .volume(7'd32), .frame(32'd0),
	.wr_valid(wr_valid), .wr_frame(32'd0), .wr_reg(wr_reg), .wr_data(wr_data), .wr_take(wr_take),
	.snd(snd)
);

task put(input [7:0] r, input [7:0] d);
	begin
		@(posedge clk);
		wr_reg <= r; wr_data <= d; wr_valid <= 1;
		@(posedge clk);
		while (!wr_take) @(posedge clk);
		wr_valid <= 0;
	end
endtask

integer f, i, n = 0;
reg sample_l = 0;
always @(posedge clk) begin
	sample_l <= dut.chip.sample;
	if (dut.chip.sample && !sample_l && n >= 0) begin
		$fdisplay(f, "%0d", dut.chip.snd);
		n = n + 1;
	end
end

initial begin
	f = $fopen("fm_tone.txt", "w");
	repeat (100) @(posedge clk);
	en = 1;
	put(8'h01, 8'h20);
	for (i = 0; i < 9; i = i + 1) put(8'hC0 + i[7:0], 8'h00);
	put(8'h20, 8'h21);
	put(8'h40, `MOD_LEVEL);
	put(8'h60, 8'hF0);
	put(8'h80, 8'h0F);
	put(8'hE0, 8'h00);
	put(8'hC0, `FEEDBACK);
	put(8'h23, 8'h21);
	put(8'h43, 8'h00);
	put(8'h63, 8'hF0);
	put(8'h83, 8'h0F);
	put(8'hE3, 8'h00);
	put(8'hA0, 8'h98);
	put(8'hB0, 8'h31);
	while (n < 8192) @(posedge clk);
	$fclose(f);
	$finish;
end

endmodule
