//============================================================================
//
//  MiSTer hybrid core host - MIDI synthesizer, the fetcher
//
//  Keeps each voice's cache of samples filled ahead of it (docs/midi_synth.md,
//  "The RTL"). A cache slot holds 64 samples by stream position: the samples
//  in the order the voice plays them (its loop again and again while it
//  loops), position 0 the one before its first. 192 slots: one for each
//  voice and 64 for notes waiting for their frame, whose first samples are
//  read before it.
//
//  Events, one at a time, six clocks each, change a slot's state and may
//  make a request (one read of DDR3 at most in flight for a slot):
//    init   a START has taken the slot: its sample, loop and first request
//    free   the slot's voice has a new one (taken back once nothing is in flight)
//    upd    the voice's position after a frame (from the sample engine), and
//           once, when a looping voice is released, where its loop ends
//    comp   a request has landed (from the host's clock)
//  A request is up to 32 samples, made when 16 or more are free, up to the
//  loop's end or the sample's; a loop of RES_L samples or less is read once
//  and the voice plays it from the cache (the sample engine takes its
//  position back with it). A voice that loops until released has the 16
//  samples after its loop's end read into the slot's tail: it plays them
//  while the fetcher goes on from there.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//============================================================================

module hybrid_midi_fetch
#(
	parameter RES_L = 52
)
(
	input             clk,
	input             clr,
	output            ready,       // the slots are cleared

	// a slot for a START
	output            alloc_valid,
	output      [7:0] alloc_id,
	input             alloc_take,

	input             init,        // the START's sample
	input       [7:0] init_slot,
	input      [24:0] init_start,
	input      [24:0] init_end,
	input      [24:0] init_ls,
	input      [24:0] init_le,
	input       [1:0] init_mode,
	input             init_looped,
	output            init_ready,

	input             free,
	input       [7:0] free_slot,
	output            free_ready,

	input             upd,         // from the sample engine: a FIFO here
	input      [25:0] upd_data,    // {slot, rk, remap, k_end, far}

	input             comp,        // from the host's clock: {slot, gen, tail, end}
	input      [18:0] comp_data,
	output            comp_take,

	output            req_valid,   // {slot, word, words, a0, k0, count, tail, gen}
	output     [53:0] req_data,
	input             req_take,

	// the sample engine: where a slot's samples have landed, and its tail
	input       [7:0] lnd_addr,
	output reg  [8:0] lnd_q        // {tail there, landed}
);

// ---- the slots' state
localparam FSW = 144;
reg  [FSW-1:0] fs[0:191];
reg    [7:0] fs_raddr;
reg  [FSW-1:0] fs_q;
reg          fs_we = 0;
reg    [7:0] fs_waddr;
reg  [FSW-1:0] fs_wdata;

always @(posedge clk) begin
	if (fs_we) fs[fs_waddr] <= fs_wdata;
	fs_q <= fs[fs_raddr];
end

reg    [8:0] lnd[0:191];
reg          lnd_we = 0;
reg    [7:0] lnd_waddr;
reg    [8:0] lnd_wdata;
always @(posedge clk) begin
	if (lnd_we) lnd[lnd_waddr] <= lnd_wdata;
	lnd_q <= lnd[lnd_addr];
end

typedef struct packed {
	logic        act;      // allocated
	logic        infl;     // a request in flight
	logic        fpend;    // freed while one was: taken back when it lands
	logic  [1:0] gen;      // requests made before a remap are stale
	logic  [7:0] fk;       // the next stream position to read
	logic [24:0] faddr;    // its sample
	logic  [7:0] rkc;      // the voice's position (it needs rkc - 1 on)
	logic  [7:0] landed;   // positions before this are in the cache
	logic        loop;     // the stream loops
	logic        res;      // the loop is short: read once
	logic        resdone;  // and has been
	logic        lapped;   // the stream has come to the loop's start once
	logic  [7:0] stopk;    // the short loop's last position + 1
	logic        fin;      // the sample's end has been read
	logic        tneed;    // the tail is to be read
	logic        tok;      // the tail is there
	logic [24:0] ls;
	logic [24:0] le;
	logic [24:0] last;     // the sample's end - 1
} fs_t;

// ---- the free slots: 0..191 fresh, then those taken back
reg  [7:0] fresh = 0;
wire [7:0] fl_q;
wire       fl_valid;
reg        fl_we = 0;
reg  [7:0] fl_wdata;
wire [8:0] fl_level;

hybrid_midi_fifo #(.W(8), .A(8)) freelist
(
	.clk(clk), .clr(clr),
	.we(fl_we), .wdata(fl_wdata), .full(),
	.re(alloc_take && fresh == 8'd192), .rdata(fl_q), .rvalid(fl_valid), .level(fl_level)
);

assign alloc_valid = ready && (fresh != 8'd192 || fl_valid);
assign alloc_id    = fresh != 8'd192 ? fresh : fl_q;

// ---- the sample engine's updates wait here
wire [25:0] uq;
wire        uq_valid;
reg         uq_take = 0;

hybrid_midi_fifo #(.W(26), .A(8)) updq
(
	.clk(clk), .clr(clr),
	.we(upd), .wdata(upd_data), .full(),
	.re(uq_take), .rdata(uq), .rvalid(uq_valid), .level()
);

// ---- the requests made
reg         rq_we = 0;
reg  [53:0] rq_wdata;

hybrid_midi_fifo #(.W(54), .A(8)) reqq
(
	.clk(clk), .clr(clr),
	.we(rq_we), .wdata(rq_wdata), .full(),
	.re(req_take), .rdata(req_data), .rvalid(req_valid), .level()
);

// ---- clearing: every slot not allocated
reg        sweep = 1;
reg  [7:0] sweep_i = 0;
assign ready = !sweep;

// ---- the events
localparam EV_COMP = 0, EV_FREE = 1, EV_INIT = 2, EV_UPD = 3;
reg  [2:0] ev_st = 0;            // 0 pick, 1 read, 2 the state, 3 the event, 4 and 5 its request
reg  [FSW-1:0] ev_s;             // the slot's state, then the new one
reg  [9:0] ev_a;                 // issue_a
reg  [1:0] ev;
reg  [7:0] ev_slot;
reg [18:0] ev_comp;
reg [25:0] ev_upd;
reg [24:0] ev_start, ev_end, ev_ls, ev_le;
reg  [1:0] ev_mode;
reg        ev_looped;

wire pick = ready && ev_st == 0 && !uq_take;
assign comp_take  = pick && comp;
assign free_ready = pick && !comp;
assign init_ready = pick && !comp && !free;

function automatic [7:0] min8(input [7:0] a, input [7:0] b);   // a, b < 256
	min8 = a < b ? a : b;
endfunction

// the request a slot makes now, in two clocks: which (main, tail) and how
// many samples, then the request and the new state
function automatic [9:0] issue_a(input fs_t s);   // {main, tail, count}
	reg [7:0] d, cnt;
	reg [25:0] run, stop;
	reg main, tail;
	begin
		// free positions: the voice needs rkc - 1 on, 64 of them
		d = s.rkc + 8'd63 - s.fk;
		if (d >= 8'd128) d = 0;
		run = (s.loop ? {1'b0, s.le} : {1'b0, s.last} + 1'd1) - {1'b0, s.faddr};
		stop = (s.res && s.loop && s.lapped) ? {18'd0, s.stopk - s.fk} : 26'd64;
		main = s.act && !s.infl && !s.resdone && !s.fin && d >= 8'd16;
		tail = s.act && !s.infl && !main && s.tneed;
		cnt = min8(d, 8'd32);
		if (run < {18'd0, cnt}) cnt = run[7:0];
		if (stop < {18'd0, cnt}) cnt = stop[7:0];
		if (tail) cnt = (s.last - s.le + 1'd1) > 25'd16 ? 8'd16 : 8'(s.last - s.le + 1'd1);
		issue_a = {main, tail, cnt};
	end
endfunction

function automatic [FSW+54:0] issue_b(input fs_t s, input [7:0] slot, input main, input tail, input [7:0] cnt);
	fs_t n;
	reg [24:0] fa;
	reg [53:0] rq;
	reg made;
	begin
		n = s;
		rq = 0;
		made = 0;
		if (main && cnt != 0) begin
			made = 1;
			rq = {slot, s.faddr[24:2], 4'((7'(s.faddr[1:0]) + 7'(cnt) + 7'd3) >> 2), s.faddr[1:0],
			      s.fk, cnt[5:0], 1'b0, s.gen};
			n.infl = 1;
			n.fk = s.fk + cnt;
			fa = s.faddr + 25'(cnt);
			if (s.loop && fa == s.le) begin
				fa = s.ls;
				if (s.res && !s.lapped) begin
					n.lapped = 1;
					n.stopk = n.fk + 8'(s.le - s.ls) + 8'd2;
				end
			end
			n.faddr = fa;
			if (s.res && s.loop && (s.lapped || n.lapped) && n.fk == n.stopk) n.resdone = 1;
			if (!s.loop && fa > s.last) n.fin = 1;
		end
		else if (tail) begin
			made = 1;
			rq = {slot, s.le[24:2], 4'((7'(s.le[1:0]) + 7'(cnt) + 7'd3) >> 2), s.le[1:0],
			      8'd0, cnt[5:0], 1'b1, s.gen};
			n.infl  = 1;
			n.tneed = 0;
		end
		issue_b = {n, rq, made};
	end
endfunction

always @(posedge clk) begin
	fs_we   <= 0;
	lnd_we  <= 0;
	fl_we   <= 0;
	rq_we   <= 0;
	uq_take <= 0;

	if (alloc_take && fresh != 8'd192) fresh <= fresh + 1'd1;

	case (ev_st)
		3'd0: if (ready) begin
			if (comp) begin
				ev       <= EV_COMP;
				ev_slot  <= comp_data[18:11];
				ev_comp  <= comp_data;
				fs_raddr <= comp_data[18:11];
				ev_st    <= 3'd1;
			end else if (free) begin
				ev       <= EV_FREE;
				ev_slot  <= free_slot;
				fs_raddr <= free_slot;
				ev_st    <= 3'd1;
			end else if (init) begin
				ev        <= EV_INIT;
				ev_slot   <= init_slot;
				ev_start  <= init_start;
				ev_end    <= init_end;
				ev_ls     <= init_ls;
				ev_le     <= init_le;
				ev_mode   <= init_mode;
				ev_looped <= init_looped;
				fs_raddr  <= init_slot;
				ev_st     <= 3'd1;
			end else if (uq_valid && !uq_take) begin
				ev       <= EV_UPD;
				ev_slot  <= uq[25:18];
				ev_upd   <= uq;
				fs_raddr <= uq[25:18];
				uq_take  <= 1;
				ev_st    <= 3'd1;
			end
		end
		3'd1: ev_st <= 3'd2;
		3'd2: begin
			ev_s  <= fs_q;
			ev_st <= 3'd3;
		end
		3'd3: begin : work
			fs_t s, n;
			reg [7:0] e, kend;
			reg loopm;
			s = fs_t'(ev_s);
			n = s;
			case (ev)
				EV_COMP: begin
					n.infl = 0;
					if (s.fpend) begin
						n.fpend  = 0;
						fl_we    <= 1;
						fl_wdata <= ev_slot;
					end else if (ev_comp[8]) begin
						n.tok = 1;
					end else begin
						e = ev_comp[7:0];
						if (ev_comp[10:9] != s.gen) begin
							// stale, made before a remap: what is before the
							// loop's end is good (k_end = fk - 16)
							kend = s.fk - 8'd16;
							if ($signed(e - kend) > 0) e = kend;
							if ($signed(e - s.landed) < 0) e = s.landed;
						end
						n.landed = e;
					end
				end
				EV_FREE: begin
					n.act = 0;
					if (s.infl) n.fpend = 1;
					else begin
						fl_we    <= 1;
						fl_wdata <= ev_slot;
					end
				end
				EV_INIT: begin
					loopm     = (ev_mode == 2'd1 || ev_mode == 2'd3) && ev_le > ev_ls;
					n         = '0;
					n.act     = 1;
					n.gen     = s.gen + 1'd1;
					n.rkc     = 8'd1;
					n.loop    = loopm;
					n.res     = loopm && (ev_le - ev_ls) <= 25'(RES_L);
					n.tneed   = loopm && ev_mode == 2'd3 && ev_end > ev_le;
					n.ls      = ev_ls;
					n.le      = ev_le;
					n.last    = ev_end - 1'd1;
					if (loopm && ev_looped && ev_start != ev_ls) begin
						// position 0 is the one before the start
						n.fk    = 0;
						n.faddr = ev_start - 1'd1;
					end else begin
						// position 0 on its own: the loop's end before a start at
						// its start, the start itself before a start outside it
						n.fk    = 8'd1;
						n.faddr = ev_start;
						n.infl  = 1;
						if (loopm && ev_looped) begin
							n.lapped = 1;
							n.stopk  = 8'd3 + 8'(ev_le - ev_ls);
						end
						rq_we    <= 1;
						rq_wdata <= loopm && ev_looped
						          ? {ev_slot, 23'(ev_le - 1'd1 >> 2), 4'd1, 2'(ev_le - 1'd1), 8'd0, 6'd1, 1'b0, n.gen}
						          : {ev_slot, ev_start[24:2], 4'd1, ev_start[1:0], 8'd0, 6'd1, 1'b0, n.gen};
					end
				end
				EV_UPD: if (s.act) begin
					n.rkc = ev_upd[17:10];
					if (ev_upd[9]) begin
						kend = ev_upd[8:1];
						if (ev_upd[0] || $signed(kend - s.fk) >= 0) begin
							// not read past the loop's end yet: on from there
							n.loop = 0;
						end else begin
							n.gen     = s.gen + 1'd1;
							n.loop    = 0;
							n.resdone = 0;
							n.fk      = kend + 8'd16;
							n.faddr   = s.le + 25'd16;
							n.fin     = s.le + 25'd16 > s.last;
							if ($signed(s.landed - kend) > 0) n.landed = kend;
						end
					end
				end
				default: ;
			endcase
			ev_s  <= n;
			ev_st <= 3'd4;
		end
		3'd4: begin
			ev_a  <= issue_a(fs_t'(ev_s));
			ev_st <= 3'd5;
		end
		3'd5: begin : request
			fs_t n;
			reg [FSW+54:0] r;
			// (an INIT that made its own request has one in flight)
			r = issue_b(fs_t'(ev_s), ev_slot, ev_a[9], ev_a[8], ev_a[7:0]);
			n = fs_t'(r[FSW+54:55]);
			if (r[0]) begin
				rq_we    <= 1;
				rq_wdata <= r[54:1];
			end
			fs_we    <= 1;
			fs_waddr <= ev_slot;
			fs_wdata <= n;
			lnd_we    <= 1;
			lnd_waddr <= ev_slot;
			lnd_wdata <= {n.tok, n.landed};
			ev_st <= 3'd0;
		end
		default: ev_st <= 3'd0;
	endcase

	if (sweep) begin
		fs_we     <= 1;
		fs_waddr  <= sweep_i;
		fs_wdata  <= '0;
		lnd_we    <= 1;
		lnd_waddr <= sweep_i;
		lnd_wdata <= 0;
		sweep_i   <= sweep_i + 1'd1;
		if (sweep_i == 8'd191) sweep <= 0;
	end

	if (clr) begin
		fresh   <= 0;
		ev_st   <= 0;
		sweep   <= 1;
		sweep_i <= 0;
	end
end

endmodule
