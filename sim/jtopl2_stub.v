// A stand-in for the FM chip in the hybrid_host testbench, which only looks at
// the writes on their way to it: the chip itself (tb_fm.sv) is far too slow to
// simulate for the hundreds of fields that testbench runs.
module jtopl2(
    input                  rst,
    input                  clk,
    input                  cen,
    input           [ 7:0] din,
    input                  addr,
    input                  cs_n,
    input                  wr_n,
    output          [ 7:0] dout,
    output                 irq_n,
    output  signed  [15:0] snd,
    output                 sample
);
assign dout = 8'd0;
assign irq_n = 1'b1;
assign snd = 16'sd0;
assign sample = 1'b0;
endmodule
