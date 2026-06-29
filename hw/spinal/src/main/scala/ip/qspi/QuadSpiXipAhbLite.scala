package ip.qspi

import other.{QuadSpiXipAhb, QuadSpiXipAhbConfig}
import spinal.core._
import spinal.lib._
import spinal.lib.bus.amba3.ahblite._

/**
 * QuadSpiXipConfig / QuadSpiXipAhbLite
 * -----------------------------------
 * 说明：
 * - 该模块对齐旧的 `ip.qspi.QuadSpiXipAhbLite` API（供 demo/top 使用）；
 * - 内部复用 `other.QuadSpiXipAhb` 作为实现。
 *
 * 注意：
 * - 本模块的 AHB 端口使用 24-bit 地址宽度（典型 XIP 窗口内地址），并在内部扩展到 32-bit。
 */
case class QuadSpiXipConfig(
                             flashBase: Int = 0x400000, // flash 内偏移（byte），最终 flash_addr = flashBase + HADDR_aligned
                             sclkDiv: Int = 1, // SCLK = sysclk / (2*(sclkDiv+1))
                             fifoDepth: Int = 8,
                             defaultPrefetchWords: Int = 4,
                             seqAheadWords: Int = 16,
                             initRstWaitCycles: Int = 5000,
                             quadIoRead: Boolean = true
                           ) {
  def toCoreCfg(ahbCfg: AhbLite3Config): QuadSpiXipAhbConfig = {
    QuadSpiXipAhbConfig(
      ahb_cfg = ahbCfg,
      sclk_div = sclkDiv,
      fifo_depth = fifoDepth,
      default_prefetch_words = defaultPrefetchWords,
      seq_ahead_words = seqAheadWords,
      init_rst_wait_cycles = initRstWaitCycles,
      flash_offset_bytes = BigInt(flashBase),
      quad_io_read = quadIoRead
    )
  }
}

case class QuadSpiPort() extends Bundle {
  val qspi_cs_n = out Bool()
  val qspi_sclk = out Bool()
  val qspi_dq_i = in Bits (4 bits)
  val qspi_dq_o = out Bits (4 bits)
  val qspi_dq_oe = out Bits (4 bits)
}

class QuadSpiXipAhbLite(cfg: QuadSpiXipConfig) extends Component {
  private val ahb_cfg_24 = AhbLite3Config(addressWidth = 24, dataWidth = 32)
  private val ahb_cfg_32 = AhbLite3Config(addressWidth = 32, dataWidth = 32)

  val io = new Bundle {
    val ahb = slave(AhbLite3(ahb_cfg_24))
    val qspi = QuadSpiPort()
  }
  noIoPrefix()

  private val core = QuadSpiXipAhb(cfg.toCoreCfg(ahb_cfg_32))
  core.setName("qspi_xip_core")

  // AHB mapping (extend 24-bit address to 32-bit)
  core.io.ahb.HSEL := io.ahb.HSEL
  core.io.ahb.HADDR := (U(0, 8 bits) @@ io.ahb.HADDR).resized
  core.io.ahb.HWRITE := io.ahb.HWRITE
  core.io.ahb.HSIZE := io.ahb.HSIZE
  core.io.ahb.HBURST := io.ahb.HBURST
  core.io.ahb.HPROT := io.ahb.HPROT
  core.io.ahb.HTRANS := io.ahb.HTRANS
  core.io.ahb.HMASTLOCK := io.ahb.HMASTLOCK
  core.io.ahb.HWDATA := io.ahb.HWDATA
  core.io.ahb.HREADY := io.ahb.HREADY

  io.ahb.HRDATA := core.io.ahb.HRDATA
  io.ahb.HREADYOUT := core.io.ahb.HREADYOUT
  io.ahb.HRESP := core.io.ahb.HRESP

  // QSPI pins
  io.qspi.qspi_cs_n := core.io.qspi_cs_n
  io.qspi.qspi_sclk := core.io.qspi_sclk
  io.qspi.qspi_dq_o := core.io.qspi_dq_o
  io.qspi.qspi_dq_oe := core.io.qspi_dq_oe
  core.io.qspi_dq_i := io.qspi.qspi_dq_i
}
