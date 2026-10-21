// The MIDI synthesizer's control engine and its filter's coefficients, for
// sim/midi_unit.cpp: each against the C model, update by update
module tb_midi_unit
(
	input             clk,

	input             go,
	input      [63:0] w1, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15,
	input       [2:0] i_sec0, i_sec1,
	input      [15:0] i_cnt0, i_cnt1,
	input      [39:0] i_x0, i_x1,
	input      [32:0] i_lfo0, i_lfo1,
	input             i_down0, i_down1,
	input      [15:0] i_blocks,
	input       [1:0] i_release,
	input             i_killed, i_first, i_looped,
	input      [28:0] i_amp,
	input      [95:0] i_send,
	input      [31:0] i_fres,
	output            done,
	output            o_off, o_released,
	output      [2:0] o_sec0, o_sec1,
	output     [15:0] o_cnt0, o_cnt1,
	output     [39:0] o_x0, o_x1,
	output     [32:0] o_lfo0, o_lfo1,
	output            o_down0, o_down1,
	output     [15:0] o_blocks,
	output            o_killed,
	output     [39:0] o_incr,
	output     [29:0] o_amp_step,
	output     [95:0] o_send_step,
	output     [31:0] o_fres, o_fres_incr,
	output      [8:0] o_fres_n,
	output            o_silent,
	output      [9:0] o_q,

	input             job,
	input       [6:0] job_voice,
	input      [31:0] job_fres,
	input       [9:0] job_q,
	output            full,
	output            wr,
	output      [6:0] wr_voice,
	output     [26:0] wr_b02, wr_b1, wr_a1, wr_a2,
	output            idle
);

hybrid_midi_ctrl #(.TABLES("rtl/midi/")) ctrl
(
	.clk(clk), .go(go),
	.w1(w1), .w3(w3), .w4(w4), .w5(w5), .w6(w6), .w7(w7), .w8(w8), .w9(w9), .w10(w10), .w11(w11),
	.w12(w12), .w13(w13), .w14(w14), .w15(w15),
	.i_sec0(i_sec0), .i_sec1(i_sec1), .i_cnt0(i_cnt0), .i_cnt1(i_cnt1), .i_x0(i_x0), .i_x1(i_x1),
	.i_lfo0(i_lfo0), .i_lfo1(i_lfo1), .i_down0(i_down0), .i_down1(i_down1), .i_blocks(i_blocks),
	.i_release(i_release), .i_killed(i_killed), .i_first(i_first), .i_looped(i_looped),
	.i_amp(i_amp), .i_send(i_send), .i_fres(i_fres),
	.done(done), .o_off(o_off), .o_released(o_released), .o_sec0(o_sec0), .o_sec1(o_sec1),
	.o_cnt0(o_cnt0), .o_cnt1(o_cnt1), .o_x0(o_x0), .o_x1(o_x1), .o_lfo0(o_lfo0), .o_lfo1(o_lfo1),
	.o_down0(o_down0), .o_down1(o_down1), .o_blocks(o_blocks), .o_killed(o_killed), .o_incr(o_incr),
	.o_amp_step(o_amp_step), .o_send_step(o_send_step), .o_fres(o_fres), .o_fres_incr(o_fres_incr),
	.o_fres_n(o_fres_n), .o_silent(o_silent), .o_q(o_q)
);

hybrid_midi_coef #(.TABLES("rtl/midi/")) coef
(
	.clk(clk), .clr(1'b0),
	.job(job), .job_voice(job_voice), .job_fres(job_fres), .job_q(job_q), .full(full),
	.wr(wr), .wr_voice(wr_voice), .wr_b02(wr_b02), .wr_b1(wr_b1), .wr_a1(wr_a1), .wr_a2(wr_a2),
	.idle(idle)
);

endmodule
