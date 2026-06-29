`ifndef GW5A_SRAM_INIT_MEMB0
`define GW5A_SRAM_INIT_MEMB0 "build/fw/kws_xip_rt_boot_0.memb"
`endif
`ifndef GW5A_SRAM_INIT_MEMB1
`define GW5A_SRAM_INIT_MEMB1 "build/fw/kws_xip_rt_boot_1.memb"
`endif
`ifndef GW5A_SRAM_INIT_MEMB2
`define GW5A_SRAM_INIT_MEMB2 "build/fw/kws_xip_rt_boot_2.memb"
`endif
`ifndef GW5A_SRAM_INIT_MEMB3
`define GW5A_SRAM_INIT_MEMB3 "build/fw/kws_xip_rt_boot_3.memb"
`endif

module Top (
    input  wire       clk,
    input  wire       reset,
    input  wire       debug_reset,

    input  wire       jtag_tms,
    input  wire       jtag_tdi,
    output wire       jtag_tdo,
    input  wire       jtag_tck,

    output wire       uart_txd,
    input  wire       uart_rxd,

    output wire       qspi_cs_n,
    output wire       qspi_sclk,
    inout  wire [3:0] qspi_dp,

    inout  wire [7:0] gpio,

    output wire       mic_sck,
    input  wire       mic_sd,
    output wire       mic_ws
);

  reg [1:0] reset_sync = 2'b00;
  reg [1:0] debug_reset_sync = 2'b00;

  always @(posedge clk or posedge reset) begin
    if (reset) begin
      reset_sync <= 2'b00;
    end else begin
      reset_sync <= {reset_sync[0], 1'b1};
    end
  end

  always @(posedge clk or posedge debug_reset) begin
    if (debug_reset) begin
      debug_reset_sync <= 2'b00;
    end else begin
      debug_reset_sync <= {debug_reset_sync[0], 1'b1};
    end
  end

  VenusCoreRVTop u_soc (
      .jtag_tms      (jtag_tms),
      .jtag_tdi      (jtag_tdi),
      .jtag_tdo      (jtag_tdo),
      .jtag_tck      (jtag_tck),
      .debug_reset_n (debug_reset_sync[1]),

      .gpio          (gpio),
      .uart_txd      (uart_txd),
      .uart_rxd      (uart_rxd),

      .qspi_cs_n     (qspi_cs_n),
      .qspi_sclk     (qspi_sclk),
      .qspi_dp       (qspi_dp),

      .mic_sck       (mic_sck),
      .mic_ws        (mic_ws),
      .mic_sd        (mic_sd),

      .clk           (clk),
      .resetn        (reset_sync[1])
  );

endmodule
