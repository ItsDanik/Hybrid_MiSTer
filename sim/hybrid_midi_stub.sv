// Stand-ins for the MIDI synthesizer and its PLL in the hybrid_host testbench
// (the real engine has a bench of its own, sim/midi_engine.sh, and is too big
// for Icarus). It reads the commands as the real one does, one at a time
// and in order, each followed by a read of samples, checks the words the
// host hands it (word k of command c is {c, k}, sample word n is
// {~n, n} of 32 bits each), and makes a constant sound while enabled.
`timescale 1ns/1ps

module pll_midi
(
	input  refclk,
	input  rst,
	output reg outclk_0,
	output locked
);
	initial outclk_0 = 0;
	always #5 outclk_0 = ~outclk_0;
	assign locked = 1;
endmodule

module hybrid_midi
(
	input              clk,
	input              mclk,
	input              en,
	input       [31:0] wcount,
	input        [6:0] volume,
	input              frame_stb,
	input       [31:0] frame,

	output             cmd_req,
	output      [13:0] cmd_addr,
	input              cmd_ack,
	output             smp_req,
	output      [22:0] smp_addr,
	output       [3:0] smp_len,
	input              smp_ack,
	input              beat,
	input       [63:0] dout,

	output reg  [15:0] out_l = 0,
	output reg  [15:0] out_r = 0,
	output      [31:0] st_read,
	output      [15:0] st_under,
	output       [7:0] st_voices,
	output     [127:0] st_bits,
	output reg  [31:0] st_frame = 0,

	output     [159:0] dbg_sums,
	output      [31:0] dbg_out,
	output      [31:0] dbg_frame,
	output             dbg_tgl
);

reg  [31:0] rd = 0;          // commands read
reg   [1:0] phase = 0;       // 0 idle, 1 a command's words, 2 the samples asked for, 3 their words
reg   [4:0] cnt = 0;
reg  [22:0] s_addr = 0;
reg   [3:0] s_len = 0;
integer     errors = 0, smp_reads = 0, frame_errors = 0, frames = 0;

assign cmd_req  = en && phase == 0 && rd != wcount;
assign cmd_addr = {rd[9:0], 4'd0};
assign smp_req  = en && phase == 2;
assign smp_addr = s_addr;
assign smp_len  = s_len;

assign st_read   = rd;
assign st_under  = en ? 16'd7 : 16'd0;
assign st_voices = en ? 8'd5 : 8'd0;
assign st_bits   = en ? {64'hFEDC_BA98_7654_3210, 64'h0123_4567_89AB_CDEF} : 128'd0;
assign dbg_sums  = 0;
assign dbg_out   = 0;
assign dbg_frame = 0;
assign dbg_tgl   = 0;

always @(posedge clk) begin
	if (frame_stb) begin
		if (frames != 0 && frame != st_frame + 1) frame_errors = frame_errors + 1;
		frames = frames + 1;
		st_frame <= frame;
		out_l <= en ? 16'h0100 : 16'd0;
		out_r <= en ? 16'hFF00 : 16'd0;
	end
	case (phase)
		0: if (cmd_ack) begin
			if (!cmd_req) errors = errors + 1;
			phase <= 1;
			cnt   <= 0;
		end
		1: if (beat) begin
			if (dout !== {rd, 27'd0, cnt}) begin
				if (errors < 5) $display("FAIL: MIDI command %0d word %0d: %h", rd, cnt, dout);
				errors = errors + 1;
			end
			cnt <= cnt + 1'd1;
			if (cnt == 15) begin
				rd     <= rd + 1'd1;
				phase  <= 2;
				s_addr <= rd[22:0] * 23'd37;
				s_len  <= 4'd1 + rd[2:0];
			end
		end
		2: if (smp_ack) begin
			phase <= 3;
			cnt   <= 0;
			smp_reads = smp_reads + 1;
		end
		3: if (beat) begin
			if (dout !== {~(32'(s_addr) + cnt), 32'(s_addr) + cnt}) begin
				if (errors < 5) $display("FAIL: MIDI sample word %0d: %h", s_addr + cnt, dout);
				errors = errors + 1;
			end
			cnt <= cnt + 1'd1;
			if (cnt + 1'd1 == s_len) phase <= 0;
		end
	endcase
	if (beat && (phase == 0 || phase == 2)) errors = errors + 1;
	if (!en) begin
		rd    <= 0;
		phase <= 0;
	end
end

endmodule
