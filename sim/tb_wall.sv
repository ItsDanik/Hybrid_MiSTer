// The picture hybrid_host shows without a game, as it comes out of the core:
// frames of its video output written to PPM files (640x480, the two fields of
// 480i woven, or 480p with VGA31=1), <OUT>_<n>.ppm. Without START, one frame
// once the intro is over; with START, FRAMES frames from that field of the
// intro on (the core's own counters, started there). MSG: a file of the 41
// words of a message block (0x001800) to show; TIMEOUT 1: the core's own
// message, that no game came. For trying a picture before a core is built:
// sim/wall_shot.sh, sim/wall_gif.sh.
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
	parameter TIMEOUT   = 0
);

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
initial begin
	for (at = 0; at < 41; at = at + 1) msg[at] = 0;
	if (MSG != "") $readmemh(MSG, msg);
end
always @(posedge clk) begin
	ddr_dout_ready <= 0;
	if (ddr_rd) begin
		beats = ddr_burstcnt;
		at = ddr_addr - 29'h06000300;
	end
	else if (beats != 0) begin
		ddr_dout_ready <= 1;
		ddr_dout <= (at >= 0 && at < 41) ? msg[at] : 64'd0;
		at = at + 1;
		beats = beats - 1;
	end
end

wire clk_vid, ce_pix, hsync, vsync, hblank, vblank, hires, fast, f1;
wire [7:0] r, g, b;

hybrid_host #(.WALL(WALL), .WALL_PAL(WALL_PAL), .WALL_DOTS(WALL_DOTS), .WALL_EASE(WALL_EASE),
	.WALL_FONT("rtl/hybrid_wall_font.hex"), .WALL_ALONE("rtl/hybrid_wall_alone.hex"), .WALL_TIMEOUT(TIMEOUT ? 2 : 716)) dut
(
	.clk(clk), .reset(reset), .refclk(clk),
	.ddr_busy(1'b0), .ddr_burstcnt(ddr_burstcnt), .ddr_addr(ddr_addr), .ddr_dout(ddr_dout),
	.ddr_dout_ready(ddr_dout_ready), .ddr_rd(ddr_rd), .ddr_din(ddr_din), .ddr_be(ddr_be), .ddr_we(ddr_we),
	.joystick_0(32'd0), .joystick_1(32'd0),
	.joy_l_analog_0(16'd0), .joy_r_analog_0(16'd0), .joy_l_analog_1(16'd0), .joy_r_analog_1(16'd0),
	.osd_status(128'd0), .ps2_key(11'd0), .ps2_mouse(25'd0), .ps2_mouse_ext(16'd0),
	.clk_vid(clk_vid), .ce_pix(ce_pix), .r(r), .g(g), .b(b), .hsync(hsync), .vsync(vsync), .hblank(hblank), .vblank(vblank),
	.vga31(VGA31 != 0), .crt_hsize(6'd0), .crt_hpos(4'd0), .crt_vpos(4'd0),
	.hires(hires), .fast(fast), .f1(f1), .menumask(),
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
			if (row < 480) frame[row * 640 + px] = {r, g, b};
			seen = seen + 1;
		end
		px = px + 1;
	end
	if (hblank && !old_hblank && !vblank) py = py + 1;
	if (vblank && !old_vblank) begin
		fields = fields + 1;
		rows = py;
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
		capture = 1;
		wait_fields(lace ? 2 : 1);
		capture = 0;
		$sformat(name, "%0s_%03d.ppm", OUT, n);
		f = $fopen(name, "wb");
		$fwrite(f, "P6\n640 480\n255\n");
		for (i = 0; i < 640 * 480; i = i + 1) $fwrite(f, "%c%c%c", frame[i][23:16], frame[i][15:8], frame[i][7:0]);
		$fclose(f);
	end
	$display("tb_wall: %0d frames of %0d pixels, %0d rows per field, %s, intro field %0d at the end", FRAMES, seen, rows,
		lace ? "480i" : "480p", dut.intro_t);
	$finish;
end

endmodule
