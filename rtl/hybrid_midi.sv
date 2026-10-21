//============================================================================
//
//  MiSTer hybrid core host - MIDI synthesizer
//
//  The voice engine of docs/midi_synth.md ("Design", "The RTL"): 128 voices
//  of SoundFont samples, sample-exact with the C model
//  (tools/midisynth/midisynth.c). It runs on a clock of its own (mclk,
//  100MHz); hybrid_host.sv (clk, 50MHz) reads the commands and the samples
//  for it from DDR3, when it asks, and mixes what it makes.
//
//  On the host's clock: the command reader (a command of the ring into a
//  free parameter slot, then to the engine) and the sample writer (a read
//  the fetcher asked for into the cache). On the engine's: at every audio
//  frame, the commands due (C), the control updates (K), every voice (S),
//  the mix; between frames the fetcher (hybrid_midi_fetch.sv).
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//============================================================================

module hybrid_midi
#(
	parameter TABLES = "../hybrid/rtl/midi/"
)
(
	input              clk,
	input              mclk,

	// the host's clock
	input              en,          // off: every voice silent, every queue empty
	input       [31:0] wcount,      // commands written to the ring
	input        [6:0] volume,
	input              frame_stb,   // the audio frame counter has counted
	input       [31:0] frame,       // to this

	output             cmd_req,     // a command to read: 16 words of the ring
	output      [13:0] cmd_addr,    // word in the ring
	input              cmd_ack,     // the read is made: its words follow
	output             smp_req,     // samples to read
	output      [22:0] smp_addr,    // word in the sample memory
	output       [3:0] smp_len,
	input              smp_ack,
	input              beat,        // a word of the read
	input       [63:0] dout,

	output reg  [15:0] out_l = 0,   // frame `frame`, from the strobe that makes it `frame`
	output reg  [15:0] out_r = 0,
	output reg  [31:0] st_read = 0, // commands read
	output reg  [15:0] st_under = 0,
	output reg   [7:0] st_voices = 0,
	output reg [127:0] st_bits = 0,
	output reg  [31:0] st_frame = 0,

	// for the testbench: the four sums of the frame last made, and which
	output reg [159:0] dbg_sums = 0,
	output reg  [31:0] dbg_out = 0,
	output reg  [31:0] dbg_frame = 0,
	output reg         dbg_tgl = 0
);

localparam [2:0] C_START = 1, C_UPDATE = 2, C_RELEASE = 3, C_KILL = 4, C_STOP = 5;
localparam [2:0] SEC_RELEASE = 5;
localparam [28:0] AMP_MAX = 29'h1FFFFFFF;

//////////////////////////////////////////////////////////////////
// Clock crossing

reg  [2:0] en_m = 0;
wire       mclr = !en_m[2];
always @(posedge mclk) en_m <= {en_m[1:0], en};

reg        ftgl = 0;
reg [31:0] fnum = 0;
reg  [6:0] fvol = 0;
always @(posedge clk) begin
	if (frame_stb) begin
		ftgl <= ~ftgl;
		fnum <= frame;
		fvol <= volume;
	end
end
reg  [2:0] ftgl_m = 0;
always @(posedge mclk) ftgl_m <= {ftgl_m[1:0], ftgl};
wire       fpulse = ftgl_m[2] != ftgl_m[1];

// what the engine publishes after a frame, and its sound
reg         stgl = 0;
reg  [15:0] m_under = 0;
reg   [7:0] m_voices = 0;
reg [127:0] m_bits = 0;
reg  [31:0] m_frame = 0;
reg  [15:0] m_out_l = 0, m_out_r = 0;
reg   [2:0] stgl_c = 0;
always @(posedge clk) begin
	stgl_c <= {stgl_c[1:0], stgl};
	if (stgl_c[2] != stgl_c[1]) begin
		st_under  <= m_under;
		st_voices <= m_voices;
		st_bits   <= m_bits;
		st_frame  <= m_frame;
	end
	if (!en) begin
		st_voices <= 0;
		st_bits   <= 0;
	end
	// the engine changes its sound a few clocks after the strobe: it is
	// taken before the next one
	if (frame_stb) begin
		out_l <= en ? m_out_l : 16'd0;
		out_r <= en ? m_out_r : 16'd0;
	end
end

//////////////////////////////////////////////////////////////////
// Memories between the clocks

// the parameter slots: 192 commands of 16 words (host writes, engine reads)
reg  [63:0] pram[0:3071];
reg  [11:0] pram_waddr;
reg         pram_we = 0;
reg  [63:0] pram_wdata;
reg  [11:0] pram_raddr = 0;
reg  [63:0] pram_q;
always @(posedge clk) if (pram_we) pram[pram_waddr] <= pram_wdata;
always @(posedge mclk) pram_q <= pram[pram_raddr];

// the cache: 192 slots of 64 samples, in 4 banks by stream position, and
// their tails of 16
reg  [15:0] cbank0[0:3071], cbank1[0:3071], cbank2[0:3071], cbank3[0:3071];
reg  [15:0] tbank0[0:767], tbank1[0:767], tbank2[0:767], tbank3[0:767];
reg   [3:0] cw_we = 0, tw_we = 0;
reg  [11:0] cw_addr[0:3];
reg   [9:0] tw_addr[0:3];
reg  [15:0] cw_data[0:3];
reg  [11:0] cr_addr[0:3];
reg   [9:0] tr_addr[0:3];
reg  [15:0] cr_q[0:3], tr_q[0:3];

always @(posedge clk) begin
	if (cw_we[0]) cbank0[cw_addr[0]] <= cw_data[0];
	if (cw_we[1]) cbank1[cw_addr[1]] <= cw_data[1];
	if (cw_we[2]) cbank2[cw_addr[2]] <= cw_data[2];
	if (cw_we[3]) cbank3[cw_addr[3]] <= cw_data[3];
	if (tw_we[0]) tbank0[tw_addr[0]] <= cw_data[0];
	if (tw_we[1]) tbank1[tw_addr[1]] <= cw_data[1];
	if (tw_we[2]) tbank2[tw_addr[2]] <= cw_data[2];
	if (tw_we[3]) tbank3[tw_addr[3]] <= cw_data[3];
end
always @(posedge mclk) begin
	cr_q[0] <= cbank0[cr_addr[0]];
	cr_q[1] <= cbank1[cr_addr[1]];
	cr_q[2] <= cbank2[cr_addr[2]];
	cr_q[3] <= cbank3[cr_addr[3]];
	tr_q[0] <= tbank0[tr_addr[0]];
	tr_q[1] <= tbank1[tr_addr[1]];
	tr_q[2] <= tbank2[tr_addr[2]];
	tr_q[3] <= tbank3[tr_addr[3]];
end

// free parameter slots, engine to host
wire        ps_empty, ps_full;
wire  [7:0] ps_id;
reg         ps_take = 0;
reg         ps_we = 0;
reg   [7:0] ps_wdata;
hybrid_midi_afifo #(.W(8), .A(4)) ps_fifo
(
	.wclk(mclk), .wclr(mclr), .we(ps_we), .wdata(ps_wdata), .wfull(ps_full),
	.rclk(clk), .rclr(!en), .re(ps_take), .rdata(ps_id), .rempty(ps_empty)
);

// commands read, host to engine: {slot, frame, voice, command, w1[62:0], w2[49:0]}
localparam IW = 8 + 32 + 7 + 3 + 63 + 50;
wire          ie_empty, ie_full;
wire [IW-1:0] ie_q;
reg           ie_take = 0;
reg           ie_we = 0;
reg  [IW-1:0] ie_wdata;
hybrid_midi_afifo #(.W(IW), .A(3)) ie_fifo
(
	.wclk(clk), .wclr(!en), .we(ie_we), .wdata(ie_wdata), .wfull(ie_full),
	.rclk(mclk), .rclr(mclr), .re(ie_take), .rdata(ie_q), .rempty(ie_empty)
);

// the fetcher's reads, engine to host: {slot, word, words, a0, k0, count, tail, gen}
wire        rq_empty, rq_full;
wire [53:0] rq_q;
reg         rq_take = 0;
wire        f_req_valid;
wire [53:0] f_req_data;
hybrid_midi_afifo #(.W(54), .A(4)) rq_fifo
(
	.wclk(mclk), .wclr(mclr), .we(f_req_valid && !rq_full), .wdata(f_req_data), .wfull(rq_full),
	.rclk(clk), .rclr(!en), .re(rq_take), .rdata(rq_q), .rempty(rq_empty)
);

// reads made, host to engine: {slot, gen, tail, end}
wire        cp_empty, cp_full;
wire [18:0] cp_q;
reg         cp_we = 0;
reg  [18:0] cp_wdata;
wire        f_comp_take;
hybrid_midi_afifo #(.W(19), .A(4)) cp_fifo
(
	.wclk(clk), .wclr(!en), .we(cp_we), .wdata(cp_wdata), .wfull(cp_full),
	.rclk(mclk), .rclr(mclr), .re(f_comp_take), .rdata(cp_q), .rempty(cp_empty)
);

//////////////////////////////////////////////////////////////////
// The host's clock: the command reader and the sample writer

reg   [1:0] cr_st = 0;          // 0 waits, 2 the words come
reg   [3:0] cr_beat;
reg   [7:0] cr_slot;
reg  [63:0] cr_w0, cr_w1;
reg  [49:0] cr_w2;
reg  [31:0] cmd_rd = 0;

assign cmd_req  = en && cr_st == 0 && sw_st == 0 && cmd_rd != wcount && !ps_empty && !ie_full;
assign cmd_addr = {cmd_rd[9:0], 4'd0};

reg   [1:0] sw_st = 0;          // 0 waits, 2 the words come, 3 the completion waits
reg  [53:0] sw_rq;
reg   [3:0] sw_beat;

assign smp_req  = en && sw_st == 0 && cr_st == 0 && !rq_empty;
assign smp_addr = rq_q[45:23];
assign smp_len  = rq_q[22:19];

// a request's fields
wire  [7:0] sw_slot  = sw_rq[53:46];
wire  [3:0] sw_words = sw_rq[22:19];
wire  [1:0] sw_a0    = sw_rq[18:17];
wire  [7:0] sw_k0    = sw_rq[16:9];
wire  [5:0] sw_cnt   = sw_rq[8:3];
wire        sw_tail  = sw_rq[2];
wire  [1:0] sw_gen   = sw_rq[1:0];

always @(posedge clk) begin : host_side
	integer q;
	reg [1:0] b;
	reg signed [7:0] s;
	reg [7:0] p;

	pram_we <= 0;
	ps_take <= 0;
	ie_we   <= 0;
	rq_take <= 0;
	cp_we   <= 0;
	cw_we   <= 0;
	tw_we   <= 0;

	// the command reader
	case (cr_st)
		2'd0: if (cmd_req && cmd_ack) begin
			cr_st   <= 2'd2;
			cr_beat <= 0;
			cr_slot <= ps_id;
			ps_take <= 1;
		end
		2'd2: if (beat) begin
			pram_we    <= 1;
			pram_waddr <= {cr_slot, cr_beat};
			pram_wdata <= dout;
			cr_beat    <= cr_beat + 1'd1;
			if (cr_beat == 4'd0) cr_w0 <= dout;
			if (cr_beat == 4'd1) cr_w1 <= dout;
			if (cr_beat == 4'd2) cr_w2 <= dout[49:0];
			// to the engine once all of it is in its slot
			if (cr_beat == 4'd15) begin
				ie_we    <= 1;
				ie_wdata <= {cr_slot, cr_w0[31:0], cr_w0[38:32], cr_w0[42:40], cr_w1[62:0], cr_w2};
				cr_st  <= 2'd0;
				cmd_rd <= cmd_rd + 1'd1;
			end
		end
		default: cr_st <= 2'd0;
	endcase

	// the sample writer: each word's four samples go to the four banks
	case (sw_st)
		2'd0: if (smp_req && smp_ack) begin
			sw_st   <= 2'd2;
			sw_rq   <= rq_q;
			sw_beat <= 0;
			rq_take <= 1;
		end
		2'd2: if (beat) begin
			for (q = 0; q < 4; q = q + 1) begin
				// the sample of the word that goes to bank q
				b = 2'(q) - (sw_tail ? 2'd0 : sw_k0[1:0]) + sw_a0;
				s = $signed({2'b0, sw_beat, b}) - $signed({6'd0, sw_a0});
				p = (sw_tail ? 8'd0 : sw_k0) + 8'(s);
				cw_data[q] <= dout[16 * b +: 16];
				if (s >= 0 && s < $signed({2'b0, sw_cnt})) begin
					if (sw_tail) begin
						tw_we[q]   <= 1;
						tw_addr[q] <= {sw_slot, p[3:2]};
					end else begin
						cw_we[q]   <= 1;
						cw_addr[q] <= {sw_slot, p[5:2]};
					end
				end
			end
			sw_beat <= sw_beat + 1'd1;
			if (sw_beat == sw_words - 1'd1) sw_st <= 2'd3;
		end
		2'd3: if (!cp_full) begin
			cp_we    <= 1;
			cp_wdata <= {sw_slot, sw_gen, sw_tail, sw_k0 + {2'd0, sw_cnt}};
			sw_st    <= 2'd0;
		end
		default: sw_st <= 2'd0;
	endcase

	if (!en) begin
		cr_st  <= 0;
		sw_st  <= 0;
		cmd_rd <= 0;
	end
	st_read <= cmd_rd;
end

//////////////////////////////////////////////////////////////////
// The engine: free parameter slots

reg  [7:0] pfresh = 0;
wire [7:0] pfl_q;
wire       pfl_valid;
reg        pfl_we = 0;
reg  [7:0] pfl_wdata;
// taken as it goes to the host: the write below, a clock without one between
wire       pfl_take = !mclr && !ps_full && !ps_we && pfresh == 8'd192 && pfl_valid;

hybrid_midi_fifo #(.W(8), .A(8)) pfl
(
	.clk(mclk), .clr(mclr),
	.we(pfl_we), .wdata(pfl_wdata), .full(),
	.re(pfl_take), .rdata(pfl_q), .rvalid(pfl_valid), .level()
);

always @(posedge mclk) begin
	ps_we <= 0;
	if (!mclr && !ps_full && !ps_we) begin
		if (pfresh != 8'd192) begin
			ps_we    <= 1;
			ps_wdata <= pfresh;
			pfresh   <= pfresh + 1'd1;
		end else if (pfl_valid) begin
			ps_we    <= 1;
			ps_wdata <= pfl_q;
		end
	end
	if (mclr) pfresh <= 0;
end

//////////////////////////////////////////////////////////////////
// The engine: intake, the fetcher

// commands waiting for their frame: {pslot, cslot, frame, voice, command}
wire [57:0] pq;
wire        pq_valid;
reg         pq_take = 0;
reg         pq_we = 0;
reg  [57:0] pq_wdata;
hybrid_midi_fifo #(.W(58), .A(8)) pending
(
	.clk(mclk), .clr(mclr),
	.we(pq_we), .wdata(pq_wdata), .full(),
	.re(pq_take), .rdata(pq), .rvalid(pq_valid), .level()
);

// the fields of an intake entry
wire  [7:0] ie_pslot = ie_q[IW-1 -: 8];
wire [31:0] ie_frame = ie_q[IW-9 -: 32];
wire  [6:0] ie_voice = ie_q[IW-41 -: 7];
wire  [2:0] ie_cmd   = ie_q[IW-48 -: 3];
wire [62:0] ie_w1    = ie_q[112:50];
wire [49:0] ie_w2    = ie_q[49:0];

wire        f_ready, f_alloc_valid, f_init_ready, f_free_ready;
wire  [7:0] f_alloc_id;
reg         f_init = 0;

// slots given back by phase C, for the fetcher
wire  [7:0] fq_slot;
wire        fq_valid;
reg         fq_we = 0;
reg   [7:0] fq_wdata;
hybrid_midi_fifo #(.W(8), .A(8)) freeq
(
	.clk(mclk), .clr(mclr),
	.we(fq_we), .wdata(fq_wdata), .full(),
	.re(fq_valid && f_free_ready), .rdata(fq_slot), .rvalid(fq_valid), .level()
);

wire        s_upd;
wire [25:0] s_upd_data;
reg   [7:0] lnd_addr = 0;
wire  [8:0] lnd_q;

hybrid_midi_fetch fetch
(
	.clk(mclk), .clr(mclr), .ready(f_ready),
	.alloc_valid(f_alloc_valid), .alloc_id(f_alloc_id), .alloc_take(f_init && f_init_ready),
	.init(f_init), .init_slot(f_alloc_id),
	.init_start(ie_w1[24:0]), .init_end(ie_w1[49:25]), .init_ls(ie_w2[24:0]), .init_le(ie_w2[49:25]),
	.init_mode(ie_w1[51:50]), .init_looped(ie_w1[62]), .init_ready(f_init_ready),
	.free(fq_valid), .free_slot(fq_slot), .free_ready(f_free_ready),
	.upd(s_upd), .upd_data(s_upd_data),
	.comp(!cp_empty), .comp_data(cp_q), .comp_take(f_comp_take),
	.req_valid(f_req_valid), .req_data(f_req_data), .req_take(f_req_valid && !rq_full),
	.lnd_addr(lnd_addr), .lnd_q(lnd_q)
);

// a command in: a START takes a cache slot and has its first samples read
always @(posedge mclk) begin
	ie_take <= 0;
	pq_we   <= 0;
	if (f_init && f_init_ready) f_init <= 0;
	if (!mclr && !ie_empty && !ie_take) begin
		if (ie_cmd == C_START) begin
			if (f_init && f_init_ready) begin
				ie_take  <= 1;
				pq_we    <= 1;
				pq_wdata <= {ie_pslot, f_alloc_id, ie_frame, ie_voice, ie_cmd};
			end else if (!f_init && f_alloc_valid) f_init <= 1;
		end else begin
			ie_take  <= 1;
			pq_we    <= 1;
			pq_wdata <= {ie_pslot, 8'd0, ie_frame, ie_voice, ie_cmd};
		end
	end
	if (mclr) f_init <= 0;
end

//////////////////////////////////////////////////////////////////
// The engine: the voices' memories (read together at vaddr)

// VS, the sample engine's: pos 58, rk 8, looped, ran 7, k_end 8, remapped,
// tailuse, incr 40, cslot 8
typedef struct packed {
	logic [57:0] pos;
	logic  [7:0] rk;
	logic        looped;
	logic  [6:0] ran;
	logic  [7:0] k_end;
	logic        remapped;
	logic        tailuse;
	logic [39:0] incr;
	logic  [7:0] cslot;
} vs_t;
// VP, the sample: start, end, loop start, loop end, mode, looped at the start
typedef struct packed {
	logic [24:0] start;
	logic [24:0] stop;        // the sample's end (the first after it)
	logic [24:0] ls;
	logic [24:0] le;
	logic  [1:0] mode;
	logic        plooped;
} vp_t;
// VF, the filter
typedef struct packed {
	logic [55:0] h1;
	logic [55:0] h2;
	logic        silent;
	logic [31:0] fres;
	logic [31:0] fres_incr;
	logic  [8:0] fres_n;
	logic  [9:0] q;
} vf_t;
// VG, gain and sends
typedef struct packed {
	logic [28:0] amp;
	logic [29:0] amp_step;
	logic [95:0] send;
	logic [95:0] send_step;
} vg_t;
// VK, the control engine's
typedef struct packed {
	logic  [2:0] sec0, sec1;
	logic [15:0] cnt0, cnt1;
	logic [39:0] x0, x1;
	logic [32:0] lfo0, lfo1;
	logic        down0, down1;
	logic [15:0] blocks;
	logic        killed;
} vk_t;

localparam VSW = $bits(vs_t), VPW = $bits(vp_t), VFW = $bits(vf_t), VGW = $bits(vg_t), VKW = $bits(vk_t);

reg  [VSW-1:0] m_vs[0:127];
reg  [VPW-1:0] m_vp[0:127];
reg  [VFW-1:0] m_vf[0:127];
reg    [107:0] m_vc[0:127];
reg  [VGW-1:0] m_vg[0:127];
reg     [95:0] m_vo[0:127];
reg  [VKW-1:0] m_vk[0:127];
reg      [7:0] m_vps[0:127];

reg      [6:0] vaddr = 0;
vs_t           vs_q;
vp_t           vp_q;
vf_t           vf_q;
reg    [107:0] vc_q;
vg_t           vg_q;
reg     [95:0] vo_q;
vk_t           vk_q;
reg      [7:0] vps_q;

reg            vs_we = 0, vp_we = 0, vf_we = 0, vg_we = 0, vo_we = 0, vk_we = 0, vps_we = 0;
reg      [6:0] vs_wa, vp_wa, vf_wa, vg_wa, vo_wa, vk_wa, vps_wa;
vs_t           vs_wd;
vp_t           vp_wd;
vf_t           vf_wd;
vg_t           vg_wd;
reg     [95:0] vo_wd;
vk_t           vk_wd;
reg      [7:0] vps_wd;

wire           vc_we;
wire     [6:0] vc_wa;
wire    [26:0] vc_b02, vc_b1, vc_a1, vc_a2;

always @(posedge mclk) begin
	if (vs_we)  m_vs[vs_wa]   <= vs_wd;
	if (vp_we)  m_vp[vp_wa]   <= vp_wd;
	if (vf_we)  m_vf[vf_wa]   <= vf_wd;
	if (vc_we)  m_vc[vc_wa]   <= {vc_b02, vc_b1, vc_a1, vc_a2};
	if (vg_we)  m_vg[vg_wa]   <= vg_wd;
	if (vo_we)  m_vo[vo_wa]   <= vo_wd;
	if (vk_we)  m_vk[vk_wa]   <= vk_wd;
	if (vps_we) m_vps[vps_wa] <= vps_wd;
	vs_q  <= m_vs[vaddr];
	vp_q  <= m_vp[vaddr];
	vf_q  <= m_vf[vaddr];
	vc_q  <= m_vc[vaddr];
	vg_q  <= m_vg[vaddr];
	vo_q  <= m_vo[vaddr];
	vk_q  <= m_vk[vaddr];
	vps_q <= m_vps[vaddr];
end

// flags of each voice
reg [127:0] v_on = 0;       // playing
reg [127:0] v_due = 0;      // started in this frame: a control update in it
reg [127:0] v_first = 0;    // no control update yet
reg [127:0] v_rel = 0;      // its volume envelope is in its release: no loop until release
reg [127:0] v_rls1 = 0;     // a RELEASE waits for a control update
reg [127:0] v_rls2 = 0;     // a KILL waits
reg [127:0] v_hasp = 0;     // holds a parameter slot
reg [127:0] v_hasc = 0;     // holds a cache slot

//////////////////////////////////////////////////////////////////
// The engine: control and filter coefficients

reg  [63:0] w[1:15];
reg         k_go = 0;
wire        k_done;
wire        k_off, k_released, k_killed, k_down0, k_down1, k_silent;
wire  [2:0] k_sec0, k_sec1;
wire [15:0] k_cnt0, k_cnt1, k_blocks;
wire [39:0] k_x0, k_x1, k_incr;
wire [32:0] k_lfo0, k_lfo1;
wire [29:0] k_amp_step;
wire [95:0] k_send_step;
wire [31:0] k_fres, k_fres_incr;
wire  [8:0] k_fres_n;
wire  [9:0] k_q;
reg   [6:0] kv;

hybrid_midi_ctrl #(.TABLES(TABLES)) ctrl
(
	.clk(mclk), .go(k_go),
	.w1(w[1]), .w3(w[3]), .w4(w[4]), .w5(w[5]), .w6(w[6]), .w7(w[7]), .w8(w[8]), .w9(w[9]),
	.w10(w[10]), .w11(w[11]), .w12(w[12]), .w13(w[13]), .w14(w[14]), .w15(w[15]),
	.i_sec0(vk_q.sec0), .i_sec1(vk_q.sec1), .i_cnt0(vk_q.cnt0), .i_cnt1(vk_q.cnt1),
	.i_x0(vk_q.x0), .i_x1(vk_q.x1), .i_lfo0(vk_q.lfo0), .i_lfo1(vk_q.lfo1),
	.i_down0(vk_q.down0), .i_down1(vk_q.down1), .i_blocks(vk_q.blocks),
	.i_release(v_rls2[kv] ? 2'd2 : v_rls1[kv] ? 2'd1 : 2'd0), .i_killed(vk_q.killed),
	.i_first(v_first[kv]), .i_looped(vs_q.looped), .i_amp(vg_q.amp), .i_send(vg_q.send),
	.i_fres(vf_q.fres),
	.done(k_done), .o_off(k_off), .o_released(k_released), .o_sec0(k_sec0), .o_sec1(k_sec1),
	.o_cnt0(k_cnt0), .o_cnt1(k_cnt1), .o_x0(k_x0), .o_x1(k_x1), .o_lfo0(k_lfo0), .o_lfo1(k_lfo1),
	.o_down0(k_down0), .o_down1(k_down1), .o_blocks(k_blocks), .o_killed(k_killed),
	.o_incr(k_incr), .o_amp_step(k_amp_step), .o_send_step(k_send_step), .o_fres(k_fres),
	.o_fres_incr(k_fres_incr), .o_fres_n(k_fres_n), .o_silent(k_silent), .o_q(k_q)
);

reg         cj = 0;
reg   [6:0] cj_voice;
reg  [31:0] cj_fres;
reg   [9:0] cj_q;
wire        coef_idle;

hybrid_midi_coef #(.TABLES(TABLES)) coef
(
	.clk(mclk), .clr(mclr),
	.job(cj), .job_voice(cj_voice), .job_fres(cj_fres), .job_q(cj_q), .full(),
	.wr(vc_we), .wr_voice(vc_wa), .wr_b02(vc_b02), .wr_b1(vc_b1), .wr_a1(vc_a1), .wr_a2(vc_a2),
	.idle(coef_idle)
);

//////////////////////////////////////////////////////////////////
// The engine: a frame

localparam [2:0] P_IDLE = 0, P_C = 1, P_K = 2, P_KW = 3, P_S = 4, P_FIN = 5, P_SWEEP = 6;
reg   [2:0] ph = P_SWEEP;
reg   [6:0] sweep_i = 0;
reg         fpend = 0;            // a frame pulse came while busy
reg  [31:0] F;                    // the frame being made
reg   [6:0] vol;
reg  [15:0] under = 0;

// phase C
reg   [2:0] c_st = 0;
reg         c_due;
reg         c_on;           // the voice of a START on (v_on, a clock before it is used)
reg   [5:0] c_made;
reg   [3:0] c_starts;
reg  [57:0] c_e;
wire  [7:0] c_pslot  = c_e[57:50];
wire  [7:0] c_cslot  = c_e[49:42];
wire  [6:0] c_voice  = c_e[9:3];
wire  [2:0] c_cmd    = c_e[2:0];
reg  [63:0] c_w1;

// phase K
reg   [5:0] k_st = 0;
reg [127:0] k_todo;
reg   [4:0] k_i;

function automatic [7:0] ffs128(input [127:0] x);  // {found, lowest bit set}
	integer i;
	begin
		ffs128 = 0;
		for (i = 127; i >= 0; i = i - 1) if (x[i]) ffs128 = {1'b1, 7'(i)};
	end
endfunction

wire  [7:0] k_next = ffs128(k_todo);

// phase S (below)
reg [127:0] s_todo;
reg   [2:0] sc = 0;               // the clock in the voices' 8
reg   [3:0] sv = 0;               // a voice in each of the four stages
reg   [6:0] sn[0:3];
wire  [7:0] s_next = ffs128(s_todo);
reg  [47:0] sums[0:3];
reg   [2:0] fin_st;

// the sample engine's per-voice work and values carried from stage to stage
typedef struct packed {
	logic        ended;       // ended at the frame's start: no sound
	logic [57:0] pos;         // after this frame
	logic  [7:0] rk;
	logic        looped;
	logic  [6:0] ran;         // the frames since the control update, before this one
	logic  [7:0] k_end;
	logic        remapped;
	logic        tailuse;
	logic        upd;         // the fetcher hears of it
	logic        remap;       // ... with the loop's end, this frame
	logic        lfar;     // the loop's end too far ahead to matter
	logic [39:0] incr;
	logic  [7:0] cslot;
	logic [55:0] h1, h2;
	logic        silent;
	logic [31:0] fres;
	logic [31:0] fres_incr;
	logic  [8:0] fres_n;
	logic  [9:0] q;
	logic [28:0] amp;
	logic [29:0] amp_step;
	logic [95:0] send;
	logic [95:0] send_step;
	logic [26:0] b02;
	logic [23:0] x0;
	logic [55:0] c;           // the filter's new state
	logic [26:0] y;
	logic [29:0] out;
} sx_t;

sx_t sx[0:3];

always @(posedge mclk) begin : engine
	reg [2:0] cmd;
	vs_t vs;
	vg_t vg;

	vs_we  <= 0;
	vp_we  <= 0;
	vf_we  <= 0;
	vg_we  <= 0;
	vo_we  <= 0;
	vk_we  <= 0;
	vps_we <= 0;
	pq_take <= 0;
	pfl_we  <= 0;
	fq_we   <= 0;
	k_go    <= 0;
	cj      <= 0;

	if (fpulse) begin
		if (ph == P_IDLE) begin
			F     <= fnum + 1'd1;
			vol   <= fvol;
			ph    <= P_C;
			c_st  <= 0;
			c_made   <= 0;
			c_starts <= 0;
		end else fpend <= 1;
		// the frame made last is heard from now on
		m_out_l <= o_l;
		m_out_r <= o_r;
	end

	case (ph)
		P_SWEEP: begin
			// the sends from 0: a voice's sends go on from where its last note left them
			vg_we <= 1;
			vg_wa <= sweep_i;
			vg_wd <= '0;
			vo_we <= 1;
			vo_wa <= sweep_i;
			vo_wd <= 0;
			sweep_i <= sweep_i + 1'd1;
			if (sweep_i == 7'd127) ph <= P_IDLE;
		end

		P_IDLE: if (fpend) begin
			fpend <= 0;
			F     <= fnum + 1'd1;
			vol   <= fvol;
			ph    <= P_C;
			c_st  <= 0;
			c_made   <= 0;
			c_starts <= 0;
		end

		// ---- C: the commands of this frame and before, in order, 32 at most,
		// 8 STARTs of them
		P_C: case (c_st)
			// the oldest due? (decided a clock before it is taken)
			3'd0: if (!pq_take) begin
				c_due <= pq_valid && $signed(pq[41:10] - F) <= 0 && c_made != 6'd32
				         && !(pq[2:0] == C_START && c_starts == 4'd8);
				c_st  <= 3'd4;
			end
			3'd4: if (c_due) begin
				c_e        <= pq;
				pq_take    <= 1;
				vaddr      <= pq[9:3];
				pram_raddr <= {pq[57:50], 4'd1};
				c_made     <= c_made + 1'd1;
				c_st       <= 3'd1;
			end else begin
				ph   <= P_K;
				k_st <= 0;
			end
			3'd1: begin
				pram_raddr <= {c_pslot, 4'd2};
				c_st <= 3'd2;
			end
			3'd2: begin
				// the voice's memories and w1 are here
				c_w1 <= pram_q;
				c_on <= v_on[c_voice];
				cmd = c_cmd;
				c_st <= 3'd0;
				case (cmd)
					C_START: c_st <= 3'd3;
					C_UPDATE: begin
						if (v_on[c_voice]) begin
							if (v_hasp[c_voice]) begin
								pfl_we    <= 1;
								pfl_wdata <= vps_q;
							end
							vps_we <= 1;
							vps_wa <= c_voice;
							vps_wd <= c_pslot;
							v_hasp[c_voice] <= 1;
						end else begin
							pfl_we    <= 1;
							pfl_wdata <= c_pslot;
						end
					end
					default: begin
						pfl_we    <= 1;
						pfl_wdata <= c_pslot;
						if (cmd == C_RELEASE && v_on[c_voice] && !v_rls1[c_voice] && !v_rls2[c_voice])
							v_rls1[c_voice] <= 1;
						if (cmd == C_KILL && v_on[c_voice]) begin
							v_rls1[c_voice] <= 0;
							v_rls2[c_voice] <= 1;
						end
						if (cmd == C_STOP) v_on[c_voice] <= 0;
					end
				endcase
			end
			3'd3: begin
				// START, w2 is here: the voice starts afresh; its sends go on,
				// from its other set if it is cut
				c_starts <= c_starts + 1'd1;
				if (v_hasp[c_voice]) begin
					pfl_we    <= 1;
					pfl_wdata <= vps_q;
				end
				if (v_hasc[c_voice]) begin
					fq_we    <= 1;
					fq_wdata <= vs_q.cslot;
				end
				vs = '0;
				vs.pos    = {1'b0, c_w1[24:0], 32'd0};
				vs.rk     = 8'd1;
				vs.looped = c_w1[62];
				vs.cslot  = c_cslot;
				vs_we <= 1;
				vs_wa <= c_voice;
				vs_wd <= vs;
				vp_we <= 1;
				vp_wa <= c_voice;
				vp_wd <= '{start: c_w1[24:0], stop: c_w1[49:25], ls: pram_q[24:0], le: pram_q[49:25],
				          mode: c_w1[51:50], plooped: c_w1[62]};
				vf_we <= 1;
				vf_wa <= c_voice;
				vf_wd <= '0;
				vg = '0;
				vg.send = c_on ? vo_q : vg_q.send;
				vg_we <= 1;
				vg_wa <= c_voice;
				vg_wd <= vg;
				vo_we <= 1;
				vo_wa <= c_voice;
				vo_wd <= c_on ? vg_q.send : vo_q;
				vk_we <= 1;
				vk_wa <= c_voice;
				vk_wd <= '0;
				vps_we <= 1;
				vps_wa <= c_voice;
				vps_wd <= c_pslot;
				v_on[c_voice]    <= 1;
				v_due[c_voice]   <= 1;
				v_first[c_voice] <= 1;
				v_rel[c_voice]   <= 0;
				v_rls1[c_voice]  <= 0;
				v_rls2[c_voice]  <= 0;
				v_hasp[c_voice]  <= 1;
				v_hasc[c_voice]  <= 1;
				c_st <= 3'd0;
			end
			default: c_st <= 3'd0;
		endcase

		// ---- K: the control updates: the voices started now, and two by turns
		P_K: case (k_st)
			6'd0: begin
				k_todo <= v_on & (v_due | (128'd1 << F[5:0]) | (128'd1 << {1'b1, F[5:0]}));
				k_st   <= 6'd1;
			end
			6'd1: if (!k_next[7]) begin
				ph <= P_KW;
			end else begin
				kv    <= k_next[6:0];
				vaddr <= k_next[6:0];
				k_todo[k_next[6:0]] <= 0;
				k_st  <= 6'd2;
			end
			6'd2: k_st <= 6'd3;          // the memories read
			6'd3: begin                  // the parameter slot is known: its words 1..15
				pram_raddr <= {vps_q, 4'd1};
				k_i  <= 5'd1;
				k_st <= 6'd4;
			end
			6'd4: begin
				pram_raddr <= pram_raddr + 1'd1;
				k_st <= 6'd5;
			end
			6'd5: begin
				w[k_i[3:0]] <= pram_q;
				pram_raddr <= pram_raddr + 1'd1;
				k_i <= k_i + 1'd1;
				if (k_i == 5'd15) begin
					k_go <= 1;
					k_st <= 6'd6;
				end
			end
			6'd6: if (k_done) begin
				v_due[kv]   <= 0;
				v_first[kv] <= 0;
				if (k_off) begin
					v_on[kv] <= 0;
				end else begin
					if (k_released) begin
						v_rls1[kv] <= 0;
						v_rls2[kv] <= 0;
					end
					v_rel[kv] <= k_sec0 >= SEC_RELEASE;
					vk_we <= 1;
					vk_wa <= kv;
					vk_wd <= '{sec0: k_sec0, sec1: k_sec1, cnt0: k_cnt0, cnt1: k_cnt1, x0: k_x0, x1: k_x1,
					          lfo0: k_lfo0, lfo1: k_lfo1, down0: k_down0, down1: k_down1,
					          blocks: k_blocks, killed: k_killed};
					vs = vs_q;
					vs.incr = k_incr;
					vs.ran  = 0;
					vs_we <= 1;
					vs_wa <= kv;
					vs_wd <= vs;
					vg = vg_q;
					vg.amp_step  = k_amp_step;
					vg.send_step = k_send_step;
					vg_we <= 1;
					vg_wa <= kv;
					vg_wd <= vg;
					vf_we <= 1;
					vf_wa <= kv;
					vf_wd <= '{h1: vf_q.h1, h2: vf_q.h2, silent: k_silent, fres: k_fres,
					          fres_incr: k_fres_n != 0 ? k_fres_incr : vf_q.fres_incr, fres_n: k_fres_n, q: k_q};
					cj       <= 1;
					cj_voice <= kv;
					cj_fres  <= k_fres;
					cj_q     <= k_q;
				end
				k_st <= 6'd1;
			end
			default: k_st <= 6'd1;
		endcase

		// ---- the coefficients of every control update are there
		P_KW: if (coef_idle && !cj) ph <= P_S;

		P_S: if (sc == 3'd7 && sv[2:0] == 0 && s_todo == 0) begin
			ph     <= P_FIN;
			fin_st <= 0;
		end

		P_FIN: begin
			fin_st <= fin_st + 1'd1;
			if (fin_st == 3'd3) begin
				m_under  <= under;
				m_voices <= v_count;
				m_bits   <= v_on;
				m_frame  <= F + 1'd1;
				stgl     <= ~stgl;
				dbg_sums <= {sums_c[3], sums_c[2], sums_c[1], sums_c[0]};
				dbg_out  <= {o_r, o_l};
				dbg_frame <= F;
				dbg_tgl  <= ~dbg_tgl;
				ph       <= P_IDLE;
			end
		end

		default: ph <= P_IDLE;
	endcase

	// the sample engine's writes and the flags it changes (stage 3)
	if (s_wr) begin
		vs_we <= 1;
		vs_wa <= sn[3];
		vs_wd <= '{pos: sx[3].pos, rk: sx[3].rk, looped: sx[3].looped, ran: sx[3].ran + 1'd1,
		          k_end: sx[3].k_end, remapped: sx[3].remapped, tailuse: sx[3].tailuse,
		          incr: sx[3].incr, cslot: sx[3].cslot};
		vf_we <= 1;
		vf_wa <= sn[3];
		vf_wd <= '{h1: sx[3].silent ? sx[3].h1 : sx[3].c, h2: sx[3].silent ? sx[3].h2 : sx[3].h1,
		          silent: sx[3].silent, fres: sx[3].fres, fres_incr: sx[3].fres_incr,
		          fres_n: sx[3].fres_n, q: sx[3].q};
		vg_we <= 1;
		vg_wa <= sn[3];
		vg_wd <= '{amp: sx[3].amp, amp_step: sx[3].amp_step, send: sx[3].send, send_step: sx[3].send_step};
		if (sx[3].ended) v_on[sn[3]] <= 0;
	end
	// the sample engine's next voice
	if (ph == P_S && sc == 3'd7) vaddr <= s_next[6:0];
	if (s_cj) begin
		cj       <= 1;
		cj_voice <= sn[0];
		cj_fres  <= s_cj_fres;
		cj_q     <= vf_q.q;
	end
	if (s_under) under <= under + 1'd1;

	if (mclr) begin
		ph      <= P_SWEEP;
		sweep_i <= 0;
		fpend   <= 0;
		under   <= 0;
		v_on    <= 0;
		v_due   <= 0;
		v_first <= 0;
		v_rel   <= 0;
		v_rls1  <= 0;
		v_rls2  <= 0;
		v_hasp  <= 0;
		v_hasc  <= 0;
		m_out_l <= 0;
		m_out_r <= 0;
	end
end

// the voices playing, counted
reg [7:0] v_count;
always @(posedge mclk) begin : count
	integer i;
	reg [7:0] n;
	n = 0;
	for (i = 0; i < 128; i = i + 1) n = n + {7'd0, v_on[i]};
	v_count <= n;
end

// the sums held at 40 bits, the output at 16
wire signed [39:0] sums_c[0:3];
genvar gs;
generate
	for (gs = 0; gs < 4; gs = gs + 1) begin : g_sum
		wire signed [47:0] sv48 = sums[gs];
		assign sums_c[gs] = sv48 > 48'sh7FFFFFFFFF ? 40'sh7FFFFFFFFF
		                  : sv48 < -48'sh8000000000 ? -40'sh8000000000 : sv48[39:0];
	end
endgenerate

// in three clocks: held, scaled, held
reg [15:0] o_l = 0, o_r = 0;
reg signed [39:0] fo_l, fo_r;
reg signed [47:0] fo_pl, fo_pr;
always @(posedge mclk) begin : outputs
	reg signed [47:0] l, r;
	if (ph == P_FIN && fin_st == 3'd0) begin
		fo_l <= sums_c[0];
		fo_r <= sums_c[1];
	end
	if (ph == P_FIN && fin_st == 3'd1) begin
		fo_pl <= 48'(fo_l) * $signed({1'b0, vol});
		fo_pr <= 48'(fo_r) * $signed({1'b0, vol});
	end
	l = fo_pl >>> 20;
	r = fo_pr >>> 20;
	if (ph == P_FIN && fin_st == 3'd2) begin
		o_l <= l > 48'sd32767 ? 16'h7FFF : l < -48'sd32768 ? 16'h8000 : l[15:0];
		o_r <= r > 48'sd32767 ? 16'h7FFF : r < -48'sd32768 ? 16'h8000 : r[15:0];
	end
	if (mclr) begin
		o_l <= 0;
		o_r <= 0;
	end
end

//////////////////////////////////////////////////////////////////
// The sample engine: a voice every 8 clocks, each through four stages of 8
//
// t (the voice's clock, 0..31):
//   0  its memories are read (vaddr)                 1  their values: positions, the cache's and the
//   2  the cache read, a1 * h1                          tables' addresses, the next position, a coefficient job
//   3  its words there, a2 * h2                      4  samples, b1 * h1
//   5  interpolation's products                      6  their sum, a1 * h1 out
//   2..6  the position wrapped into the loop
//   7  a1 * h1 + a2 * h2                             8  the filter's c, b1 * h1 out
//   9  b02 * (c + h2)                                13 y
//   14 y * gain                                      18 out
//   19..22 out * send (4)                            22..25 the sums
//   26 the voice's memories written, the fetcher told
// The big multiplier (27 x 57 bits, out 4 clocks after in) takes a1 * h1, a2 * h2
// and b1 * h1 at t 2, 3, 4 (stage 0), b02 * (c + h2) and y * gain at t 9 and 14
// (stage 1): at clocks 2, 3, 4, 1, 6 of the 8, never two at once.

reg  signed [26:0] bm_a = 0;
reg  signed [56:0] bm_b = 0;
reg  signed [83:0] bm_p1 = 0, bm_p2 = 0, bm_q = 0;
always @(posedge mclk) begin
	bm_p1 <= bm_a * bm_b;
	bm_p2 <= bm_p1;
	bm_q  <= bm_p2;
end

// the sends' multiplier: out (30) * send (24), out 3 clocks after in
reg  signed [29:0] sm_a = 0;
reg  signed [23:0] sm_b = 0;
reg  signed [53:0] sm_p1 = 0, sm_q = 0;
always @(posedge mclk) begin
	sm_p1 <= sm_a * sm_b;
	sm_q  <= sm_p1;
end

// interpolation
wire [71:0] it_q;
hybrid_midi_rom #(.W(72), .D(256), .A(8), .FILE({TABLES, "ms_interp.hex"})) rom_interp
	(.clk(mclk), .addr(vs_q.pos[31:24]), .q(it_q));

wire        s_wr = sv[3] && sc == 3'd2;
reg         s_cj = 0;
reg  [31:0] s_cj_fres;
reg         s_under = 0;
assign s_upd = s_wr && sx[3].upd;
assign s_upd_data = {sx[3].cslot, sx[3].rk, sx[3].remap, sx[3].k_end, sx[3].lfar};

// stage 0's values of the voice (valid from t 1 to t 8)
wire [25:0] s_idx     = vs_q.pos[57:32];
wire        s_looping = (vp_q.mode == 2'd1 || (vp_q.mode == 2'd3 && !v_rel[sn[0]])) && vp_q.le > vp_q.ls;
wire        s_end     = !s_looping && s_idx >= {1'b0, vp_q.stop};
// a voice released out of its loop: where the loop's end is in its stream
wire        s_rmp     = vp_q.mode == 2'd3 && v_rel[sn[0]] && !vs_q.remapped && vp_q.le > vp_q.ls;
wire [25:0] s_d       = {1'b0, vp_q.le} - s_idx;
wire        s_far     = s_d >= 26'd128;
wire  [7:0] s_kend    = vs_q.rk + s_d[7:0];
wire        s_tu      = vs_q.remapped ? vs_q.tailuse : (s_rmp && !s_far);
wire  [7:0] s_ke      = vs_q.remapped ? vs_q.k_end : s_kend;
wire [25:0] s_e       = {1'b0, vp_q.stop} - 26'd1 - s_idx;   // to the sample's last

// the stream positions rk - 1 .. rk + 2
wire  [7:0] s_p0 = vs_q.rk - 8'd1;
wire  [7:0] s_t0 = s_p0 - s_ke;                               // in the tail

// the reads' addresses at t 1, from the memories' outputs (read at t 2,
// there at t 3): bank j holds the one of rk - 1 .. rk + 2 whose position is
// j mod 4 (in the tail: from k_end)
// (short: s_p0 mod 64, and s_t0 mod 16, which is idx - le - 1 before a
// remap; bank j's word is the next one when j is below the low bits)
always @(posedge mclk) begin : s_addr
	integer j;
	reg [5:0] p0;
	reg [3:0] t0;
	if (sc == 3'd1) begin
		p0 = 6'(vs_q.rk - 8'd1);
		t0 = vs_q.remapped ? 4'(vs_q.rk - 8'd1 - vs_q.k_end) : s_idx[3:0] - vp_q.le[3:0] - 4'd1;
		lnd_addr <= vs_q.cslot;
		for (j = 0; j < 4; j = j + 1) begin
			cr_addr[j] <= {vs_q.cslot, p0[5:2] + {3'd0, 2'(j) < p0[1:0]}};
			tr_addr[j] <= {vs_q.cslot, t0[3:2] + {1'b0, 2'(j) < t0[1:0]}};
		end
	end
end

// two steps of the remainder, bits b + 1 and b: less 3, 2 or 1 times
// L << b, the most that fits (a loop longer than 256: L at most once, at the
// last step); the three side by side
function automatic [25:0] mod_step2(input [25:0] r, input [24:0] l, input integer b);
	reg [35:0] d1, d2, d3;
	begin
		d1 = {11'd0, l} << b;
		d2 = {11'd0, l} << (b + 1);
		d3 = d1 + d2;
		mod_step2 = r;
		if (l > 25'd256) begin
			if (b == 0 && r >= {1'b0, l}) mod_step2 = r - {1'b0, l};
		end else if ({10'd0, r} >= d3) mod_step2 = r - d3[25:0];
		else if ({10'd0, r} >= d2) mod_step2 = r - d2[25:0];
		else if ({10'd0, r} >= d1) mod_step2 = r - d1[25:0];
	end
endfunction

// stage 0 registers
reg  [7:0] s2_p0, s2_t0;
reg        s2_tu, s2_looping, s2_end, s2_rmp, s2_far, s2_short;
reg  [7:0] s2_ke, s2_rk0, s2_rkl;
reg [25:0] s2_e;
reg        s3_e1, s3_e2, s3_eq1;   // s2_e >= 1, >= 2, == 1 (t 3)
reg [57:0] s2_adv;
reg [15:0] r_cr[0:3], r_tr[0:3];
reg  [8:0] r_lnd;
reg [15:0] s3_s[0:3];
reg [17:0] s3_c[0:3];
reg signed [33:0] s4_p[0:3];
reg signed [35:0] s5_acc;
reg signed [83:0] s_ra1;
reg signed [84:0] s_suma;
reg signed [83:0] s_rb1;
// the wrap into the loop: (pos - ls) mod (le - ls), two bits a clock
reg [25:0] s_md_r;
reg [24:0] s_md_l;
reg        s_md_wrap;

always @(posedge mclk) begin : sample_engine
	integer j;
	reg [7:0] p, tj;
	reg [15:0] v[0:3];
	reg ok[0:3];
	reg use_[0:3];
	reg bad;
	reg [25:0] dd;
	reg signed [84:0] yy;
	reg signed [60:0] cs;
	reg signed [56:0] cl;
	sx_t x;

	s_cj    <= 0;
	s_under <= 0;

	if (ph == P_S) begin
		sc <= sc + 1'd1;
		// the voices move on a stage
		if (sc == 3'd7) begin
			sv <= {sv[2:0], s_next[7]};
			sn[0] <= s_next[6:0];
			sn[1] <= sn[0];
			sn[2] <= sn[1];
			sn[3] <= sn[2];
			if (s_next[7]) s_todo[s_next[6:0]] <= 0;
		end
	end else begin
		sv <= 0;
		sc <= 3'd7;
		s_todo <= v_on;
	end

	case (sc)
		// t 1: the positions, the reads of the cache and the tables
		3'd1: begin
			s2_p0 <= s_p0;
			s2_t0 <= s_t0;
			s2_tu <= s_tu;
			s2_looping <= s_looping;
			s2_end <= s_end;
			s2_e   <= s_e;
			s2_adv <= vs_q.pos + {18'd0, vs_q.incr};
			s2_ke  <= s_ke;
			s2_rmp <= s_rmp;
			s2_far <= s_far;
			// the stream position after a short loop's wrap: its first lap's
			s2_short <= vp_q.le - vp_q.ls <= 25'd52;
			s2_rk0   <= vp_q.plooped && vp_q.start == vp_q.ls ? 8'd1 : 8'(vp_q.le - vp_q.start + 1'd1);
			// the cutoff on, while it moves, and its coefficients for the next frame
			if (sv[0] && !s_end && !vf_q.silent && vf_q.fres_n != 0) begin
				s_cj      <= 1;
				s_cj_fres <= vf_q.fres + vf_q.fres_incr;
			end
		end
		// t 2: the wrap's start, the stream position on without it
		3'd2: begin
			s2_rkl <= vs_q.rk + 8'(s2_adv[57:32] - s_idx);
			dd = s2_adv[57:32] - {1'b0, vp_q.ls};
			s_md_r    <= dd;
			s_md_l    <= vp_q.le - vp_q.ls;
			s_md_wrap <= s2_looping && s2_adv[57:32] >= {1'b0, vp_q.le};
		end
		// t 3: the reads are there
		3'd3: begin
			s3_e1  <= s2_e >= 26'd1;
			s3_e2  <= s2_e >= 26'd2;
			s3_eq1 <= s2_e == 26'd1;
			for (j = 0; j < 4; j = j + 1) begin
				r_cr[j] <= cr_q[j];
				r_tr[j] <= tr_q[j];
			end
			r_lnd <= lnd_q;
		end
		// t 4: the samples
		3'd4: begin
			bad = 0;
			for (j = 0; j < 4; j = j + 1) begin
				p  = s2_p0 + 8'(j);
				tj = s2_t0 + 8'(j);
				if (s2_tu && tj < 8'd16) begin
					v[j]  = r_tr[tj[1:0]];
					ok[j] = r_lnd[8];
				end else begin
					v[j]  = r_cr[p[1:0]];
					ok[j] = (r_lnd[7:0] - p - 8'd1) < 8'd127;
				end
				use_[j] = j < 2 || s2_looping || (j == 2 ? s3_e1 : s3_e2);
				if (!ok[j]) v[j] = 0;
				if (use_[j] && !ok[j]) bad = 1;
			end
			s3_s[0] <= v[0];
			s3_s[1] <= v[1];
			// past the sample's end, its last
			s3_s[2] <= s2_looping || s3_e1 ? v[2] : v[1];
			s3_s[3] <= s2_looping || s3_e2 ? v[3] : s3_eq1 ? v[2] : v[1];
			for (j = 0; j < 4; j = j + 1) s3_c[j] <= it_q[18 * j +: 18];
			s_under <= sv[0] && !s2_end && bad;
		end
		3'd5: begin
			for (j = 0; j < 4; j = j + 1) s4_p[j] <= $signed(s3_c[j]) * $signed(s3_s[j]);
		end
		3'd6: begin
			s5_acc <= 36'(s4_p[0]) + 36'(s4_p[1]) + 36'(s4_p[2]) + 36'(s4_p[3]);
		end
		default: ;
	endcase

	// the wrap, t 3..6: the remainder by the loop's length (D < L + 256),
	// two bits of the quotient a clock
	case (sc)
		3'd3: s_md_r <= mod_step2(s_md_r, s_md_l, 6);
		3'd4: s_md_r <= mod_step2(s_md_r, s_md_l, 4);
		3'd5: s_md_r <= mod_step2(s_md_r, s_md_l, 2);
		3'd6: s_md_r <= mod_step2(s_md_r, s_md_l, 0);
		default: ;
	endcase

	// the big multiplier's inputs and outputs
	case (sc)
		3'd2: begin bm_a <= $signed(vc_q[53:27]);  bm_b <= 57'($signed(vf_q.h1)); end     // a1 * h1
		3'd3: begin bm_a <= $signed(vc_q[26:0]);   bm_b <= 57'($signed(vf_q.h2)); end     // a2 * h2
		3'd4: begin bm_a <= $signed(vc_q[80:54]);  bm_b <= 57'($signed(vf_q.h1)); end     // b1 * h1
		3'd1: begin bm_a <= $signed(sx[1].b02);    bm_b <= 57'($signed(sx[1].c)) + 57'($signed(sx[1].h2)); end
		3'd6: begin bm_a <= $signed(sx[1].y);      bm_b <= {28'd0, sx[1].amp}; end        // y * gain
		default: ;
	endcase
	case (sc)
		3'd6: s_ra1 <= bm_q;
		3'd7: s_suma <= 85'(s_ra1) + 85'(bm_q);
		3'd0: s_rb1 <= bm_q;
		default: ;
	endcase

	// stage 0 ends: its values into stage 1
	if (sc == 3'd7) begin
		x = '0;
		x.ended     = s2_end;
		x.incr      = vs_q.incr;
		x.cslot     = vs_q.cslot;
		x.looped    = vs_q.looped;
		x.ran       = vs_q.ran;
		x.remapped  = vs_q.remapped | s2_rmp;
		x.tailuse   = s2_tu;
		x.k_end     = s2_ke;
		x.remap     = s2_rmp;
		x.lfar      = s2_far;
		// the position on, back into the loop
		if (s_md_wrap) begin
			x.pos    = {1'b0, 25'(vp_q.ls + s_md_r[24:0]), s2_adv[31:0]};
			x.looped = 1;
		end else x.pos = s2_adv;
		// the stream position on with it; a short loop is played from the
		// cache: back to its first lap
		x.rk  = s_md_wrap && s2_short ? s2_rk0 + 8'(s_md_r) : s2_rkl;
		x.upd = !s2_end && (x.rk != vs_q.rk || s2_rmp);
		x.h1        = vf_q.h1;
		x.h2        = vf_q.h2;
		x.silent    = vf_q.silent;
		x.fres      = vf_q.fres;
		x.fres_incr = vf_q.fres_incr;
		x.fres_n    = vf_q.fres_n;
		if (!s2_end && !vf_q.silent && vf_q.fres_n != 0) begin
			x.fres   = vf_q.fres + vf_q.fres_incr;
			x.fres_n = vf_q.fres_n - 1'd1;
		end
		x.q         = vf_q.q;
		x.amp       = vg_q.amp;
		x.amp_step  = vg_q.amp_step;
		x.send      = vg_q.send;
		x.send_step = vg_q.send_step;
		x.b02       = vc_q[107:81];
		x.x0        = 24'(s5_acc >>> 12);
		sx[1] <= x;
	end

	// stage 1: the filter
	if (sc == 3'd0) begin
		// c = x0 - (a1 h1 + a2 h2) >> 24, held at 56 bits
		cs = 61'($signed(sx[1].x0)) - 61'(s_suma >>> 24);
		sx[1].c <= cs > 61'sh7FFFFFFFFFFFFF ? 56'h7FFFFFFFFFFFFF : cs < -61'sh80000000000000 ? 56'h80000000000000 : cs[55:0];
	end
	if (sc == 3'd5) begin
		// y = (b02 (c + h2) + b1 h1) >> 24, held at 27 bits; none in a silent block
		yy = (85'(bm_q) + 85'(s_rb1)) >>> 24;
		sx[1].y <= sx[1].silent ? 27'd0
		         : yy > 85'sh3FFFFFF ? 27'h3FFFFFF : yy < -85'sh4000000 ? 27'h4000000 : yy[26:0];
	end
	// stages 1 to 2 and 2 to 3, with the gain's step (after y * gain was
	// taken at t 14) and the second send's (t 23)
	if (ph == P_S && sc == 3'd7) begin : shift
		sx_t x1, x2;
		reg signed [30:0] a;
		x1 = sx[1];
		a = $signed({2'b0, x1.amp}) + 31'($signed(x1.amp_step));
		x1.amp = a < 0 ? 29'd0 : a > $signed({2'b0, AMP_MAX}) ? AMP_MAX : a[28:0];
		sx[2] <= x1;
		x2 = sx[2];
		if (sv[2]) begin
			if (x2.ended) begin
				if (x2.ran != 0) x2.send[47:24] = x2.send[47:24] + 24'(sm_q);
			end else x2.send[47:24] = x2.send[47:24] + x2.send_step[47:24];
		end
		sx[3] <= x2;
	end
	// stage 2: out = y * gain >> 26 (with the gain before its step: it was
	// taken at t 14)
	if (sc == 3'd2) sx[2].out <= 30'(bm_q >>> 26);
	// the sends: out * send, or for a voice that ended, step * (64 - ran)
	if (sc >= 3'd3 && sc <= 3'd6) begin : sm_in
		reg [1:0] k;
		k = 2'(sc - 3'd3);
		if (sx[2].ended) begin
			sm_a <= {23'd0, 7'd64 - sx[2].ran};
			sm_b <= sx[2].send_step[24 * k +: 24];
		end else begin
			sm_a <= sx[2].out;
			sm_b <= sx[2].send[24 * k +: 24];
		end
	end
	// t 22..25: the products out, into the sums or, for a voice that ended,
	// its sends (stage 2 at clocks 6 and 7: the second at the shift above;
	// stage 3 at 0 and 1)
	if (sc == 3'd6 && sv[2]) begin
		if (sx[2].ended) begin
			if (sx[2].ran != 0) sx[2].send[23:0] <= sx[2].send[23:0] + 24'(sm_q);
		end else begin
			sums[0] <= sums[0] + 48'(sm_q >>> 12);
			sx[2].send[23:0] <= sx[2].send[23:0] + sx[2].send_step[23:0];
		end
	end
	if (sc == 3'd7 && sv[2] && !sx[2].ended) sums[1] <= sums[1] + 48'(sm_q >>> 12);
	if ((sc == 3'd0 || sc == 3'd1) && sv[3]) begin : sm_out
		reg [6:0] o;
		o = sc == 3'd0 ? 7'd48 : 7'd72;
		if (sx[3].ended) begin
			if (sx[3].ran != 0) sx[3].send[o +: 24] <= sx[3].send[o +: 24] + 24'(sm_q);
		end else begin
			sums[sc == 3'd0 ? 2 : 3] <= sums[sc == 3'd0 ? 2 : 3] + 48'(sm_q >>> 12);
			sx[3].send[o +: 24] <= sx[3].send[o +: 24] + sx[3].send_step[o +: 24];
		end
	end

	if (ph == P_KW) begin
		sums[0] <= 0;
		sums[1] <= 0;
		sums[2] <= 0;
		sums[3] <= 0;
	end
end

endmodule
