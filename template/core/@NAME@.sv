//============================================================================
//
//  @NAME@ (@TITLE@) hybrid core for MiSTer
//
//  The game itself runs on the HPS. This core scans the game's
//  framebuffer out of DDR3 with native 15kHz timings, plays its audio and
//  hands input to the HPS. See ../hybrid/rtl/hybrid_host.sv for the shared
//  memory layout and ../hybrid/README.md for what all hybrid cores share.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//============================================================================

module emu
(
	`include "sys/emu_ports.vh"
);

///////// Default values for ports not used in this core /////////

assign ADC_BUS  = 'Z;
assign USER_OUT = '1;
assign {UART_RTS, UART_TXD, UART_DTR} = 0;
assign {SD_SCK, SD_MOSI, SD_CS} = 'Z;
assign {SDRAM_DQ, SDRAM_A, SDRAM_BA, SDRAM_CLK, SDRAM_CKE, SDRAM_DQML, SDRAM_DQMH, SDRAM_nWE, SDRAM_nCAS, SDRAM_nRAS, SDRAM_nCS} = 'Z;

assign VGA_SCALER  = 0;
assign VGA_DISABLE = 0;
assign HDMI_FREEZE = 0;
assign HDMI_BLACKOUT = 0;
assign HDMI_BOB_DEINT = 0;

assign AUDIO_S = 1;
assign AUDIO_MIX = status[6:5];

assign LED_DISK = 0;
assign LED_POWER = 0;
assign LED_USER = 0;
assign BUTTONS = 0;

//////////////////////////////////////////////////////////////////

// Status bits [63:0] are forwarded to the HPS side. Bits [23:0] mean the same
// in every hybrid core (MH_OSD_* in hybrid/hps/mister_hybrid.h), game options
// start at bit 24 (decoded in the game's MiSTer source file). Defaults are 0.
// The order of the options is the one in hybrid/README.md, "OSD".
`include "build_id.v"
localparam CONF_STR = {
	"@NAME@;;",
	"-;",
	"O[122:121],Aspect ratio,Original,Full Screen,[ARC1],[ARC2];",
	"O[125:123],Scale,Normal,V-Integer,Narrower HV-Integer,Wider HV-Integer,HV-Integer;",
	"O[4:2],Scandoubler Fx,None,HQ2x,CRT 25%,CRT 50%,CRT 75%;",
	"O[6:5],Stereo Mix,None,25%,50%,100%;",
	"-;",
	// Sound Volume / Music Volume (bits 10:7 and 14:11) go here if the game
	// mixes them separately, then the game's own options from bit 24 up,
	// followed by "-;". The first entry of an option is its default.
	"O[19:16],Menu OK,MiSTer,A,B,X,Y,L,R,Select,Start;",
	"O[23:20],Menu Back,MiSTer,A,B,X,Y,L,R,Select,Start;",
	"-;",
	// MiSTer's default map only knows the SNES-style pad. Main refuses to map a
	// button twice, so Menu OK/Back here are for spare buttons without a game
	// function and unmapped by default; the OSD Menu OK/Back options cover
	// buttons that have one. J1 has the game's eight most important actions,
	// the last one opens the game's menu; jn gives each of them a button of the
	// default pad, Start for the menu. The game knows this list as buttons 0..9
	// of the SDL joystick, the launcher repeats "jn" as MISTER_HYBRID_JN
	// (package/games/@NAME@/danik_hybrid_launch.sh).
	"J1,Action 1,Action 2,Action 3,Action 4,Action 5,Action 6,Action 7,Menu,Menu OK,Menu Back;",
	"jn,A,B,X,Y,L,R,Select,Start;",
	"V,v",`BUILD_DATE
};

wire         forced_scandoubler;
wire   [1:0] buttons;
wire [127:0] status;
wire  [10:0] ps2_key;
wire  [24:0] ps2_mouse;
wire  [15:0] ps2_mouse_ext;
wire  [31:0] joystick_0, joystick_1;
wire  [15:0] joy_l_analog_0, joy_r_analog_0, joy_l_analog_1, joy_r_analog_1;
wire  [21:0] gamma_bus;

hps_io #(.CONF_STR(CONF_STR)) hps_io
(
	.clk_sys(clk_sys),
	.HPS_BUS(HPS_BUS),
	.EXT_BUS(),
	.gamma_bus(gamma_bus),

	.forced_scandoubler(forced_scandoubler),

	.buttons(buttons),
	.status(status),
	.status_menumask(16'd0),

	.joystick_0(joystick_0),
	.joystick_1(joystick_1),
	.joystick_l_analog_0(joy_l_analog_0),
	.joystick_r_analog_0(joy_r_analog_0),
	.joystick_l_analog_1(joy_l_analog_1),
	.joystick_r_analog_1(joy_r_analog_1),

	.ps2_key(ps2_key),
	.ps2_mouse(ps2_mouse),
	.ps2_mouse_ext(ps2_mouse_ext)
);

///////////////////////   CLOCKS   ///////////////////////////////

wire clk_sys;
pll pll
(
	.refclk(CLK_50M),
	.rst(0),
	.outclk_0(clk_sys)
);

wire reset = RESET | buttons[1];

//////////////////////////////////////////////////////////////////

wire       ce_pix;
wire [7:0] r, g, b;
wire       hsync, vsync, hblank, vblank;

hybrid_host host
(
	.clk(clk_sys),
	.reset(reset),

	.ddr_busy(DDRAM_BUSY),
	.ddr_burstcnt(DDRAM_BURSTCNT),
	.ddr_addr(DDRAM_ADDR),
	.ddr_dout(DDRAM_DOUT),
	.ddr_dout_ready(DDRAM_DOUT_READY),
	.ddr_rd(DDRAM_RD),
	.ddr_din(DDRAM_DIN),
	.ddr_be(DDRAM_BE),
	.ddr_we(DDRAM_WE),

	.joystick_0(joystick_0),
	.joystick_1(joystick_1),
	.joy_l_analog_0(joy_l_analog_0),
	.joy_r_analog_0(joy_r_analog_0),
	.joy_l_analog_1(joy_l_analog_1),
	.joy_r_analog_1(joy_r_analog_1),
	.osd_status(status[63:0]),
	.ps2_key(ps2_key),
	.ps2_mouse(ps2_mouse),
	.ps2_mouse_ext(ps2_mouse_ext),

	.ce_pix(ce_pix),
	.r(r),
	.g(g),
	.b(b),
	.hsync(hsync),
	.vsync(vsync),
	.hblank(hblank),
	.vblank(vblank),

	.audio_l(AUDIO_L),
	.audio_r(AUDIO_R)
);

assign DDRAM_CLK = clk_sys;

//////////////////////////////////////////////////////////////////
// Video output. The native signal is 15kHz, in the video mode the game set
// (320x200 unless it asks hybrid_host for another, which is why the line
// buffer below has room for the widest one); video_mixer scandoubles it
// for VGA monitors (forced_scandoubler) or when a scandoubler effect is
// selected. Analog output is untouched otherwise so CRTs get the real
// 15kHz signal. HDMI goes through the framework's scaler; video_freak
// gives it the aspect ratio and the integer scaling modes.

wire [1:0] ar = status[122:121];
wire [2:0] scale = status[4:2];
wire [2:0] sl = scale ? scale - 1'd1 : 3'd0;
wire       vga_de;

assign CLK_VIDEO = clk_sys;
assign VGA_SL = sl[1:0];
assign VGA_F1 = 0;

video_mixer #(.LINE_LENGTH(640), .HALF_DEPTH(0), .GAMMA(1)) video_mixer
(
	.CLK_VIDEO(CLK_VIDEO),
	.CE_PIXEL(CE_PIXEL),
	.ce_pix(ce_pix),

	.scandoubler(scale || forced_scandoubler),
	.hq2x(scale == 1),
	.gamma_bus(gamma_bus),

	.R(r),
	.G(g),
	.B(b),

	.HSync(hsync),
	.VSync(vsync),
	.HBlank(hblank),
	.VBlank(vblank),

	.HDMI_FREEZE(HDMI_FREEZE),
	.freeze_sync(),

	.VGA_R(VGA_R),
	.VGA_G(VGA_G),
	.VGA_B(VGA_B),
	.VGA_VS(VGA_VS),
	.VGA_HS(VGA_HS),
	.VGA_DE(vga_de)
);

video_freak video_freak
(
	.CLK_VIDEO(CLK_VIDEO),
	.CE_PIXEL(CE_PIXEL),
	.VGA_VS(VGA_VS),
	.HDMI_WIDTH(HDMI_WIDTH),
	.HDMI_HEIGHT(HDMI_HEIGHT),
	.VGA_DE(VGA_DE),
	.VIDEO_ARX(VIDEO_ARX),
	.VIDEO_ARY(VIDEO_ARY),

	.VGA_DE_IN(vga_de),
	.ARX((!ar) ? 12'd4 : (ar - 1'd1)),
	.ARY((!ar) ? 12'd3 : 12'd0),
	.CROP_SIZE(0),
	.CROP_OFF(0),
	.SCALE(status[125:123])
);

endmodule
