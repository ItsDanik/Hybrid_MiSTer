// The picture hybrid_host shows without a game, as it comes out of the core:
// frames of its video output written to PPM files (640x480, the two fields of
// 480i woven, or 480p with VGA31=1), <OUT>_<n>.ppm. Without START, one frame
// once the intro is over; with START, FRAMES frames from that field of the
// intro on (the core's own counters, started there). MSG: a file of the 41
// words of a message block (0x001800) to show; TIMEOUT 1: the core's own
// message, that no game came. For trying a picture before a core is built:
// sim/wall_shot.sh, sim/wall_gif.sh.
// LOGO: a folder with ovl_fb.hex / ovl_pal.hex (a game's logo as its overlay
// frame, 640x480 8bpp, and its palette) and game_fb.hex / game_pal.hex (its
// first frame, 320x200 8bpp): a game starting with its logo (host version 17,
// docs/game_logo.md), the memory doing what the library does, on a timeline
// of fields from the intro's start (L_*); START is a field of it. A frame of
// 320x200 is put in the middle of the 640x480, each pixel 2x2. The logo rises
// into place (w2[59]).
// PAGE 1 (with LOGO): the MIDI Player's start instead, page_fb.hex /
// page_pal.hex its page (640x480 8bpp, index 0 transparent): after the logo's
// 2 seconds the overlay goes out (w2[58]), the line is cleared, the page and
// its palette are put up and the overlay comes in again, without rising.
// FLICKER, FLT: the core's FLICKER and the OSD's Flicker Filter line (status
// bits 106:105), the page filtered at 480i (sim/page_flicker.sh checks it).
// HSIZE: the OSD's Horizontal Size (-24..7), which the page takes and the
// core's own picture does not.
`timescale 1ns/1ps

// Stand-in for hybrid_vclk.sv, as in tb_host.sv
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

module tb_wall
#(
	parameter WALL      = "rtl/hybrid_wall.hex",
	parameter WALL_PAL  = "rtl/hybrid_wall_pal.hex",
	parameter WALL_DOTS = "rtl/hybrid_wall_dots.hex",
	parameter WALL_EASE = "rtl/hybrid_wall_ease.hex",
	parameter OUT       = "wall",
	parameter VGA31     = 0,
	parameter START     = -1,
	parameter FRAMES    = 1,
	parameter MSG       = "",
	parameter TIMEOUT   = 0,
	parameter LOGO      = "",
	parameter PAGE      = 0,
	parameter FLICKER   = 0,
	parameter FLT       = 0,
	parameter HSIZE     = 0
);

// the timeline of a game starting with its logo, in fields from the intro's
// start: once the intro is over the two lines under the MiSTer logo, then the
// handover (the lines going out with the MiSTer logo), "Loading" alone once
// the screen is empty. PAGE (no files to check): "Loading" alone, the
// handover as soon as the intro is over.
localparam L_CHECK = 243;             // the intro is over: "Checking Game Data..."
localparam L_LINE  = 24;              // each line at least 0.4 s
localparam L_HAND  = PAGE ? L_CHECK : L_CHECK + 2 * L_LINE;  // the handover begins
localparam L_GONE  = L_HAND + 31;     // the MiSTer logo is gone
localparam L_LOAD  = L_HAND + 32;     // "Loading Doom..."
localparam L_IN    = L_HAND + 92;     // the logo is all there
localparam L_OUT   = L_IN + 120;      // 2 s later the overlay is given up
localparam L_DARK  = L_OUT + 43;      // the picture has gone out
// PAGE: the overlay out 31 fields after L_OUT, the line cleared, then the page
localparam L_CLEAR = L_OUT + 32;
localparam L_SWAP  = L_OUT + 34;

reg clk = 0;
always #10 clk = ~clk;
reg reset = 1;

// memory that is all zeros but a message: no game
wire  [7:0] ddr_burstcnt;
wire [28:0] ddr_addr;
wire        ddr_rd, ddr_we;
wire [63:0] ddr_din;
wire  [7:0] ddr_be;
reg         ddr_dout_ready = 0;
reg  [63:0] ddr_dout = 0;
reg  [63:0] msg[0:40];
integer     beats = 0, at = 0;
// a game starting with its logo
reg  [63:0] ovl_fb[0:38399], ovl_pal[0:127], game_fb[0:7999], game_pal[0:127];
reg  [63:0] page_fb[0:38399], page_pal[0:127];
wire        swapped = PAGE && tl >= L_SWAP;
integer     tl = -1;                  // the field of its timeline, -1: not running
integer     lines = 0;                // the lines: 1, 2 the first ones, 3 "Loading" alone
reg         game_frame = 0;           // the library has put the game's first frame up
initial begin
	for (at = 0; at < 41; at = at + 1) msg[at] = 0;
	if (MSG != "") $readmemh(MSG, msg);
	if (LOGO != "") begin
		$readmemh({LOGO, "/ovl_fb.hex"}, ovl_fb);
		$readmemh({LOGO, "/ovl_pal.hex"}, ovl_pal);
		if (PAGE) begin
			$readmemh({LOGO, "/page_fb.hex"}, page_fb);
			$readmemh({LOGO, "/page_pal.hex"}, page_pal);
		end else begin
			$readmemh({LOGO, "/game_fb.hex"}, game_fb);
			$readmemh({LOGO, "/game_pal.hex"}, game_pal);
		end
	end
end
// the lines, as the library writes them: the message block of n lines
function [63:0] line_word(input integer w, input integer n);
	reg [8*80-1:0] text;
	integer l;
	begin
		l = (w - 1) / 10;
		text = PAGE ? "Loading MIDI Player..." : (n == 3) ? "Loading Doom..." : (l == 0) ? "Checking Game Data..." : "Game Data OK";
		if (w == 0) line_word = n == 0 ? 64'd0 : {16'd0, n[15:0], 32'h534D484D};
		else line_word = l < (n == 3 ? 1 : n) ? text_word(text, (w - 1) % 10) : 64'd0;
	end
endfunction
// the lines the library shows at field t of the timeline
function integer timeline_lines(input integer t);
	begin
		if (PAGE) timeline_lines = (t < L_LOAD || t >= L_CLEAR) ? 0 : 1;
		else timeline_lines = (t < L_CHECK) ? 0 : (t < L_CHECK + L_LINE) ? 1 : (t < L_HAND) ? 2 : (t < L_LOAD) ? 0 : 3;
	end
endfunction
// a string's characters come right-aligned: character i of it at byte i
function [63:0] text_word(input [8*80-1:0] text, input integer k);
	integer len, b, i;
	begin
		len = 0;
		for (i = 0; i < 80; i = i + 1) if (text[i * 8 +: 8] != 0) len = i + 1;
		text_word = 0;
		for (b = 0; b < 8; b = b + 1) begin
			i = k * 8 + b;
			if (i < len) text_word[b * 8 +: 8] = text[(len - 1 - i) * 8 +: 8];
		end
	end
endfunction
// the control block: the overlay with the logo's frame (framebuffer 0,
// palette slot 0) until it is given up, the game's mode set meanwhile, then
// its first frame (framebuffer 1, palette slot 1)
function [63:0] ctrl_word(input integer w);
	begin
		// [58] overlay, [48] palette slot, [47:44] mode, [39:32] framebuffer
		if (PAGE) begin
			// the overlay all along: the logo's frame, out, then the page's
			if (w == 0) ctrl_word = {5'd0, 1'b1, 9'd0, swapped, 4'd4, 4'd0, 8'(swapped ? 1 : 0), 32'h4259484D};
			else if (w == 1) ctrl_word = swapped ? 64'd2 : 64'd1;
			else ctrl_word = {4'd0, !swapped, tl >= L_OUT && !swapped, 58'd0};
		end
		else if (w == 0) ctrl_word = game_frame ? {15'd0, 1'b1, 4'd0, 4'd0, 8'd1, 32'h4259484D}
		                      : (tl < L_HAND) ? 64'd0   // no control block while the lines are under the MiSTer logo
		                      : {5'd0, tl < L_OUT, 9'd0, 1'b0, tl < L_OUT ? 4'd4 : 4'd0, 4'd0, 8'd0, 32'h4259484D};
		else if (w == 1) ctrl_word = game_frame ? 64'd2 : 64'd1;
		else ctrl_word = {4'd0, 1'b1, 59'd0};   // [59] the logo rises
	end
endfunction
function [63:0] mem(input integer a);
	begin
		if (LOGO == "" || tl < 0) mem = (a >= 'h300 && a < 'h300 + 41) ? msg[a - 'h300] : 64'd0;
		else if (a < 3) mem = ctrl_word(a);
		else if (a >= 'h200 && a < 'h280) mem = ovl_pal[a - 'h200];
		else if (a >= 'h280 && a < 'h300) mem = PAGE ? page_pal[a - 'h280] : game_pal[a - 'h280];
		else if (a >= 'h300 && a < 'h300 + 41) mem = game_frame ? 64'd0 : line_word(a - 'h300, lines);
		else if (a >= 'h20000 && a < 'h20000 + 38400) mem = ovl_fb[a - 'h20000];
		else if (PAGE && a >= 'h40000 && a < 'h40000 + 38400) mem = page_fb[a - 'h40000];
		else if (!PAGE && a >= 'h40000 && a < 'h40000 + 8000) mem = game_fb[a - 'h40000];
		else mem = 0;
	end
endfunction
always @(posedge clk) begin
	ddr_dout_ready <= 0;
	if (ddr_rd) begin
		beats = ddr_burstcnt;
		at = ddr_addr - 29'h06000000;
	end
	else if (beats != 0) begin
		ddr_dout_ready <= 1;
		ddr_dout <= mem(at);
		at = at + 1;
		beats = beats - 1;
	end
end

wire clk_vid, ce_pix, hsync, vsync, hblank, vblank, hires, fast, f1;
wire [7:0] r, g, b;

hybrid_host #(.WALL(WALL), .WALL_PAL(WALL_PAL), .WALL_DOTS(WALL_DOTS), .WALL_EASE(WALL_EASE),
	.WALL_FONT("rtl/hybrid_wall_font.hex"), .WALL_ALONE("rtl/hybrid_wall_alone.hex"), .WALL_TIMEOUT(TIMEOUT ? 2 : 716), .FLICKER(FLICKER)) dut
(
	.clk(clk), .reset(reset), .refclk(clk),
	.ddr_busy(1'b0), .ddr_burstcnt(ddr_burstcnt), .ddr_addr(ddr_addr), .ddr_dout(ddr_dout),
	.ddr_dout_ready(ddr_dout_ready), .ddr_rd(ddr_rd), .ddr_din(ddr_din), .ddr_be(ddr_be), .ddr_we(ddr_we),
	.joystick_0(32'd0), .joystick_1(32'd0),
	.joy_l_analog_0(16'd0), .joy_r_analog_0(16'd0), .joy_l_analog_1(16'd0), .joy_r_analog_1(16'd0),
	.osd_status({21'd0, 2'(FLT), 105'd0}), .osd_open(1'b0), .ps2_key(11'd0), .ps2_mouse(25'd0), .ps2_mouse_ext(16'd0),
	.clk_vid(clk_vid), .ce_pix(ce_pix), .r(r), .g(g), .b(b), .hsync(hsync), .vsync(vsync), .hblank(hblank), .vblank(vblank),
	.vga31(VGA31 != 0), .crt_hsize(6'(HSIZE)), .crt_hpos(4'd0), .crt_vpos(4'd0),
	.hires(hires), .fast(fast), .f1(f1), .menumask(),
	.clk_audio(1'b0), .user_in(7'h7F), .user_out(), .mt32_on(), .info_req(), .info(),
	.audio_l(), .audio_r()
);

// the frame, from the output: a row per line of a field and its place in the
// frame (interlaced, the field with f1 has the odd rows)
reg  [23:0] frame[0:640 * 480 - 1];
integer     px = 0, py = 0, fields = 0, capture = 0, seen = 0, rows = 0;
reg         old_hblank = 1, old_vblank = 1;
wire        lace = dut.v_lace;

always @(posedge clk_vid) if (ce_pix) begin
	old_hblank <= hblank;
	old_vblank <= vblank;
	if (!vblank && old_vblank) py = 0;
	if (!hblank && old_hblank) px = 0;
	if (!hblank && !vblank) begin
		if (capture && px < 640) begin : put
			integer row;
			row = lace ? py * 2 + (f1 ? 1 : 0) : py;
			if (dut.v_mode == 4'd0) begin
				// 320x200: in the middle, 2x2
				if (px < 320 && py < 200) begin
					frame[(40 + py * 2) * 640 + px * 2] = {r, g, b};
					frame[(40 + py * 2) * 640 + px * 2 + 1] = {r, g, b};
					frame[(41 + py * 2) * 640 + px * 2] = {r, g, b};
					frame[(41 + py * 2) * 640 + px * 2 + 1] = {r, g, b};
				end
			end
			else if (row < 480) frame[row * 640 + px] = {r, g, b};
			seen = seen + 1;
		end
		px = px + 1;
	end
	if (hblank && !old_hblank && !vblank) py = py + 1;
	if (vblank && !old_vblank) begin
		fields = fields + 1;
		rows = py;
		// the logo's timeline: what the library does at each field
		if (tl >= 0) begin
			tl = tl + 1;
			lines = timeline_lines(tl);
			// the game's first frame, once the library has seen the screen is the game's
			if (dut.game_on) game_frame = 1;
		end
	end
end

task wait_fields(input integer n);
	integer target;
	begin
		target = fields + n;
		while (fields < target) @(posedge clk);
	end
endtask

integer f, i, n;
reg [8*256-1:0] name;
initial begin
	repeat (20) @(posedge clk);
	reset = 0;
	while (dut.ctrl_clear != 0) wait_fields(1);
	if (START < 0) begin
		// the end of the intro
		while (!dut.intro_done || dut.wall_level != 9'd256) wait_fields(1);
		wait_fields(2);
	end else if (LOGO != "") begin
		// field START of a game's start with its logo
		@(negedge vblank);
		repeat (100) @(posedge clk);
		// 6 fields early: the levels and the palette settle before the first frame
		tl = START - 6;
		// not in the MiSTer logo's going out: the lines going with it are
		// the core's (msg_keep), from the handover's start
		if (!PAGE && tl >= L_HAND - 2 && tl < L_GONE) tl = L_HAND - 2;
		lines = timeline_lines(tl);
		dut.wall_hold  = 0;
		dut.intro_on   = 1;
		dut.attached   = 1;
		dut.intro_t    = (tl < 242) ? tl : 242;
		dut.dot_t      = (tl > dut.WALL_T_DOTS) ? tl - dut.WALL_T_DOTS : 0;
		dut.wall_level = (PAGE || tl < L_OUT) ? 9'd256 : (tl >= L_DARK) ? 9'd0 : 9'd256 - 6 * (tl - L_OUT);
		if (tl >= L_HAND) begin
			dut.ovl_on = 1;
			dut.ovl_t  = (tl - L_HAND > 127) ? 127 : tl - L_HAND;
			// PAGE: going out from 91 down to 61, held, in again from the swap
			if (PAGE && tl >= L_OUT)
				dut.ovl_t = (tl >= L_SWAP) ? ((tl - L_SWAP + 61 > 127) ? 127 : tl - L_SWAP + 61)
				          : (91 - (tl - L_OUT) < 61) ? 61 : 91 - (tl - L_OUT);
		end
		wait_fields(START - tl);
	end else begin
		// the intro from field START, as the core would be there: set in the
		// middle of a field, the vblank after it takes it on
		@(negedge vblank);
		repeat (100) @(posedge clk);
		dut.wall_hold  = 0;
		dut.intro_on   = 1;
		dut.wall_level = 9'd256;
		dut.intro_t    = START;
		dut.dot_t      = (START > dut.WALL_T_DOTS) ? START - dut.WALL_T_DOTS : 0;
	end
	// the core's own message, when it is to be seen
	if (TIMEOUT) while (!dut.msg_timeout) wait_fields(1);
	// a frame starts with the field of the even rows
	wait_fields(1);
	if (lace) while (!f1) wait_fields(1);
	for (n = 0; n < FRAMES; n = n + 1) begin
		seen = 0;
		if (LOGO != "") for (i = 0; i < 640 * 480; i = i + 1) frame[i] = 0;
		capture = 1;
		wait_fields(lace ? 2 : 1);
		capture = 0;
		// progressive (the game): a frame every second field, 30 a second as 480i
		if (LOGO != "" && !lace) wait_fields(1);
		$sformat(name, "%0s_%03d.ppm", OUT, n);
		f = $fopen(name, "wb");
		$fwrite(f, "P6\n640 480\n255\n");
		for (i = 0; i < 640 * 480; i = i + 1) $fwrite(f, "%c%c%c", frame[i][23:16], frame[i][15:8], frame[i][7:0]);
		$fclose(f);
	end
	$display("tb_wall: %0d frames of %0d pixels, %0d rows per field, %s, intro field %0d at the end%0s", FRAMES, seen, rows,
		lace ? "480i" : "480p", dut.intro_t, LOGO != "" ? "" : "");
	if (LOGO != "") $display("tb_wall: logo timeline field %0d, overlay field %0d, level %0d, game %0d, size %0d", tl, dut.ovl_t,
		dut.wall_level, dut.game_on, $signed(dut.v_hsize));
	$finish;
end

endmodule
