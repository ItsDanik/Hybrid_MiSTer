//============================================================================
//
//  MiSTer hybrid core host - HPS <-> FPGA bridge shared by all hybrid cores
//
//  The game runs on the HPS (ARM). It renders frames into DDR3 and this
//  module scans them out in the video mode the game asks for, with native
//  15kHz timings where the mode has them (see "Video timing" below). Audio
//  comes from a ring in the same memory and input state is published back
//  through it. The HPS side of this protocol is hybrid/hps/mister_hybrid.c.
//
//  While no game is attached (no valid control block) the core is in
//  320x240 and shows a picture of its own: a logo in the middle of the screen
//  and the version of this module in the bottom right corner (see "Picture
//  without a game" below).
//
//  Two clocks: `clk` (50MHz) for the DDR3 side, audio and input, and
//  `clk_vid` for the video output, made here by hybrid_vclk.sv.
//
//  Shared memory layout (physical address 0x30000000 + offset):
//    0x000000  control (HPS -> FPGA), 2 x 64-bit words
//              w0[31:0]  magic "MHYB" (0x4259484D)
//              w0[39:32] framebuffer index to display (0..2)
//              w0[40]    pixel format: 0 = 8bpp paletted, 1 = RGB565
//              w0[47:44] video mode: 0 = 320x200, 1 = 640x200, 2 = 640x400,
//                        3 = 320x240, 4 = 640x480, 5 = 800x600, 6 = 1024x768,
//                        7 = 640x240
//              w0[48]    palette slot (0..1)
//              w0[56]    audio enable
//              w0[63:60] OSD menu mask: bits a game sets to hide or grey out
//                        lines of the core's CONF_STR (H/h/D/d 0..3)
//              w1[31:0]  palette sequence number (reload when changed)
//    0x000040  status (FPGA -> HPS), 16 x 64-bit words, written every vblank
//              w0  {frame counter, magic "MHYS" (0x5359484D)}
//              w1  {version, 13'b0, fast, running, vga31, mode[3:0], field,
//                   ctrl_valid, lace, format, 6'b0, fb_index[1:0]}
//                   lace: the mode is shown interlaced.
//                   field: the field that was on the screen had the even
//                   rows (the core takes a new frame when it is 0).
//                   mode: the mode shown; 0 while the one asked for is not
//                   available.
//                   vga31: modes above 15kHz are available (800x600 and
//                   1024x768 have no other form).
//                   running: 0 while the video output is off for a change
//                   of the video clock.
//                   fast: the mode is shown at 31kHz or more.
//              w2  {joystick_1, joystick_0}
//              w3  {r_analog_1, l_analog_1, r_analog_0, l_analog_0}
//              w4  OSD status[63:0]
//              w5  {mouse y accumulator, mouse x accumulator}
//              w6  {wheel accumulator[15:0], 13'b0, mouse buttons[2:0]}
//              w7  {32'b0, 3'b0, video clock measured[12:0], 8'b0, video
//                   clock asked for[7:0]}: the clock asked for in units of
//                   0.5MHz, the one measured in units of 1/80 MHz
//              w8..w15 keyboard bitmap, bit index = {extended, PS/2 set 2 code}
//    0x0000C0  audio fetch pointer (FPGA -> HPS), 32-bit count of stereo
//              frames read from the ring, written after every audio fetch
//    0x001000  palette slot 0: 256 x 32-bit 0x00RRGGBB
//    0x001400  palette slot 1
//    0x010000  audio ring: 16384 stereo frames, 16-bit signed {R, L}, 44.1kHz
//    0x100000  framebuffer 0 (row stride = width of the video mode in pixels;
//              an interlaced mode has all its rows here, as a progressive one)
//    0x200000  framebuffer 1
//    0x300000  framebuffer 2
//    0x400000  framebuffer 0 of 1024x768, which does not fit in the above
//    0x600000  framebuffer 1 of 1024x768
//    0x800000  framebuffer 2 of 1024x768
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//============================================================================

module hybrid_host
#(
	// the picture without a game: logo, its colours and the text. Quartus
	// reads them from the project's directory, core/
	parameter WALL      = "../hybrid/rtl/hybrid_wall.hex",
	parameter WALL_PAL  = "../hybrid/rtl/hybrid_wall_pal.hex",
	parameter WALL_TEXT = "../hybrid/rtl/hybrid_wall_text.hex"
)
(
	input             clk,          // 50MHz
	input             reset,
	input             refclk,       // 50MHz for the PLL of the video clock (CLK_50M)

	// DDR3 via the MiSTer framework (Avalon-MM, 64 bit, address in 8-byte units)
	input             ddr_busy,
	output reg  [7:0] ddr_burstcnt,
	output reg [28:0] ddr_addr,
	input      [63:0] ddr_dout,
	input             ddr_dout_ready,
	output reg        ddr_rd,
	output reg [63:0] ddr_din,
	output      [7:0] ddr_be,
	output reg        ddr_we,

	// input state published to the HPS
	input      [31:0] joystick_0,
	input      [31:0] joystick_1,
	input      [15:0] joy_l_analog_0,
	input      [15:0] joy_r_analog_0,
	input      [15:0] joy_l_analog_1,
	input      [15:0] joy_r_analog_1,
	input      [63:0] osd_status,
	input      [10:0] ps2_key,
	input      [24:0] ps2_mouse,
	input      [15:0] ps2_mouse_ext,

	// video out, all of it in the clk_vid domain; 1 pixel = ce_pix
	output            clk_vid,
	output reg        ce_pix,
	output reg  [7:0] r,
	output reg  [7:0] g,
	output reg  [7:0] b,
	output reg        hsync,
	output reg        vsync,
	output reg        hblank,
	output reg        vblank,
	// A screen that takes 31kHz and more is the only one that gets it: either
	// the analog output is a VGA monitor (forced_scandoubler of the MiSTer
	// framework) or the core switches the analog output off while `fast` is 1
	// ("HDMI only"). 640x400 and 640x480 are then progressive at 31kHz and
	// 800x600 and 1024x768 are available; else the first two are interlaced
	// at 15kHz and the others are not shown.
	input             vga31,
	// CRT options, for the 15kHz timings only. Size -24..+7 in steps of 1% of
	// the pixels in a line, position -8..+7 in steps of 2 pixels of 320 and of
	// 1 line; positive is larger, to the right and down.
	input       [5:0] crt_hsize,
	input       [3:0] crt_hpos,
	input       [3:0] crt_vpos,
	// interlaced or above 15kHz: must not be scandoubled
	output            hires,
	// the signal on the output is 31kHz or more (follows vga31, and drops
	// only after the output went quiet)
	output            fast,
	// interlaced: the field on the screen is the second one, with the odd
	// rows (VGA_F1 of the MiSTer framework)
	output            f1,
	// OSD menu mask the game set
	output reg  [3:0] menumask,

	// audio, 44.1kHz signed
	output reg [15:0] audio_l,
	output reg [15:0] audio_r
);

localparam [31:0] CTRL_MAGIC   = 32'h4259484D; // "MHYB"
localparam [31:0] STATUS_MAGIC = 32'h5359484D; // "MHYS"
// the picture without a game has the version in it: run tools/make_wall.py
localparam [31:0] VERSION      = 32'd8;

localparam [28:0] BASE       = 29'h06000000;   // 0x30000000 >> 3
localparam [28:0] CTRL_ADDR  = BASE;
localparam [28:0] STAT_ADDR  = BASE + 29'h8;
localparam [28:0] PAL_ADDR   = BASE + 29'h200;
localparam [28:0] FB_ADDR    = BASE + 29'h20000;
localparam [28:0] FB_XL_ADDR = BASE + 29'h80000;   // 1024x768: 2MB each
localparam [28:0] AUD_ADDR   = BASE + 29'h2000;   // ring, 8192 words
localparam [28:0] AUD_PTR    = BASE + 29'h18;
localparam  [7:0] AUD_BURST  = 8'd16;             // 32 frames

assign ddr_be = 8'hFF;

//////////////////////////////////////////////////////////////////
// Video timing
//
// mode  size      15kHz screen                 with vga31
//  0    320x200   progressive                  the same (scandoubled by the core)
//  1    640x200   progressive                  the same
//  2    640x400   interlaced                   progressive, 31kHz
//  3    320x240   progressive                  the same
//  4    640x480   interlaced                   progressive, 31kHz
//  5    800x600   not shown                    progressive, 37.9kHz, 40MHz
//  6    1024x768  not shown                    progressive, 48.4kHz, 65MHz
//  7    640x240   progressive                  the same
//
// 15kHz: 6.25MHz pixel clock (video clock of 50MHz / 8), 400 x 262 lines ->
//   15.625kHz / 59.6Hz; 12.5MHz and 800 pixels in a line at 640 wide. 200 or
//   240 of the lines have the picture.
//   Interlaced, the lines of a mode with half the rows in two fields of 262.5
//   lines, 525 per frame, 29.8 frames per second. The field with the even
//   rows of the frame has 263 lines and its vsync starts in the middle of a
//   line; the field with the odd rows has 262 lines. That puts the lines of
//   the second half a line below the first's.
//   The CRT options move the sync pulses (position) and change the video
//   clock and the number of pixels in a line by the same 1% (horizontal
//   size): the line stays 64us long, the pixels all get wider or narrower.
// 31kHz: 25MHz pixel clock, 800 x 525 lines -> 31.25kHz / 59.5Hz, the timing
//   of VGA 640x480, with the 400 lines of 640x400 in its middle.
// 800x600 and 1024x768: the VESA timings at 60Hz, a pixel every clock.
//
// What is on the output is decided here, in the clk domain, as a "class":
// the kind of timing (15kHz progressive, interlaced, 31kHz, high) and the
// video clock. A change of class (another mode, vga31, the horizontal size)
// stops the video output first (no sync, black), then sets the clock, and
// starts the output again once hybrid_vclk has measured the new clock. So a
// 15kHz screen never gets the lines of another class, not even for a moment.
// Changes within a class (320 or 640 wide, 200 or 240 lines, positions) are
// taken when the next field starts.

reg  [3:0] vmode = 4'd3;      // the mode the HPS asks for; 320x240 without a game

wire       m_dual = (vmode == 4'd2) || (vmode == 4'd4);
wire       m_hi   = (vmode == 4'd5) || (vmode == 4'd6);
wire       t_fast = m_dual & vga31;
wire       t_lace = m_dual & ~vga31;
wire       t_hi   = m_hi & vga31;
wire       t_15k  = ~(t_fast | t_hi);
// the size is -24..+7: beyond that the blanking gets too short or the clock too fast
wire [5:0] hsize  = ($signed(crt_hsize) > 6'sd7) ? 6'sd7 : ($signed(crt_hsize) < -6'sd24) ? -6'sd24 : crt_hsize;
wire [7:0] t_code = t_hi ? ((vmode == 4'd5) ? 8'd80 : 8'd130)
                  : t_fast ? 8'd100 : 8'd100 - {{2{hsize[5]}}, hsize};

// in use
reg        run = 0;           // the video output is on
reg  [7:0] vclk_code = 8'd100;
reg        u_fast = 0, u_lace = 0, u_hi = 0;
reg  [4:0] stop_cnt = 0;
// for the video side: these hold still whenever it reads them
reg  [3:0] s_mode = 0;
reg        s_off = 0;         // a mode that this screen does not get: black
reg  [5:0] s_hsize = 0;
reg  [3:0] s_hpos = 0, s_vpos = 0;

wire       vclk_ready;
wire [12:0] vclk_count;
wire       class_change = {t_fast, t_lace, t_hi, t_code} != {u_fast, u_lace, u_hi, vclk_code};

hybrid_vclk vclk
(
	.clk(clk),
	.refclk(refclk),
	.code(vclk_code),
	.clk_vid(clk_vid),
	.ready(vclk_ready),
	.count(vclk_count)
);

always @(posedge clk) begin
	s_mode  <= (m_hi & ~vga31) ? 4'd0 : vmode;
	s_off   <= m_hi & ~vga31;
	s_hsize <= t_15k ? hsize : 6'd0;
	s_hpos  <= crt_hpos;
	s_vpos  <= crt_vpos;

	if (run) begin
		stop_cnt <= 0;
		if (class_change) run <= 0;
	end
	else if (!stop_cnt[4]) begin
		// time for the video side to go quiet
		stop_cnt <= stop_cnt + 1'd1;
	end
	else begin
		{u_fast, u_lace, u_hi, vclk_code} <= {t_fast, t_lace, t_hi, t_code};
		if (!class_change && vclk_ready) run <= 1;
	end
end

assign hires = u_fast | u_lace | u_hi;
assign fast  = u_fast | u_hi;

// ---- from here on clk_vid ----

reg  [2:0] vrst_s = 3'b111;
always @(posedge clk_vid) vrst_s <= {vrst_s[1:0], ~run};
wire       vrst = vrst_s[2];

// the timing in use, taken when a field starts
reg  [3:0] v_mode = 0;
reg        v_fast = 0, v_lace = 0, v_hi = 0, v_off = 0;
reg  [5:0] v_hsize = 0;
reg  [3:0] v_hpos = 0, v_vpos = 0;
reg        v_fmt16 = 0, v_valid = 0;
reg        field = 0;         // interlaced: 1 = even rows

assign f1 = v_lace & ~field;

wire       w640 = (v_mode == 4'd1) || (v_mode == 4'd2) || (v_mode == 4'd4) || (v_mode == 4'd7);
wire       l240 = (v_mode == 4'd3) || (v_mode == 4'd4) || (v_mode == 4'd7);
wire       xl   = (v_mode == 4'd6);

// 15kHz, in pixels of 320: 400 - 4 * size in a line, 32 of sync. The front
// porch is 16 at size 0 and takes half of what the size adds or removes,
// which keeps the middle of the picture where it is; then it is moved by
// the position, to at least 2 and leaving 4 of back porch.
wire signed [9:0] hsz    = {{4{v_hsize[5]}}, v_hsize};
wire signed [9:0] hps    = {{6{v_hpos[3]}}, v_hpos};
wire signed [9:0] vps    = {{6{v_vpos[3]}}, v_vpos};
wire signed [9:0] htot   = 10'sd400 - (hsz <<< 2);
wire signed [9:0] fp_raw = 10'sd16 - (hsz <<< 1) - (hps <<< 1);
wire signed [9:0] fp_max = 10'sd44 - (hsz <<< 2);
wire signed [9:0] fp     = (fp_raw < 10'sd2) ? 10'sd2 : (fp_raw > fp_max) ? fp_max : fp_raw;
wire        [9:0] hs320  = 10'd320 + fp[9:0];
// 240 lines leave 22 for the blanking: 1 to 10 of them before the sync
wire signed [9:0] vs_raw = 10'sd245 - vps;
wire        [9:0] vs240  = (vs_raw < 10'sd241) ? 10'd241 : (vs_raw > 10'sd250) ? 10'd250 : vs_raw[9:0];
wire        [9:0] vs200  = 10'sd228 - vps;

reg [10:0] H_TOTAL, H_ACTIVE, HS_START, HS_END, HS_HALF;
reg  [9:0] V_TOTAL, V_ACTIVE, VS_START, VS_END;

always @(posedge clk_vid) begin
	if (v_hi) begin
		H_TOTAL  <= xl ? 11'd1344 : 11'd1056;
		H_ACTIVE <= xl ? 11'd1024 : 11'd800;
		HS_START <= xl ? 11'd1048 : 11'd840;
		HS_END   <= xl ? 11'd1184 : 11'd968;
		V_TOTAL  <= xl ? 10'd806 : 10'd628;
		V_ACTIVE <= xl ? 10'd768 : 10'd600;
		VS_START <= xl ? 10'd771 : 10'd601;
		VS_END   <= xl ? 10'd777 : 10'd605;
	end
	else if (v_fast) begin
		H_TOTAL  <= 11'd800;
		H_ACTIVE <= 11'd640;
		HS_START <= 11'd656;
		HS_END   <= 11'd752;
		V_TOTAL  <= 10'd525;
		V_ACTIVE <= l240 ? 10'd480 : 10'd400;
		VS_START <= l240 ? 10'd490 : 10'd450;
		VS_END   <= l240 ? 10'd492 : 10'd452;
	end
	else begin
		H_TOTAL  <= w640 ? {htot[9:0], 1'b0} : {1'b0, htot[9:0]};
		H_ACTIVE <= w640 ? 11'd640 : 11'd320;
		HS_START <= w640 ? {hs320, 1'b0} : {1'b0, hs320};
		HS_END   <= w640 ? {hs320 + 10'd32, 1'b0} : {1'b0, hs320 + 10'd32};
		V_TOTAL  <= (v_lace && field) ? 10'd263 : 10'd262;
		V_ACTIVE <= l240 ? 10'd240 : 10'd200;
		VS_START <= l240 ? vs240 : vs200;
		VS_END   <= (l240 ? vs240 : vs200) + 10'd3;
	end
	HS_HALF <= w640 ? {1'b0, htot[9:0]} : {2'b0, htot[9:1]};
end

reg  [2:0] ce_div = 0;
reg [10:0] hc = 0;
reg  [9:0] vc = 0;

wire       ce = v_hi ? 1'b1 : v_fast ? ce_div[0] : w640 ? (ce_div[1:0] == 2'd3) : (ce_div == 3'd7);
wire       line_start = ce && (hc == H_TOTAL - 1'd1);   // next ce begins a new line
wire [9:0] next_vc  = (vc == V_TOTAL - 1'd1) ? 10'd0 : vc + 1'd1;
wire [9:0] next2_vc = (next_vc == V_TOTAL - 1'd1) ? 10'd0 : next_vc + 1'd1;

// vblank tasks start at the first blank line
wire       vbl_start = line_start && (next_vc == V_ACTIVE);

// requests to the clk side: a line to fetch, vblank
reg        req_tgl = 0, vbl_tgl = 0;
reg  [9:0] req_row;
reg        req_bank;
reg        vbl_field = 0;

always @(posedge clk_vid) begin
	ce_div <= ce_div + 1'd1;
	if (ce) begin
		if (hc == H_TOTAL - 1'd1) begin
			hc <= 0;
			vc <= next_vc;
			if (next_vc == 0) begin
				v_mode  <= s_mode;
				v_off   <= s_off;
				v_hpos  <= s_hpos;
				v_vpos  <= s_vpos;
				v_fmt16 <= fmt16;
				v_valid <= ctrl_valid;
				field   <= v_lace & ~field;
			end
		end else begin
			hc <= hc + 1'd1;
		end
	end

	// fetch the line after the one starting now. Interlaced, a field has
	// every second row of the frame; the first lines of a field are fetched
	// while the one before it ends.
	if (line_start && next2_vc < V_ACTIVE) begin
		req_tgl  <= ~req_tgl;
		req_row  <= !v_lace ? next2_vc : {next2_vc[8:0], (next2_vc < vc) ? field : ~field};
		req_bank <= next2_vc[0];
	end
	if (vbl_start) begin
		vbl_tgl   <= ~vbl_tgl;
		vbl_field <= field;
	end

	// off: everything of the class is taken as it is and the counters wait
	// in the blanking
	if (vrst) begin
		{v_fast, v_lace, v_hi} <= {u_fast, u_lace, u_hi};
		v_mode  <= s_mode;
		v_off   <= s_off;
		v_hsize <= s_hsize;
		v_hpos  <= s_hpos;
		v_vpos  <= s_vpos;
		v_fmt16 <= fmt16;
		v_valid <= ctrl_valid;
		field   <= 0;
		ce_div  <= 0;
		hc      <= 0;
		vc      <= V_ACTIVE;
	end
end

//////////////////////////////////////////////////////////////////
// Control state (latched from DDR during vblank)

reg        ctrl_valid = 0;
reg        audio_en = 0;
reg  [1:0] fb_index = 0;
reg        fmt16 = 0;         // RGB565 instead of 8bpp paletted
reg [31:0] pal_seq = 0;
reg        pal_loaded = 0;
reg [31:0] frame_cnt = 0;

reg [63:0] ctrl_w0, ctrl_w1;

//////////////////////////////////////////////////////////////////
// Memories

// Both are written on clk and read on clk_vid.
// line buffer: two banks of 256 x 64-bit words (40 used at 320 pixels of
// 8bpp, all of them at 1024 pixels of RGB565)
reg [63:0] lbuf[0:511];
reg  [8:0] lbuf_waddr;
reg        lbuf_we;
reg [63:0] lbuf_wdata;
reg [63:0] lbuf_q;
always @(posedge clk) if (lbuf_we) lbuf[lbuf_waddr] <= lbuf_wdata;
always @(posedge clk_vid) lbuf_q <= lbuf[{vc[0], v_fmt16 ? hc[9:2] : {1'b0, hc[9:3]}}];

// palette: 128 x 64-bit words, two 0x00RRGGBB entries per word
reg [63:0] pal[0:127];
reg  [6:0] pal_waddr;
reg        pal_we;
reg [63:0] pal_wdata;
reg [63:0] pal_q;
reg  [2:0] byte_sel;
reg        pal_hi;
wire [7:0] pix_index = lbuf_q[{byte_sel, 3'b0} +: 8];
always @(posedge clk) if (pal_we) pal[pal_waddr] <= pal_wdata;
always @(posedge clk_vid) begin
	pal_q  <= pal[pix_index[7:1]];
	pal_hi <= pix_index[0];
end

// RGB565 pixels skip the palette
reg [15:0] pix16;
always @(posedge clk_vid) pix16 <= lbuf_q[{byte_sel[1:0], 4'b0} +: 16];

//////////////////////////////////////////////////////////////////
// Picture without a game
//
// A logo of 244x64 in the middle of the screen, on a background of one
// colour, and the text "danik HCF v<VERSION>" in the bottom right corner, 10
// pixels from the edges: a 15kHz screen does not show all of the picture.
// tools/make_wall.py makes the three files. The core is in 320x240 then, but
// this works in every mode: the picture is laid out on a screen of 320
// pixels in a line, with 2x2 pixels of the mode for one of it from 640x400
// on (800x600 and 1024x768 get 400x300 and 512x384 of them).

localparam [23:0] WALL_BG  = 24'h7F30A0;   // BACKGROUND of make_wall.py
localparam [23:0] WALL_INK = 24'hEFEFEF;   // the text

reg  [3:0] wall[0:15615];     // 244 x 64, 4 bits per pixel
reg [23:0] wall_pal[0:15];
reg [95:0] wall_text[0:7];    // 96 x 7 and an empty row, bit 0 is the leftmost pixel
initial begin
	$readmemh(WALL, wall);
	$readmemh(WALL_PAL, wall_pal);
	$readmemh(WALL_TEXT, wall_text);
end

// position on the screen the picture is laid out on, and its size
wire        wall_x2 = w640 | v_hi;
wire        wall_y2 = v_fast | v_hi;
wire  [9:0] wall_hc = wall_x2 ? hc[10:1] : hc[9:0];
wire  [9:0] wall_vc = wall_y2 ? {1'b0, vc[9:1]} : vc;
wire  [9:0] wall_w  = !v_hi ? 10'd320 : xl ? 10'd512 : 10'd400;
wire  [9:0] wall_h  = v_hi ? (xl ? 10'd384 : 10'd300) : l240 ? 10'd240 : 10'd200;
// relative to the logo and to the text
wire  [9:0] logo_x  = wall_hc - (wall_w - 10'd244) / 2;
wire  [9:0] logo_y  = wall_vc - (wall_h - 10'd64) / 2;
wire  [9:0] text_x  = wall_hc - (wall_w - 10'd106);
wire  [9:0] text_y  = wall_vc - (wall_h - 10'd17);

// The row is worked out a clock after the line starts, which no pixel of the
// logo or the text is near. Then, as for a pixel of the game: the address,
// the pixel of the logo, and its colour when the output takes it.
reg         logo_row_in, text_row_in;
reg  [13:0] logo_row;         // address of the first pixel of the row
reg  [95:0] text_row;
reg         logo_in, logo_in_d, text_on, text_on_d;
reg  [13:0] wall_addr;
reg   [3:0] wall_q;
wire [23:0] wall_rgb = logo_in_d ? wall_pal[wall_q] : text_on_d ? WALL_INK : WALL_BG;

always @(posedge clk_vid) begin
	logo_row_in <= (logo_y < 10'd64);
	logo_row    <= logo_y[5:0] * 8'd244;
	text_row_in <= (text_y < 10'd7);
	text_row    <= wall_text[text_y[2:0]];

	logo_in   <= logo_row_in && (logo_x < 10'd244);
	wall_addr <= logo_row + logo_x[7:0];
	text_on   <= text_row_in && (text_x < 10'd96) && text_row[text_x[6:0]];

	logo_in_d <= logo_in;
	wall_q    <= wall[wall_addr];
	text_on_d <= text_on;
end

//////////////////////////////////////////////////////////////////
// Audio
//
// 64 word FIFO (128 frames) refilled from the DDR ring in bursts of 16
// words. Samples leave at exactly 44.1kHz: 50MHz * 441 / 500000.

reg [63:0] afifo[0:63];
reg  [6:0] afifo_wr = 0;      // one extra bit for full/empty
reg  [6:0] afifo_rd = 0;
reg        afifo_we;
reg [63:0] afifo_wdata;
reg [63:0] afifo_q;
reg [31:0] aud_fetch = 0;     // frames fetched from the ring
reg        aud_half = 0;
reg [18:0] aud_acc = 0;
reg        aud_tick = 0;
reg        aud_tick2 = 0;

wire [6:0] afifo_level = afifo_wr - afifo_rd;

always @(posedge clk) begin
	if (afifo_we) begin
		afifo[afifo_wr[5:0]] <= afifo_wdata;
		afifo_wr <= afifo_wr + 1'd1;
	end
	afifo_q <= afifo[afifo_rd[5:0]];
end

always @(posedge clk) begin
	aud_tick <= 0;
	if (aud_acc + 19'd441 >= 19'd500000) begin
		aud_acc  <= aud_acc + 19'd441 - 19'd500000;
		aud_tick <= 1;
	end else begin
		aud_acc <= aud_acc + 19'd441;
	end

	// afifo_q is valid one clock after afifo_rd changes; ticks are ~1134 clocks apart
	aud_tick2 <= aud_tick;
	if (aud_tick2) begin
		if (!audio_en) begin
			audio_l <= 0;
			audio_r <= 0;
			aud_half <= 0;
		end else if (afifo_level != 0) begin
			{audio_r, audio_l} <= aud_half ? afifo_q[63:32] : afifo_q[31:0];
			aud_half <= ~aud_half;
			if (aud_half) afifo_rd <= afifo_rd + 1'd1;
		end
		// on underrun the last sample is held
	end

	if (!audio_en) afifo_rd <= afifo_wr;
end

//////////////////////////////////////////////////////////////////
// Input state

reg [511:0] keys = 0;
reg         old_key_stb = 0;
reg  [31:0] mouse_x = 0, mouse_y = 0;
reg  [15:0] mouse_wheel = 0;
reg   [2:0] mouse_btn = 0;
reg         old_mouse_stb = 0;

always @(posedge clk) begin
	old_key_stb <= ps2_key[10];
	if (old_key_stb != ps2_key[10]) keys[{ps2_key[8], ps2_key[7:0]}] <= ps2_key[9];

	old_mouse_stb <= ps2_mouse[24];
	if (old_mouse_stb != ps2_mouse[24]) begin
		// 9 bit movement: sign, then 8 bits
		mouse_x     <= mouse_x + {{24{ps2_mouse[4]}}, ps2_mouse[15:8]};
		mouse_y     <= mouse_y + {{24{ps2_mouse[5]}}, ps2_mouse[23:16]};
		mouse_wheel <= mouse_wheel + {{8{ps2_mouse_ext[7]}}, ps2_mouse_ext[7:0]};
		mouse_btn   <= ps2_mouse[2:0];
	end

	if (reset) keys <= 0;
end

//////////////////////////////////////////////////////////////////
// DDR engine

localparam S_IDLE      = 0;
localparam S_CTRL      = 1;
localparam S_CTRL_WAIT = 2;
localparam S_PAL_WAIT  = 3;
localparam S_STAT      = 4;
localparam S_LINE_WAIT = 5;
localparam S_AUD_WAIT  = 6;
localparam S_AUD_PTR   = 7;

reg  [2:0] state = S_IDLE;
reg        line_req = 0;
reg        vbl_req = 0;
reg  [9:0] fetch_row;
reg        fetch_bank;
reg  [7:0] rd_cnt;          // beats received in the current burst
reg  [7:0] rd_len;          // beats expected in the current burst
reg  [7:0] line_words;      // words fetched so far for the current line
reg  [8:0] line_left;       // words of the line still to ask for
reg  [3:0] wr_beat;

// requests of the video side
reg  [2:0] req_s = 0, vbl_s = 0;
reg [19:0] idle_cnt = 0;

// framebuffer address of a row: the framebuffer + row * words in a row, 40
// at 320 pixels of 8bpp. A row of more than 200 words is read in two bursts.
wire  [7:0] row_bytes = (s_mode == 4'd0 || s_mode == 4'd3) ? 8'd40 : (s_mode == 4'd5) ? 8'd100
                      : (s_mode == 4'd6) ? 8'd128 : 8'd80;    // words in a row of 8bpp
wire  [8:0] row_len = fmt16 ? {row_bytes, 1'b0} : {1'b0, row_bytes};
wire  [7:0] line_burst = (row_len > 9'd200) ? 8'd128 : row_len[7:0];
wire [28:0] fb_addr = (s_mode == 4'd6) ? FB_XL_ADDR + {9'd0, fb_index, 18'd0} : FB_ADDR + {10'd0, fb_index, 17'd0};
wire [28:0] row_addr = fb_addr + fetch_row * row_len;

reg [63:0] stat_word;
always @(*) begin
	case (wr_beat)
		4'd0:  stat_word = {frame_cnt, STATUS_MAGIC};
		4'd1:  stat_word = {VERSION, 13'd0, fast, run, vga31, s_mode, vbl_field, ctrl_valid, u_lace, fmt16, 6'd0, fb_index};
		4'd2:  stat_word = {joystick_1, joystick_0};
		4'd3:  stat_word = {joy_r_analog_1, joy_l_analog_1, joy_r_analog_0, joy_l_analog_0};
		4'd4:  stat_word = osd_status;
		4'd5:  stat_word = {mouse_y, mouse_x};
		4'd6:  stat_word = {32'd0, mouse_wheel, 13'd0, mouse_btn};
		4'd7:  stat_word = {32'd0, 3'd0, vclk_count, 8'd0, vclk_code};
		default: stat_word = keys[{wr_beat[2:0], 6'd0} +: 64];
	endcase
end

always @(posedge clk) begin
	lbuf_we <= 0;
	pal_we <= 0;
	afifo_we <= 0;

	// requests from the video timing; what comes with them was set before
	// the request and stays until the next one
	req_s <= {req_s[1:0], req_tgl};
	vbl_s <= {vbl_s[1:0], vbl_tgl};
	if (req_s[2] != req_s[1]) begin
		line_req   <= 1;
		fetch_row  <= req_row;
		fetch_bank <= req_bank;
	end
	// while the video output is off, the control block is still read and the
	// status written, about as often
	idle_cnt <= run ? 20'd0 : idle_cnt + 1'd1;
	if (vbl_s[2] != vbl_s[1] || (&idle_cnt)) begin
		vbl_req   <= 1;
		frame_cnt <= frame_cnt + 1'd1;
	end

	// read data (Avalon readdatavalid is independent of waitrequest)
	if (ddr_dout_ready) begin
		rd_cnt <= rd_cnt + 1'd1;
		case (state)
			S_CTRL_WAIT: if (rd_cnt == 0) ctrl_w0 <= ddr_dout; else ctrl_w1 <= ddr_dout;
			S_PAL_WAIT: begin
				pal_we    <= 1;
				pal_waddr <= rd_cnt[6:0];
				pal_wdata <= ddr_dout;
			end
			S_AUD_WAIT: begin
				afifo_we    <= 1;
				afifo_wdata <= ddr_dout;
			end
			S_LINE_WAIT: begin
				lbuf_we    <= 1;
				lbuf_waddr <= {fetch_bank, line_words};
				lbuf_wdata <= ddr_dout;
				line_words <= line_words + 1'd1;
			end
			default: ;
		endcase
	end

	if (!ddr_busy) begin
		ddr_rd <= 0;

		case (state)
			S_IDLE:
				if (line_req) begin
					line_req     <= 0;
					line_words   <= 0;
					line_left    <= row_len - line_burst;
					rd_cnt       <= 0;
					rd_len       <= line_burst;
					ddr_addr     <= row_addr;
					ddr_burstcnt <= line_burst;
					ddr_rd       <= 1;
					state        <= S_LINE_WAIT;
				end
				else if (audio_en && afifo_level <= 7'd48) begin
					rd_cnt       <= 0;
					rd_len       <= AUD_BURST;
					ddr_addr     <= AUD_ADDR + {16'd0, aud_fetch[13:1]};
					ddr_burstcnt <= AUD_BURST;
					ddr_rd       <= 1;
					state        <= S_AUD_WAIT;
				end
				else if (vbl_req) begin
					vbl_req      <= 0;
					rd_cnt       <= 0;
					rd_len       <= 8'd2;
					ddr_addr     <= CTRL_ADDR;
					ddr_burstcnt <= 8'd2;
					ddr_rd       <= 1;
					state        <= S_CTRL_WAIT;
				end

			S_LINE_WAIT:
				if (rd_cnt == rd_len) begin
					if (line_left != 0) begin
						line_left    <= 0;
						rd_cnt       <= 0;
						rd_len       <= line_left[7:0];
						ddr_addr     <= ddr_addr + rd_len;
						ddr_burstcnt <= line_left[7:0];
						ddr_rd       <= 1;
					end
					else state <= S_IDLE;
				end

			S_AUD_WAIT:
				// wait for the FIFO write of the last beat as well
				if (rd_cnt == rd_len && !afifo_we) begin
					aud_fetch <= aud_fetch + 32'd32;
					state     <= S_AUD_PTR;
				end

			S_AUD_PTR:
				// publish the fetch pointer: single beat write
				if (!ddr_we) begin
					ddr_we       <= 1;
					ddr_addr     <= AUD_PTR;
					ddr_burstcnt <= 8'd1;
					ddr_din      <= {32'd0, aud_fetch};
				end else begin
					ddr_we <= 0;
					state  <= S_IDLE;
				end

			S_CTRL_WAIT:
				if (rd_cnt == rd_len) state <= S_CTRL;

			S_CTRL: begin
				ctrl_valid <= (ctrl_w0[31:0] == CTRL_MAGIC);
				audio_en <= (ctrl_w0[31:0] == CTRL_MAGIC) && ctrl_w0[56];
				menumask <= (ctrl_w0[31:0] == CTRL_MAGIC) ? ctrl_w0[63:60] : 4'd0;
				if (ctrl_w0[31:0] == CTRL_MAGIC) begin
					// interlaced, both fields are of the same frame: a new one
					// is taken when the field with the odd rows has been shown
					if (!(u_lace && run && vbl_field))
						fb_index <= (ctrl_w0[39:32] > 8'd2) ? 2'd0 : ctrl_w0[33:32];
					fmt16    <= ctrl_w0[40];
					// a mode the core does not have is 320x200
					vmode    <= (ctrl_w0[47:44] > 4'd7) ? 4'd0 : ctrl_w0[47:44];
				end
				// no game: the core's picture, in 320x240
				else vmode <= 4'd3;
				if (ctrl_w0[31:0] == CTRL_MAGIC && (!pal_loaded || ctrl_w1[31:0] != pal_seq)) begin
					pal_loaded   <= 1;
					pal_seq      <= ctrl_w1[31:0];
					rd_cnt       <= 0;
					rd_len       <= 8'd128;
					ddr_addr     <= PAL_ADDR + (ctrl_w0[48] ? 29'h80 : 29'h0);
					ddr_burstcnt <= 8'd128;
					ddr_rd       <= 1;
					state        <= S_PAL_WAIT;
				end else begin
					state <= S_STAT;
					wr_beat <= 0;
				end
			end

			S_PAL_WAIT:
				if (rd_cnt == rd_len) begin
					state   <= S_STAT;
					wr_beat <= 0;
				end

			S_STAT: begin
				// write burst: first beat carries address and burst count
				if (!ddr_we) begin
					ddr_we       <= 1;
					ddr_addr     <= STAT_ADDR;
					ddr_burstcnt <= 8'd16;
					ddr_din      <= stat_word;
					wr_beat      <= 1;
				end else if (wr_beat == 0) begin
					// beat 15 accepted on the previous cycle
					ddr_we <= 0;
					state  <= S_IDLE;
				end else begin
					ddr_din <= stat_word;
					wr_beat <= wr_beat + 1'd1;
				end
			end

			default: state <= S_IDLE;
		endcase
	end

	if (reset) begin
		state      <= S_IDLE;
		ddr_rd     <= 0;
		ddr_we     <= 0;
		line_req   <= 0;
		vbl_req    <= 0;
		ctrl_valid <= 0;
		pal_loaded <= 0;
		audio_en   <= 0;
		menumask   <= 0;
	end
	if (!audio_en) aud_fetch <= 0;
end

//////////////////////////////////////////////////////////////////
// Pixel pipeline (clk_vid)
//
// ce edge E0: counters advance. One clock later the line buffer word is
// read, another one later the palette (or the RGB565 pixel is selected, or
// the pixel of the core's own picture is there).
// RGB and syncs for the same pixel are registered at the first ce after
// that: the next one at 15kHz, the second one at 31kHz, where a ce comes
// every other clock, and the third one where every clock has one. Blanking
// and syncs wait one or two ce for that.

wire       hbl_now    = (hc >= H_ACTIVE);
wire       vbl_now    = (vc >= V_ACTIVE);
wire       hs_now     = (hc >= HS_START && hc < HS_END);
wire       vs_now     = (vc >= VS_START && vc < VS_END);

wire [3:0] now = {hbl_now, vbl_now, hs_now, vs_now};
reg  [3:0] now_d1, now_d2;
wire [3:0] out = v_hi ? now_d2 : v_fast ? now_d1 : now;
wire       o_hbl, o_vbl, o_hs, o_vs;
assign {o_hbl, o_vbl, o_hs, o_vs} = out;

always @(posedge clk_vid) byte_sel <= hc[2:0];

always @(posedge clk_vid) begin
	ce_pix <= ce;
	if (ce) begin
		now_d1 <= now;
		now_d2 <= now_d1;

		hblank <= o_hbl;
		vblank <= o_vbl;
		hsync  <= o_hs;
		// interlaced: half a line later in the field with the even rows
		if (v_lace && field) begin
			if (hc == HS_HALF) vsync <= vs_now;
		end else begin
			vsync <= o_vs;
		end

		if (o_hbl | o_vbl | v_off) begin
			{r, g, b} <= 24'd0;
		end else if (!v_valid) begin
			// the core's picture until the HPS side publishes a valid control block
			{r, g, b} <= wall_rgb;
		end else if (v_fmt16) begin
			r <= {pix16[15:11], pix16[15:13]};
			g <= {pix16[10:5], pix16[10:9]};
			b <= {pix16[4:0], pix16[4:2]};
		end else begin
			{r, g, b} <= pal_hi ? pal_q[55:32] : pal_q[23:0];
		end
	end

	// off: no sync, black
	if (vrst) begin
		now_d1 <= 4'b1100;
		now_d2 <= 4'b1100;
		hblank <= 1;
		vblank <= 1;
		hsync  <= 0;
		vsync  <= 0;
		{r, g, b} <= 24'd0;
	end
end

endmodule
