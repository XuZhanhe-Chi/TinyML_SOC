package other

import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.ahblite._

case class QuadSpiXipAhbConfig(
                                ahb_cfg: AhbLite3Config = AhbLite3Config(32, 32),
                                sclk_div: Int = 1, // SCLK = sysclk / (2*(sclk_div+1))
                                fifo_depth: Int = 8, // 预取 FIFO 深度（word）
                                default_prefetch_words: Int = 4, // 非 burst/非 seq 的默认预取
                                seq_ahead_words: Int = 16, // SEQ/INCR 的“ahead window”
                                init_rst_wait_cycles: Int = 5000, // 99h 后等待，按系统时钟配置
                                init_qe_retry_max: Int = 2, // QE 置位重试次数
                                dq_bit_reverse: Boolean = false, // 若板级 DQ[3:0] 位序反了可开（一般不需要）
                                flash_offset_bytes: BigInt = 0, // 新增：Flash 内偏移（byte），必须 4 字节对齐
                                quad_io_read: Boolean = true // false: conservative 0x03 single-SPI read
                              )

case class QspiWord() extends Bundle {
  val addr = UInt(32 bits) // AHB byte address, 32-bit aligned
  val data = Bits(32 bits) // little-endian packed word
}

case class QuadSpiXipAhb(cfg: QuadSpiXipAhbConfig) extends Component {
  val io = new Bundle {
    val ahb = slave(AhbLite3(cfg.ahb_cfg))

    val qspi_cs_n = out Bool()
    val qspi_sclk = out Bool()
    val qspi_dq_i = in Bits (4 bits)
    val qspi_dq_o = out Bits (4 bits)
    val qspi_dq_oe = out Bits (4 bits) // 1=drive
  }
  noIoPrefix()

  // ------------------------------------------------------------
  // 参数合法性检查
  // ------------------------------------------------------------
  require((cfg.flash_offset_bytes & 0x3) == 0, "flash_offset_bytes must be 4-byte aligned.")
  require(cfg.flash_offset_bytes >= 0, "flash_offset_bytes must be non-negative.")
  require(cfg.flash_offset_bytes < (BigInt(1) << 32), "flash_offset_bytes must fit in 32-bit.")

  val flash_offset_u32 = U(cfg.flash_offset_bytes, 32 bits)

  // ------------------------------------------------------------
  // 常量 / 小工具
  // ------------------------------------------------------------
  val AHB_RESP_OKAY = B"0"
  val AHB_RESP_ERROR = B"1"

  val HTRANS_IDLE = B"00"
  val HTRANS_BUSY = B"01"
  val HTRANS_NONSEQ = B"10"
  val HTRANS_SEQ = B"11"

  val HBURST_SINGLE = B"000"
  val HBURST_INCR = B"001"
  val HBURST_WRAP4 = B"010"
  val HBURST_INCR4 = B"011"
  val HBURST_WRAP8 = B"100"
  val HBURST_INCR8 = B"101"
  val HBURST_WRAP16 = B"110"
  val HBURST_INCR16 = B"111"

  def align32(addr: UInt): UInt = (addr & U"32'hFFFF_FFFC")

  // DQ nibble 位序映射（可选）
  def mapNibble(n: Bits): Bits = {
    if (cfg.dq_bit_reverse) n.reversed else n
  }

  // burst 长度（word），INCR 返回 0 表示“未知/不定长”
  def burstLenWords(hburst: Bits): UInt = {
    val w = UInt(6 bits)
    w := U(1)
    switch(hburst) {
      is(HBURST_SINGLE) {
        w := U(1)
      }
      is(HBURST_INCR4) {
        w := U(4)
      }
      is(HBURST_INCR8) {
        w := U(8)
      }
      is(HBURST_INCR16) {
        w := U(16)
      }
      is(HBURST_WRAP4) {
        w := U(4)
      }
      is(HBURST_WRAP8) {
        w := U(8)
      }
      is(HBURST_WRAP16) {
        w := U(16)
      }
      is(HBURST_INCR) {
        w := U(0)
      } // 不定长
      default {
        w := U(1)
      }
    }
    w
  }

  // ------------------------------------------------------------
  // QSPI 物理层寄存器（CS/SCLK/DQ）
  // ------------------------------------------------------------
  val cs_n_reg = Reg(Bool()) init (True)
  val sclk_reg = Reg(Bool()) init (False)
  val dq_o_reg = Reg(Bits(4 bits)) init (0)
  val dq_oe_reg = Reg(Bits(4 bits)) init (0)

  io.qspi_cs_n := cs_n_reg
  io.qspi_sclk := sclk_reg
  io.qspi_dq_o := dq_o_reg
  io.qspi_dq_oe := dq_oe_reg

  // SCLK 分频 + 停钟策略修复
  val div_width = log2Up(cfg.sclk_div + 2)
  val div_reload = U(cfg.sclk_div, div_width bits)
  val div_cnt_reg = Reg(UInt(div_width bits)) init (div_reload)
  val clk_run_reg = Reg(Bool()) init (False)

  val toggle_pulse = clk_run_reg && (div_cnt_reg === 0)
  val sclk_rise = toggle_pulse && !sclk_reg
  val sclk_fall = toggle_pulse && sclk_reg

  when(clk_run_reg) {
    when(div_cnt_reg === 0) {
      div_cnt_reg := div_reload
      sclk_reg := ~sclk_reg
    } otherwise {
      div_cnt_reg := div_cnt_reg - 1
    }
  } otherwise {
    // /CS=0 时停钟要冻结 SCLK，/CS=1 时规整为 mode0 idle low
    when(cs_n_reg) {
      div_cnt_reg := div_reload
      sclk_reg := False
    } otherwise {
      div_cnt_reg := div_cnt_reg
      sclk_reg := sclk_reg
    }
  }

  // ------------------------------------------------------------
  // AHB Data Phase 寄存（标准 AHB-Lite 两阶段）
  // ------------------------------------------------------------
  val init_done_reg = Reg(Bool()) init (False)
  val init_error_reg = Reg(Bool()) init (False)

  val aphase_active = io.ahb.HTRANS =/= HTRANS_IDLE && io.ahb.HTRANS =/= HTRANS_BUSY
  val aphase_read = io.ahb.HSEL && aphase_active && !io.ahb.HWRITE
  val aphase_ok = aphase_read && init_done_reg && !init_error_reg

  val dphase_valid_reg = Reg(Bool()) init (False)
  val dphase_addr_reg = Reg(UInt(32 bits)) init (0)
  val dphase_htrans_reg = Reg(Bits(2 bits)) init (HTRANS_IDLE)
  val dphase_hburst_reg = Reg(Bits(3 bits)) init (HBURST_SINGLE)

  when(io.ahb.HREADY) {
    dphase_valid_reg := aphase_ok
    dphase_addr_reg := align32(io.ahb.HADDR)
    dphase_htrans_reg := io.ahb.HTRANS
    dphase_hburst_reg := io.ahb.HBURST
  }

  // ------------------------------------------------------------
  // 预取 FIFO（自实现环形缓冲）
  // ------------------------------------------------------------
  val fifo_ptr_w = log2Up(cfg.fifo_depth)
  val fifo_cnt_w = log2Up(cfg.fifo_depth + 1)

  val fifo_mem = Vec(Reg(QspiWord()), cfg.fifo_depth)
  val fifo_rd_ptr_reg = Reg(UInt(fifo_ptr_w bits)) init (0)
  val fifo_wr_ptr_reg = Reg(UInt(fifo_ptr_w bits)) init (0)
  val fifo_cnt_reg = Reg(UInt(fifo_cnt_w bits)) init (0)

  val fifo_empty = fifo_cnt_reg === 0
  val fifo_full = fifo_cnt_reg === cfg.fifo_depth

  val fifo_head = fifo_mem(fifo_rd_ptr_reg)

  // 时序优化：对 read response 做本地打拍（插入 1-cycle wait-state）
  // 目的：切断 fifo_head.data/addr -> AHB HRDATA -> 下游寄存器 的组合关键路径。
  val rsp_pipe_pending_reg = RegInit(False)
  val rsp_pipe_data_reg = Reg(Bits(32 bits)) init (0)

  def fifoFlush(): Unit = {
    fifo_rd_ptr_reg := 0
    fifo_wr_ptr_reg := 0
    fifo_cnt_reg := 0
    rsp_pipe_pending_reg := False
  }

  val dphase_hit = dphase_valid_reg && !fifo_empty && (fifo_head.addr === dphase_addr_reg)
  val ahb_ready_this = (!dphase_valid_reg) || dphase_hit

  val push_fire = Bool()
  val push_payload = QspiWord()
  push_fire := False
  push_payload.addr := 0
  push_payload.data := 0

  val pop_fire = dphase_hit && io.ahb.HREADY && io.ahb.HREADYOUT
  when(pop_fire) {
    fifo_rd_ptr_reg := fifo_rd_ptr_reg + 1
  }

  when(push_fire) {
    fifo_mem(fifo_wr_ptr_reg) := push_payload
    fifo_wr_ptr_reg := fifo_wr_ptr_reg + 1
  }

  switch(push_fire ## pop_fire) {
    is(B"01") {
      fifo_cnt_reg := fifo_cnt_reg - 1
    }
    is(B"10") {
      fifo_cnt_reg := fifo_cnt_reg + 1
    }
    default {
      fifo_cnt_reg := fifo_cnt_reg
    }
  }

  // ------------------------------------------------------------
  // AHB 输出：只读，miss 则等待（HREADYOUT=0）
  // ------------------------------------------------------------
  io.ahb.HREADYOUT := True
  io.ahb.HRESP := AHB_RESP_OKAY.asBool
  io.ahb.HRDATA := B(0, 32 bits)

  when(!init_done_reg && !init_error_reg) {
    io.ahb.HREADYOUT := False
    io.ahb.HRESP := AHB_RESP_OKAY.asBool
    io.ahb.HRDATA := B(0, 32 bits)
  } elsewhen (init_error_reg) {
    io.ahb.HREADYOUT := True
    io.ahb.HRESP := AHB_RESP_ERROR.asBool
    io.ahb.HRDATA := B(0, 32 bits)
  } otherwise {
    io.ahb.HRESP := AHB_RESP_OKAY.asBool

    // response pipeline: 命中时先 stall 1-cycle 并锁存数据，下一周期完成数据相位
    when(rsp_pipe_pending_reg) {
      io.ahb.HREADYOUT := True
      io.ahb.HRDATA := rsp_pipe_data_reg
      when(io.ahb.HREADY) {
        rsp_pipe_pending_reg := False
      }
    } elsewhen (dphase_hit && dphase_valid_reg) {
      io.ahb.HREADYOUT := False
      io.ahb.HRDATA := B(0, 32 bits)
      rsp_pipe_data_reg := fifo_head.data
      rsp_pipe_pending_reg := True
    } otherwise {
      io.ahb.HREADYOUT := ahb_ready_this
      io.ahb.HRDATA := B(0, 32 bits)
    }
  }

  // ------------------------------------------------------------
  // 顺序预取控制：stream_base / stream_end（更激进）
  // ------------------------------------------------------------
  val stream_active_reg = Reg(Bool()) init (False)
  val stream_base_addr_reg = Reg(UInt(32 bits)) init (0)
  val stream_end_addr_reg = Reg(UInt(32 bits)) init (0)
  val last_aphase_addr_reg = Reg(UInt(32 bits)) init (0)

  when(io.ahb.HREADY && aphase_ok) {
    val new_addr = align32(io.ahb.HADDR)
    val is_seq = (io.ahb.HTRANS === HTRANS_SEQ) && (new_addr === (last_aphase_addr_reg + 4))
    val is_burst = (io.ahb.HBURST =/= HBURST_SINGLE)

    last_aphase_addr_reg := new_addr

    when(stream_active_reg && (is_seq || is_burst)) {
      val extend_end = new_addr + U(cfg.seq_ahead_words * 4, 32 bits)
      when(extend_end > stream_end_addr_reg) {
        stream_end_addr_reg := extend_end
      }
    }
  }

  // ------------------------------------------------------------
  // XIP FSM（XT25F64F / W25Q64：Quad I/O Fast Read）
  //
  // - opcode: 0xEB (1-bit on IO0)
  // - addr  : 24-bit (quad)
  // - mode  : M7..0 (quad, 2 cycles; usually 0x00)
  // - dummy : 4 cycles (total dummy=6 by default, M byte counts as 2)
  // - data  : quad
  // ------------------------------------------------------------
  object XipState extends SpinalEnum {
    val IDLE, OPCODE, ADDR, MODE, DUMMY, STREAM, ADDR_SPI, STREAM_SPI = newElement()
  }

  val xip_state_reg = Reg(XipState()) init (XipState.IDLE)

  val dphase_miss = dphase_valid_reg && !dphase_hit && init_done_reg && !init_error_reg

  val start_stream_pulse = Bool()
  start_stream_pulse := False

  when(dphase_miss) {
    val miss_addr = dphase_addr_reg
    val miss_in_window = stream_active_reg && (miss_addr >= stream_base_addr_reg) && (miss_addr < stream_end_addr_reg)

    // 重要：当前实现的 AHB 侧命中条件仅支持 fifo_head.addr == dphase_addr_reg。
    // 若 master 发起“窗口内但非 head”的访问（例如 I$ critical-word-first 先取 0x6c，再补齐 0x60..0x68），
    // 会导致 fifo 永远无法 pop，形成死等。这里在“窗口内但 miss_addr != fifo_head.addr 且 FIFO 非空”时直接 restart。
    val miss_is_head = (!fifo_empty) && (fifo_head.addr === miss_addr)
    val xip_fetching = xip_state_reg =/= XipState.IDLE
    // fifo_empty 时仅当 XIP engine 正在拉取（非 IDLE）才允许“等待后台填充”，否则会永久死等/或反复 restart
    val can_keep_streaming = miss_in_window && (miss_is_head || (fifo_empty && xip_fetching))

    // miss is outside current stream window -> restart stream at miss_addr
    when(!stream_active_reg || !can_keep_streaming) {
      fifoFlush()
      stream_active_reg := True
      stream_base_addr_reg := miss_addr

      val bl = burstLenWords(dphase_hburst_reg)
      val base_words = UInt(16 bits)
      when(bl === 0) {
        base_words := U(cfg.seq_ahead_words, 16 bits)
      } otherwise {
        base_words := (bl.resize(16) + U(cfg.seq_ahead_words, 16 bits))
      }
      when(dphase_hburst_reg === HBURST_SINGLE && dphase_htrans_reg =/= HTRANS_SEQ) {
        base_words := U(cfg.default_prefetch_words, 16 bits)
      }

      stream_end_addr_reg := miss_addr + (base_words.resize(32) << 2).resized
      start_stream_pulse := True
    } otherwise {
      // miss is within the stream window: keep streaming, and optionally extend a bit further
      val extend_end = miss_addr + U(cfg.default_prefetch_words * 4, 32 bits)
      when(extend_end > stream_end_addr_reg) {
        stream_end_addr_reg := extend_end
      }
    }
  }

  // ------------------------------------------------------------
  // SPI 1-bit Byte Engine（用于 init 以及 opcode（0xEB））
  // ------------------------------------------------------------
  val spi_active_reg = Reg(Bool()) init (False)
  val spi_tx_shift_reg = Reg(Bits(8 bits)) init (0)
  val spi_rx_shift_reg = Reg(Bits(8 bits)) init (0)
  val spi_bits_left_reg = Reg(UInt(4 bits)) init (0)
  val spi_do_rx_reg = Reg(Bool()) init (False)
  val spi_done_pulse = Bool()
  spi_done_pulse := False

  val spi_start_pulse = Bool()
  val spi_start_tx = Bits(8 bits)
  val spi_start_do_rx = Bool()

  spi_start_pulse := False
  spi_start_tx := 0
  spi_start_do_rx := False

  when(spi_start_pulse) {
    spi_active_reg := True
    spi_tx_shift_reg := spi_start_tx
    spi_rx_shift_reg := 0
    spi_bits_left_reg := U(8)
    spi_do_rx_reg := spi_start_do_rx
  }

  when(spi_active_reg) {
    when(sclk_rise && spi_do_rx_reg) {
      spi_rx_shift_reg := spi_rx_shift_reg(6 downto 0) ## io.qspi_dq_i(1)
    }
    when(sclk_fall) {
      when(spi_bits_left_reg === 1) {
        spi_active_reg := False
        spi_done_pulse := True
        spi_bits_left_reg := 0
      } otherwise {
        spi_tx_shift_reg := spi_tx_shift_reg(6 downto 0) ## B(0, 1 bits)
        spi_bits_left_reg := spi_bits_left_reg - 1
      }
    }
  }

  // ------------------------------------------------------------
  // Init FSM（W25Q64FV：复位 + QE + wrap）
  // ------------------------------------------------------------
  object InitState extends SpinalEnum {
    val RESET_EN, RESET_MEM, WAIT_RST,
    RD_SR1_CMD, RD_SR1_DAT,
    RD_SR2_CMD, RD_SR2_DAT,
    QE_SET_VOL_EN, QE_SET_WRSR_CMD, QE_SET_WRSR_SR1, QE_SET_WRSR_SR2,
    POLL_BUSY_CMD, POLL_BUSY_DAT,
    VERIFY_SR2_CMD, VERIFY_SR2_DAT,
    SET_WRAP_CMD, SET_WRAP_D0, SET_WRAP_D1, SET_WRAP_D2, SET_WRAP_W,
    DONE, ERROR = newElement()
  }

  val init_state_reg = Reg(InitState()) init (InitState.RESET_EN)
  val rst_wait_cnt_reg = Reg(UInt(32 bits)) init (0)

  val sr1_reg = Reg(Bits(8 bits)) init (0)
  val sr2_reg = Reg(Bits(8 bits)) init (0)
  val qe_retry_cnt_reg = Reg(UInt(2 bits)) init (0)

  def qeBit(sr2: Bits): Bool = sr2(1)

  val addr_shift_reg = Reg(Bits(24 bits)) init (0)
  val addr_nibbles_left_reg = Reg(UInt(3 bits)) init (0)
  val dummy_left_reg = Reg(UInt(3 bits)) init (0)

  val mode_shift_reg = Reg(Bits(8 bits)) init (0)
  val mode_nibbles_left_reg = Reg(UInt(2 bits)) init (0)

  val hi_nibble_reg = Reg(Bits(4 bits)) init (0)
  val nibble_phase_reg = Reg(Bool()) init (False)
  val byte_idx_reg = Reg(UInt(2 bits)) init (0)
  val word_buf_reg = Reg(Bits(32 bits)) init (0)
  val stream_word_addr_reg = Reg(UInt(32 bits)) init (0)

  val word_in_progress = !(byte_idx_reg === 0 && !nibble_phase_reg)

  def abortStream(): Unit = {
    cs_n_reg := True
    clk_run_reg := False
    spi_active_reg := False
    spi_bits_left_reg := 0
    byte_idx_reg := 0
    nibble_phase_reg := False
    word_buf_reg := 0
    xip_state_reg := XipState.IDLE
  }

  // ------------------------------------------------------------
  // DQ 驱动选择
  // ------------------------------------------------------------
  dq_o_reg := 0
  dq_oe_reg := 0

  when(spi_active_reg) {
    dq_oe_reg := B"0001"
    dq_o_reg(0) := spi_tx_shift_reg(7)
  }

  when(xip_state_reg === XipState.ADDR) {
    dq_oe_reg := B"1111"
    dq_o_reg := mapNibble(addr_shift_reg(23 downto 20))
  }

  when(xip_state_reg === XipState.MODE) {
    dq_oe_reg := B"1111"
    dq_o_reg := mapNibble(mode_shift_reg(7 downto 4))
  }

  val any_activity = Bool()
  any_activity := False

  // ------------------------
  // Init FSM 行为
  // ------------------------
  switch(init_state_reg) {
    is(InitState.RESET_EN) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h66"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        cs_n_reg := True
        clk_run_reg := False
        init_state_reg := InitState.RESET_MEM
      }
    }

    is(InitState.RESET_MEM) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h99"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        cs_n_reg := True
        clk_run_reg := False
        rst_wait_cnt_reg := U(cfg.init_rst_wait_cycles, 32 bits)
        init_state_reg := InitState.WAIT_RST
      }
    }

    is(InitState.WAIT_RST) {
      any_activity := True
      cs_n_reg := True
      clk_run_reg := False
      when(rst_wait_cnt_reg === 0) {
        if (cfg.quad_io_read) {
          init_state_reg := InitState.RD_SR1_CMD
        } else {
          init_state_reg := InitState.DONE
        }
      } otherwise {
        rst_wait_cnt_reg := rst_wait_cnt_reg - 1
      }
    }

    is(InitState.RD_SR1_CMD) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h05"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        clk_run_reg := False
        init_state_reg := InitState.RD_SR1_DAT
      }
    }
    is(InitState.RD_SR1_DAT) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h00"
        spi_start_do_rx := True
      }
      when(spi_done_pulse) {
        sr1_reg := spi_rx_shift_reg
        cs_n_reg := True
        clk_run_reg := False
        init_state_reg := InitState.RD_SR2_CMD
      }
    }

    is(InitState.RD_SR2_CMD) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h35"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        clk_run_reg := False
        init_state_reg := InitState.RD_SR2_DAT
      }
    }
    is(InitState.RD_SR2_DAT) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h00"
        spi_start_do_rx := True
      }
      when(spi_done_pulse) {
        sr2_reg := spi_rx_shift_reg
        cs_n_reg := True
        clk_run_reg := False
        when(qeBit(spi_rx_shift_reg)) {
          init_state_reg := InitState.SET_WRAP_CMD
        } otherwise {
          qe_retry_cnt_reg := 0
          init_state_reg := InitState.QE_SET_VOL_EN
        }
      }
    }

    is(InitState.QE_SET_VOL_EN) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg) {
        spi_start_pulse := True
        spi_start_tx := B"8'h50"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        cs_n_reg := True
        clk_run_reg := False
        init_state_reg := InitState.QE_SET_WRSR_CMD
      }
    }

    is(InitState.QE_SET_WRSR_CMD) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h01"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        clk_run_reg := False
        init_state_reg := InitState.QE_SET_WRSR_SR1
      }
    }
    is(InitState.QE_SET_WRSR_SR1) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := sr1_reg
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        clk_run_reg := False
        init_state_reg := InitState.QE_SET_WRSR_SR2
      }
    }
    is(InitState.QE_SET_WRSR_SR2) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        val sr2_new = (sr2_reg | B"8'h02")
        spi_start_pulse := True
        spi_start_tx := sr2_new
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        cs_n_reg := True
        clk_run_reg := False
        init_state_reg := InitState.POLL_BUSY_CMD
      }
    }

    is(InitState.POLL_BUSY_CMD) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h05"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        clk_run_reg := False
        init_state_reg := InitState.POLL_BUSY_DAT
      }
    }
    is(InitState.POLL_BUSY_DAT) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h00"
        spi_start_do_rx := True
      }
      when(spi_done_pulse) {
        val sr1_now = spi_rx_shift_reg
        cs_n_reg := True
        clk_run_reg := False
        when(sr1_now(0)) {
          init_state_reg := InitState.POLL_BUSY_CMD
        } otherwise {
          init_state_reg := InitState.VERIFY_SR2_CMD
        }
      }
    }

    is(InitState.VERIFY_SR2_CMD) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h35"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        clk_run_reg := False
        init_state_reg := InitState.VERIFY_SR2_DAT
      }
    }
    is(InitState.VERIFY_SR2_DAT) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h00"
        spi_start_do_rx := True
      }
      when(spi_done_pulse) {
        val sr2_now = spi_rx_shift_reg
        sr2_reg := sr2_now
        cs_n_reg := True
        clk_run_reg := False
        when(qeBit(sr2_now)) {
          init_state_reg := InitState.SET_WRAP_CMD
        } otherwise {
          when(qe_retry_cnt_reg === U(cfg.init_qe_retry_max - 1, 2 bits)) {
            init_state_reg := InitState.ERROR
          } otherwise {
            qe_retry_cnt_reg := qe_retry_cnt_reg + 1
            init_state_reg := InitState.QE_SET_VOL_EN
          }
        }
      }
    }

    is(InitState.SET_WRAP_CMD) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h77"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        clk_run_reg := False
        init_state_reg := InitState.SET_WRAP_D0
      }
    }
    is(InitState.SET_WRAP_D0) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h00"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        clk_run_reg := False
        init_state_reg := InitState.SET_WRAP_D1
      }
    }
    is(InitState.SET_WRAP_D1) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h00"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        clk_run_reg := False
        init_state_reg := InitState.SET_WRAP_D2
      }
    }
    is(InitState.SET_WRAP_D2) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h00"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        clk_run_reg := False
        init_state_reg := InitState.SET_WRAP_W
      }
    }
    is(InitState.SET_WRAP_W) {
      any_activity := True
      cs_n_reg := False
      clk_run_reg := True
      when(!spi_active_reg && !spi_done_pulse) {
        spi_start_pulse := True
        spi_start_tx := B"8'h10"
        spi_start_do_rx := False
      }
      when(spi_done_pulse) {
        cs_n_reg := True
        clk_run_reg := False
        init_state_reg := InitState.DONE
      }
    }

    is(InitState.DONE) {
      any_activity := False
      init_done_reg := True
      init_error_reg := False
    }

    is(InitState.ERROR) {
      any_activity := False
      init_done_reg := False
      init_error_reg := True
    }
  }

  // ------------------------------------------------------------
  // XIP FSM（只有 init_done 才允许）
  // ------------------------------------------------------------
  //
  // 注意：init 阶段也需要驱动 cs/sclk/DQ 完成 QE / wrap 配置。
  // 因此 init_done==False 时，这里不能覆盖 cs_n_reg/clk_run_reg，否则会导致 init 卡死在 RESET_EN。
  //
  when(init_error_reg) {
    // init 错误时强制停止 QSPI 访问
    xip_state_reg := XipState.IDLE
    cs_n_reg := True
    clk_run_reg := False
  } elsewhen (init_done_reg && !init_error_reg) {
    switch(xip_state_reg) {
      is(XipState.IDLE) {
        cs_n_reg := True
        clk_run_reg := False
        when(start_stream_pulse) {
          cs_n_reg := False
          clk_run_reg := True
          xip_state_reg := XipState.OPCODE
          when(!spi_active_reg) {
            spi_start_pulse := True
            spi_start_tx := (if (cfg.quad_io_read) B"8'hEB" else B"8'h03")
            spi_start_do_rx := False
          }
        }
      }

      is(XipState.OPCODE) {
        cs_n_reg := False
        clk_run_reg := True
        when(spi_done_pulse) {
          clk_run_reg := False
          // 新增：flash_offset_bytes 加到起始地址，再取低 24-bit 作为 flash 地址
          val flash_addr_24 = (stream_base_addr_reg + flash_offset_u32)(23 downto 0).asBits
          addr_shift_reg := flash_addr_24
          if (cfg.quad_io_read) {
            addr_nibbles_left_reg := U(6)
            // Quad I/O: after addr, send 1 mode byte then dummy.
            mode_shift_reg := B"8'h00"
            mode_nibbles_left_reg := U(2)
            xip_state_reg := XipState.ADDR
          } else {
            addr_nibbles_left_reg := U(3)
            xip_state_reg := XipState.ADDR_SPI
          }
        }
      }

      is(XipState.ADDR_SPI) {
        cs_n_reg := False
        // 单线 SPI 每个 byte 之间停钟一拍，确保下一 byte 的 MSB 先稳定到 DQ0。
        clk_run_reg := spi_active_reg && !spi_done_pulse
        when(!spi_active_reg && !spi_done_pulse) {
          spi_start_pulse := True
          spi_start_tx := addr_shift_reg(23 downto 16)
          spi_start_do_rx := False
        }
        when(spi_done_pulse) {
          when(addr_nibbles_left_reg === 1) {
            addr_nibbles_left_reg := 0
            stream_word_addr_reg := stream_base_addr_reg
            hi_nibble_reg := 0
            nibble_phase_reg := False
            byte_idx_reg := 0
            word_buf_reg := 0
            xip_state_reg := XipState.STREAM_SPI
          } otherwise {
            addr_shift_reg := addr_shift_reg(15 downto 0) ## B(0, 8 bits)
            addr_nibbles_left_reg := addr_nibbles_left_reg - 1
          }
        }
      }

      is(XipState.ADDR) {
        cs_n_reg := False
        clk_run_reg := True
        when(sclk_fall) {
          when(addr_nibbles_left_reg === 1) {
            addr_nibbles_left_reg := 0
            xip_state_reg := XipState.MODE
          } otherwise {
            addr_shift_reg := addr_shift_reg(19 downto 0) ## B(0, 4 bits)
            addr_nibbles_left_reg := addr_nibbles_left_reg - 1
          }
        }
      }

      is(XipState.MODE) {
        cs_n_reg := False
        clk_run_reg := True
        when(sclk_fall) {
          when(mode_nibbles_left_reg === 1) {
            mode_nibbles_left_reg := 0
            dummy_left_reg := U(4) // total dummy=6 default, mode byte counts as 2 cycles
            xip_state_reg := XipState.DUMMY
          } otherwise {
            mode_shift_reg := mode_shift_reg(3 downto 0) ## B(0, 4 bits)
            mode_nibbles_left_reg := mode_nibbles_left_reg - 1
          }
        }
      }

      is(XipState.DUMMY) {
        cs_n_reg := False
        clk_run_reg := True
        when(sclk_rise) {
          when(dummy_left_reg === 1) {
            dummy_left_reg := 0
            stream_word_addr_reg := stream_base_addr_reg
            hi_nibble_reg := 0
            nibble_phase_reg := False
            byte_idx_reg := 0
            word_buf_reg := 0
            xip_state_reg := XipState.STREAM
          } otherwise {
            dummy_left_reg := dummy_left_reg - 1
          }
        }
      }

      is(XipState.STREAM) {
        cs_n_reg := False

        val can_start_next_word = !word_in_progress && !fifo_full && (stream_word_addr_reg < stream_end_addr_reg)
        val need_finish_word = word_in_progress
        clk_run_reg := need_finish_word || can_start_next_word

        when(sclk_rise && clk_run_reg) {
          val nib = mapNibble(io.qspi_dq_i)
          when(!nibble_phase_reg) {
            hi_nibble_reg := nib
            nibble_phase_reg := True
          } otherwise {
            val byte_val = hi_nibble_reg ## nib
            val word_next = Bits(32 bits)
            word_next := word_buf_reg
            switch(byte_idx_reg) {
              is(U(0)) {
                word_next(7 downto 0) := byte_val
              }
              is(U(1)) {
                word_next(15 downto 8) := byte_val
              }
              is(U(2)) {
                word_next(23 downto 16) := byte_val
              }
              default {
                word_next(31 downto 24) := byte_val
              }
            }
            word_buf_reg := word_next

            nibble_phase_reg := False
            byte_idx_reg := byte_idx_reg + 1

            when(byte_idx_reg === 3) {
              push_fire := True
              push_payload.addr := stream_word_addr_reg
              push_payload.data := word_next

              stream_word_addr_reg := stream_word_addr_reg + 4
              byte_idx_reg := 0
              word_buf_reg := 0
            }
          }
        }

        when(start_stream_pulse) {
          abortStream()
        }
      }

      is(XipState.STREAM_SPI) {
        cs_n_reg := False

        val can_start_next_byte = !spi_active_reg && !fifo_full && (stream_word_addr_reg < stream_end_addr_reg)
        // 读数据 byte 之间同样停钟一拍，否则 byte engine 刚启动时可能丢第一位。
        clk_run_reg := spi_active_reg && !spi_done_pulse

        when(can_start_next_byte && !spi_done_pulse) {
          spi_start_pulse := True
          spi_start_tx := B"8'h00"
          spi_start_do_rx := True
        }

        when(spi_done_pulse) {
          val byte_val = spi_rx_shift_reg
          val word_next = Bits(32 bits)
          word_next := word_buf_reg
          switch(byte_idx_reg) {
            is(U(0)) {
              word_next(7 downto 0) := byte_val
            }
            is(U(1)) {
              word_next(15 downto 8) := byte_val
            }
            is(U(2)) {
              word_next(23 downto 16) := byte_val
            }
            default {
              word_next(31 downto 24) := byte_val
            }
          }
          word_buf_reg := word_next
          byte_idx_reg := byte_idx_reg + 1

          when(byte_idx_reg === 3) {
            push_fire := True
            push_payload.addr := stream_word_addr_reg
            push_payload.data := word_next

            stream_word_addr_reg := stream_word_addr_reg + 4
            byte_idx_reg := 0
            word_buf_reg := 0
          }
        }

        when(start_stream_pulse) {
          abortStream()
        }
      }
    }
  }

  when(!init_done_reg && !init_error_reg) {
    xip_state_reg := XipState.IDLE
  }
}

/** 顶层 Verilog 生成器 */
object QuadSpiXipAhb extends App {
  SpinalConfig(
    targetDirectory = "rtl",
    oneFilePerComponent = false,
    enumPrefixEnable = true,
    headerWithDate = false,
    anonymSignalPrefix = "tmp",
    keepAll = true
  ).generateVerilog(new QuadSpiXipAhb(QuadSpiXipAhbConfig()))
}
