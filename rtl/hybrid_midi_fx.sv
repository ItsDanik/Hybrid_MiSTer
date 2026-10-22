//============================================================================
//
//  MiSTer hybrid core host - MIDI synthesizer, the reverb and the chorus
//
//  FluidSynth 2.6.1's reverb "free" (Freeverb) and its chorus with their
//  defaults, as the C model's ms_effects() (tools/midisynth/midisynth.c;
//  docs/midi_synth.md, "The effects"), sample-exact. A frame's reverb send
//  (sums[2]) and chorus send (sums[3]) in, what they add to the left and the
//  right out, about 130 clocks after `go`.
//    Every delay line in one memory of 27,499 words of 24 bits, never
//  cleared: a reverb line reads 0 until it has gone round once, the chorus's
//  line where it has not been written yet.
//    One multiplier (27 x 27, two clocks from its inputs to its product).
//  The reverb: its 16 combs a clock in three, overlapped (the read, then
//  b * damp2 and store * damp1, the filter's store, store * feedback, the
//  write: 9 clocks), the allpass lines read in between; then the eight
//  allpasses in series, a clock each, and the output's four products. The
//  chorus: its three blocks one after another (every 5th frame first the
//  LFO's step: the sine table, its interpolation, the position), then its
//  output's four products.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//============================================================================

module hybrid_midi_fx
#(
	parameter TABLES = "../hybrid/rtl/midi/"
)
(
	input                    clk,
	input                    clr,
	input                    go,          // the frame's sums are there (held until done)
	input      signed [39:0] rev_in,      // sums[2]
	input      signed [39:0] cho_in,      // sums[3]
	output reg               done = 0,    // a clock: wet_l and wet_r are the frame's
	output reg signed [39:0] wet_l = 0,
	output reg signed [39:0] wet_r = 0
);

// as tools/midisynth/midisynth_tables.h (mktables.py, fx_consts())
localparam signed [26:0] K_REV_IN = 27'sd1966;     // Q.16
localparam signed [26:0] K_FB     = 27'sd27525;    // Q.15 from here
localparam signed [26:0] K_DAMP1  = 27'sd6554;
localparam signed [26:0] K_DAMP2  = 27'sd26214;
localparam signed [26:0] K_RWET1  = 27'sd53389;
localparam signed [26:0] K_RWET2  = 27'sd5932;
localparam signed [26:0] K_CWET1  = 27'sd36045;
localparam signed [26:0] K_CWET2  = -27'sd29491;
localparam signed [26:0] K_DEPTH  = 27'sd93;
localparam        [11:0] CHO_SIZE = 12'd2049;
localparam        [11:0] CHO_RATE = 12'd5;
localparam        [47:0] CHO_STEP = 48'd6382652533; // Q.48 of a turn
localparam        [14:0] CHO_BASE = 15'd25450;      // the chorus's line, after the reverb's
localparam        [18:0] SIN_ONE  = 19'd262144;     // the sine table's last point, not in the ROM

// the reverb's lines: the combs, left then right, then the allpasses
function automatic [14:0] lbase(input [4:0] n);
	case (n)
		5'd0:  lbase = 15'd0;     5'd1:  lbase = 15'd1116;  5'd2:  lbase = 15'd2304;  5'd3:  lbase = 15'd3581;
		5'd4:  lbase = 15'd4937;  5'd5:  lbase = 15'd6359;  5'd6:  lbase = 15'd7850;  5'd7:  lbase = 15'd9407;
		5'd8:  lbase = 15'd11024; 5'd9:  lbase = 15'd12163; 5'd10: lbase = 15'd13374; 5'd11: lbase = 15'd14674;
		5'd12: lbase = 15'd16053; 5'd13: lbase = 15'd17498; 5'd14: lbase = 15'd19012; 5'd15: lbase = 15'd20592;
		5'd16: lbase = 15'd22232; 5'd17: lbase = 15'd22788; 5'd18: lbase = 15'd23229; 5'd19: lbase = 15'd23570;
		5'd20: lbase = 15'd23795; 5'd21: lbase = 15'd24374; 5'd22: lbase = 15'd24838; default: lbase = 15'd25202;
	endcase
endfunction

function automatic [10:0] llast(input [4:0] n);     // the length less one
	case (n)
		5'd0:  llast = 11'd1115; 5'd1:  llast = 11'd1187; 5'd2:  llast = 11'd1276; 5'd3:  llast = 11'd1355;
		5'd4:  llast = 11'd1421; 5'd5:  llast = 11'd1490; 5'd6:  llast = 11'd1556; 5'd7:  llast = 11'd1616;
		5'd8:  llast = 11'd1138; 5'd9:  llast = 11'd1210; 5'd10: llast = 11'd1299; 5'd11: llast = 11'd1378;
		5'd12: llast = 11'd1444; 5'd13: llast = 11'd1513; 5'd14: llast = 11'd1579; 5'd15: llast = 11'd1639;
		5'd16: llast = 11'd555;  5'd17: llast = 11'd440;  5'd18: llast = 11'd340;  5'd19: llast = 11'd224;
		5'd20: llast = 11'd578;  5'd21: llast = 11'd463;  5'd22: llast = 11'd363;  default: llast = 11'd247;
	endcase
endfunction

// v >> n rounded to the nearest, half up
function automatic signed [53:0] rnd(input signed [53:0] v, input integer n);
	rnd = (v + (54'sd1 <<< (n - 1))) >>> n;
endfunction

function automatic signed [23:0] hold24(input signed [53:0] v);
	hold24 = v > 54'sd8388607 ? 24'sd8388607 : v < -54'sd8388608 ? -24'sd8388608 : v[23:0];
endfunction

function automatic signed [25:0] hold26(input signed [53:0] v);
	hold26 = v > 54'sd33554431 ? 26'sd33554431 : v < -54'sd33554432 ? -26'sd33554432 : v[25:0];
endfunction

function automatic signed [26:0] hold27(input signed [53:0] v);
	hold27 = v > 54'sd67108863 ? 27'sd67108863 : v < -54'sd67108864 ? -27'sd67108864 : v[26:0];
endfunction

//////////////////////////////////////////////////////////////////
// The delay lines, read two clocks after the address, and the multiplier,
// its product two clocks after its inputs

reg [23:0] mem[0:27498];
reg [14:0] ra = 0, wa = 0;
reg [23:0] rq, wd;
reg        we = 0;
always @(posedge clk) begin
	rq <= mem[ra];
	if (we) mem[wa] <= wd;
end

reg signed [26:0] ma = 0, mb = 0;
reg signed [53:0] mp;
always @(posedge clk) mp <= ma * mb;

// the chorus's sine: a quarter wave of 1024 points (the 1025th is SIN_ONE)
reg   [9:0] sa = 0;
wire [18:0] sq;
hybrid_midi_rom #(.W(19), .D(1024), .A(10), .FILE({TABLES, "ms_fxsin.hex"})) sine
(
	.clk(clk), .addr(sa), .q(sq)
);

//////////////////////////////////////////////////////////////////

localparam [2:0] S_IDLE = 0, S_REV = 1, S_AP = 2, S_RWET = 3, S_CHO = 4, S_CWET = 5;
reg   [2:0] s = S_IDLE;
reg   [5:0] t;
reg signed [39:0] r_in, c_in;

// the reverb
reg  [10:0] ptr[0:23];
reg  [23:0] filled = 0;            // the line has gone round
reg signed [24:0] store[0:15];     // the combs' low-pass filters
reg signed [23:0] x;               // the input
reg signed [53:0] lo;
reg   [1:0] m3;                    // the clock in a comb's three
reg   [4:0] cn;                    // combs read
reg   [3:0] an;                    // allpass lines read
reg   [9:1] cv = 0;                // a comb read 1..9 clocks ago, and which
reg   [3:0] ci[1:9];
reg   [2:1] av = 0;                // an allpass line read 1..2 clocks ago, and which
reg   [2:0] ai[1:2];
reg signed [23:0] apb[0:7];
reg signed [26:0] csum[0:1];
reg signed [53:0] p1, q0;
reg signed [24:0] st_r;
reg   [2:0] aj;
reg signed [26:0] apo, side[0:1];
reg signed [39:0] rw_l, rw_r;

// the chorus
reg  [11:0] cin = 0, ccnt = 0;     // where the next input goes, words written (to CHO_SIZE)
reg  [11:0] center;
reg   [2:0] crate;
reg         cstep;
reg  [47:0] phase[0:2];
reg  [11:0] cout[0:2];             // each block's read position
reg  [17:0] frac[0:2];
reg signed [25:0] clast[0:2];      // its interpolator's last output
reg   [1:0] cb;
reg  [10:0] si;
reg  [19:0] sf;
reg         sneg;
reg  [18:0] tv;
reg signed [31:0] pos;
reg  [11:0] ca1;
reg         ok0, ok1;
reg signed [23:0] co;
reg signed [26:0] d0, d1;

always @(posedge clk) begin : fx
	integer i;
	reg signed [23:0] b;
	reg signed [53:0] v;
	reg signed [27:0] in2;
	reg  [30:0] xx;
	reg  [31:0] p32;
	reg  [47:0] ph;
	reg signed [13:0] at;
	reg  [18:0] t1v;
	reg signed [25:0] o;

	we   <= 0;
	done <= 0;
	t    <= t + 1'd1;
	cv   <= {cv[8:1], 1'b0};
	av   <= {av[1], 1'b0};
	for (i = 9; i > 1; i = i - 1) ci[i] <= ci[i - 1];
	ai[2] <= ai[1];

	case (s)
		S_IDLE: if (go) begin
			r_in    <= rev_in;
			c_in    <= cho_in;
			t       <= 0;
			m3      <= 0;
			cn      <= 0;
			an      <= 0;
			csum[0] <= 0;
			csum[1] <= 0;
			s       <= S_REV;
		end

		// ---- the combs, the allpass lines' reads
		S_REV: begin
			m3 <= m3 == 2'd2 ? 2'd0 : m3 + 1'd1;
			if (m3 == 2'd0 && !cn[4]) begin
				ra    <= lbase(cn) + 15'(ptr[cn]);
				cv[1] <= 1;
				ci[1] <= cn[3:0];
				cn    <= cn + 1'd1;
			end
			if (m3 == 2'd1 && !an[3]) begin
				ra    <= lbase({2'b10, an[2:0]}) + 15'(ptr[{2'b10, an[2:0]}]);
				av[1] <= 1;
				ai[1] <= an[2:0];
				an    <= an + 1'd1;
			end

			// the input, x = sums[2] * K_REV_IN >> 22, in two products
			if (t == 6'd0) begin
				ma <= {7'd0, r_in[19:0]};
				mb <= K_REV_IN;
			end
			if (t == 6'd1) begin
				ma <= 27'($signed(r_in[39:20]));
				mb <= K_REV_IN;
			end
			if (t == 6'd2) lo <= mp;
			if (t == 6'd3) x <= hold24(rnd((mp <<< 20) + lo, 22));

			if (cv[2]) begin
				b = filled[ci[2]] ? $signed(rq) : 24'sd0;
				ma <= 27'(b);
				mb <= K_DAMP2;
				csum[ci[2][3]] <= csum[ci[2][3]] + 27'(b);
			end
			if (cv[3]) begin
				ma <= 27'(store[ci[3]]);
				mb <= K_DAMP1;
			end
			if (cv[4]) p1 <= mp;
			if (cv[5]) begin
				v = rnd(p1 + mp, 15);
				store[ci[5]] <= v[24:0];
				st_r <= v[24:0];
			end
			if (cv[7]) begin
				ma <= 27'(st_r);
				mb <= K_FB;
			end
			if (cv[9]) begin
				we <= 1;
				wa <= lbase({1'b0, ci[9]}) + 15'(ptr[ci[9]]);
				wd <= hold24(54'(x) + rnd(mp, 15));
			end
			if (av[2]) apb[ai[2]] <= filled[{2'b10, ai[2]}] ? $signed(rq) : 24'sd0;

			// the last comb's write
			if (t == 6'd54) begin
				aj <= 0;
				s  <= S_AP;
			end
		end

		// ---- the allpasses, four in series on each side
		S_AP: begin
			in2 = aj[1:0] == 2'd0 ? 28'(csum[aj[2]]) : 28'(apo);
			we <= 1;
			wa <= lbase({2'b10, aj}) + 15'(ptr[{2'b10, aj}]);
			wd <= hold24(rnd(54'(in2) + 54'(apb[aj]), 1));
			apo <= hold27(54'(apb[aj]) * 2 - 54'(in2));
			if (aj[1:0] == 2'd3) side[aj[2]] <= hold27(54'(apb[aj]) * 2 - 54'(in2));
			aj <= aj + 1'd1;
			if (aj == 3'd7) begin
				for (i = 0; i < 24; i = i + 1) begin
					if (ptr[i] == llast(5'(i))) begin
						ptr[i]    <= 0;
						filled[i] <= 1;
					end else ptr[i] <= ptr[i] + 1'd1;
				end
				t <= 0;
				s <= S_RWET;
			end
		end

		// ---- the reverb's output
		S_RWET: begin
			case (t)
				6'd0: begin ma <= side[0]; mb <= K_RWET1; end
				6'd1: begin ma <= side[1]; mb <= K_RWET2; end
				6'd2: begin ma <= side[1]; mb <= K_RWET1; q0 <= mp; end
				6'd3: begin ma <= side[0]; mb <= K_RWET2; rw_l <= 40'(rnd(q0 + mp, 15) <<< 6); end
				6'd4: q0 <= mp;
				default: begin
					rw_r  <= 40'(rnd(q0 + mp, 15) <<< 6);
					cstep <= crate + 1'd1 >= CHO_RATE[2:0];
					cb    <= 0;
					t     <= crate + 1'd1 >= CHO_RATE[2:0] ? 6'd0 : 6'd9;
					s     <= S_CHO;
				end
			endcase
		end

		// ---- the chorus's blocks: t 0..8 the LFO's step (every 5th frame),
		// 9..14 the line read and interpolated
		S_CHO: begin
			case (t)
				6'd0: begin
					ph = phase[cb] + CHO_STEP;
					phase[cb] <= ph;
					p32 = ph[47:16];
					xx = p32[30] ? 31'h40000000 - {1'b0, p32[29:0]} : {1'b0, p32[29:0]};
					si   <= xx[30:20];
					sf   <= xx[19:0];
					sneg <= p32[31];
					sa   <= xx[29:20];
				end
				6'd1: sa <= sa + 1'd1;
				6'd2: tv <= si[10] ? SIN_ONE : sq;
				6'd3: begin
					t1v = si == 11'd1023 ? SIN_ONE : sq;
					ma <= 27'(t1v) - 27'(tv);
					mb <= {7'd0, sf};
				end
				6'd5: begin
					v = $signed(54'(tv)) + (si[10] ? 54'sd0 : mp >>> 20);
					ma <= 27'(sneg ? -v : v);
					mb <= K_DEPTH;
				end
				6'd7: pos <= $signed({2'd0, center, 18'd0}) + 32'(mp);
				6'd8: begin
					at = pos[31:18];
					frac[cb] <= pos[17:0];
					cout[cb] <= at < 0 ? 12'(at + 14'sd2049) : at >= 14'sd2049 ? 12'(at - 14'sd2049) : 12'(at);
				end
				6'd9: begin
					ra  <= CHO_BASE + 15'(cout[cb]);
					ok0 <= cout[cb] < ccnt;
					ca1 <= cout[cb] == CHO_SIZE - 1'd1 ? 12'd0 : cout[cb] + 1'd1;
					cout[cb] <= cout[cb] == CHO_SIZE - 1'd1 ? 12'd0 : cout[cb] + 1'd1;
				end
				6'd10: begin
					ra  <= CHO_BASE + 15'(ca1);
					ok1 <= ca1 < ccnt;
				end
				6'd11: co <= ok0 ? $signed(rq) : 24'sd0;
				6'd12: begin
					ma <= 27'(ok1 ? $signed(rq) : 24'sd0) - 27'(clast[cb]);
					mb <= {9'd0, frac[cb]};
				end
				6'd14: begin
					o = hold26(54'(co) + rnd(mp, 18));
					clast[cb] <= o;
					d0 <= cb == 2'd1 ? d0 : 27'(o) + (cb == 2'd2 ? d0 : 27'sd0);
					d1 <= cb == 2'd0 ? d1 : 27'(o) + (cb == 2'd2 ? d1 : 27'sd0);
					cb <= cb + 1'd1;
					t  <= cstep ? 6'd0 : 6'd9;
					if (cb == 2'd2) begin
						if (cstep) begin
							crate  <= 0;
							center <= center + CHO_RATE >= CHO_SIZE ? center + CHO_RATE - CHO_SIZE : center + CHO_RATE;
						end else crate <= crate + 1'd1;
						t <= 0;
						s <= S_CWET;
					end
				end
				default: ;
			endcase
		end

		// ---- the chorus's input and output
		S_CWET: begin
			case (t)
				6'd0: begin
					ma <= d0; mb <= K_CWET1;
					we <= 1;
					wa <= CHO_BASE + 15'(cin);
					wd <= hold24(rnd(54'(c_in), 8));
					cin  <= cin == CHO_SIZE - 1'd1 ? 12'd0 : cin + 1'd1;
					if (ccnt != CHO_SIZE) ccnt <= ccnt + 1'd1;
				end
				6'd1: begin ma <= d1; mb <= K_CWET2; end
				6'd2: begin ma <= d1; mb <= K_CWET1; q0 <= mp; end
				6'd3: begin ma <= d0; mb <= K_CWET2; wet_l <= rw_l + 40'(rnd(q0 + mp, 15) <<< 8); end
				6'd4: q0 <= mp;
				default: begin
					wet_r <= rw_r + 40'(rnd(q0 + mp, 15) <<< 8);
					done  <= 1;
					s     <= S_IDLE;
				end
			endcase
		end

		default: s <= S_IDLE;
	endcase

	if (clr) begin
		s      <= S_IDLE;
		cv     <= 0;
		av     <= 0;
		filled <= 0;
		for (i = 0; i < 24; i = i + 1) ptr[i] <= 0;
		for (i = 0; i < 16; i = i + 1) store[i] <= 0;
		// as FluidSynth's: the centre the depth and one sample behind the
		// input, the LFOs a third of a turn apart, stepped at the first frame
		cin    <= 0;
		ccnt   <= 0;
		center <= CHO_SIZE - 1'd1 - 12'(K_DEPTH);
		crate  <= CHO_RATE[2:0];
		phase[0] <= 48'h0;
		phase[1] <= 48'h555555555555;
		phase[2] <= 48'hAAAAAAAAAAAB;
		for (i = 0; i < 3; i = i + 1) begin
			cout[i]  <= 0;
			frac[i]  <= 0;
			clast[i] <= 0;
		end
	end
end

endmodule
