`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 03/11/2026 02:39:14 PM
// Design Name: 
// Module Name: my_state
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////


module my_state(
    input wire clock,
    input wire reset,
    input [1:0] control,
    input [31:0] value,
    output reg [31:0] sum,
    output reg [31:0] carry
    );

    reg [63:0] accumulator;
    reg [1:0]  control_prev;

always @(posedge clock)
begin
    if (!reset) begin
        accumulator  <= 64'd0;
        control_prev <= 2'd0;
        sum          <= 32'd0;
        carry        <= 32'd0;
    end else begin
        control_prev <= control;

        if (control_prev == 2'd0 && control == 2'd1) begin
            accumulator <= accumulator + {32'd0, value};
        end else if (control_prev == 2'd0 && control == 2'd2) begin
            accumulator <= 64'd0;
        end

        sum   <= accumulator[31:0];
        carry <= accumulator[63:32];
    end
end

endmodule
