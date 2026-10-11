//============================================================================
//
//  MiSTer hybrid core host - the FM chip (YM3812 / OPL2, "AdLib")
//
//  A game that made its music with an AdLib card's chip does not have to
//  work that chip out on the ARM: it sends the register writes and the chip
//  is here (JTOPL by Jose Tejada, rtl/jtopl/, GPL-3).
//
//  hybrid_host.sv hands over one write at a time, each with the number of
//  the audio frame it belongs to. The write is made when that frame is
//  played (at once if that is past), so the music keeps the game's own
//  timing to a 44.1kHz frame whatever the game's frame rate does, and stays
//  where it was against the game's sound effects.
//
//  The chip runs from its own 3.579545MHz enable and makes 49716 samples a
//  second; the host takes the newest one at each 44.1kHz frame.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 3 of the License, or (at your option)
//  any later version.
//
//============================================================================

module hybrid_fm
(
	input             clk,          // 50MHz
	input             en,           // 0: the chip is held in reset and silent
	input       [6:0] volume,       // 32 = as it comes, up to 127 = four times

	input      [31:0] frame,        // number of the audio frame being played
	// the next write: valid while `wr_valid`; `wr_take` for one clock when it
	// has been made
	input             wr_valid,
	input      [31:0] wr_frame,
	input       [7:0] wr_reg,
	input       [7:0] wr_data,
	output reg        wr_take,

	output reg signed [15:0] snd    // after `volume`, held at the limits
);

// 3.579545MHz from 50MHz: 715909 / 10000000
reg [23:0] cen_acc = 0;
reg        cen = 0;
always @(posedge clk) begin
	cen <= 0;
	if (cen_acc + 24'd715909 >= 24'd10000000) begin
		cen_acc <= cen_acc + 24'd715909 - 24'd10000000;
		cen     <= 1;
	end else begin
		cen_acc <= cen_acc + 24'd715909;
	end
end

// The chip wants its reset for some of its clocks, and a write only when it
// is out of it.
reg  [7:0] rst_cnt = 0;
wire       rst = !rst_cnt[7];
always @(posedge clk) begin
	if (!en) rst_cnt <= 0;
	else if (cen && rst) rst_cnt <= rst_cnt + 1'd1;
end

// One write is two bus cycles, the register's number and then its value.
// The chip takes a value into the operator it belongs to when that operator
// comes round, once in 72 of its clocks, and forgets it at the next write:
// so the next one waits 128 of them (36us).
localparam W_IDLE = 0, W_ADDR = 1, W_GAP = 2, W_DATA = 3, W_HOLD = 4;
reg  [2:0] wstate = W_IDLE;
reg  [7:0] hold = 0;
reg        bus_wr = 0;
reg        bus_a = 0;
reg  [7:0] bus_d = 0;
// frames are counted round and round: due when it is not in the future
wire [31:0] late = frame - wr_frame;
wire        due = !late[31];

always @(posedge clk) begin
	wr_take <= 0;
	bus_wr  <= 0;
	case (wstate)
		W_IDLE: if (!rst && wr_valid && !wr_take && due) begin
			bus_wr <= 1;
			bus_a  <= 0;
			bus_d  <= wr_reg;
			wstate <= W_ADDR;
		end
		W_ADDR: wstate <= W_GAP;
		W_GAP: begin
			bus_wr <= 1;
			bus_a  <= 1;
			bus_d  <= wr_data;
			wstate <= W_DATA;
		end
		W_DATA: begin
			wr_take <= 1;
			hold    <= 0;
			wstate  <= W_HOLD;
		end
		W_HOLD: if (cen) begin
			hold <= hold + 1'd1;
			if (hold[7]) wstate <= W_IDLE;
		end
		default: wstate <= W_IDLE;
	endcase
	if (rst) wstate <= W_IDLE;
end

wire signed [15:0] chip_snd;

jtopl2 chip
(
	.rst    (rst),
	.clk    (clk),
	.cen    (cen),
	.din    (bus_d),
	.addr   (bus_a),
	.cs_n   (1'b0),
	.wr_n   (!bus_wr),
	.dout   (),
	.irq_n  (),
	.snd    (chip_snd),
	.sample ()
);

wire signed [23:0] scaled = chip_snd * $signed({1'b0, volume});
wire signed [18:0] level = scaled[23:5];
always @(posedge clk) begin
	if (rst) snd <= 0;
	else if (level > 19'sd32767) snd <= 16'sd32767;
	else if (level < -19'sd32768) snd <= -16'sd32768;
	else snd <= level[15:0];
end

endmodule
