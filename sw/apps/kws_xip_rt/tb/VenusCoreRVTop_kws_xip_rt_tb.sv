`timescale 1ns/1ps

// -----------------------------------------------------------------------------
// Minimal W25Q64/XT25F64-like QSPI flash model for QuadSpiXipAhb
// -----------------------------------------------------------------------------
module W25Q64_XIP_stub #(
  parameter int FLASH_SIZE_BYTES = 8 * 1024 * 1024
) (
  input logic CSn,
  input logic CLK,
  inout      DIO0,
  inout      DIO1,
  inout      DIO2,
  inout      DIO3
);
  integer plusarg_ret;
  typedef enum logic [3:0] {
    ST_IDLE = 4'd0,
    ST_CMD  = 4'd1,

    ST_SR_OUT = 4'd2,
    ST_WRSR   = 4'd3,
    ST_WRAP   = 4'd4,

    ST_EB_ADDR  = 4'd5,
    ST_EB_MODE  = 4'd6,
    ST_EB_DUMMY = 4'd7,

    ST_E7_ADDR  = 4'd8,
    ST_E7_DUMMY = 4'd9,

    ST_QUAD_OUT = 4'd10
  } st_e;

  st_e st;

  wire [3:0] dq_in = {DIO3, DIO2, DIO1, DIO0};
  logic [3:0] dq_out;
  logic [3:0] dq_oe;

  assign DIO0 = dq_oe[0] ? dq_out[0] : 1'bz;
  assign DIO1 = dq_oe[1] ? dq_out[1] : 1'bz;
  assign DIO2 = dq_oe[2] ? dq_out[2] : 1'bz;
  assign DIO3 = dq_oe[3] ? dq_out[3] : 1'bz;

  logic [7:0] sr1, sr2;
  logic       wel;

  logic [7:0] cmd_shift;
  logic [4:0] bit_cnt;

  logic [7:0] sr_out_byte;
  logic [3:0] sr_out_idx;

  logic [7:0] wrsr_sr1;
  logic [7:0] wrsr_sr2;
  logic       wrsr_phase;

  logic [1:0] wrap_byte_cnt;

  logic [23:0] addr_shift;
  logic [23:0] rd_addr;
  logic [2:0]  e7_addr_nib_cnt;
  logic [2:0]  dummy_left;
  logic [1:0]  mode_left;
  logic [7:0]  cur_byte;
  logic        nib_phase;
  integer      quad_dbg_left;
  bit          dbg_flash = 0;

  byte unsigned mem [0:FLASH_SIZE_BYTES-1];

  function automatic byte unsigned mem_read(input logic [23:0] a);
    if (a < FLASH_SIZE_BYTES) mem_read = mem[a];
    else mem_read = 8'hFF;
  endfunction

  initial begin
    string flash_hex;
    integer fd;
    int unsigned flash_load_offset;
    if (!$value$plusargs("FLASH_HEX=%s", flash_hex)) flash_hex = "bin/led_xip_flash.hex";
    flash_load_offset = 32'h0040_0000;
    plusarg_ret = $value$plusargs("FLASH_LOAD_OFFSET=%h", flash_load_offset);
    if (flash_load_offset >= FLASH_SIZE_BYTES) begin
      $fatal(1, "[KWS_XIP_RT_TB] FLASH_LOAD_OFFSET out of range: 0x%0x >= FLASH_SIZE_BYTES=%0d",
             flash_load_offset, FLASH_SIZE_BYTES);
    end

    fd = $fopen(flash_hex, "r");
    if (fd == 0) $fatal(1, "[KWS_XIP_RT_TB] Cannot open FLASH_HEX: %s", flash_hex);
    $fclose(fd);

    $display("[KWS_XIP_RT_TB] Flash preload: %s (load_offset=0x%0x)", flash_hex, flash_load_offset);
    $readmemh(flash_hex, mem, flash_load_offset);
  end

  initial begin
    plusarg_ret = $value$plusargs("DBG_FLASH=%d", dbg_flash);
  end

  event ev_busy_start;
  initial begin
    forever begin
      @ev_busy_start;
      sr1[0] <= 1'b1;
      #200;
      sr1[0] <= 1'b0;
    end
  end

  always @(posedge CSn) begin
    st              <= ST_IDLE;
    bit_cnt         <= '0;
    cmd_shift       <= '0;
    dq_oe           <= 4'b0000;
    dq_out          <= 4'h0;
    sr_out_idx      <= '0;
    addr_shift      <= '0;
    e7_addr_nib_cnt <= '0;
    dummy_left      <= '0;
    mode_left       <= '0;
    nib_phase       <= 1'b0;
    wrsr_phase      <= 1'b0;
    wrap_byte_cnt   <= '0;
  end

  always @(negedge CSn) begin
    st              <= ST_CMD;
    bit_cnt         <= 0;
    cmd_shift       <= 8'h00;
    dq_oe           <= 4'b0000;
    dq_out          <= 4'h0;
    sr_out_idx      <= 0;
    addr_shift      <= 24'h0;
    e7_addr_nib_cnt <= 0;
    dummy_left      <= 0;
    mode_left       <= 0;
    nib_phase       <= 1'b0;
    wrsr_phase      <= 1'b0;
    wrsr_sr1        <= 8'h00;
    wrsr_sr2        <= 8'h00;
    wrap_byte_cnt   <= 0;
  end

  always @(posedge CLK) begin
    if (!CSn) begin
      case (st)
        ST_CMD: begin
          cmd_shift <= {cmd_shift[6:0], dq_in[0]};
          bit_cnt   <= bit_cnt + 1;
          if (bit_cnt == 5'd7) begin
            logic [7:0] cmd;
            cmd = {cmd_shift[6:0], dq_in[0]};
            bit_cnt <= 0;
            case (cmd)
              8'h05: begin
                sr_out_byte <= sr1;
                sr_out_idx  <= 0;
                st          <= ST_SR_OUT;
              end
              8'h35: begin
                sr_out_byte <= sr2;
                sr_out_idx  <= 0;
                st          <= ST_SR_OUT;
                if (dbg_flash) $display("[KWS_XIP_RT_TB][FLASH] RDSR2: SR2=0x%02x (QE=%0d) @%0t", sr2, sr2[1], $time);
              end
              8'h06, 8'h50: begin
                wel <= 1'b1;
                st  <= ST_IDLE;
                if (dbg_flash) $display("[KWS_XIP_RT_TB][FLASH] WREN cmd=0x%02x @%0t", cmd, $time);
              end
              8'h01: begin
                st         <= ST_WRSR;
                wrsr_phase <= 1'b0;
                wrsr_sr1   <= 8'h00;
                wrsr_sr2   <= 8'h00;
                bit_cnt    <= 0;
                if (dbg_flash) $display("[KWS_XIP_RT_TB][FLASH] WRSR start @%0t (wel=%0d)", $time, wel);
              end
              8'h77: begin
                st            <= ST_WRAP;
                wrap_byte_cnt <= 0;
                bit_cnt       <= 0;
              end
              8'h66, 8'h99: begin
                st <= ST_IDLE;
              end
              8'hE7: begin
                st              <= ST_E7_ADDR;
                e7_addr_nib_cnt <= 0;
                addr_shift      <= 24'h0;
              end
              8'hEB: begin
                st              <= ST_EB_ADDR;
                e7_addr_nib_cnt <= 0;
                addr_shift      <= 24'h0;
                mode_left       <= 0;
              end
              default: begin
                st <= ST_IDLE;
              end
            endcase
          end
        end

        ST_WRSR: begin
          logic [7:0] next_byte;
          if (!wrsr_phase) begin
            next_byte = {wrsr_sr1[6:0], dq_in[0]};
            wrsr_sr1 <= next_byte;
          end else begin
            next_byte = {wrsr_sr2[6:0], dq_in[0]};
            wrsr_sr2 <= next_byte;
          end

          bit_cnt <= bit_cnt + 1;
          if (bit_cnt == 5'd7) begin
            bit_cnt <= 0;
            if (!wrsr_phase) begin
              wrsr_phase <= 1'b1;
            end else begin
              if (wel) begin
                sr1 <= wrsr_sr1;
                sr2 <= next_byte;
                wel <= 1'b0;
                -> ev_busy_start;
                if (dbg_flash) $display("[KWS_XIP_RT_TB][FLASH] WRSR commit: SR1=0x%02x SR2=0x%02x (QE=%0d) @%0t",
                                        wrsr_sr1, next_byte, next_byte[1], $time);
              end
              st <= ST_IDLE;
            end
          end
        end

        ST_WRAP: begin
          bit_cnt <= bit_cnt + 1;
          if (bit_cnt == 5'd7) begin
            bit_cnt <= 0;
            wrap_byte_cnt <= wrap_byte_cnt + 1;
            if (wrap_byte_cnt == 2'd3) begin
              st <= ST_IDLE;
            end
          end
        end

        ST_E7_ADDR: begin
          addr_shift <= {addr_shift[19:0], dq_in};
          e7_addr_nib_cnt <= e7_addr_nib_cnt + 1;
          if (e7_addr_nib_cnt == 3'd5) begin
            rd_addr <= {addr_shift[19:0], dq_in};
            dummy_left <= 2;
            st <= ST_E7_DUMMY;
          end
        end

        ST_E7_DUMMY: begin
          if (dummy_left != 0) begin
            dummy_left <= dummy_left - 1;
          end
          if (dummy_left == 2'd1) begin
            st <= ST_QUAD_OUT;
            nib_phase <= 1'b0;
            cur_byte <= mem_read(rd_addr);
            if (dbg_flash && (quad_dbg_left > 0)) begin
              $display("[KWS_XIP_RT_TB][FLASH] QUAD_OUT start addr=0x%06x byte=0x%02x @%0t",
                       rd_addr, mem_read(rd_addr), $time);
            end
          end
        end

        ST_EB_ADDR: begin
          addr_shift <= {addr_shift[19:0], dq_in};
          e7_addr_nib_cnt <= e7_addr_nib_cnt + 1;
          if (e7_addr_nib_cnt == 3'd5) begin
            rd_addr <= {addr_shift[19:0], dq_in};
            mode_left <= 2'd2;
            st <= ST_EB_MODE;
          end
        end

        ST_EB_MODE: begin
          if (mode_left != 0) begin
            mode_left <= mode_left - 1;
          end
          if (mode_left == 2'd1) begin
            dummy_left <= 3'd4;
            st <= ST_EB_DUMMY;
          end
        end

        ST_EB_DUMMY: begin
          if (dummy_left != 0) begin
            dummy_left <= dummy_left - 1;
          end
          if (dummy_left == 2'd1) begin
            st <= ST_QUAD_OUT;
            nib_phase <= 1'b0;
            cur_byte <= mem_read(rd_addr);
            if (dbg_flash && (quad_dbg_left > 0)) begin
              $display("[KWS_XIP_RT_TB][FLASH] QUAD_OUT start addr=0x%06x byte=0x%02x @%0t",
                       rd_addr, mem_read(rd_addr), $time);
            end
          end
        end

        default: ;
      endcase
    end
  end

  // Drive outputs on falling edge (Mode0)
  always @(negedge CLK) begin
    if (!CSn) begin
      case (st)
        ST_SR_OUT: begin
          dq_oe     <= 4'b0010;
          dq_out    <= 4'h0;
          dq_out[1] <= sr_out_byte[7 - sr_out_idx];
          if (sr_out_idx == 4'd7) begin
            st    <= ST_IDLE;
          end
          sr_out_idx <= sr_out_idx + 1'b1;
        end

        ST_QUAD_OUT: begin
          dq_oe <= 4'b1111;
          if (!nib_phase) begin
            dq_out    <= {cur_byte[7], cur_byte[6], cur_byte[5], cur_byte[4]};
            nib_phase <= 1'b1;
          end else begin
            dq_out    <= {cur_byte[3], cur_byte[2], cur_byte[1], cur_byte[0]};
            nib_phase <= 1'b0;
            rd_addr   <= rd_addr + 1'b1;
            cur_byte  <= mem_read(rd_addr + 1'b1);
            if (dbg_flash && (quad_dbg_left > 0)) begin
              quad_dbg_left <= quad_dbg_left - 1;
              $display("[KWS_XIP_RT_TB][FLASH] QUAD_OUT byte addr=0x%06x byte=0x%02x @%0t",
                       rd_addr, cur_byte, $time);
            end
          end
        end

        default: begin
          dq_oe  <= 4'b0000;
          dq_out <= 4'h0;
        end
      endcase
    end else begin
      dq_oe  <= 4'b0000;
      dq_out <= 4'h0;
    end
  end

  initial begin
    st        = ST_IDLE;
    sr1       = 8'h00;
    sr2       = 8'h00; // QE=0 initially
    wel       = 1'b0;
    dq_oe     = 4'b0000;
    dq_out    = 4'h0;
    bit_cnt   = 0;
    cmd_shift = 0;
    quad_dbg_left = 16;
  end
endmodule

module VenusCoreRVTop_kws_xip_rt_tb;
  integer plusarg_ret;
  localparam int KWS_CLIP_SAMPLES = 16000;
  localparam int PCM_MAX_SAMPLES = 20000;
  localparam integer FLASH_BASE = 32'h0010_0000;
  localparam integer FLASH_SIZE_BYTES = 8 * 1024 * 1024;

  reg clk = 0;
  reg resetn = 0;
  reg jtag_tms = 0;
  reg jtag_tdi = 0;
  reg jtag_tck = 0;
  reg debug_reset_n = 0;
  wire jtag_tdo;

  always #10 clk = ~clk; // 50 MHz, matches the GW5A implementation

  initial begin
    resetn = 0;
    #100;
    resetn = 1;
  end

  always @(posedge clk) begin
    if (!resetn) debug_reset_n <= 1'b0;
    else debug_reset_n <= 1'b1;
  end

  wire uart_txd;
  reg  uart_rxd = 1'b1;

  wire qspi_cs_n;
  wire qspi_sclk;
  tri  [3:0] qspi_dp;
  wire mic_sck;
  wire mic_ws;
  reg  mic_sd = 1'b0;
  tri  [7:0] gpio;
  wire [7:0] gpio_status = dut.apb_periph_gpio_write;
  integer fast_qspi_ahb = 0;
  reg [31:0] fast_qspi_dphase_addr_reg = FLASH_BASE;
  reg [31:0] fast_qspi_hrdata_reg = 32'h0;

  VenusCoreRVTop dut (
`ifdef TINYML_SOC_SIM_FAST_QSPI
    .sim_qspi_hrdata   (fast_qspi_hrdata_reg),
    .sim_qspi_hreadyout(1'b1),
    .sim_qspi_hresp    (1'b0),
`endif
    .jtag_tms        (jtag_tms),
    .jtag_tdi        (jtag_tdi),
    .jtag_tdo        (jtag_tdo),
    .jtag_tck        (jtag_tck),
    .debug_reset_n   (debug_reset_n),
    .uart_txd        (uart_txd),
    .uart_rxd        (uart_rxd),
    .qspi_cs_n       (qspi_cs_n),
    .qspi_sclk       (qspi_sclk),
    .mic_sck         (mic_sck),
    .mic_ws          (mic_ws),
    .mic_sd          (mic_sd),
    .gpio            (gpio),
    .qspi_dp         (qspi_dp),
    .clk             (clk),
    .resetn          (resetn)
  );

  // -----------------------------------------------------------------------------
  // CPU stall watchdog
  // -----------------------------------------------------------------------------
  // Purpose: detect "CPU stuck" cases earlier than the global TB timeout.
  // Heuristic: if lastStagePc stays constant while lastStageIsValid==1 for
  // CPU_WDOG_CYCLES cycles -> fatal.
  //
  // Notes:
  // - This does NOT catch tight loops where PC alternates between a few addresses.
  // - Default threshold is intentionally large to avoid false positives.
  bit cpu_wdog_en = 1'b1;
  longint unsigned cpu_wdog_start_cycles = 200000;
  longint unsigned cpu_wdog_cycles = 100000000;
  bit cpu_wdog_dbg = 1'b0;
  longint unsigned cpu_wdog_dbg_period = 0;
  initial begin
    plusarg_ret = $value$plusargs("CPU_WDOG_EN=%d", cpu_wdog_en);
    plusarg_ret = $value$plusargs("CPU_WDOG_START=%d", cpu_wdog_start_cycles);
    plusarg_ret = $value$plusargs("CPU_WDOG_CYCLES=%d", cpu_wdog_cycles);
    plusarg_ret = $value$plusargs("CPU_WDOG_DBG=%d", cpu_wdog_dbg);
    plusarg_ret = $value$plusargs("CPU_WDOG_DBG_PERIOD=%d", cpu_wdog_dbg_period);
  end

  longint unsigned cpu_cycle_cnt = 0;
  logic [31:0] cpu_pc_last = 32'h0;
  bit cpu_pc_last_valid = 1'b0;
  longint unsigned cpu_pc_same_cycles = 0;
  always @(posedge clk) begin
    if (!resetn) begin
      cpu_cycle_cnt <= 0;
      cpu_pc_last <= 32'h0;
      cpu_pc_last_valid <= 1'b0;
      cpu_pc_same_cycles <= 0;
    end else begin
      cpu_cycle_cnt <= cpu_cycle_cnt + 1;

      if (cpu_wdog_en && (cpu_cycle_cnt >= cpu_wdog_start_cycles)) begin
        logic [31:0] pc;
        logic vld;
        logic [31:0] fetch_pc;
        logic [31:0] pc_mon;
        pc = dut.cpu_ahb.cpu.lastStagePc;
        vld = dut.cpu_ahb.cpu.lastStageIsValid;
        fetch_pc = dut.cpu_ahb.cpu.IBusCachedPlugin_fetchPc_pcReg;
        pc_mon = vld ? pc : fetch_pc;

        if (!cpu_pc_last_valid) begin
          cpu_pc_last <= pc_mon;
          cpu_pc_last_valid <= 1'b1;
          cpu_pc_same_cycles <= 0;
        end else if (pc_mon === cpu_pc_last) begin
          cpu_pc_same_cycles <= cpu_pc_same_cycles + 1;
          if (cpu_pc_same_cycles >= cpu_wdog_cycles) begin
            $display("[KWS_XIP_RT_TB] CPU STALL: pc stuck at 0x%08x (v=%0d fetch=0x%08x) for %0d cycles (sim=%0t)",
                     pc_mon, vld, fetch_pc, cpu_pc_same_cycles, $time);
            $display("[KWS_XIP_RT_TB] CPU STALL (qspi_req_cnt=%0d qspi_rd_cnt=%0d sram_rd_cnt=%0d sram_wr_cnt=%0d apb_req=%0d apb_wr=%0d)",
                     qspi_req_cnt, qspi_rd_cnt, sram_req_cnt, sram_wr_cnt, apb_ahb_req_cnt, apb_ahb_wr_cnt);
            $display("[KWS_XIP_RT_TB] CPU STALL (m_cpu: htrans=0x%0x hwrite=%0d haddr=0x%08x hready=%0d)",
                     dut.m_cpu_HTRANS, dut.m_cpu_HWRITE, dut.m_cpu_HADDR, dut.m_cpu_HREADY);
            $fatal(1);
          end
        end else begin
          cpu_pc_last <= pc_mon;
          cpu_pc_same_cycles <= 0;
        end

        if (cpu_wdog_dbg && (cpu_wdog_dbg_period != 0) && ((cpu_cycle_cnt % cpu_wdog_dbg_period) == 0)) begin
          $display("[KWS_XIP_RT_TB] CPU DBG: pc=0x%08x v=%0d fetch=0x%08x mon=0x%08x pc_same=%0d cycles (m_cpu htrans=0x%0x haddr=0x%08x hready=%0d sim=%0t)",
                   dut.cpu_ahb.cpu.lastStagePc,
                   dut.cpu_ahb.cpu.lastStageIsValid,
                   fetch_pc,
                   pc_mon,
                   cpu_pc_same_cycles,
                   dut.m_cpu_HTRANS,
                   dut.m_cpu_HADDR,
                   dut.m_cpu_HREADY,
                   $time);
          $display("[KWS_XIP_RT_TB] CPU DBG: gpio_status=0x%02x apb_req=%0d apb_wr=%0d",
                   gpio_status, apb_ahb_req_cnt, apb_ahb_wr_cnt);
        end
      end
    end
  end

  always @(posedge clk) begin
    if (resetn && dut.cpu_ahb.cpu.CsrPlugin_exception &&
        (dut.cpu_ahb.cpu.CsrPlugin_exceptionPortCtrl_exceptionContext_code != 4'd11)) begin
      $display("[KWS_XIP_RT_TB] CPU EXCEPTION pc=0x%08x code=%0d bad=0x%08x instr=0x%08x illegal=%0d @%0t",
               dut.cpu_ahb.cpu.writeBack_PC,
               dut.cpu_ahb.cpu.CsrPlugin_exceptionPortCtrl_exceptionContext_code,
               dut.cpu_ahb.cpu.CsrPlugin_exceptionPortCtrl_exceptionContext_badAddr,
               dut.cpu_ahb.cpu.writeBack_INSTRUCTION,
               dut.cpu_ahb.cpu.execute_CsrPlugin_illegalInstruction,
               $time);
    end
  end

  function automatic [31:0] sram_word(input int unsigned addr);
    int unsigned widx;
    begin
      widx = addr >> 2;
      sram_word = {dut.s0_sram.ram_symbol3[widx],
                   dut.s0_sram.ram_symbol2[widx],
                   dut.s0_sram.ram_symbol1[widx],
                   dut.s0_sram.ram_symbol0[widx]};
    end
  endfunction

  int unsigned flash_load_offset = 32'h0040_0000;
  initial begin
    plusarg_ret = $value$plusargs("FLASH_LOAD_OFFSET=%h", flash_load_offset);
  end

  W25Q64_XIP_stub #(.FLASH_SIZE_BYTES(FLASH_SIZE_BYTES)) u_flash (
    .CSn (qspi_cs_n),
    .CLK (qspi_sclk),
    .DIO0(qspi_dp[0]),
    .DIO1(qspi_dp[1]),
    .DIO2(qspi_dp[2]),
    .DIO3(qspi_dp[3])
  );

  // FAST_QSPI_AHB: for long RT workloads, pin-level QSPI is prohibitively slow.
  // Enable this to bypass QuadSpiXipAhb pin toggling and respond as a 1-cycle AHB ROM
  // backed by u_flash.mem[] (still uses the same FLASH_HEX content).
  // Experimental: keep default 0 (pin-level QSPI) to match real SoC behavior.
  // Enable for quick iteration: +FAST_QSPI_AHB=1
  function automatic [7:0] flash_byte(input int unsigned off);
    if (off < FLASH_SIZE_BYTES) flash_byte = u_flash.mem[off];
    else flash_byte = 8'hFF;
  endfunction

  function automatic [31:0] flash_word(input [31:0] addr);
    int unsigned off;
    int unsigned a;
    begin
      if ((addr >= FLASH_BASE) && (addr < (FLASH_BASE + FLASH_SIZE_BYTES))) begin
        off = (addr - FLASH_BASE) + flash_load_offset;
      end else begin
        off = addr;
      end
      a = off & 32'hFFFF_FFFC;
      flash_word = {flash_byte(a + 3), flash_byte(a + 2), flash_byte(a + 1), flash_byte(a + 0)};
    end
  endfunction

  initial begin
    plusarg_ret = $value$plusargs("FAST_QSPI_AHB=%d", fast_qspi_ahb);
    if (fast_qspi_ahb != 0) begin
      $display("[KWS_XIP_RT_TB] FAST_QSPI_AHB=1 (bypass pin-level QSPI; 1-cycle AHB ROM)");
`ifndef TINYML_SOC_SIM_FAST_QSPI
      force dut.s1_qspi_HREADYOUT = 1'b1;
      force dut.s1_qspi_HRESP = 1'b0;
      force dut.s1_qspi_HRDATA = fast_qspi_hrdata_reg;
`endif
    end
  end

  always @(*) begin
    fast_qspi_hrdata_reg = flash_word(fast_qspi_dphase_addr_reg);
  end

  always @(posedge clk) begin
    if (!resetn) begin
      fast_qspi_dphase_addr_reg <= FLASH_BASE;
    end else if (fast_qspi_ahb != 0) begin
      // Standard AHB pipeline: latch the address phase and drive its data
      // combinationally during the following data phase.
      if (dut.s1_qspi_HREADY && dut.s1_qspi_HSEL && dut.s1_qspi_HTRANS[1]) begin
        fast_qspi_dphase_addr_reg <= dut.s1_qspi_HADDR;
      end
    end
  end

`ifdef ENABLE_FSDB
  bit dump_fsdb = 0;
  initial begin
    plusarg_ret = $value$plusargs("DUMP_FSDB=%d", dump_fsdb);
    if (dump_fsdb) begin
      $fsdbDumpfile("VenusCoreRVTop_kws_xip_rt_tb.fsdb");
      $fsdbDumpvars(0, VenusCoreRVTop_kws_xip_rt_tb);
      $fsdbDumpvars(0, VenusCoreRVTop_kws_xip_rt_tb.dut.cpu_ahb);
      $fsdbDumpvars(0, VenusCoreRVTop_kws_xip_rt_tb.dut.qspi_xip);
      $fsdbDumpvars(0, VenusCoreRVTop_kws_xip_rt_tb.dut.s0_sram);
    end
  end
`else
  initial begin
    integer dump_fsdb = 0;
    plusarg_ret = $value$plusargs("DUMP_FSDB=%d", dump_fsdb);
    if (dump_fsdb != 0) begin
      $display("[KWS_XIP_RT_TB] DUMP_FSDB requested but simv built without ENABLE_FSDB (rebuild with WAVES=1).");
    end
  end
`endif

  // UART monitor
  integer uart_div = 432;
  integer uart_half = 216;
  bit dbg_uart = 0;
  bit dbg_qspi_init = 0;
  bit dbg_qspi_pin = 0;
  bit dbg_qspi_pushpop = 0;
  initial begin
    plusarg_ret = $value$plusargs("UART_DIV=%d", uart_div);
    uart_half = (uart_div / 2);
    plusarg_ret = $value$plusargs("DBG_UART=%d", dbg_uart);
    plusarg_ret = $value$plusargs("DBG_QSPI_INIT=%d", dbg_qspi_init);
    plusarg_ret = $value$plusargs("DBG_QSPI_PIN=%d", dbg_qspi_pin);
    plusarg_ret = $value$plusargs("DBG_QSPI_PUSHPOP=%d", dbg_qspi_pushpop);
    $display("[KWS_XIP_RT_TB] UART decode: uart_div=%0d uart_half=%0d", uart_div, uart_half);
  end

  typedef enum logic [1:0] {
    UART_IDLE  = 2'b00,
    UART_START = 2'b01,
    UART_DATA  = 2'b10,
    UART_STOP  = 2'b11
  } uart_state_e;

  uart_state_e uart_state;
  integer uart_cnt;
  integer uart_bit;
  reg [7:0] uart_data;
  string uart_line;

  integer expected_label = -1;
  integer stop_on_kws = 1;
  integer stop_on_tv = 0;
  initial begin
    plusarg_ret = $value$plusargs("EXPECTED_LABEL=%d", expected_label);
    plusarg_ret = $value$plusargs("STOP_ON_KWS=%d", stop_on_kws);
    plusarg_ret = $value$plusargs("STOP_ON_TV=%d", stop_on_tv);
  end

  // GPIO status monitor (preferred for RT KWS; avoids UART overhead).
  // Use APB GPIO internal write bus for robustness (pin-level gpio may be Z/X in TB).
  // gpio_status[7:4]=state, gpio_status[3:0]=payload
  // - state=0x1 payload=0x1 : testvector PASS
  // - state=0x8 payload=idx : realtime result (top1 idx)
  // - state=0xF payload=err : firmware error code
  bit dbg_gpio = 0;
  reg [7:0] gpio_last = 8'hxx;
  integer gpio_state;
  integer gpio_payload;
  initial begin
    plusarg_ret = $value$plusargs("DBG_GPIO=%d", dbg_gpio);
  end

  always @(posedge clk) begin
    if (!resetn) begin
      gpio_last <= 8'hxx;
    end else begin
      if (gpio_status !== gpio_last) begin
        gpio_last <= gpio_status;
        gpio_state = gpio_status[7:4];
        gpio_payload = gpio_status[3:0];
        if (dbg_gpio) begin
          $display("[KWS_XIP_RT_TB] GPIO=0x%02x state=0x%0x payload=0x%0x @%0t",
                   gpio_status, gpio_state, gpio_payload, $time);
        end
        if ((gpio_state == 4'h1) && (gpio_payload == 4'h1)) begin
          $display("[KWS_XIP_RT_TB] TV PASS (GPIO)");
          if (stop_on_tv != 0) begin
            $finish;
          end
        end
        if (gpio_state == 4'h8) begin
          integer top1_idx;
          top1_idx = gpio_payload;
          $display("[KWS_XIP_RT_TB] KWS top1_idx=%0d (GPIO)", top1_idx);
          if (expected_label >= 0 && top1_idx != expected_label) begin
            $display("[KWS_XIP_RT_TB] FAIL expected=%0d got=%0d", expected_label, top1_idx);
            $fatal(1);
          end else begin
            $display("[KWS_XIP_RT_TB] PASS expected=%0d got=%0d", expected_label, top1_idx);
          end
          if (stop_on_kws != 0) begin
            $finish;
          end
        end
        if (gpio_state == 4'hF) begin
          $display("[KWS_XIP_RT_TB] FW FAIL (GPIO err=0x%0x)", gpio_payload);
          if (gpio_payload == 4'h4) begin
            $display("[KWS_XIP_RT_TB] TV output @0x00004e80: %08x %08x %08x",
                     sram_word(32'h00004e80),
                     sram_word(32'h00004e84),
                     sram_word(32'h00004e88));
          end
          $fatal(1);
        end
      end
    end
  end

  always @(posedge clk) begin
    if (!resetn) begin
      uart_state <= UART_IDLE;
      uart_cnt <= 0;
      uart_bit <= 0;
      uart_data <= 8'h00;
      uart_line = "";
    end else begin
      case (uart_state)
        UART_IDLE: begin
          if (uart_txd == 1'b0) begin
            uart_state <= UART_START;
            uart_cnt <= uart_half;
          end
        end
        UART_START: begin
          if (uart_cnt == 0) begin
            if (uart_txd == 1'b0) begin
              uart_state <= UART_DATA;
              uart_cnt <= uart_div - 1;
              uart_bit <= 0;
              uart_data <= 8'h00;
            end else begin
              uart_state <= UART_IDLE;
            end
          end else begin
            uart_cnt <= uart_cnt - 1;
          end
        end
        UART_DATA: begin
          if (uart_cnt == 0) begin
            uart_data[uart_bit] <= uart_txd;
            if (uart_bit == 7) begin
              uart_state <= UART_STOP;
              uart_cnt <= uart_div - 1;
            end else begin
              uart_bit <= uart_bit + 1;
              uart_cnt <= uart_div - 1;
            end
          end else begin
            uart_cnt <= uart_cnt - 1;
          end
        end
        UART_STOP: begin
          if (uart_cnt == 0) begin
            if (dbg_uart) begin
              $display("[UART][RAW] 0x%02x '%s' @%0t", uart_data, $sformatf("%c", uart_data), $time);
            end
            if ((uart_data == 8'h0A) || (uart_data == 8'h0D)) begin
              if (uart_line.len() > 0) begin
                integer top1_idx;
                if (uart_line == "[KWS][TV] start") begin
                  $display("[KWS_XIP_RT_TB] TV input preview @0x%08x: %08x %08x %08x %08x",
                           32'h00001000,
                           sram_word(32'h00001000),
                           sram_word(32'h00001004),
                           sram_word(32'h00001008),
                           sram_word(32'h0000100c));
                end
                if (uart_line == "[KWS][TV] FAIL") begin
                  $display("[KWS_XIP_RT_TB] TV output preview @0x%08x: %08x %08x %08x",
                           32'h00004e80,
                           sram_word(32'h00004e80),
                           sram_word(32'h00004e84),
                           sram_word(32'h00004e88));
                  $display("[KWS_XIP_RT_TB] TV FAIL");
                  $fatal(1);
                end
                if (uart_line == "[KWS][TV] PASS") begin
                  $display("[KWS_XIP_RT_TB] TV PASS");
                  if (stop_on_tv != 0) begin
                    $finish;
                  end
                end
                if (($sscanf(uart_line, "[KWS] detect idx=%d", top1_idx) == 1) ||
                    ($sscanf(uart_line, "[KWS][RT] top1_idx=%d", top1_idx) == 1) ||
                    ($sscanf(uart_line, "[KWS][RT] raw_top1_idx=%d", top1_idx) == 1) ||
                    ($sscanf(uart_line, "[KWS][RT] detect idx=%d", top1_idx) == 1)) begin
                  $display("[KWS_XIP_RT_TB] KWS top1_idx=%0d", top1_idx);
                  if (expected_label >= 0 && top1_idx != expected_label) begin
                    $display("[KWS_XIP_RT_TB] FAIL expected=%0d got=%0d", expected_label, top1_idx);
                    $fatal(1);
                  end else begin
                    $display("[KWS_XIP_RT_TB] PASS expected=%0d got=%0d", expected_label, top1_idx);
                  end
                  if (stop_on_kws != 0) begin
                    $finish;
                  end
                end
                $display("[UART] %s", uart_line);
                uart_line = "";
              end
            end else begin
              uart_line = {uart_line, $sformatf("%c", uart_data)};
              if (uart_line.len() > 200) begin
                $display("[UART] %s", uart_line);
                uart_line = "";
              end
            end
            uart_state <= UART_IDLE;
          end else begin
            uart_cnt <= uart_cnt - 1;
          end
        end
        default: uart_state <= UART_IDLE;
      endcase
    end
  end

  // SRAM boot stub is initialized inside AhbLite3OnChipRam by $readmemb lane files.
  initial begin
    #1;
    $display("[KWS_XIP_RT_TB] SRAM boot word0 via readmemb: 0x%08x",
             {dut.s0_sram.ram_symbol3[0],
              dut.s0_sram.ram_symbol2[0],
              dut.s0_sram.ram_symbol1[0],
              dut.s0_sram.ram_symbol0[0]});
  end

  // QSPI AHB read observe
  integer qspi_rd_cnt = 0;
  integer qspi_req_cnt = 0;
  integer qspi_rd_print_left = 8;
  bit dbg_qspi_rd = 0;
  integer qspi_npu_rd_left = 32;
  initial begin
    plusarg_ret = $value$plusargs("DBG_QSPI_RD=%d", dbg_qspi_rd);
  end
  always @(posedge clk) begin
    if (dut.s1_qspi_HSEL && dut.s1_qspi_HTRANS[1] && !dut.s1_qspi_HWRITE) begin
      qspi_req_cnt <= qspi_req_cnt + 1;
    end
    if (dut.s1_qspi_HSEL && dut.s1_qspi_HTRANS[1] && !dut.s1_qspi_HWRITE && dut.s1_qspi_HREADYOUT) begin
      qspi_rd_cnt <= qspi_rd_cnt + 1;
      if (dbg_qspi_init && (qspi_rd_print_left > 0)) begin
        qspi_rd_print_left <= qspi_rd_print_left - 1;
        $display("[KWS_XIP_RT_TB] QSPI RD: addr=0x%08x data=0x%08x pc=0x%08x @%0t",
                 dut.s1_qspi_HADDR,
                 dut.s1_qspi_HRDATA,
                 dut.cpu_ahb.cpu.IBusCachedPlugin_fetchPc_pcReg,
                 $time);
      end
      if (dbg_qspi_rd && !dut.ahb_ic.grant_cpu_reg && (qspi_npu_rd_left > 0)) begin
        qspi_npu_rd_left <= qspi_npu_rd_left - 1;
        $display("[KWS_XIP_RT_TB] QSPI RD (NPU): addr=0x%08x data=0x%08x @%0t",
                 dut.s1_qspi_HADDR,
                 dut.s1_qspi_HRDATA,
                 $time);
      end
    end
  end

  // QSPI pin-level debug
  integer qspi_pin_dbg_left = 0;
  always @(posedge qspi_sclk) begin
    if (dbg_qspi_pin && !qspi_cs_n && (dut.qspi_xip.qspi_xip_core.xip_state_reg == 3'd4) && (qspi_pin_dbg_left > 0)) begin
      qspi_pin_dbg_left <= qspi_pin_dbg_left - 1;
      $display("[KWS_XIP_RT_TB] QSPI PIN: dp=0x%1x oe=0x%1x out=0x%1x xip_state=0x%0x @%0t",
               qspi_dp,
               dut.qspi_xip_qspi_dp_writeEnable,
               dut.qspi_xip_qspi_dp_write,
               dut.qspi_xip.qspi_xip_core.xip_state_reg,
               $time);
    end
  end

  integer sram_req_cnt = 0;
  integer sram_wr_cnt = 0;
  always @(posedge clk) begin
    if (dut.s0_sram_HSEL && dut.s0_sram_HTRANS[1] && !dut.s0_sram_HWRITE) begin
      sram_req_cnt <= sram_req_cnt + 1;
    end
    if (dut.s0_sram_HSEL && dut.s0_sram_HTRANS[1] && dut.s0_sram_HWRITE) begin
      sram_wr_cnt <= sram_wr_cnt + 1;
    end
  end

  integer apb_ahb_req_cnt = 0;
  integer apb_ahb_wr_cnt = 0;
  bit dbg_apb = 0;
  integer apb_dbg_left = 16;
  initial begin
    plusarg_ret = $value$plusargs("DBG_APB=%d", dbg_apb);
  end
  always @(posedge clk) begin
    if (dut.s2_apb_HSEL && dut.s2_apb_HTRANS[1]) begin
      apb_ahb_req_cnt <= apb_ahb_req_cnt + 1;
      if (dut.s2_apb_HWRITE) apb_ahb_wr_cnt <= apb_ahb_wr_cnt + 1;
      if (dbg_apb && dut.s2_apb_HWRITE && dut.s2_apb_HREADYOUT && (apb_dbg_left > 0)) begin
        apb_dbg_left <= apb_dbg_left - 1;
        $display("[KWS_XIP_RT_TB] APB-AHB WR: addr=0x%08x data=0x%08x @%0t",
                 dut.s2_apb_HADDR, dut.s2_apb_HWDATA, $time);
      end
    end
  end

  integer d_ahb_dbg_left = 16;
  always @(posedge clk) begin
    if (resetn && dut.m_cpu_HTRANS[1] && (d_ahb_dbg_left > 0)) begin
      d_ahb_dbg_left <= d_ahb_dbg_left - 1;
      $display("[KWS_XIP_RT_TB] CPU-AHB: addr=0x%08x wr=%0d wdata=0x%08x hready=%0d @%0t",
               dut.m_cpu_HADDR,
               dut.m_cpu_HWRITE,
               dut.m_cpu_HWDATA,
               dut.m_cpu_HREADY,
               $time);
    end
  end

  bit dbg_npu_ahb = 0;
  integer npu_ahb_dbg_left = 64;
  initial begin
    plusarg_ret = $value$plusargs("DBG_NPU_AHB=%d", dbg_npu_ahb);
  end
  always @(posedge clk) begin
    if (dbg_npu_ahb && resetn && dut.m_npu_HTRANS[1] && (npu_ahb_dbg_left > 0)) begin
      npu_ahb_dbg_left <= npu_ahb_dbg_left - 1;
      $display("[KWS_XIP_RT_TB] NPU-AHB: addr=0x%08x wr=%0d hburst=0x%0x htrans=0x%0x hready=%0d @%0t",
               dut.m_npu_HADDR,
               dut.m_npu_HWRITE,
               dut.m_npu_HBURST,
               dut.m_npu_HTRANS,
               dut.m_npu_HREADY,
               $time);
    end
  end

  bit dbg_npu_rdata = 0;
  integer npu_rdata_left = 32;
  reg [31:0] npu_prev_addr;
  reg npu_prev_write;
  reg npu_prev_valid;
  bit check_npu_qspi = 0;
  initial begin
    plusarg_ret = $value$plusargs("DBG_NPU_RDATA=%d", dbg_npu_rdata);
    plusarg_ret = $value$plusargs("CHECK_NPU_QSPI=%d", check_npu_qspi);
    npu_prev_addr = 32'h0;
    npu_prev_write = 1'b0;
    npu_prev_valid = 1'b0;
  end
  always @(posedge clk) begin
    if (!resetn) begin
      npu_prev_addr <= 32'h0;
      npu_prev_write <= 1'b0;
      npu_prev_valid <= 1'b0;
    end else if (dut.m_npu_HREADY) begin
      if (dbg_npu_rdata && npu_prev_valid && !npu_prev_write && (npu_rdata_left > 0)) begin
        if (npu_prev_addr >= 32'h0010_0000 && npu_prev_addr < 32'h0050_0000) begin
          npu_rdata_left <= npu_rdata_left - 1;
          $display("[KWS_XIP_RT_TB] NPU-RD: addr=0x%08x data=0x%08x @%0t",
                   npu_prev_addr,
                   dut.m_npu_HRDATA,
                   $time);
        end
      end
      if (check_npu_qspi && npu_prev_valid && !npu_prev_write) begin
        if (npu_prev_addr >= 32'h0010_0000 && npu_prev_addr < 32'h0050_0000) begin
          int unsigned off;
          reg [31:0] exp;
          off = (npu_prev_addr - 32'h0010_0000) + flash_load_offset;
          exp = {u_flash.mem[off + 3],
                 u_flash.mem[off + 2],
                 u_flash.mem[off + 1],
                 u_flash.mem[off + 0]};
          if (exp !== dut.m_npu_HRDATA) begin
            $display("[KWS_XIP_RT_TB] NPU QSPI DATA MISMATCH addr=0x%08x exp=0x%08x got=0x%08x @%0t",
                     npu_prev_addr, exp, dut.m_npu_HRDATA, $time);
            $fatal(1);
          end
        end
      end
      npu_prev_addr <= dut.m_npu_HADDR;
      npu_prev_write <= dut.m_npu_HWRITE;
      npu_prev_valid <= dut.m_npu_HTRANS[1];
    end
  end

  integer qspi_push_dbg_left = 8;
  always @(posedge clk) begin
    if (dbg_qspi_pushpop && resetn && dut.qspi_xip.qspi_xip_core.push_fire && (qspi_push_dbg_left > 0)) begin
      qspi_push_dbg_left <= qspi_push_dbg_left - 1;
      $display("[KWS_XIP_RT_TB] QSPI PUSH: addr=0x%08x data=0x%08x word_buf=0x%08x byte_idx=%0d nibble_phase=%0d @%0t",
               dut.qspi_xip.qspi_xip_core.push_payload_addr,
               dut.qspi_xip.qspi_xip_core.push_payload_data,
               dut.qspi_xip.qspi_xip_core.word_buf_reg,
               dut.qspi_xip.qspi_xip_core.byte_idx_reg,
               dut.qspi_xip.qspi_xip_core.nibble_phase_reg,
               $time);
    end
  end

  integer qspi_pop_dbg_left = 8;
  always @(posedge clk) begin
    if (dbg_qspi_pushpop && resetn && dut.qspi_xip.qspi_xip_core.pop_fire && (qspi_pop_dbg_left > 0)) begin
      qspi_pop_dbg_left <= qspi_pop_dbg_left - 1;
      $display("[KWS_XIP_RT_TB] QSPI POP : dphase_addr=0x%08x data=0x%08x fifo_cnt=%0d @%0t",
               dut.qspi_xip.qspi_xip_core.dphase_addr_reg,
               dut.qspi_xip.qspi_xip_core.fifo_head_data,
               dut.qspi_xip.qspi_xip_core.fifo_cnt_reg,
               $time);
    end
  end

  reg [4:0] last_init_state;
  reg [2:0] last_xip_state;
  initial begin
    last_init_state = 5'h1F;
    last_xip_state  = 3'h7;
    wait(resetn === 1'b1);
    forever begin
      @(posedge clk);
      if (dbg_qspi_init && dut.qspi_xip.qspi_xip_core.init_state_reg !== last_init_state) begin
        last_init_state = dut.qspi_xip.qspi_xip_core.init_state_reg;
        $display("[KWS_XIP_RT_TB] qspi init_state=0x%0x done=%0d err=%0d @%0t",
                 dut.qspi_xip.qspi_xip_core.init_state_reg,
                 dut.qspi_xip.qspi_xip_core.init_done_reg,
                 dut.qspi_xip.qspi_xip_core.init_error_reg,
                 $time);
      end
      if (dbg_qspi_init && dut.qspi_xip.qspi_xip_core.xip_state_reg !== last_xip_state) begin
        last_xip_state = dut.qspi_xip.qspi_xip_core.xip_state_reg;
        $display("[KWS_XIP_RT_TB] qspi xip_state=0x%0x qspi_rd_cnt=%0d pc=0x%08x @%0t",
                 dut.qspi_xip.qspi_xip_core.xip_state_reg,
                 qspi_rd_cnt,
                 dut.cpu_ahb.cpu.IBusCachedPlugin_fetchPc_pcReg,
                 $time);
        if (dut.qspi_xip.qspi_xip_core.xip_state_reg == 3'd4) begin
          if (dbg_qspi_pin) qspi_pin_dbg_left = 64;
        end
      end
      if ((fast_qspi_ahb == 0) && dut.qspi_xip.qspi_xip_core.init_error_reg) begin
        $fatal(1, "[KWS_XIP_RT_TB] QSPI init entered ERROR (init_state=0x%0x)", dut.qspi_xip.qspi_xip_core.init_state_reg);
      end
    end
  end

  // I2S PCM player
  reg signed [15:0] pcm_mem [0:PCM_MAX_SAMPLES-1];
  integer pcm_samples = KWS_CLIP_SAMPLES;
  bit dbg_i2s = 0;
  string pcm_hex;

  initial begin
    for (int i = 0; i < PCM_MAX_SAMPLES; i++) begin
      pcm_mem[i] = 16'sd0;
    end

    if ($value$plusargs("PCM_HEX=%s", pcm_hex)) begin
      $display("[KWS_XIP_RT_TB] PCM_HEX=%s", pcm_hex);
      $readmemh(pcm_hex, pcm_mem);
    end else begin
      $display("[KWS_XIP_RT_TB] PCM_HEX not set, using silence");
    end

    plusarg_ret = $value$plusargs("PCM_SAMPLES=%d", pcm_samples);
    plusarg_ret = $value$plusargs("DBG_I2S=%d", dbg_i2s);
    if (pcm_samples > PCM_MAX_SAMPLES) begin
      $display("[KWS_XIP_RT_TB] PCM_SAMPLES clamp %0d -> %0d", pcm_samples, PCM_MAX_SAMPLES);
      pcm_samples = PCM_MAX_SAMPLES;
    end
  end

  integer pcm_idx = 0;
  integer left_bit_idx = 0;
  reg [23:0] current_sample = 24'h0;
  reg ws_prev = 1'b1;

  // Optional fast-path input preload (NCHWc4 int8 bytes).
  string input_hex;
  integer input_base = 32'h00001000;
  integer input_size = 8000;
  integer input_fd;
  integer input_idx;
  integer input_addr;
  integer input_byte_val;
  integer input_ret;
  initial begin
    plusarg_ret = $value$plusargs("INPUT_BASE=%d", input_base);
    plusarg_ret = $value$plusargs("INPUT_SIZE=%d", input_size);
    if ($value$plusargs("INPUT_HEX=%s", input_hex)) begin
      $display("[KWS_XIP_RT_TB] INPUT_HEX=%s base=0x%08x size=%0d", input_hex, input_base, input_size);
      // Byte-hex -> SRAM symbols (word addressed).
      input_fd = $fopen(input_hex, "r");
      if (input_fd == 0) begin
        $fatal(1, "[KWS_XIP_RT_TB] failed to open INPUT_HEX=%s", input_hex);
      end
      input_idx = 0;
      while (!$feof(input_fd) && (input_idx < input_size)) begin
        input_ret = $fscanf(input_fd, "%h\n", input_byte_val);
        if (input_ret == 1) begin
          input_addr = input_base + input_idx;
          case (input_addr[1:0])
            2'b00: dut.s0_sram.ram_symbol0[input_addr >> 2] = input_byte_val[7:0];
            2'b01: dut.s0_sram.ram_symbol1[input_addr >> 2] = input_byte_val[7:0];
            2'b10: dut.s0_sram.ram_symbol2[input_addr >> 2] = input_byte_val[7:0];
            2'b11: dut.s0_sram.ram_symbol3[input_addr >> 2] = input_byte_val[7:0];
          endcase
          input_idx = input_idx + 1;
        end
      end
      $fclose(input_fd);
    end
  end

  always @(negedge mic_sck) begin
    if (!resetn) begin
      mic_sd = 1'b0;
      pcm_idx = 0;
      left_bit_idx = 0;
      current_sample = 24'h0;
      ws_prev = 1'b1;
    end else begin
      if (ws_prev && !mic_ws) begin
        left_bit_idx = 0;
        if (pcm_idx < pcm_samples) begin
          current_sample = {pcm_mem[pcm_idx], 8'h00};
          pcm_idx = pcm_idx + 1;
        end else begin
          current_sample = 24'h0;
        end
      end
      ws_prev = mic_ws;

      if (!mic_ws) begin
        if (left_bit_idx < 24) begin
          mic_sd = current_sample[23 - left_bit_idx];
        end else begin
          mic_sd = 1'b0;
        end
        left_bit_idx = left_bit_idx + 1;
        if (dbg_i2s && left_bit_idx == 24) begin
          $display("[KWS_XIP_RT_TB] PCM idx=%0d sample=0x%06x", pcm_idx - 1, current_sample);
        end
      end else begin
        mic_sd = 1'b0;
        left_bit_idx = 0;
      end
    end
  end

  // TB timeout
  // 默认值要足够覆盖 XIP 解压/搬运 + 1s 音频采集 + 前端 + NPU，一般会明显超过 2s。
  // 仍可通过 plusarg 覆盖：+TB_TIMEOUT_NS=<ns>
  longint unsigned tb_timeout_ns = 64'd30000000000;
  initial begin
    plusarg_ret = $value$plusargs("TB_TIMEOUT_NS=%d", tb_timeout_ns);
    #(tb_timeout_ns);
    $display("[KWS_XIP_RT_TB] TIMEOUT after %0d ns", tb_timeout_ns);
    $display("[KWS_XIP_RT_TB] TIMEOUT (qspi_req_cnt=%0d qspi_rd_cnt=%0d sram_rd_cnt=%0d sram_wr_cnt=%0d apb_req=%0d apb_wr=%0d pc=0x%08x last_pc=0x%08x last_v=%0d)",
             qspi_req_cnt,
             qspi_rd_cnt,
             sram_req_cnt,
             sram_wr_cnt,
             apb_ahb_req_cnt,
             apb_ahb_wr_cnt,
             dut.cpu_ahb.cpu.IBusCachedPlugin_fetchPc_pcReg,
             dut.cpu_ahb.cpu.lastStagePc,
             dut.cpu_ahb.cpu.lastStageIsValid);
    $display("[KWS_XIP_RT_TB] TIMEOUT (qspi_core: init_done=%0d init_err=%0d xip_state=%0d dphase_v=%0d dphase_addr=0x%08x fifo_cnt=%0d fifo_head_addr=0x%08x)",
             dut.qspi_xip.qspi_xip_core.init_done_reg,
             dut.qspi_xip.qspi_xip_core.init_error_reg,
             dut.qspi_xip.qspi_xip_core.xip_state_reg,
             dut.qspi_xip.qspi_xip_core.dphase_valid_reg,
             dut.qspi_xip.qspi_xip_core.dphase_addr_reg,
             dut.qspi_xip.qspi_xip_core.fifo_cnt_reg,
             dut.qspi_xip.qspi_xip_core.fifo_head_addr);
    $fatal(1);
  end
endmodule
