package ip.i2s

import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.apb._

/**
 * MSM261S I2S Mic + APB3 peripheral.
 *
 * Register map (byte offsets, 4KB window):
 * - 0x00 CTRL   [0]=en, [1]=soft_rst, [2]=clr_ovf
 * - 0x04 DIV    [15:0]=sck_div
 * - 0x08 STATUS [0]=empty, [1]=full, [2]=ovf, [23:8]=fifo_level_words
 * - 0x0C DATA   read pops 1 FIFO word (2x int16 samples)
 *
 * 实现说明：
 * - APB 寄存器访问使用 `Apb3SlaveFactory`；
 * - FIFO 使用 `StreamFifo`（32-bit word，打包 2x int16 samples）。
 */
case class Msm261sApb(
    fifoDepth: Int = 512,
    apbCfg: Apb3Config = Apb3Config(addressWidth = 12, dataWidth = 32, selWidth = 1, useSlaveError = false)
) extends Component {
  require(apbCfg.dataWidth == 32, "Msm261sApb assumes APB dataWidth=32")
  require(apbCfg.selWidth == 1, "Msm261sApb assumes APB selWidth=1")

  require(fifoDepth >= 2, "fifoDepth must be >= 2")
  require((fifoDepth & (fifoDepth - 1)) == 0, "fifoDepth must be power of 2")
  require(fifoDepth <= 65536, "fifoDepth must be <= 65536 (STATUS.level is 16-bit)")

  val io = new Bundle {
    val apb = slave(Apb3(apbCfg)).setName("apb")

    val mic_sck = out Bool()
    val mic_ws = out Bool()
    val mic_sd = in Bool()
  }
  noIoPrefix()

  // --------------------------------------------------------------------------
  // APB register bank (use SlaveFactory)
  // --------------------------------------------------------------------------

  private val bus_ctrl = Apb3SlaveFactory(io.apb)

  private val ctrl_en_reg = Reg(Bool()) init (False)
  private val ctrl_soft_rst_reg = Reg(Bool()) init (False)
  private val ctrl_clr_ovf_reg = Reg(Bool()) init (False)

  private val sck_div_cfg_reg = Reg(UInt(16 bits)) init (0)

  // 0x00 CTRL
  bus_ctrl.read(ctrl_en_reg, address = 0x00, bitOffset = 0, documentation = "CTRL.EN")
  bus_ctrl.write(ctrl_en_reg, address = 0x00, bitOffset = 0, documentation = "CTRL.EN")
  bus_ctrl.read(ctrl_soft_rst_reg, address = 0x00, bitOffset = 1, documentation = "CTRL.SOFT_RST")
  bus_ctrl.write(ctrl_soft_rst_reg, address = 0x00, bitOffset = 1, documentation = "CTRL.SOFT_RST")
  bus_ctrl.read(ctrl_clr_ovf_reg, address = 0x00, bitOffset = 2, documentation = "CTRL.CLR_OVF")
  bus_ctrl.write(ctrl_clr_ovf_reg, address = 0x00, bitOffset = 2, documentation = "CTRL.CLR_OVF")

  // 0x04 DIV
  bus_ctrl.read(sck_div_cfg_reg, address = 0x04, bitOffset = 0, documentation = "DIV.SCK_DIV[15:0]")
  bus_ctrl.write(sck_div_cfg_reg, address = 0x04, bitOffset = 0, documentation = "DIV.SCK_DIV[15:0]")

  private val ctrl_en = ctrl_en_reg
  private val core_rst = ctrl_soft_rst_reg
  private val sck_div_cfg = sck_div_cfg_reg

  // --------------------------------------------------------------------------
  // I2S clock / WS generation
  // --------------------------------------------------------------------------

  private val sck_div_cnt_reg = Reg(UInt(16 bits)) init (0)
  private val sck_reg = Reg(Bool()) init (False)
  private val sck_reg_d = Reg(Bool()) init (False)
  private val ws_reg = Reg(Bool()) init (False)
  private val bit_cnt_reg = Reg(UInt(6 bits)) init (0) // 0..63

  private val sck_rise = (!sck_reg_d) && sck_reg
  private val sck_fall = sck_reg_d && (!sck_reg)

  when(core_rst || !ctrl_en) {
    sck_div_cnt_reg := 0
    sck_reg := False
    sck_reg_d := False
    ws_reg := False
    bit_cnt_reg := 0
  } otherwise {
    sck_reg_d := sck_reg

    when(sck_div_cnt_reg >= sck_div_cfg) {
      sck_div_cnt_reg := 0
      sck_reg := ~sck_reg
    } otherwise {
      sck_div_cnt_reg := sck_div_cnt_reg + 1
    }

    when(sck_rise) {
      when(bit_cnt_reg === U(63, 6 bits)) {
        bit_cnt_reg := 0
      } otherwise {
        bit_cnt_reg := bit_cnt_reg + 1
      }
    }

    when(sck_fall) {
      when(bit_cnt_reg === U(31, 6 bits)) {
        ws_reg := True
      } elsewhen (bit_cnt_reg === U(63, 6 bits)) {
        ws_reg := False
      }
    }
  }

  io.mic_sck := sck_reg
  io.mic_ws := ws_reg

  // --------------------------------------------------------------------------
  // I2S sample capture (left channel only, 24-bit)
  // --------------------------------------------------------------------------

  private val sample_shift_reg = Reg(Bits(24 bits)) init (0)
  private val sample_latched_reg = Reg(Bits(24 bits)) init (0)
  private val sample_push_reg = Reg(Bool()) init (False)

  sample_push_reg := False
  when(core_rst) {
    sample_shift_reg := 0
    sample_latched_reg := 0
    sample_push_reg := False
  } elsewhen (ctrl_en && sck_rise) {
    when(!ws_reg) {
      when(bit_cnt_reg >= U(1, 6 bits) && bit_cnt_reg <= U(24, 6 bits)) {
        val shift_next = sample_shift_reg(22 downto 0) ## io.mic_sd.asBits
        sample_shift_reg := shift_next
        when(bit_cnt_reg === U(24, 6 bits)) {
          sample_latched_reg := shift_next
          sample_push_reg := True
        }
      }
    }
  }

  private val sample_16bit = sample_latched_reg(23 downto 8)

  // --------------------------------------------------------------------------
  // FIFO: pack 2x int16 samples into 1x 32-bit word (old in low16, new in high16)
  // --------------------------------------------------------------------------

  private val sample_buffer_reg = Reg(Bits(16 bits)) init (0)
  private val pack_cnt_reg = Reg(Bool()) init (False)

  when(core_rst) {
    sample_buffer_reg := 0
    pack_cnt_reg := False
  } otherwise {
    when(sample_push_reg) {
      when(!pack_cnt_reg) {
        sample_buffer_reg := sample_16bit
        pack_cnt_reg := True
      } otherwise {
        pack_cnt_reg := False
      }
    }
  }

  private val fifo_wr_en = sample_push_reg && pack_cnt_reg
  private val fifo_wr_data = (sample_16bit ## sample_buffer_reg).asBits

  private val fifo = new StreamFifo(
    dataType = HardType(Bits(32 bits)),
    depth = fifoDepth,
    withAsyncRead = true,
    withBypass = false
  )
  fifo.setName("fifo")
  fifo.io.flush := core_rst

  // depth 较小时，强制用寄存器实现，避免综合推到 BRAM（节约 BRAM 资源）
  // private val SMALL_FIFO_MAX_DEPTH = 32
  // if (fifoDepth <= SMALL_FIFO_MAX_DEPTH) {
  //   fifo.logic.ram.addAttribute("syn_ramstyle", "registers")
  // }

  fifo.io.push.valid := fifo_wr_en
  fifo.io.push.payload := fifo_wr_data

  private val status_ovf_reg = Reg(Bool()) init (False)
  when(core_rst) {
    status_ovf_reg := False
  } otherwise {
    when(ctrl_clr_ovf_reg) {
      status_ovf_reg := False
    }
    when(fifo_wr_en && !fifo.io.push.ready) {
      status_ovf_reg := True
    }
  }

  // --------------------------------------------------------------------------
  // STATUS / DATA readout
  // --------------------------------------------------------------------------

  private val fifo_empty = !fifo.io.pop.valid
  private val fifo_full = fifo.io.occupancy === U(fifoDepth, fifo.io.occupancy.getWidth bits)
  private val fifo_level_words = fifo.io.occupancy.resize(16)

  bus_ctrl.read(fifo_empty, address = 0x08, bitOffset = 0, documentation = "STATUS.EMPTY")
  bus_ctrl.read(fifo_full, address = 0x08, bitOffset = 1, documentation = "STATUS.FULL")
  bus_ctrl.read(status_ovf_reg, address = 0x08, bitOffset = 2, documentation = "STATUS.OVF")
  bus_ctrl.read(fifo_level_words, address = 0x08, bitOffset = 8, documentation = "STATUS.LEVEL[23:8] (words)")

  private val data_rdata = Bits(32 bits)
  data_rdata := fifo_empty ? B(0, 32 bits) | fifo.io.pop.payload

  fifo.io.pop.ready := False
  bus_ctrl.onRead(0x0C, documentation = "DATA read pops FIFO word") {
    when(!fifo_empty) {
      fifo.io.pop.ready := True
    }
  }
  bus_ctrl.read(data_rdata, address = 0x0C, bitOffset = 0, documentation = "DATA[31:0]")
}
