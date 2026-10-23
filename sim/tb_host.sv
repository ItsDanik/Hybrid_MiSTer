// Testbench for hybrid_host: DDR model with random waitrequest and read
// latency, checks scanout pixels against framebuffer+palette (8bpp) and the
// RGB565 framebuffer in all video modes, sync timings, the CRT options, the
// status block, the audio ring, the FM ring, the MT32-pi (a stand-in for
// the Pi on the user port), the reads for the MIDI synthesizer (with a
// stand-in for it, hybrid_midi_stub.sv) and the info box. All the time it
// watches that no line is shorter than 64us unless vga31 allows it.
`timescale 1ns/1ps

// Stand-in for hybrid_vclk.sv (tb_vclk.sv tests the real one): the clock
// changes some time after another one is asked for and is ready later still.
// As built it is 65MHz.
module hybrid_vclk
(
	input             clk,
	input             refclk,
	input       [7:0] code,
	output reg        clk_vid,
	output            ready,
	output reg [12:0] count
);
	reg [7:0] cur = 8'd130;
	integer   busy = 0;
	initial clk_vid = 0;
	always #(1000.0 / cur) clk_vid = ~clk_vid;
	always @(posedge clk) begin
		if (busy == 0 && code != cur) busy = 3000;
		if (busy != 0) begin
			busy = busy - 1;
			if (busy == 1500) cur = code;
		end
		count <= cur * 40;
	end
	assign ready = (busy == 0) && (cur == code);
endmodule

module tb_host;
// a core without the MIDI synthesizer with -DNO_SYNTH (MIDI_SYNTH 0; run it
// with +trigonly, which checks the status bit that says so)
`ifdef NO_SYNTH
localparam SYNTH = 0;
`else
localparam SYNTH = 1;
`endif

reg clk = 0;
always #10 clk = ~clk; // 50MHz

reg reset = 1;

wire        ddr_busy;
wire  [7:0] ddr_burstcnt;
wire [28:0] ddr_addr;
reg  [63:0] ddr_dout;
reg         ddr_dout_ready;
wire        ddr_rd;
wire [63:0] ddr_din;
wire  [7:0] ddr_be;
wire        ddr_we;

reg  [31:0] joystick_0 = 32'h00000012, joystick_1 = 32'h00000034;
reg  [10:0] ps2_key = 0;
reg  [24:0] ps2_mouse = 0;

wire clk_vid, ce_pix, hsync, vsync, hblank, vblank, hires, fast, f1;
wire [3:0] menumask;
reg  vga31 = 0;
reg  [3:0] trig = 0;      // OSD triggers, status bits 56..59
reg        diag = 0;      // 16: Save Diagnostic Report
reg        osd_open = 0;  // the OSD menu is shown
reg  [5:0] crt_hsize = 0;
reg  [3:0] crt_hpos = 0, crt_vpos = 0;
wire [15:0] audio_l, audio_r;
wire [7:0] r, g, b;
// OSD status 96..103: the MT32-pi's lines (bit 96 set: off)
reg  [7:0] osd_mt32 = 8'b0000_00_1_0;  // the Pi's own SoundFont, MT-32 v1, MT-32 mode, on
reg        notes_trig = 0;              // 104: Reset Hanging Notes
reg        osd_music = 0;               // 87: Music, MIDI / FM chip
// the Pi: SDA (with the core's, open drain), MIDI in, I2S word select, SCL,
// bit clock, data
reg        pi_sda = 1, pi_scl = 1, pi_ws = 0, pi_bclk = 0, pi_dat = 0;
wire [6:0] user_out;
wire [6:0] user_in = {1'b1, pi_dat, pi_bclk, pi_scl, pi_ws, 1'b1, pi_sda & user_out[0]};
wire       mt32_on, info_req;
wire [7:0] info;
reg        clk_audio = 0;
always #20.345 clk_audio = ~clk_audio; // 24.576MHz

hybrid_host #(.WALL("rtl/hybrid_wall.hex"), .WALL_PAL("rtl/hybrid_wall_pal.hex"), .WALL_DOTS("rtl/hybrid_wall_dots.hex"),
	.WALL_EASE("rtl/hybrid_wall_ease.hex"), .WALL_FONT("rtl/hybrid_wall_font.hex"), .WALL_ALONE("rtl/hybrid_wall_alone.hex"),
	.WALL_TIMEOUT(1500), .MT32_QUIET(10_000_000), .MIDI_SYNTH(SYNTH)) dut
(
	.clk(clk), .reset(reset), .refclk(clk),
	.ddr_busy(ddr_busy), .ddr_burstcnt(ddr_burstcnt), .ddr_addr(ddr_addr), .ddr_dout(ddr_dout),
	.ddr_dout_ready(ddr_dout_ready), .ddr_rd(ddr_rd), .ddr_din(ddr_din), .ddr_be(ddr_be), .ddr_we(ddr_we),
	.joystick_0(joystick_0), .joystick_1(joystick_1),
	.joy_l_analog_0(16'h1122), .joy_r_analog_0(16'h3344), .joy_l_analog_1(16'h5566), .joy_r_analog_1(16'h7788),
	.osd_status({23'h091A, notes_trig, osd_mt32, 8'h78, osd_music, 23'h1A_BCDE, 4'h0, trig, 39'h0, diag, 16'hCAFE}), .osd_open(osd_open), .ps2_key(ps2_key), .ps2_mouse(ps2_mouse), .ps2_mouse_ext(16'd0),
	.clk_vid(clk_vid), .ce_pix(ce_pix), .r(r), .g(g), .b(b), .hsync(hsync), .vsync(vsync), .hblank(hblank), .vblank(vblank),
	.vga31(vga31), .crt_hsize(crt_hsize), .crt_hpos(crt_hpos), .crt_vpos(crt_vpos),
	.hires(hires), .fast(fast), .f1(f1), .menumask(menumask),
	.audio_l(audio_l), .audio_r(audio_r),
	.clk_audio(clk_audio), .user_in(user_in), .user_out(user_out), .mt32_on(mt32_on), .info_req(info_req), .info(info)
);

//////////////////////////////////////////////////////////////////
// DDR model. mem index = word address - BASE

localparam [28:0] BASE = 29'h06000000;
reg [63:0] mem[0:524287];

// random waitrequest
reg busy_r = 0;
always @(posedge clk) busy_r <= ($random & 3) == 0;
assign ddr_busy = busy_r;

// read queue: bursts become readable after a random latency
reg [28:0] rq_addr[0:15];
reg  [7:0] rq_len[0:15];
integer    rq_head = 0, rq_tail = 0;
integer    cur_beat = 0;
integer    lat = 0;
integer    reads_issued = 0;
reg [31:0] smp_n;

// write burst tracking
reg        wr_active = 0;
reg [28:0] wr_addr;
integer    wr_left = 0, wr_idx = 0;
integer    stat_writes = 0;

always @(posedge clk) begin
	ddr_dout_ready <= 0;
	if (!ddr_busy) begin
		if (ddr_rd) begin
			rq_addr[rq_tail % 16] = ddr_addr;
			rq_len[rq_tail % 16]  = ddr_burstcnt;
			rq_tail = rq_tail + 1;
			reads_issued = reads_issued + 1;
			if (ddr_burstcnt == 0 || ddr_burstcnt > 200) begin
				$display("FAIL: bad burst count %0d", ddr_burstcnt); $finish;
			end
		end
		if (ddr_we) begin
			if (ddr_rd) begin $display("FAIL: rd and we together"); $finish; end
			if (!wr_active) begin
				wr_active = 1;
				wr_addr = ddr_addr;
				wr_left = ddr_burstcnt;
				wr_idx = 0;
			end
			mem[wr_addr - BASE + wr_idx] = ddr_din;
			wr_idx = wr_idx + 1;
			wr_left = wr_left - 1;
			if (wr_left == 0) begin
				wr_active = 0;
				stat_writes = stat_writes + 1;
			end
		end
	end
	// return read data with latency and random gaps
	if (rq_head != rq_tail) begin
		if (lat < 12) lat = lat + 1;
		else if (($random & 7) != 0) begin
			// the MIDI samples, beyond this memory: word n is {~n, n}
			if (rq_addr[rq_head % 16] - BASE >= 29'h200000) begin
				smp_n = rq_addr[rq_head % 16] - BASE - 29'h200000 + cur_beat;
				ddr_dout <= {~smp_n, smp_n};
			end else
				ddr_dout <= mem[rq_addr[rq_head % 16] - BASE + cur_beat];
			ddr_dout_ready <= 1;
			cur_beat = cur_beat + 1;
			if (cur_beat == rq_len[rq_head % 16]) begin
				cur_beat = 0;
				rq_head = rq_head + 1;
				lat = 0;
			end
		end
	end
end

//////////////////////////////////////////////////////////////////
// Shared memory contents

function [7:0] fb_pixel(input integer fb, input integer x, input integer y);
	fb_pixel = (x * 3 + y * 7 + fb * 50) & 8'hFF;
endfunction

function [15:0] fb_pixel16(input integer x, input integer y);
	fb_pixel16 = (x * 197 + y * 911 + 16'h1234) & 16'hFFFF;
endfunction

function [23:0] rgb565(input [15:0] p);
	rgb565 = {p[15:11], p[15:13], p[10:5], p[10:9], p[4:0], p[4:2]};
endfunction

function [23:0] pal_rgb(input integer slot, input integer i);
	pal_rgb = {i[7:0], ~i[7:0], i[7:0] ^ (slot ? 8'h55 : 8'hAA)};
endfunction

// mode: pixel format, vmode: video mode
integer cur_vmode = 0, cur_mask = 0;
// the FM chip: enable, and the number of entries written to its ring
integer cur_fm = 0, cur_fmw = 0;
// the MIDI synthesizer: enable, volume, commands written; the info box
integer cur_midi = 0, cur_midivol = 32, cur_midiw = 0, cur_info = 0, cur_infocnt = 0;
// where the MIDI goes, the program's choice (MH_MidiDest(), host version 17):
// cur_prog 1 to choose, cur_prog_pi 1 for the MT32-pi
integer cur_prog = 0, cur_prog_pi = 0;
task set_ctrl(input integer fb, input integer mode, input integer slot, input integer seq);
	mem[0] = {cur_mask[3:0], 2'd0, cur_fm[0], 1'b1, 7'd64, slot[0], cur_vmode[3:0], 3'd0, mode[0], fb[7:0], 32'h4259484D};
	mem[1] = {cur_fmw[31:0], seq[31:0]};
	mem[2] = {6'd0, cur_prog_pi[0], cur_prog[0], cur_infocnt[7:0], cur_info[7:0], cur_midi[0], cur_midivol[6:0], cur_midiw[31:0]};
endtask
// command c of the MIDI ring: word k is {c, k}
task midi_cmds(input integer from, input integer to);
	integer c, k;
	for (c = from; c < to; c = c + 1)
		for (k = 0; k < 16; k = k + 1)
			mem[29'h6000 + (c % 1024) * 16 + k] = {c[31:0], k[31:0]};
endtask

// the video modes
function integer mode_w(input integer m);
	mode_w = (m == 0 || m == 3) ? 320 : 640;
endfunction
function integer mode_h(input integer m);
	mode_h = (m == 2) ? 400 : (m == 3 || m == 7) ? 240 : (m == 4) ? 480 : 200;
endfunction
// word index of a framebuffer
function integer fb_word(input integer m, input integer fb);
	fb_word = 29'h20000 * (fb + 1);
endfunction

// framebuffers of a mode: fb0 and fb1 8bpp, fb2 RGB565
task fill(input integer m);
	integer wx, wy, w, h, ff;
	begin
		w = mode_w(m);
		h = mode_h(m);
		for (wy = 0; wy < h; wy = wy + 1)
			for (wx = 0; wx < w; wx = wx + 1) begin
				for (ff = 0; ff < 2; ff = ff + 1)
					mem[fb_word(m, ff) + (wy * w + wx) / 8][((wx % 8) * 8) +: 8] = fb_pixel(ff, wx, wy);
				mem[fb_word(m, 2) + (wy * w + wx) / 4][((wx % 4) * 16) +: 16] = fb_pixel16(wx, wy);
			end
	end
endtask

integer i, x, y, f, s;
initial begin
	for (i = 0; i < 524288; i = i + 1) mem[i] = 0;
	for (s = 0; s < 2; s = s + 1)
		for (i = 0; i < 256; i = i + 2)
			mem[29'h200 + s * 29'h80 + i / 2] = {8'd0, pal_rgb(s, i + 1), 8'd0, pal_rgb(s, i)};
	// audio ring: frame n = {R = ~n, L = n * 3}
	for (i = 0; i < 16384; i = i + 1)
		mem[29'h2000 + i / 2][(i % 2) * 32 +: 32] = {~i[15:0], i[15:0] * 16'd3};
	fill(0);
end

//////////////////////////////////////////////////////////////////
// Output checker

integer cur_fb = 0, cur_mode = 0, cur_slot = 1;
integer px = 0, py = 0;
integer errors = 0, checked = 0;
integer fields_seen = 0;
integer check_enable = 0, check_black = 0, check_wall = 0;
// the picture without a game, on a screen of 640x480: the logo (429x111) in
// the middle, the badge (320x54) 34 pixels from the right edge and from the
// bottom, their pixels one after the other in the file and 16 colours each.
// Its colours are at wall_lvl of 256, the level the core had when the field
// started (the levels themselves are checked where the picture fades), as
// the intro leaves it at its end. The dots under "MiSTer" light up in turn
// all the time: their cells are not checked (wall_skip).
reg  [3:0] wall[0:429 * 111 + 320 * 54 - 1];
reg [23:0] wall_pal[0:31];
reg  [4:0] wall_dots[0:428];
integer wall_lvl = 0;
// the message, when one is shown (tb_msg_on): its 40 words of lines, and the font
reg  [5:0] tb_font[0:95 * 13 - 1];
reg [63:0] tb_msg[0:39];
reg [63:0] tb_alone[0:39];
integer    tb_msg_on = 0;
integer    tb_list = -1;   // a list: its line selected
// the core's mixing, from a to b at level of 256
function [7:0] tb_mix8(input [7:0] a, input [7:0] b, input integer level);
	integer d;
	begin
		d = (b - a) * level;
		tb_mix8 = a + ((d >= 0) ? d / 256 : -((-d + 255) / 256));
	end
endfunction
function [23:0] tb_mix(input [23:0] a, input [23:0] b, input integer level);
	tb_mix = {tb_mix8(a[23:16], b[23:16], level), tb_mix8(a[15:8], b[15:8], level), tb_mix8(a[7:0], b[7:0], level)};
endfunction
initial begin
	$readmemh("rtl/hybrid_wall.hex", wall);
	$readmemh("rtl/hybrid_wall_pal.hex", wall_pal);
	$readmemh("rtl/hybrid_wall_dots.hex", wall_dots);
	$readmemh("rtl/hybrid_wall_font.hex", tb_font);
	$readmemh("rtl/hybrid_wall_alone.hex", tb_alone);
end
// a pixel of the message's text: 4 lines of 71 characters of 6x13, 16 apart,
// the first 16 below the logo, at its left edge
function msg_bit(input integer wx, input integer wy);
	integer tx, ty, c;
	reg [7:0] ch;
	begin
		tx = wx - 105;
		ty = wy - (184 + 111 + 16);
		msg_bit = 0;
		if (tb_msg_on && tx >= 0 && tx < 426 && ty >= 0 && ty < 64 && ty % 16 < 13) begin
			c  = tx / 6;
			ch = tb_msg[(ty / 16) * 10 + c / 8][(c % 8) * 8 +: 8];
			if (ch >= 32 && ch <= 126) msg_bit = tb_font[(ch - 32) * 13 + ty % 16][tx % 6];
		end
	end
endfunction
function wall_skip(input integer wx, input integer wy);
	integer lx, ly;
	begin
		lx = wx - (640 - 429) / 2;
		ly = wy - (480 - 111) / 2;
		wall_skip = lx >= 0 && lx < 429 && ly >= 91 && ly <= 97 && wall_dots[lx] != 31;
	end
endfunction
function [23:0] wall_exp(input integer wx, input integer wy);
	integer lx, ly, bx, by;
	reg [23:0] c;
	begin
		lx = wx - (640 - 429) / 2;
		ly = wy - (480 - 111) / 2;
		bx = wx - (640 - 34 - 320);
		by = wy - (480 - 34 - 54);
		if (msg_bit(wx, wy)) c = (tb_list >= 0 && (wx - 105) / 6 == 70) ? tb_mix(24'h7F30A0, 24'hEFEFEF, 154) : 24'hEFEFEF;
		else if (tb_list >= 0 && wx >= 101 && wx < 101 + 434 && wy >= 311 - 2 + tb_list * 16 && wy < 311 - 2 + tb_list * 16 + 16)
			c = tb_mix(24'h7F30A0, 24'hFF5CB4, 192);
		else if (lx >= 0 && lx < 429 && ly >= 0 && ly < 111) c = wall_pal[wall[ly * 429 + lx]];
		else if (bx >= 0 && bx < 320 && by >= 0 && by < 54) c = wall_pal[16 + wall[429 * 111 + by * 320 + bx]];
		else c = 24'h7F30A0;
		wall_exp[23:16] = c[23:16] * wall_lvl / 256;
		wall_exp[15:8]  = c[15:8] * wall_lvl / 256;
		wall_exp[7:0]   = c[7:0] * wall_lvl / 256;
	end
endfunction
// The intro: the fields of black it still has to wait, then 242 fields to
// its end, counted; then the picture as it stays, all of it checked but the
// dots, at the full level.
// The MT32-pi. Before it answers: not there, and MIDI goes nowhere. Then
// it writes its synth over I2C (SoundFont 3): there. The info box names
// it once MIDI is enabled, once. MIDI messages in the FM ring between FM writes: the
// bytes come out of the UART in order, the last message not before its
// frame, the FM writes made. Its I2S sound is in the mix at the MIDI
// volume, and not with the OSD's MT32-pi off, when nothing is sent.
task test_mt32;
	integer f0, n, aud_errors0, n_reports, n_req;
	aud_errors0 = aud_errors;
	if (mt32_on || mem[9][23]) begin $display("FAIL: an MT32-pi before one answered"); errors = errors + 1; end
	pi_write(8'hA2, 8'h00, 8'h03);
	pi_polling = 1;
	wait_fields(1);
	// the status block is written a little after the field starts
	repeat (5000) @(posedge clk);
	$display("mt32: there %0d, status bit %0d, info boxes %0d", mt32_on, mem[9][23], info_reqs);
	if (!mt32_on || !mem[9][23]) begin $display("FAIL: MT32-pi not seen"); errors = errors + 1; end
	if (info_reqs != 0) begin $display("FAIL: MT32-pi info box without MIDI"); errors = errors + 1; end
	cur_midi = 1;
	set_ctrl(1, 0, 0, 10);
	wait_fields(2);
	pi_stop;
	pi_write(8'hA2, 8'h00, 8'h03);
	pi_polling = 1;
	wait_fields(1);
	$display("mt32: MIDI on, info boxes %0d, last %0d", info_reqs, info_last);
	if (info_reqs != 1 || info_last != 4) begin $display("FAIL: MT32-pi info box"); errors = errors + 1; end

	// the SoundFont: the Pi's own, as it reported it (3); the OSD's 5; the
	// Pi's 44, which the OSD cannot list, kept and named
	pi_stop;
	pi_read(n);
	$display("mt32: SoundFont asked for %0d (the Pi's own)", pi_rd_sf);
	if (pi_rd_sf != 3) begin $display("FAIL: MT32-pi's own SoundFont"); errors = errors + 1; end
	osd_mt32[7:4] = 6;
	repeat (100) @(posedge clk);
	pi_read(n);
	$display("mt32: SoundFont asked for %0d (the OSD's 5)", pi_rd_sf);
	if (pi_rd_sf != 5) begin $display("FAIL: the OSD's SoundFont"); errors = errors + 1; end
	osd_mt32[7:4] = 0;
	n_reports = mem[9][27:24];
	pi_write(8'hA2, 8'h00, 8'd44);
	wait_fields(1);
	pi_read(n);
	$display("mt32: SoundFont asked for %0d (the Pi's 44), info box %0d", pi_rd_sf, info_last);
	if (pi_rd_sf != 44 || info_last != 13) begin $display("FAIL: MT32-pi's SoundFont 44"); errors = errors + 1; end
	pi_write(8'hA2, 8'h00, 8'h03);
	wait_fields(1);
	repeat (5000) @(posedge clk);
	// each report of the Pi counted for the game (status bits 24..27): the
	// two writes above
	$display("mt32: reports %0d, before %0d", mem[9][27:24], n_reports);
	if (mem[9][27:24] != ((n_reports + 2) & 15)) begin $display("FAIL: MT32-pi reports not counted"); errors = errors + 1; end

	// Reset Hanging Notes: the Pi asked for "mute", then the synth again;
	// it stays there all the while, for the OSD and the game
	notes_trig = 1;
	repeat (2000) @(posedge clk);
	notes_trig = 0;
	repeat (2000) @(posedge clk);
	n = mt32_on;
	pi_read(n_req);
	$display("mt32: Reset Hanging Notes, there %0d %0d, request %h", n, mt32_on, n_req[7:0]);
	if (n_req[7:0] != 8'hA0 || !n || !mt32_on || !dut.midi_out) begin $display("FAIL: MT32-pi Reset Hanging Notes"); errors = errors + 1; end
	pi_read(n_req);
	if (n_req[7:0] != 8'hA1) begin $display("FAIL: MT32-pi request %h after Reset Hanging Notes", n_req[7:0]); errors = errors + 1; end
	pi_polling = 1;
	wait_fields(1);

	f0 = dut.aud_play;
	mem[29'h4000] = {6'd0, 2'd3, 8'h40, 8'h3C, 8'h90, 32'd0};          // note on
	mem[29'h4001] = {16'd0, 8'hA0, 8'h55, 32'd0};                      // an FM write
	mem[29'h4002] = {6'd0, 2'd2, 8'h00, 8'h05, 8'hC1, 32'd0};          // program change
	mem[29'h4003] = {6'd0, 2'd1, 8'h00, 8'h00, 8'hF0, 32'd0};          // one byte
	mem[29'h4004] = {6'd0, 2'd3, 8'h00, 8'h3C, 8'h80, f0[31:0] + 32'd8000}; // note off, later
	cur_fmw = 5; cur_fm = 1; cur_midi = 1;
	rx_n = 0;
	set_ctrl(1, 0, 0, 10);
	wait_fields(4);
	$display("mt32: %0d bytes sent, FM writes %0d", rx_n, fm_takes);
	if (rx_n != 6 || rx[0] != 8'h90 || rx[1] != 8'h3C || rx[2] != 8'h40 || rx[3] != 8'hC1 || rx[4] != 8'h05 || rx[5] != 8'hF0) begin
		$display("FAIL: MIDI bytes %h %h %h %h %h %h", rx[0], rx[1], rx[2], rx[3], rx[4], rx[5]); errors = errors + 1;
	end
	if (fm_takes != 4 || fm_last != 16'hA055) begin $display("FAIL: FM write among MIDI"); errors = errors + 1; end
	wait_fields(10);
	$display("mt32: %0d bytes, the note off at frame %0d (asked for %0d)", rx_n, rx_frame, f0 + 8000);
	if (rx_n != 9 || rx[6] != 8'h80 || rx_frame < f0 + 8000 || rx_frame > f0 + 8060) begin
		$display("FAIL: MIDI message for a later frame"); errors = errors + 1;
	end

	// the sound: L 0x1000, R -0x0800, at volume 32 and 64
	pi_left = 16'h1000; pi_right = 16'hF800;
	wait_fields(1);
	if (dut.mt32_snd_l != 19'sh1000 || dut.mt32_snd_r != -19'sh0800) begin
		$display("FAIL: MT32-pi sound %h %h", dut.mt32_snd_l, dut.mt32_snd_r); errors = errors + 1;
	end
	cur_midivol = 64;
	set_ctrl(1, 0, 0, 10);
	wait_fields(2);
	if (dut.mt32_snd_l != 19'sh2000) begin $display("FAIL: MT32-pi volume %h", dut.mt32_snd_l); errors = errors + 1; end

	// the OSD's MT32-pi off: silent, nothing sent, the Pi told to stop
	// (asked for "mute" when it reads)
	pi_stop;
	osd_mt32[0] = 1;
	mem[29'h4005] = {6'd0, 2'd3, 8'h40, 8'h3C, 8'h90, 32'd0};
	cur_fmw = 6;
	set_ctrl(1, 0, 0, 10);
	wait_fields(3);
	pi_read(n);
	$display("mt32: OSD off, %0d bytes, sound %h, request %h", rx_n, dut.mt32_snd_l, n[7:0]);
	if (rx_n != 9 || dut.mt32_snd_l != 0 || n[7:0] != 8'hA0) begin $display("FAIL: MT32-pi with the OSD's off"); errors = errors + 1; end
	pi_read(n);
	if (n[7:0] != 8'hA1) begin $display("FAIL: MT32-pi request %h after the mute", n[7:0]); errors = errors + 1; end
	osd_mt32[0] = 0;
	// the OSD's Music on FM chip (bit 87, host version 16): the same, once
	// the MIDI is out again (a poll of the Pi in between)
	wait_fields(1);
	pi_read(n);
	if (!dut.midi_out) begin $display("FAIL: MT32-pi not back after the OSD's on"); errors = errors + 1; end
	osd_music = 1;
	mem[29'h4006] = {6'd0, 2'd3, 8'h40, 8'h3E, 8'h90, 32'd0};
	cur_fmw = 7;
	set_ctrl(1, 0, 0, 10);
	wait_fields(3);
	pi_read(n);
	$display("mt32: Music on FM chip, %0d bytes, sound %h, request %h", rx_n, dut.mt32_snd_l, n[7:0]);
	if (rx_n != 9 || dut.mt32_snd_l != 0 || n[7:0] != 8'hA0) begin $display("FAIL: MT32-pi with the OSD's Music on FM chip"); errors = errors + 1; end
	pi_read(n);
	if (n[7:0] != 8'hA1) begin $display("FAIL: MT32-pi request %h after the mute", n[7:0]); errors = errors + 1; end
	// the program's choice (MH_MidiDest(), host version 17) over the OSD's
	// Music on FM chip and MT32-pi off: the Pi plays; then the synthesizer
	// chosen, the Pi silent again
	osd_mt32[0] = 1;
	cur_prog = 1; cur_prog_pi = 1;
	set_ctrl(1, 0, 0, 10);
	wait_fields(1);
	pi_read(n);
	if (!dut.midi_out) begin $display("FAIL: MT32-pi not out with the program's choice"); errors = errors + 1; end
	mem[29'h4007] = {6'd0, 2'd3, 8'h40, 8'h3F, 8'h90, 32'd0};
	cur_fmw = 8;
	set_ctrl(1, 0, 0, 10);
	wait_fields(3);
	pi_read(n);
	$display("mt32: the program's choice, the Pi: %0d bytes, %h %h %h", rx_n, rx[9], rx[10], rx[11]);
	if (rx_n != 12 || rx[9] != 8'h90 || rx[10] != 8'h3F || rx[11] != 8'h40) begin
		$display("FAIL: MT32-pi with the program's choice"); errors = errors + 1;
	end
	cur_prog_pi = 0;
	mem[29'h4008] = {6'd0, 2'd3, 8'h40, 8'h41, 8'h90, 32'd0};
	cur_fmw = 9;
	set_ctrl(1, 0, 0, 10);
	wait_fields(3);
	pi_read(n);
	$display("mt32: the program's choice, the synthesizer: %0d bytes, request %h", rx_n, n[7:0]);
	if (rx_n != 12 || dut.midi_out || n[7:0] != 8'hA0) begin $display("FAIL: MT32-pi with the program's choice of the synthesizer"); errors = errors + 1; end
	pi_read(n);
	cur_prog = 0;
	osd_mt32[0] = 0;
	osd_music = 0;
	cur_fm = 0; cur_fmw = 0; cur_midi = 0; cur_midivol = 32;
	set_ctrl(1, 0, 0, 10);
	wait_fields(3);
	// the Pi unplugged: no poll for 200ms. Gone, the music goes nowhere, the
	// module held in reset; back with its next polls
	cur_fm = 1; cur_midi = 1;
	set_ctrl(1, 0, 0, 10);
	wait_fields(14);
	repeat (5000) @(posedge clk);
	$display("mt32: unplugged, there %0d, status bit %0d, available %0d, MIDI out %0d", mt32_on, mem[9][23], dut.mt32_available, dut.midi_out);
	if (mt32_on || mem[9][23] || dut.mt32_available || dut.midi_out) begin $display("FAIL: an MT32-pi that is gone"); errors = errors + 1; end
	pi_polling = 1;
	wait_fields(5);
	repeat (5000) @(posedge clk);
	$display("mt32: plugged in again, there %0d, status bit %0d, MIDI out %0d", mt32_on, mem[9][23], dut.midi_out);
	if (!mt32_on || !mem[9][23] || !dut.midi_out) begin $display("FAIL: MT32-pi plugged in again"); errors = errors + 1; end
	pi_stop;
	cur_fm = 0; cur_midi = 0;
	set_ctrl(1, 0, 0, 10);
	wait_fields(1);
	// the Pi's sound in the mix (the audio checker adds it)
	$display("mt32: %0d audio samples wrong", aud_errors - aud_errors0);
	if (aud_errors != aud_errors0) errors = errors + 1;
endtask

// The MIDI synthesizer's test (the comment where it is called)
task test_midi;
`ifndef NO_SYNTH
	midi_cmds(0, 40);
	cur_midiw = 40;
	set_ctrl(1, 0, 0, 10);
	wait_fields(4);
	if (dut.midi_synth.midi.rd != 0 || dut.midi_en) begin $display("FAIL: MIDI read without the enable"); errors = errors + 1; end
	// w0[63:56] is the MT32-pi's synth (host version 17), written with or
	// without the enable: the rest is 0
	if (mem[29'h1A][55:0] != 0 || mem[29'h1B] != 0) begin $display("FAIL: MIDI status without the enable %h", mem[29'h1A]); errors = errors + 1; end
	if (mem[29'h1A][63:56] != dut.pi_synth) begin $display("FAIL: the MT32-pi's synth in the MIDI status %h, not %h", mem[29'h1A][63:56], dut.pi_synth); errors = errors + 1; end
	cur_midi = 1; cur_midivol = 77;
	set_ctrl(1, 0, 0, 10);
	wait_fields(4);
	$display("midi: %0d commands read, %0d sample reads, status %h %h %h frame %0d (playing %0d), errors %0d, frame errors %0d",
		dut.midi_synth.midi.rd, dut.midi_synth.midi.smp_reads, mem[29'h1A], mem[29'h1B], mem[29'h1C], mem[29'h1D][31:0], dut.aud_play,
		dut.midi_synth.midi.errors, dut.midi_synth.midi.frame_errors);
	if (dut.midi_synth.midi.rd != 40 || dut.midi_synth.midi.smp_reads != 40 || dut.midi_vol != 77
		|| mem[29'h1A] != {dut.pi_synth, 8'd5, 16'd7, 32'd40} || mem[29'h1B] != 64'h0123_4567_89AB_CDEF
		|| mem[29'h1C] != 64'hFEDC_BA98_7654_3210 || mem[29'h1D][31:0] > dut.aud_play + 1 || mem[29'h1D][31:0] + 2000 < dut.aud_play) begin
		$display("FAIL: MIDI reads or status"); errors = errors + 1;
	end
	if (midi_samples == 0) begin $display("FAIL: no MIDI sound in the mix"); errors = errors + 1; end
	midi_cmds(40, 1000);
	cur_midiw = 1000;
	set_ctrl(1, 0, 0, 10);
	wait_fields(4);
	midi_cmds(1000, 1100);
	cur_midiw = 1100;
	set_ctrl(1, 0, 0, 10);
	wait_fields(4);
	$display("midi: %0d commands read across the ring's end, errors %0d", dut.midi_synth.midi.rd, dut.midi_synth.midi.errors);
	if (dut.midi_synth.midi.rd != 1100 || dut.midi_synth.midi.smp_reads != 1100 || mem[29'h1A][31:0] != 1100) begin
		$display("FAIL: MIDI ring's end"); errors = errors + 1;
	end
	cur_midi = 0;
	set_ctrl(1, 0, 0, 10);
	wait_fields(3);
	if (dut.midi_synth.midi.rd != 0 || mem[29'h1A][55:0] != 0 || mem[29'h1B] != 0 || mem[29'h1C] != 0 || dut.midi_l != 0) begin
		$display("FAIL: MIDI after the enable went"); errors = errors + 1;
	end
	if (dut.midi_synth.midi.errors != 0 || dut.midi_synth.midi.frame_errors != 0) begin $display("FAIL: MIDI reads"); errors = errors + 1; end
	cur_midiw = 0;
`endif
endtask


task test_triggers;
begin
	// an OSD trigger: a pulse of a few clocks flips its bit in the status
	trig = 4'b0010;
	repeat (3) @(posedge clk);
	trig = 0;
	wait_fields(2);
	if (mem[12] != {4'h0, 4'b0010, 56'hCAFE}) begin $display("FAIL: OSD trigger %h", mem[12]); errors = errors + 1; end
	trig = 4'b0010;
	repeat (3) @(posedge clk);
	trig = 0;
	wait_fields(2);
	if (mem[12] != 64'hCAFE) begin $display("FAIL: OSD trigger again %h", mem[12]); errors = errors + 1; end
	// Save Diagnostic Report (bit 16): flipped at the pulse's end, also when
	// the bit was 1 before (a saved setting of the old Menu OK)
	diag = 1;
	repeat (3) @(posedge clk);
	diag = 0;
	wait_fields(2);
	if (mem[12] != 64'h1CAFE) begin $display("FAIL: diagnostic trigger %h", mem[12]); errors = errors + 1; end
	diag = 1;
	wait_fields(2);
	if (mem[12] != 64'h1CAFE) begin $display("FAIL: diagnostic trigger flipped while held %h", mem[12]); errors = errors + 1; end
	diag = 0;
	wait_fields(2);
	if (mem[12] != 64'hCAFE) begin $display("FAIL: diagnostic trigger again %h", mem[12]); errors = errors + 1; end
	// status word 1 bit 28: the synthesizer is there
	if (mem[9][28] != (SYNTH != 0)) begin $display("FAIL: synthesizer bit %b, built with %0d", mem[9][28], SYNTH); errors = errors + 1; end
end
endtask

task test_intro;
	integer n, hold;
	begin
		@(negedge vblank);
		repeat (50) @(posedge clk);
		hold = dut.wall_hold;
		n = 0;
		while (!dut.intro_done && n < 400) begin
			wait_fields(1);
			n = n + 1;
		end
		$display("intro: over after %0d fields, %0d of them black at first", n, hold);
		if (n < hold + 242 || n > hold + 244) begin $display("FAIL: intro length"); errors = errors + 1; end
		wait_fields(2);
		check_wall = 1;
		check_enable = 1;
		wait_fields(2);
		check_enable = 0;
		check_wall = 0;
		$display("intro over: %0d errors, level %0d", errors, wall_lvl);
		if (wall_lvl != 256) begin $display("FAIL: intro level"); errors = errors + 1; end
	end
endtask
reg     old_hblank = 1, old_vblank = 1, old_vsync = 0;
integer ce_count = 0, last_hs_ce = 0, hs_period = 0, last_vs_ce = 0, vs_period = 0;
reg     old_hsync = 0;
integer field_rows = 0;
// interlaced: the row of the frame a line of the field is, fields of each kind seen
integer row = 0, even_fields = 0, odd_fields = 0;
integer vs_period_prev = 0;
wire    lace = dut.v_lace;
reg [1:0] fb_at_even = 0;
// sync positions: pixels from the end of a line to its hsync, lines from the
// end of the picture to the vsync
integer hbl_ce = 0, h_front = 0, v_lines = 0, v_front = 0;
// line length in time, and the shortest seen while vga31 was 0
real    hs_time = 0, hs_ns = 0, vga31_time = 0, vs_time = 0, vs_ns = 0, crt_time = -1e9;
integer short_lines = 0, unmasked_lines = 0;

always @(vga31) vga31_time = $realtime;
always @(crt_hpos) crt_time = $realtime;

always @(posedge clk_vid) if (ce_pix) begin
	ce_count = ce_count + 1;
	old_hsync <= hsync;
	if (hsync && !old_hsync) begin
		hs_period = ce_count - last_hs_ce;
		last_hs_ce = ce_count;
		hs_ns = $realtime - hs_time;
		// a 15kHz screen: every line is 64us, whatever happens (the first one
		// after a change between 320 and 640 pixels is 0.08us shorter). One
		// line is shorter when the player moves the picture to the right.
		if (!vga31 && hs_time > vga31_time && hs_ns < 63900 && $realtime - crt_time > 40e6) begin
			if (short_lines < 10) $display("FAIL: line of %0f ns without vga31 at %0t", hs_ns, $time);
			short_lines = short_lines + 1;
		end
		// and `fast` covers every line that is shorter
		if (hs_ns < 60000 && !fast) begin
			if (unmasked_lines < 10) $display("FAIL: line of %0f ns without fast at %0t", hs_ns, $time);
			unmasked_lines = unmasked_lines + 1;
		end
		hs_time = $realtime;
		h_front = ce_count - hbl_ce;
		v_lines = v_lines + 1;
	end
	old_vsync <= vsync;
	if (vsync && !old_vsync) begin
		vs_period_prev = vs_period;
		vs_period = ce_count - last_vs_ce;
		last_vs_ce = ce_count;
		vs_ns = $realtime - vs_time;
		vs_time = $realtime;
		v_front = v_lines;
	end

	old_hblank <= hblank;
	old_vblank <= vblank;
	if (!vblank && old_vblank) begin
		py = 0;
		wall_lvl = dut.wall_level;
	end
	if (!hblank && old_hblank) px = 0;
	if (hblank && !old_hblank) hbl_ce = ce_count;

	if (!hblank && !vblank) begin
		if (check_enable) begin : chk
			reg [23:0] exp;
			row = lace ? py * 2 + (f1 ? 1 : 0) : py;
			exp = check_black ? 24'd0 : check_wall ? (wall_skip(px, row) ? {r, g, b} : wall_exp(px, row)) : cur_mode ? rgb565(fb_pixel16(px, row)) : pal_rgb(cur_slot, fb_pixel(cur_fb, px, row));
			if ({r, g, b} !== exp) begin
				if (errors < 10) $display("FAIL: fb %0d px %0d row %0d got %h exp %h", cur_fb, px, row, {r, g, b}, exp);
				errors = errors + 1;
			end
			checked = checked + 1;
		end
		px = px + 1;
	end
	if (hblank && !old_hblank && !vblank) py = py + 1;
	if (vblank && !old_vblank) begin
		fields_seen = fields_seen + 1;
		field_rows = py;
		v_lines = 0;
		if (lace && !f1) even_fields = even_fields + 1;
		if (lace && f1) odd_fields = odd_fields + 1;
	end
end

task wait_fields(input integer n);
	integer target;
	begin
		target = fields_seen + n;
		while (fields_seen < target) @(posedge clk);
	end
endtask

// A video mode in both pixel formats: the picture, its timing in pixels and
// the status block
task test_mode(input integer m, input integer v31, input integer rows, input integer hs, input integer vs, input integer is_lace);
	begin
		fill(m);
		vga31 = v31[0];
		cur_vmode = m;
		set_ctrl(0, 0, 0, 10);
		wait_fields(4);
		cur_fb = 0; cur_mode = 0; cur_slot = 0;
		check_enable = 1;
		wait_fields(is_lace ? 4 : 2);
		check_enable = 0;
		$display("mode %0d (%0dx%0d%s) vga31 %0d: checked %0d pixels, %0d errors, rows/field %0d, hsync period %0d, vsync period %0d, line %0f us, %0f Hz",
			m, mode_w(m), mode_h(m), is_lace ? "i" : "", v31, checked, errors, field_rows, hs_period, vs_period, hs_ns / 1000, 1e9 / vs_ns);
		if (field_rows != rows || hs_period != hs || vs_period != vs || (is_lace && vs_period_prev != vs)) begin
			$display("FAIL: mode %0d timing", m); $finish;
		end
		if (mem[9][15:12] != m || mem[9][9] != is_lace[0] || mem[9][16] || !mem[9][17] || mem[9][18] != fast) begin
			$display("FAIL: mode %0d status %h", m, mem[9][31:0]); errors = errors + 1;
		end
		if (hires != (is_lace != 0 || rows > 240) || fast != (rows > 240)) begin
			$display("FAIL: mode %0d hires/fast", m); errors = errors + 1;
		end
		set_ctrl(2, 1, 0, 10);
		wait_fields(is_lace ? 4 : 2);
		cur_fb = 2; cur_mode = 1;
		check_enable = 1;
		wait_fields(is_lace ? 4 : 2);
		check_enable = 0;
		$display("mode %0d rgb565: checked %0d pixels, %0d errors", m, checked, errors);
	end
endtask

initial begin
	$display("tb_host: start");
	// a control block that a game left behind: the core clears its magic
	// when it starts and shows its picture
	set_ctrl(0, 0, 0, 0);
	repeat (20) @(posedge clk);
	reset = 0;

	wait_fields(1);
	if (dut.ctrl_valid) begin $display("FAIL: ctrl valid without magic"); $finish; end
	// 640x480, interlaced: both kinds of field
	even_fields = 0; odd_fields = 0;
	check_wall = 1;
	check_enable = 1;
	wait_fields(2);
	check_enable = 0;
	check_wall = 0;
	$display("no game: checked %0d pixels, %0d errors, rows/field %0d, hsync period %0d", checked, errors, field_rows, hs_period);
	if (checked != 2 * 153600 || field_rows != 240 || hs_period != 800 || !lace || even_fields != 1 || odd_fields != 1 || mem[9][15:12] != 4) begin
		$display("FAIL: picture without a game"); $finish;
	end
	if (mem[0][31:0] != 0) begin $display("FAIL: magic of an earlier game not cleared: %h", mem[0]); errors = errors + 1; end
	while (dut.ctrl_clear != 0) wait_fields(1);
	wait_fields(1);
	// +mt32only: the MT32-pi test and the MIDI synthesizer's alone, without the rest (it is near the end
	// and the whole run takes long). fm_takes as the FM test leaves it.
	// +trigonly: the OSD triggers' toggles alone
	if ($test$plusargs("trigonly")) begin
		wait_fields(2);
		test_triggers;
		if (errors != 0) $display("FAIL: %0d errors", errors);
		else $display("PASS");
		$finish;
	end
	if ($test$plusargs("mt32only")) begin
		fm_takes = 3;
		test_mt32;
		test_midi;
		if (errors != 0) $display("FAIL: %0d errors", errors);
		else $display("PASS");
		$finish;
	end
	// black for 16 fields in all, then it fades in
	if (wall_lvl != 0) begin $display("FAIL: picture not black at first"); errors = errors + 1; end
	test_intro;

	// mode 0 from framebuffer index 0. The picture fades out first, in 43
	// fields and still in 640x480; the game gets the screen after a black one.
	begin : fade_out
		integer n, bad;
		bad = 0;
		n = 0;
		check_wall = 1;
		check_enable = 1;
		// in the middle of a field: the next vblank reads it
		@(negedge vblank);
		repeat (50) @(posedge clk);
		set_ctrl(0, 0, 1, 7);
		wait_fields(1);
		repeat (5000) @(posedge clk);
		while (!dut.game_on && n < 60) begin
			@(negedge vblank);
			repeat (50) @(posedge clk);
			n = n + 1;
			if (wall_lvl != ((n > 42) ? 0 : 256 - 6 * n)) bad = bad + 1;
			if (!mem[9][10] || mem[9][19] || mem[9][15:12] != 4 || field_rows != 240 || !lace) bad = bad + 1;
			wait_fields(1);
			// the control block is read early in the vblank
			repeat (5000) @(posedge clk);
		end
		check_enable = 0;
		check_wall = 0;
		$display("fade out: %0d errors, %0d fields, %0d of them wrong", errors, n, bad);
		if (n != 43 || bad != 0 || wall_lvl != 0) begin $display("FAIL: fade out"); errors = errors + 1; end
	end
	cur_fb = 0; cur_mode = 0; cur_slot = 1;
	// key press and joystick for the status block
	ps2_key = {~ps2_key[10], 1'b1, 1'b1, 8'h75}; // extended up arrow pressed
	wait_fields(2);
	if (!mem[9][19]) begin $display("FAIL: status without the game bit"); errors = errors + 1; end
	check_enable = 1;
	wait_fields(2);
	check_enable = 0;
	$display("video: checked %0d pixels, %0d errors, rows/field %0d, hsync period %0d, vsync period %0d",
		checked, errors, field_rows, hs_period, vs_period);
	if (field_rows != 200 || hs_period != 400 || vs_period != 400 * 262) begin $display("FAIL: video timing"); $finish; end

	// status block
	if (mem[8][31:0] != 32'h5359484D) begin $display("FAIL: status magic %h", mem[8]); $finish; end
	if (mem[10] != {32'h34, 32'h12}) begin $display("FAIL: status joystick %h", mem[10]); $finish; end
	if (mem[11] != 64'h7788556633441122) begin $display("FAIL: status analog %h", mem[11]); $finish; end
	if (mem[12] != 64'hCAFE) begin $display("FAIL: status osd %h", mem[12]); $finish; end
	// OSD status [127:64] at 0xC8, next to the audio fetch pointer (checked below)
	if (mem[25] != 64'h0012_3402_781A_BCDE) begin $display("FAIL: status osd high %h", mem[25]); $finish; end
	test_triggers;
	// the OSD menu opened and closed again: bit 22 of the status word 1
	if (mem[9][22]) begin $display("FAIL: OSD open without the OSD"); errors = errors + 1; end
	osd_open = 1;
	wait_fields(2);
	if (!mem[9][22]) begin $display("FAIL: OSD open not in the status"); errors = errors + 1; end
	osd_open = 0;
	wait_fields(2);
	if (mem[9][22]) begin $display("FAIL: OSD still open in the status"); errors = errors + 1; end
	if (!mem[8 + 8 + (9'h175 / 64)][9'h175 % 64]) begin $display("FAIL: key bitmap"); $finish; end
	$display("status: frame counter %0d, writes %0d", mem[8][63:32], stat_writes);

	// mouse: 3 to the right and 2 up, then 5 to the left and 4 down
	ps2_mouse = {~ps2_mouse[24], 8'd2, 8'd3, 8'h08};
	repeat (4) @(posedge clk);
	ps2_mouse = {~ps2_mouse[24], 8'hFC, 8'hFB, 8'h38};
	wait_fields(2);
	if (mem[13] != {32'hFFFFFFFE, 32'hFFFFFFFE}) begin $display("FAIL: status mouse %h", mem[13]); errors = errors + 1; end

	// page flip + palette slot change
	set_ctrl(1, 0, 0, 8);
	wait_fields(2);
	cur_fb = 1; cur_slot = 0;
	check_enable = 1;
	wait_fields(2);
	check_enable = 0;
	$display("flip: checked %0d pixels, %0d errors", checked, errors);

	// third framebuffer in RGB565
	set_ctrl(2, 1, 1, 9);
	wait_fields(2);
	cur_fb = 2; cur_slot = 1; cur_mode = 1;
	check_enable = 1;
	wait_fields(2);
	check_enable = 0;
	$display("rgb565: checked %0d pixels, %0d errors", checked, errors);
	if (!mem[9][8]) begin $display("FAIL: status format bit"); errors = errors + 1; end

	// and back to 8bpp
	set_ctrl(0, 0, 0, 10);
	wait_fields(2);
	cur_fb = 0; cur_slot = 0; cur_mode = 0;
	check_enable = 1;
	wait_fields(2);
	check_enable = 0;
	$display("8bpp again: checked %0d pixels, %0d errors", checked, errors);

	// 640x200
	test_mode(1, 0, 200, 800, 800 * 262, 0);

	// 640x400 interlaced (a 15kHz screen): two fields of 262.5 lines, the
	// even rows of the frame in one and the odd rows in the other
	cur_mask = 5;
	test_mode(2, 0, 200, 800, 800 * 525 / 2, 1);
	if (even_fields < 3 || odd_fields < 3) begin $display("FAIL: 640x400i fields"); errors = errors + 1; end
	// a new frame is only taken for the field with the even rows: never two
	// frames in the fields of one picture
	begin : lace_flip
		integer n, bad;
		bad = 0;
		for (n = 0; n < 6; n = n + 1) begin
			set_ctrl(n % 3, 1, 0, 10);
			wait_fields(1);
			@(negedge vblank);
			if (f1 == 1 && dut.fb_index != fb_at_even) bad = bad + 1;
			if (f1 == 0) fb_at_even = dut.fb_index;
		end
		if (bad != 0) begin $display("FAIL: frame changed between the fields of a picture"); errors = errors + 1; end
	end

	// the same at 31kHz: VGA timing, 400 lines
	test_mode(2, 1, 400, 800, 800 * 525, 0);
	// 320x240, 640x480 interlaced and at 31kHz
	test_mode(3, 0, 240, 400, 400 * 262, 0);
	// 640x240
	test_mode(7, 0, 240, 800, 800 * 262, 0);
	test_mode(4, 0, 240, 800, 800 * 525 / 2, 1);
	test_mode(4, 1, 480, 800, 800 * 525, 0);
	// 800x600 and 1024x768 (up to host version 12) are modes the core does
	// not have, with vga31 as well: 320x200
	for (i = 5; i <= 6; i = i + 1) begin
		cur_vmode = i;
		set_ctrl(2, 1, 0, 10);
		wait_fields(4);
		$display("mode %0d vga31 1: hsync period %0d, mode shown %0d, status vga31 %0d", i, hs_period, mem[9][15:12], mem[9][16]);
		if (hs_period != 400 || mem[9][15:12] != 0 || mem[9][16] || fast || hires) begin $display("FAIL: mode %0d", i); errors = errors + 1; end
	end
	// 640x480 turns interlaced when vga31 goes away
	cur_vmode = 4;
	fill(4);
	set_ctrl(2, 1, 0, 10);
	wait_fields(4);
	repeat (123456) @(posedge clk);
	vga31 = 0;
	wait_fields(5);
	check_enable = 1;
	wait_fields(4);
	check_enable = 0;
	$display("640x480 to interlaced: checked %0d pixels, %0d errors", checked, errors);
	if (!lace || fast || vs_period != 800 * 525 / 2) begin $display("FAIL: 640x480 to interlaced"); errors = errors + 1; end

	// a mode the core does not have is 320x200
	cur_vmode = 8;
	set_ctrl(2, 1, 0, 10);
	wait_fields(4);
	if (hs_period != 400 || mem[9][15:12] != 0) begin $display("FAIL: unknown mode"); errors = errors + 1; end

	// back to 320x200
	fill(0);
	cur_vmode = 0;
	set_ctrl(1, 0, 0, 10);
	wait_fields(3);
	cur_fb = 1; cur_mode = 0;
	check_enable = 1;
	wait_fields(2);
	check_enable = 0;
	$display("320x200 again: checked %0d pixels, %0d errors, rows/field %0d, hsync period %0d, vsync period %0d, front porches %0d %0d",
		checked, errors, field_rows, hs_period, vs_period, h_front, v_front);
	if (field_rows != 200 || hs_period != 400 || vs_period != 400 * 262 || h_front != 16 || v_front != 28) begin
		$display("FAIL: video timing"); $finish;
	end

	// CRT options. Horizontal size: 1% fewer pixels in a line and a clock 1%
	// slower per step, the line stays 64us; positions move the sync pulses.
	begin : crt
		integer n, sz, hp, vp, exp_fp, exp_max;
		for (n = 0; n < 8; n = n + 1) begin
			sz = (n == 0) ? 7 : (n == 1) ? -24 : (n == 2) ? 3 : 0;
			hp = (n == 3) ? 7 : (n == 4) ? -8 : (n == 2) ? -2 : 0;
			vp = (n == 5) ? 7 : (n == 6) ? -8 : (n == 2) ? 4 : 0;
			if (n == 0) hp = 5;
			crt_hsize = sz[5:0]; crt_hpos = hp[3:0]; crt_vpos = vp[3:0];
			// a size the option does not have is the nearest it has
			if (n == 7) begin crt_hsize = -6'sd32; sz = -24; end
			wait_fields(4);
			check_enable = 1;
			wait_fields(2);
			check_enable = 0;
			exp_fp = 16 - 2 * sz - 2 * hp;
			exp_max = 44 - 4 * sz;
			if (exp_fp < 2) exp_fp = 2;
			if (exp_fp > exp_max) exp_fp = exp_max;
			$display("crt size %0d pos %0d %0d: %0d errors, hsync period %0d, line %0f us, clock code %0d, front porches %0d %0d",
				sz, hp, vp, errors, hs_period, hs_ns / 1000, mem[15][7:0], h_front, v_front);
			if (hs_period != 400 - 4 * sz || mem[15][7:0] != 100 - sz || h_front != exp_fp || v_front != 28 - vp
				|| vs_period != hs_period * 262 || field_rows != 200) begin
				$display("FAIL: CRT options"); errors = errors + 1;
			end
		end
		// 240 lines: the sync stays in the 22 blank lines. 640 wide: twice the pixels
		crt_hsize = 6'd2; crt_hpos = -4'sd3; crt_vpos = 4'd7;
		cur_vmode = 3;
		set_ctrl(2, 1, 0, 10);
		wait_fields(6);
		$display("crt 320x240: hsync period %0d, front porches %0d %0d", hs_period, h_front, v_front);
		if (hs_period != 392 || h_front != 18 || v_front != 1) begin $display("FAIL: CRT options, 320x240"); errors = errors + 1; end
		cur_vmode = 1;
		set_ctrl(2, 1, 0, 10);
		wait_fields(4);
		$display("crt 640x200: hsync period %0d, front porches %0d %0d", hs_period, h_front, v_front);
		if (hs_period != 784 || h_front != 36 || v_front != 21) begin $display("FAIL: CRT options, 640x200"); errors = errors + 1; end
		cur_vmode = 4;
		set_ctrl(2, 1, 0, 10);
		// not at 31kHz
		vga31 = 1;
		wait_fields(4);
		if (hs_period != 800 || h_front != 16 || mem[15][7:0] != 100) begin $display("FAIL: CRT options at 31kHz"); errors = errors + 1; end
		vga31 = 0;
		crt_hsize = 0; crt_hpos = 0; crt_vpos = 0;
		cur_vmode = 0;
		set_ctrl(1, 0, 0, 10);
		wait_fields(4);
	end

	if (menumask != 4'b0101) begin $display("FAIL: menu mask"); errors = errors + 1; end
	if (short_lines != 0 || unmasked_lines != 0) errors = errors + 1;

	$display("audio: %0d samples, %0d errors, rate %0d Hz, fetch pointer %0d",
		aud_samples, aud_errors,
		(aud_samples - 1) * 64'd1000000000 / (aud_last_tick_time - aud_first_tick_time) , mem[24][31:0]);
	if (aud_samples < 1000 || aud_errors != 0) errors = errors + 1;
	if (mem[24][31:0] < aud_samples || mem[24][31:0] > aud_samples + 200) begin
		$display("FAIL: audio fetch pointer"); errors = errors + 1;
	end

	// The FM chip: three register writes in its ring (none of them makes a
	// sound, the audio is still being compared with the ring), the last one
	// for an audio frame a little ahead. All three are read, in order, the
	// last one made only when its frame is played; without the enable
	// nothing is read.
	begin : fm_test
		integer f0;
		fm_takes = 0;
		f0 = dut.aud_play;
		mem[29'h4000] = {16'd0, 8'hC0, 8'h00, 32'd0};
		mem[29'h4001] = {16'd0, 8'h20, 8'h01, 32'd0};
		mem[29'h4002] = {16'd0, 8'h40, 8'h3F, f0[31:0] + 32'd6000};
		cur_fmw = 3;
		set_ctrl(1, 0, 0, 10);
		wait_fields(4);
		if (fm_takes != 0 || dut.fm_fetch != 0) begin $display("FAIL: FM ring read without the enable"); errors = errors + 1; end
		cur_fm = 1;
		set_ctrl(1, 0, 0, 10);
		wait_fields(4);
		if (fm_takes != 2 || fm_last != 16'h2001) begin $display("FAIL: FM writes, %0d made, last %h", fm_takes, fm_last); errors = errors + 1; end
		wait_fields(8);
		$display("fm: %0d writes made, last %h at frame %0d (asked for %0d), read pointer %0d", fm_takes, fm_last, fm_last_frame, f0 + 6000, mem[15][63:32]);
		if (fm_takes != 3 || fm_last != 16'h403F || fm_last_frame < f0 + 6000 || fm_last_frame > f0 + 6010 || mem[15][63:32] != 3) begin
			$display("FAIL: FM write for a later frame"); errors = errors + 1;
		end
		cur_fm = 0; cur_fmw = 0;
		set_ctrl(1, 0, 0, 10);
		wait_fields(3);
		if (dut.fm_fetch != 0) begin $display("FAIL: FM ring after the enable went"); errors = errors + 1; end
	end

	test_mt32;
	// The MIDI synthesizer: nothing read without the enable; with it, every
	// command in order (also across the ring's end), each followed by the
	// samples it asks for; its status in the status block, its sound in the
	// mix (the audio checker adds it). Without the enable again its count
	// starts anew.
	test_midi;

	// The info box: asked for while the OSD menu is open, shown once it has
	// closed, once; the same count again or a text of 0 shows nothing
	begin : info_test
		info_reqs = 0;
		osd_open = 1;
		cur_info = 3; cur_infocnt = 1;
		set_ctrl(1, 0, 0, 10);
		wait_fields(3);
		if (info_reqs != 0) begin $display("FAIL: info box while the OSD menu is open"); errors = errors + 1; end
		osd_open = 0;
		wait_fields(2);
		if (info_reqs != 1 || info_last != 3) begin $display("FAIL: info box, %0d shown, text %0d", info_reqs, info_last); errors = errors + 1; end
		cur_info = 0; cur_infocnt = 2;
		set_ctrl(1, 0, 0, 10);
		wait_fields(3);
		cur_info = 5; cur_infocnt = 2;
		set_ctrl(1, 0, 0, 10);
		wait_fields(3);
		if (info_reqs != 1) begin $display("FAIL: info box without a new count, or of text 0"); errors = errors + 1; end
		cur_info = 5; cur_infocnt = 3;
		set_ctrl(1, 0, 0, 10);
		wait_fields(3);
		$display("info: %0d boxes, last text %0d", info_reqs, info_last);
		if (info_reqs != 2 || info_last != 5) begin $display("FAIL: info box"); errors = errors + 1; end
	end

	// the game leaves in 640x400 interlaced: back to 640x480 and the core's
	// picture. The CRT's horizontal size is the game's only: the core's
	// picture keeps size 0, whatever the option says
	crt_hsize = -6'sd9;
	cur_vmode = 2;
	fill(2);
	set_ctrl(0, 0, 0, 10);
	wait_fields(6);
	if (!lace) begin $display("FAIL: 640x400 before the game leaves"); errors = errors + 1; end
	if (hs_period != 872 || mem[15][7:0] != 109) begin $display("FAIL: horizontal size for the game"); errors = errors + 1; end
	mem[0][31:0] = 0;
	wait_fields(4);
	checked = 0;
	check_wall = 1;
	check_enable = 1;
	wait_fields(2);
	check_enable = 0;
	check_wall = 0;
	$display("game left: checked %0d pixels, %0d errors, rows/field %0d, hsync period %0d", checked, errors, field_rows, hs_period);
	if (checked != 2 * 153600 || !lace || !hires || field_rows != 240 || hs_period != 800 || mem[15][7:0] != 100
		|| mem[9][15:12] != 4 || mem[9][10] || mem[9][19]) begin
		$display("FAIL: picture after the game left"); errors = errors + 1;
	end
	// it starts from black again, and stays black for 3 seconds (180 fields)
	// this time
	if (wall_lvl != 0 || dut.wall_hold < 170) begin $display("FAIL: picture not black after the game left"); errors = errors + 1; end
	// (not all of the 3 seconds: the simulation is slow)
	dut.wall_hold = 4;
	test_intro;
	// with vga31 the picture is progressive 640x480 at 31kHz
	vga31 = 1;
	wait_fields(5);
	check_wall = 1;
	check_enable = 1;
	wait_fields(2);
	check_enable = 0;
	check_wall = 0;
	$display("no game, vga31 1: checked %0d pixels, %0d errors, rows/field %0d, hsync period %0d", checked, errors, field_rows, hs_period);
	if (field_rows != 480 || hs_period != 800 || lace || !fast || mem[9][15:12] != 4) begin $display("FAIL: picture at 31kHz"); errors = errors + 1; end
	vga31 = 0;
	wait_fields(4);
	if (field_rows != 240 || hs_period != 800 || !lace || mem[9][15:12] != 4 || mem[15][7:0] != 100) begin
		$display("FAIL: 640x480 interlaced without a game"); errors = errors + 1;
	end
	crt_hsize = 0;

	// A game that attaches while the intro runs waits for its end, then the
	// picture goes out in 43 fields and the game has the screen: from field
	// 220 of the intro, 22 + 43 fields
	begin : intro_wait
		integer n;
		@(negedge vblank);
		repeat (50) @(posedge clk);
		dut.intro_t = 220;
		cur_vmode = 0;
		fill(0);
		set_ctrl(0, 0, 0, 10);
		wait_fields(10);
		if (dut.game_on || wall_lvl != 256) begin $display("FAIL: the game did not wait for the intro"); errors = errors + 1; end
		n = 10;
		while (!dut.game_on && n < 200) begin
			wait_fields(1);
			n = n + 1;
		end
		$display("game during the intro: the screen after %0d fields", n);
		if (n < 64 || n > 68) begin $display("FAIL: game during the intro"); errors = errors + 1; end
	end

	// A message: the game has the screen, a program puts a message up and
	// takes the screen from it (mh_message): no black, the picture as at the
	// end of its intro fading in, the text under the logo, the dots standing
	// still for an error. Then another message, then none.
	begin : message
		integer n, l, c;
		reg [8*80-1:0] line;
		for (n = 0; n < 40; n = n + 1) tb_msg[n] = 0;
		for (l = 0; l < 4; l = l + 1) begin
			line = (l == 0) ? "Wolfenstein 3D: the game's data files were not found."
			     : (l == 1) ? "Copy VSWAP.WL6 and the other *.WL6 files to games/dnk/ECWolf/, then"
			     : (l == 2) ? "~ ! @ # $ % ^ & * ( ) _ + { } | : < > ? 0123456789 abcdefghijklmnopqrstu"
			     : "start the game again.";
			// the string is right-aligned in `line`: its characters from the left
			c = 79;
			while (c >= 0 && line[8 * c +: 8] == 0) c = c - 1;
			for (n = 0; n <= c; n = n + 1) tb_msg[l * 10 + n / 8][(n % 8) * 8 +: 8] = line[8 * (c - n) +: 8];
		end
		for (n = 0; n < 40; n = n + 1) mem[29'h301 + n] = tb_msg[n];
		mem[29'h300] = {15'd0, 1'b1, 16'd7, 32'h534D484D};
		mem[0][31:0] = 0;
		n = 0;
		while (!(mem[9][20] && wall_lvl == 256) && n < 100) begin
			wait_fields(1);
			n = n + 1;
		end
		$display("message: shown at the full level after %0d fields, hold %0d, intro field %0d, dots %0d", n, dut.wall_hold, dut.intro_t, dut.a_dots);
		if (n < 40 || n > 50 || dut.a_dots) begin $display("FAIL: message picture"); errors = errors + 1; end
		tb_msg_on = 1;
		wait_fields(2);
		checked = 0;
		check_wall = 1;
		check_enable = 1;
		wait_fields(2);
		check_enable = 0;
		check_wall = 0;
		$display("message: checked %0d pixels, %0d errors", checked, errors);
		// another one: read again for its sequence number
		tb_msg[0][7:0] = "X";
		mem[29'h301] = tb_msg[0];
		mem[29'h300] = {15'd0, 1'b0, 16'd8, 32'h534D484D};
		wait_fields(3);
		check_wall = 1;
		check_enable = 1;
		wait_fields(2);
		check_enable = 0;
		check_wall = 0;
		$display("message changed: %0d errors, dots %0d", errors, dut.a_dots);
		if (!dut.a_dots) begin $display("FAIL: dots of a message that is no error"); errors = errors + 1; end
		// a list: line 2 selected, a mark that there is more in the last column of line 0
		tb_msg[8][55:48] = "^";
		mem[29'h301 + 8] = tb_msg[8];
		mem[29'h300] = {12'd0, 2'd2, 1'b1, 1'b0, 16'd9, 32'h534D484D};
		tb_list = 2;
		wait_fields(3);
		check_wall = 1;
		check_enable = 1;
		wait_fields(2);
		check_enable = 0;
		check_wall = 0;
		$display("list: %0d errors, dots %0d", errors, dut.a_dots);
		if (dut.a_dots) begin $display("FAIL: dots of a list"); errors = errors + 1; end
		tb_list = -1;
		// none
		mem[29'h300] = 0;
		tb_msg_on = 0;
		wait_fields(3);
		if (mem[9][20]) begin $display("FAIL: message still shown"); errors = errors + 1; end
		check_wall = 1;
		check_enable = 1;
		wait_fields(2);
		check_enable = 0;
		check_wall = 0;
		$display("message gone: %0d errors", errors);
	end

	// The core's own message, when no game came in WALL_TIMEOUT fields after
	// it started (not after a reset of the core: the counting is shortened)
	begin : alone
		integer n;
		reset = 1;
		repeat (20) @(posedge clk);
		reset = 0;
		wait_fields(2);
		dut.alone_t = 1495;
		n = 0;
		while (!mem[9][21] && n < 20) begin
			wait_fields(1);
			n = n + 1;
		end
		if (!mem[9][21] || mem[9][20]) begin $display("FAIL: the core's own message"); errors = errors + 1; end
		@(negedge vblank);
		repeat (50) @(posedge clk);
		dut.wall_hold = 0;
		dut.intro_on = 1;
		dut.intro_t = 242;
		dut.wall_level = 256;
		for (n = 0; n < 40; n = n + 1) tb_msg[n] = tb_alone[n];
		tb_msg_on = 1;
		wait_fields(3);
		check_wall = 1;
		check_enable = 1;
		wait_fields(2);
		check_enable = 0;
		check_wall = 0;
		$display("no game came: the core's message, %0d errors, dots %0d", errors, dut.a_dots);
		if (dut.a_dots) begin $display("FAIL: dots with the core's message"); errors = errors + 1; end
		// a game that comes after all has the screen
		tb_msg_on = 0;
		cur_vmode = 0;
		set_ctrl(0, 0, 0, 10);
		wait_fields(60);
		if (!dut.game_on || mem[9][21]) begin $display("FAIL: a game after the core's message"); errors = errors + 1; end
	end
	if (short_lines != 0 || unmasked_lines != 0) errors = errors + 1;
	// the audio is checked to the end, also after its summary above
	if (aud_errors != 0) begin $display("FAIL: %0d audio samples wrong in all", aud_errors); errors = errors + 1; end
	$display("video lines late (status w6): %0d", dut.late_lines);

	if (errors != 0) $display("FAIL: %0d errors", errors);
	else $display("PASS");
	$finish;
end

//////////////////////////////////////////////////////////////////
// Audio checker: every output sample must be the next ring frame

integer aud_next = -1, aud_samples = 0, aud_errors = 0, midi_samples = 0;
// the MT32-pi's sound and the MIDI synthesizer's are mixed in; not checked
// on the sample where the Pi's level changes
integer aud_exp_l, aud_exp_r;
reg [15:0] aud_raw_l, aud_raw_r;
reg signed [18:0] aud_pi_l = 0, aud_pi_r = 0;
time    aud_first_tick_time = 0, aud_last_tick_time = 0;
reg     aud_check = 0;
reg [15:0] aud_start;
// Audio off (a game left): the core starts the ring from its beginning when
// the next one switches it on, the checker anew
always @(posedge clk) if (!dut.audio_en) aud_next = -1;
always @(posedge clk) if (dut.aud_tick2 && dut.audio_en) begin
	// outputs update on the next edge, look at them just after it (the
	// MIDI synthesizer's sample, from this edge on, is the one mixed in)
	@(posedge clk);
	#1;
	if (audio_l != 0 || audio_r != 0 || aud_next >= 0) begin
		if (aud_next < 0) begin
			// the game's sample, without the Pi's and the synthesizer's
			// sound (either may be on already)
			aud_start = audio_l - dut.mt32_snd_l[15:0] - dut.midi_l;
			aud_next = aud_start / 3;
			aud_first_tick_time = $time;
		end
		aud_raw_l = aud_next[15:0] * 16'd3;
		aud_raw_r = ~aud_next[15:0];
		aud_exp_l = $signed(aud_raw_l) + dut.mt32_snd_l + $signed(dut.midi_l);
		aud_exp_r = $signed(aud_raw_r) + dut.mt32_snd_r + $signed(dut.midi_r);
		if (aud_exp_l > 32767) aud_exp_l = 32767;
		if (aud_exp_l < -32768) aud_exp_l = -32768;
		if (aud_exp_r > 32767) aud_exp_r = 32767;
		if (aud_exp_r < -32768) aud_exp_r = -32768;
		if ((dut.mt32_snd_l == aud_pi_l && dut.mt32_snd_r == aud_pi_r)
			&& (audio_l !== aud_exp_l[15:0] || audio_r !== aud_exp_r[15:0])) begin
			if (aud_errors < 10) $display("FAIL: audio sample %0d got %h/%h", aud_next, audio_l, audio_r);
			aud_errors = aud_errors + 1;
		end
		aud_pi_l = dut.mt32_snd_l;
		aud_pi_r = dut.mt32_snd_r;
		if (dut.midi_l != 0) midi_samples = midi_samples + 1;
		aud_next = (aud_next + 1) % 16384;
		aud_samples = aud_samples + 1;
		aud_last_tick_time = $time;
	end
end

initial begin
	repeat (18) #2_000_000_000;
	$display("FAIL: timeout");
	$finish;
end

// The Pi. I2C at 100kHz: a write of its synth to address 0x45 (mode, ROM,
// SoundFont), a read of what the core asks for (the mode byte)
task i2c_bit(input b);
	begin
		pi_sda = b; #2500; pi_scl = 1; #5000; pi_scl = 0; #2500;
	end
endtask
task i2c_byte(input [7:0] v);
	integer i;
	begin
		for (i = 7; i >= 0; i = i - 1) i2c_bit(v[i]);
		i2c_bit(1);     // the core's ACK
	end
endtask
task pi_write(input [7:0] a, input [7:0] b, input [7:0] c);
	begin
		pi_sda = 0; #5000; pi_scl = 0; #2500;   // start
		i2c_byte({7'h45, 1'b0});
		i2c_byte(a); i2c_byte(b); i2c_byte(c);
		pi_sda = 0; #2500; pi_scl = 1; #5000; pi_sda = 1; #10000;   // stop
	end
endtask
reg [7:0] pi_rd_sf;   // the SoundFont the last read asked for
task pi_read(output integer v);
	integer i, k;
	begin
		v = 0;
		pi_sda = 0; #5000; pi_scl = 0; #2500;
		i2c_byte({7'h45, 1'b1});
		for (k = 0; k < 3; k = k + 1) begin
			for (i = 0; i < 8; i = i + 1) begin
				pi_sda = 1; #2500; pi_scl = 1; #2500;
				if (k == 0) v = {v[30:0], user_in[0]};
				if (k == 2) pi_rd_sf = {pi_rd_sf[6:0], user_in[0]};
				#2500; pi_scl = 0; #2500;
			end
			i2c_bit(k == 2);   // the Pi's ACK, none after the last
		end
		pi_sda = 0; #2500; pi_scl = 1; #5000; pi_sda = 1; #10000;
	end
endtask
// The Pi reads the core's settings every 50ms; here every 20ms, and the core
// takes it for gone after 200ms (MT32_QUIET) instead of a second. pi_stop
// before the test's own reads and writes.
reg     pi_polling = 0, pi_busy = 0;
integer pi_poll_v;
initial forever begin
	#20000000;
	if (pi_polling) begin
		pi_busy = 1;
		pi_read(pi_poll_v);
		pi_busy = 0;
	end
end
task pi_stop;
	begin
		pi_polling = 0;
		wait (!pi_busy);
	end
endtask
// I2S, 16 bits a channel at 48kHz: 1.536MHz bit clock, word select 1 for
// the right channel and changing a bit before the word
reg [15:0] pi_left = 0, pi_right = 0;
// MiSTer's mt32pi.sv leaves some registers without a start value; the FPGA
// starts them at 0, the simulation with x (its I2C slave's divider would
// never reach 0, so it never looked at its pins)
initial begin
	dut.mt32pi.i2c_slave.div = 0;
	dut.mt32pi.clk_rate[0].clkr.cnt_tmp = 0;
	dut.mt32pi.clk_rate[1].clkr.cnt_tmp = 0;
	dut.mt32pi.clk_rate[0].cnt = 0;
	dut.mt32pi.clk_rate[1].cnt = 0;
	dut.mt32pi.mt32_newmode = 0;
	dut.mt32pi.mt32_mode = 0;
	dut.mt32pi.mt32_rom = 0;
	dut.mt32pi.mt32_sf = 0;
end

initial begin : i2s
	integer i;
	reg [31:0] frame;
	forever begin
		frame = {pi_right, pi_left};
		for (i = 0; i < 32; i = i + 1) begin
			pi_ws = (i == 31) ? 1'b0 : (i >= 15);
			pi_dat = (i < 16) ? pi_left[15 - i] : pi_right[31 - i];
			#325.5 pi_bclk = 1; #325.5 pi_bclk = 0;
		end
	end
end
// MIDI from the core: 31250 baud
reg [7:0] rx[0:15];
integer   rx_n = 0, rx_frame = 0;
initial begin : uart_rx
	integer i;
	reg [7:0] v;
	forever begin
		@(negedge user_out[1]);
		#16000;   // the middle of the start bit
		for (i = 0; i < 8; i = i + 1) begin
			#32000 v[i] = user_out[1];
		end
		#32000;
		if (user_out[1] !== 1'b1) begin $display("FAIL: MIDI stop bit"); errors = errors + 1; end
		rx[rx_n % 16] = v;
		rx_n = rx_n + 1;
		rx_frame = dut.aud_play;
	end
end
// info boxes asked of Main_MiSTer
integer info_reqs = 0, info_last = 0;
reg     info_req_d = 0;
always @(posedge clk) begin
	info_req_d <= info_req;
	if (info_req && !info_req_d) begin
		info_reqs = info_reqs + 1;
		info_last = info;
	end
end

// writes made to the FM chip
integer fm_takes = 0, fm_last = 0, fm_last_frame = 0;
always @(posedge clk) if (dut.fm_take) begin
	fm_takes = fm_takes + 1;
	fm_last = {dut.fm_ent[47:40], dut.fm_ent[39:32]};
	fm_last_frame = dut.aud_play;
end

endmodule
