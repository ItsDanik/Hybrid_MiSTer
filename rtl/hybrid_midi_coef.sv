//============================================================================
//
//  MiSTer hybrid core host - MIDI synthesizer, the filter's coefficients
//
//  For a voice's cutoff (cents, Q.16) and Q (cB, 0..960), as the C model's
//  coefs() (tools/midisynth/midisynth.c) and FluidSynth's
//  fluid_iir_filter_calculate_coefficients():
//    alpha = sin * inv2q >> 24, a0 = 2^48 / (2^24 + alpha),
//    a1 = -(cos * a0 >> 23), a2 = (2^24 - alpha) * a0 >> 24,
//    b1 = ((2^24 - cos) * a0 >> 24) * isq >> 24, b02 = b1 >> 1     (Q2.24)
//  The jobs come from the control engine (a block's cutoff) and the sample
//  engine (a cutoff that moves, every frame), wait in a FIFO and leave in
//  that order; a voice's coefficients are written when they are worked out.
//  Three steps, each with a job of its own: the tables and alpha, the
//  division (two bits a clock, radix 4, 13 clocks), the four products. A job every
//  13 clocks.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//============================================================================

module hybrid_midi_coef
#(
	parameter TABLES = "../hybrid/rtl/midi/"
)
(
	input             clk,
	input             clr,

	input             job,          // a job: the voice, its cutoff and Q
	input       [6:0] job_voice,
	input      [31:0] job_fres,     // cents Q.16, within the limits
	input       [9:0] job_q,        // 0..960
	output            full,

	output reg        wr = 0,       // the coefficients of wr_voice
	output reg  [6:0] wr_voice,
	output reg [26:0] wr_b02,
	output reg [26:0] wr_b1,
	output reg [26:0] wr_a1,
	output reg [26:0] wr_a2,

	output            idle          // nothing waits or is worked on
);

localparam signed [15:0] FC_MIN = 16'sd1500;

// the jobs waiting: {voice, fres, q}
wire [48:0] fq;
wire        fq_valid;
reg         fq_take = 0;
wire  [8:0] fq_level;

hybrid_midi_fifo #(.W(49), .A(8)) fifo
(
	.clk(clk), .clr(clr),
	.we(job), .wdata({job_voice, job_fres, job_q}), .full(full),
	.re(fq_take), .rdata(fq), .rvalid(fq_valid), .level(fq_level)
);

// step 1: the tables, alpha
reg   [1:0] s1 = 0;             // 0 free, 1 tables read, 2 their values there, 3 alpha
reg   [6:0] s1_voice;
reg  [13:0] s1_i = 0;
reg   [9:0] s1_q = 0;
wire [23:0] t_sin, t_inv2q;
wire [24:0] t_cos, t_isq;
reg  [24:0] s1_cos, s1_isq;
reg  [47:0] s1_prod;
reg         s1_done = 0;        // alpha is there for step 2

hybrid_midi_rom #(.W(24), .D(12001), .A(14), .FILE({TABLES, "ms_sin.hex"}))   rom_sin   (.clk(clk), .addr(s1_i), .q(t_sin));
hybrid_midi_rom #(.W(25), .D(12001), .A(14), .FILE({TABLES, "ms_cos.hex"}))   rom_cos   (.clk(clk), .addr(s1_i), .q(t_cos));
hybrid_midi_rom #(.W(24), .D(961),   .A(10), .FILE({TABLES, "ms_inv2q.hex"})) rom_inv2q (.clk(clk), .addr(s1_q), .q(t_inv2q));
hybrid_midi_rom #(.W(25), .D(961),   .A(10), .FILE({TABLES, "ms_isq.hex"}))   rom_isq   (.clk(clk), .addr(s1_q), .q(t_isq));

// the cutoff in whole cents, rounded down; 1500 and below take the first entry
wire signed [15:0] fc_cents = fq[41:26];
wire signed [15:0] fc_i     = fc_cents - FC_MIN;

// step 2: the division, a0 = 2^48 / (2^24 + alpha), restoring, from the
// quotient's bit 24 (2^24 <= 2^24 + alpha < 2^25)
reg         s2 = 0, s2_done = 0;
reg   [6:0] s2_voice;
reg  [24:0] s2_cos, s2_isq;
reg  [23:0] s2_alpha;
reg  [26:0] s2_d;
reg  [26:0] s2_r;               // the remainder, before the next bit's compare
reg  [24:0] s2_a0;
reg   [4:0] s2_bit;             // the next quotient bit

reg  [28:0] s2_d3;              // 3 d
// two bits a clock: k = 2r / d (r < 2d), the three compares side by side;
// the remainder on is 2 (2r - k d)
wire [28:0] r2  = {1'b0, s2_r, 1'b0};
wire [28:0] r2a = r2 - {2'b0, s2_d};
wire [28:0] r2b = r2 - {1'b0, s2_d, 1'b0};
wire [28:0] r2c = r2 - s2_d3;
wire  [1:0] k   = !r2c[28] ? 2'd3 : !r2b[28] ? 2'd2 : !r2a[28] ? 2'd1 : 2'd0;
wire [28:0] rk  = k == 2'd3 ? r2c : k == 2'd2 ? r2b : k == 2'd1 ? r2a : r2;
wire        q1  = k[1];
wire        q2  = k[0];
wire [26:0] r2s = {rk[25:0], 1'b0};

// step 3: the products, one a clock into the multiplier, three clocks to
// come out: m_q of the product tagged m_tag3
reg   [2:0] s3 = 0;             // 0 free, 1..3 the first three in, 4 waits for t, 5 waits for b1
reg   [6:0] s3_voice;
reg  [24:0] s3_cos, s3_isq;
reg  [23:0] s3_alpha;
reg  [24:0] s3_a0;
reg  signed [26:0] m_a = 0, m_b = 0;
reg  signed [53:0] m_p = 0, m_q = 0;
reg   [2:0] m_tag = 0, m_tag2 = 0, m_tag3 = 0;
reg  signed [26:0] s3_a1, s3_a2;

always @(posedge clk) begin
	m_p <= m_a * m_b;
	m_q <= m_p;
end

assign idle = !fq_valid && fq_level == 0 && s1 == 0 && !s1_done && !s2 && !s2_done && s3 == 0;

always @(posedge clk) begin
	fq_take <= 0;
	wr      <= 0;

	// step 1
	case (s1)
		2'd0: if (fq_valid && !fq_take && !s1_done) begin
			fq_take  <= 1;
			s1_voice <= fq[48:42];
			s1_i     <= fc_cents <= FC_MIN ? 14'd0 : fc_i[13:0];
			s1_q     <= fq[9:0];
			s1       <= 2'd1;
		end
		2'd1: s1 <= 2'd2;
		2'd2: begin
			s1_prod <= t_sin * t_inv2q;
			s1_cos  <= t_cos;
			s1_isq  <= t_isq;
			s1      <= 2'd3;
		end
		2'd3: begin
			s1      <= 2'd0;
			s1_done <= 1;
		end
	endcase

	// step 2, when it is free
	if (s1_done && !s2 && !s2_done) begin
		s1_done  <= 0;
		s2       <= 1;
		s2_voice <= s1_voice;
		s2_cos   <= s1_cos;
		s2_isq   <= s1_isq;
		s2_alpha <= s1_prod[47:24];
		s2_d     <= (27'd1 << 24) + {3'd0, s1_prod[47:24]};
		s2_d3    <= 3 * ((29'd1 << 24) + {5'd0, s1_prod[47:24]});
		s2_r     <= 27'd1 << 24;
		s2_bit   <= 5'd24;
	end
	if (s2) begin
		s2_a0[s2_bit] <= q1;
		if (s2_bit == 5'd0) begin
			s2      <= 0;
			s2_done <= 1;
		end else begin
			s2_a0[s2_bit - 1'd1] <= q2;
			s2_r   <= r2s;
			s2_bit <= s2_bit - 5'd2;
		end
	end

	// step 3, when it is free
	if (s2_done && s3 == 0) begin
		s2_done  <= 0;
		s3       <= 3'd1;
		s3_voice <= s2_voice;
		s3_cos   <= s2_cos;
		s3_isq   <= s2_isq;
		s3_alpha <= s2_alpha;
		s3_a0    <= s2_a0;
	end
	m_tag  <= 0;
	m_tag2 <= m_tag;
	m_tag3 <= m_tag2;
	case (s3)
		3'd1: begin   // cos * a0
			m_a   <= 27'(signed'(s3_cos));
			m_b   <= {2'b0, s3_a0};
			m_tag <= 3'd1;
			s3    <= 3'd2;
		end
		3'd2: begin   // (2^24 - alpha) * a0
			m_a   <= (27'sd1 <<< 24) - {3'b0, s3_alpha};
			m_b   <= {2'b0, s3_a0};
			m_tag <= 3'd2;
			s3    <= 3'd3;
		end
		3'd3: begin   // (2^24 - cos) * a0
			m_a   <= (27'sd1 <<< 24) - 27'(signed'(s3_cos));
			m_b   <= {2'b0, s3_a0};
			m_tag <= 3'd3;
			s3    <= 3'd4;
		end
		default: ;
	endcase
	case (m_tag3)
		3'd1: s3_a1 <= -(27'(m_q >>> 23));
		3'd2: s3_a2 <= 27'(m_q >>> 24);
		3'd3: begin   // t = that >> 24, then t * isq
			m_a   <= 27'(m_q >>> 24);
			m_b   <= {2'b0, s3_isq};
			m_tag <= 3'd4;
			s3    <= 3'd5;
		end
		3'd4: begin
			wr       <= 1;
			wr_voice <= s3_voice;
			wr_a1    <= s3_a1;
			wr_a2    <= s3_a2;
			wr_b1    <= 27'(m_q >>> 24);
			wr_b02   <= 27'(m_q >>> 25);
			s3       <= 3'd0;
		end
		default: ;
	endcase

	if (clr) begin
		s1 <= 0;
		s1_done <= 0;
		s2 <= 0;
		s2_done <= 0;
		s3 <= 0;
		m_tag <= 0;
		m_tag2 <= 0;
		m_tag3 <= 0;
		fq_take <= 0;
	end
end

endmodule
