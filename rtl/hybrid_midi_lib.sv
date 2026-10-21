//============================================================================
//
//  MiSTer hybrid core host - MIDI synthesizer, building blocks
//
//  A FIFO between two clocks, a FIFO in one clock and a table read from a
//  file, for hybrid_midi.sv and its parts.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//============================================================================

// A FIFO from one clock to another, with Gray-coded pointers. Each side is
// emptied by its own clear, which must last a few clocks of both (the
// synthesizer's enable, which changes at most once a field).
module hybrid_midi_afifo
#(
	parameter W = 64,
	parameter A = 4              // 2^A entries
)
(
	input              wclk,
	input              wclr,
	input              we,
	input      [W-1:0] wdata,
	output             wfull,

	input              rclk,
	input              rclr,
	input              re,       // takes rdata
	output     [W-1:0] rdata,    // the oldest entry, valid while !rempty
	output             rempty
);

reg  [W-1:0] mem[0:(1<<A)-1];
reg    [A:0] wbin = 0, wgray = 0, rbin = 0, rgray = 0;
reg    [A:0] wg_r1 = 0, wg_r2 = 0;   // write pointer in the read clock
reg    [A:0] rg_w1 = 0, rg_w2 = 0;   // read pointer in the write clock

wire   [A:0] wbin_n  = wbin + 1'd1;
wire   [A:0] rbin_n  = rbin + 1'd1;

always @(posedge wclk) begin
	if (we && !wfull) begin
		mem[wbin[A-1:0]] <= wdata;
		wbin  <= wbin_n;
		wgray <= wbin_n ^ (wbin_n >> 1);
	end
	{rg_w2, rg_w1} <= {rg_w1, rgray};
	if (wclr) begin
		wbin  <= 0;
		wgray <= 0;
		rg_w1 <= 0;
		rg_w2 <= 0;
	end
end
assign wfull = wgray == {~rg_w2[A:A-1], rg_w2[A-2:0]};

always @(posedge rclk) begin
	if (re && !rempty) begin
		rbin  <= rbin_n;
		rgray <= rbin_n ^ (rbin_n >> 1);
	end
	{wg_r2, wg_r1} <= {wg_r1, wgray};
	if (rclr) begin
		rbin  <= 0;
		rgray <= 0;
		wg_r1 <= 0;
		wg_r2 <= 0;
	end
end
assign rempty = rgray == wg_r2;
// read without a clock: a small memory in logic; the entry is written
// before the pointer that shows it crosses
assign rdata = mem[rbin[A-1:0]];

endmodule

// A FIFO in one clock, in block RAM, the oldest entry shown a clock after it
// is there (show-ahead with a registered output)
module hybrid_midi_fifo
#(
	parameter W = 64,
	parameter A = 8
)
(
	input              clk,
	input              clr,
	input              we,
	input      [W-1:0] wdata,
	output             full,
	input              re,       // takes rdata
	output reg [W-1:0] rdata,
	output reg         rvalid,   // rdata is the oldest entry
	output       [A:0] level     // entries in it, rdata's among them
);

reg  [W-1:0] mem[0:(1<<A)-1];
reg    [A:0] wp = 0, rp = 0;     // rp: the next to come out of the memory
reg  [W-1:0] q;
reg          q_ok = 0;           // q is mem[rp - 1], read last clock

wire   [A:0] stored = wp - rp;   // in the memory, not yet read out
assign level = stored + {{A{1'b0}}, q_ok} + {{A{1'b0}}, rvalid};
assign full  = level >= (1 << A) - 1;

wire take = re && rvalid;
// the output takes q, or the memory's next entry is read into q
wire fill_out = !rvalid || take;

always @(posedge clk) begin
	if (we) begin
		mem[wp[A-1:0]] <= wdata;
		wp <= wp + 1'd1;
	end
	if (take) rvalid <= 0;
	if (fill_out && q_ok) begin
		rdata  <= q;
		rvalid <= 1;
	end
	// a read into q when q is free or moves on now
	if (stored != 0 && (!q_ok || fill_out)) begin
		q    <= mem[rp[A-1:0]];
		q_ok <= 1;
		rp   <= rp + 1'd1;
	end else if (fill_out) begin
		q_ok <= 0;
	end
	if (clr) begin
		wp     <= 0;
		rp     <= 0;
		q_ok   <= 0;
		rvalid <= 0;
	end
end

endmodule

// A table, read a clock after the address (block RAM)
module hybrid_midi_rom
#(
	parameter W = 32,
	parameter D = 1024,
	parameter A = 10,
	parameter FILE = ""
)
(
	input              clk,
	input      [A-1:0] addr,
	output reg [W-1:0] q
);

reg [W-1:0] rom[0:D-1];
initial $readmemh(FILE, rom);
always @(posedge clk) q <= rom[addr];

endmodule
