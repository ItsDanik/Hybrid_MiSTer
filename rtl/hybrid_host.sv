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
//  640x480 and shows a picture of its own: a logo in the middle of the screen
//  and the version of this module in the bottom right corner (see "Picture
//  without a game" below). It comes with an intro when the core starts,
//  which a game waits for, and fades out before the game gets the screen
//  (with INTRO 0, a frontend's: neither).
//
//  A game is attached while the control block has its magic. One that was
//  killed, or running when the MiSTer was reset, leaves that behind, and the
//  memory keeps it: the core clears the magic when it starts (ctrl_clear).
//
//  A reset once the core has started (the USER button, or the OSD's Reset
//  line: status bit 0, R0; host version 18) is the game's, as in every other
//  MiSTer core: the core's picture comes back at once, as at the end of its
//  intro (no intro again), the magic is cleared as at the start, and status
//  word 1 bits 7..4 count up, at which the HPS side ends the program at once
//  (exit code 44) and the launch script starts the same game again. A button
//  held down is one reset: after the start only its press resets, so the
//  status goes on being written (a game that sees it stand still takes the
//  core for gone). The reset while the core loads is the core's own.
//
//  Four clocks: `clk` (50MHz) for the DDR3 side, audio and input,
//  `clk_vid` for the video output, made here by hybrid_vclk.sv, the MIDI
//  synthesizer's 100MHz (hybrid_midi.sv, its own PLL) and the framework's
//  CLK_AUDIO for the MT32-pi's module.
//
//  An MT32-pi (an external MIDI synthesizer on a Raspberry Pi, through the
//  user port, host version 15) is handled here as well: see "MT32-pi" below.
//
//  Shared memory layout (physical address 0x30000000 + offset):
//    0x000000  control (HPS -> FPGA), 3 x 64-bit words
//              w0[31:0]  magic "MHYB" (0x4259484D)
//              w0[39:32] framebuffer index to display (0..2)
//              w0[40]    pixel format: 0 = 8bpp paletted, 1 = RGB565
//              w0[47:44] video mode: 0 = 320x200, 1 = 640x200, 2 = 640x400,
//                        3 = 320x240, 4 = 640x480, 7 = 640x240 (5 and 6
//                        were 800x600 and 1024x768 up to host version 12)
//              w0[48]    palette slot (0..1)
//              w0[55:49] FM chip volume: 32 = as it comes, 127 = four times
//              w0[56]    audio enable
//              w0[57]    FM chip enable (the chip is in reset without it,
//                        and without audio enable)
//              w0[58]    overlay (host version 17): the frame is drawn over
//                        the core's picture, which stays, instead of taking
//                        the screen; 8bpp 640x480, index 0 transparent (see
//                        "The overlay" below)
//              w0[59]    0
//              w0[63:60] OSD menu mask: bits a game sets to hide or grey out
//                        lines of the core's CONF_STR (H/h/D/d 0..3)
//              w1[31:0]  palette sequence number (reload when changed)
//              w1[63:32] number of entries written to the FM ring so far
//              w2 (host version 15; the core clears it when it starts, so a
//              game built before, which never writes it, leaves it 0)
//                  [31:0]  commands written to the MIDI ring so far (host
//                          version 16)
//                  [38:32] MIDI volume: the MT32-pi's sound and the
//                          synthesizer's, 32 = as it comes, 127 = four times
//                  [39]    MIDI enable (with audio enable): MIDI messages of
//                          the FM ring go to the MT32-pi, and its sound is
//                          in the mix; off, every voice of the synthesizer is
//                          silent and its commands read count from 0
//                  [47:40] info text, [55:48] info request count (host
//                          version 16): when the count changes and the text
//                          is not 0, Main_MiSTer's info box shows that text
//                          of the core's "I," line, once the OSD menu is
//                          closed
//                  [56]    the program, not the OSD, decides where the MIDI
//                          music goes (host version 17): [57] 1 to the
//                          MT32-pi (while one answers), 0 to the synthesizer
//                  [58]    the overlay goes out (host version 17): its
//                          fade-in backwards, then it stays out (status
//                          bit 30) until the bit is cleared, when it comes
//                          in again: the program changes its frame and
//                          palette meanwhile (the game's logo to the MIDI
//                          Player's page)
//                  [59]    the overlay rises 16 pixels into place as it
//                          comes in, and sinks as it goes out (the game's
//                          logo); without it, it only fades
//                  [60]    field rate (host version 18): interlaced, a new
//                          frame is taken at every field, not only after
//                          the field with the odd rows. Each field shows its
//                          rows of the newest frame: 59.6 pictures a second
//                          on a CRT, where the HDMI scaler's weave combs what
//                          moves (for a menu, docs/field_rate_prompt.md).
//                          Status word 1 bit 31 says it is taken so
//                  [61]    the OSD's Flicker Filter line (host version 18,
//                          FLICKER) is to be set to [63:62] (0 Off, 1 Light,
//                          2 Strong) while this differs from status word 6
//                          bit 34, as if the player had picked it: the core
//                          writes it to Main_MiSTer's status (hps_io's
//                          status_set) and takes this bit there. A program
//                          keeps it as status word 6 bit 34 has it, and asks
//                          by sending the other value
//                  [63:62] the Flicker Filter line asked for
//    0x000040  status (FPGA -> HPS), 16 x 64-bit words, written every vblank
//              w0  {frame counter, magic "MHYS" (0x5359484D)}
//              w1  {version, field rate, overlay out, overlay, synth, reports[3:0], mt32, osd, 2'b(messages), game, fast, running, vga31, mode[3:0],
//                   field, ctrl_valid, lace, format, resets[3:0], filter on, filter, fb_index[1:0]}
//                   filter: the core has the flicker filter (host version
//                   18, FLICKER); filter on: the OSD's Flicker Filter line
//                   (status bits 106:105: 0 Light, the default, 1 Strong,
//                   2 Off) is Light or Strong (which: w6).
//                   The filter acts on a game's picture while it is
//                   interlaced, RGB565 or 8bpp, never on the core's own:
//                   each row Strong a quarter of the row above, half its
//                   own and a quarter of the row below; Light an eighth,
//                   three quarters and an eighth. RGB565 has the bits that
//                   adds dithered away (Strong 4x4 as the program would do
//                   it, dnkLauncher's out_box(); Light 4x2), 8bpp rounds
//                   after the palette.
//                   resets: counted up at each reset of the game (host
//                   version 18): the USER button or the OSD's Reset line,
//                   once the core has started.
//                   lace: the mode is shown interlaced.
//                   field: the field that was on the screen had the even
//                   rows (the core takes a new frame when it is 0).
//                   mode: the mode shown; 0 while the one asked for is not
//                   available.
//                   vga31: 0 (up to host version 12: 800x600 and
//                   1024x768 are available, which no core has any more).
//                   bit 20: a message (0x001800) is shown; bit 21: the
//                   core's own, that no game came (host version 13).
//                   bit 22: the OSD menu is open (host version 14).
//                   Main_MiSTer passes no buttons and keys to the core
//                   while it is, and the HPS side holds back what it reads
//                   from Linux itself (triggers, stick clicks, the physical
//                   Menu OK / Back) too. 0 for its info boxes (volume, a
//                   controller connected).
//                   bit 23: an MT32-pi answers on the user port (host
//                   version 15); cleared a second after it stops.
//                   bits 27..24: counted up each time the MT32-pi reports
//                   what it plays: after a reset, a change of synth, ROM or
//                   SoundFont (when it has loaded it), and after "Reset
//                   Hanging Notes". It then has none of the song's settings:
//                   the game sends them again (MH_MidiSynthChanged()).
//                   bit 28: the core has the MIDI synthesizer (host version
//                   16; MIDI_SYNTH). Without it the MIDI music goes to an
//                   MT32-pi, or the game plays its FM music.
//                   bit 29: the overlay is shown (host version 17): its
//                   handover has begun. bit 30: it is out, as w2[58] asked
//                   (its level 0).
//                   bit 31: field rate (host version 18): the mode is
//                   interlaced and a new frame is taken at every field, as
//                   w2[60] asked; 0 otherwise, and in every older core.
//                   running: 0 while the video output is off for a change
//                   of the video clock.
//                   fast: the mode is shown at 31kHz or more.
//                   game: the game has the screen. 0 while the core shows
//                   its own picture, which it still does after ctrl_valid
//                   became 1: to the end of its intro (4.1 seconds from the
//                   intro's start at most), then 0.7 seconds of fading out
//                   (the mode is 640x480 until then). With INTRO 0 at the
//                   next frame (one field, two interlaced without field
//                   rate).
//              w2  {joystick_1, joystick_0}
//              w3  {r_analog_1, l_analog_1, r_analog_0, l_analog_0}
//              w4  OSD status[63:0], but bits 59..56: a trigger's toggle
//                  (host version 13): an OSD trigger ("T[56]".."T[59]") is
//                  a pulse of microseconds, which a game reading the status
//                  once a field would miss; the core flips the bit at each
//                  one instead. Bit 16 the same for "Save Diagnostic Report"
//                  (host version 16, docs/diagnostics.md), flipped at the
//                  pulse's end: a saved setting of the old Menu OK may hold
//                  the bit at 1, and the trigger then only clears it
//              w5  {mouse y accumulator, mouse x accumulator}
//              w6  {video lines late[15:0], 13'b0, filter toggle, flicker filter[1:0],
//                   wheel accumulator[15:0], 13'b0, mouse buttons[2:0]}: a
//                   line is late when the next one is asked for before its
//                   read is done (host version 16; counts on, wrapping); the
//                   OSD's Flicker Filter line, 0 Off, 1 Light, 2 Strong (host
//                   version 18, 0 without FLICKER), and the toggle of the last
//                   request to set it that was taken (control word 2 bit 61)
//              w7  {FM ring entries read so far[31:0], 3'b0, video clock
//                   measured[12:0], 8'b0, video clock asked for[7:0]}: the
//                   clock asked for in units of 0.5MHz, the one measured in
//                   units of 1/80 MHz
//              w8..w15 keyboard bitmap, bit index = {extended, PS/2 set 2 code}
//    0x0000C0  audio fetch pointer (FPGA -> HPS), 32-bit count of stereo
//              frames read from the ring, written after every audio fetch
//    0x0000C8  OSD status[127:64] (FPGA -> HPS), written every vblank after
//              the status block (host version 13): the CRT Options page,
//              with the HUD margins at bits 77..81 and 82..86
//    0x0000D0  MIDI status (FPGA -> HPS, host version 16), written every
//              vblank after that: w0 {the MT32-pi's synth[7:0], voices
//              playing, cache underruns[15:0], commands read [31:0]}, w1 and
//              w2 the voices playing
//              (bit v of voice v), w3 [31:0] the audio frame they are of.
//              The MT32-pi's synth (host version 17): 0 none reported (or no
//              Pi), else as the info texts number it: 1..8 SoundFont 0..7,
//              9..11 MT-32 v1, v2, CM-32L, 12 unknown, 13 a SoundFont after
//              the eighth
//    0x001000  palette slot 0: 256 x 32-bit 0x00RRGGBB
//    0x001400  palette slot 1
//    0x001800  message (HPS -> FPGA, host version 13), shown under the logo
//              while no game has the screen: w0 {12'b0, selected[1:0], list,
//              error, sequence number[15:0], magic "MHMS" (0x534D484D)},
//              then 4 lines of 80 bytes (w1..w40): characters 32..126 of the
//              font, the first 71 of a line shown, anything else blank. The
//              core reads the lines again when the sequence number changes,
//              and clears the magic when it starts. error: the dots stand
//              still. list: the lines are a list to pick from (the dots stand
//              still), the line `selected` on a pink bar, the last character
//              of a line (a mark that there is more) dimmed.
//    0x010000  audio ring: 16384 stereo frames, 16-bit signed {R, L}, 44.1kHz
//    0x020000  FM ring: 4096 register writes for the FM chip (hybrid_fm.sv),
//              64 bits each: [31:0] the audio frame to make it at, counted
//              as the audio fetch pointer counts, [39:32] value, [47:40]
//              register. The chip's sound is added to the audio ring's.
//              The ring also carries MIDI messages (host version 15): an
//              entry with [57:56] = n, 1..3, is n bytes of MIDI for the
//              MT32-pi, [39:32], [47:40] and [55:48] in that order, sent at
//              its audio frame. A register write has [63:48] 0. Entries of a
//              kind that is not enabled are skipped. The ring is read while
//              audio is enabled with the FM chip or MIDI or both.
//    0x030000  MIDI ring (host version 16): 1024 commands of 16 words for
//              the synthesizer (hybrid_midi.sv, docs/midi_synth.md); its
//              sound is added too
//    0x100000  framebuffer 0 (row stride = width of the video mode in pixels;
//              an interlaced mode has all its rows here, as a progressive one)
//    0x200000  framebuffer 1
//    0x300000  framebuffer 2
//    (0x400000 to 0x9FFFFF were the framebuffers of 1024x768 up to host
//    version 12)
//    0x1000000 the MIDI synthesizer's samples (host version 16): 64 MB of
//              16-bit samples, sample n at byte 2n
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//============================================================================

module hybrid_host
#(
	// the picture without a game, its colours, the dots of the logo and the
	// curve of the intro's movements. Quartus reads them from the project's
	// directory, core/
	parameter WALL      = "../hybrid/rtl/hybrid_wall.hex",
	parameter WALL_PAL  = "../hybrid/rtl/hybrid_wall_pal.hex",
	parameter WALL_DOTS = "../hybrid/rtl/hybrid_wall_dots.hex",
	parameter WALL_EASE = "../hybrid/rtl/hybrid_wall_ease.hex",
	// the font of the messages, and the core's own message when no game
	// comes: in that many fields after the core started (12 seconds)
	parameter WALL_FONT    = "../hybrid/rtl/hybrid_wall_font.hex",
	parameter WALL_ALONE   = "../hybrid/rtl/hybrid_wall_alone.hex",
	parameter WALL_TIMEOUT = 716,
	// the MT32-pi counts as gone when it has not polled for that many clocks
	// (1 second; it polls every 50ms)
	parameter MT32_QUIET   = 50_000_000,
	// the MIDI synthesizer (host version 16): 1 in a core whose games play
	// MIDI music, 0 leaves it and its 100MHz PLL out (about 9,500 ALMs, 316
	// M10K and 20 DSP blocks); status word 1 bit 28 says which
	parameter MIDI_SYNTH   = 1,
	// the intro (see "Picture without a game"): 1 in every core. 0 only in a
	// frontend whose first frames take over the picture as it is at the end
	// of the intro (dnkLauncher, docs/dnklauncher.md): that picture from the
	// start, no black and no intro, and the program's first frame on the
	// screen at the next frame, without the picture going out first
	parameter INTRO        = 1,
	// the flicker filter (host version 18; the OSD's Flicker Filter line,
	// status bits 106:105, 0 Light, the default, 1 Strong, 2 Off): 1 in a core with a 640x400 or 640x480 mode, 0 leaves
	// it out (two more line buffers and two more copies of the palette, 12
	// M10K blocks); status word 1 bit 2 says which
	parameter FLICKER      = 0
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
	input     [127:0] osd_status,
	input             osd_open,     // the OSD menu is shown (OSD_STATUS of the framework)
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
	// A screen that takes 31kHz is the only one that gets it: either the
	// analog output is a VGA monitor (forced_scandoubler of the MiSTer
	// framework) or the core switches the analog output off while `fast` is 1
	// ("HDMI only"). 640x400 and 640x480 are then progressive at 31kHz; else
	// they are interlaced at 15kHz.
	input             vga31,
	// CRT options, for the 15kHz timings only. Size -24..+7 in steps of 1% of
	// the pixels in a line, position -8..+7 in steps of 2 pixels of 320 and of
	// 1 line; positive is larger, to the right and down.
	input       [5:0] crt_hsize,
	input       [3:0] crt_hpos,
	input       [3:0] crt_vpos,
	// interlaced or above 15kHz: must not be scandoubled
	output            hires,
	// the signal on the output is 31kHz (follows vga31, and drops only after
	// the output went quiet)
	output            fast,
	// interlaced: the field on the screen is the second one, with the odd
	// rows (VGA_F1 of the MiSTer framework)
	output            f1,
	// OSD menu mask the game set
	output reg  [3:0] menumask,
	// the picture is interlaced (the OSD's Flicker Filter line is greyed out
	// otherwise)
	output            lace,
	// the OSD's status, written back with the Flicker Filter line changed by
	// the program (w2[63:61]): hps_io's status_in / status_set
	output reg [127:0] status_out = 0,
	output reg        status_set = 0,

	// audio, 44.1kHz signed
	output reg [15:0] audio_l,
	output reg [15:0] audio_r,

	// MT32-pi on the user port (MiSTer's sys/mt32pi.sv)
	input             clk_audio,    // CLK_AUDIO of the framework
	input       [6:0] user_in,
	output      [6:0] user_out,
	// an MT32-pi answers: its OSD lines are shown (a bit of status_menumask)
	output            mt32_on,
	// Main_MiSTer's info box (hps_io's info_req / info): text `info` of the
	// CONF_STR's "I" line at a rising edge: the MT32-pi's synth when it has
	// changed, or the game's (MH_OSDInfo())
	output reg        info_req = 0,
	output reg  [7:0] info = 0
);

localparam [31:0] CTRL_MAGIC   = 32'h4259484D; // "MHYB"
localparam [31:0] STATUS_MAGIC = 32'h5359484D; // "MHYS"
// the picture without a game has the version in it: run tools/make_wall.py
localparam [31:0] VERSION      = 32'd18;

localparam [28:0] BASE       = 29'h06000000;   // 0x30000000 >> 3
localparam [28:0] CTRL_ADDR  = BASE;
localparam [28:0] STAT_ADDR  = BASE + 29'h8;
localparam [28:0] PAL_ADDR   = BASE + 29'h200;
localparam [28:0] MSG_ADDR   = BASE + 29'h300;
localparam [31:0] MSG_MAGIC  = 32'h534D484D;    // "MHMS"
localparam [28:0] FB_ADDR    = BASE + 29'h20000;
localparam [28:0] AUD_ADDR   = BASE + 29'h2000;   // ring, 8192 words
localparam [28:0] AUD_PTR    = BASE + 29'h18;
localparam [28:0] STAT_HI    = BASE + 29'h19;      // OSD status[127:64]
localparam  [7:0] AUD_BURST  = 8'd16;             // 32 frames
localparam [28:0] FM_ADDR    = BASE + 29'h4000;   // ring, 4096 words
localparam [28:0] MIDI_ADDR  = BASE + 29'h6000;   // ring, 1024 commands of 16 words
localparam [28:0] MSTAT_ADDR = BASE + 29'h1A;     // MIDI status, 4 words
localparam [28:0] SMP_ADDR   = BASE + 29'h200000; // the MIDI samples

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
//  7    640x240   progressive                  the same
// (5 and 6, 800x600 and 1024x768 at 31kHz and above, are gone since host
// version 13: a game asks for them as for a mode the core does not have)
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
//
// What is on the output is decided here, in the clk domain, as a "class":
// the kind of timing (15kHz progressive, interlaced, 31kHz) and the video
// clock. A change of class (another mode, vga31, the horizontal size)
// stops the video output first (no sync, black), then sets the clock, and
// starts the output again once hybrid_vclk has measured the new clock. So a
// 15kHz screen never gets the lines of another class, not even for a moment.
// Changes within a class (320 or 640 wide, 200 or 240 lines, positions) are
// taken when the next field starts.

reg        game_on = 0;       // the game has the screen
reg        fr_on = 0;         // field rate: interlaced, a frame taken at every field (w2[60])
reg        filt_req = 0;      // the OSD's Flicker Filter line is Light or Strong (FLICKER)
reg  [1:0] filt_lvl = 0;      // the line: 0 Off, 1 Light, 2 Strong
reg        filt_tgl = 0;      // w2[61] of the last request taken
reg  [3:0] vmode = 4'd4;      // the mode the HPS asks for; 640x480 until the game has the screen

wire       m_dual = (vmode == 4'd2) || (vmode == 4'd4);
wire       t_fast = m_dual & vga31;
wire       t_lace = m_dual & ~vga31;
// the size is -24..+7: beyond that the blanking gets too short or the clock too fast.
// Only for a game's picture: the core's own (the purple screen) has nothing at
// its edges, and its text would get narrow and hard to read
wire [5:0] hsize  = !game_on ? 6'd0 :
                    ($signed(crt_hsize) > 6'sd7) ? 6'sd7 : ($signed(crt_hsize) < -6'sd24) ? -6'sd24 : crt_hsize;
wire [7:0] t_code = t_fast ? 8'd100 : 8'd100 - {{2{hsize[5]}}, hsize};

// in use
reg        run = 0;           // the video output is on
reg  [7:0] vclk_code = 8'd100;
reg        u_fast = 0, u_lace = 0;
reg  [4:0] stop_cnt = 0;
// for the video side: these hold still whenever it reads them
reg  [3:0] s_mode = 0;
reg  [5:0] s_hsize = 0;
reg  [3:0] s_hpos = 0, s_vpos = 0;

wire       vclk_ready;
wire [12:0] vclk_count;
wire       class_change = {t_fast, t_lace, t_code} != {u_fast, u_lace, vclk_code};

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
	s_mode  <= vmode;
	s_hsize <= t_fast ? 6'd0 : hsize;
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
		{u_fast, u_lace, vclk_code} <= {t_fast, t_lace, t_code};
		if (!class_change && vclk_ready) run <= 1;
	end
end

assign hires = u_fast | u_lace;
assign fast  = u_fast;
assign lace  = u_lace;

// ---- from here on clk_vid ----

reg  [2:0] vrst_s = 3'b111;
always @(posedge clk_vid) vrst_s <= {vrst_s[1:0], ~run};
wire       vrst = vrst_s[2];

// the timing in use, taken when a field starts
reg  [3:0] v_mode = 0;
reg        v_fast = 0, v_lace = 0;
reg  [5:0] v_hsize = 0;
reg  [3:0] v_hpos = 0, v_vpos = 0;
reg        v_fmt16 = 0, v_valid = 0, v_ovl = 0;
reg        v_filt = 0;        // the flicker filter: a game's picture, interlaced, the line not Off
reg        v_light = 0;       // Light, not Strong
reg        field = 0;         // interlaced: 1 = even rows

assign f1 = v_lace & ~field;

wire       w640 = (v_mode == 4'd1) || (v_mode == 4'd2) || (v_mode == 4'd4) || (v_mode == 4'd7);
wire       l240 = (v_mode == 4'd3) || (v_mode == 4'd4) || (v_mode == 4'd7);

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
	if (v_fast) begin
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

wire       ce = v_fast ? ce_div[0] : w640 ? (ce_div[1:0] == 2'd3) : (ce_div == 3'd7);
wire       line_start = ce && (hc == H_TOTAL - 1'd1);   // next ce begins a new line
wire [9:0] next_vc  = (vc == V_TOTAL - 1'd1) ? 10'd0 : vc + 1'd1;
wire [9:0] next2_vc = (next_vc == V_TOTAL - 1'd1) ? 10'd0 : next_vc + 1'd1;

// vblank tasks start at the first blank line
wire       vbl_start = line_start && (next_vc == V_ACTIVE);

// requests to the clk side: a line to fetch, vblank
reg        req_tgl = 0, vbl_tgl = 0;
reg  [9:0] req_row;
reg        req_bank;
// the overlay rising into place: its rows that many lower, none above them
// (taken in the vblank, after the levels of the frame are worked out)
reg  [4:0] v_ovl_dy = 0;
reg        lb_blank[0:1];
initial {lb_blank[0], lb_blank[1]} = 2'b00;
// the flicker filter: the rows above and below the one to fetch as well (the
// picture's first and last rows their own neighbours), and whether it is odd
// (the filter's dither)
reg  [9:0] req_up, req_dn;
reg        req_filt = 0;
reg        lb_odd[0:1];
initial {lb_odd[0], lb_odd[1]} = 2'b00;
wire [9:0] last_row = {V_ACTIVE[8:0] - 9'd1, 1'b1};   // interlaced
wire [9:0] scr_row = !v_lace ? next2_vc : {next2_vc[8:0], (next2_vc < vc) ? field : ~field};
reg        vbl_field = 0;

always @(posedge clk_vid) begin
	ce_div <= ce_div + 1'd1;
	if (ce) begin
		if (hc == H_TOTAL - 1'd1) begin
			hc <= 0;
			vc <= next_vc;
			if (next_vc == 0) begin
				v_mode  <= s_mode;
				v_hpos  <= s_hpos;
				v_vpos  <= s_vpos;
				v_fmt16 <= fmt16;
				v_filt  <= FLICKER != 0 && filt_req && v_lace && game_on && !ovl_left;
				v_light <= filt_lvl == 2'd1;
				v_valid <= game_on & ~ovl_left;
				v_ovl   <= ovl_vis & ~fmt16 & ~game_on;
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
		req_row  <= (v_ovl && scr_row >= v_ovl_dy) ? scr_row - v_ovl_dy : scr_row;
		req_bank <= next2_vc[0];
		lb_blank[next2_vc[0]] <= v_ovl && scr_row < v_ovl_dy;
		req_up   <= (scr_row == 10'd0) ? scr_row : scr_row - 1'd1;
		req_dn   <= (scr_row == last_row) ? scr_row : scr_row + 1'd1;
		req_filt <= v_filt;
		lb_odd[next2_vc[0]] <= scr_row[0];
	end
	if (vbl_start) begin
		vbl_tgl   <= ~vbl_tgl;
		vbl_field <= field;
	end
	if (line_start && next_vc == V_ACTIVE + 10'd2) v_ovl_dy <= ovl_vis ? a_ovl_dy : 5'd0;

	// off: everything of the class is taken as it is and the counters wait
	// in the blanking
	if (vrst) begin
		{v_fast, v_lace} <= {u_fast, u_lace};
		v_mode  <= s_mode;
		v_hsize <= s_hsize;
		v_hpos  <= s_hpos;
		v_vpos  <= s_vpos;
		v_fmt16 <= fmt16;
		v_filt  <= 0;
		v_valid <= game_on & ~ovl_left;
		v_ovl   <= 0;
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
reg  [9:0] pal_lv = 256;      // the levels its colours have: the overlay's from the background, or
                              // with bit 9 the master level from black (256: as they are)
reg [31:0] frame_cnt = 0;

reg [63:0] ctrl_w0, ctrl_w1, ctrl_w2;
reg  [7:0] info_seen = 0;
reg        info_pend = 0;
reg  [7:0] info_txt = 0;
reg  [3:0] pi_info_seen = 0;

//////////////////////////////////////////////////////////////////
// Memories

// Both are written on clk and read on clk_vid.
// line buffer: two banks of 256 x 64-bit words (40 used at 320 pixels of
// 8bpp, 160 at 640 pixels of RGB565); with the flicker filter (FLICKER) two
// more, for the rows above and below (lbuf_wpart 1 and 2), and the palette
// twice more, for their pixels
reg [63:0] lbuf[0:511];
reg  [8:0] lbuf_waddr;
reg        lbuf_we;
reg  [1:0] lbuf_wpart;
reg [63:0] lbuf_wdata;
reg [63:0] lbuf_q, lbuf_qu, lbuf_qd;
wire [8:0] lbuf_raddr = {vc[0], v_fmt16 ? hc[9:2] : {1'b0, hc[9:3]}};
always @(posedge clk) if (lbuf_we && lbuf_wpart == 2'd0) lbuf[lbuf_waddr] <= lbuf_wdata;
always @(posedge clk_vid) lbuf_q <= lbuf[lbuf_raddr];
generate
	if (FLICKER != 0) begin : flicker_rows
		reg [63:0] lbuf_u[0:511];
		reg [63:0] lbuf_d[0:511];
		always @(posedge clk) begin
			if (lbuf_we && lbuf_wpart == 2'd1) lbuf_u[lbuf_waddr] <= lbuf_wdata;
			if (lbuf_we && lbuf_wpart == 2'd2) lbuf_d[lbuf_waddr] <= lbuf_wdata;
		end
		always @(posedge clk_vid) begin
			lbuf_qu <= lbuf_u[lbuf_raddr];
			lbuf_qd <= lbuf_d[lbuf_raddr];
		end
	end else begin : no_flicker_rows
		always @(posedge clk_vid) {lbuf_qu, lbuf_qd} <= 128'd0;
	end
endgenerate

// palette: 128 x 64-bit words, two 0x00RRGGBB entries per word
reg [63:0] pal[0:127];
reg  [6:0] pal_waddr;
reg        pal_we;
reg [63:0] pal_wdata;
reg [63:0] pal_q;
reg  [2:0] byte_sel;
reg        pal_hi;
reg        pix_clear;         // index 0: the overlay's transparent colour
wire [7:0] pix_index = lbuf_q[{byte_sel, 3'b0} +: 8];
always @(posedge clk) if (pal_we) pal[pal_waddr] <= pal_wdata;
always @(posedge clk_vid) begin
	pal_q     <= pal[pix_index[7:1]];
	pal_hi    <= pix_index[0];
	pix_clear <= pix_index == 8'd0 || lb_blank[vc[0]];
end
// the flicker filter's rows above and below, at 8bpp: the palette twice more
reg [63:0] pal_qu, pal_qd;
reg        pal_hi_u, pal_hi_d;
wire [7:0] pix_index_u = lbuf_qu[{byte_sel, 3'b0} +: 8];
wire [7:0] pix_index_d = lbuf_qd[{byte_sel, 3'b0} +: 8];
generate
	if (FLICKER != 0) begin : flicker_pal
		reg [63:0] pal_u[0:127];
		reg [63:0] pal_d[0:127];
		always @(posedge clk) if (pal_we) begin
			pal_u[pal_waddr] <= pal_wdata;
			pal_d[pal_waddr] <= pal_wdata;
		end
		always @(posedge clk_vid) begin
			pal_qu   <= pal_u[pix_index_u[7:1]];
			pal_qd   <= pal_d[pix_index_d[7:1]];
			pal_hi_u <= pix_index_u[0];
			pal_hi_d <= pix_index_d[0];
		end
	end else begin : no_flicker_pal
		always @(posedge clk_vid) {pal_qu, pal_qd, pal_hi_u, pal_hi_d} <= 130'd0;
	end
endgenerate

// RGB565 pixels skip the palette
reg [15:0] pix16;
always @(posedge clk_vid) pix16 <= lbuf_q[{byte_sel[1:0], 4'b0} +: 16];

// The flicker filter (FLICKER, the OSD's line): a quarter of the row above, half of
// this one and a quarter of the one below, per channel of RGB565, and the
// two bits that adds dithered away: the 4x4 pattern of dnkLauncher's
// out_box() (gfx.c), 0 2 0 2 in even rows and 3 1 3 1 in odd ones, so that
// the picture is the one the program made when it filtered it itself
reg [15:0] pix16_u, pix16_d;
reg  [1:0] pix_bias;
reg        pix_bias_lo;
always @(posedge clk_vid) begin
	pix16_u  <= lbuf_qu[{byte_sel[1:0], 4'b0} +: 16];
	pix16_d  <= lbuf_qd[{byte_sel[1:0], 4'b0} +: 16];
	pix_bias <= lb_odd[vc[0]] ? (byte_sel[0] ? 2'd1 : 2'd3) : (byte_sel[0] ? 2'd2 : 2'd0);
	pix_bias_lo <= byte_sel[1];
end
wire [6:0] filt_r = pix16_u[15:11] + {pix16[15:11], 1'b0} + pix16_d[15:11] + pix_bias;
wire [7:0] filt_g = pix16_u[10:5] + {pix16[10:5], 1'b0} + pix16_d[10:5] + pix_bias;
wire [6:0] filt_b = pix16_u[4:0] + {pix16[4:0], 1'b0} + pix16_d[4:0] + pix_bias;
wire [15:0] pix16_f = {filt_r[6:2], filt_g[7:2], filt_b[6:2]};
// Light: an eighth, three quarters and an eighth, the three bits dithered
// away by the same pattern with a bit more (0..7 over 4x2 pixels)
wire [2:0] pix_bias8 = {pix_bias, pix_bias_lo};
wire [7:0] lfilt_r = pix16_u[15:11] + {pix16[15:11], 2'b0} + {pix16[15:11], 1'b0} + pix16_d[15:11] + pix_bias8;
wire [8:0] lfilt_g = pix16_u[10:5] + {pix16[10:5], 2'b0} + {pix16[10:5], 1'b0} + pix16_d[10:5] + pix_bias8;
wire [7:0] lfilt_b = pix16_u[4:0] + {pix16[4:0], 2'b0} + {pix16[4:0], 1'b0} + pix16_d[4:0] + pix_bias8;
wire [15:0] pix16_l = {lfilt_r[7:3], lfilt_g[8:3], lfilt_b[7:3]};
// at 8bpp after the palette, rounded
wire [23:0] pix8_c = pal_hi   ? pal_q[55:32]  : pal_q[23:0];
wire [23:0] pix8_u = pal_hi_u ? pal_qu[55:32] : pal_qu[23:0];
wire [23:0] pix8_d = pal_hi_d ? pal_qd[55:32] : pal_qd[23:0];
wire [9:0] filt8_r = pix8_u[23:16] + {pix8_c[23:16], 1'b0} + pix8_d[23:16] + 2'd2;
wire [9:0] filt8_g = pix8_u[15:8] + {pix8_c[15:8], 1'b0} + pix8_d[15:8] + 2'd2;
wire [9:0] filt8_b = pix8_u[7:0] + {pix8_c[7:0], 1'b0} + pix8_d[7:0] + 2'd2;
wire [10:0] lfilt8_r = pix8_u[23:16] + {pix8_c[23:16], 2'b0} + {pix8_c[23:16], 1'b0} + pix8_d[23:16] + 3'd4;
wire [10:0] lfilt8_g = pix8_u[15:8] + {pix8_c[15:8], 2'b0} + {pix8_c[15:8], 1'b0} + pix8_d[15:8] + 3'd4;
wire [10:0] lfilt8_b = pix8_u[7:0] + {pix8_c[7:0], 2'b0} + {pix8_c[7:0], 1'b0} + pix8_d[7:0] + 3'd4;

//////////////////////////////////////////////////////////////////
// Picture without a game
//
// A screen of 640x480 on a background of one colour: the MiSTer logo
// (429x111) in the middle, and in the bottom right corner the badge, the text
// "Hybrid Core Framework v<VERSION>" over "by danik" and the icon right of
// it, 34 pixels from the right edge and from the bottom (a 15kHz screen hides
// some of the edges). The core is in 640x480 then: interlaced at 15kHz,
// progressive at 31kHz with vga31.
// tools/make_wall.py makes the files from assets/ and the VERSION: the
// pixels of the logo and then those of the badge (WALL_BADGE_W x
// WALL_BADGE_H: the text right-aligned, the icon at its right end), 4 bits
// each, and 32 colours, 16 for each (colour 0 of each is the background, the
// last 4 of the badge are the text's); the dot under "MiSTer" each column of
// the logo is in; the ease-out curve of the movements.
//
// The intro. Black for the first 16 fields (wall_hold), which a screen needs
// to lock to the new signal, and for 3 seconds after a game has left: leaving
// is mostly on the way to the MiSTer menu, and the picture is not to flash up
// before that. Then, in fields from there (intro_t, 59.6 a second):
//    0  the background fades in                                 31 fields
//   31  the logo fades in and rises 16 pixels into place        31
//   62  the dots under "MiSTer" light up in turn, round and round until the
//       game has the screen
//   86  the icon slides in from the right edge of the screen    30
//  116  the text fades in, sliding 20 pixels out from behind the icon      30
//  146  a shine sweeps across the logo                          96
//  242  the end
// Movements follow the ease-out curve; the shine and the fades are even. A game that attaches
// waits for the end (the HPS side waits for game_on, 4.8 seconds at most);
// one that attaches while the picture is still black gets the screen at
// once. Then everything goes back to black in 43 fields, 0.7 seconds
// (wall_level, the master level, 256..0 in steps of 6), and only then the
// game gets the screen (game_on) and its video mode is set.
// What moves changes when a field starts, interlaced as well (host version
// 18; up to 17 when a frame started, both fields of a frame the same picture
// for the HDMI scaler's weave): a CRT shows the moves at 59.6 pictures a
// second, and on HDMI without HDMI Only they comb, as Danik accepted
// (2026-10-27).
// The colours are not multiplied on their way to the output: the 32 of the
// pictures, the background and the lit dot are worked out for the levels of
// the field in the vblank, on clk, and hold still while the video side reads
// them. The dots and the shine are worked out per pixel, from the colour.
//
// The overlay (host version 17, docs/midiplayer.md): a program that asks for
// it (w0[58]) has its frame drawn over the picture, which stays, instead of
// taking the screen: 640x480 at 8bpp, palette index 0 transparent. It never
// gets game_on (the picture's own timing, no Horizontal Size). Once the intro
// is over (ovl_t, in fields from there):
//    0  the logo goes as it came: its intro step backwards, fading out while
//       it sinks the 16 pixels it rose; the dots and the message go with it  31
//   31  the background and the badge alone                     30
//   61  the overlay fades in, rising 16 pixels into place as the logo did
//       in the intro with w2[59] (the game's logo)              31
// w2[58] runs that last step backwards and holds the overlay out at 61, a
// message going with it, until the bit is cleared.
// The overlay's level is put into its palette as the palette is read (each
// field while it changes): a colour mixed with the background, as if the
// program kept clear of the badge, which it is to do. The pixel only decides
// between the palette and the picture. A message there when the handover
// begins goes out with the logo; one shown later (the game's lines under its
// logo, docs/game_logo.md) is at its full level. Without w0[58] again (the
// magic kept) the picture goes out for a game as ever, the overlay with it at
// the master level (in its palette as well) and the logo staying gone. The
// game then has the screen in its own mode, black until it publishes another
// framebuffer than the overlay's (ovl_left): its first frame always is.
//
// INTRO 0 (a frontend, docs/dnklauncher.md): the picture is at the end of
// its intro from the core's start and after a reset (no black fields, no
// intro: the levels, places and dots as at intro field 242, the dots going
// round), and a program that attaches has the screen at the next frame
// (game_take: the field after the vblank that read its control block, or the
// one after that when that vblank was between the two fields of a frame and
// the program has no field rate, w2[60]),
// in its own mode, without the picture going out: a cut. The frontend's first
// frames are the same picture, so nothing is seen to change. Everything else
// is as with INTRO 1: messages, the overlay (its handover begins at once), the
// core's own message after WALL_TIMEOUT, and a program that leaves: black for
// WALL_HOLD_LEFT, then the picture fading in at the end of its intro, as with
// a message. A Horizontal Size other than 0 still makes the cut a change of
// the video clock (the picture's own is size 0): a moment of no signal.

localparam [23:0] WALL_BG  = 24'h7F30A0;   // BACKGROUND of make_wall.py
localparam [23:0] WALL_LIT = 24'hFF5CB4;   // a lit dot: the pink of "FPGA", brighter
localparam [23:0] WALL_INK = 24'hEFEFEF;   // the text of a message, as the badge's
// sizes and places, as make_wall.py has them
localparam        WALL_LOGO_W  = 429;
localparam        WALL_LOGO_H  = 111;
localparam        WALL_LOGO_X  = (640 - WALL_LOGO_W) / 2;
localparam        WALL_LOGO_Y  = (480 - WALL_LOGO_H) / 2;
localparam        WALL_BADGE_W = 320;
localparam        WALL_BADGE_H = 54;
localparam        WALL_BADGE_X = 640 - 34 - WALL_BADGE_W;
localparam        WALL_BADGE_Y = 480 - 34 - WALL_BADGE_H;
localparam        WALL_ICON_W  = 53;                          // at the badge's right end
localparam        WALL_TEXT_W  = WALL_BADGE_W - WALL_ICON_W;  // the rest
localparam        WALL_ICON_X  = WALL_BADGE_X + WALL_TEXT_W;
localparam        WALL_BADGE_AT = WALL_LOGO_W * WALL_LOGO_H;  // its first pixel
localparam        WALL_PIXELS = WALL_BADGE_AT + WALL_BADGE_W * WALL_BADGE_H;
localparam        WALL_DOT_Y0  = 91;                          // the rows of the dots in the logo
localparam        WALL_DOT_Y1  = 97;
localparam  [7:0] WALL_HOLD = 8'd16;       // fields of black before the intro
localparam  [7:0] WALL_HOLD_LEFT = 8'd180; // the same after a game has left
localparam  [8:0] WALL_STEP = 9'd6;        // of the master level, per field, going out
// the intro, in fields: when each part starts and how long it takes
localparam        WALL_T_BG    = 0,   WALL_D_BG    = 31;
localparam        WALL_T_LOGO  = 31,  WALL_D_LOGO  = 31;
localparam        WALL_T_DOTS  = 62;
localparam        WALL_T_ICON  = 86,  WALL_D_ICON  = 30;
localparam        WALL_T_TEXT  = 116, WALL_D_TEXT  = 30;
localparam        WALL_T_SHINE = 146, WALL_D_SHINE = 96;
localparam        WALL_T_END   = 242;
localparam        WALL_RISE    = 16;                          // of the logo
localparam        WALL_SLIDE   = 640 - WALL_ICON_X;           // of the icon, from the edge
localparam        WALL_TEXT_SLIDE = 20;
// the shine: a band across the logo, leaning right, along x + (110 - y) / 2
localparam        WALL_SHINE_FROM = -24;
localparam        WALL_SHINE_SPAN = WALL_LOGO_W + (WALL_LOGO_H - 1) / 2 + 48;
// a message: 4 lines of 71 characters of 6x13, the first 16 pixels below the
// logo and left-aligned with it, 16 pixels from line to line (MH_MSG_* of
// mister_hybrid.h, MSG_* of make_wall.py)
localparam        WALL_MSG_X     = WALL_LOGO_X;
localparam        WALL_MSG_Y     = WALL_LOGO_Y + WALL_LOGO_H + 16;
localparam        WALL_MSG_COLS  = 71;
localparam        WALL_MSG_PITCH = 16;
// the overlay's handover, in fields from the end of the intro
localparam        OVL_T_GAP = 31;                 // the logo is gone
localparam        OVL_T_IN  = 61, OVL_D_IN = 31;  // the overlay fades in

reg  [3:0] wall[0:WALL_PIXELS - 1];
reg [23:0] wall_pal[0:31];
reg [23:0] wall_fpal[0:31];   // wall_pal at the levels of this field
reg  [4:0] wall_dots[0:WALL_LOGO_W - 1];
reg  [8:0] wall_ease[0:256];
integer    wall_n;
initial begin
	$readmemh(WALL, wall);
	$readmemh(WALL_PAL, wall_pal);
	$readmemh(WALL_DOTS, wall_dots);
	$readmemh(WALL_EASE, wall_ease);
	for (wall_n = 0; wall_n < 32; wall_n = wall_n + 1) wall_fpal[wall_n] = 24'd0;
end

// the message: its first word, the lines as read (clk), the core's own
reg [63:0] msg_hdr = 0;
reg [63:0] msg_mem[0:39];
reg [63:0] msg_alone[0:39];
reg  [5:0] wall_font[0:95 * 13 - 1];
initial begin
	$readmemh(WALL_FONT, wall_font);
	$readmemh(WALL_ALONE, msg_alone);
end
reg        msg_we = 0;
reg  [5:0] msg_waddr;
reg [63:0] msg_wdata;
always @(posedge clk) if (msg_we) msg_mem[msg_waddr] <= msg_wdata;
reg        msg_show = 0;      // a message is shown
reg        msg_loaded = 0;    // its lines are in msg_mem
reg [15:0] msg_seq = 0;
reg        msg_err = 0;       // it is an error: the dots stand still
reg        msg_list = 0;      // it is a list: the dots stand still, a bar on the line selected
reg  [1:0] msg_sel = 0;
reg        msg_timeout = 0;   // the core's own is shown
reg        attached = 0;      // a game was there since the core started
reg [10:0] alone_t = 0;       // fields without one
reg        intro_skip = INTRO == 0;   // the intro begun with a message (or INTRO 0): at its end at once
reg        ovl_on = 0;        // the overlay's handover has begun
reg  [6:0] ovl_t = 0;         // its fields, up to 127
reg        msg_keep = 0;      // the message, gone meanwhile, goes out with the logo
reg  [8:0] lv_ovl = 0;        // the overlay's level, 0..256
reg        ovl_vis = 0;       // and it is seen
reg        ovl_left = 0;      // the game has the screen after an overlay, its first frame not yet there
reg  [1:0] ovl_fb = 0;        // the overlay's framebuffer
wire       ovl_out_req;       // w2[58]: the overlay out, held there
wire       ovl_rise;          // w2[59]: it rises in and sinks out

// INTRO 0: at the end of the intro from the start (and after a reset)
reg  [7:0] wall_hold = INTRO ? WALL_HOLD : 8'd0;   // fields of black still to come
reg  [8:0] wall_level = INTRO ? 9'd0 : 9'd256;     // the master level, 0..256
reg        intro_on = INTRO == 0;                  // the intro has begun
reg  [8:0] intro_t = INTRO ? 9'd0 : 9'd0 + WALL_T_END;   // its fields, up to 511
reg  [5:0] dot_t = 0;         // fields since the dots began, round and round
wire       intro_done = intro_t >= WALL_T_END;
reg        fade_start = 0;    // a field's levels to work out (set in the vblank)
reg [23:0] wall_bg = 0;       // the background, as shown
reg [23:0] dot_lit = 0;       // a lit dot, as shown
reg [23:0] msg_ink = 0;       // the text of a message, as shown
reg [23:0] msg_dim = 0;       // a list's marks that there is more: 60% of the way to the text
reg [23:0] msg_bar = 0;       // its bar: 75% of the way to the pink of a lit dot
reg  [8:0] lv_msg = 256;      // the message's level: the logo's, while it goes out for the overlay
reg [15:0] wall_dark = 0;     // the logo's colours that are dark, which light up in a dot

function [7:0] fade8(input [7:0] c, input [8:0] level);
	reg [16:0] p;
	begin
		p = c * level;
		fade8 = p[15:8];
	end
endfunction
function [23:0] fade(input [23:0] rgb, input [8:0] level);
	fade = {fade8(rgb[23:16], level), fade8(rgb[15:8], level), fade8(rgb[7:0], level)};
endfunction
// from a to b: a at level 0, b at 256
function [7:0] mix8(input [7:0] a, input [7:0] b, input [8:0] level);
	reg signed [9:0]  d;
	reg signed [19:0] p;
	begin
		d = $signed({2'b0, b}) - $signed({2'b0, a});
		p = d * $signed({1'b0, level});
		mix8 = a + p[15:8];
	end
endfunction
function [23:0] mix(input [23:0] a, input [23:0] b, input [8:0] level);
	mix = {mix8(a[23:16], b[23:16], level), mix8(a[15:8], b[15:8], level), mix8(a[7:0], b[7:0], level)};
endfunction
// how far a part that starts at t0 and takes d fields is at t, 0..256
function [8:0] progress(input [8:0] t, input integer t0, input integer d);
	reg [21:0] p;
	begin
		p = (t < t0) ? 22'd0 : (t - t0) * ((65536 + d - 1) / d);
		progress = (p[21:8] > 256) ? 9'd256 : p[16:8];
	end
endfunction

// The levels and places of this field, and from them the colours.
// One step per clock after the vblank asked for it (fade_start): first the
// progress of each part and its place on the curve (one table, so one part
// at a time), then the 32 colours and the message's 3, each mixed from the
// background at its part's level in one clock and put at the master level in
// the next.
reg  [6:0] wall_seq = INTRO ? 7'h7F : 7'h0;   // 7F: idle; INTRO 0 works out the picture's first field at once
reg  [8:0] p_logo = 0, p_icon = 0, p_text = 0, p_shine = 0, p_ovl = 0;
reg  [8:0] e_logo = 0, e_icon = 0, e_text = 0, e_ovl = 0;
reg  [8:0] ease_q;
reg  [8:0] lv_bg = 0, lv_logo = 0, lv_text = 0;
reg [23:0] bg_in = 0;         // the background at its level, before the master's
reg [23:0] pal_mix;
reg  [4:0] pal_n;
reg  [1:0] put_kind;          // 0 a colour of the pictures, 1..3 the message's ink, dim and bar
reg        pal_put = 0;
reg  [8:0] lv_dim = 154, lv_bar = 192;   // the message's marks and bar
// read by the video side
reg  [4:0] a_logo_dy = WALL_RISE;
reg  [9:0] a_icon_x = 640, a_text_dx = WALL_TEXT_SLIDE;
reg signed [10:0] a_shine = WALL_SHINE_FROM;
reg        a_dots = 0;
reg  [4:0] a_dot_head = 0;
reg  [4:0] a_ovl_dy = WALL_RISE;   // the overlay below its place, rising in

wire [4:0] seq_n = wall_seq[4:0] - 5'd9;      // the colour of steps 9..40
wire [8:0] lv_n  = (seq_n == 5'd0 || seq_n == 5'd16) ? 9'd0    // the background of a picture
                 : (seq_n < 5'd16) ? lv_logo : (seq_n >= 5'd28) ? lv_text : 9'd256;
// the message's colours after the pictures': steps 41..43
wire [23:0] seq_src = (wall_seq == 7'd41 || wall_seq == 7'd42) ? WALL_INK : (wall_seq == 7'd43) ? WALL_LIT : wall_pal[seq_n];
wire  [8:0] seq_lv  = (wall_seq == 7'd41) ? lv_msg : (wall_seq == 7'd42) ? lv_dim : (wall_seq == 7'd43) ? lv_bar : lv_n;
// the overlay's handover: the logo's intro step backwards
wire  [8:0] logo_t  = !ovl_on ? intro_t : (ovl_t >= OVL_T_GAP) ? 9'd0 + WALL_T_LOGO : 9'd0 + WALL_T_LOGO + WALL_D_LOGO - ovl_t;
wire [23:0] pal_c = wall_pal[pal_n];
wire [15:0] pal_lum = pal_c[23:16] * 8'd77 + pal_c[15:8] * 8'd150 + pal_c[7:0] * 8'd29;

always @(posedge clk) begin
	ease_q  <= wall_ease[(wall_seq == 7'd1) ? p_logo : (wall_seq == 7'd2) ? p_icon : (wall_seq == 7'd3) ? p_text : p_ovl];
	pal_put <= 0;
	case (wall_seq)
		7'd0: begin
			p_logo  <= progress(logo_t, WALL_T_LOGO, WALL_D_LOGO);
			p_ovl   <= progress({2'd0, ovl_t}, OVL_T_IN, OVL_D_IN);
			p_icon  <= progress(intro_t, WALL_T_ICON, WALL_D_ICON);
			p_text  <= progress(intro_t, WALL_T_TEXT, WALL_D_TEXT);
			p_shine <= progress(intro_t, WALL_T_SHINE, WALL_D_SHINE);
		end
		// ease_q is the curve at what the step before asked for
		7'd2: e_logo  <= ease_q;
		7'd3: e_icon  <= ease_q;
		7'd4: e_text  <= ease_q;
		7'd5: e_ovl   <= ease_q;
		7'd6: begin
			lv_bg      <= progress(intro_t, WALL_T_BG, WALL_D_BG);
			lv_logo    <= p_logo;
			lv_text    <= p_text;
			a_logo_dy  <= (WALL_RISE * (10'd256 - e_logo)) >> 8;
			a_icon_x   <= WALL_ICON_X + ((WALL_SLIDE * (10'd256 - e_icon)) >> 8);
			a_text_dx  <= (WALL_TEXT_SLIDE * (10'd256 - e_text)) >> 8;
			a_shine    <= WALL_SHINE_FROM + ((WALL_SHINE_SPAN * p_shine) >> 8);
			a_dots     <= intro_on && intro_t >= WALL_T_DOTS && !(msg_show ? msg_err | msg_list : msg_timeout);
			a_dot_head <= dot_t[5:1];
			lv_ovl     <= ovl_on ? p_ovl : 9'd0;
			a_ovl_dy   <= ovl_rise ? (WALL_RISE * (10'd256 - e_ovl)) >> 8 : 5'd0;
			ovl_vis    <= ovl_on && p_ovl != 0;
			lv_msg     <= (ovl_on && ovl_t < OVL_T_GAP) ? p_logo : (ovl_on && ovl_out_req) ? p_ovl : 9'd256;
		end
		7'd7: begin
			bg_in  <= fade(WALL_BG, lv_bg);
			// 17 bits: in the 9 of lv_msg the product overflowed (the bar and
			// the marks at level 0, the background's colour)
			lv_dim <= (lv_msg * 17'd154) >> 8;
			lv_bar <= (lv_msg * 17'd192) >> 8;
		end
		7'd8: begin
			wall_bg <= fade(bg_in, wall_level);
			dot_lit <= fade(mix(bg_in, WALL_LIT, lv_logo), wall_level);
		end
		default: ;
	endcase
	if (wall_seq >= 7'd9 && wall_seq <= 7'd43) begin
		pal_mix  <= mix(bg_in, seq_src, seq_lv);
		pal_n    <= seq_n;
		put_kind <= (wall_seq <= 7'd40) ? 2'd0 : wall_seq[1:0];   // 41, 42, 43: 1, 2, 3
		pal_put  <= 1;
	end
	if (pal_put) begin
		case (put_kind)
			2'd0: begin
				wall_fpal[pal_n] <= fade(pal_mix, wall_level);
				if (!pal_n[4]) wall_dark[pal_n[3:0]] <= pal_lum[15:8] < 8'd160;
			end
			2'd1: msg_ink <= fade(pal_mix, wall_level);
			2'd2: msg_dim <= fade(pal_mix, wall_level);
			default: msg_bar <= fade(pal_mix, wall_level);
		endcase
	end
	if (wall_seq != 7'h7F) wall_seq <= (wall_seq == 7'd44) ? 7'h7F : wall_seq + 1'd1;
	if (fade_start) wall_seq <= 0;
end

// The row of the screen (interlaced, the field has every second one) and
// what of the pictures is on it, a clock or two after the line starts, which
// no pixel of them is near: whether one is (the logo and the badge have no
// row in common), the address of its first pixel, and whether it has the
// dots. Then, as for a pixel of the game: the address (the logo; on the
// badge's rows the icon, and left of it the text), the pixel, and its colour
// when the output takes it. Laid out for 640x480; in another mode (only ever
// black: the level is 0 then) it is cut off.
wire  [9:0] wall_y  = v_lace ? {vc[8:0], ~field} : vc;
wire  [9:0] logo_y  = wall_y - WALL_LOGO_Y - a_logo_dy;
wire  [9:0] badge_y = wall_y - WALL_BADGE_Y;
reg         logo_row_in, badge_row_in;
reg   [6:0] logo_row_y;
reg   [5:0] badge_row_y;
reg         row_logo, row_badge, row_dots;
reg  [16:0] row_addr0;        // address of the first pixel of the row
reg   [9:0] row_shine;        // (110 - row) / 2: where the shine is on this row
wire  [9:0] lx = hc[9:0] - WALL_LOGO_X;
wire  [9:0] ix = hc[9:0] - a_icon_x;
wire  [9:0] tx = hc[9:0] - WALL_BADGE_X - a_text_dx;
wire signed [11:0] shine_d = $signed({2'b0, lx + row_shine}) - a_shine;
wire [11:0] shine_a = shine_d[11] ? -shine_d : shine_d;
reg         wall_in, wall_in_d, src_badge, src_badge_d;
reg  [16:0] wall_addr;
reg   [3:0] wall_q;
reg   [1:0] shine_l, shine_l_d;   // 0: none .. 3: the middle of the band
reg   [4:0] dot_n, dot_n_d;       // the dot of the column, 31: none

always @(posedge clk_vid) begin
	logo_row_in  <= (logo_y < WALL_LOGO_H);
	logo_row_y   <= logo_y[6:0];
	badge_row_in <= (badge_y < WALL_BADGE_H);
	badge_row_y  <= badge_y[5:0];

	row_logo  <= logo_row_in;
	row_badge <= badge_row_in & ~logo_row_in;
	row_dots  <= logo_row_in && logo_row_y >= WALL_DOT_Y0 && logo_row_y <= WALL_DOT_Y1;
	row_addr0 <= logo_row_in ? logo_row_y * WALL_LOGO_W : WALL_BADGE_AT + badge_row_y * WALL_BADGE_W;
	row_shine <= (WALL_LOGO_H - 1 - logo_row_y) >> 1;

	if (row_logo) begin
		wall_in   <= lx < WALL_LOGO_W;
		wall_addr <= row_addr0 + lx;
	end
	else if (hc[9:0] >= a_icon_x) begin
		wall_in   <= row_badge && ix < WALL_ICON_W;
		wall_addr <= row_addr0 + WALL_TEXT_W + ix;
	end
	else begin
		// the text, left of the icon: never over it
		wall_in   <= row_badge && tx < WALL_TEXT_W;
		wall_addr <= row_addr0 + tx;
	end
	src_badge <= !row_logo;
	shine_l   <= (shine_a < 12'd4) ? 2'd3 : (shine_a < 12'd10) ? 2'd2 : (shine_a < 12'd18) ? 2'd1 : 2'd0;
	dot_n     <= (row_dots && lx < WALL_LOGO_W) ? wall_dots[lx[8:0]] : 5'd31;

	wall_in_d   <= wall_in;
	src_badge_d <= src_badge;
	wall_q      <= wall[wall_addr];
	shine_l_d   <= shine_l;
	dot_n_d     <= dot_n;
end

// The text of a message, a line of pixels at a time: at the start of a line,
// the one after the next is worked out into a line buffer of 72 characters'
// rows of 6 pixels, one character a clock (its word of the message, then its
// row of the font), in the half of the buffer the next line does not read.
// The text is only ever on rows of the screen that have nothing else.
wire        msg_vis   = msg_show | msg_timeout | msg_keep;
wire  [9:0] fill_y    = v_lace ? {next2_vc[8:0], ~field} : next2_vc;
wire  [9:0] fill_ty   = fill_y - WALL_MSG_Y;
wire        fill_on   = msg_vis && fill_ty < 4 * WALL_MSG_PITCH && fill_ty[3:0] < 4'd13;
reg   [5:0] msg_lb[0:255];
reg         lb_on[0:1];
reg   [7:0] f_c = 8'd255;     // the character being fetched; 72 and up: none
reg   [1:0] f_line;
reg   [3:0] f_row;
reg         f_bank, f_rom;
reg  [63:0] f_qm, f_qa;
reg   [2:0] f_byte;
reg   [6:0] f_c1, f_c2;
reg         f_v1 = 0, f_v2 = 0, f_blank;
reg   [5:0] f_bits;
wire [63:0] f_q       = f_rom ? f_qa : f_qm;    // the message or the core's own
wire  [7:0] f_ch      = f_q[{f_byte, 3'b0} +: 8];
wire [10:0] f_glyph   = {3'd0, f_ch - 8'd32} * 11'd13 + f_row;   // its row in the font
wire  [5:0] f_word    = f_line * 4'd10 + f_c[6:3];

always @(posedge clk_vid) begin
	if (line_start) begin
		lb_on[next2_vc[0]] <= fill_on;
		f_line <= fill_ty[5:4];
		f_row  <= fill_ty[3:0];
		f_bank <= next2_vc[0];
		f_rom  <= !(msg_show | msg_keep);
		f_c    <= 0;
	end
	else if (f_c < 8'd72) f_c <= f_c + 1'd1;
	// the word of the character
	f_qm   <= msg_mem[f_word];
	f_qa   <= msg_alone[f_word];
	f_byte <= f_c[2:0];
	f_c1   <= f_c[6:0];
	f_v1   <= f_c < 8'd72;
	// its row of the font
	f_bits  <= wall_font[f_glyph];
	f_blank <= f_ch < 8'd32 || f_ch > 8'd126;
	f_c2    <= f_c1;
	f_v2    <= f_v1;
	if (f_v2) msg_lb[{f_bank, f_c2}] <= f_blank ? 6'd0 : f_bits;
end
// on the screen, as for the pictures: the character and its column, then its
// row of pixels
wire  [9:0] mx    = hc[9:0] - WALL_MSG_X;
wire [17:0] mx_d6 = mx * 9'd171;          // mx / 6 for mx < 432: (mx * 171) >> 10
wire  [6:0] mcol  = mx_d6[16:10];
reg         txt_in, txt_in_d;
reg   [7:0] txt_addr;
reg   [2:0] txt_x, txt_x_d;
reg   [5:0] txt_q;
reg         txt_last, txt_last_d;     // the last column: a list's mark
// a list: the bar behind the line selected, from 2 rows above it to 2 below,
// 4 pixels left and right of the text
wire  [9:0] bar_y = wall_y - (WALL_MSG_Y - 2) - msg_sel * WALL_MSG_PITCH;
wire  [9:0] bar_x = hc[9:0] - (WALL_MSG_X - 4);
reg         bar_row, bar_in, bar_in_d;
always @(posedge clk_vid) begin
	txt_in   <= lb_on[vc[0]] && mx < WALL_MSG_COLS * 6;
	txt_addr <= {vc[0], mcol};
	txt_x    <= mx - mcol * 3'd6;
	txt_last <= mcol == WALL_MSG_COLS - 1;
	bar_row  <= msg_show && msg_list && bar_y < WALL_MSG_PITCH;
	bar_in   <= bar_row && bar_x < WALL_MSG_COLS * 6 + 8;
	txt_in_d <= txt_in;
	txt_x_d  <= txt_x;
	txt_last_d <= txt_last;
	bar_in_d <= bar_in;
	txt_q    <= msg_lb[txt_addr];
end
wire        txt_on = txt_in_d && txt_q[txt_x_d];

// a dot lights up as the head of the round passes it, and dims in the next
// two steps after; the 24 dots and 8 steps of none in a round of 32
wire  [4:0] dot_d   = a_dot_head - dot_n_d;
wire  [1:0] dot_l   = (!a_dots || dot_n_d == 5'd31 || src_badge_d || !wall_dark[wall_q]) ? 2'd0
                    : (dot_d == 5'd0) ? 2'd3 : (dot_d == 5'd1) ? 2'd2 : (dot_d == 5'd2) ? 2'd1 : 2'd0;
wire [23:0] wall_c  = txt_on ? ((msg_list && txt_last_d) ? msg_dim : msg_ink) : bar_in_d ? msg_bar
                    : wall_in_d ? wall_fpal[{src_badge_d, wall_q}] : wall_bg;
wire        shine_on = wall_in_d && !src_badge_d && wall_q != 4'd0 && shine_l_d != 2'd0;
// One more register on the way, for the clock's sake: the output takes the
// pixel at the next ce, which is two clocks after wall_q at the earliest
// (31kHz) and the dots and the shine come after the palette.
reg  [23:0] wall_c_r;
reg   [1:0] dot_l_r, shine_l_r;
reg         shine_on_r;
always @(posedge clk_vid) begin
	wall_c_r   <= wall_c;
	dot_l_r    <= dot_l;
	shine_l_r  <= shine_l_d;
	shine_on_r <= shine_on;
end
function [7:0] toward(input [7:0] a, input [7:0] b, input [1:0] l);   // l: none, 1/4, 1/2, all
	toward = (l == 2'd3) ? b : (l == 2'd2) ? ({1'b0, a} + {1'b0, b}) >> 1
	       : (l == 2'd1) ? ({2'b0, a} + {1'b0, a, 1'b0} + {2'b0, b}) >> 2 : a;
endfunction
wire [23:0] wall_dc = {toward(wall_c_r[23:16], dot_lit[23:16], dot_l_r), toward(wall_c_r[15:8], dot_lit[15:8], dot_l_r),
                       toward(wall_c_r[7:0], dot_lit[7:0], dot_l_r)};
// the shine: 1/8, 1/4 or 1/2 of the way to white, on the logo
function [7:0] shine8(input [7:0] c, input [1:0] l);
	shine8 = c + ((8'd255 - c) >> (3'd4 - l));
endfunction
wire [23:0] wall_rgb = shine_on_r ? {shine8(wall_dc[23:16], shine_l_r), shine8(wall_dc[15:8], shine_l_r), shine8(wall_dc[7:0], shine_l_r)}
                                  : wall_dc;

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
reg        aud_tick3 = 0;
reg [31:0] aud_play = 0;      // frames played
reg signed [15:0] pcm_l = 0, pcm_r = 0;
wire signed [15:0] fm_snd;
wire signed [18:0] mt32_snd_l, mt32_snd_r;
wire signed [15:0] midi_l, midi_r;
wire signed [19:0] mix_l = pcm_l + fm_snd + mt32_snd_l + midi_l;
wire signed [19:0] mix_r = pcm_r + fm_snd + mt32_snd_r + midi_r;

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
			pcm_l <= 0;
			pcm_r <= 0;
			aud_half <= 0;
		end else if (afifo_level != 0) begin
			{pcm_r, pcm_l} <= aud_half ? afifo_q[63:32] : afifo_q[31:0];
			aud_half <= ~aud_half;
			aud_play <= aud_play + 1'd1;
			if (aud_half) afifo_rd <= afifo_rd + 1'd1;
		end
		// on underrun the last sample is held
	end

	// the FM chip's sound, the MT32-pi's and the MIDI synthesizer's on top
	// (silent while not enabled), held at the limits
	aud_tick3 <= aud_tick2;
	if (aud_tick3) begin
		audio_l <= (mix_l > 20'sd32767) ? 16'sd32767 : (mix_l < -20'sd32768) ? -16'sd32768 : mix_l[15:0];
		audio_r <= (mix_r > 20'sd32767) ? 16'sd32767 : (mix_r < -20'sd32768) ? -16'sd32768 : mix_r[15:0];
	end

	if (!audio_en) begin
		afifo_rd <= afifo_wr;
		aud_play <= 0;
	end
end

//////////////////////////////////////////////////////////////////
// FM chip (hybrid_fm.sv)
//
// The DDR engine below reads the FM ring one entry at a time into fm_ent,
// whenever that is empty and the game has written more than was read; the
// chip takes it when its audio frame is being played. An entry of MIDI bytes
// goes to the MT32-pi instead (below).

reg        fm_en = 0;
reg  [6:0] fm_vol = 7'd64;
reg [31:0] fm_wcount = 0;     // entries the game has written
reg [31:0] fm_fetch = 0;      // entries read
reg [63:0] fm_ent = 0;
reg        fm_ent_valid = 0;
wire       fm_take;
// the entry is MIDI bytes (their number), and its frame is being played
wire [31:0] ent_late = aud_play - fm_ent[31:0];
wire        ent_due  = !ent_late[31];
wire        ent_midi = fm_ent[57:56] != 2'd0;

hybrid_fm fm
(
	.clk      (clk),
	.en       (fm_en),
	.volume   (fm_vol),
	.frame    (aud_play),
	.wr_valid (fm_ent_valid && !ent_midi),
	.wr_frame (fm_ent[31:0]),
	.wr_reg   (fm_ent[47:40]),
	.wr_data  (fm_ent[39:32]),
	.wr_take  (fm_take),
	.snd      (fm_snd)
);

//////////////////////////////////////////////////////////////////
// MT32-pi
//
// An MT32-pi (github.com/dwhinham/mt32-pi, a MIDI synthesizer on a Raspberry
// Pi: Munt for the Roland MT-32 and CM-32L, FluidSynth for SoundFonts) on
// the user port, with MiSTer's own module for it, sys/mt32pi.sv, as ao486
// has it: MIDI out on one pin, its sound back over I2S, and its settings
// over I2C, which the Pi polls; it answers only when it is there
// (mt32_available). The game's MIDI messages come through the FM ring and
// are sent at their audio frame, at 31250 baud, from a queue of 1024 bytes.
// What the Pi plays is the OSD's (status bits 96..103, the template's
// CONF_STR): MT32-pi on / off, SoundFont or MT-32, the MT-32 ROM, the
// SoundFont: the Pi's own (0, the default) or 0..7 (1..8). The Pi takes all
// three from the core when it first finds it (mt32-pi's mister.cpp), so for
// its own SoundFont the core sends back the one it reported: it may have up
// to 512, which the OSD cannot list. It reports it before it takes anything:
// the core's first answer after a reset is "mute", and the Pi replies to that
// with what it plays. Its sound is mixed in at the MIDI volume while the game has
// MIDI enabled and the OSD has the Pi on. When either goes off it is told to
// stop all its notes (mt32pi's reset: the "mute" request).
//
// The Pi reads the core's settings every 50ms (mt32-pi's
// MisterUpdatePeriodMillis), from a core of its own, also while it loads a
// SoundFont; mt32pi.sv answers on SDA only to a transaction for it. A second
// without an answer and the Pi is gone (unplugged, or its MiSTer support off):
// mt32pi.sv, which would say "available" for ever, is held in reset until the
// Pi answers again, so the OSD lines go, the game hears it is gone and plays
// the FM chip again, and a Pi plugged in again starts as a new one. Its
// "available" goes at every mute as well (a reset): the Pi counts as there
// from the first time it was until it goes quiet, so that a mute neither
// hides the OSD lines nor sends the music to the FM chip for a moment.

reg        midi_en = 0;
reg  [6:0] midi_vol = 7'd32;
reg        midi_prog = 0;     // the program chooses where the music goes (host version 17)
reg        midi_prog_pi = 0;  // and chose the MT32-pi
wire       ring_en = fm_en || midi_en;
// the MT32-pi's synth for the info box, for the block that drives it
reg  [7:0] pi_info = 0;
reg  [3:0] pi_info_cnt = 0;

wire        mt32_available, mt32_newmode;
wire  [7:0] mt32_mode, mt32_rom, mt32_sf;
wire [15:0] mt32_i2s_l, mt32_i2s_r;
reg   [2:0] mt32_avail_s = 0;
reg         mt32_seen = 0;
reg   [3:0] mt32_reports = 0;
wire  [3:0] sf_osd = osd_status[103:100];
wire  [7:0] sf_req = (sf_osd == 0 || sf_osd > 4'd8) ? mt32_sf : {4'd0, sf_osd - 4'd1};
reg   [1:0] pi_sda_s = 2'b11;
reg  [25:0] pi_quiet_t = 0;
wire        pi_quiet = (pi_quiet_t == MT32_QUIET);
assign      mt32_on = mt32_seen && !pi_quiet;
// the OSD's Music on MIDI (bit 87, host version 16) and MT32-pi on, or the
// program's choice of the Pi (MH_MidiDest(), host version 17)
wire        midi_want = midi_en && (midi_prog ? midi_prog_pi : !osd_status[87] && !osd_status[96]);
wire        midi_out  = midi_want && mt32_on;

// the queue
reg   [7:0] mq[0:1023];
reg  [10:0] mq_wr = 0, mq_rd = 0;
wire [10:0] mq_level = mq_wr - mq_rd;
reg   [7:0] mq_q;
reg         mq_put = 0;       // the DDR engine hands over fm_ent's bytes
reg   [1:0] mq_left = 0;
reg  [23:0] mq_bytes;
reg   [1:0] mq_seen = 0;      // a byte is there, as the queue's output has it

always @(posedge clk) begin
	mq_q <= mq[mq_rd[9:0]];
	if (mq_put) begin
		mq_bytes <= fm_ent[55:32];
		mq_left  <= fm_ent[57:56];
	end
	else if (mq_left != 0) begin
		mq[mq_wr[9:0]] <= mq_bytes[7:0];
		mq_wr    <= mq_wr + 1'd1;
		mq_bytes <= mq_bytes >> 8;
		mq_left  <= mq_left - 1'd1;
	end
end

// the UART: 8N1, 1600 clocks of 50MHz a bit
reg   [9:0] tx_sh = 10'h3FF;
reg   [3:0] tx_bits = 0;
reg  [10:0] tx_cnt = 0;
always @(posedge clk) begin
	mq_seen <= {mq_seen[0], mq_level != 0};
	if (tx_bits != 0) begin
		if (tx_cnt == 11'd1599) begin
			tx_cnt  <= 0;
			tx_sh   <= {1'b1, tx_sh[9:1]};
			tx_bits <= tx_bits - 1'd1;
		end
		else tx_cnt <= tx_cnt + 1'd1;
	end
	else if (mq_seen[1] && midi_out) begin
		tx_sh   <= {1'b1, mq_q, 1'b0};
		tx_bits <= 4'd10;
		tx_cnt  <= 0;
		mq_rd   <= mq_rd + 1'd1;
	end
	// nobody to send them to: what is queued goes
	if (!midi_out) mq_rd <= mq_wr;
end

// mute: when the game's MIDI or the OSD's MT32-pi goes off, the OSD's
// "Reset Hanging Notes" (status bit 104, a trigger), and at a reset, long
// enough for the Pi's clock
reg        midi_want_d = 0, notes_trig_d = 0;
reg  [4:0] mute_t = 0;
always @(posedge clk) begin
	midi_want_d  <= midi_want;
	notes_trig_d <= osd_status[104];
	if ((midi_want_d && !midi_want) || (osd_status[104] && !notes_trig_d)) mute_t <= 5'd31;
	else if (mute_t != 0) mute_t <= mute_t - 1'd1;
end

mt32pi mt32pi
(
	.CLK_AUDIO      (clk_audio),
	// no picture of its display here
	.CLK_VIDEO      (clk_vid),
	.CE_PIXEL       (1'b0),
	.VGA_VS         (1'b0),
	.VGA_DE         (1'b0),
	.USER_IN        (user_in),
	.USER_OUT       (user_out),
	.reset          (reset || mute_t != 0 || pi_quiet),
	.midi_tx        (tx_sh[0]),
	.midi_rx        (),
	.mt32_i2s_r     (mt32_i2s_r),
	.mt32_i2s_l     (mt32_i2s_l),
	.mt32_available (mt32_available),
	// the OSD: SoundFont (its first value) or MT-32, ROM, SoundFont
	.mt32_mode_req  (!osd_status[97]),
	.mt32_rom_req   (osd_status[99:98]),
	.mt32_sf_req    (sf_req),
	.mt32_mode      (mt32_mode),
	.mt32_rom       (mt32_rom),
	.mt32_sf        (mt32_sf),
	.mt32_newmode   (mt32_newmode),
	.mt32_lcd_en    (),
	.mt32_lcd_pix   (),
	.mt32_lcd_update()
);

// From the Pi's clock: its sound taken when it was the same twice, the
// settings it reports a few clocks after it says they changed
reg  [15:0] i2s_l1, i2s_r1, i2s_l2, i2s_r2;
reg  signed [15:0] mt32_l = 0, mt32_r = 0;
reg   [3:0] newmode_s = 0;
reg  [23:0] synth = 24'hFFFFFF;  // {mode, ROM, SoundFont} the Pi reported
reg  [23:0] shown = 24'hFFFFFF;  // the one last named in the info box
// info texts: 1..8 SoundFont 0..7, 9..11 the MT-32 ROMs, 12 unknown, 13 a
// SoundFont after the eighth
wire  [7:0] info_n = (synth[23:16] == 8'hA2) ? ((synth[7:0] <= 8'd7) ? 8'd1 + synth[2:0] : 8'd13)
                   : (synth[23:16] == 8'hA1 && synth[15:8] <= 8'd2) ? 8'd9 + synth[15:8] : 8'd12;
// and for the program (the MIDI status, host version 17): 0 while there is none
wire  [7:0] pi_synth = (!mt32_on || synth == 24'hFFFFFF) ? 8'd0 : info_n;
always @(posedge clk) begin
	mt32_avail_s <= {mt32_avail_s[1:0], mt32_available};
	pi_sda_s     <= {pi_sda_s[0], user_out[0]};
	if (!pi_sda_s[1]) pi_quiet_t <= 0;
	else if (!pi_quiet) pi_quiet_t <= pi_quiet_t + 1'd1;
	if (pi_quiet) mt32_seen <= 0;
	else if (mt32_avail_s[2]) mt32_seen <= 1;
	i2s_l1 <= mt32_i2s_l;
	i2s_r1 <= mt32_i2s_r;
	i2s_l2 <= i2s_l1;
	i2s_r2 <= i2s_r1;
	if (i2s_l1 == i2s_l2) mt32_l <= i2s_l2;
	if (i2s_r1 == i2s_r2) mt32_r <= i2s_r2;

	// "MT32-pi: SoundFont 3" in the info box while the music goes to the Pi:
	// when it starts to, and when the Pi reports another synth (it reports
	// the same one again after a mute). A core whose game plays no MIDI has
	// none, nor the CONF_STR's "I" line. The DDR engine's block asks for it.
	newmode_s <= {newmode_s[2:0], mt32_newmode};
	if (newmode_s[3] != newmode_s[2]) begin
		synth        <= {mt32_mode, mt32_rom, mt32_sf};
		mt32_reports <= mt32_reports + 1'd1;
	end
	if (!midi_out) shown <= 24'hFFFFFF;
	else if (synth != 24'hFFFFFF && synth != shown) begin
		shown       <= synth;
		pi_info     <= info_n;
		pi_info_cnt <= pi_info_cnt + 1'd1;
	end
end

// at the MIDI volume, 32 as it comes
wire signed [23:0] mt32_l_v = mt32_l * $signed({1'b0, midi_vol});
wire signed [23:0] mt32_r_v = mt32_r * $signed({1'b0, midi_vol});
assign mt32_snd_l = midi_out ? mt32_l_v[23:5] : 19'sd0;
assign mt32_snd_r = midi_out ? mt32_r_v[23:5] : 19'sd0;

//////////////////////////////////////////////////////////////////
// MIDI synthesizer (hybrid_midi.sv)
//
// On its own 100MHz clock. The DDR engine below reads for it: the commands
// of the MIDI ring, one at a time into its parameter slots, as soon as the
// game has written them and a slot is free, and the samples its fetcher asks
// for. It makes each audio frame in the one before; its sound is taken at
// the strobe that counts aud_play to that frame.

reg [31:0] midi_wcount = 0;
wire       midi_cmd_req, midi_smp_req;
wire [13:0] midi_cmd_addr;
wire [22:0] midi_smp_addr;
wire  [3:0] midi_smp_len;
reg        midi_cmd_ack = 0, midi_smp_ack = 0;
wire       midi_beat;
wire [31:0] midi_read, midi_frame;
wire [15:0] midi_under;
wire  [7:0] midi_voices;
wire [127:0] midi_bits;
wire       clk_midi;

generate
if (MIDI_SYNTH) begin : midi_synth
	pll_midi pll_midi
	(
		.refclk(refclk),
		.rst(1'b0),
		.outclk_0(clk_midi),
		.locked()
	);

	hybrid_midi midi
	(
		.clk       (clk),
		.mclk      (clk_midi),
		.en        (midi_en),
		.wcount    (midi_wcount),
		.volume    (midi_vol),
		.frame_stb (aud_tick2 && audio_en && afifo_level != 0),
		.frame     (aud_play + 1'd1),
		.cmd_req   (midi_cmd_req),
		.cmd_addr  (midi_cmd_addr),
		.cmd_ack   (midi_cmd_ack),
		.smp_req   (midi_smp_req),
		.smp_addr  (midi_smp_addr),
		.smp_len   (midi_smp_len),
		.smp_ack   (midi_smp_ack),
		.beat      (midi_beat),
		.dout      (ddr_dout),
		.out_l     (midi_l),
		.out_r     (midi_r),
		.st_read   (midi_read),
		.st_under  (midi_under),
		.st_voices (midi_voices),
		.st_bits   (midi_bits),
		.st_frame  (midi_frame),
		.dbg_sums  (),
		.dbg_out   (),
		.dbg_frame (),
		.dbg_tgl   ()
	);
end else begin : no_midi_synth
	// no synthesizer: no commands or samples asked for, silence, and its
	// status 0 (the library uses it only with status word 1 bit 28)
	assign clk_midi      = 1'b0;
	assign midi_cmd_req  = 1'b0;
	assign midi_cmd_addr = 14'd0;
	assign midi_smp_req  = 1'b0;
	assign midi_smp_addr = 23'd0;
	assign midi_smp_len  = 4'd0;
	assign midi_l        = 16'sd0;
	assign midi_r        = 16'sd0;
	assign midi_read     = 32'd0;
	assign midi_under    = 16'd0;
	assign midi_voices   = 8'd0;
	assign midi_bits     = 128'd0;
	assign midi_frame    = 32'd0;
end
endgenerate

//////////////////////////////////////////////////////////////////
// Input state

reg [511:0] keys = 0;
// OSD triggers, bits 56..59: flipped at each pulse; bit 16 (Save Diagnostic
// Report) at each pulse's end
reg   [3:0] trig_toggle = 0, trig_prev = 0;
reg         diag_toggle = 0, diag_prev = 0;
// OSD_STATUS comes from the framework's clock
reg   [1:0] osd_open_s = 0;
reg         old_key_stb = 0;
reg  [31:0] mouse_x = 0, mouse_y = 0;
reg  [15:0] mouse_wheel = 0;
reg   [2:0] mouse_btn = 0;
reg         old_mouse_stb = 0;

always @(posedge clk) begin
	trig_prev   <= osd_status[59:56];
	trig_toggle <= trig_toggle ^ (osd_status[59:56] & ~trig_prev);
	diag_prev   <= osd_status[16];
	diag_toggle <= diag_toggle ^ (diag_prev & ~osd_status[16]);
	osd_open_s  <= {osd_open_s[0], osd_open};
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
localparam S_CLEAR     = 8;
localparam S_FM_WAIT   = 9;
localparam S_STAT_HI   = 10;
localparam S_MSG_RD    = 11;
localparam S_MSG_HDR   = 12;
localparam S_MSG_BODY  = 13;
localparam S_CLEAR2    = 14;
localparam S_MIDI_WAIT = 15;
localparam S_MIDI_STAT = 16;

reg  [4:0] state = S_IDLE;

// the words of a read for the MIDI synthesizer
assign midi_beat = ddr_dout_ready && state == S_MIDI_WAIT;
// The control block is not read before the core has cleared its magic, in
// its first 4 vblanks (more than once: the memory may not be there for the
// very first write). When the core starts, and at the game's reset (host
// version 18), not with the reset while the core loads.
reg  [2:0] ctrl_clear = 3'd4;
reg        line_req = 0;
reg [15:0] late_lines = 0;
reg        vbl_req = 0;
reg  [9:0] fetch_row;
reg        fetch_bank;
// the flicker filter: the line's rows above and below as well, each a burst
// of its own into its line buffer (fetch_part 1 and 2) after the row's (0)
reg  [9:0] fetch_up, fetch_dn;
reg        fetch_filt = 0;
reg  [1:0] fetch_part = 0;
reg  [7:0] rd_cnt;          // beats received in the current burst
reg  [7:0] rd_len;          // beats expected in the current burst
reg  [7:0] line_words;      // words fetched so far for the current line
reg  [3:0] wr_beat;

// requests of the video side
reg  [2:0] req_s = 0, vbl_s = 0;
wire       new_req = req_s[2] != req_s[1];

// A reset (see the top): until the core has started (`ran`, its intro over)
// the core's own, for as long as it is held; after that the game's, at its
// press alone, counted in `resets`, without the intro again
reg        ran = 0;
reg        rst_d = 0;
reg  [3:0] resets = 0;
wire       rst_in   = reset | osd_status[0];
wire       host_rst = ran ? rst_in & ~rst_d : rst_in;
wire       replay   = INTRO != 0 && !ran;
reg [19:0] idle_cnt = 0;

// framebuffer address of a row: the framebuffer + row * words in a row, 40
// at 320 pixels of 8bpp, 160 at most (640 of RGB565): one burst
wire  [6:0] row_bytes = (s_mode == 4'd0 || s_mode == 4'd3) ? 7'd40 : 7'd80;    // words in a row of 8bpp
wire  [7:0] row_len = fmt16 ? {row_bytes, 1'b0} : {1'b0, row_bytes};
wire [28:0] fb_addr = FB_ADDR + {10'd0, fb_index, 17'd0};
wire  [9:0] part_row = (fetch_part == 2'd1) ? fetch_up : (fetch_part == 2'd2) ? fetch_dn : fetch_row;
wire [28:0] row_addr = fb_addr + part_row * row_len;

// the game gets the screen once the core's picture has faded out; one that
// asks for the overlay never does. INTRO 0: at once, when a frame starts
// (interlaced, both fields of a frame are of the same picture, but with
// field rate)
wire       ctrl_magic = (ctrl_w0[31:0] == CTRL_MAGIC);
wire       field_rate = ctrl_magic && ctrl_w2[60];
// interlaced, the field with the even rows was shown: the frame's second
// field comes next, of the same frame unless the program has field rate
wire       lace_hold  = u_lace && run && vbl_field && !field_rate;
wire       ovl_want   = ctrl_magic && ctrl_w0[58];
assign     ovl_out_req = ctrl_magic && ctrl_w2[58];
assign     ovl_rise    = ctrl_w2[59];
wire       game_take  = ctrl_magic && !ovl_want && (game_on || wall_level == 0 || (INTRO == 0 && !lace_hold));
wire [9:0] pal_want   = !ovl_on ? 10'd256 : (wall_level != 9'd256) ? {1'b1, wall_level} : {1'b0, lv_ovl};

reg [63:0] stat_word;
always @(*) begin
	case (wr_beat)
		4'd0:  stat_word = {frame_cnt, STATUS_MAGIC};
		4'd1:  stat_word = {VERSION, fr_on, ovl_on && ovl_out_req && ovl_t == OVL_T_IN, ovl_on, MIDI_SYNTH != 0, mt32_reports, mt32_on, osd_open_s[1], msg_timeout, msg_show, game_on, fast, run, 1'b0, s_mode, vbl_field, ctrl_valid, u_lace, fmt16, resets, filt_req, FLICKER != 0, fb_index};
		4'd2:  stat_word = {joystick_1, joystick_0};
		4'd3:  stat_word = {joy_r_analog_1, joy_l_analog_1, joy_r_analog_0, joy_l_analog_0};
		4'd4:  stat_word = {osd_status[63:60], trig_toggle, osd_status[55:17], diag_toggle, osd_status[15:0]};
		4'd5:  stat_word = {mouse_y, mouse_x};
		4'd6:  stat_word = {late_lines, 13'd0, filt_tgl, filt_lvl, mouse_wheel, 13'd0, mouse_btn};
		4'd7:  stat_word = {fm_fetch, 3'd0, vclk_count, 8'd0, vclk_code};
		default: stat_word = keys[{wr_beat[2:0], 6'd0} +: 64];
	endcase
end

always @(posedge clk) begin
	lbuf_we <= 0;
	pal_we <= 0;
	status_set <= 0;
	msg_we <= 0;
	afifo_we <= 0;
	fade_start <= 0;
	if (fm_take) fm_ent_valid <= 0;
	// an entry nobody takes is skipped; MIDI bytes go into the MT32-pi's
	// queue at their frame, when there is room for three
	mq_put <= 0;
	if (fm_ent_valid && !fm_take && !mq_put) begin
		if (ent_midi) begin
			if (!midi_out) fm_ent_valid <= 0;
			else if (ent_due && mq_left == 0 && mq_level <= 11'd1021) begin
				mq_put       <= 1;
				fm_ent_valid <= 0;
			end
		end
		else if (!fm_en) fm_ent_valid <= 0;
	end

	// requests from the video timing; what comes with them was set before
	// the request and stays until the next one
	req_s <= {req_s[1:0], req_tgl};
	vbl_s <= {vbl_s[1:0], vbl_tgl};
	if (new_req) begin
		if (line_req || state == S_LINE_WAIT) late_lines <= late_lines + 1'd1;
		line_req   <= 1;
		fetch_row  <= req_row;
		fetch_bank <= req_bank;
		fetch_up   <= req_up;
		fetch_dn   <= req_dn;
		fetch_filt <= req_filt;
		fetch_part <= 2'd0;
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
			S_CTRL_WAIT: if (rd_cnt == 0) ctrl_w0 <= ddr_dout; else if (rd_cnt == 1) ctrl_w1 <= ddr_dout; else ctrl_w2 <= ddr_dout;
			S_MSG_HDR:   msg_hdr <= ddr_dout;
			S_MSG_BODY: begin
				msg_we    <= 1;
				msg_waddr <= rd_cnt[5:0];
				msg_wdata <= ddr_dout;
			end
			S_PAL_WAIT: begin
				pal_we    <= 1;
				pal_waddr <= rd_cnt[6:0];
				// the overlay fading in: each colour from the background's
				pal_wdata <= (pal_lv == 10'd256) ? ddr_dout
				           : {8'd0, mix(pal_lv[9] ? 24'd0 : WALL_BG, ddr_dout[55:32], pal_lv[8:0]),
				              8'd0, mix(pal_lv[9] ? 24'd0 : WALL_BG, ddr_dout[23:0], pal_lv[8:0])};
			end
			S_AUD_WAIT: begin
				afifo_we    <= 1;
				afifo_wdata <= ddr_dout;
			end
			S_FM_WAIT: begin
				fm_ent       <= ddr_dout;
				fm_ent_valid <= 1;
				fm_fetch     <= fm_fetch + 1'd1;
			end
			S_LINE_WAIT: begin
				lbuf_we    <= 1;
				lbuf_wpart <= fetch_part;
				lbuf_waddr <= {fetch_bank, line_words};
				lbuf_wdata <= ddr_dout;
				line_words <= line_words + 1'd1;
			end
			default: ;
		endcase
	end

	midi_cmd_ack <= 0;
	midi_smp_ack <= 0;

	if (!ddr_busy) begin
		ddr_rd <= 0;

		case (state)
			S_IDLE:
				if (line_req) begin
					line_req     <= 0;
					line_words   <= 0;
					rd_cnt       <= 0;
					rd_len       <= row_len;
					ddr_addr     <= row_addr;
					ddr_burstcnt <= row_len;
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
				// the MIDI synthesizer's samples (the ack's clock: its
				// request is still up, the next one not yet)
				else if (midi_smp_req && !midi_smp_ack && !midi_cmd_ack) begin
					rd_cnt       <= 0;
					rd_len       <= {4'd0, midi_smp_len};
					ddr_addr     <= SMP_ADDR + {6'd0, midi_smp_addr};
					ddr_burstcnt <= {4'd0, midi_smp_len};
					ddr_rd       <= 1;
					midi_smp_ack <= 1;
					state        <= S_MIDI_WAIT;
				end
				else if (ring_en && !fm_ent_valid && !fm_take && !mq_put && fm_fetch != fm_wcount) begin
					rd_cnt       <= 0;
					rd_len       <= 8'd1;
					ddr_addr     <= FM_ADDR + {17'd0, fm_fetch[11:0]};
					ddr_burstcnt <= 8'd1;
					ddr_rd       <= 1;
					state        <= S_FM_WAIT;
				end
				else if (midi_cmd_req && !midi_smp_ack && !midi_cmd_ack) begin
					rd_cnt       <= 0;
					rd_len       <= 8'd16;
					ddr_addr     <= MIDI_ADDR + {15'd0, midi_cmd_addr};
					ddr_burstcnt <= 8'd16;
					ddr_rd       <= 1;
					midi_cmd_ack <= 1;
					state        <= S_MIDI_WAIT;
				end
				else if (vbl_req && ctrl_clear != 0) begin
					vbl_req <= 0;
					state   <= S_CLEAR;
				end
				else if (vbl_req) begin
					vbl_req      <= 0;
					rd_cnt       <= 0;
					rd_len       <= 8'd3;
					ddr_addr     <= CTRL_ADDR;
					ddr_burstcnt <= 8'd3;
					ddr_rd       <= 1;
					state        <= S_CTRL_WAIT;
				end

			S_LINE_WAIT:
				if (rd_cnt == rd_len) begin
					state <= S_IDLE;
					// the flicker filter: the rows above and below next
					if (fetch_filt && fetch_part != 2'd2 && !new_req) begin
						fetch_part <= fetch_part + 1'd1;
						line_req   <= 1;
					end
				end

			S_AUD_WAIT:
				// wait for the FIFO write of the last beat as well
				if (rd_cnt == rd_len && !afifo_we) begin
					aud_fetch <= aud_fetch + 32'd32;
					state     <= S_AUD_PTR;
				end

			S_FM_WAIT:
				if (rd_cnt == rd_len) state <= S_IDLE;

			S_MIDI_WAIT:
				if (rd_cnt == rd_len) state <= S_IDLE;

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

			S_CLEAR:
				// the control block's 3 words, then the status as in every vblank
				if (!ddr_we) begin
					ddr_we       <= 1;
					ddr_addr     <= CTRL_ADDR;
					ddr_burstcnt <= 8'd3;
					ddr_din      <= 64'd0;
					wr_beat      <= 1;
				end else if (wr_beat != 3) begin
					wr_beat <= wr_beat + 1'd1;
				end else begin
					ddr_we     <= 0;
					ctrl_clear <= ctrl_clear - 1'd1;
					state      <= S_CLEAR2;
				end

			S_CLEAR2:
				// and the magic of a message
				if (!ddr_we) begin
					ddr_we       <= 1;
					ddr_addr     <= MSG_ADDR;
					ddr_burstcnt <= 8'd1;
					ddr_din      <= 64'd0;
				end else begin
					ddr_we     <= 0;
					wr_beat    <= 0;
					state      <= S_STAT;
				end

			// the message: its first word every vblank, the lines when that changed
			S_MSG_RD: begin
				rd_cnt       <= 0;
				rd_len       <= 8'd1;
				ddr_addr     <= MSG_ADDR;
				ddr_burstcnt <= 8'd1;
				ddr_rd       <= 1;
				state        <= S_MSG_HDR;
			end

			S_MSG_HDR:
				if (rd_cnt == rd_len) begin
					wr_beat <= 0;
					state   <= S_STAT;
					if (msg_hdr[31:0] != MSG_MAGIC) begin
						msg_show   <= 0;
						msg_loaded <= 0;
					end
					else if (msg_loaded && msg_hdr[47:32] == msg_seq) begin
						// the same lines; the selected one may have moved
						msg_err  <= msg_hdr[48];
						msg_list <= msg_hdr[49];
						msg_sel  <= msg_hdr[51:50];
					end
					else begin
						rd_cnt       <= 0;
						rd_len       <= 8'd40;
						ddr_addr     <= MSG_ADDR + 29'd1;
						ddr_burstcnt <= 8'd40;
						ddr_rd       <= 1;
						state        <= S_MSG_BODY;
					end
				end

			S_MSG_BODY:
				if (rd_cnt == rd_len && !msg_we) begin
					msg_loaded <= 1;
					msg_seq    <= msg_hdr[47:32];
					msg_err    <= msg_hdr[48];
					msg_list   <= msg_hdr[49];
					msg_sel    <= msg_hdr[51:50];
					msg_show   <= 1;
					wr_beat    <= 0;
					state      <= S_STAT;
				end

			S_CTRL_WAIT:
				if (rd_cnt == rd_len) state <= S_CTRL;

			S_CTRL: begin
				ctrl_valid <= ctrl_magic;
				audio_en <= ctrl_magic && ctrl_w0[56];
				fm_en    <= ctrl_magic && ctrl_w0[56] && ctrl_w0[57];
				fm_vol   <= ctrl_w0[55:49];
				fm_wcount <= ctrl_w1[63:32];
				midi_en     <= ctrl_magic && ctrl_w0[56] && ctrl_w2[39];
				midi_vol    <= ctrl_w2[38:32];
				midi_prog    <= ctrl_magic && ctrl_w2[56];
				midi_prog_pi <= ctrl_w2[57];
				midi_wcount <= ctrl_w2[31:0];
				// an info box asked for: shown once the OSD menu is closed
				if (ctrl_magic && ctrl_w2[55:48] != info_seen) begin
					info_seen <= ctrl_w2[55:48];
					if (ctrl_w2[47:40] != 0) begin
						info_pend <= 1;
						info_txt  <= ctrl_w2[47:40];
					end
				end
				menumask <= ctrl_magic ? ctrl_w0[63:60] : 4'd0;
				game_on  <= game_take;
				fr_on    <= u_lace && field_rate;
				// the line Light (the default, Danik 2026-10-28), Strong, Off
				// (3 as Off), as the level 1, 2, 0 the program sees
				filt_req <= FLICKER != 0 && !osd_status[106];
				filt_lvl <= FLICKER == 0 || osd_status[106] ? 2'd0 : osd_status[105] ? 2'd2 : 2'd1;
				// the program sets the OSD's Flicker Filter line (dnkLauncher's
				// own row): Main_MiSTer takes the status written back as the
				// player's change. The momentary bits (Reset, the triggers,
				// Save Diagnostic Report, Reset Hanging Notes) go back as 0
				if (FLICKER != 0 && ctrl_magic && ctrl_w2[61] != filt_tgl) begin
					filt_tgl   <= ctrl_w2[61];
					// the level as the line's entry: Off 2, Light 0, Strong (3 as Strong) 1
					status_out <= {osd_status[127:107], ctrl_w2[63:62] == 2'd0, ctrl_w2[63], 1'b0, osd_status[103:60], 4'd0,
					               osd_status[55:17], 1'b0, osd_status[15:1], 1'b0};
					status_set <= 1;
				end
				if (ctrl_magic) begin
					// interlaced, both fields are of the same frame: a new one
					// is taken when the field with the odd rows has been shown,
					// or at every field with field rate
					if (!lace_hold)
						fb_index <= (ctrl_w0[39:32] > 8'd2) ? 2'd0 : ctrl_w0[33:32];
					fmt16    <= ctrl_w0[40];
				end
				// a mode the core does not have is 320x200
				if (game_take) vmode <= (ctrl_w0[47:44] > 4'd7 || ctrl_w0[47:44] == 4'd5 || ctrl_w0[47:44] == 4'd6) ? 4'd0 : ctrl_w0[47:44];
				// no game, or not on the screen yet: the core's picture, in 640x480
				else vmode <= 4'd4;
				// the picture's next field: going out for a game once the intro
				// is over (or before it began), black again when one has left,
				// else the intro, while there is an output
				// no game for WALL_TIMEOUT fields after the core started: the
				// core's own message
				if (ctrl_magic) begin
					attached   <= 1;
					msg_timeout <= 0;
				end
				else if (!attached && run && !msg_timeout) begin
					if (alone_t == WALL_TIMEOUT) msg_timeout <= 1;
					else alone_t <= alone_t + 1'd1;
				end
				if (!game_take) begin
					fade_start <= 1;
					if (ctrl_magic && !ovl_want && (!intro_on || intro_done)) begin
						// INTRO 0: no going out, the game has the screen at the next frame
						if (INTRO != 0) wall_level <= (wall_level < WALL_STEP) ? 9'd0 : wall_level - WALL_STEP;
						if (intro_on) dot_t <= dot_t + 1'd1;
						msg_keep   <= 0;
					end
					// the overlay: its handover, then the picture as it is
					else if (ovl_on && ovl_want) begin
						// asked to go out: its fade-in backwards, from its end
						if (ovl_out_req && ovl_t > OVL_T_IN)
							ovl_t <= (ovl_t > OVL_T_IN + OVL_D_IN) ? 7'd0 + OVL_T_IN + OVL_D_IN - 1'd1 : ovl_t - 1'd1;
						else if (!(ovl_out_req && ovl_t == OVL_T_IN) && ovl_t != 7'd127) ovl_t <= ovl_t + 1'd1;
						if (ovl_t >= OVL_T_GAP) msg_keep <= 0;
						dot_t <= dot_t + 1'd1;
					end
					else if (game_on || ovl_on) begin
						wall_hold  <= WALL_HOLD_LEFT;
						wall_level <= 0;
						intro_on   <= 0;
						intro_skip <= 0;
						intro_t    <= 0;
						dot_t      <= 0;
						ovl_on     <= 0;
						msg_keep   <= 0;
					end
					else if (!run) ;
					// a message: no black, and the picture as at the end of
					// its intro, fading in; INTRO 0 that picture, fading in,
					// after a game has left
					else if (wall_hold != 0) wall_hold <= msg_show ? 8'd0 : wall_hold - 1'd1;
					else begin
						if (!intro_on && (msg_show || INTRO == 0)) begin
							intro_skip <= 1;
							intro_t    <= WALL_T_END;
						end
						intro_on   <= 1;
						wall_level <= !(intro_skip || (!intro_on && (msg_show || INTRO == 0))) ? 9'd256
						            : (wall_level > 9'd256 - WALL_STEP) ? 9'd256 : wall_level + WALL_STEP;
						if (intro_on && intro_t != 9'd511) intro_t <= intro_t + 1'd1;
						if (intro_on && intro_t >= WALL_T_DOTS) dot_t <= dot_t + 1'd1;
						// the overlay's handover begins once the intro is over
						// and the picture is all there
						if (ovl_want && intro_on && intro_done && wall_level == 9'd256) begin
							ovl_on   <= 1;
							ovl_t    <= 0;
							msg_keep <= msg_show;
						end
					end
				end
				// the overlay given up: gone with the picture, once the game has the
				// screen, which is black until the game's first frame
				if (game_take && ovl_on) begin
					ovl_on   <= 0;
					ovl_left <= 1;
					ovl_fb   <= fb_index;
				end
				else if (ovl_left && (!game_take || fb_index != ovl_fb)) ovl_left <= 0;
				// a level of the overlay's at every field, as the picture's
				if (ctrl_magic && (!pal_loaded || ctrl_w1[31:0] != pal_seq || pal_want != pal_lv)) begin
					pal_loaded   <= 1;
					pal_seq      <= ctrl_w1[31:0];
					pal_lv       <= pal_want;
					rd_cnt       <= 0;
					rd_len       <= 8'd128;
					ddr_addr     <= PAL_ADDR + (ctrl_w0[48] ? 29'h80 : 29'h0);
					ddr_burstcnt <= 8'd128;
					ddr_rd       <= 1;
					state        <= S_PAL_WAIT;
				end else begin
					state <= S_MSG_RD;
				end
			end

			S_PAL_WAIT:
				if (rd_cnt == rd_len) state <= S_MSG_RD;

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
					state  <= S_STAT_HI;
				end else begin
					ddr_din <= stat_word;
					wr_beat <= wr_beat + 1'd1;
				end
			end

			S_STAT_HI:
				// the upper half of the OSD status: single beat write
				if (!ddr_we) begin
					ddr_we       <= 1;
					ddr_addr     <= STAT_HI;
					ddr_burstcnt <= 8'd1;
					ddr_din      <= osd_status[127:64];
				end else begin
					ddr_we  <= 0;
					wr_beat <= 0;
					state   <= S_MIDI_STAT;
				end

			S_MIDI_STAT:
				// the MIDI synthesizer's status: a burst of 4
				if (!ddr_we) begin
					ddr_we       <= 1;
					ddr_addr     <= MSTAT_ADDR;
					ddr_burstcnt <= 8'd4;
					ddr_din      <= {pi_synth, midi_voices, midi_under, midi_read};
					wr_beat      <= 1;
				end else if (wr_beat == 4'd4) begin
					ddr_we <= 0;
					state  <= S_IDLE;
				end else begin
					ddr_din <= wr_beat == 4'd1 ? midi_bits[63:0] : wr_beat == 4'd2 ? midi_bits[127:64] : {32'd0, midi_frame};
					wr_beat <= wr_beat + 1'd1;
				end

			default: state <= S_IDLE;
		endcase
	end

	rst_d <= rst_in;
	if (intro_done) ran <= 1;
	if (host_rst) begin
		// the game's: its control block forgotten, as when the core starts,
		// and the HPS side told (status word 1 bits 7..4)
		if (ran) begin
			ctrl_clear <= 3'd4;
			resets     <= resets + 1'd1;
		end
		state      <= S_IDLE;
		ddr_rd     <= 0;
		ddr_we     <= 0;
		line_req   <= 0;
		vbl_req    <= 0;
		ctrl_valid <= 0;
		game_on    <= 0;
		// INTRO 0, or the game's reset: the picture at the end of its intro
		// at once
		wall_hold  <= replay ? WALL_HOLD : 8'd0;
		wall_level <= replay ? 9'd0 : 9'd256;
		intro_on   <= !replay;
		intro_skip <= !replay;
		ovl_on     <= 0;
		msg_keep   <= 0;
		ovl_left   <= 0;
		attached   <= 0;
		alone_t    <= 0;
		msg_timeout <= 0;
		msg_show   <= 0;
		msg_loaded <= 0;
		intro_t    <= replay ? 9'd0 : 9'd0 + WALL_T_END;
		dot_t      <= 0;
		fade_start <= 1;
		pal_loaded <= 0;
		audio_en   <= 0;
		fm_en      <= 0;
		midi_en    <= 0;
		menumask   <= 0;
		info_pend  <= 0;
	end
	// the info box: the MT32-pi's synth as soon as it changes, the game's
	// text once the OSD menu is closed; hps_io takes `info` at the rising
	// edge of info_req
	info_req <= 0;
	if (pi_info_cnt != pi_info_seen && !info_req) begin
		pi_info_seen <= pi_info_cnt;
		info         <= pi_info;
		info_req     <= 1;
	end
	else if (info_pend && !osd_open_s[1] && !info_req) begin
		info      <= info_txt;
		info_req  <= 1;
		info_pend <= 0;
	end
	if (!audio_en) aud_fetch <= 0;
	if (!ring_en) begin
		fm_fetch     <= 0;
		fm_ent_valid <= 0;
	end
end

//////////////////////////////////////////////////////////////////
// Pixel pipeline (clk_vid)
//
// ce edge E0: counters advance. One clock later the line buffer word is
// read, another one later the palette (or the RGB565 pixel is selected, or
// the pixel of the core's own picture is there).
// RGB and syncs for the same pixel are registered at the first ce after
// that: the next one at 15kHz, the second one at 31kHz, where a ce comes
// every other clock. Blanking and syncs wait one ce for that there.

wire       hbl_now    = (hc >= H_ACTIVE);
wire       vbl_now    = (vc >= V_ACTIVE);
wire       hs_now     = (hc >= HS_START && hc < HS_END);
wire       vs_now     = (vc >= VS_START && vc < VS_END);

wire [3:0] now = {hbl_now, vbl_now, hs_now, vs_now};
reg  [3:0] now_d1;
wire [3:0] out = v_fast ? now_d1 : now;
wire       o_hbl, o_vbl, o_hs, o_vs;
assign {o_hbl, o_vbl, o_hs, o_vs} = out;

always @(posedge clk_vid) byte_sel <= hc[2:0];

always @(posedge clk_vid) begin
	ce_pix <= ce;
	if (ce) begin
		now_d1 <= now;

		hblank <= o_hbl;
		vblank <= o_vbl;
		hsync  <= o_hs;
		// interlaced: half a line later in the field with the even rows
		if (v_lace && field) begin
			if (hc == HS_HALF) vsync <= vs_now;
		end else begin
			vsync <= o_vs;
		end

		if (o_hbl | o_vbl) begin
			{r, g, b} <= 24'd0;
		end else if (!v_valid) begin
			// the core's picture until the HPS side publishes a valid control
			// block, and under the overlay
			{r, g, b} <= (v_ovl && !pix_clear) ? (pal_hi ? pal_q[55:32] : pal_q[23:0]) : wall_rgb;
		end else if (v_fmt16 && v_filt && v_light) begin
			r <= {pix16_l[15:11], pix16_l[15:13]};
			g <= {pix16_l[10:5], pix16_l[10:9]};
			b <= {pix16_l[4:0], pix16_l[4:2]};
		end else if (v_fmt16 && v_filt) begin
			r <= {pix16_f[15:11], pix16_f[15:13]};
			g <= {pix16_f[10:5], pix16_f[10:9]};
			b <= {pix16_f[4:0], pix16_f[4:2]};
		end else if (v_filt && v_light) begin
			{r, g, b} <= {lfilt8_r[10:3], lfilt8_g[10:3], lfilt8_b[10:3]};
		end else if (v_filt) begin
			{r, g, b} <= {filt8_r[9:2], filt8_g[9:2], filt8_b[9:2]};
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
		hblank <= 1;
		vblank <= 1;
		hsync  <= 0;
		vsync  <= 0;
		{r, g, b} <= 24'd0;
	end
end

endmodule
