//============================================================================
//
//  MiSTer hybrid core host - MIDI synthesizer, the control engine
//
//  A voice's control update, once a block of 64 frames: the C model's
//  control() (tools/midisynth/midisynth.c), every rounding as it has it.
//  hybrid_midi.sv reads the voice's state and parameter block, starts this
//  with `go`, and writes back what comes out at `done`. One multiplier (37 x
//  37 bits) does every product; one goes in a clock and comes out three
//  clocks later. A fixed sequence of steps, 37 clocks.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//============================================================================

module hybrid_midi_ctrl
#(
	parameter TABLES = "../hybrid/rtl/midi/"
)
(
	input             clk,
	input             go,

	// the parameter block: words 1..15 (word 2 is not used here)
	input      [63:0] w1, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15,

	// the voice's state
	input       [2:0] i_sec0, i_sec1,
	input      [15:0] i_cnt0, i_cnt1,
	input      [39:0] i_x0, i_x1,        // Q1.39
	input      [32:0] i_lfo0, i_lfo1,    // Q1.31, signed
	input             i_down0, i_down1,
	input      [15:0] i_blocks,
	input       [1:0] i_release,         // 1 RELEASE, 2 KILL waiting
	input             i_killed,
	input             i_first,
	input             i_looped,
	input      [28:0] i_amp,
	input      [95:0] i_send,            // 4 x 24 bits, signed
	input      [31:0] i_fres,

	output reg        done = 0,
	output reg        o_off,             // the voice ends
	output reg        o_released,        // a release was made: the voice's waiting one is gone
	output reg  [2:0] o_sec0, o_sec1,
	output reg [15:0] o_cnt0, o_cnt1,
	output reg [39:0] o_x0, o_x1,
	output reg [32:0] o_lfo0, o_lfo1,
	output reg        o_down0, o_down1,
	output reg [15:0] o_blocks,
	output reg        o_killed,
	output reg [39:0] o_incr,            // Q8.32
	output reg [29:0] o_amp_step,        // signed
	output reg [95:0] o_send_step,       // 4 x 24 bits, signed
	output reg [31:0] o_fres,
	output reg [31:0] o_fres_incr,
	output reg  [8:0] o_fres_n,
	output reg        o_silent,
	output reg  [9:0] o_q                // the filter's Q for its coefficients (at most 960)
);

localparam [2:0] SEC_DELAY = 0, SEC_ATTACK = 1, SEC_HOLD = 2, SEC_DECAY = 3, SEC_SUSTAIN = 4,
                 SEC_RELEASE = 5, SEC_FINISHED = 6;
localparam signed [41:0] X1       = 42'sd1 <<< 39;
localparam signed [34:0] LFO_ONE  = 35'sd1 <<< 31;
localparam        [28:0] AMP_MAX  = 29'h1FFFFFFF;
localparam        [39:0] INCR_MAX = 40'hFFFFFFFFFF;
localparam        [39:0] KILL_DEC = 40'd2521815660;
localparam        [16:0] K_CB     = 17'd69666;
localparam        [19:0] K_REL    = 20'd526088;
localparam signed [31:0] FRES_LO  = -32'sd55791973;
localparam signed [31:0] FRES_HI  = 32'sd884352868;
localparam        [16:0] INV1200  = 17'd55925;       // 2^26 / 1200, rounded up

// ---- the parameters
wire        [9:0] p_q          = w1[61:52];
wire signed [31:0] p_pitch     = w3[31:0];
wire       [15:0] p_atten      = w3[47:32];
wire signed [15:0] p_root      = w3[63:48];
wire       [21:0] p_fc         = w5[21:0];
wire       [35:0] p_rate       = w5[57:22];
wire signed [23:0] p_mlfo_pitch = w6[23:0];
wire signed [23:0] p_vlfo_pitch = w6[47:24];
wire       [15:0] p_lfo_delay0 = w6[63:48];
wire signed [23:0] p_menv_pitch = w7[23:0];
wire signed [23:0] p_mlfo_fc   = w7[47:24];
wire       [15:0] p_lfo_delay1 = w7[63:48];
wire signed [23:0] p_menv_fc   = w8[23:0];
wire signed [23:0] p_mlfo_vol  = w8[47:24];
wire       [39:0] p_attack0    = {w9[31:0], 8'd0};
wire       [39:0] p_decay0     = {w9[63:32], w8[55:48]};
wire       [39:0] p_sustain0   = {w10[31:0], 8'd0};
wire       [39:0] p_release0   = {w10[63:32], w8[63:56]};
wire       [39:0] p_attack1    = {w11[31:0], 8'd0};
wire       [39:0] p_decay1     = {w11[63:32], 8'd0};
wire       [39:0] p_sustain1   = {w12[31:0], 8'd0};
wire       [39:0] p_release1   = {w12[63:32], 8'd0};
wire       [15:0] p_delay0     = w13[15:0];
wire       [15:0] p_hold0      = w13[31:16];
wire       [15:0] p_delay1     = w13[47:32];
wire       [15:0] p_hold1      = w13[63:48];
wire       [31:0] p_end0       = w14[31:0];
wire       [31:0] p_end1       = w14[63:32];
wire       [31:0] p_incr0      = w15[31:0];
wire       [31:0] p_incr1      = w15[63:32];

// ---- tables
reg   [9:0] log2_a = 0;
wire [15:0] log2_q;
reg   [6:0] cvx_a = 0, cvx_b = 0;
wire [23:0] cvx_qa, cvx_qb;
reg  [10:0] cb_a = 0;
wire [26:0] cb_q;
reg   [9:0] amp_a = 0;
wire [26:0] amp_q;
reg  [10:0] pt_a = 0;
wire [32:0] pt_q;
reg   [9:0] fr_a = 0;
wire  [8:0] frn_q;
wire [26:0] fri_q;

hybrid_midi_rom #(.W(16), .D(1024), .A(10), .FILE({TABLES, "ms_log2.hex"}))     rom_log2  (.clk(clk), .addr(log2_a), .q(log2_q));
hybrid_midi_rom #(.W(24), .D(128),  .A(7),  .FILE({TABLES, "ms_convex.hex"}))   rom_cvxa  (.clk(clk), .addr(cvx_a), .q(cvx_qa));
hybrid_midi_rom #(.W(24), .D(128),  .A(7),  .FILE({TABLES, "ms_convex.hex"}))   rom_cvxb  (.clk(clk), .addr(cvx_b), .q(cvx_qb));
hybrid_midi_rom #(.W(27), .D(1441), .A(11), .FILE({TABLES, "ms_cb2amp.hex"}))   rom_cb    (.clk(clk), .addr(cb_a), .q(cb_q));
hybrid_midi_rom #(.W(27), .D(1024), .A(10), .FILE({TABLES, "ms_amp.hex"}))      rom_amp   (.clk(clk), .addr(amp_a), .q(amp_q));
hybrid_midi_rom #(.W(33), .D(1200), .A(11), .FILE({TABLES, "ms_pitch.hex"}))    rom_pitch (.clk(clk), .addr(pt_a), .q(pt_q));
hybrid_midi_rom #(.W(9),  .D(961),  .A(10), .FILE({TABLES, "ms_fres_n.hex"}))   rom_frn   (.clk(clk), .addr(fr_a), .q(frn_q));
hybrid_midi_rom #(.W(27), .D(961),  .A(10), .FILE({TABLES, "ms_fres_inv.hex"})) rom_fri   (.clk(clk), .addr(fr_a), .q(fri_q));

// ---- the multiplier: m_a * m_b set in step t, m_q in step t + 3
reg  signed [36:0] m_a = 0, m_b = 0;
reg  signed [73:0] m_p = 0, m_q = 0;
always @(posedge clk) begin
	m_p <= m_a * m_b;
	m_q <= m_p;
end

// ---- the envelope's step (env_step()), in two clocks: env_pre() the
// section and count after a delay or hold has ended and x plus the step,
// {section, count, x + step (42 bits, signed)}; env_fin() the compares,
// {section, count, x}
function automatic [61:0] env_pre(input [2:0] sec, input [15:0] cnt, input [39:0] x,
                                   input [15:0] delay, input [39:0] attack, input [15:0] hold,
                                   input [39:0] decay, input [39:0] rel_rate);
	reg [2:0] s;
	reg [15:0] c;
	reg signed [41:0] v;
	begin
		s = sec;
		c = cnt;
		v = {2'b0, x};
		if (s == SEC_DELAY && c >= delay) begin
			s = SEC_ATTACK;
			c = 0;
		end
		if (s == SEC_HOLD && c >= hold) begin
			s = SEC_DECAY;
			c = 0;
		end
		case (s)
			SEC_ATTACK:  v = v + $signed({2'b0, attack});
			SEC_DECAY:   v = v - $signed({2'b0, decay});
			SEC_RELEASE: v = v - $signed({2'b0, rel_rate});
			default: ;
		endcase
		env_pre = {s, c, v};
	end
endfunction

function automatic [58:0] env_fin(input [61:0] e, input [39:0] sustain);
	reg [2:0] s;
	reg [15:0] c;
	reg signed [41:0] v;
	begin
		{s, c, v} = e;
		case (s)
			SEC_DELAY: begin
				v = 0;
				c = c + 1'd1;
			end
			SEC_ATTACK: begin
				if (v >= X1) begin
					v = X1;
					s = SEC_HOLD;
					c = 0;
				end else c = c + 1'd1;
			end
			SEC_HOLD: c = c + 1'd1;
			SEC_DECAY: begin
				if (v < $signed({2'b0, sustain})) begin
					v = $signed({2'b0, sustain});
					s = SEC_SUSTAIN;
					c = 0;
				end else c = c + 1'd1;
			end
			SEC_SUSTAIN: ;
			SEC_RELEASE: begin
				if (v < 0) begin
					v = 0;
					s = SEC_FINISHED;
				end
			end
			default: v = 0;
		endcase
		env_fin = {s, c, v[39:0]};
	end
endfunction

// the LFO's step (lfo_step()): {down, value}
function automatic [33:0] lfo_step(input [32:0] val, input down, input [31:0] incr);
	reg signed [34:0] v;
	reg d;
	begin
		v = $signed({{2{val[32]}}, val});
		d = down;
		v = d ? v - $signed({3'b0, incr}) : v + $signed({3'b0, incr});
		if (v > LFO_ONE) begin
			d = 1;
			v = (LFO_ONE <<< 1) - v;
		end else if (v < -LFO_ONE) begin
			d = 0;
			v = -(LFO_ONE <<< 1) - v;
		end
		lfo_step = {d, v[32:0]};
	end
endfunction

// the highest bit set (x > 0)
function automatic [4:0] msb32(input [31:0] x);
	integer k;
	begin
		msb32 = 0;
		for (k = 0; k < 32; k = k + 1) if (x[k]) msb32 = k[4:0];
	end
endfunction

// cb2amp()'s table for an attenuation c >= 0 (1/4096 cB): its whole cB, 1441
// for above 1440 (none: 0)
function automatic [10:0] cb_index(input signed [31:0] c);
	cb_index = (c >>> 12) > 32'sd1440 ? 11'd1441 : 11'(c >>> 12);
endfunction

// ---- working values
reg   [5:0] st = 0;
reg         rel;               // a release in this update
reg   [4:0] rn;                // att2rel(): the shift of x
reg  [22:0] cf;                // convex(): the fraction
reg         cvx_top;           // convex(): i >= 127
reg         attack1;           // the modulation envelope's convex value is wanted
reg  signed [24:0] menv;       // the modulation envelope's value, Q1.23
reg  signed [31:0] lfovol;
reg  signed [31:0] cc;         // the attenuation of the envelope and the LFO, 1/4096 cB
reg  [10:0] ca_i;              // cb2amp(attenuation)'s entry
reg  [26:0] ca;
reg  [28:0] cb;                // cb2amp(cc)
reg   [1:0] cn;                // cb2amp(cc < 0): the shift
reg         cb_max;
reg  signed [57:0] pm, fm;     // the pitch's and the cutoff's sums, Q.39
reg  signed [20:0] cents;
reg  signed [20:0] qo = 0;     // octaves of it
reg  signed [21:0] rem;
reg  signed [31:0] fres_new;
reg  [31:0] target;
reg  [68:0] prod = 0;
reg  signed [20:0] qa;         // the octaves, before they are made exact
reg  signed [21:0] r0;         // and the remainder
reg  signed [31:0] c_env;      // the attenuation of the envelope, 1/4096 cB
reg  signed [73:0] y6;         // att2rel()'s y
reg         y6_do;
reg  [61:0] e0p, e1p;          // the envelopes' steps, before the compares: {section, count, x + step}

wire signed [31:0] att_c = {12'd0, p_atten, 4'd0} <<< 4;     // atten << 8
wire [31:0] rx  = i_x0[39:8];                               // Q1.31
wire [31:0] rxn = rx << (5'd31 - msb32(rx));
wire [30:0] cv_rel = 31'(i_x1[39:16]) * 31'd127;           // convex(): x * 127
wire [30:0] cv_mod = 31'(o_x1[39:16]) * 31'd127;
wire signed [32:0] menv8 = 33'(menv) <<< 8;                 // Q1.31

// the increment from rate * 2^(rem / 1200) and the octaves (step 5's end),
// in two clocks: shifted (inc_w, or inc_k 1: 0, 2: the most), then held
wire signed [21:0] shf = 22'sd32 - 22'(qo);
reg [127:0] inc_w;
reg   [1:0] inc_k;
wire [39:0] incr_c = inc_k == 2'd1 ? 40'd0 : inc_k == 2'd2 ? INCR_MAX
                   : inc_w[127:40] != 0 ? INCR_MAX : inc_w[39:0];

always @(posedge clk) begin
	done <= 0;
	m_a  <= 0;
	m_b  <= 0;

	case (st)
		6'd0: if (go) begin
			o_off      <= 0;
			o_released <= 0;
			o_sec0     <= i_sec0;
			o_sec1     <= i_sec1;
			o_cnt0     <= i_cnt0;
			o_cnt1     <= i_cnt1;
			o_x0       <= i_x0;
			o_x1       <= i_x1;
			o_lfo0     <= i_lfo0;
			o_lfo1     <= i_lfo1;
			o_down0    <= i_down0;
			o_down1    <= i_down1;
			o_killed   <= i_killed;
			o_q        <= p_q > 10'd960 ? 10'd960 : p_q;
			fr_a       <= p_q > 10'd960 ? 10'd960 : p_q;
			o_blocks   <= i_blocks == 16'hFFFF ? 16'hFFFF : i_blocks + 1'd1;
			// 1. a release waits for 7 updates (FluidSynth's 10 ms of a note)
			rel <= i_release != 0 && i_blocks >= 7;
			st  <= (i_release != 0 && i_blocks >= 7) ? 6'd1 : 6'd8;
		end

		// 1. both envelopes to their release; from the attack, the volume
		// envelope's amplitude to dB (att2rel()) and the modulation
		// envelope's convex value, without a jump
		6'd1: begin
			o_released <= 1;
			o_sec0 <= SEC_RELEASE;
			o_sec1 <= SEC_RELEASE;
			o_cnt0 <= 0;
			o_cnt1 <= 0;
			if (i_release == 2'd2) o_killed <= 1;
			rn      <= 5'd31 - msb32(rx);
			log2_a  <= rxn[30:21];
			cvx_a   <= cv_rel[29:23] >= 7'd127 ? 7'd127 : cv_rel[29:23];
			cvx_b   <= cv_rel[29:23] >= 7'd126 ? 7'd127 : cv_rel[29:23] + 1'd1;
			cvx_top <= cv_rel[30:23] >= 8'd127;
			cf      <= cv_rel[22:0];
			st <= 6'd2;
		end
		6'd2: st <= 6'd3;
		6'd3: begin
			m_a <= 37'($signed({1'b0, log2_q}) - $signed({11'd0, rn, 16'd0}));
			m_b <= {17'd0, K_REL};
			st <= 6'd4;
		end
		6'd4: begin
			m_a <= 37'($signed({1'b0, cvx_qb}) - $signed({1'b0, cvx_qa}));
			m_b <= {14'd0, cf};
			st <= 6'd5;
		end
		6'd5: st <= 6'd6;
		6'd6: begin
			// y = 1 + log2(x) * 200 / 960 * log10(2), held in 0..1 (step 7)
			y6    <= (74'sd1 <<< 31) + (m_q >>> 8);
			y6_do <= i_sec0 == SEC_ATTACK && i_x0 != 0;
			st <= 6'd7;
		end
		6'd7: begin
			if (y6_do) o_x0 <= y6 < 0 ? 40'd0 : y6 > (74'sd1 <<< 31) ? (40'd1 << 39) : {y6[31:0], 8'd0};
			if (i_sec1 == SEC_ATTACK && i_x1 != 0)
				o_x1 <= cvx_top ? {cvx_qa, 16'd0} : {24'(cvx_qa + 24'(m_q >>> 23)), 16'd0};
			st <= 6'd8;
		end

		// 2. both envelopes a block on: the sections and the sums, then the
		// compares (step 10)
		6'd8: begin
			e0p <= env_pre(o_sec0, o_cnt0, o_x0, p_delay0, p_attack0, p_hold0, p_decay0,
			               o_killed ? KILL_DEC : p_release0);
			e1p <= env_pre(o_sec1, o_cnt1, o_x1, p_delay1, p_attack1, p_hold1, p_decay1, p_release1);
			st <= 6'd10;
		end

		// 2. both envelopes a block on, 9. the end of a finished voice; 3. the
		// LFOs once their delay is over
		6'd10: begin : env_blk
			reg [58:0] e0, e1;
			e0 = env_fin(e0p, p_sustain0);
			e1 = env_fin(e1p, p_sustain1);
			{o_sec0, o_cnt0, o_x0} <= e0;
			{o_sec1, o_cnt1, o_x1} <= e1;
			if (i_blocks >= p_lfo_delay0) {o_down0, o_lfo0} <= lfo_step(o_lfo0, o_down0, p_incr0);
			if (i_blocks >= p_lfo_delay1) {o_down1, o_lfo1} <= lfo_step(o_lfo1, o_down1, p_incr1);
			if (e0[58:56] == SEC_FINISHED) begin
				o_off <= 1;
				st <= 6'd38;
			end else st <= 6'd11;
		end

		// 4. the modulation envelope's value: in its attack, convex
		6'd11: begin
			menv    <= 25'(o_x1[39:16]);
			attack1 <= o_sec1 == SEC_ATTACK && o_x1[39:16] != 0;
			cvx_a   <= cv_mod[29:23] >= 7'd127 ? 7'd127 : cv_mod[29:23];
			cvx_b   <= cv_mod[29:23] >= 7'd126 ? 7'd127 : cv_mod[29:23] + 1'd1;
			cvx_top <= cv_mod[30:23] >= 8'd127;
			cf      <= cv_mod[22:0];
			st <= (o_sec1 == SEC_ATTACK && o_x1[39:16] != 0) ? 6'd12 : 6'd17;
		end
		6'd12: st <= 6'd13;
		6'd13: begin
			m_a <= 37'($signed({1'b0, cvx_qb}) - $signed({1'b0, cvx_qa}));
			m_b <= {14'd0, cf};
			st <= 6'd14;
		end
		6'd14: st <= 6'd15;
		6'd15: st <= 6'd16;
		6'd16: begin
			menv <= cvx_top ? 25'(cvx_qa) : 25'(cvx_qa + 24'(m_q >>> 23));
			st <= 6'd17;
		end

		// the products, one a clock, each out three steps later:
		//   17 lfovol   18 pitch: mod LFO   19 pitch: vibrato   20 pitch: mod env
		//   21 e of cb2amp(cc < 0)   22 cutoff: mod LFO   23 cutoff: mod env
		//   25 octaves   28 gain   29 cutoff step   31 gain * x   32 increment
		6'd17: begin
			m_a  <= 37'(signed'(o_lfo0));
			m_b  <= 37'(p_mlfo_vol);
			pm   <= 58'(p_pitch) <<< 24;
			fm   <= 58'(signed'({1'b0, p_fc})) <<< 31;
			cb_a <= cb_index(att_c);
			ca_i <= cb_index(att_c);
			c_env <= 32'(((52'(X1[39:0]) - 52'(o_x0)) * 52'd960) >> 27);
			st <= 6'd18;
		end
		6'd18: begin
			m_a <= 37'(signed'(o_lfo0));
			m_b <= 37'(p_mlfo_pitch);
			st <= 6'd19;
		end
		6'd19: begin
			m_a <= 37'(signed'(o_lfo1));
			m_b <= 37'(p_vlfo_pitch);
			ca  <= ca_i > 11'd1440 ? 27'd0 : cb_q;
			st <= 6'd20;
		end
		6'd20: begin
			begin : g_blk
				reg signed [31:0] lv, c;
				lv = 32'(m_q >>> 27);
				lfovol <= lv;
				// 7. the attenuation of the envelope and the LFO, 1/4096 cB
				c = (o_sec0 == SEC_ATTACK ? 32'sd0 : c_env) - lv;
				cc <= c;
				// 9. the end of an inaudible voice, after its attack
				if (o_sec0 != SEC_DELAY && o_sec0 != SEC_ATTACK
				    && o_x0 < {(i_looped ? p_end1 : p_end0), 8'd0})
					o_off <= 1;
			end
			m_a <= 37'(menv8);
			m_b <= 37'(p_menv_pitch);
			st <= 6'd21;
		end
		6'd21: begin
			pm   <= pm + 58'(m_q);
			cb_a <= cb_index(cc);
			if (cc < 0) begin
				m_a <= 37'(-(cc >>> 8));
				m_b <= {20'd0, K_CB};
			end
			st <= o_off ? 6'd38 : 6'd22;
		end
		6'd22: begin
			pm  <= pm + 58'(m_q);
			m_a <= 37'(signed'(o_lfo0));
			m_b <= 37'(p_mlfo_fc);
			st <= 6'd23;
		end
		6'd23: begin
			pm  <= pm + 58'(m_q);
			cb  <= cb_a > 11'd1440 ? 29'd0 : {2'd0, cb_q};
			m_a <= 37'(menv8);
			m_b <= 37'(p_menv_fc);
			st <= 6'd24;
		end
		6'd24: begin
			// cb2amp(cc < 0): e = -(cc >> 8) * K >> 16, 2^-e/1024, held below 8
			begin : e_blk
				reg [31:0] e;
				reg [22:0] n;
				e = 32'(m_q >>> 16);
				n = e[31:10] + {22'd0, e[9:0] != 0};
				cn     <= n[1:0];
				cb_max <= n >= 3;
				amp_a  <= 10'(11'd1024 - {1'b0, e[9:0]});
			end
			// 5. whole cents, towards 0, less the root
			cents <= 21'((pm >= 0 ? (pm >>> 39) : -((-pm) >>> 39)) - 58'(p_root));
			st <= 6'd25;
		end
		6'd25: begin
			fm  <= fm + 58'(m_q);
			// the octaves: cents * 2^26 / 1200, made exact in step 28
			m_a <= 37'(cents);
			m_b <= {20'd0, INV1200};
			st <= 6'd26;
		end
		6'd26: begin
			fm <= fm + 58'(m_q);
			st <= 6'd27;
		end
		6'd27: begin
			if (cc < 0) cb <= cb_max ? AMP_MAX : 29'({amp_q, 2'b0} >> (2'd2 - cn));
			// 6. the cutoff, Q.39 to Q.16 rounded down, held
			begin : f_blk
				reg signed [57:0] f;
				f = fm >>> 23;
				fres_new <= f < 58'(FRES_LO) ? FRES_LO : f > 58'(FRES_HI) ? FRES_HI : 32'(f);
			end
			st <= 6'd28;
		end
		6'd28: begin
			qa  <= 21'(m_q >>> 26);
			m_a <= {10'd0, ca};
			m_b <= {8'd0, cb};
			st <= 6'd29;
		end
		6'd29: begin
			r0  <= 22'(cents) - 22'(qa) * 22'sd1200;
			m_a <= 37'(fres_new - $signed(i_fres));
			m_b <= {10'd0, fri_q};
			st <= 6'd30;
		end
		6'd30: begin
			// the octaves and the remainder, made exact
			if (r0 < 0) begin
				qo   <= qa - 1'd1;
				pt_a <= 11'(r0 + 22'sd1200);
			end else if (r0 >= 22'sd1200) begin
				qo   <= qa + 1'd1;
				pt_a <= 11'(r0 - 22'sd1200);
			end else begin
				qo   <= qa;
				pt_a <= r0[10:0];
			end
			st <= 6'd31;
		end
		6'd31: begin
			target <= 32'(m_q >>> 26);
			m_a <= {5'd0, 32'(m_q >>> 26)};
			m_b <= {5'd0, o_x0[39:8]};
			st <= 6'd32;
		end
		6'd32: begin
			// the cutoff: more than a cent away, in steps over a number of frames
			begin : fr_blk
				reg signed [31:0] d;
				d = fres_new - $signed(i_fres);
				if (!i_first && (d > 32'sd65536 || d < -32'sd65536)) begin
					o_fres      <= i_fres;
					o_fres_incr <= 32'(m_q >>> 32);
					o_fres_n    <= frn_q;
				end else begin
					o_fres      <= fres_new;
					o_fres_incr <= 0;
					o_fres_n    <= 0;
				end
			end
			m_a <= {1'b0, p_rate};
			m_b <= {4'd0, pt_q};
			st <= 6'd33;
		end
		6'd33: st <= 6'd34;
		6'd34: begin
			// 7. the gain's target, 8. its steps and the sends' over the block
			begin : t_blk
				reg [31:0] t;
				reg signed [30:0] ds;
				reg signed [24:0] dk;
				integer k;
				t = o_sec0 == SEC_DELAY ? 32'd0 : o_sec0 == SEC_ATTACK ? 32'(m_q >>> 31) : target;
				t = t > {3'd0, AMP_MAX} ? {3'd0, AMP_MAX} : t;
				ds = $signed({2'b0, t[28:0]}) - $signed({2'b0, i_amp});
				o_amp_step <= 30'(ds >>> 6);
				o_silent   <= i_amp == 0 && (ds >>> 6) == 0;
				for (k = 0; k < 4; k = k + 1) begin
					dk = $signed({3'b0, w4[16 * k +: 16], 6'd0}) - 25'(signed'(i_send[24 * k +: 24]));
					o_send_step[24 * k +: 24] <= 24'(dk >>> 6);
				end
			end
			st <= 6'd35;
		end
		6'd35: begin
			prod <= m_q[68:0];
			st <= 6'd36;
		end
		6'd36: begin
			inc_k <= 0;
			inc_w <= 0;
			if (shf >= 22'sd69) inc_k <= 2'd1;
			else if (shf >= 0) inc_w <= {59'd0, prod} >> shf;
			else if (prod == 0) inc_k <= 2'd1;
			else if (-shf >= 22'sd40) inc_k <= 2'd2;
			else inc_w <= {59'd0, prod} << (-shf);
			st <= 6'd37;
		end
		6'd37: begin
			o_incr <= incr_c == 0 ? 40'd1 : incr_c;
			st <= 6'd38;
		end
		6'd38: begin
			done <= 1;
			st <= 6'd0;
		end
		default: st <= 6'd0;
	endcase
end

endmodule
