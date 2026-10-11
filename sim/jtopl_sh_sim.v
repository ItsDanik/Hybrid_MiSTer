// For the testbenches only, in the place of rtl/jtopl/jtopl_sh.v: the same
// shift register, starting with zeroes. The real one starts with whatever
// is there, which the chip washes out by itself; a simulator that knows
// "unknown" keeps that for ever.
module jtopl_sh #(parameter width=5, stages=24 )
(
	input 				clk,
	input				cen,
	input	[width-1:0]	din,
   	output	[width-1:0]	drop
);

reg [stages-1:0] bits[width-1:0];

genvar i;
integer k;
initial for (k = 0; k < width; k = k + 1) bits[k] = {stages{1'b0}};
generate
	for (i=0; i < width; i=i+1) begin: bit_shifter
		always @(posedge clk) if(cen) begin
			bits[i] <= {bits[i][stages-2:0], din[i]};
		end
		assign drop[i] = bits[i][stages-1];
	end
endgenerate

endmodule
