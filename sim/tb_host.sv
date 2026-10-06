// Testbench for hybrid_host: DDR model with random waitrequest and read
// latency, checks scanout pixels against framebuffer+palette (8bpp) and the
// RGB565 framebuffer in all video modes, sync timings, the CRT options, the
// status block and the audio ring. All the time it watches that no line is
// shorter than 64us unless vga31 allows it.
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
reg  [5:0] crt_hsize = 0;
reg  [3:0] crt_hpos = 0, crt_vpos = 0;
wire [15:0] audio_l, audio_r;
wire [7:0] r, g, b;

hybrid_host dut
(
	.clk(clk), .reset(reset), .refclk(clk),
	.ddr_busy(ddr_busy), .ddr_burstcnt(ddr_burstcnt), .ddr_addr(ddr_addr), .ddr_dout(ddr_dout),
	.ddr_dout_ready(ddr_dout_ready), .ddr_rd(ddr_rd), .ddr_din(ddr_din), .ddr_be(ddr_be), .ddr_we(ddr_we),
	.joystick_0(joystick_0), .joystick_1(joystick_1),
	.joy_l_analog_0(16'h1122), .joy_r_analog_0(16'h3344), .joy_l_analog_1(16'h5566), .joy_r_analog_1(16'h7788),
	.osd_status(64'hCAFE), .ps2_key(ps2_key), .ps2_mouse(ps2_mouse), .ps2_mouse_ext(16'd0),
	.clk_vid(clk_vid), .ce_pix(ce_pix), .r(r), .g(g), .b(b), .hsync(hsync), .vsync(vsync), .hblank(hblank), .vblank(vblank),
	.vga31(vga31), .crt_hsize(crt_hsize), .crt_hpos(crt_hpos), .crt_vpos(crt_vpos),
	.hires(hires), .fast(fast), .f1(f1), .menumask(menumask),
	.audio_l(audio_l), .audio_r(audio_r)
);

//////////////////////////////////////////////////////////////////
// DDR model. mem index = word address - BASE

localparam [28:0] BASE = 29'h06000000;
reg [63:0] mem[0:1310719];

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
task set_ctrl(input integer fb, input integer mode, input integer slot, input integer seq);
	mem[0] = {cur_mask[3:0], 3'd0, 1'b1, 7'd0, slot[0], cur_vmode[3:0], 3'd0, mode[0], fb[7:0], 32'h4259484D};
	mem[1] = {32'd0, seq[31:0]};
endtask

// the video modes
function integer mode_w(input integer m);
	mode_w = (m == 0 || m == 3) ? 320 : (m == 5) ? 800 : (m == 6) ? 1024 : 640;
endfunction
function integer mode_h(input integer m);
	mode_h = (m == 2) ? 400 : (m == 3) ? 240 : (m == 4) ? 480 : (m == 5) ? 600 : (m == 6) ? 768 : 200;
endfunction
// word index of a framebuffer: 1024x768 has its own, larger ones
function integer fb_word(input integer m, input integer fb);
	fb_word = (m == 6) ? 29'h80000 + fb * 29'h40000 : 29'h20000 * (fb + 1);
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
	for (i = 0; i < 1310720; i = i + 1) mem[i] = 0;
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
integer check_enable = 0, check_black = 0;
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
	end
	if (!hblank && old_hblank) px = 0;
	if (hblank && !old_hblank) hbl_ce = ce_count;

	if (!hblank && !vblank) begin
		if (check_enable) begin : chk
			reg [23:0] exp;
			row = lace ? py * 2 + (f1 ? 1 : 0) : py;
			exp = check_black ? 24'd0 : cur_mode ? rgb565(fb_pixel16(px, row)) : pal_rgb(cur_slot, fb_pixel(cur_fb, px, row));
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
		if (mem[9][15:12] != m || mem[9][9] != is_lace[0] || mem[9][16] != v31[0] || !mem[9][17] || mem[9][18] != fast) begin
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
	set_ctrl(0, 0, 0, 0);
	mem[0][31:0] = 0; // invalid control block: test pattern
	repeat (20) @(posedge clk);
	reset = 0;

	// mode 0 from framebuffer index 0
	wait_fields(1);
	if (dut.ctrl_valid) begin $display("FAIL: ctrl valid without magic"); $finish; end
	set_ctrl(0, 0, 1, 7);
	cur_fb = 0; cur_mode = 0; cur_slot = 1;
	// key press and joystick for the status block
	ps2_key = {~ps2_key[10], 1'b1, 1'b1, 8'h75}; // extended up arrow pressed
	wait_fields(2);
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
	test_mode(4, 0, 240, 800, 800 * 525 / 2, 1);
	test_mode(4, 1, 480, 800, 800 * 525, 0);
	// 800x600 at 40MHz, 1024x768 at 65MHz
	test_mode(5, 1, 600, 1056, 1056 * 628, 0);
	if (mem[15][7:0] != 8'd80 || mem[15][28:16] != 13'd3200) begin $display("FAIL: status video clock %h", mem[15]); errors = errors + 1; end
	test_mode(6, 1, 768, 1344, 1344 * 806, 0);
	if (hs_ns < 20670 || hs_ns > 20685) begin $display("FAIL: 1024x768 line %0f ns", hs_ns); errors = errors + 1; end

	// vga31 goes away in the middle of 1024x768: the output stops at once
	// and comes back at 15kHz and black, the mode is reported as not shown
	repeat (100000) @(posedge clk);
	vga31 = 0;
	wait_fields(3);
	check_black = 1;
	check_enable = 1;
	wait_fields(2);
	check_enable = 0;
	check_black = 0;
	$display("1024x768 without vga31: checked %0d pixels, %0d errors, hsync period %0d, line %0f us", checked, errors, hs_period, hs_ns / 1000);
	if (hs_period != 400 || mem[9][15:12] != 0 || mem[9][16] || fast || hires) begin $display("FAIL: mode that is not available"); errors = errors + 1; end
	// and it is shown again when vga31 is back
	vga31 = 1;
	wait_fields(4);
	cur_fb = 2; cur_mode = 1;
	check_enable = 1;
	wait_fields(2);
	check_enable = 0;
	if (hs_period != 1344 || mem[9][15:12] != 6) begin $display("FAIL: 1024x768 again"); errors = errors + 1; end
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
	cur_vmode = 7;
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
	if (errors != 0) $display("FAIL: %0d errors", errors);
	else $display("PASS");
	$finish;
end

//////////////////////////////////////////////////////////////////
// Audio checker: every output sample must be the next ring frame

integer aud_next = -1, aud_samples = 0, aud_errors = 0;
integer aud_first_tick_time = 0, aud_last_tick_time = 0;
reg     aud_check = 0;
always @(posedge clk) if (dut.aud_tick2 && dut.audio_en) begin
	// outputs update on this edge, look at them one clock later
	@(posedge clk);
	if (audio_l != 0 || audio_r != 0 || aud_next >= 0) begin
		if (aud_next < 0) begin
			aud_next = audio_l / 3;
			aud_first_tick_time = $time;
		end
		if (audio_l !== aud_next[15:0] * 16'd3 || audio_r !== ~aud_next[15:0]) begin
			if (aud_errors < 10) $display("FAIL: audio sample %0d got %h/%h", aud_next, audio_l, audio_r);
			aud_errors = aud_errors + 1;
		end
		aud_next = (aud_next + 1) % 16384;
		aud_samples = aud_samples + 1;
		aud_last_tick_time = $time;
	end
end

initial begin
	repeat (10) #2_000_000_000;
	$display("FAIL: timeout");
	$finish;
end

endmodule
